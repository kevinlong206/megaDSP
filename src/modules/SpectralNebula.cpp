#include "SpectralNebula.h"
#include "DspHelpers.h"
#include "DspSafety.h"

#include <algorithm>
#include <cmath>

namespace megadsp
{
namespace
{
constexpr auto twoPi = juce::MathConstants<float>::twoPi;

float principalPhase(float phase) noexcept
{
    return std::remainder(phase, twoPi);
}

float bandTaper(float frequency, float lowCut, float highCut) noexcept
{
    const auto lowStart = juce::jmax(10.0f, lowCut * 0.72f);
    const auto highEnd = highCut * 1.28f;
    const auto lowGain = juce::jlimit(
        0.0f, 1.0f, (frequency - lowStart)
            / juce::jmax(1.0f, lowCut - lowStart));
    const auto highGain = juce::jlimit(
        0.0f, 1.0f, (highEnd - frequency)
            / juce::jmax(1.0f, highEnd - highCut));
    return lowGain * highGain;
}
} // namespace

void SpectralNebulaModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    stft.prepare(spec);
    sampleRate = stft.sampleRate();
    const auto bins = static_cast<std::size_t>(FixedLatencyStft::binCount);
    for (int domain = 0; domain < 2; ++domain)
    {
        inputMagnitude[static_cast<std::size_t>(domain)].resize(bins);
        cloudMagnitude[static_cast<std::size_t>(domain)].resize(bins);
        frameMagnitude[static_cast<std::size_t>(domain)].resize(bins);
        cloudTonality[static_cast<std::size_t>(domain)].resize(bins);
        outputPhase[static_cast<std::size_t>(domain)].resize(bins);
        cloudRelativePhase[static_cast<std::size_t>(domain)].resize(bins);
        previousInputPhase[static_cast<std::size_t>(domain)].resize(bins);
        cloudPhaseAdvance[static_cast<std::size_t>(domain)].resize(bins);
        inputPhaseConfidence[static_cast<std::size_t>(domain)].resize(bins);
        cloudPeakOwner[static_cast<std::size_t>(domain)].resize(bins);
        inputDomain[static_cast<std::size_t>(domain)].resize(bins);
    }
    drySmoothed.reset(sampleRate, 0.04);
    cloudSmoothed.reset(sampleRate, 0.04);
    outputSmoothed.reset(sampleRate, 0.04);
    reset();
}

void SpectralNebulaModule::reset()
{
    stft.reset();
    for (auto& values : inputMagnitude)
        std::fill(values.begin(), values.end(), 0.0f);
    for (auto& values : cloudMagnitude)
        std::fill(values.begin(), values.end(), 0.0f);
    for (auto& values : frameMagnitude)
        std::fill(values.begin(), values.end(), 0.0f);
    for (auto& values : cloudTonality)
        std::fill(values.begin(), values.end(), 0.0f);
    for (auto& values : outputPhase)
        std::fill(values.begin(), values.end(), 0.0f);
    for (auto& values : cloudRelativePhase)
        std::fill(values.begin(), values.end(), 0.0f);
    for (auto& values : previousInputPhase)
        std::fill(values.begin(), values.end(), 0.0f);
    for (auto& values : cloudPhaseAdvance)
        std::fill(values.begin(), values.end(), 0.0f);
    for (auto& values : inputPhaseConfidence)
        std::fill(values.begin(), values.end(), 0.0f);
    for (auto& values : cloudPeakOwner)
        for (std::size_t bin = 0; bin < values.size(); ++bin)
            values[bin] = static_cast<int>(bin);
    for (auto& values : inputDomain)
        std::fill(values.begin(), values.end(), Complex {});
    drySmoothed.setCurrentAndTargetValue(1.0f);
    cloudSmoothed.setCurrentAndTargetValue(0.55f);
    outputSmoothed.setCurrentAndTargetValue(1.0f);
    driftPhase = 0.0;
    measuredInputEnergy = 0.0f;
    measuredCloudEnergy = 0.0f;
    currentDriftCents = 0.0f;
    bandEnergy.fill(0.0f);
    freeze = false;
    parametersInitialised = false;
    telemetrySequence = 0;
    telemetryState = {};
    telemetry.clear();
    wetMeter.store(0.0f, std::memory_order_relaxed);
}

void SpectralNebulaModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    smear = detail::normalizedControl(controls[smearControl], 0.72f);
    decaySeconds = detail::exponential(
        0.2f, 30.0f,
        detail::normalizedControl(controls[decayControl], 0.68f));
    driftDepthCents =
        detail::normalizedControl(controls[driftControl], 0.25f) * 40.0f;
    shimmer =
        detail::normalizedControl(controls[shimmerControl], 0.18f);
    focus = detail::lerp(
        -1.0f, 1.0f,
        detail::normalizedControl(controls[focusControl], 0.5f));
    freeze =
        detail::normalizedControl(controls[freezeControl], 0.0f) >= 0.5f;
    width = detail::normalizedControl(controls[widthControl], 0.65f);
    lowCutHz = detail::exponential(
        20.0f, 1000.0f,
        detail::normalizedControl(controls[lowCutControl], 0.35f));
    highCutHz = detail::exponential(
        1000.0f, 20000.0f,
        detail::normalizedControl(controls[highCutControl], 0.83f));
    const auto dry =
        detail::normalizedControl(controls[dryControl], 1.0f);
    const auto cloud =
        detail::normalizedControl(controls[cloudControl], 0.55f);
    const auto output = juce::Decibels::decibelsToGain(detail::lerp(
        -18.0f, 12.0f,
        detail::normalizedControl(controls[outputControl], 0.6f)));
    if (!parametersInitialised)
    {
        drySmoothed.setCurrentAndTargetValue(dry);
        cloudSmoothed.setCurrentAndTargetValue(cloud);
        outputSmoothed.setCurrentAndTargetValue(output);
        smoothedWidth = width;
        smoothedLowCutHz = lowCutHz;
        smoothedHighCutHz = highCutHz;
        parametersInitialised = true;
    }
    else
    {
        drySmoothed.setTargetValue(dry);
        cloudSmoothed.setTargetValue(cloud);
        outputSmoothed.setTargetValue(output);
    }

    stft.process(buffer, this, processFrameCallback, outputCallback);
    publishTelemetry(environment.captureTelemetry);
}

double SpectralNebulaModule::tailSeconds(
    const ControlValues& controls) const
{
    if (detail::normalizedControl(controls[freezeControl], 0.0f) >= 0.5f)
        return 120.0;
    const auto decay = detail::exponential(
        0.2f, 30.0f,
        detail::normalizedControl(controls[decayControl], 0.68f));
    return juce::jmin(
        120.0,
        static_cast<double>(decay * 6.0f)
            + static_cast<double>(FixedLatencyStft::fftSize) / sampleRate);
}

void SpectralNebulaModule::processFrameCallback(
    void* context, Complex* const* spectra,
    int channels, int bins) noexcept
{
    static_cast<SpectralNebulaModule*>(context)->processFrame(
        spectra, channels, bins);
}

float SpectralNebulaModule::outputCallback(
    void* context, int channel, float input, float, float wet) noexcept
{
    auto& module = *static_cast<SpectralNebulaModule*>(context);
    if (channel == 0)
    {
        module.currentDry = module.drySmoothed.getNextValue();
        module.currentCloud = module.cloudSmoothed.getNextValue();
        module.currentOutput = module.outputSmoothed.getNextValue();
    }
    const auto boundedWet = std::tanh(wet * 1.2f) / 1.2f;
    return detail::finiteSample(
        (input * module.currentDry + boundedWet * module.currentCloud)
        * module.currentOutput);
}

void SpectralNebulaModule::processFrame(
    Complex* const* spectra, int channels, int bins) noexcept
{
    const auto stereo = channels > 1;
    const auto binHz =
        static_cast<float>(sampleRate / FixedLatencyStft::fftSize);
    double inputPower = 0.0;
    for (int bin = 0; bin < bins; ++bin)
    {
        const auto left = safeComplex(spectra[0][bin]);
        const auto right = stereo ? safeComplex(spectra[1][bin]) : left;
        inputDomain[0][static_cast<std::size_t>(bin)] =
            stereo ? 0.5f * (left + right) : left;
        inputDomain[1][static_cast<std::size_t>(bin)] =
            stereo ? 0.5f * (left - right) : Complex {};
        for (int domain = 0; domain < 2; ++domain)
        {
            const auto magnitude = std::abs(
                inputDomain[static_cast<std::size_t>(domain)]
                           [static_cast<std::size_t>(bin)]);
            inputMagnitude[static_cast<std::size_t>(domain)]
                          [static_cast<std::size_t>(bin)] =
                std::isfinite(magnitude) ? magnitude : 0.0f;
            inputPower += static_cast<double>(magnitude) * magnitude;
        }
    }
    measuredInputEnergy = static_cast<float>(std::sqrt(
        inputPower / static_cast<double>(juce::jmax(1, bins * 2))))
        / static_cast<float>(FixedLatencyStft::fftSize);

    const auto frameSeconds =
        static_cast<float>(FixedLatencyStft::hopSize / sampleRate);
    const auto captureAttack = std::exp(
        -frameSeconds / detail::lerp(0.025f, 0.14f, smear));
    const auto replacementRelease = std::exp(
        -frameSeconds / detail::lerp(0.12f, 0.68f, smear));
    const auto spectralSmoothing = std::exp(-frameSeconds / 0.06f);
    smoothedWidth = detail::lerp(
        width, smoothedWidth, spectralSmoothing);
    smoothedLowCutHz = detail::lerp(
        lowCutHz, smoothedLowCutHz, spectralSmoothing);
    smoothedHighCutHz = detail::lerp(
        highCutHz, smoothedHighCutHz, spectralSmoothing);
    constexpr auto magnitudeFloor = 1.0e-6f;
    for (int domain = 0; domain < 2; ++domain)
    {
        auto& input = inputMagnitude[static_cast<std::size_t>(domain)];
        auto& cloud = cloudMagnitude[static_cast<std::size_t>(domain)];
        auto& frame = frameMagnitude[static_cast<std::size_t>(domain)];
        auto& tonality = cloudTonality[static_cast<std::size_t>(domain)];
        auto& relativePhase =
            cloudRelativePhase[static_cast<std::size_t>(domain)];
        auto& peakOwner =
            cloudPeakOwner[static_cast<std::size_t>(domain)];
        auto& previousPhase =
            previousInputPhase[static_cast<std::size_t>(domain)];
        auto& phaseAdvance =
            cloudPhaseAdvance[static_cast<std::size_t>(domain)];
        auto& phaseConfidence =
            inputPhaseConfidence[static_cast<std::size_t>(domain)];
        const auto domainPeak = *std::max_element(
            input.begin(), input.begin() + bins);
        const auto domainActive = domainPeak > magnitudeFloor;
        for (int bin = 0; bin < bins; ++bin)
        {
            const auto index = static_cast<std::size_t>(bin);
            const auto centre = input[index];
            auto owner = bin;
            auto ownerMagnitude = centre;
            const auto currentIsPeak = bin > 0 && bin < bins - 1
                && centre >= input[static_cast<std::size_t>(bin - 1)]
                && centre > input[static_cast<std::size_t>(bin + 1)];
            if (!currentIsPeak)
            {
                const auto searchStart = juce::jmax(1, bin - 3);
                const auto searchEnd = juce::jmin(bins - 2, bin + 3);
                for (int candidate = searchStart;
                     candidate <= searchEnd; ++candidate)
                {
                    const auto candidateMagnitude =
                        input[static_cast<std::size_t>(candidate)];
                    if (candidateMagnitude >= input[static_cast<std::size_t>(
                            candidate - 1)]
                        && candidateMagnitude > input[static_cast<std::size_t>(
                            candidate + 1)]
                        && candidateMagnitude > ownerMagnitude)
                    {
                        owner = candidate;
                        ownerMagnitude = candidateMagnitude;
                    }
                }
            }
            auto localMean = 0.0f;
            auto localCount = 0;
            for (int neighbour = juce::jmax(0, owner - 5);
                 neighbour <= juce::jmin(bins - 1, owner + 5);
                 ++neighbour)
            {
                localMean += input[static_cast<std::size_t>(neighbour)];
                ++localCount;
            }
            localMean /= static_cast<float>(juce::jmax(1, localCount));
            const auto spectralContrast = ownerMagnitude
                / juce::jmax(magnitudeFloor, localMean);
            const auto inputTonality = juce::jlimit(
                0.0f, 1.0f, (spectralContrast - 1.35f) / 3.0f);
            const auto balancedMask =
                0.14f + 0.86f * inputTonality;
            const auto focusGain = focus >= 0.0f
                ? detail::lerp(
                    balancedMask,
                    0.025f + 0.975f * inputTonality, focus)
                : detail::lerp(
                    balancedMask,
                    0.10f + 0.90f * (1.0f - inputTonality), -focus);
            const auto target = centre * focusGain;
            const auto expectedAdvance = twoPi * static_cast<float>(bin)
                * static_cast<float>(FixedLatencyStft::hopSize)
                / static_cast<float>(FixedLatencyStft::fftSize);
            const auto inputPhase = std::arg(
                inputDomain[static_cast<std::size_t>(domain)][index]);
            auto instantaneousAdvance = expectedAdvance;
            if (centre > 1.0e-6f)
            {
                if (phaseConfidence[index] > 0.5f)
                    instantaneousAdvance = expectedAdvance + principalPhase(
                        inputPhase - previousPhase[index] - expectedAdvance);
                previousPhase[index] = inputPhase;
                phaseConfidence[index] = 1.0f;
            }
            else
            {
                phaseConfidence[index] = 0.0f;
            }
            if (!freeze)
            {
                const auto previousMagnitude = cloud[index];
                if (domainActive)
                {
                    const auto coefficient = target > previousMagnitude
                        ? captureAttack : replacementRelease;
                    cloud[index] = detail::lerp(
                        target, previousMagnitude, coefficient);
                    tonality[index] = detail::lerp(
                        inputTonality, tonality[index], coefficient);
                    if (target > magnitudeFloor)
                    {
                        const auto captureStrength = target
                            / juce::jmax(
                                magnitudeFloor,
                                target + previousMagnitude);
                        if (previousMagnitude <= magnitudeFloor)
                        {
                            phaseAdvance[index] = instantaneousAdvance;
                            outputPhase[static_cast<std::size_t>(domain)]
                                       [index] = principalPhase(
                                inputPhase - instantaneousAdvance);
                        }
                        else
                        {
                            phaseAdvance[index] = detail::lerp(
                                phaseAdvance[index], instantaneousAdvance,
                                0.16f + 0.79f * captureStrength);
                        }
                        peakOwner[index] = owner;
                        const auto ownerPhase = std::arg(
                            inputDomain[static_cast<std::size_t>(domain)]
                                       [static_cast<std::size_t>(owner)]);
                        relativePhase[index] = principalPhase(
                            inputPhase - ownerPhase);
                    }
                }
                else
                {
                    const auto noiseTailScale = focus < 0.0f
                        ? detail::lerp(0.12f, 1.0f, -focus) : 0.12f;
                    const auto binDecaySeconds = decaySeconds * detail::lerp(
                        noiseTailScale, 1.0f, tonality[index]);
                    cloud[index] *= std::exp(
                        -frameSeconds / juce::jmax(
                            0.02f, binDecaySeconds));
                }
                if (cloud[index] < 1.0e-8f)
                {
                    cloud[index] = 0.0f;
                    tonality[index] = 0.0f;
                    peakOwner[index] = bin;
                    relativePhase[index] = 0.0f;
                }
            }
            frame[index] = cloud[index];
        }
    }

    const auto driftRate = 0.055f;
    driftPhase += twoPi * driftRate * frameSeconds;
    if (driftPhase >= twoPi)
        driftPhase -= twoPi;
    currentDriftCents = driftDepthCents * (
        0.72f * std::sin(static_cast<float>(driftPhase))
        + 0.28f * std::sin(
            static_cast<float>(driftPhase * 1.731 + 0.83)));
    const auto pitchRatio =
        std::pow(2.0f, currentDriftCents / 1200.0f);
    const auto sideGain = smoothedWidth * 2.0f;
    for (int domain = 0; domain < 2; ++domain)
    {
        auto& phases = outputPhase[static_cast<std::size_t>(domain)];
        const auto& advances =
            cloudPhaseAdvance[static_cast<std::size_t>(domain)];
        const auto& owners =
            cloudPeakOwner[static_cast<std::size_t>(domain)];
        const auto& magnitudes =
            frameMagnitude[static_cast<std::size_t>(domain)];
        for (int bin = 0; bin < bins; ++bin)
        {
            const auto index = static_cast<std::size_t>(bin);
            const auto candidate = juce::jlimit(0, bins - 1, owners[index]);
            const auto owner = owners[static_cast<std::size_t>(candidate)]
                    == candidate
                && magnitudes[static_cast<std::size_t>(candidate)] > 0.0f
                ? candidate : bin;
            if (owner == bin)
                phases[index] = principalPhase(
                    phases[index] + advances[index] * pitchRatio);
        }
    }
    double cloudPower = 0.0;
    bandEnergy.fill(0.0f);
    std::array<int, 3> bandCounts {};

    for (int bin = 0; bin < bins; ++bin)
    {
        const auto frequency = static_cast<float>(bin) * binHz;
        const auto taper = bandTaper(
            frequency, smoothedLowCutHz, smoothedHighCutHz);
        std::array<Complex, 2> outputDomains {};
        for (int domain = 0; domain < 2; ++domain)
        {
            auto& phases = outputPhase[static_cast<std::size_t>(domain)];
            const auto& source =
                frameMagnitude[static_cast<std::size_t>(domain)];
            const auto& tonalities =
                cloudTonality[static_cast<std::size_t>(domain)];
            const auto& owners =
                cloudPeakOwner[static_cast<std::size_t>(domain)];
            const auto index = static_cast<std::size_t>(bin);
            const auto candidate = juce::jlimit(0, bins - 1, owners[index]);
            const auto owner = owners[static_cast<std::size_t>(candidate)]
                    == candidate
                && source[static_cast<std::size_t>(candidate)] > 0.0f
                ? candidate : bin;
            const auto phase = owner == bin
                ? phases[index]
                : principalPhase(
                    phases[static_cast<std::size_t>(owner)]
                    + cloudRelativePhase[static_cast<std::size_t>(domain)]
                                        [index]);
            phases[index] = phase;
            auto cloudValue = polar(source[index] * taper, phase);
            if ((bin & 1) == 0)
            {
                const auto sourceBin = bin / 2;
                const auto sourceIndex =
                    static_cast<std::size_t>(sourceBin);
                const auto sourceIsPeak =
                    owners[sourceIndex] == sourceBin;
                const auto sourceFrequency =
                    static_cast<float>(sourceBin) * binHz;
                if (sourceIsPeak && sourceFrequency >= 150.0f)
                {
                    const auto tonalAmount = juce::jlimit(
                        0.0f, 1.0f,
                        (tonalities[sourceIndex] - 0.30f) / 0.70f);
                    const auto airRolloff =
                        1.0f / (1.0f + frequency / 7000.0f);
                    cloudValue += polar(
                        shimmer * 0.24f * tonalAmount
                            * source[sourceIndex] * taper * airRolloff,
                        principalPhase(2.0f * phases[sourceIndex]));
                }
            }
            outputDomains[static_cast<std::size_t>(domain)] =
                safeComplex(cloudValue);
        }
        outputDomains[1] *= sideGain;
        spectra[0][bin] = safeComplex(
            outputDomains[0] + outputDomains[1]);
        if (stereo)
            spectra[1][bin] = safeComplex(
                outputDomains[0] - outputDomains[1]);

        const auto magnitude = std::abs(outputDomains[0]);
        cloudPower += static_cast<double>(magnitude) * magnitude;
        const auto band = frequency < 300.0f ? 0
            : (frequency < 3000.0f ? 1 : 2);
        bandEnergy[static_cast<std::size_t>(band)] +=
            magnitude * magnitude;
        ++bandCounts[static_cast<std::size_t>(band)];
    }
    measuredCloudEnergy = static_cast<float>(std::sqrt(
        cloudPower / static_cast<double>(juce::jmax(1, bins))))
        / static_cast<float>(FixedLatencyStft::fftSize);
    for (std::size_t band = 0; band < bandEnergy.size(); ++band)
        bandEnergy[band] = std::sqrt(
            bandEnergy[band]
            / static_cast<float>(juce::jmax(
                1, bandCounts[band])))
            / static_cast<float>(FixedLatencyStft::fftSize);
    wetMeter.store(
        juce::jlimit(0.0f, 1.0f, measuredCloudEnergy * 4.0f),
        std::memory_order_relaxed);
}

void SpectralNebulaModule::publishTelemetry(bool capture) noexcept
{
    if (!capture)
        return;
    telemetryState.sequence = ++telemetrySequence;
    telemetryState.valueCount = telemetryValueCount;
    telemetryState.values[inputEnergy] = measuredInputEnergy;
    telemetryState.values[cloudEnergy] = measuredCloudEnergy;
    telemetryState.values[lowEnergy] = bandEnergy[0];
    telemetryState.values[midEnergy] = bandEnergy[1];
    telemetryState.values[highEnergy] = bandEnergy[2];
    telemetryState.values[actualDriftCents] = currentDriftCents;
    telemetryState.values[frozen] = freeze ? 1.0f : 0.0f;
    telemetryState.values[focusAmount] = focus;
    appendContinuousTelemetryHistory(
        telemetryState,
        { bandEnergy[0], bandEnergy[1], bandEnergy[2],
          measuredCloudEnergy },
        telemetryHistoryValueCount);
    telemetry.publish(telemetryState);
}

bool SpectralNebulaModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}

SpectralNebulaModule::Complex SpectralNebulaModule::safeComplex(
    Complex value) noexcept
{
    if (!std::isfinite(value.real()) || !std::isfinite(value.imag()))
        return {};
    constexpr auto limit = 1.0e6f;
    return {
        juce::jlimit(-limit, limit, value.real()),
        juce::jlimit(-limit, limit, value.imag())
    };
}

SpectralNebulaModule::Complex SpectralNebulaModule::polar(
    float magnitude, float phase) noexcept
{
    const auto safeMagnitude = juce::jlimit(
        0.0f, 1.0e6f, detail::finiteSample(magnitude));
    return {
        safeMagnitude * std::cos(phase),
        safeMagnitude * std::sin(phase)
    };
}
} // namespace megadsp
