#include "Equalizer.h"
#include "DspHelpers.h"

#include <cmath>

namespace megadsp
{
using detail::exponential;
using detail::lerp;

void EqualizerModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    for (auto& band : topologyMix)
        for (auto& smoother : band)
            smoother.reset(sampleRate, 0.02);
    reset();
}

void EqualizerModule::reset()
{
    for (auto& channel : filters)
        for (auto& band : channel)
            for (auto& filter : band)
                filter.reset();
    for (auto& band : topologyMix)
        for (auto& smoother : band)
            smoother.setCurrentAndTargetValue(0.0f);
    topologyInitialized = false;
}

float EqualizerModule::Biquad::process(float input)
{
    const auto output = b0 * input + z1;
    z1 = b1 * input - a1 * output + z2;
    z2 = b2 * input - a2 * output;
    return output;
}

void EqualizerModule::Biquad::setPeak(double rate, float frequency, float q, float gainDb)
{
    const auto a = std::pow(10.0f, gainDb / 40.0f);
    const auto omega = juce::MathConstants<float>::twoPi * frequency
                       / static_cast<float>(rate);
    const auto alpha = std::sin(omega) / (2.0f * q);
    const auto cosOmega = std::cos(omega);
    const auto a0 = 1.0f + alpha / a;
    b0 = (1.0f + alpha * a) / a0;
    b1 = (-2.0f * cosOmega) / a0;
    b2 = (1.0f - alpha * a) / a0;
    a1 = (-2.0f * cosOmega) / a0;
    a2 = (1.0f - alpha / a) / a0;
}

void EqualizerModule::Biquad::setLowShelf(
    double rate, float frequency, float q, float gainDb)
{
    const auto a = std::pow(10.0f, gainDb / 40.0f);
    const auto omega = juce::MathConstants<float>::twoPi * frequency
                       / static_cast<float>(rate);
    const auto cosine = std::cos(omega);
    const auto alpha = std::sin(omega) / (2.0f * q);
    const auto rootA = std::sqrt(a);
    const auto twoRootAAlpha = 2.0f * rootA * alpha;
    const auto a0 = (a + 1.0f) + (a - 1.0f) * cosine + twoRootAAlpha;
    b0 = a * ((a + 1.0f) - (a - 1.0f) * cosine + twoRootAAlpha) / a0;
    b1 = 2.0f * a * ((a - 1.0f) - (a + 1.0f) * cosine) / a0;
    b2 = a * ((a + 1.0f) - (a - 1.0f) * cosine - twoRootAAlpha) / a0;
    a1 = -2.0f * ((a - 1.0f) + (a + 1.0f) * cosine) / a0;
    a2 = ((a + 1.0f) + (a - 1.0f) * cosine - twoRootAAlpha) / a0;
}

void EqualizerModule::Biquad::setHighShelf(
    double rate, float frequency, float q, float gainDb)
{
    const auto a = std::pow(10.0f, gainDb / 40.0f);
    const auto omega = juce::MathConstants<float>::twoPi * frequency
                       / static_cast<float>(rate);
    const auto cosine = std::cos(omega);
    const auto alpha = std::sin(omega) / (2.0f * q);
    const auto rootA = std::sqrt(a);
    const auto twoRootAAlpha = 2.0f * rootA * alpha;
    const auto a0 = (a + 1.0f) - (a - 1.0f) * cosine + twoRootAAlpha;
    b0 = a * ((a + 1.0f) + (a - 1.0f) * cosine + twoRootAAlpha) / a0;
    b1 = -2.0f * a * ((a - 1.0f) + (a + 1.0f) * cosine) / a0;
    b2 = a * ((a + 1.0f) + (a - 1.0f) * cosine - twoRootAAlpha) / a0;
    a1 = 2.0f * ((a - 1.0f) - (a + 1.0f) * cosine) / a0;
    a2 = ((a + 1.0f) - (a - 1.0f) * cosine - twoRootAAlpha) / a0;
}

void EqualizerModule::Biquad::setHighPass(double rate, float frequency, float q)
{
    const auto omega = juce::MathConstants<float>::twoPi * frequency
                       / static_cast<float>(rate);
    const auto alpha = std::sin(omega) / (2.0f * q);
    const auto cosOmega = std::cos(omega);
    const auto a0 = 1.0f + alpha;
    b0 = (1.0f + cosOmega) * 0.5f / a0;
    b1 = -(1.0f + cosOmega) / a0;
    b2 = b0;
    a1 = -2.0f * cosOmega / a0;
    a2 = (1.0f - alpha) / a0;
}

void EqualizerModule::Biquad::setLowPass(double rate, float frequency, float q)
{
    const auto omega = juce::MathConstants<float>::twoPi * frequency
                       / static_cast<float>(rate);
    const auto alpha = std::sin(omega) / (2.0f * q);
    const auto cosOmega = std::cos(omega);
    const auto a0 = 1.0f + alpha;
    b0 = (1.0f - cosOmega) * 0.5f / a0;
    b1 = (1.0f - cosOmega) / a0;
    b2 = b0;
    a1 = -2.0f * cosOmega / a0;
    a2 = (1.0f - alpha) / a0;
}

void EqualizerModule::Biquad::reset()
{
    z1 = 0.0f;
    z2 = 0.0f;
}

void EqualizerModule::process(juce::AudioBuffer<float>& buffer,
                              const ControlValues& controls,
                              const ProcessEnvironment&)
{
    const std::array<float, 3> frequencies {
        exponential(30.0f, static_cast<float>(
            juce::jmin(1200.0, sampleRate * 0.475)), controls[0]),
        exponential(150.0f, static_cast<float>(
            juce::jmin(7000.0, sampleRate * 0.475)), controls[3]),
        exponential(1500.0f, static_cast<float>(
            juce::jmin(20000.0, sampleRate * 0.475)), controls[6])
    };
    const std::array<float, 3> gains {
        lerp(-18.0f, 18.0f, controls[1]),
        lerp(-18.0f, 18.0f, controls[4]),
        lerp(-18.0f, 18.0f, controls[7])
    };
    const std::array<float, 3> qs {
        exponential(0.2f, 10.0f, controls[2]),
        exponential(0.2f, 10.0f, controls[5]),
        exponential(0.2f, 10.0f, controls[8])
    };
    const std::array<int, 3> topologyTargets {
        static_cast<int>(equalizerBandMode(controls[10])),
        static_cast<int>(EqualizerBandMode::bell),
        static_cast<int>(equalizerBandMode(controls[11]))
    };
    for (int band = 0; band < 3; ++band)
        for (int topology = 0; topology < topologyCount; ++topology)
        {
            auto& smoother = topologyMix[static_cast<size_t>(band)]
                                        [static_cast<size_t>(topology)];
            const auto target =
                topologyTargets[static_cast<size_t>(band)] == topology
                    ? 1.0f : 0.0f;
            if (!topologyInitialized)
                smoother.setCurrentAndTargetValue(target);
            else
                smoother.setTargetValue(target);
        }
    topologyInitialized = true;

    for (int band = 0; band < 3; ++band)
    {
        for (int channel = 0; channel < juce::jmin(2, buffer.getNumChannels()); ++channel)
        {
            auto& topologies = filters[static_cast<size_t>(channel)]
                                      [static_cast<size_t>(band)];
            topologies[0].setPeak(
                sampleRate, frequencies[static_cast<size_t>(band)],
                qs[static_cast<size_t>(band)],
                gains[static_cast<size_t>(band)]);
            if (band == 0)
            {
                topologies[1].setLowShelf(
                    sampleRate, frequencies[0], qs[0], gains[0]);
                topologies[2].setHighPass(
                    sampleRate, frequencies[0], qs[0]);
            }
            else if (band == 2)
            {
                topologies[1].setHighShelf(
                    sampleRate, frequencies[2], qs[2], gains[2]);
                topologies[2].setLowPass(
                    sampleRate, frequencies[2], qs[2]);
            }
        }
    }

    const auto outputGain = juce::Decibels::decibelsToGain(
        lerp(-18.0f, 18.0f, controls[9]));
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        std::array<std::array<float, topologyCount>, 3> mixes {};
        for (int band = 0; band < 3; ++band)
            for (int topology = 0; topology < topologyCount; ++topology)
                mixes[static_cast<size_t>(band)]
                     [static_cast<size_t>(topology)] =
                    topologyMix[static_cast<size_t>(band)]
                               [static_cast<size_t>(topology)].getNextValue();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto value = buffer.getSample(channel, sample);
            const auto channelIndex =
                static_cast<size_t>(juce::jmin(channel, 1));
            for (int band = 0; band < 3; ++band)
            {
                const auto index = static_cast<size_t>(band);
                auto processed = 0.0f;
                if (band == 1)
                    processed = filters[channelIndex][index][0].process(value);
                else
                    for (int topology = 0; topology < topologyCount; ++topology)
                        processed += filters[channelIndex][index]
                                            [static_cast<size_t>(topology)]
                                                .process(value)
                                     * mixes[index][static_cast<size_t>(topology)];
                value = processed;
            }
            buffer.setSample(channel, sample, value * outputGain);
        }
    }
}
} // namespace megadsp
