#include "Tremolo.h"
#include "DspHelpers.h"

#include <cmath>

namespace megadsp
{
using detail::exponential;
using detail::lerp;

void TremoloModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    const auto capacity = static_cast<size_t>(
        juce::jmax(16, static_cast<int>(std::ceil(sampleRate * 0.5))));
    for (auto& channel : delayBuffer)
        channel.assign(capacity, 0.0f);
    for (auto& smoother : modeMix)
        smoother.reset(sampleRate, 0.025);
    for (auto* smoother : {
             &rateSmoothed, &tremoloDepthSmoothed, &pitchDepthSmoothed,
             &shapeSmoothed, &stereoPhaseSmoothed, &crossoverSmoothed,
             &mixSmoothed })
        smoother->reset(sampleRate, 0.02);
    outputSmoothed.reset(sampleRate, 0.02);
    reset();
}

void TremoloModule::reset()
{
    for (auto& channel : delayBuffer)
        std::fill(channel.begin(), channel.end(), 0.0f);
    crossoverState.fill(0.0f);
    writePosition = 0;
    phase = 0.0f;
    initialized = false;
    for (int mode = 0; mode < 3; ++mode)
        modeMix[static_cast<size_t>(mode)].setCurrentAndTargetValue(
            mode == 0 ? 1.0f : 0.0f);
    rateSmoothed.setCurrentAndTargetValue(4.0f);
    tremoloDepthSmoothed.setCurrentAndTargetValue(0.7f);
    pitchDepthSmoothed.setCurrentAndTargetValue(24.0f);
    shapeSmoothed.setCurrentAndTargetValue(0.0f);
    stereoPhaseSmoothed.setCurrentAndTargetValue(0.0f);
    crossoverSmoothed.setCurrentAndTargetValue(700.0f);
    mixSmoothed.setCurrentAndTargetValue(1.0f);
    outputSmoothed.setCurrentAndTargetValue(1.0f);
}

float TremoloModule::readDelay(int channel, float delaySamples) const
{
    const auto& history = delayBuffer[static_cast<size_t>(juce::jmin(channel, 1))];
    const auto size = static_cast<int>(history.size());
    auto position = static_cast<float>(writePosition) - delaySamples;
    while (position < 0.0f)
        position += static_cast<float>(size);
    const auto index1 = static_cast<int>(position) % size;
    const auto fraction = position - std::floor(position);
    const auto index0 = (index1 - 1 + size) % size;
    const auto index2 = (index1 + 1) % size;
    const auto index3 = (index1 + 2) % size;
    const auto y0 = history[static_cast<size_t>(index0)];
    const auto y1 = history[static_cast<size_t>(index1)];
    const auto y2 = history[static_cast<size_t>(index2)];
    const auto y3 = history[static_cast<size_t>(index3)];
    const auto a0 = y3 - y2 - y0 + y1;
    const auto a1 = y0 - y1 - a0;
    const auto a2 = y2 - y0;
    return ((a0 * fraction + a1) * fraction + a2) * fraction + y1;
}

float TremoloModule::lfoValue(float phaseValue, float shape) const
{
    const auto wrapped = phaseValue - std::floor(phaseValue);
    const auto sine = std::sin(juce::MathConstants<float>::twoPi * wrapped);
    const auto triangle = 1.0f - 4.0f * std::abs(wrapped - 0.5f);
    if (shape <= 0.5f)
        return sine + (triangle - sine) * shape * 2.0f;
    const auto drive = lerp(1.0f, 6.0f, (shape - 0.5f) * 2.0f);
    const auto pulse = std::tanh(triangle * drive) / std::tanh(drive);
    return triangle + (pulse - triangle) * (shape - 0.5f) * 2.0f;
}

void TremoloModule::process(juce::AudioBuffer<float>& buffer,
                            const ControlValues& controls,
                            const ProcessEnvironment& environment)
{
    static constexpr std::array<float, 8> beatsPerCycle {
        16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.75f, 0.25f
    };
    const auto mode = discreteIndex(controls[0], 3);
    const auto freeRate = exponential(0.05f, 20.0f, controls[1]);
    const auto division = beatsPerCycle[static_cast<size_t>(
        discreteIndex(controls[3], static_cast<int>(beatsPerCycle.size())))];
    const auto syncedRate = static_cast<float>(
        juce::jlimit(20.0, 400.0, environment.bpm)) / (60.0f * division);
    const auto targetRate = controls[2] >= 0.5f ? syncedRate : freeRate;
    const auto targetPitchDepth = controls[5] * 100.0f;
    const auto targetStereoPhase = controls[7] * 0.5f;
    const auto targetCrossover = exponential(100.0f, 4000.0f, controls[8]);
    const auto targetOutput = juce::Decibels::decibelsToGain(
        lerp(-12.0f, 12.0f, controls[10]));
    for (int index = 0; index < 3; ++index)
    {
        auto& smoother = modeMix[static_cast<size_t>(index)];
        const auto target = index == mode ? 1.0f : 0.0f;
        if (!initialized)
            smoother.setCurrentAndTargetValue(target);
        else
            smoother.setTargetValue(target);
    }
    if (!initialized)
    {
        rateSmoothed.setCurrentAndTargetValue(targetRate);
        tremoloDepthSmoothed.setCurrentAndTargetValue(controls[4]);
        pitchDepthSmoothed.setCurrentAndTargetValue(targetPitchDepth);
        shapeSmoothed.setCurrentAndTargetValue(controls[6]);
        stereoPhaseSmoothed.setCurrentAndTargetValue(targetStereoPhase);
        crossoverSmoothed.setCurrentAndTargetValue(targetCrossover);
        mixSmoothed.setCurrentAndTargetValue(controls[9]);
        outputSmoothed.setCurrentAndTargetValue(targetOutput);
    }
    else
    {
        rateSmoothed.setTargetValue(targetRate);
        tremoloDepthSmoothed.setTargetValue(controls[4]);
        pitchDepthSmoothed.setTargetValue(targetPitchDepth);
        shapeSmoothed.setTargetValue(controls[6]);
        stereoPhaseSmoothed.setTargetValue(targetStereoPhase);
        crossoverSmoothed.setTargetValue(targetCrossover);
        mixSmoothed.setTargetValue(controls[9]);
        outputSmoothed.setTargetValue(targetOutput);
    }
    initialized = true;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto rate = rateSmoothed.getNextValue();
        const auto tremoloDepth = tremoloDepthSmoothed.getNextValue();
        const auto pitchCents = pitchDepthSmoothed.getNextValue();
        const auto shape = shapeSmoothed.getNextValue();
        const auto stereoOffset = stereoPhaseSmoothed.getNextValue();
        const auto crossover = crossoverSmoothed.getNextValue();
        const auto crossoverCoefficient = 1.0f - std::exp(
            -juce::MathConstants<float>::twoPi * crossover
            / static_cast<float>(sampleRate));
        const auto mix = mixSmoothed.getNextValue();
        const auto output = outputSmoothed.getNextValue();
        std::array<float, 3> modes {};
        for (int index = 0; index < 3; ++index)
            modes[static_cast<size_t>(index)] =
                modeMix[static_cast<size_t>(index)].getNextValue();

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto historyChannel = juce::jmin(channel, 1);
            const auto dry = buffer.getSample(channel, sample);
            delayBuffer[static_cast<size_t>(historyChannel)]
                       [static_cast<size_t>(writePosition)] = dry;
            const auto channelPhase = phase
                + (channel == 1 ? stereoOffset : 0.0f);
            const auto lfo = lfoValue(channelPhase, shape);
            const auto amplitudeGain =
                1.0f - tremoloDepth * (0.5f + 0.5f * lfo);
            const auto amplitude = dry * amplitudeGain;

            auto& lowState = crossoverState[static_cast<size_t>(historyChannel)];
            lowState += crossoverCoefficient * (dry - lowState);
            const auto high = dry - lowState;
            const auto lowGain =
                1.0f - tremoloDepth * (0.5f + 0.5f * lfo);
            const auto highGain =
                1.0f - tremoloDepth * (0.5f - 0.5f * lfo);
            const auto harmonic = lowState * lowGain + high * highGain;

            const auto pitchRatio =
                std::pow(2.0f, pitchCents / 1200.0f) - 1.0f;
            const auto radiansPerSample =
                juce::MathConstants<float>::twoPi * rate
                / static_cast<float>(sampleRate);
            const auto amplitudeSamples = juce::jlimit(
                0.0f, static_cast<float>(delayBuffer[0].size()) * 0.48f,
                pitchRatio / juce::jmax(1.0e-7f, radiansPerSample));
            const auto delaySamples = 4.0f + amplitudeSamples * (1.0f + lfo);
            const auto vibrato = readDelay(historyChannel, delaySamples);
            const auto wet = amplitude * modes[0] + harmonic * modes[1]
                             + vibrato * modes[2];
            buffer.setSample(channel, sample,
                             (dry + (wet - dry) * mix) * output);
        }
        writePosition = (writePosition + 1)
                        % static_cast<int>(delayBuffer[0].size());
        phase += rate / static_cast<float>(sampleRate);
        phase -= std::floor(phase);
    }
}
} // namespace megadsp
