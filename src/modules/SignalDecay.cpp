#include "SignalDecay.h"
#include "DspHelpers.h"
#include "DspSafety.h"

#include <cmath>

namespace megadsp
{
namespace
{
using detail::exponential;
using detail::lerp;
}

void SignalDecayModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    fixedLatencySamples = juce::jmax(8, juce::roundToInt(sampleRate * 0.020));
    const auto capacity = static_cast<size_t>(
        fixedLatencySamples + juce::roundToInt(sampleRate * 0.005) + 16);
    for (auto& history : wetHistory)
        history.assign(capacity, 0.0f);
    for (auto& history : dryHistory)
        history.assign(static_cast<size_t>(fixedLatencySamples), 0.0f);
    for (auto* smoother : { &resolutionSmoothed, &jitterSmoothed,
                            &dropoutSmoothed, &wowSmoothed, &flutterSmoothed,
                            &stereoWearSmoothed, &mixSmoothed })
        smoother->reset(sampleRate, 0.04);
    for (auto* smoother : { &sampleRateSmoothed, &bandwidthSmoothed,
                            &noiseSmoothed, &outputSmoothed })
        smoother->reset(sampleRate, 0.04);
    reset();
}

void SignalDecayModule::reset()
{
    for (auto& history : wetHistory)
        std::fill(history.begin(), history.end(), 0.0f);
    for (auto& history : dryHistory)
        std::fill(history.begin(), history.end(), 0.0f);
    heldSample.fill(0.0f);
    bandwidthState.fill(0.0f);
    noiseState.fill(0.0f);
    clockPhase.fill(1.0f);
    jitterState.fill(0.0f);
    dropoutGain.fill(1.0f);
    dropoutRemaining.fill(0);
    dropoutLength.fill(0);
    randomState = 0x9e3779b9u;
    wowPhase = 0.0;
    flutterPhase = 0.0;
    writePosition = 0;
    dryPosition = 0;
    initialized = false;
    telemetrySequence = 0;
    telemetryEventSequence = 0;
    telemetry.clear();
    eventTelemetry.clear();
}

float SignalDecayModule::randomBipolar() noexcept
{
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return static_cast<float>(randomState & 0x00ffffffu)
               / static_cast<float>(0x00800000u) - 1.0f;
}

float SignalDecayModule::readDelay(int channel, float delaySamples) const noexcept
{
    const auto& history = wetHistory[static_cast<size_t>(channel)];
    const auto size = static_cast<int>(history.size());
    auto position = static_cast<float>(writePosition)
                    - juce::jlimit(2.0f, static_cast<float>(size - 3),
                                   delaySamples);
    while (position < 0.0f)
        position += static_cast<float>(size);
    const auto index = static_cast<int>(position);
    const auto fraction = position - static_cast<float>(index);
    const auto at = [&history, size](int positionToRead)
    {
        while (positionToRead < 0)
            positionToRead += size;
        return history[static_cast<size_t>(positionToRead % size)];
    };
    const auto y0 = at(index - 1);
    const auto y1 = at(index);
    const auto y2 = at(index + 1);
    const auto y3 = at(index + 2);
    const auto a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    const auto a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const auto a2 = -0.5f * y0 + 0.5f * y2;
    return ((a0 * fraction + a1) * fraction + a2) * fraction + y1;
}

void SignalDecayModule::process(juce::AudioBuffer<float>& buffer,
                                const ControlValues& controls,
                                const ProcessEnvironment& environment)
{
    const auto channels = juce::jlimit(0, 2, buffer.getNumChannels());
    if (channels == 0 || wetHistory[0].empty())
        return;

    const auto resolution = lerp(4.0f, 24.0f,
        detail::normalizedControl(controls[0], 0.6f));
    const auto degradedRate = exponential(1000.0f, 48000.0f,
        detail::normalizedControl(controls[1], 0.89f));
    const auto jitter = detail::normalizedControl(controls[2], 0.08f);
    const auto dropouts = detail::normalizedControl(controls[3], 0.05f);
    const auto bandwidth = exponential(1000.0f, 20000.0f,
        detail::normalizedControl(controls[4], 0.88f));
    const auto noiseGain = juce::Decibels::decibelsToGain(
        lerp(-90.0f, -24.0f,
             detail::normalizedControl(controls[5], 0.27f)));
    const auto wow = detail::normalizedControl(controls[6], 0.08f);
    const auto flutter = detail::normalizedControl(controls[7], 0.06f);
    const auto stereoWear = detail::normalizedControl(controls[8], 0.1f);
    const auto mix = detail::normalizedControl(controls[9], 1.0f);
    const auto output = juce::Decibels::decibelsToGain(
        lerp(-18.0f, 12.0f,
             detail::normalizedControl(controls[10], 0.6f)));

    if (!initialized)
    {
        resolutionSmoothed.setCurrentAndTargetValue(resolution);
        sampleRateSmoothed.setCurrentAndTargetValue(degradedRate);
        jitterSmoothed.setCurrentAndTargetValue(jitter);
        dropoutSmoothed.setCurrentAndTargetValue(dropouts);
        bandwidthSmoothed.setCurrentAndTargetValue(bandwidth);
        noiseSmoothed.setCurrentAndTargetValue(noiseGain);
        wowSmoothed.setCurrentAndTargetValue(wow);
        flutterSmoothed.setCurrentAndTargetValue(flutter);
        stereoWearSmoothed.setCurrentAndTargetValue(stereoWear);
        mixSmoothed.setCurrentAndTargetValue(mix);
        outputSmoothed.setCurrentAndTargetValue(output);
        initialized = true;
    }
    resolutionSmoothed.setTargetValue(resolution);
    sampleRateSmoothed.setTargetValue(degradedRate);
    jitterSmoothed.setTargetValue(jitter);
    dropoutSmoothed.setTargetValue(dropouts);
    bandwidthSmoothed.setTargetValue(bandwidth);
    noiseSmoothed.setTargetValue(noiseGain);
    wowSmoothed.setTargetValue(wow);
    flutterSmoothed.setTargetValue(flutter);
    stereoWearSmoothed.setTargetValue(stereoWear);
    mixSmoothed.setTargetValue(mix);
    outputSmoothed.setTargetValue(output);

    EventTelemetrySnapshot events;
    float telemetryWear = stereoWear;
    float telemetryRate = degradedRate;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const std::array<float, 2> dry {
            detail::finiteSample(buffer.getSample(0, sample)),
            channels > 1
                ? detail::finiteSample(buffer.getSample(1, sample))
                : detail::finiteSample(buffer.getSample(0, sample))
        };
        const auto currentResolution = resolutionSmoothed.getNextValue();
        const auto levels = std::exp2(currentResolution - 1.0f) - 1.0f;
        const auto currentRate = juce::jmin(
            static_cast<float>(sampleRate),
            sampleRateSmoothed.getNextValue());
        const auto currentJitter = jitterSmoothed.getNextValue();
        const auto currentDropouts = dropoutSmoothed.getNextValue();
        const auto currentBandwidth = juce::jmin(
            static_cast<float>(sampleRate * 0.45),
            bandwidthSmoothed.getNextValue());
        const auto currentNoise = noiseSmoothed.getNextValue();
        const auto currentWear = stereoWearSmoothed.getNextValue();
        telemetryWear = currentWear;
        telemetryRate = currentRate;
        const auto bandwidthCoefficient = 1.0f - std::exp(
            -juce::MathConstants<float>::twoPi * currentBandwidth
            / static_cast<float>(sampleRate));

        const auto commonJitter = randomBipolar();
        const auto commonDitherA = randomBipolar();
        const auto commonDitherB = randomBipolar();
        const auto commonNoise = randomBipolar();
        const auto commonDropout = randomBipolar() * 0.5f + 0.5f;
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto independentJitter = randomBipolar();
            jitterState[static_cast<size_t>(channel)] += 0.02f
                * ((commonJitter + (independentJitter - commonJitter)
                                     * currentWear)
                   - jitterState[static_cast<size_t>(channel)]);
            const auto clockScale = juce::jlimit(
                0.72f, 1.28f,
                1.0f + jitterState[static_cast<size_t>(channel)]
                           * currentJitter * 0.22f);
            clockPhase[static_cast<size_t>(channel)] +=
                currentRate / static_cast<float>(sampleRate) * clockScale;
            if (clockPhase[static_cast<size_t>(channel)] >= 1.0f)
            {
                clockPhase[static_cast<size_t>(channel)] -= 1.0f;
                const auto independentA = randomBipolar();
                const auto independentB = randomBipolar();
                const auto ditherA = commonDitherA
                    + (independentA - commonDitherA) * currentWear;
                const auto ditherB = commonDitherB
                    + (independentB - commonDitherB) * currentWear;
                const auto dither = (ditherA - ditherB) * 0.5f / levels;
                heldSample[static_cast<size_t>(channel)] =
                    std::round(juce::jlimit(-1.0f, 1.0f,
                                           dry[static_cast<size_t>(channel)]
                                               + dither)
                               * levels)
                    / levels;

                const auto eventsPerSecond = currentDropouts
                    * currentDropouts * 7.5f;
                const auto independentDropout =
                    randomBipolar() * 0.5f + 0.5f;
                const auto eventValue = commonDropout
                    + (independentDropout - commonDropout) * currentWear;
                if (dropoutRemaining[static_cast<size_t>(channel)] <= 0
                    && eventValue
                           < eventsPerSecond / juce::jmax(1.0f, currentRate))
                {
                    const auto length = juce::jlimit(
                        16, juce::roundToInt(sampleRate * 0.12),
                        juce::roundToInt(sampleRate
                            * lerp(0.006f, 0.08f, currentDropouts)));
                    dropoutRemaining[static_cast<size_t>(channel)] = length;
                    dropoutLength[static_cast<size_t>(channel)] = length;
                    if (environment.captureTelemetry
                        && events.eventCount < eventTelemetryCapacity)
                    {
                        auto& event = events.events[events.eventCount++];
                        event.sequence = ++telemetryEventSequence;
                        event.kind = dropoutStarted;
                        event.flags = 1u << static_cast<unsigned>(channel);
                        event.position[0] = static_cast<float>(channel);
                        event.values[0] =
                            static_cast<float>(length / sampleRate);
                        event.values[1] = currentDropouts;
                        event.values[2] = currentWear;
                    }
                }
            }

            auto targetDropout = 1.0f;
            auto& remaining = dropoutRemaining[static_cast<size_t>(channel)];
            if (remaining > 0)
            {
                const auto length = dropoutLength[static_cast<size_t>(channel)];
                const auto progress = 1.0f
                    - static_cast<float>(remaining) / static_cast<float>(length);
                const auto window = std::sin(
                    juce::MathConstants<float>::pi * progress);
                targetDropout = 1.0f
                    - window * lerp(0.12f, 0.96f, currentDropouts);
                --remaining;
            }
            dropoutGain[static_cast<size_t>(channel)] += 0.08f
                * (targetDropout - dropoutGain[static_cast<size_t>(channel)]);

            const auto independentNoise = randomBipolar();
            const auto noise = commonNoise
                + (independentNoise - commonNoise) * currentWear;
            noiseState[static_cast<size_t>(channel)] += 0.18f
                * (noise - noiseState[static_cast<size_t>(channel)]);
            auto degraded = heldSample[static_cast<size_t>(channel)]
                            * dropoutGain[static_cast<size_t>(channel)]
                            + noiseState[static_cast<size_t>(channel)]
                                  * currentNoise;
            bandwidthState[static_cast<size_t>(channel)] +=
                bandwidthCoefficient
                * (degraded - bandwidthState[static_cast<size_t>(channel)]);
            degraded = bandwidthState[static_cast<size_t>(channel)];
            wetHistory[static_cast<size_t>(channel)]
                      [static_cast<size_t>(writePosition)] =
                std::isfinite(degraded) ? degraded : 0.0f;
        }

        wowPhase += 0.42 / sampleRate;
        flutterPhase += 7.3 / sampleRate;
        wowPhase -= std::floor(wowPhase);
        flutterPhase -= std::floor(flutterPhase);
        const auto wowValue = std::sin(
            juce::MathConstants<double>::twoPi * wowPhase);
        const auto flutterValue = std::sin(
            juce::MathConstants<double>::twoPi * flutterPhase);
        const auto wowDepth = wowSmoothed.getNextValue()
                              * static_cast<float>(sampleRate * 0.0035);
        const auto flutterDepth = flutterSmoothed.getNextValue()
                                  * static_cast<float>(sampleRate * 0.0007);
        const auto wearOffset = currentWear
                                * static_cast<float>(sampleRate * 0.00018);
        const auto currentMix = mixSmoothed.getNextValue();
        const auto currentOutput = outputSmoothed.getNextValue();
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto polarity = channel == 0 ? -1.0f : 1.0f;
            const auto modulation = wowDepth * static_cast<float>(wowValue)
                + flutterDepth * static_cast<float>(flutterValue)
                + wearOffset * polarity
                      * static_cast<float>(0.7 * wowValue
                                           + 0.3 * flutterValue);
            const auto wet = readDelay(
                channel, juce::jlimit(
                    3.0f,
                    static_cast<float>(wetHistory[0].size() - 3),
                    static_cast<float>(fixedLatencySamples) + modulation));
            auto& dryDelay = dryHistory[static_cast<size_t>(channel)];
            const auto delayedDry = dryDelay[static_cast<size_t>(dryPosition)];
            dryDelay[static_cast<size_t>(dryPosition)] =
                dry[static_cast<size_t>(channel)];
            buffer.setSample(
                channel, sample,
                detail::finiteSample(
                    (delayedDry + (wet - delayedDry) * currentMix)
                    * currentOutput));
        }
        writePosition = (writePosition + 1)
                        % static_cast<int>(wetHistory[0].size());
        dryPosition = (dryPosition + 1) % fixedLatencySamples;
    }

    if (environment.captureTelemetry)
    {
        ContinuousTelemetrySnapshot snapshot;
        snapshot.sequence = ++telemetrySequence;
        snapshot.valueCount = telemetryValueCount;
        snapshot.values[leftDropoutGain] = dropoutGain[0];
        snapshot.values[rightDropoutGain] =
            channels > 1 ? dropoutGain[1] : dropoutGain[0];
        snapshot.values[leftClockPhase] = clockPhase[0];
        snapshot.values[rightClockPhase] =
            channels > 1 ? clockPhase[1] : clockPhase[0];
        snapshot.values[leftClockJitter] = jitterState[0];
        snapshot.values[rightClockJitter] =
            channels > 1 ? jitterState[1] : jitterState[0];
        snapshot.values[stereoWearAmount] = telemetryWear;
        snapshot.values[currentSampleRate] = telemetryRate;
        telemetry.publish(snapshot);
        events.sequence = telemetrySequence;
        eventTelemetry.publish(events);
    }
}

bool SignalDecayModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}

bool SignalDecayModule::readEventTelemetry(
    EventTelemetrySnapshot& snapshot) const noexcept
{
    return eventTelemetry.read(snapshot);
}

double SignalDecayModule::tailSeconds(const ControlValues&) const
{
    return static_cast<double>(fixedLatencySamples) / sampleRate;
}
} // namespace megadsp
