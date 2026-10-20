#include "EnvelopeConvolution.h"

#include "DspHelpers.h"
#include "DspSafety.h"
#include "EnvelopeModulator.h"

#include <cmath>

namespace megadsp
{
void EnvelopeConvolutionModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    maximumBlockSize = static_cast<int>(juce::jmax(1u, spec.maximumBlockSize));
    preparedChannels = static_cast<int>(juce::jlimit(1u, 2u, spec.numChannels));
    const juce::dsp::ProcessSpec safeSpec {
        sampleRate, static_cast<juce::uint32>(maximumBlockSize),
        static_cast<juce::uint32>(preparedChannels)
    };
    envelopeLowCut.prepare(safeSpec);
    envelopeHighCut.prepare(safeSpec);
    envelopeLowCut.setType(juce::dsp::StateVariableTPTFilterType::highpass);
    envelopeHighCut.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    for (auto& value : smoothed)
        value.reset(sampleRate, 0.03);
    availability.reset(sampleRate, 0.02);
    historyInterval = juce::jmax(1, juce::roundToInt(sampleRate * 0.02));
    ConvolutionReverbModule::prepare(safeSpec);
}

void EnvelopeConvolutionModule::reset()
{
    ConvolutionReverbModule::reset();
    envelopeLowCut.reset();
    envelopeHighCut.reset();
    envelope = 0.0f;
    previousLowCut = previousHighCut = -1.0f;
    historySamples = 0;
    wetEnergy = 0.0;
    initialized = false;
    wasCapturing = false;
    telemetrySequence = 0;
    liveTelemetry = {};
    telemetry.clear();
    availability.setCurrentAndTargetValue(0.0f);
}

bool EnvelopeConvolutionModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}

void EnvelopeConvolutionModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    juce::ScopedNoDenormals noDenormals;
    const auto channels = juce::jmin(preparedChannels, buffer.getNumChannels());
    const auto samples = buffer.getNumSamples();
    if (channels <= 0 || samples <= 0)
        return;

    const auto defaults = moduleDefaults(ModuleType::envelopeConvolution);
    std::array<float, activeControlCount> targets {};
    for (size_t index = 0; index < targets.size(); ++index)
        targets[index] = detail::normalizedControl(controls[index], defaults[index]);
    for (const auto control : { quietLevelControl, loudLevelControl })
        targets[control] = detail::lerp(-72.0f, 0.0f, targets[control]);
    targets[attackControl] = detail::coefficient(
        sampleRate, detail::exponential(1.0f, 200.0f, targets[attackControl]));
    targets[releaseControl] = detail::coefficient(
        sampleRate, detail::exponential(30.0f, 3000.0f, targets[releaseControl]));
    targets[lowCutControl] = detail::exponential(
        20.0f, 1000.0f, targets[lowCutControl]);
    targets[highCutControl] = juce::jmin(
        static_cast<float>(sampleRate * 0.45),
        detail::exponential(2000.0f, 20000.0f, targets[highCutControl]));
    targets[outputControl] = juce::Decibels::decibelsToGain(
        detail::lerp(-18.0f, 18.0f, targets[outputControl]));
    for (size_t index = 0; index < targets.size(); ++index)
    {
        if (initialized)
            smoothed[index].setTargetValue(targets[index]);
        else
            smoothed[index].setCurrentAndTargetValue(targets[index]);
    }
    if (!initialized)
        availability.setCurrentAndTargetValue(hasImpulseResponse() ? 1.0f : 0.0f);
    initialized = true;
    if (environment.captureTelemetry && !wasCapturing)
    {
        liveTelemetry = {};
        historySamples = 0;
        wetEnergy = 0.0;
    }
    wasCapturing = environment.captureTelemetry;

    for (int offset = 0; offset < samples; offset += maximumBlockSize)
    {
        const auto count = juce::jmin(maximumBlockSize, samples - offset);
        std::array<float*, 2> pointers {
            buffer.getWritePointer(0, offset),
            buffer.getWritePointer(channels > 1 ? 1 : 0, offset)
        };
        juce::AudioBuffer<float> input(pointers.data(), channels, count);
        for (int channel = 0; channel < channels; ++channel)
            for (int sample = 0; sample < count; ++sample)
                input.setSample(channel, sample, detail::finiteSample(
                    input.getSample(channel, sample)));
        const auto ready = renderConvolution(input, environment);
        availability.setTargetValue(ready ? 1.0f : 0.0f);
        const auto& wetBuffer = convolvedBuffer();
        for (int sample = 0; sample < count; ++sample)
        {
            std::array<float, activeControlCount> current {};
            for (size_t index = 0; index < current.size(); ++index)
                current[index] = smoothed[index].getNextValue();
            if (!juce::exactlyEqual(current[lowCutControl], previousLowCut))
            {
                previousLowCut = current[lowCutControl];
                envelopeLowCut.setCutoffFrequency(previousLowCut);
            }
            if (!juce::exactlyEqual(current[highCutControl], previousHighCut))
            {
                previousHighCut = current[highCutControl];
                envelopeHighCut.setCutoffFrequency(previousHighCut);
            }
            auto peak = 0.0f;
            for (int channel = 0; channel < channels; ++channel)
                peak = juce::jmax(peak, std::abs(input.getSample(channel, sample)));
            const auto coefficient = peak > envelope
                ? current[attackControl] : current[releaseControl];
            envelope += (peak - envelope) * (1.0f - coefficient);
            const auto level = juce::Decibels::gainToDecibels(envelope, -100.0f);
            const auto thresholds = EnvelopeModulatorModule::orderedThresholds(
                current[quietLevelControl], current[loudLevelControl]);
            const auto position = juce::jlimit(
                0.0f, 1.0f, (level - thresholds[0]) / (thresholds[1] - thresholds[0]));
            const auto blend = detail::lerp(
                current[quietBlendControl], current[loudBlendControl], position)
                * availability.getNextValue();
            for (int channel = 0; channel < preparedChannels; ++channel)
            {
                const auto wet = envelopeHighCut.processSample(
                    channel, envelopeLowCut.processSample(
                        channel, ready ? detail::finiteSample(
                            wetBuffer.getSample(channel, sample)) : 0.0f));
                if (channel < channels)
                {
                    if (environment.captureTelemetry)
                        wetEnergy += static_cast<double>(wet) * wet;
                    const auto dry = input.getSample(channel, sample);
                    input.setSample(channel, sample, detail::finiteSample(
                        (dry * (1.0f - blend) + wet * blend) * current[outputControl]));
                }
            }
            if (environment.captureTelemetry)
            {
                liveTelemetry.values[inputLevelDb] = level;
                liveTelemetry.values[quietThresholdDb] = thresholds[0];
                liveTelemetry.values[loudThresholdDb] = thresholds[1];
                liveTelemetry.values[currentBlend] = blend;
                liveTelemetry.values[envelopePosition] = position;
                liveTelemetry.values[impulseReady] = ready ? 1.0f : 0.0f;
                if (++historySamples >= historyInterval)
                {
                    const auto wetDb = juce::Decibels::gainToDecibels(
                        static_cast<float>(std::sqrt(
                            wetEnergy / static_cast<double>(historySamples * channels))),
                        -100.0f);
                    liveTelemetry.values[wetLevelDb] = wetDb;
                    appendContinuousTelemetryHistory(
                        liveTelemetry, { level, blend, wetDb, 0.0f }, 3);
                    historySamples = 0;
                    wetEnergy = 0.0;
                }
            }
        }
    }
    if (environment.captureTelemetry)
    {
        liveTelemetry.valueCount = telemetryValueCount;
        liveTelemetry.sequence = ++telemetrySequence;
        telemetry.publish(liveTelemetry);
    }
}
} // namespace megadsp
