#include "Compressor.h"
#include "DspHelpers.h"

#include <cmath>

namespace megadsp
{
using detail::coefficient;
using detail::exponential;
using detail::lerp;

void CompressorModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    reset();
}

void CompressorModule::reset()
{
    envelope = 0.0f;
    gain = 1.0f;
    averageReductionDb = 0.0f;
    autoMakeupDb = 0.0f;
    gainReductionDb.store(0.0f);
}

void CompressorModule::process(juce::AudioBuffer<float>& buffer,
                               const ControlValues& controls,
                               const ProcessEnvironment& environment)
{
    const auto threshold = lerp(-60.0f, 0.0f, controls[0]);
    const auto ratio = exponential(1.0f, 20.0f, controls[1]);
    const auto attack = exponential(0.1f, 100.0f, controls[2]);
    const auto release = exponential(10.0f, 1000.0f, controls[3]);
    const auto knee = lerp(0.0f, 18.0f, controls[4]);
    const auto makeup = juce::Decibels::decibelsToGain(lerp(0.0f, 24.0f, controls[5]));
    const auto mix = juce::jlimit(0.0f, 1.0f, controls[6]);
    const auto autoMakeupEnabled = controls[8] >= 0.5f;
    const bool useSidechain = controls[7] >= 0.5f
                              && environment.sidechain != nullptr
                              && environment.sidechain->getNumChannels() > 0;
    const auto& detectorBuffer = useSidechain ? *environment.sidechain : buffer;
    const auto attackCoefficient = coefficient(sampleRate, attack);
    const auto releaseCoefficient = coefficient(sampleRate, release);
    const auto averageRiseCoefficient = coefficient(sampleRate, 1000.0f);
    const auto averageFallCoefficient = coefficient(sampleRate, 3000.0f);
    const auto autoMakeupCoefficient = coefficient(sampleRate, 200.0f);
    float minimumGain = 1.0f;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        float detector = 0.0f;
        float programPeak = 0.0f;
        for (int channel = 0; channel < detectorBuffer.getNumChannels(); ++channel)
            detector = juce::jmax(detector, std::abs(detectorBuffer.getSample(channel, sample)));
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            programPeak = juce::jmax(
                programPeak, std::abs(buffer.getSample(channel, sample)));

        const auto detectorCoefficient = detector > envelope
                                             ? attackCoefficient
                                             : releaseCoefficient;
        envelope = detectorCoefficient * envelope
                   + (1.0f - detectorCoefficient) * detector;
        const auto levelDb = juce::Decibels::gainToDecibels(envelope, -100.0f);
        const auto over = levelDb - threshold;
        float reductionDb = 0.0f;

        if (knee > 0.0f && over > -knee * 0.5f && over < knee * 0.5f)
        {
            const auto kneeInput = over + knee * 0.5f;
            reductionDb = (1.0f / ratio - 1.0f) * kneeInput * kneeInput / (2.0f * knee);
        }
        else if (over >= knee * 0.5f)
        {
            reductionDb = (1.0f / ratio - 1.0f) * over;
        }

        const auto targetGain = juce::Decibels::decibelsToGain(reductionDb);
        gain = targetGain < gain
                   ? attackCoefficient * gain + (1.0f - attackCoefficient) * targetGain
                   : releaseCoefficient * gain + (1.0f - releaseCoefficient) * targetGain;
        minimumGain = juce::jmin(minimumGain, gain);
        const auto instantaneousReductionDb =
            -juce::Decibels::gainToDecibels(gain, -100.0f);
        const auto active = programPeak > juce::Decibels::decibelsToGain(-60.0f)
                            && detector > juce::Decibels::decibelsToGain(-60.0f);
        const auto averageTarget = active ? instantaneousReductionDb : 0.0f;
        const auto averageCoefficient = averageTarget > averageReductionDb
                                            ? averageRiseCoefficient
                                            : averageFallCoefficient;
        averageReductionDb = averageCoefficient * averageReductionDb
                             + (1.0f - averageCoefficient) * averageTarget;
        const auto autoTargetDb = autoMakeupEnabled
                                      ? juce::jmin(18.0f, averageReductionDb)
                                      : 0.0f;
        autoMakeupDb = autoMakeupCoefficient * autoMakeupDb
                       + (1.0f - autoMakeupCoefficient) * autoTargetDb;
        const auto automaticMakeup =
            juce::Decibels::decibelsToGain(autoMakeupDb);

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* samples = buffer.getWritePointer(channel);
            const auto dry = samples[sample];
            samples[sample] = dry
                              + (dry * gain * makeup * automaticMakeup - dry)
                                    * mix;
        }
    }

    gainReductionDb.store(-juce::Decibels::gainToDecibels(minimumGain, -100.0f));
}
} // namespace megadsp
