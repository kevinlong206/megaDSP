#include "TimeMosaic.h"
#include "DspHelpers.h"
#include "DspSafety.h"

#include <algorithm>
#include <cmath>

namespace megadsp
{
namespace
{
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

void TimeMosaicModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    stft.prepare(spec);
    sampleRate = stft.sampleRate();
    const auto bins = static_cast<std::size_t>(FixedLatencyStft::binCount);
    const auto capacity = static_cast<std::size_t>(std::ceil(
        maximumHistorySeconds * sampleRate
        / static_cast<double>(FixedLatencyStft::hopSize))) + 2;
    for (int channel = 0; channel < FixedLatencyStft::maxChannels; ++channel)
    {
        history[static_cast<std::size_t>(channel)].prepare(capacity, bins * 2);
        writeScratch[static_cast<std::size_t>(channel)].resize(bins * 2);
    }
    ageStart.resize(bins);
    ageTarget.resize(bins);
    ageCurrent.resize(bins);
    ageProgress.resize(bins);
    pitchByBin.resize(bins);
    binEnergy.resize(bins);
    tileIdByBin.resize(bins);
    mixSmoothed.reset(sampleRate, 0.025);
    outputSmoothed.reset(sampleRate, 0.025);
    reset();
}

void TimeMosaicModule::reset()
{
    stft.reset();
    for (auto& channel : history)
        channel.reset();
    for (auto& channel : writeScratch)
        std::fill(channel.begin(), channel.end(), 0.0f);
    std::fill(ageStart.begin(), ageStart.end(), 0.0f);
    std::fill(ageTarget.begin(), ageTarget.end(), 0.0f);
    std::fill(ageCurrent.begin(), ageCurrent.end(), 0.0f);
    std::fill(ageProgress.begin(), ageProgress.end(), 1.0f);
    std::fill(pitchByBin.begin(), pitchByBin.end(), 0.0f);
    std::fill(binEnergy.begin(), binEnergy.end(), 0.0f);
    std::fill(tileIdByBin.begin(), tileIdByBin.end(), 0u);
    mixSmoothed.setCurrentAndTargetValue(1.0f);
    outputSmoothed.setCurrentAndTargetValue(1.0f);
    historySeconds = 2.0f;
    baseAge = 0.5f;
    motion = 0.5f;
    coherence = 0.75f;
    pitchDriftSemitones = 0.0f;
    stereoSpread = 0.5f;
    tileSeconds = 0.15f;
    tileWidthOctaves = 0.5f;
    currentMix = 1.0f;
    currentOutput = 1.0f;
    meanAgeSeconds = 0.0f;
    meanPitchSemitones = 0.0f;
    currentWetEnergy = 0.0f;
    bandMeanAge.fill(0.0f);
    tileBins = 8;
    activeHistoryFrames = 1;
    transitionFrames = 1;
    framesUntilAssignment = 0;
    freeze = false;
    previousFreeze = false;
    parametersInitialised = false;
    captureCurrentBlock = false;
    assignmentGeneration = 0;
    continuousSequence = 0;
    eventSequence = 0;
    eventPublicationSequence = 0;
    continuousState = {};
    eventWorking = {};
    continuousTelemetry.clear();
    eventTelemetry.clear();
    wetMeter.store(0.0f, std::memory_order_relaxed);
    ageMeter.store(0.0f, std::memory_order_relaxed);
}

void TimeMosaicModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    historySeconds = detail::exponential(
        0.25f, maximumHistorySeconds,
        detail::normalizedControl(controls[historyControl], 0.5f));
    const auto widthNormalized =
        detail::normalizedControl(controls[tileWidthControl], 0.35f);
    tileWidthOctaves = detail::exponential(
        1.0f / 24.0f, 3.0f, widthNormalized);
    tileSeconds = detail::exponential(
        0.010f, 0.5f,
        detail::normalizedControl(controls[tileTimeControl], 0.45f));
    baseAge = detail::normalizedControl(controls[ageControl], 0.5f);
    motion = detail::normalizedControl(controls[motionControl], 0.4f);
    coherence =
        detail::normalizedControl(controls[coherenceControl], 0.75f);
    pitchDriftSemitones = 0.5f
        * detail::normalizedControl(controls[pitchDriftControl], 0.0f);
    freeze =
        detail::normalizedControl(controls[freezeControl], 0.0f) >= 0.5f;
    stereoSpread =
        detail::normalizedControl(controls[stereoSpreadControl], 0.5f);
    const auto targetMix =
        detail::normalizedControl(controls[mixControl], 1.0f);
    const auto targetOutput = juce::Decibels::decibelsToGain(detail::lerp(
        -18.0f, 12.0f,
        detail::normalizedControl(controls[outputControl], 0.6f)));
    activeHistoryFrames = juce::jlimit(
        1, static_cast<int>(history[0].capacity()) - 1,
        static_cast<int>(std::ceil(
            historySeconds * static_cast<float>(sampleRate)
            / static_cast<float>(FixedLatencyStft::hopSize))));
    transitionFrames = juce::jmax(
        1, static_cast<int>(std::ceil(
            tileSeconds * static_cast<float>(sampleRate)
            / static_cast<float>(FixedLatencyStft::hopSize))));
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

    captureCurrentBlock = environment.captureTelemetry;
    eventWorking = {};
    if (freeze != previousFreeze)
    {
        if (captureCurrentBlock)
            addFreezeEvent();
        previousFreeze = freeze;
    }
    stft.process(buffer, this, processFrameCallback, outputCallback);
    publishTelemetry(environment.captureTelemetry);
}

void TimeMosaicModule::processFrameCallback(
    void* context, FixedLatencyStft::Complex* const* spectra,
    int channels, int bins) noexcept
{
    static_cast<TimeMosaicModule*>(context)->processFrame(
        spectra, channels, bins);
}

float TimeMosaicModule::outputCallback(
    void* context, int channel, float dry, float wet) noexcept
{
    auto& module = *static_cast<TimeMosaicModule*>(context);
    if (channel == 0)
    {
        module.currentMix = module.mixSmoothed.getNextValue();
        module.currentOutput = module.outputSmoothed.getNextValue();
    }
    return detail::finiteSample(
        (dry + module.currentMix * (wet - dry)) * module.currentOutput);
}

void TimeMosaicModule::processFrame(
    FixedLatencyStft::Complex* const* spectra, int channels, int bins) noexcept
{
    if (!freeze)
    {
        for (int channel = 0; channel < channels; ++channel)
        {
            auto& scratch = writeScratch[static_cast<std::size_t>(channel)];
            for (int bin = 0; bin < bins; ++bin)
            {
                const auto value = safeComplex(spectra[channel][bin]);
                scratch[static_cast<std::size_t>(bin * 2)] = value.real();
                scratch[static_cast<std::size_t>(bin * 2 + 1)] = value.imag();
            }
            history[static_cast<std::size_t>(channel)].push(
                scratch.data(), scratch.size());
        }
    }

    if (framesUntilAssignment <= 0)
    {
        assignTiles(bins);
        framesUntilAssignment = transitionFrames;
    }
    --framesUntilAssignment;

    const auto availableFrames = static_cast<float>(juce::jmax(
        0, juce::jmin(
            activeHistoryFrames - 1,
            static_cast<int>(history[0].size()) - 1)));
    const auto progressStep = 1.0f / static_cast<float>(transitionFrames);
    const auto binHz =
        static_cast<float>(sampleRate / FixedLatencyStft::fftSize);
    double ageSum = 0.0;
    double pitchSum = 0.0;
    double wetPower = 0.0;
    bandMeanAge.fill(0.0f);
    std::array<int, 3> bandCounts {};

    for (int bin = 0; bin < bins; ++bin)
    {
        const auto index = static_cast<std::size_t>(bin);
        ageProgress[index] = juce::jmin(
            1.0f, ageProgress[index] + progressStep);
        const auto blend = 0.5f - 0.5f * std::cos(
            juce::MathConstants<float>::pi * ageProgress[index]);
        ageCurrent[index] = detail::lerp(
            ageStart[index], ageTarget[index], blend);
        const auto requestedAge = availableFrames * ageCurrent[index];
        ageSum += requestedAge;
        pitchSum += pitchByBin[index];

        const auto ratio = std::exp2(pitchByBin[index] / 12.0f);
        const auto sourceBin = juce::jlimit(
            0.0f, static_cast<float>(bins - 1),
            static_cast<float>(bin) / ratio);
        const auto firstBin = static_cast<int>(std::floor(sourceBin));
        const auto secondBin = juce::jmin(bins - 1, firstBin + 1);
        const auto binBlend = sourceBin - static_cast<float>(firstBin);

        float linkedPower = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto side = channels > 1
                ? (channel == 0 ? -1.0f : 1.0f) : 0.0f;
            const auto tile = tileIdByBin[static_cast<std::size_t>(bin)];
            const auto spread = side * stereoSpread * 0.08f
                * hashSigned(tile + 0x9e37u * assignmentGeneration);
            const auto channelAge = juce::jlimit(
                0.0f, availableFrames,
                requestedAge + spread * availableFrames);
            const auto low =
                readHistory(channel, firstBin, channelAge);
            const auto high =
                readHistory(channel, secondBin, channelAge);
            const auto value = safeComplex(
                low + binBlend * (high - low));
            spectra[channel][bin] = value;
            linkedPower += std::norm(value);
        }
        linkedPower /= static_cast<float>(juce::jmax(1, channels));
        binEnergy[index] = std::sqrt(linkedPower)
            / static_cast<float>(FixedLatencyStft::fftSize);
        wetPower += linkedPower
            / static_cast<double>(
                FixedLatencyStft::fftSize * FixedLatencyStft::fftSize);
        const auto frequency = static_cast<float>(bin) * binHz;
        const auto band = frequency < 300.0f ? 0
            : (frequency < 3000.0f ? 1 : 2);
        bandMeanAge[static_cast<std::size_t>(band)] +=
            ageCurrent[index] * historySeconds;
        ++bandCounts[static_cast<std::size_t>(band)];
    }

    for (std::size_t band = 0; band < bandMeanAge.size(); ++band)
        bandMeanAge[band] /= static_cast<float>(
            juce::jmax(1, bandCounts[band]));
    meanAgeSeconds = bins > 0
        ? static_cast<float>(ageSum / static_cast<double>(bins))
            * static_cast<float>(FixedLatencyStft::hopSize / sampleRate)
        : 0.0f;
    meanPitchSemitones = bins > 0
        ? static_cast<float>(pitchSum / static_cast<double>(bins)) : 0.0f;
    currentWetEnergy = bins > 0
        ? static_cast<float>(std::sqrt(wetPower / static_cast<double>(bins)))
        : 0.0f;
    wetMeter.store(currentWetEnergy, std::memory_order_relaxed);
    ageMeter.store(
        historySeconds > 0.0f ? meanAgeSeconds / historySeconds : 0.0f,
        std::memory_order_relaxed);
}

void TimeMosaicModule::assignTiles(int bins) noexcept
{
    ++assignmentGeneration;
    int first = 0;
    std::uint32_t tile = 0;
    while (first < bins)
    {
        const auto next = first == 0 ? 1 : juce::jlimit(
            first + 1, bins,
            static_cast<int>(std::ceil(
                static_cast<float>(first) * std::exp2(tileWidthOctaves))));
        const auto end = juce::jmax(first + 1, next);
        tileBins = end - first;
        const auto tileRandom = hashSigned(static_cast<std::uint32_t>(
            tile * 0x85ebca6bu
            + assignmentGeneration * 0xc2b2ae35u));
        const auto tilePitchRandom = hashSigned(static_cast<std::uint32_t>(
            tile * 0x27d4eb2du
            + assignmentGeneration * 0x165667b1u));
        float eventEnergy = 0.0f;
        float eventAge = 0.0f;
        for (int bin = first; bin < end; ++bin)
        {
            const auto index = static_cast<std::size_t>(bin);
            tileIdByBin[index] = tile;
            const auto binRandom = hashSigned(static_cast<std::uint32_t>(
                static_cast<std::uint32_t>(bin) * 0x9e3779b9u
                + assignmentGeneration * 0x7f4a7c15u));
            const auto random =
                coherence * tileRandom + (1.0f - coherence) * binRandom;
            const auto normalizedAge = juce::jlimit(
                0.0f, 1.0f, baseAge + 0.5f * motion * random);
            ageStart[index] = ageCurrent[index];
            ageTarget[index] =
                activeHistoryFrames > 1 ? normalizedAge : 0.0f;
            ageProgress[index] = 0.0f;
            pitchByBin[index] = pitchDriftSemitones
                * (coherence * tilePitchRandom
                   + (1.0f - coherence) * binRandom);
            eventEnergy += binEnergy[index];
            eventAge += normalizedAge;
        }
        if (end > first)
        {
            const auto count = static_cast<float>(end - first);
            addTileEvent(
                first, eventAge / count, pitchDriftSemitones * tilePitchRandom,
                eventEnergy / count);
        }
        first = end;
        ++tile;
    }
}

FixedLatencyStft::Complex TimeMosaicModule::readHistory(
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
    const auto offset = static_cast<std::size_t>(bin * 2);
    const FixedLatencyStft::Complex firstValue {
        first[offset], first[offset + 1]
    };
    if (second == nullptr || fraction <= 0.0f)
        return safeComplex(firstValue);
    const FixedLatencyStft::Complex secondValue {
        second[offset], second[offset + 1]
    };
    return safeComplex(
        firstValue + fraction * (secondValue - firstValue));
}

void TimeMosaicModule::addTileEvent(
    int firstBin, float normalizedAge, float semitones, float energy) noexcept
{
    if (!captureCurrentBlock || eventWorking.eventCount >= eventTelemetryCapacity)
        return;
    auto& event = eventWorking.events[eventWorking.eventCount++];
    event = {};
    event.sequence = ++eventSequence;
    event.kind = tileReassigned;
    event.flags = freeze ? 1u : 0u;
    event.position = {
        static_cast<float>(firstBin)
            / static_cast<float>(FixedLatencyStft::binCount - 1),
        juce::jlimit(0.0f, 1.0f, normalizedAge),
        juce::jlimit(-1.0f, 1.0f, semitones / 0.5f)
    };
    event.values = {
        detail::finiteSample(energy), static_cast<float>(tileBins),
        tileSeconds, coherence
    };
}

void TimeMosaicModule::addFreezeEvent() noexcept
{
    if (eventWorking.eventCount >= eventTelemetryCapacity)
        return;
    auto& event = eventWorking.events[eventWorking.eventCount++];
    event = {};
    event.sequence = ++eventSequence;
    event.kind = freezeChanged;
    event.flags = freeze ? 1u : 0u;
    event.values[0] = freeze ? 1.0f : 0.0f;
}

void TimeMosaicModule::publishTelemetry(bool capture) noexcept
{
    if (!capture)
        return;
    continuousState.sequence = ++continuousSequence;
    continuousState.valueCount = telemetryValueCount;
    continuousState.values = {
        historySeconds, meanAgeSeconds, tileWidthOctaves,
        tileSeconds, motion, coherence, meanPitchSemitones * 100.0f,
        freeze ? 1.0f : 0.0f
    };
    appendContinuousTelemetryHistory(
        continuousState,
        { bandMeanAge[0], bandMeanAge[1], bandMeanAge[2],
          currentWetEnergy },
        telemetryHistoryValueCount);
    continuousTelemetry.publish(continuousState);
    eventWorking.sequence = ++eventPublicationSequence;
    eventTelemetry.publish(eventWorking);
}

bool TimeMosaicModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return continuousTelemetry.read(snapshot);
}

bool TimeMosaicModule::readEventTelemetry(
    EventTelemetrySnapshot& snapshot) const noexcept
{
    return eventTelemetry.read(snapshot);
}

float TimeMosaicModule::hashSigned(std::uint32_t value) noexcept
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return static_cast<float>(value & 0x00ffffffu)
        * (2.0f / 16777215.0f) - 1.0f;
}

double TimeMosaicModule::tailSeconds(const ControlValues& controls) const
{
    return static_cast<double>(detail::exponential(
        0.25f, maximumHistorySeconds,
        detail::normalizedControl(controls[historyControl], 0.5f)))
        + static_cast<double>(FixedLatencyStft::fftSize) / sampleRate;
}
} // namespace megadsp
