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
    for (auto& smoother : rolloffMix)
        smoother.reset(sampleRate, 0.02);
    reset();
}

void EqualizerModule::reset()
{
    for (auto& channel : filters)
        for (auto& filter : channel)
            filter.reset();
    for (auto& channel : rolloffFilters)
        for (auto& filter : channel)
            filter.reset();
    for (auto& smoother : rolloffMix)
        smoother.setCurrentAndTargetValue(0.0f);
    rolloffInitialized = false;
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
    const std::array<float, 3> rolloffTargets {
        equalizerLowIsHighPass(controls[10]) ? 1.0f : 0.0f,
        0.0f,
        equalizerHighIsLowPass(controls[11]) ? 1.0f : 0.0f
    };
    for (int band = 0; band < 3; ++band)
    {
        auto& smoother = rolloffMix[static_cast<size_t>(band)];
        if (!rolloffInitialized)
            smoother.setCurrentAndTargetValue(
                rolloffTargets[static_cast<size_t>(band)]);
        else
            smoother.setTargetValue(rolloffTargets[static_cast<size_t>(band)]);
    }
    rolloffInitialized = true;

    for (int band = 0; band < 3; ++band)
    {
        for (int channel = 0; channel < juce::jmin(2, buffer.getNumChannels()); ++channel)
        {
            auto& filter =
                filters[static_cast<size_t>(channel)][static_cast<size_t>(band)];
            filter.setPeak(sampleRate, frequencies[static_cast<size_t>(band)],
                           qs[static_cast<size_t>(band)],
                           gains[static_cast<size_t>(band)]);
            auto& rolloff = rolloffFilters[static_cast<size_t>(channel)]
                                          [static_cast<size_t>(band)];
            if (band == 0)
                rolloff.setHighPass(sampleRate, frequencies[0], qs[0]);
            else if (band == 2)
                rolloff.setLowPass(sampleRate, frequencies[2], qs[2]);
        }
    }

    const auto outputGain = juce::Decibels::decibelsToGain(
        lerp(-18.0f, 18.0f, controls[9]));
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        std::array<float, 3> mixes {};
        for (int band = 0; band < 3; ++band)
            mixes[static_cast<size_t>(band)] =
                rolloffMix[static_cast<size_t>(band)].getNextValue();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto value = buffer.getSample(channel, sample);
            const auto channelIndex =
                static_cast<size_t>(juce::jmin(channel, 1));
            for (int band = 0; band < 3; ++band)
            {
                const auto index = static_cast<size_t>(band);
                const auto peak = filters[channelIndex][index].process(value);
                if (band == 0 || band == 2)
                {
                    const auto rolloff =
                        rolloffFilters[channelIndex][index].process(value);
                    value = peak + (rolloff - peak) * mixes[index];
                }
                else
                    value = peak;
            }
            buffer.setSample(channel, sample, value * outputGain);
        }
    }
}
} // namespace megadsp
