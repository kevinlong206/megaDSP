#include "Limiter.h"
#include "DspHelpers.h"

#include <cmath>

namespace megadsp
{
using detail::coefficient;
using detail::exponential;
using detail::lerp;

void LimiterModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    maximumLookaheadSamples = juce::jmax(
        1, static_cast<int>(std::ceil(sampleRate * 0.01)));
    activeLookaheadSamples.store(maximumLookaheadSamples);
    for (auto& channel : delayBuffer)
        channel.assign(static_cast<size_t>(maximumLookaheadSamples), 0.0f);
    gainBuffer.assign(static_cast<size_t>(maximumLookaheadSamples), 1.0f);
    reset();
}

void LimiterModule::reset()
{
    for (auto& channel : delayBuffer)
        std::fill(channel.begin(), channel.end(), 0.0f);
    writePosition = 0;
    gain = 1.0f;
    std::fill(gainBuffer.begin(), gainBuffer.end(), 1.0f);
    autoGain = 1.0f;
    gainReductionDb.store(0.0f);
}

void LimiterModule::setLookaheadControl(float normalizedValue)
{
    const auto lookahead = juce::jlimit(
        1, maximumLookaheadSamples,
        juce::roundToInt(static_cast<float>(sampleRate)
                         * lerp(0.001f, 0.01f, normalizedValue)));
    activeLookaheadSamples.store(lookahead);
}

void LimiterModule::process(juce::AudioBuffer<float>& buffer,
                            const ControlValues& controls,
                            const ProcessEnvironment&)
{
    if (delayBuffer[0].empty())
        return;

    const auto threshold = juce::Decibels::decibelsToGain(
        lerp(-24.0f, 0.0f, controls[0]));
    const auto ceiling = juce::Decibels::decibelsToGain(
        lerp(-12.0f, 0.0f, controls[1]));
    const auto releaseCoefficient = coefficient(
        sampleRate, exponential(10.0f, 1000.0f, controls[2]));
    setLookaheadControl(controls[3]);
    const auto lookahead = activeLookaheadSamples.load();
    const auto inputDrive = ceiling / juce::jmax(threshold, 0.000001f);
    const auto targetAutoGain = controls[4] >= 0.5f
                                    ? threshold / juce::jmax(ceiling, 0.000001f)
                                    : 1.0f;
    const auto autoGainCoefficient = coefficient(sampleRate, 200.0f);
    float minimumGain = 1.0f;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        autoGain = autoGainCoefficient * autoGain
                   + (1.0f - autoGainCoefficient) * targetAutoGain;
        float peak = 0.0f;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            peak = juce::jmax(peak, std::abs(
                buffer.getSample(channel, sample) * inputDrive));

        const auto targetGain = peak > ceiling && peak > 0.0f
                                    ? juce::jmin(1.0f, ceiling / peak)
                                    : 1.0f;
        gain = targetGain < gain
                   ? targetGain
                   : releaseCoefficient * gain + (1.0f - releaseCoefficient);
        minimumGain = juce::jmin(minimumGain, gain);
        auto gainReadPosition = writePosition
                                - (maximumLookaheadSamples - lookahead);
        if (gainReadPosition < 0)
            gainReadPosition += maximumLookaheadSamples;
        const auto delayedGain = gainBuffer[static_cast<size_t>(gainReadPosition)];
        gainBuffer[static_cast<size_t>(writePosition)] = gain;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto index = static_cast<size_t>(juce::jmin(channel, 1));
            auto* samples = buffer.getWritePointer(channel);
            const auto delayed = delayBuffer[index][static_cast<size_t>(writePosition)];
            delayBuffer[index][static_cast<size_t>(writePosition)] = samples[sample];
            const auto limited = juce::jlimit(
                -ceiling, ceiling,
                delayed * inputDrive
                    * delayedGain);
            samples[sample] = juce::jlimit(
                -ceiling, ceiling, limited * autoGain);
        }

        if (++writePosition >= maximumLookaheadSamples)
            writePosition = 0;
    }

    gainReductionDb.store(-juce::Decibels::gainToDecibels(minimumGain, -100.0f));
}
} // namespace megadsp
