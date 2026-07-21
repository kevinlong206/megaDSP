#include "Delay.h"
#include "DspHelpers.h"

#include <cmath>

namespace megadsp
{
using detail::exponential;

void DelayModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    const auto capacity = static_cast<size_t>(std::ceil(sampleRate * 6.1));
    for (auto& channel : delayBuffer)
        channel.assign(capacity, 0.0f);
    reset();
}

void DelayModule::reset()
{
    for (auto& channel : delayBuffer)
        std::fill(channel.begin(), channel.end(), 0.0f);
    filterState.fill(0.0f);
    writePosition = 0;
    phase = 0.0f;
    delaySamplesSmoothed.reset(sampleRate, 0.03);
    delaySamplesSmoothed.setCurrentAndTargetValue(
        static_cast<float>(sampleRate * 0.25));
}

void DelayModule::process(juce::AudioBuffer<float>& buffer,
                          const ControlValues& controls,
                          const ProcessEnvironment& environment)
{
    if (delayBuffer[0].empty())
        return;

    const auto freeTimeMs = exponential(1.0f, 2000.0f, controls[0]);
    constexpr std::array<float, 8> beatDivisions {
        0.125f, 0.25f, 0.375f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f
    };
    const auto divisionIndex = discreteIndex(
        controls[6], static_cast<int>(beatDivisions.size()));
    const auto syncedMs = 60000.0f / static_cast<float>(juce::jmax(20.0, environment.bpm))
                          * beatDivisions[static_cast<size_t>(divisionIndex)];
    const auto baseTimeMs = controls[5] >= 0.5f ? syncedMs : freeTimeMs;
    const auto feedback = juce::jmin(0.95f, controls[1] * 0.95f);
    const auto mix = controls[2];
    const auto toneHz = exponential(800.0f, 20000.0f, controls[3]);
    const bool pingPong = controls[4] >= 0.5f && buffer.getNumChannels() > 1;
    const auto modRate = exponential(0.05f, 8.0f, controls[7]);
    const auto modDepthMs = controls[8] * 10.0f;
    const auto filterCoefficient = std::exp(
        -juce::MathConstants<float>::twoPi * toneHz / static_cast<float>(sampleRate));
    const auto capacity = static_cast<int>(delayBuffer[0].size());

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto modulation = std::sin(phase) * modDepthMs;
        phase += juce::MathConstants<float>::twoPi * modRate
                 / static_cast<float>(sampleRate);
        if (phase >= juce::MathConstants<float>::twoPi)
            phase -= juce::MathConstants<float>::twoPi;

        const auto targetDelaySamples = juce::jlimit(
            1.0f, static_cast<float>(capacity - 2),
            (baseTimeMs + modulation) * 0.001f * static_cast<float>(sampleRate));
        delaySamplesSmoothed.setTargetValue(targetDelaySamples);
        const auto delaySamples = delaySamplesSmoothed.getNextValue();
        auto readPosition = static_cast<float>(writePosition) - delaySamples;
        while (readPosition < 0.0f)
            readPosition += static_cast<float>(capacity);
        const auto readIndexA = static_cast<int>(readPosition);
        const auto readIndexB = (readIndexA + 1) % capacity;
        const auto fraction = readPosition - static_cast<float>(readIndexA);

        std::array<float, 2> delayed {};
        std::array<float, 2> dry {
            buffer.getSample(0, sample),
            buffer.getNumChannels() > 1 ? buffer.getSample(1, sample)
                                        : buffer.getSample(0, sample)
        };
        for (int channel = 0; channel < juce::jmin(2, buffer.getNumChannels()); ++channel)
        {
            const auto a = delayBuffer[static_cast<size_t>(channel)][
                static_cast<size_t>(readIndexA)];
            const auto b = delayBuffer[static_cast<size_t>(channel)][
                static_cast<size_t>(readIndexB)];
            delayed[static_cast<size_t>(channel)] = a + (b - a) * fraction;
        }

        const std::array<float, 2> injection {
            pingPong ? (dry[0] + dry[1]) * 0.5f : dry[0],
            pingPong ? 0.0f : dry[1]
        };
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto index = static_cast<size_t>(juce::jmin(channel, 1));
            auto* samples = buffer.getWritePointer(channel);
            const auto feedbackSource = pingPong
                                            ? delayed[static_cast<size_t>(1 - channel)]
                                            : delayed[index];
            auto& filtered = filterState[index];
            filtered = filterCoefficient * filtered
                       + (1.0f - filterCoefficient) * feedbackSource;
            delayBuffer[index][static_cast<size_t>(writePosition)] =
                injection[index] + filtered * feedback;
            samples[sample] = dry[index] + (delayed[index] - dry[index]) * mix;
        }

        if (++writePosition >= capacity)
            writePosition = 0;
    }
}

double DelayModule::tailSeconds(const ControlValues& controls) const
{
    const auto feedback = juce::jmin(0.95f, controls[1] * 0.95f);
    if (feedback < 0.001f)
        return 6.1;
    return 6.1 * std::log(0.001) / std::log(static_cast<double>(feedback));
}
} // namespace megadsp
