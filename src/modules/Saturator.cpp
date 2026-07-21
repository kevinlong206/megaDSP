#include "Saturator.h"
#include "DspHelpers.h"

#include <cmath>

namespace megadsp
{
using detail::exponential;
using detail::lerp;
using detail::softClip;

void SaturatorModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    oversampling = std::make_unique<juce::dsp::Oversampling<float>>(
        static_cast<size_t>(spec.numChannels), 1,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    oversampling->initProcessing(spec.maximumBlockSize);
    oversamplingLatency = juce::roundToInt(oversampling->getLatencyInSamples());
    dryBuffer.setSize(static_cast<int>(spec.numChannels),
                      static_cast<int>(spec.maximumBlockSize), false, true, true);
    for (auto& channel : dryLatencyBuffer)
        channel.assign(static_cast<size_t>(juce::jmax(1, oversamplingLatency)), 0.0f);
    reset();
}

void SaturatorModule::reset()
{
    toneState.fill(0.0f);
    if (oversampling != nullptr)
        oversampling->reset();
    for (auto& channel : dryLatencyBuffer)
        std::fill(channel.begin(), channel.end(), 0.0f);
    dryLatencyPosition = 0;
    inputMeanSquare = 1.0e-8f;
    wetMeanSquare = 1.0e-8f;
    compensationGain = 1.0f;
}

void SaturatorModule::process(juce::AudioBuffer<float>& buffer,
                              const ControlValues& controls,
                              const ProcessEnvironment&)
{
    if (oversampling == nullptr)
        return;

    const auto driveDb = lerp(0.0f, 36.0f, controls[0]);
    const auto drive = juce::Decibels::decibelsToGain(driveDb);
    const auto toneHz = exponential(500.0f, 20000.0f, controls[1]);
    const auto bias = lerp(-0.35f, 0.35f, controls[2]);
    const auto output = juce::Decibels::decibelsToGain(lerp(-24.0f, 6.0f, controls[3]));
    const auto mix = controls[4];
    const auto mode = discreteIndex(controls[5], 3);
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        dryBuffer.copyFrom(channel, 0, buffer, channel, 0, buffer.getNumSamples());

    juce::dsp::AudioBlock<float> inputBlock(buffer);
    auto upsampled = oversampling->processSamplesUp(inputBlock);
    const auto processingRate = sampleRate * 2.0;
    const auto lowPass = std::exp(
        -juce::MathConstants<float>::twoPi * toneHz / static_cast<float>(processingRate));

    for (size_t channel = 0; channel < upsampled.getNumChannels(); ++channel)
    {
        auto* samples = upsampled.getChannelPointer(channel);
        auto& state = toneState[static_cast<size_t>(juce::jmin(
            static_cast<int>(channel), 1))];
        for (size_t sample = 0; sample < upsampled.getNumSamples(); ++sample)
        {
            auto wet = softClip(samples[sample] * drive + bias, mode)
                       - softClip(bias, mode);
            state = lowPass * state + (1.0f - lowPass) * wet;
            samples[sample] = state;
        }
    }
    oversampling->processSamplesDown(inputBlock);

    double blockInputEnergy = 0.0;
    double blockWetEnergy = 0.0;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto index = static_cast<size_t>(juce::jmin(channel, 1));
            auto& delayedDry = dryLatencyBuffer[index][
                static_cast<size_t>(dryLatencyPosition)];
            const auto dry = delayedDry;
            delayedDry = dryBuffer.getSample(channel, sample);
            dryBuffer.setSample(channel, sample, dry);
            const auto wet = buffer.getSample(channel, sample);
            blockInputEnergy += static_cast<double>(dry) * dry;
            blockWetEnergy += static_cast<double>(wet) * wet;
        }
        if (++dryLatencyPosition >= juce::jmax(1, oversamplingLatency))
            dryLatencyPosition = 0;
    }

    const auto measuredSamples = juce::jmax(
        1, buffer.getNumSamples() * buffer.getNumChannels());
    const auto detectorRetention = std::exp(
        -static_cast<float>(buffer.getNumSamples())
        / static_cast<float>(sampleRate * 0.12));
    inputMeanSquare = detectorRetention * inputMeanSquare
        + (1.0f - detectorRetention)
            * static_cast<float>(blockInputEnergy / measuredSamples);
    wetMeanSquare = detectorRetention * wetMeanSquare
        + (1.0f - detectorRetention)
            * static_cast<float>(blockWetEnergy / measuredSamples);

    auto targetCompensation = 1.0f;
    if (inputMeanSquare > 1.0e-9f && wetMeanSquare > inputMeanSquare)
        targetCompensation = juce::jlimit(
            0.05f, 1.0f, std::sqrt(inputMeanSquare / wetMeanSquare));

    const auto attack = std::exp(-1.0f / static_cast<float>(sampleRate * 0.04));
    const auto release = std::exp(-1.0f / static_cast<float>(sampleRate * 0.6));
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto coefficient =
            targetCompensation < compensationGain ? attack : release;
        compensationGain = coefficient * compensationGain
                           + (1.0f - coefficient) * targetCompensation;
        const auto wetGain = compensationGain * output;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto dry = dryBuffer.getSample(channel, sample);
            const auto wet = buffer.getSample(channel, sample) * wetGain;
            buffer.setSample(channel, sample, dry + (wet - dry) * mix);
        }
    }
}
} // namespace megadsp
