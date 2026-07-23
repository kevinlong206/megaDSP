#include "SpectralDelayCanvas.h"
#include "DspHelpers.h"
#include "DspSafety.h"

#include <algorithm>
#include <cmath>

namespace megadsp
{
namespace
{
constexpr std::array<float, 7> beatDivisions {
    0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f
};

FixedLatencyStft::Complex safeComplex(
    FixedLatencyStft::Complex value) noexcept
{
    if (!std::isfinite(value.real()) || !std::isfinite(value.imag()))
        return {};
    constexpr float limit = 1.0e6f;
    return {
        juce::jlimit(-limit, limit, value.real()),
        juce::jlimit(-limit, limit, value.imag())
    };
}
} // namespace

void SpectralDelayCanvasModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    stft.prepare(spec);
    sampleRate = stft.sampleRate();
    const auto bins = static_cast<std::size_t>(FixedLatencyStft::binCount);
    const auto capacity = static_cast<std::size_t>(std::ceil(
        maximumDelaySeconds * sampleRate
        / static_cast<double>(FixedLatencyStft::hopSize))) + 2;
    for (int channel = 0; channel < FixedLatencyStft::maxChannels; ++channel)
    {
        history[static_cast<std::size_t>(channel)].prepare(capacity, bins * 2);
        writeScratch[static_cast<std::size_t>(channel)].resize(bins * 2);
        rendered[static_cast<std::size_t>(channel)].resize(bins);
        previousWet[static_cast<std::size_t>(channel)].resize(bins);
    }
    delaySmoother.prepare(bins);
    mixSmoothed.reset(sampleRate, 0.025);
    outputSmoothed.reset(sampleRate, 0.025);
    reset();
}

void SpectralDelayCanvasModule::reset()
{
    stft.reset();
    for (auto& channel : history)
        channel.reset();
    for (auto& channel : writeScratch)
        std::fill(channel.begin(), channel.end(), 0.0f);
    for (auto& channel : rendered)
        std::fill(channel.begin(), channel.end(), FixedLatencyStft::Complex {});
    for (auto& channel : previousWet)
        std::fill(channel.begin(), channel.end(), FixedLatencyStft::Complex {});
    delaySmoother.reset();
    mixSmoothed.setCurrentAndTargetValue(1.0f);
    outputSmoothed.setCurrentAndTargetValue(1.0f);
    anchorDelaySeconds.fill(0.0f);
    bandEnergy.fill(0.0f);
    feedback = 0.0f;
    diffusion = 0.0f;
    stereoSpread = 0.0f;
    currentMix = 1.0f;
    currentOutput = 1.0f;
    freeze = false;
    previousFreeze = false;
    delayStateInitialised = false;
    parametersInitialised = false;
    continuousSequence = 0;
    continuousState = {};
    continuousTelemetry.clear();
    wetMeter.store(0.0f, std::memory_order_relaxed);
    historyMeter.store(-100.0f, std::memory_order_relaxed);
}

void SpectralDelayCanvasModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    const auto sync =
        detail::normalizedControl(controls[syncControl], 0.0f) >= 0.5f;
    const auto baseSeconds = detail::exponential(
        0.010f, 4.0f,
        detail::normalizedControl(controls[baseTimeControl], 0.45f));
    const auto divisionIndex = juce::jlimit(
        0, static_cast<int>(beatDivisions.size()) - 1,
        static_cast<int>(detail::normalizedControl(
            controls[divisionControl], 0.5f)
            * static_cast<float>(beatDivisions.size())));
    auto musicalSeconds = baseSeconds;
    if (sync && std::isfinite(environment.bpm)
        && environment.bpm >= 20.0 && environment.bpm <= 400.0)
        musicalSeconds = static_cast<float>(
            60.0 / environment.bpm
            * beatDivisions[static_cast<std::size_t>(divisionIndex)]);

    anchorDelaySeconds[0] = juce::jmin(
        maximumDelaySeconds, musicalSeconds * 2.0f
            * detail::normalizedControl(controls[lowDelayControl], 0.5f));
    anchorDelaySeconds[1] = juce::jmin(
        maximumDelaySeconds, musicalSeconds * 2.0f
            * detail::normalizedControl(controls[midDelayControl], 0.5f));
    anchorDelaySeconds[2] = juce::jmin(
        maximumDelaySeconds, musicalSeconds * 2.0f
            * detail::normalizedControl(controls[highDelayControl], 0.5f));
    feedback = 0.90f
        * detail::normalizedControl(controls[feedbackControl], 0.35f);
    diffusion = 0.7f
        * detail::normalizedControl(controls[diffusionControl], 0.25f);
    stereoSpread =
        detail::normalizedControl(controls[stereoSpreadControl], 0.5f);
    freeze =
        detail::normalizedControl(controls[freezeControl], 0.0f) >= 0.5f;
    const auto targetMix =
        detail::normalizedControl(controls[mixControl], 1.0f);
    const auto targetOutput = juce::Decibels::decibelsToGain(detail::lerp(
        -18.0f, 12.0f,
        detail::normalizedControl(controls[outputControl], 0.6f)));
    if (!parametersInitialised)
    {
        mixSmoothed.setCurrentAndTargetValue(targetMix);
        outputSmoothed.setCurrentAndTargetValue(targetOutput);
        parametersInitialised = true;
    }
    else
    {
        mixSmoothed.setTargetValue(targetMix);
        outputSmoothed.setTargetValue(targetOutput);
    }

    if (freeze != previousFreeze)
    {
        for (auto& channel : previousWet)
            std::fill(
                channel.begin(), channel.end(), FixedLatencyStft::Complex {});
        previousFreeze = freeze;
    }
    stft.process(buffer, this, processFrameCallback, outputCallback);
    publishTelemetry(environment.captureTelemetry);
}

void SpectralDelayCanvasModule::processFrameCallback(
    void* context, FixedLatencyStft::Complex* const* spectra,
    int channels, int bins) noexcept
{
    static_cast<SpectralDelayCanvasModule*>(context)->processFrame(
        spectra, channels, bins);
}

float SpectralDelayCanvasModule::outputCallback(
    void* context, int channel, float dry, float wet) noexcept
{
    auto& module = *static_cast<SpectralDelayCanvasModule*>(context);
    if (channel == 0)
    {
        module.currentMix = module.mixSmoothed.getNextValue();
        module.currentOutput = module.outputSmoothed.getNextValue();
    }
    const auto mixed = dry + module.currentMix * (wet - dry);
    return detail::finiteSample(mixed * module.currentOutput);
}

void SpectralDelayCanvasModule::processFrame(
    FixedLatencyStft::Complex* const* spectra, int channels, int bins) noexcept
{
    if (!freeze)
    {
        for (int channel = 0; channel < channels; ++channel)
        {
            auto& scratch = writeScratch[static_cast<std::size_t>(channel)];
            auto& feedbackSpectrum =
                previousWet[static_cast<std::size_t>(channel)];
            for (int bin = 0; bin < bins; ++bin)
            {
                const auto value = safeComplex(
                    safeComplex(spectra[channel][bin])
                    + feedback * feedbackSpectrum[static_cast<std::size_t>(bin)]);
                scratch[static_cast<std::size_t>(bin * 2)] = value.real();
                scratch[static_cast<std::size_t>(bin * 2 + 1)] = value.imag();
            }
            history[static_cast<std::size_t>(channel)].push(
                scratch.data(), scratch.size());
        }
    }

    const auto binHz =
        static_cast<float>(sampleRate / FixedLatencyStft::fftSize);
    bandEnergy.fill(0.0f);
    std::array<int, 3> bandCount {};
    float totalEnergy = 0.0f;
    for (int bin = 0; bin < bins; ++bin)
    {
        const auto frequency = static_cast<float>(bin) * binHz;
        const auto targetFrames = delayAt(frequency)
            * static_cast<float>(sampleRate)
            / static_cast<float>(FixedLatencyStft::hopSize);
        const auto delayFrames = delaySmoother.process(
            static_cast<std::size_t>(bin), targetFrames,
            delayStateInitialised ? 0.65f : 0.0f);
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto side = channels > 1
                ? (channel == 0 ? -1.0f : 1.0f) : 0.0f;
            const auto spreadFrames = side * stereoSpread
                * 0.12f * juce::jmax(1.0f, delayFrames);
            rendered[static_cast<std::size_t>(channel)]
                    [static_cast<std::size_t>(bin)] =
                readHistory(channel, bin, juce::jmax(
                    0.0f, delayFrames + spreadFrames));
        }
    }
    delayStateInitialised = true;

    for (int channel = 0; channel < channels; ++channel)
    {
        auto& wet = rendered[static_cast<std::size_t>(channel)];
        auto& feedbackSpectrum =
            previousWet[static_cast<std::size_t>(channel)];
        for (int bin = 0; bin < bins; ++bin)
        {
            auto value = wet[static_cast<std::size_t>(bin)];
            if (diffusion > 0.0f && bins > 2)
            {
                const auto low = wet[static_cast<std::size_t>(
                    juce::jmax(0, bin - 1))];
                const auto high = wet[static_cast<std::size_t>(
                    juce::jmin(bins - 1, bin + 1))];
                value = (1.0f - diffusion) * value
                    + 0.5f * diffusion * (low + high);
            }
            value = safeComplex(value);
            spectra[channel][bin] = value;
            feedbackSpectrum[static_cast<std::size_t>(bin)] = value;
            const auto power = std::norm(value)
                / static_cast<float>(
                    FixedLatencyStft::fftSize * FixedLatencyStft::fftSize);
            totalEnergy += power;
            if (channel == 0)
            {
                const auto frequency = static_cast<float>(bin) * binHz;
                const auto band = frequency < 300.0f ? 0
                    : (frequency < 3000.0f ? 1 : 2);
                bandEnergy[static_cast<std::size_t>(band)] += power;
                ++bandCount[static_cast<std::size_t>(band)];
            }
        }
    }
    for (std::size_t band = 0; band < bandEnergy.size(); ++band)
        bandEnergy[band] = std::sqrt(
            bandEnergy[band]
            / static_cast<float>(juce::jmax(1, bandCount[band])));

    wetMeter.store(
        std::sqrt(totalEnergy / static_cast<float>(
            juce::jmax(1, bins * channels))),
        std::memory_order_relaxed);
    const auto stored = history[0].size();
    const auto capacity = history[0].capacity();
    historyMeter.store(
        capacity > 0
            ? static_cast<float>(stored) / static_cast<float>(capacity)
            : 0.0f,
        std::memory_order_relaxed);

}

FixedLatencyStft::Complex SpectralDelayCanvasModule::readHistory(
    int channel, int bin, float framesAgo) const noexcept
{
    const auto& channelHistory = history[static_cast<std::size_t>(channel)];
    if (channelHistory.size() == 0)
        return {};
    framesAgo = juce::jlimit(
        0.0f, static_cast<float>(channelHistory.capacity() - 1), framesAgo);
    const auto firstAge = static_cast<std::size_t>(std::floor(framesAgo));
    const auto secondAge = firstAge + 1;
    const auto* first = channelHistory.frame(firstAge);
    if (first == nullptr)
        return {};
    const auto fraction = framesAgo - static_cast<float>(firstAge);
    const auto* second = channelHistory.frame(secondAge);
    const auto realIndex = static_cast<std::size_t>(bin * 2);
    const FixedLatencyStft::Complex firstValue {
        first[realIndex], first[realIndex + 1]
    };
    if (second == nullptr || fraction <= 0.0f)
        return safeComplex(firstValue);
    const FixedLatencyStft::Complex secondValue {
        second[realIndex], second[realIndex + 1]
    };
    return safeComplex(
        firstValue + fraction * (secondValue - firstValue));
}

float SpectralDelayCanvasModule::delayAt(float frequency) const noexcept
{
    if (frequency <= 250.0f)
        return anchorDelaySeconds[0];
    if (frequency < 2500.0f)
    {
        const auto amount = std::log(frequency / 250.0f)
            / std::log(2500.0f / 250.0f);
        return detail::lerp(
            anchorDelaySeconds[0], anchorDelaySeconds[1], amount);
    }
    const auto nyquist = juce::jmax(
        2501.0f, static_cast<float>(sampleRate * 0.5));
    const auto amount = juce::jlimit(
        0.0f, 1.0f,
        std::log(frequency / 2500.0f) / std::log(nyquist / 2500.0f));
    return detail::lerp(
        anchorDelaySeconds[1], anchorDelaySeconds[2], amount);
}

void SpectralDelayCanvasModule::publishTelemetry(bool capture) noexcept
{
    if (!capture)
        return;
    continuousState.sequence = ++continuousSequence;
    continuousState.valueCount = telemetryValueCount;
    continuousState.values = {
        bandEnergy[0], bandEnergy[1], bandEnergy[2],
        anchorDelaySeconds[0], anchorDelaySeconds[1], anchorDelaySeconds[2],
        feedback, freeze ? 1.0f : 0.0f
    };
    appendContinuousTelemetryHistory(
        continuousState,
        { bandEnergy[0], bandEnergy[1], bandEnergy[2],
          (anchorDelaySeconds[0] + anchorDelaySeconds[1]
           + anchorDelaySeconds[2]) / (3.0f * maximumDelaySeconds) },
        telemetryHistoryValueCount);
    continuousTelemetry.publish(continuousState);
}

bool SpectralDelayCanvasModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return continuousTelemetry.read(snapshot);
}

double SpectralDelayCanvasModule::tailSeconds(
    const ControlValues& controls) const
{
    const auto base = detail::exponential(
        0.010f, 4.0f,
        detail::normalizedControl(controls[baseTimeControl], 0.45f));
    const auto maximumAnchor = 2.0f * base * std::max({
        detail::normalizedControl(controls[lowDelayControl], 0.5f),
        detail::normalizedControl(controls[midDelayControl], 0.5f),
        detail::normalizedControl(controls[highDelayControl], 0.5f)
    });
    const auto synced =
        detail::normalizedControl(controls[syncControl], 0.0f) >= 0.5f;
    const auto longestDelay = synced
        ? maximumDelaySeconds
        : juce::jmin(maximumDelaySeconds, maximumAnchor);
    const auto gain = 0.90f
        * detail::normalizedControl(controls[feedbackControl], 0.35f);
    const auto repeats = gain > 0.001f
        ? std::log(0.001f) / std::log(gain) : 1.0f;
    return juce::jlimit(
        0.0, 30.0,
        static_cast<double>(longestDelay)
            * static_cast<double>(juce::jmax(1.0f, repeats))
            + static_cast<double>(FixedLatencyStft::fftSize) / sampleRate);
}
} // namespace megadsp
