#include "VolumeSwell.h"
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

float VolumeSwellModule::envelopeCoefficient(
    double rate, float milliseconds) noexcept
{
    return std::exp(
        -1.0f / juce::jmax(
            1.0f, milliseconds * 0.001f * static_cast<float>(rate)));
}

float VolumeSwellModule::curveExponent(float normalized) noexcept
{
    const auto safe = juce::jlimit(0.0f, 1.0f, normalized);
    return safe < 0.5f
        ? lerp(0.35f, 1.0f, safe * 2.0f)
        : lerp(1.0f, 3.0f, (safe - 0.5f) * 2.0f);
}

void VolumeSwellModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    lookaheadSamples = juce::jmax(
        1, juce::roundToInt(sampleRate * 0.030));
    duckLengthSamples = juce::jmax(
        1, juce::roundToInt(sampleRate * 0.007));
    const auto delaySize = static_cast<size_t>(lookaheadSamples + 1);
    for (auto& channel : delay)
        channel.assign(delaySize, 0.0f);
    timeSmoothed.reset(sampleRate, 0.04);
    curveSmoothed.reset(sampleRate, 0.04);
    floorSmoothed.reset(sampleRate, 0.04);
    mixSmoothed.reset(sampleRate, 0.04);
    outputSmoothed.reset(sampleRate, 0.04);
    fastCoefficient = envelopeCoefficient(sampleRate, 7.0f);
    slowCoefficient = envelopeCoefficient(sampleRate, 350.0f);
    reset();
}

void VolumeSwellModule::reset()
{
    for (auto& channel : delay)
        std::fill(channel.begin(), channel.end(), 0.0f);
    fastEnergy = 0.0f;
    slowEnergy = 0.0f;
    envelopeGain = 1.0f;
    duckStartGain = 1.0f;
    progress = 1.0f;
    writePosition = 0;
    duckSamplesRemaining = 0;
    samplesSinceTrigger = juce::roundToInt(sampleRate);
    onsetArmed = true;
    swelling = false;
    initialized = false;
    telemetrySequence = 0;
    telemetry.clear();
}

void VolumeSwellModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    const auto channels = juce::jlimit(0, 2, buffer.getNumChannels());
    if (channels == 0 || buffer.getNumSamples() <= 0)
        return;

    const auto sensitivity =
        detail::normalizedControl(controls[0], 0.75f);
    const auto swellSeconds = exponential(
        0.05f, 5.0f, detail::normalizedControl(controls[1], 0.65f));
    const auto curve =
        detail::normalizedControl(controls[2], 0.62f);
    const auto floorGain = juce::Decibels::decibelsToGain(
        lerp(-60.0f, -6.0f, detail::normalizedControl(controls[3], 0.22f)));
    const auto mix = detail::normalizedControl(controls[4], 1.0f);
    const auto outputGain = juce::Decibels::decibelsToGain(
        lerp(-18.0f, 12.0f, detail::normalizedControl(controls[5], 0.6f)));
    timeSmoothed.setTargetValue(swellSeconds);
    curveSmoothed.setTargetValue(curve);
    floorSmoothed.setTargetValue(floorGain);
    mixSmoothed.setTargetValue(mix);
    outputSmoothed.setTargetValue(outputGain);
    if (!initialized)
    {
        timeSmoothed.setCurrentAndTargetValue(swellSeconds);
        curveSmoothed.setCurrentAndTargetValue(curve);
        floorSmoothed.setCurrentAndTargetValue(floorGain);
        mixSmoothed.setCurrentAndTargetValue(mix);
        outputSmoothed.setCurrentAndTargetValue(outputGain);
        initialized = true;
    }

    const auto requiredInput = juce::Decibels::decibelsToGain(
        lerp(-24.0f, -60.0f, sensitivity));
    const auto requiredOnset = lerp(2.0f, 1.35f, sensitivity);
    const auto refractorySamples = juce::roundToInt(sampleRate * 0.45);
    const auto delaySize = static_cast<int>(delay[0].size());
    float latestOnset = 0.0f;
    bool triggeredThisBlock = false;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        std::array<float, 2> input {
            detail::finiteSample(buffer.getSample(0, sample)),
            channels > 1
                ? detail::finiteSample(buffer.getSample(1, sample))
                : detail::finiteSample(buffer.getSample(0, sample))
        };
        const auto inputLevel =
            juce::jmax(std::abs(input[0]), std::abs(input[1]));
        const auto inputEnergy = inputLevel * inputLevel;
        fastEnergy = fastCoefficient * fastEnergy
            + (1.0f - fastCoefficient) * inputEnergy;
        slowEnergy = slowCoefficient * slowEnergy
            + (1.0f - slowCoefficient) * inputEnergy;
        const auto fastEnvelope = std::sqrt(fastEnergy);
        const auto slowEnvelope = std::sqrt(slowEnergy);
        const auto onset = fastEnvelope / (slowEnvelope + 0.004f);
        latestOnset = onset;
        ++samplesSinceTrigger;

        if (!onsetArmed && samplesSinceTrigger >= refractorySamples
            && onset < 1.12f)
            onsetArmed = true;
        if (onsetArmed && inputLevel >= requiredInput
            && onset >= requiredOnset
            && samplesSinceTrigger >= refractorySamples)
        {
            duckStartGain = envelopeGain;
            duckSamplesRemaining = duckLengthSamples;
            progress = 0.0f;
            swelling = true;
            samplesSinceTrigger = 0;
            onsetArmed = false;
            triggeredThisBlock = true;
        }

        const auto readPosition = (writePosition + 1) % delaySize;
        std::array<float, 2> delayed {};
        for (int channel = 0; channel < 2; ++channel)
        {
            auto& channelDelay = delay[static_cast<size_t>(channel)];
            delayed[static_cast<size_t>(channel)] =
                channelDelay[static_cast<size_t>(readPosition)];
            channelDelay[static_cast<size_t>(writePosition)] =
                input[static_cast<size_t>(channel)];
        }
        writePosition = readPosition;

        const auto currentFloor = floorSmoothed.getNextValue();
        if (duckSamplesRemaining > 0)
        {
            const auto phase = 1.0f
                - static_cast<float>(duckSamplesRemaining)
                    / static_cast<float>(duckLengthSamples);
            const auto shaped = phase * phase * (3.0f - 2.0f * phase);
            envelopeGain = lerp(duckStartGain, currentFloor, shaped);
            --duckSamplesRemaining;
            if (duckSamplesRemaining == 0)
                envelopeGain = currentFloor;
        }
        else if (swelling)
        {
            const auto durationSamples = juce::jmax(
                1.0f, timeSmoothed.getNextValue()
                    * static_cast<float>(sampleRate));
            progress = juce::jmin(1.0f, progress + 1.0f / durationSamples);
            envelopeGain = lerp(
                currentFloor, 1.0f,
                std::pow(progress, curveExponent(curveSmoothed.getNextValue())));
            if (progress >= 1.0f)
            {
                envelopeGain = 1.0f;
                swelling = false;
            }
        }
        else
        {
            timeSmoothed.getNextValue();
            curveSmoothed.getNextValue();
            envelopeGain = 1.0f;
        }

        const auto currentMix = mixSmoothed.getNextValue();
        const auto currentOutput = outputSmoothed.getNextValue();
        const auto gain = lerp(1.0f, envelopeGain, currentMix) * currentOutput;
        for (int channel = 0; channel < channels; ++channel)
            buffer.setSample(
                channel, sample,
                detail::finiteSample(
                    delayed[static_cast<size_t>(channel)] * gain));
    }

    if (environment.captureTelemetry)
    {
        ContinuousTelemetrySnapshot snapshot;
        snapshot.sequence = ++telemetrySequence;
        snapshot.valueCount = telemetryValueCount;
        snapshot.values[inputEnvelope] = std::sqrt(fastEnergy);
        snapshot.values[inputThreshold] = requiredInput;
        snapshot.values[onsetStrength] = latestOnset;
        snapshot.values[onsetThreshold] = requiredOnset;
        snapshot.values[swellProgress] = progress;
        snapshot.values[swellGain] = envelopeGain;
        snapshot.values[triggered] = triggeredThisBlock ? 1.0f : 0.0f;
        telemetry.publish(snapshot);
    }
}

bool VolumeSwellModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}
} // namespace megadsp
