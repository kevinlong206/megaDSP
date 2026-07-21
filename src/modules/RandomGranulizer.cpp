#include "RandomGranulizer.h"
#include "DspHelpers.h"

#include <cmath>

namespace megadsp
{
using detail::exponential;
using detail::lerp;

void RandomGranulizerModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = juce::jmax(8000.0, spec.sampleRate);
    inputChannels = juce::jlimit(
        1, 2, static_cast<int>(spec.numChannels));
    const auto capacity = juce::jmax(
        1, juce::roundToInt(sampleRate * 6.0));
    for (auto& channel : history)
        channel.assign(static_cast<size_t>(capacity), 0.0f);
    mixSmoothed.reset(sampleRate, 0.02);
    outputSmoothed.reset(sampleRate, 0.02);
    feedbackSmoothed.reset(sampleRate, 0.04);
    reset();
}

void RandomGranulizerModule::reset()
{
    for (auto& channel : history)
        std::fill(channel.begin(), channel.end(), 0.0f);
    for (auto& voice : voices)
        voice = {};
    for (auto& grain : pending)
        grain = {};
    feedbackState.fill(0.0f);
    writePosition = 0;
    writtenSamples = 0;
    validHistorySamples = 0;
    samplesUntilLaunch = 0.0;
    randomState = 0x6d2b79f5u;
    eventSequence = 0;
    parametersInitialized = false;
    activeVoices.store(0, std::memory_order_relaxed);
    maximumObservedVoices.store(0, std::memory_order_relaxed);
    mixSmoothed.setCurrentAndTargetValue(0.0f);
    outputSmoothed.setCurrentAndTargetValue(1.0f);
    feedbackSmoothed.setCurrentAndTargetValue(0.0f);
    for (auto& event : visual)
    {
        event.publicationSequence.fetch_add(
            1, std::memory_order_acq_rel);
        event.historyPosition.store(0.0f, std::memory_order_relaxed);
        event.durationSeconds.store(0.0f, std::memory_order_relaxed);
        event.progress.store(1.0f, std::memory_order_relaxed);
        event.pan.store(0.0f, std::memory_order_relaxed);
        event.filter.store(1.0f, std::memory_order_relaxed);
        event.reverse.store(false, std::memory_order_relaxed);
        event.sequence.store(0, std::memory_order_relaxed);
        event.publicationSequence.fetch_add(
            1, std::memory_order_release);
    }
}

std::uint32_t RandomGranulizerModule::nextRandom()
{
    auto value = randomState;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    randomState = value != 0 ? value : 0x6d2b79f5u;
    return randomState;
}

float RandomGranulizerModule::randomUnit()
{
    return static_cast<float>(nextRandom() >> 8)
           * (1.0f / 16777216.0f);
}

int RandomGranulizerModule::randomInteger(int low, int high)
{
    if (high <= low)
        return low;
    const auto range = static_cast<std::uint32_t>(high - low + 1);
    return low + static_cast<int>(nextRandom() % range);
}

float RandomGranulizerModule::readHistory(int channel, double position) const
{
    const auto& source = history[static_cast<size_t>(juce::jlimit(0, 1, channel))];
    if (source.empty())
        return 0.0f;
    const auto size = static_cast<double>(source.size());
    auto wrapped = std::fmod(position, size);
    if (wrapped < 0.0)
        wrapped += size;
    const auto first = static_cast<int>(wrapped);
    const auto second = first + 1 < static_cast<int>(source.size())
                            ? first + 1 : 0;
    const auto fraction = static_cast<float>(wrapped - first);
    return source[static_cast<size_t>(first)]
           + (source[static_cast<size_t>(second)]
              - source[static_cast<size_t>(first)]) * fraction;
}

void RandomGranulizerModule::scheduleGrain(
    const ControlValues& controls, double bpm)
{
    auto safe = [](float value, float fallback)
    {
        return std::isfinite(value) ? juce::jlimit(0.0f, 1.0f, value)
                                    : fallback;
    };
    const auto firstSize = exponential(
        50.0f, 2000.0f, safe(controls[1], 0.127f));
    const auto secondSize = exponential(
        50.0f, 2000.0f, safe(controls[4], 0.467f));
    const auto minimumDuration = juce::jmax(
        1, juce::roundToInt(
            juce::jmin(firstSize, secondSize) * 0.001 * sampleRate));
    const auto maximumDuration = juce::jmax(
        minimumDuration, juce::roundToInt(
            juce::jmax(firstSize, secondSize) * 0.001 * sampleRate));
    if (validHistorySamples <= maximumDuration + 8)
        return;

    auto grain = std::find_if(
        pending.begin(), pending.end(),
        [](const auto& candidate) { return !candidate.active; });
    if (grain == pending.end())
        return;
    const auto duration = randomInteger(minimumDuration, maximumDuration);

    static constexpr std::array<float, 6> beatDelays {
        0.0f, 0.125f, 0.25f, 0.5f, 1.0f, 2.0f
    };
    const auto maximumDelayIndex = juce::roundToInt(
        safe(controls[7], 0.0f) * 5.0f);
    const auto delayIndex = randomInteger(0, maximumDelayIndex);
    const auto safeBpm = std::isfinite(bpm) && bpm >= 30.0 && bpm <= 400.0
                             ? bpm : 120.0;
    const auto delaySamples = juce::roundToInt(
        beatDelays[static_cast<size_t>(delayIndex)]
        * 60.0 / safeBpm * sampleRate);
    const auto maximumCutoff = exponential(
        500.0f, 20000.0f, safe(controls[8], 0.75f));
    const auto filterSteps = randomInteger(0, 24);

    grain->active = true;
    grain->remainingDelay = delaySamples;
    grain->originalDelay = delaySamples;
    grain->waitSamples = 0;
    grain->duration = duration;
    grain->scatter = safe(controls[3], 0.35f);
    grain->pan = (randomUnit() * 2.0f - 1.0f)
                 * safe(controls[6], 0.65f);
    grain->cutoff = juce::jmax(
        80.0f, maximumCutoff
                   * std::pow(2.0f, -static_cast<float>(filterSteps) / 12.0f));
    grain->reverse = randomUnit() < safe(controls[5], 0.2f);
    grain->voiceCap = juce::jlimit(
        1, maximumVoices,
        juce::roundToInt(1.0f + safe(controls[0], 5.0f / 15.0f) * 15.0f));
}

bool RandomGranulizerModule::launchGrain(PendingGrain& pendingGrain)
{
    const auto currentActive = static_cast<int>(std::count_if(
        voices.begin(), voices.end(),
        [](const auto& voice) { return voice.active; }));
    if (currentActive >= pendingGrain.voiceCap)
        return false;
    auto voice = std::find_if(
        voices.begin(), voices.end(),
        [](const auto& candidate) { return !candidate.active; });
    if (voice == voices.end())
        return false;

    const auto sourceSpan = pendingGrain.duration + 4;
    const auto availableScatter = juce::jmax(
        0, validHistorySamples - sourceSpan * 2 - 2);
    const auto scatterSamples = juce::roundToInt(
        pendingGrain.scatter * randomUnit()
        * static_cast<float>(availableScatter));
    if (validHistorySamples <= sourceSpan * 2 + scatterSamples)
        return false;

    voice->active = true;
    voice->increment = pendingGrain.reverse ? -1.0f : 1.0f;
    voice->duration = pendingGrain.duration;
    voice->age = 0;
    voice->preLaunchDelay = pendingGrain.originalDelay;
    voice->pan = pendingGrain.pan;
    const auto angle = (pendingGrain.pan + 1.0f)
                       * juce::MathConstants<float>::pi * 0.25f;
    const auto rootTwo = std::sqrt(2.0f);
    voice->leftGain = std::cos(angle) * rootTwo;
    voice->rightGain = std::sin(angle) * rootTwo;
    voice->gain = 0.9f / std::sqrt(
        static_cast<float>(pendingGrain.voiceCap));
    voice->filterCoefficient = 1.0f - std::exp(
        -juce::MathConstants<float>::twoPi
        * juce::jmin(pendingGrain.cutoff,
                     static_cast<float>(sampleRate * 0.45))
        / static_cast<float>(sampleRate));
    voice->filterState.fill(0.0f);
    const auto newest = static_cast<double>(writtenSamples) - 2.0
                        - static_cast<double>(scatterSamples);
    voice->readPosition = pendingGrain.reverse
                              ? newest
                              : newest
                                    - static_cast<double>(
                                          pendingGrain.duration);
    publishEvent(
        *voice,
        static_cast<float>(scatterSamples)
            / static_cast<float>(juce::jmax(1, validHistorySamples)),
        pendingGrain.cutoff, pendingGrain.reverse);
    pendingGrain.active = false;
    return true;
}

void RandomGranulizerModule::publishEvent(
    Voice& voice, float historyPosition, float cutoff, bool reverse)
{
    const auto sequence = ++eventSequence;
    auto& event = visual[static_cast<size_t>(
        sequence % static_cast<std::uint32_t>(grainVisualEventCount))];
    event.publicationSequence.fetch_add(
        1, std::memory_order_acq_rel);
    event.historyPosition.store(
        juce::jlimit(0.0f, 1.0f, historyPosition),
        std::memory_order_relaxed);
    event.durationSeconds.store(
        static_cast<float>(voice.duration / sampleRate),
        std::memory_order_relaxed);
    event.progress.store(0.0f, std::memory_order_relaxed);
    event.pan.store(voice.pan, std::memory_order_relaxed);
    event.filter.store(
        juce::jlimit(0.0f, 1.0f,
                     std::log(juce::jmax(500.0f, cutoff) / 500.0f)
                         / std::log(40.0f)),
        std::memory_order_relaxed);
    event.reverse.store(reverse, std::memory_order_relaxed);
    event.sequence.store(sequence, std::memory_order_relaxed);
    event.publicationSequence.fetch_add(
        1, std::memory_order_release);
    voice.visualSequence = sequence;
}

void RandomGranulizerModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    if (history[0].empty() || buffer.getNumChannels() <= 0)
        return;
    auto safe = [](float value, float fallback)
    {
        return std::isfinite(value) ? juce::jlimit(0.0f, 1.0f, value)
                                    : fallback;
    };
    const auto mixTarget = safe(controls[10], 0.35f);
    const auto outputTarget = juce::Decibels::decibelsToGain(
        lerp(-18.0f, 12.0f, safe(controls[11], 0.6f)));
    const auto feedbackTarget = safe(controls[9], 0.125f) * 0.8f;
    mixSmoothed.setTargetValue(mixTarget);
    outputSmoothed.setTargetValue(outputTarget);
    feedbackSmoothed.setTargetValue(feedbackTarget);
    if (!parametersInitialized)
    {
        mixSmoothed.setCurrentAndTargetValue(mixTarget);
        outputSmoothed.setCurrentAndTargetValue(outputTarget);
        feedbackSmoothed.setCurrentAndTargetValue(feedbackTarget);
        parametersInitialized = true;
    }

    const auto historySize = static_cast<int>(history[0].size());
    const auto activity = exponential(
        0.5f, 30.0f, safe(controls[2], 0.61f));
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        std::array<float, 2> dry {
            buffer.getSample(0, sample),
            buffer.getNumChannels() > 1 ? buffer.getSample(1, sample)
                                        : buffer.getSample(0, sample)
        };
        const auto feedback = feedbackSmoothed.getNextValue();
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto captured = dry[static_cast<size_t>(channel)]
                                  + feedback
                                        * feedbackState[static_cast<size_t>(channel)];
            history[static_cast<size_t>(channel)][
                static_cast<size_t>(writePosition)] =
                juce::jlimit(-8.0f, 8.0f,
                             std::isfinite(captured) ? captured : 0.0f);
        }
        if (++writePosition >= historySize)
            writePosition = 0;
        ++writtenSamples;
        validHistorySamples = juce::jmin(
            historySize, validHistorySamples + 1);

        if (--samplesUntilLaunch <= 0.0)
        {
            scheduleGrain(controls, environment.bpm);
            const auto jitter = 0.75 + 0.5 * static_cast<double>(randomUnit());
            samplesUntilLaunch = sampleRate / activity * jitter;
        }

        for (auto& grain : pending)
        {
            if (!grain.active)
                continue;
            if (grain.remainingDelay > 0)
            {
                --grain.remainingDelay;
                continue;
            }
            if (!launchGrain(grain)
                && ++grain.waitSamples
                       > juce::roundToInt(sampleRate * 0.5))
                grain.active = false;
        }

        std::array<float, 2> wet {};
        int activeCount = 0;
        for (auto& voice : voices)
        {
            if (!voice.active)
                continue;
            ++activeCount;
            const auto phase = static_cast<float>(voice.age)
                               / static_cast<float>(
                                     juce::jmax(1, voice.duration));
            const auto sine = std::sin(
                juce::MathConstants<float>::pi
                * juce::jlimit(0.0f, 1.0f, phase));
            const auto window = sine * sine;
            std::array<float, 2> filtered {};
            for (int channel = 0; channel < 2; ++channel)
            {
                const auto source = readHistory(channel, voice.readPosition);
                auto& state = voice.filterState[static_cast<size_t>(channel)];
                state += voice.filterCoefficient * (source - state);
                filtered[static_cast<size_t>(channel)] = state;
            }
            if (buffer.getNumChannels() == 1)
                wet[0] += (filtered[0] + filtered[1]) * 0.5f
                          * window * voice.gain;
            else
            {
                const auto mid = (filtered[0] + filtered[1]) * 0.5f;
                const auto side = (filtered[0] - filtered[1]) * 0.5f
                                  * (1.0f - std::abs(voice.pan));
                wet[0] += (mid + side) * voice.leftGain
                          * window * voice.gain;
                wet[1] += (mid - side) * voice.rightGain
                          * window * voice.gain;
            }
            voice.readPosition += voice.increment;
            if (++voice.age >= voice.duration)
            {
                voice.active = false;
                auto& event = visual[static_cast<size_t>(
                    voice.visualSequence
                    % static_cast<std::uint32_t>(grainVisualEventCount))];
                if (event.sequence.load(std::memory_order_acquire)
                    == voice.visualSequence)
                {
                    event.publicationSequence.fetch_add(
                        1, std::memory_order_acq_rel);
                    event.progress.store(1.0f, std::memory_order_relaxed);
                    event.publicationSequence.fetch_add(
                        1, std::memory_order_release);
                }
            }
        }
        activeVoices.store(activeCount, std::memory_order_relaxed);
        maximumObservedVoices.store(
            juce::jmax(maximumObservedVoices.load(std::memory_order_relaxed),
                       activeCount),
            std::memory_order_relaxed);
        if (buffer.getNumChannels() == 1)
            wet[1] = wet[0];
        for (int channel = 0; channel < 2; ++channel)
            feedbackState[static_cast<size_t>(channel)] = juce::jlimit(
                -4.0f, 4.0f,
                std::isfinite(wet[static_cast<size_t>(channel)])
                    ? wet[static_cast<size_t>(channel)] : 0.0f);

        const auto mix = mixSmoothed.getNextValue();
        const auto dryGain = mix <= 0.0f
                                 ? 1.0f
                                 : std::cos(
                                       mix
                                       * juce::MathConstants<float>::halfPi);
        const auto wetGain = mix <= 0.0f
                                 ? 0.0f
                                 : std::sin(
                                       mix
                                       * juce::MathConstants<float>::halfPi);
        const auto output = outputSmoothed.getNextValue();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto index = static_cast<size_t>(juce::jmin(channel, 1));
            const auto granular = buffer.getNumChannels() == 1
                                      ? wet[0] : wet[index];
            const auto result = (dry[index] * dryGain
                                 + granular * wetGain) * output;
            buffer.setSample(channel, sample,
                !std::isfinite(result) ? 0.0f
                : mix <= 0.0f ? result
                              : juce::jlimit(-32.0f, 32.0f, result));
        }
    }

    auto finalActive = 0;
    for (const auto& voice : voices)
    {
        if (!voice.active)
            continue;
        ++finalActive;
        auto& event = visual[static_cast<size_t>(
            voice.visualSequence
            % static_cast<std::uint32_t>(grainVisualEventCount))];
        if (event.sequence.load(std::memory_order_acquire)
            == voice.visualSequence)
        {
            event.publicationSequence.fetch_add(
                1, std::memory_order_acq_rel);
            event.progress.store(
                static_cast<float>(voice.age)
                    / static_cast<float>(juce::jmax(1, voice.duration)),
                std::memory_order_relaxed);
            event.publicationSequence.fetch_add(
                1, std::memory_order_release);
        }
    }
    activeVoices.store(finalActive, std::memory_order_relaxed);
}

double RandomGranulizerModule::tailSeconds(
    const ControlValues& controls) const
{
    const auto firstSize = exponential(
        50.0f, 2000.0f,
        std::isfinite(controls[1]) ? controls[1] : 0.127f);
    const auto secondSize = exponential(
        50.0f, 2000.0f,
        std::isfinite(controls[4]) ? controls[4] : 0.467f);
    const auto size = juce::jmax(firstSize, secondSize) * 0.001f;
    const auto delay = juce::jlimit(
        0.0f, 1.0f, std::isfinite(controls[7]) ? controls[7] : 0.0f);
    const auto feedback = juce::jlimit(
        0.0f, 0.8f,
        (std::isfinite(controls[9]) ? controls[9] : 0.0f) * 0.8f);
    return static_cast<double>(
        size + delay * 4.0f + size * feedback / (1.0f - feedback) + 0.1f);
}

GrainVisualEvents
RandomGranulizerModule::grainVisualEvents() const noexcept
{
    GrainVisualEvents result {};
    for (size_t index = 0; index < result.size(); ++index)
    {
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            const auto publication =
                visual[index].publicationSequence.load(
                    std::memory_order_acquire);
            if ((publication & 1u) != 0)
                continue;

            GrainVisualEvent candidate;
            candidate.sequence =
                visual[index].sequence.load(std::memory_order_relaxed);
            candidate.historyPosition =
                visual[index].historyPosition.load(std::memory_order_relaxed);
            candidate.durationSeconds =
                visual[index].durationSeconds.load(std::memory_order_relaxed);
            candidate.progress =
                visual[index].progress.load(std::memory_order_relaxed);
            candidate.pan =
                visual[index].pan.load(std::memory_order_relaxed);
            candidate.filter =
                visual[index].filter.load(std::memory_order_relaxed);
            candidate.reverse =
                visual[index].reverse.load(std::memory_order_relaxed);

            if (visual[index].publicationSequence.load(
                    std::memory_order_acquire) == publication)
            {
                result[index] = candidate;
                break;
            }
        }
    }
    return result;
}
} // namespace megadsp
