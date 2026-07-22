#include "BeatPermuter.h"
#include "DspHelpers.h"

#include <cmath>

namespace megadsp
{
using detail::lerp;

namespace
{
constexpr std::array<double, 6> gridDurations {
    1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125
};

float safeNormalized(float value, float fallback)
{
    return std::isfinite(value) ? juce::jlimit(0.0f, 1.0f, value)
                                : fallback;
}

double safeBpm(double bpm)
{
    return std::isfinite(bpm) && bpm >= 30.0 && bpm <= 400.0 ? bpm : 120.0;
}

int wrapIndex(int value, int size)
{
    if (size <= 0)
        return 0;
    value %= size;
    return value < 0 ? value + size : value;
}

float equalPowerGate(float phase, float gate)
{
    phase = juce::jlimit(0.0f, 1.0f, phase);
    gate = juce::jlimit(0.2f, 1.0f, gate);
    const auto quiet = 0.5f * (1.0f - gate);
    const auto start = quiet;
    const auto end = 1.0f - quiet;
    const auto fadeWidth = juce::jmin(
        0.25f * gate, 0.035f + (1.0f - gate) * 0.12f);
    if (phase <= start || phase >= end)
        return 0.0f;
    if (phase < start + fadeWidth)
    {
        const auto ramp =
            (phase - start) / juce::jmax(0.0001f, fadeWidth);
        return std::sin(ramp * juce::MathConstants<float>::halfPi);
    }
    if (phase > end - fadeWidth)
    {
        const auto ramp =
            (end - phase) / juce::jmax(0.0001f, fadeWidth);
        return std::sin(ramp * juce::MathConstants<float>::halfPi);
    }
    return 1.0f;
}
} // namespace

void BeatPermuterModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = juce::jmax(8000.0, spec.sampleRate);
    const auto maximumHistorySeconds =
        (60.0 / 30.0)
        * static_cast<double>(maximumWindowSlices + 2);
    const auto capacity = juce::jmax(
        1, juce::roundToInt(maximumHistorySeconds * sampleRate) + 8);
    for (auto& channel : history)
        channel.assign(static_cast<size_t>(capacity), 0.0f);

    regenerationSmoothed.reset(sampleRate, 0.05);
    mixSmoothed.reset(sampleRate, 0.02);
    outputSmoothed.reset(sampleRate, 0.02);
    reset();
}

void BeatPermuterModule::reset()
{
    for (auto& channel : history)
        std::fill(channel.begin(), channel.end(), 0.0f);
    regenerationState.fill(0.0f);
    transitionFrom.fill(0.0f);
    lastWetFrame.fill(0.0f);
    activeEvent = {};
    currentSliceSamples = juce::jmax(1.0, sampleRate * 0.125);
    sliceSamplePosition = 0.0;
    writtenSamples = 0;
    sliceCounter = 0;
    randomState = 0x9e3779b9u;
    writePosition = 0;
    validHistorySamples = 0;
    transitionSamples = 0;
    transitionPosition = 0;
    parametersInitialized = false;
    transportInitialized = false;
    outputMeter.store(0.0f, std::memory_order_relaxed);
    regenerationSmoothed.setCurrentAndTargetValue(0.0f);
    mixSmoothed.setCurrentAndTargetValue(0.0f);
    outputSmoothed.setCurrentAndTargetValue(1.0f);

    for (auto& event : visual)
    {
        event.publicationSequence.fetch_add(
            1, std::memory_order_acq_rel);
        event.sequence.store(0, std::memory_order_relaxed);
        event.sourcePosition.store(0.0f, std::memory_order_relaxed);
        event.progress.store(1.0f, std::memory_order_relaxed);
        event.windowSize.store(0.0f, std::memory_order_relaxed);
        event.repeatCount.store(0.0f, std::memory_order_relaxed);
        event.gate.store(1.0f, std::memory_order_relaxed);
        event.stereoBias.store(0.0f, std::memory_order_relaxed);
        event.pattern.store(0, std::memory_order_relaxed);
        event.reverse.store(false, std::memory_order_relaxed);
        event.publicationSequence.fetch_add(
            1, std::memory_order_release);
    }
}

std::uint32_t BeatPermuterModule::nextRandom()
{
    auto value = randomState;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    randomState = value != 0 ? value : 0x9e3779b9u;
    return randomState;
}

float BeatPermuterModule::randomUnit()
{
    return static_cast<float>(nextRandom() >> 8)
           * (1.0f / 16777216.0f);
}

int BeatPermuterModule::randomInteger(int low, int high)
{
    if (high <= low)
        return low;
    const auto range = static_cast<std::uint32_t>(high - low + 1);
    return low + static_cast<int>(nextRandom() % range);
}

double BeatPermuterModule::sliceDurationSamples(
    float normalizedGrid, double bpm) const
{
    const auto gridIndex = discreteIndex(normalizedGrid, 6);
    const auto safeGrid = gridDurations[static_cast<size_t>(gridIndex)];
    return juce::jmax(8.0, sampleRate * 60.0 / safeBpm(bpm) * safeGrid);
}

void BeatPermuterModule::finishActiveEvent()
{
    if (!activeEvent.active)
        return;

    auto& event = visual[static_cast<size_t>(
        activeEvent.visualSequence
        % static_cast<std::uint32_t>(beatPermutationVisualEventCount))];
    if (event.sequence.load(std::memory_order_acquire)
        == activeEvent.visualSequence)
    {
        event.publicationSequence.fetch_add(
            1, std::memory_order_acq_rel);
        event.progress.store(1.0f, std::memory_order_relaxed);
        event.publicationSequence.fetch_add(
            1, std::memory_order_release);
    }
    activeEvent = {};
}

void BeatPermuterModule::publishEvent(const PermutationEvent& eventToPublish)
{
    auto averageOffset = 0.0f;
    for (int channel = 0; channel < 2; ++channel)
        averageOffset += static_cast<float>(
            eventToPublish.sourceOffsets[static_cast<size_t>(channel)][0]);
    averageOffset *= 0.5f;
    const auto sourcePosition = eventToPublish.windowSlices <= 1
        ? 0.0f
        : averageOffset
              / static_cast<float>(eventToPublish.windowSlices - 1);
    const auto stereoBias = eventToPublish.windowSlices <= 1
        ? 0.0f
        : static_cast<float>(
              eventToPublish.sourceOffsets[1][0]
              - eventToPublish.sourceOffsets[0][0])
              / static_cast<float>(eventToPublish.windowSlices - 1);
    const auto reverse =
        eventToPublish.reversePlayback[0][0]
        || eventToPublish.reversePlayback[1][0];

    auto& event = visual[static_cast<size_t>(
        eventToPublish.visualSequence
        % static_cast<std::uint32_t>(beatPermutationVisualEventCount))];
    event.publicationSequence.fetch_add(
        1, std::memory_order_acq_rel);
    event.sequence.store(
        eventToPublish.visualSequence, std::memory_order_relaxed);
    event.sourcePosition.store(
        juce::jlimit(0.0f, 1.0f, sourcePosition),
        std::memory_order_relaxed);
    event.progress.store(0.0f, std::memory_order_relaxed);
    event.windowSize.store(
        static_cast<float>(eventToPublish.windowSlices)
            / static_cast<float>(maximumWindowSlices),
        std::memory_order_relaxed);
    event.repeatCount.store(
        static_cast<float>(eventToPublish.lengthSlices)
            / static_cast<float>(maximumEventSlices),
        std::memory_order_relaxed);
    event.gate.store(
        juce::jlimit(0.0f, 1.0f, eventToPublish.gate),
        std::memory_order_relaxed);
    event.stereoBias.store(
        juce::jlimit(-1.0f, 1.0f, stereoBias),
        std::memory_order_relaxed);
    event.pattern.store(eventToPublish.pattern, std::memory_order_relaxed);
    event.reverse.store(reverse, std::memory_order_relaxed);
    event.publicationSequence.fetch_add(
        1, std::memory_order_release);
}

void BeatPermuterModule::startPermutationEvent(
    const ControlValues& controls, double bpm)
{
    const auto activity = safeNormalized(controls[1], 0.35f);
    const auto candidateSliceSamples =
        sliceDurationSamples(safeNormalized(controls[0], 0.6f), bpm);
    const auto completeSlices = static_cast<int>(std::floor(
        (static_cast<double>(validHistorySamples) - 4.0)
        / juce::jmax(1.0, candidateSliceSamples)));
    const auto requestedWindow = juce::jlimit(
        1, maximumWindowSlices, juce::roundToInt(lerp(
                                     1.0f, 8.0f,
                                     safeNormalized(controls[3], 3.0f / 7.0f))));
    if (activity <= 0.0f || randomUnit() > activity || completeSlices <= 0)
        return;

    PermutationEvent event;
    event.active = true;
    event.pattern = discreteIndex(safeNormalized(controls[2], 0.0f), 4);
    event.windowSlices = juce::jmin(requestedWindow, completeSlices);
    event.lengthSlices = juce::jlimit(
        1, maximumEventSlices, juce::roundToInt(lerp(
                                     1.0f, 8.0f,
                                     safeNormalized(controls[4], 1.0f / 7.0f))));
    event.currentStep = 0;
    event.sliceSamples = candidateSliceSamples;
    event.sourceBoundary = writtenSamples;
    event.gate = lerp(0.2f, 1.0f, safeNormalized(controls[5], 0.875f));
    const auto pitchSemitones =
        lerp(-12.0f, 12.0f, safeNormalized(controls[6], 0.5f));
    event.pitchRatio = std::pow(2.0f, pitchSemitones / 12.0f);
    event.stereoOffset = safeNormalized(controls[8], 0.25f);
    event.variation = safeNormalized(controls[7], 0.2f);
    event.visualSequence = nextRandom();

    const auto stereoShift = juce::roundToInt(
        event.stereoOffset
        * static_cast<float>(juce::jmax(0, event.windowSlices - 1)));
    const auto rotateBase = event.windowSlices > 1
        ? randomInteger(0, event.windowSlices - 1)
        : 0;
    const auto repeatBase = event.windowSlices > 1
        ? randomInteger(0, event.windowSlices - 1)
        : 0;
    const auto reverseBase = juce::jmax(0, event.windowSlices - 1);
    const auto rotateStep = juce::jlimit(
        1, juce::jmax(1, event.windowSlices - 1),
        1 + juce::roundToInt(
                event.variation
                * static_cast<float>(juce::jmax(0, event.windowSlices - 1))));

    for (int channel = 0; channel < 2; ++channel)
    {
        const auto channelShift = channel == 0 ? -stereoShift : stereoShift;
        for (int step = 0; step < maximumEventSlices; ++step)
        {
            auto sourceOffset = 0;
            auto reversePlayback = false;
            if (step < event.lengthSlices)
            {
                switch (event.pattern)
                {
                    case 0:
                        sourceOffset =
                            wrapIndex(repeatBase + channelShift,
                                      event.windowSlices);
                        reversePlayback = event.variation > 0.7f
                                          && ((step + channel) & 1) != 0;
                        break;
                    case 1:
                        sourceOffset =
                            wrapIndex(reverseBase - step + channelShift,
                                      event.windowSlices);
                        reversePlayback = true;
                        break;
                    case 2:
                        sourceOffset =
                            wrapIndex(rotateBase + rotateStep * step
                                          + channelShift,
                                      event.windowSlices);
                        break;
                    case 3:
                    default:
                        sourceOffset =
                            wrapIndex(randomInteger(0, event.windowSlices - 1)
                                          + channelShift,
                                      event.windowSlices);
                        reversePlayback =
                            randomUnit() < event.variation * 0.55f;
                        break;
                }
            }

            event.sourceOffsets[static_cast<size_t>(channel)][
                static_cast<size_t>(step)] = sourceOffset;
            event.phaseOffsets[static_cast<size_t>(channel)][
                static_cast<size_t>(step)] =
                (randomUnit() * 2.0f - 1.0f) * 0.45f * event.variation;
            event.reversePlayback[static_cast<size_t>(channel)][
                static_cast<size_t>(step)] = reversePlayback;
        }
    }

    activeEvent = event;
    currentSliceSamples = activeEvent.sliceSamples;
    publishEvent(activeEvent);
}

void BeatPermuterModule::beginSlice(
    const ControlValues& controls, double bpm)
{
    transitionFrom = lastWetFrame;
    transitionSamples = juce::jlimit(
        8, 64, juce::roundToInt(currentSliceSamples * 0.06));
    transitionPosition = 0;
    sliceSamplePosition = 0.0;
    ++sliceCounter;

    if (activeEvent.active
        && activeEvent.currentStep + 1 < activeEvent.lengthSlices)
    {
        ++activeEvent.currentStep;
        activeEvent.sourceBoundary = writtenSamples;
        currentSliceSamples = activeEvent.sliceSamples;
        return;
    }

    finishActiveEvent();
    currentSliceSamples =
        sliceDurationSamples(safeNormalized(controls[0], 0.6f), bpm);
    startPermutationEvent(controls, bpm);
}

float BeatPermuterModule::sampleAt(
    int channel, std::int64_t absoluteSample) const
{
    const auto& source =
        history[static_cast<size_t>(juce::jlimit(0, 1, channel))];
    if (source.empty() || absoluteSample < 0
        || static_cast<std::uint64_t>(absoluteSample) >= writtenSamples)
        return 0.0f;

    const auto oldest = static_cast<std::int64_t>(writtenSamples)
                        - static_cast<std::int64_t>(validHistorySamples);
    if (absoluteSample < oldest)
        return 0.0f;

    const auto size = static_cast<std::int64_t>(source.size());
    auto index = absoluteSample % size;
    if (index < 0)
        index += size;
    return source[static_cast<size_t>(index)];
}

float BeatPermuterModule::readHistory(
    int channel, double absoluteSample) const
{
    const auto base = static_cast<std::int64_t>(std::floor(absoluteSample));
    const auto fraction = static_cast<float>(
        absoluteSample - static_cast<double>(base));
    const auto y0 = sampleAt(channel, base - 1);
    const auto y1 = sampleAt(channel, base);
    const auto y2 = sampleAt(channel, base + 1);
    const auto y3 = sampleAt(channel, base + 2);
    const auto a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    const auto a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const auto a2 = -0.5f * y0 + 0.5f * y2;
    return ((a0 * fraction + a1) * fraction + a2) * fraction + y1;
}

std::array<float, 2> BeatPermuterModule::renderWetSample(
    double sliceOffset) const
{
    if (!activeEvent.active)
        return {};

    const auto slicePhase = static_cast<float>(
        juce::jlimit(0.0, 1.0,
                     activeEvent.sliceSamples > 0.0
                         ? sliceOffset / activeEvent.sliceSamples
                         : 0.0));
    const auto gate = equalPowerGate(slicePhase, activeEvent.gate);
    std::array<float, 2> wet {};
    const auto step = juce::jlimit(
        0, activeEvent.lengthSlices - 1, activeEvent.currentStep);

    for (int channel = 0; channel < 2; ++channel)
    {
        const auto offset =
            activeEvent.sourceOffsets[static_cast<size_t>(channel)][
                static_cast<size_t>(step)];
        auto phase = std::fmod(
            static_cast<double>(slicePhase)
                * static_cast<double>(activeEvent.pitchRatio)
            + static_cast<double>(
                activeEvent.phaseOffsets[static_cast<size_t>(channel)][
                    static_cast<size_t>(step)]),
            1.0);
        if (phase < 0.0)
            phase += 1.0;
        auto localPosition = phase * activeEvent.sliceSamples;
        if (activeEvent.reversePlayback[static_cast<size_t>(channel)][
                static_cast<size_t>(step)])
            localPosition =
                juce::jmax(0.0, activeEvent.sliceSamples - 1.0 - localPosition);

        const auto sourceStart =
            static_cast<double>(activeEvent.sourceBoundary)
            - static_cast<double>(offset + 1) * activeEvent.sliceSamples;
        wet[static_cast<size_t>(channel)] =
            readHistory(channel, sourceStart + localPosition) * gate;
    }

    return wet;
}

void BeatPermuterModule::updateVisualProgress()
{
    if (!activeEvent.active)
        return;

    auto& event = visual[static_cast<size_t>(
        activeEvent.visualSequence
        % static_cast<std::uint32_t>(beatPermutationVisualEventCount))];
    if (event.sequence.load(std::memory_order_acquire)
        != activeEvent.visualSequence)
        return;

    const auto sliceProgress = static_cast<float>(juce::jlimit(
        0.0, 1.0,
        activeEvent.sliceSamples > 0.0
            ? sliceSamplePosition / activeEvent.sliceSamples
            : 1.0));
    const auto progress =
        (static_cast<float>(activeEvent.currentStep) + sliceProgress)
        / static_cast<float>(juce::jmax(1, activeEvent.lengthSlices));
    event.publicationSequence.fetch_add(
        1, std::memory_order_acq_rel);
    event.progress.store(
        juce::jlimit(0.0f, 1.0f, progress), std::memory_order_relaxed);
    event.publicationSequence.fetch_add(
        1, std::memory_order_release);
}

void BeatPermuterModule::process(juce::AudioBuffer<float>& buffer,
                                 const ControlValues& controls,
                                 const ProcessEnvironment& environment)
{
    juce::ScopedNoDenormals noDenormals;
    if (history[0].empty() || buffer.getNumChannels() <= 0)
        return;

    const auto regenerationTarget =
        safeNormalized(controls[9], 0.10f / 0.75f) * 0.75f;
    const auto mixTarget = safeNormalized(controls[10], 0.35f);
    const auto outputTarget = juce::Decibels::decibelsToGain(
        lerp(-18.0f, 12.0f, safeNormalized(controls[11], 0.6f)));
    regenerationSmoothed.setTargetValue(regenerationTarget);
    mixSmoothed.setTargetValue(mixTarget);
    outputSmoothed.setTargetValue(outputTarget);

    if (!parametersInitialized)
    {
        regenerationSmoothed.setCurrentAndTargetValue(regenerationTarget);
        mixSmoothed.setCurrentAndTargetValue(mixTarget);
        outputSmoothed.setCurrentAndTargetValue(outputTarget);
        parametersInitialized = true;
    }
    if (!transportInitialized)
    {
        currentSliceSamples =
            sliceDurationSamples(safeNormalized(controls[0], 0.6f),
                                 environment.bpm);
        sliceSamplePosition = 0.0;
        transportInitialized = true;
    }

    const auto historySize = static_cast<int>(history[0].size());
    auto meter = outputMeter.load(std::memory_order_relaxed);

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        if (sliceSamplePosition >= currentSliceSamples)
            beginSlice(controls, environment.bpm);

        const std::array<float, 2> dry {
            buffer.getSample(0, sample),
            buffer.getNumChannels() > 1 ? buffer.getSample(1, sample)
                                        : buffer.getSample(0, sample)
        };

        auto wet = renderWetSample(sliceSamplePosition);
        if (transitionPosition < transitionSamples)
        {
            const auto progress = static_cast<float>(transitionPosition)
                / static_cast<float>(juce::jmax(1, transitionSamples));
            const auto oldGain =
                std::cos(progress * juce::MathConstants<float>::halfPi);
            const auto newGain =
                std::sin(progress * juce::MathConstants<float>::halfPi);
            for (int channel = 0; channel < 2; ++channel)
                wet[static_cast<size_t>(channel)] =
                    transitionFrom[static_cast<size_t>(channel)] * oldGain
                    + wet[static_cast<size_t>(channel)] * newGain;
            ++transitionPosition;
        }

        const auto regeneration = regenerationSmoothed.getNextValue();
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto captured = dry[static_cast<size_t>(channel)]
                + regeneration
                      * regenerationState[static_cast<size_t>(channel)];
            history[static_cast<size_t>(channel)][
                static_cast<size_t>(writePosition)] =
                juce::jlimit(-16.0f, 16.0f,
                             std::isfinite(captured) ? captured : 0.0f);
        }

        regenerationState[0] =
            0.82f * regenerationState[0]
            + 0.18f * wet[0];
        regenerationState[1] =
            0.82f * regenerationState[1]
            + 0.18f * wet[1];

        if (++writePosition >= historySize)
            writePosition = 0;
        ++writtenSamples;
        validHistorySamples = juce::jmin(historySize, validHistorySamples + 1);

        meter = meter * 0.9925f
                + 0.0075f
                      * juce::jmax(std::abs(wet[0]), std::abs(wet[1]));
        const auto mix = mixSmoothed.getNextValue();
        const auto dryGain = mix <= 0.0f
            ? 1.0f
            : std::cos(mix * juce::MathConstants<float>::halfPi);
        const auto wetGain = mix <= 0.0f
            ? 0.0f
            : std::sin(mix * juce::MathConstants<float>::halfPi);
        const auto output = outputSmoothed.getNextValue();

        if (buffer.getNumChannels() == 1)
        {
            const auto monoWet = (wet[0] + wet[1]) * 0.5f;
            const auto result =
                (dry[0] * dryGain + monoWet * wetGain) * output;
            buffer.setSample(0, sample, std::isfinite(result) ? result : 0.0f);
        }
        else
        {
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                const auto index = static_cast<size_t>(juce::jmin(channel, 1));
                const auto result =
                    (dry[index] * dryGain + wet[index] * wetGain) * output;
                buffer.setSample(channel, sample,
                                 std::isfinite(result) ? result : 0.0f);
            }
        }

        lastWetFrame = wet;
        sliceSamplePosition += 1.0;
    }

    outputMeter.store(juce::jlimit(0.0f, 1.0f, meter),
                      std::memory_order_relaxed);
    updateVisualProgress();
}

double BeatPermuterModule::tailSeconds(const ControlValues& controls) const
{
    const auto sliceSeconds =
        60.0 / 120.0
        * gridDurations[static_cast<size_t>(discreteIndex(
            std::isfinite(controls[0]) ? controls[0] : 0.6f, 6))];
    const auto windowSlices = static_cast<double>(juce::jlimit(
        1, maximumWindowSlices,
        juce::roundToInt(lerp(
            1.0f, 8.0f, std::isfinite(controls[3]) ? controls[3]
                                                   : 3.0f / 7.0f))));
    const auto eventSlices = static_cast<double>(juce::jlimit(
        1, maximumEventSlices,
        juce::roundToInt(lerp(
            1.0f, 8.0f, std::isfinite(controls[4]) ? controls[4]
                                                   : 1.0f / 7.0f))));
    const auto regeneration = juce::jlimit(
        0.0, 0.75,
        static_cast<double>(
            (std::isfinite(controls[9]) ? controls[9] : (0.10f / 0.75f))
            * 0.75f));
    const auto sustain = regeneration <= 0.001
        ? 0.0
        : sliceSeconds * (1.0 + windowSlices * 0.25)
              * regeneration / (1.0 - regeneration);
    return sliceSeconds * (windowSlices + eventSlices) + sustain + 0.05;
}

BeatPermutationVisualEvents
BeatPermuterModule::beatPermutationVisualEvents() const noexcept
{
    BeatPermutationVisualEvents result {};
    for (size_t index = 0; index < result.size(); ++index)
    {
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            const auto publication =
                visual[index].publicationSequence.load(
                    std::memory_order_acquire);
            if ((publication & 1u) != 0)
                continue;

            BeatPermutationVisualEvent candidate;
            candidate.sequence =
                visual[index].sequence.load(std::memory_order_relaxed);
            candidate.sourcePosition =
                visual[index].sourcePosition.load(std::memory_order_relaxed);
            candidate.progress =
                visual[index].progress.load(std::memory_order_relaxed);
            candidate.windowSize =
                visual[index].windowSize.load(std::memory_order_relaxed);
            candidate.repeatCount =
                visual[index].repeatCount.load(std::memory_order_relaxed);
            candidate.gate =
                visual[index].gate.load(std::memory_order_relaxed);
            candidate.stereoBias =
                visual[index].stereoBias.load(std::memory_order_relaxed);
            candidate.pattern =
                visual[index].pattern.load(std::memory_order_relaxed);
            candidate.reverse =
                visual[index].reverse.load(std::memory_order_relaxed);

            if (visual[index].publicationSequence.load(
                    std::memory_order_acquire)
                == publication)
            {
                result[index] = candidate;
                break;
            }
        }
    }
    return result;
}
} // namespace megadsp
