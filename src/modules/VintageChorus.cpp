#include "VintageChorus.h"
#include "DspHelpers.h"

#include <cmath>

namespace megadsp
{
using detail::exponential;
using detail::lerp;

void VintageChorusModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    channels = juce::jlimit(1, 2, static_cast<int>(spec.numChannels));
    const auto capacity = static_cast<size_t>(
        std::ceil(sampleRate * 0.075) + 8.0);
    for (auto& channel : delayBuffer)
        channel.assign(capacity, 0.0f);

    for (auto& smoother : modelMix)
        smoother.reset(sampleRate, 0.035);
    for (auto& smoother : voiceMix)
        smoother.reset(sampleRate, 0.025);
    rateSmoothed.reset(sampleRate, 0.04);
    depthSmoothed.reset(sampleRate, 0.04);
    delaySmoothed.reset(sampleRate, 0.04);
    widthSmoothed.reset(sampleRate, 0.04);
    feedbackSmoothed.reset(sampleRate, 0.04);
    toneSmoothed.reset(sampleRate, 0.04);
    ageSmoothed.reset(sampleRate, 0.06);
    stereoPhaseSmoothed.reset(sampleRate, 0.04);
    mixSmoothed.reset(sampleRate, 0.025);
    outputSmoothed.reset(sampleRate, 0.025);
    reset();
}

void VintageChorusModule::reset()
{
    for (auto& channel : delayBuffer)
        std::fill(channel.begin(), channel.end(), 0.0f);
    for (auto& state : toneState)
        state.fill(0.0f);
    feedbackState.fill(0.0f);
    inputColourState.fill(0.0f);
    for (int model = 0; model < modelCount; ++model)
        for (int voice = 0; voice < maximumVoices; ++voice)
            phases[static_cast<size_t>(model)][static_cast<size_t>(voice)] =
                0.17f * static_cast<float>(model);
    writePosition = 0;
    signalEnergy = 0.0f;
    activeModel = 0;
    currentStereoPhase = 0.5f;
    initialized = false;
}

float VintageChorusModule::readDelay(int channel, float delaySamples) const
{
    const auto& history = delayBuffer[static_cast<size_t>(
        juce::jlimit(0, 1, channel))];
    const auto size = static_cast<int>(history.size());
    auto position = static_cast<float>(writePosition)
                    - juce::jlimit(2.0f, static_cast<float>(size - 3),
                                   delaySamples);
    while (position < 0.0f)
        position += static_cast<float>(size);
    const auto index = static_cast<int>(position);
    const auto fraction = position - static_cast<float>(index);
    const auto at = [&history, size](int i)
    {
        while (i < 0)
            i += size;
        return history[static_cast<size_t>(i % size)];
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

float VintageChorusModule::lfo(float phase, int model, float age)
{
    const auto wrapped = phase - std::floor(phase);
    const auto sine = std::sin(juce::MathConstants<float>::twoPi * wrapped);
    if (model == 0)
    {
        const auto asymmetric = 0.78f * sine
            + 0.22f * std::sin(
                juce::MathConstants<float>::twoPi * (2.0f * wrapped + 0.12f));
        const auto irregular = std::sin(
            juce::MathConstants<float>::twoPi * (7.0f * wrapped + 0.31f));
        return juce::jlimit(-1.0f, 1.0f,
                            asymmetric + irregular * age * 0.045f);
    }
    if (model == 1)
        return 0.72f * sine + 0.28f * std::sin(
            juce::MathConstants<float>::twoPi * (2.0f * wrapped + 0.25f));
    if (model == 3)
        return 0.82f * sine + 0.18f * std::sin(
            juce::MathConstants<float>::twoPi * (3.0f * wrapped + 0.17f));
    return sine;
}

std::array<float, 2> VintageChorusModule::renderModel(
    int model, float depth, float delayMs, float age)
{
    static constexpr std::array<int, modelCount> naturalVoices { 2, 4, 3, 6 };
    static constexpr std::array<float, modelCount> depthScale {
        0.82f, 0.30f, 0.68f, 1.0f
    };
    std::array<float, 2> result {};
    float normalizer = 0.0f;
    const auto phaseOffset = currentStereoPhase * 0.5f;
    const auto baseSamples = delayMs * 0.001f * static_cast<float>(sampleRate);
    const auto maximumSwingMs = juce::jmin(8.0f, delayMs * 0.72f);

    for (int voice = 0; voice < maximumVoices; ++voice)
    {
        const auto gain = voiceMix[static_cast<size_t>(voice)].getCurrentValue();
        if (gain <= 0.00001f)
            continue;
        const auto topologyCount = naturalVoices[static_cast<size_t>(model)];
        float voicePhase = 0.0f;
        if (model == 0)
            voicePhase = (voice % 2 == 0 ? 0.0f : 0.47f)
                         + static_cast<float>(voice / 2) * 0.073f;
        else if (model == 1)
            voicePhase = static_cast<float>(voice % topologyCount)
                         / static_cast<float>(topologyCount)
                         + static_cast<float>(voice / topologyCount) * 0.055f;
        else if (model == 2)
            voicePhase = static_cast<float>(voice % 3) / 3.0f
                         + static_cast<float>(voice / 3) * 0.08f;
        else
            voicePhase = static_cast<float>(voice % topologyCount)
                         / static_cast<float>(topologyCount);

        const auto localPhase =
            phases[static_cast<size_t>(model)][static_cast<size_t>(voice)]
            + voicePhase;
        const auto leftLfo = lfo(localPhase, model, age);
        const auto rightLfo = lfo(localPhase + phaseOffset, model, age);
        auto swing = maximumSwingMs * depth
                     * depthScale[static_cast<size_t>(model)];
        if (model == 1)
            swing *= 0.72f + 0.28f * (voice % 2 == 0 ? 1.0f : -1.0f);
        const auto staticOffset = model == 1
            ? (static_cast<float>(voice % 4) - 1.5f) * 0.65f
            : model == 3 ? (voice % 2 == 0 ? -0.7f : 0.7f) : 0.0f;
        const auto leftDelay = baseSamples
            + (staticOffset + swing * leftLfo) * 0.001f
                  * static_cast<float>(sampleRate);
        const auto rightDelay = baseSamples
            + (staticOffset + swing * rightLfo) * 0.001f
                  * static_cast<float>(sampleRate);
        auto left = readDelay(0, leftDelay);
        auto right = readDelay(channels > 1 ? 1 : 0, rightDelay);

        if (model == 0)
        {
            const auto drive = 1.08f + age * 0.72f;
            left = std::tanh(left * drive) / drive;
            right = std::tanh(right * drive) / drive;
        }

        const auto pan = model == 0 ? (voice % 2 == 0 ? -0.34f : 0.34f)
            : model == 1 ? std::array<float, 4> { -0.85f, 0.72f, -0.42f, 0.91f }[
                  static_cast<size_t>(voice % 4)]
            : model == 2 ? std::array<float, 3> { -0.72f, 0.0f, 0.72f }[
                  static_cast<size_t>(voice % 3)]
            : std::array<float, 6> { -0.9f, 0.66f, -0.36f, 0.36f, -0.66f, 0.9f }[
                  static_cast<size_t>(voice % 6)];
        const auto cross = model == 1 ? 0.18f : model == 2 ? 0.10f : 0.06f;
        result[0] += gain * ((1.0f - 0.28f * pan) * left + cross * right);
        result[1] += gain * ((1.0f + 0.28f * pan) * right + cross * left);
        normalizer += gain * (1.0f + cross);
    }

    if (normalizer > 0.00001f)
    {
        result[0] /= normalizer;
        result[1] /= normalizer;
    }
    if (model == 0 && age > 0.0f)
    {
        const auto gatedClock = std::sqrt(signalEnergy) * age * 0.0009f
            * std::sin(juce::MathConstants<float>::twoPi
                       * (phases[0][0] * 43.0f + 0.173f));
        result[0] += gatedClock;
        result[1] -= gatedClock * 0.72f;
    }
    return result;
}

void VintageChorusModule::process(juce::AudioBuffer<float>& buffer,
                                  const ControlValues& controls,
                                  const ProcessEnvironment&)
{
    if (delayBuffer[0].empty() || buffer.getNumChannels() == 0)
        return;

    const auto model = discreteIndex(controls[0], modelCount);
    const auto voices = juce::jlimit(
        1, maximumVoices, juce::roundToInt(lerp(1.0f, 6.0f, controls[4])));
    const auto outputDb = lerp(-18.0f, 12.0f, controls[11]);
    const auto targets = std::array<float, 10> {
        exponential(0.05f, 8.0f, controls[1]), controls[2],
        exponential(2.0f, 30.0f, controls[3]), controls[5] * 2.0f,
        lerp(-0.75f, 0.75f, controls[6]),
        exponential(800.0f, 18000.0f, controls[7]), controls[8],
        controls[9], controls[10],
        std::abs(outputDb) < 0.0001f
            ? 1.0f : juce::Decibels::decibelsToGain(outputDb)
    };

    if (!initialized)
    {
        activeModel = model;
        for (int index = 0; index < modelCount; ++index)
            modelMix[static_cast<size_t>(index)].setCurrentAndTargetValue(
                index == model ? 1.0f : 0.0f);
        for (int voice = 0; voice < maximumVoices; ++voice)
            voiceMix[static_cast<size_t>(voice)].setCurrentAndTargetValue(
                voice < voices ? 1.0f : 0.0f);
        rateSmoothed.setCurrentAndTargetValue(targets[0]);
        depthSmoothed.setCurrentAndTargetValue(targets[1]);
        delaySmoothed.setCurrentAndTargetValue(targets[2]);
        widthSmoothed.setCurrentAndTargetValue(targets[3]);
        feedbackSmoothed.setCurrentAndTargetValue(targets[4]);
        toneSmoothed.setCurrentAndTargetValue(targets[5]);
        ageSmoothed.setCurrentAndTargetValue(targets[6]);
        stereoPhaseSmoothed.setCurrentAndTargetValue(targets[7]);
        mixSmoothed.setCurrentAndTargetValue(targets[8]);
        outputSmoothed.setCurrentAndTargetValue(targets[9]);
        initialized = true;
    }
    if (model != activeModel)
    {
        activeModel = model;
        for (int index = 0; index < modelCount; ++index)
            modelMix[static_cast<size_t>(index)].setTargetValue(
                index == model ? 1.0f : 0.0f);
    }
    for (int voice = 0; voice < maximumVoices; ++voice)
        voiceMix[static_cast<size_t>(voice)].setTargetValue(
            voice < voices ? 1.0f : 0.0f);
    rateSmoothed.setTargetValue(targets[0]);
    depthSmoothed.setTargetValue(targets[1]);
    delaySmoothed.setTargetValue(targets[2]);
    widthSmoothed.setTargetValue(targets[3]);
    feedbackSmoothed.setTargetValue(targets[4]);
    toneSmoothed.setTargetValue(targets[5]);
    ageSmoothed.setTargetValue(targets[6]);
    stereoPhaseSmoothed.setTargetValue(targets[7]);
    mixSmoothed.setTargetValue(targets[8]);
    outputSmoothed.setTargetValue(targets[9]);

    const auto channelCount = juce::jmin(2, buffer.getNumChannels());
    channels = channelCount;
    const auto capacity = static_cast<int>(delayBuffer[0].size());
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const std::array<float, 2> dry {
            buffer.getSample(0, sample),
            channelCount > 1 ? buffer.getSample(1, sample)
                             : buffer.getSample(0, sample)
        };
        signalEnergy = 0.9995f * signalEnergy
                       + 0.0005f * 0.5f
                           * (dry[0] * dry[0] + dry[1] * dry[1]);
        for (auto& smoother : voiceMix)
            smoother.getNextValue();

        const auto rate = rateSmoothed.getNextValue();
        const auto depth = depthSmoothed.getNextValue();
        const auto delayMs = delaySmoothed.getNextValue();
        const auto width = widthSmoothed.getNextValue();
        const auto feedback = feedbackSmoothed.getNextValue();
        const auto tone = toneSmoothed.getNextValue();
        const auto age = ageSmoothed.getNextValue();
        currentStereoPhase = stereoPhaseSmoothed.getNextValue();
        const auto mix = mixSmoothed.getNextValue();
        const auto output = outputSmoothed.getNextValue();

        std::array<float, 2> wet {};
        for (int topology = 0; topology < modelCount; ++topology)
        {
            const auto topologyMix =
                modelMix[static_cast<size_t>(topology)].getNextValue();
            const auto topologyRateScale = topology == 1 ? 0.63f
                : topology == 3 ? 1.42f : topology == 0 ? 0.86f : 1.0f;
            for (int voice = 0; voice < maximumVoices; ++voice)
            {
                const auto groupRate = topology == 3
                    ? (voice % 2 == 0 ? 0.72f : 1.37f) : 1.0f;
                auto& oscillator =
                    phases[static_cast<size_t>(topology)]
                          [static_cast<size_t>(voice)];
                oscillator += rate * topologyRateScale * groupRate
                              / static_cast<float>(sampleRate);
                oscillator -= std::floor(oscillator);
            }
            if (topologyMix <= 0.000001f)
                continue;
            auto rendered = renderModel(
                topology, depth, delayMs, age);
            const auto modelCutoff = topology == 0
                ? tone * lerp(0.88f, 0.42f, age)
                : topology == 3 ? tone * 0.62f
                : topology == 1 ? tone * 0.94f : tone;
            const auto filter = std::exp(
                -juce::MathConstants<float>::twoPi
                * juce::jlimit(300.0f,
                               static_cast<float>(sampleRate * 0.45),
                               modelCutoff)
                / static_cast<float>(sampleRate));
            for (int channel = 0; channel < 2; ++channel)
            {
                auto& state = toneState[static_cast<size_t>(topology)]
                                       [static_cast<size_t>(channel)];
                state = filter * state + (1.0f - filter)
                                      * rendered[static_cast<size_t>(channel)];
                wet[static_cast<size_t>(channel)] += topologyMix * state;
            }
        }

        const auto mid = 0.5f * (wet[0] + wet[1]);
        const auto side = 0.5f * (wet[0] - wet[1]) * width;
        wet = { mid + side, mid - side };
        const auto feedbackCutoff = juce::jmin(tone, 12000.0f);
        const auto feedbackCoefficient = std::exp(
            -juce::MathConstants<float>::twoPi * feedbackCutoff
            / static_cast<float>(sampleRate));
        for (int channel = 0; channel < 2; ++channel)
        {
            auto& state = feedbackState[static_cast<size_t>(channel)];
            state = feedbackCoefficient * state
                    + (1.0f - feedbackCoefficient)
                          * wet[static_cast<size_t>(channel)];
            const auto colourCoefficient = std::exp(
                -juce::MathConstants<float>::twoPi
                * lerp(16000.0f, 4800.0f, age)
                / static_cast<float>(sampleRate));
            auto& colour = inputColourState[static_cast<size_t>(channel)];
            colour = colourCoefficient * colour
                     + (1.0f - colourCoefficient)
                           * dry[static_cast<size_t>(channel)];
            const auto bbdWeight = modelMix[0].getCurrentValue();
            const auto injection = dry[static_cast<size_t>(channel)]
                + bbdWeight * age * 0.12f
                      * (dry[static_cast<size_t>(channel)] - colour);
            delayBuffer[static_cast<size_t>(channel)]
                       [static_cast<size_t>(writePosition)] =
                injection + state * feedback * 0.93f;
        }

        const auto dryGain = mix <= 0.000001f ? 1.0f
                                              : std::cos(
                                                  mix * juce::MathConstants<float>::halfPi);
        const auto wetGain = mix <= 0.000001f ? 0.0f
                                              : std::sin(
                                                  mix * juce::MathConstants<float>::halfPi);
        for (int channel = 0; channel < channelCount; ++channel)
        {
            const auto wetSample = channelCount == 1
                ? 0.5f * (wet[0] + wet[1])
                : wet[static_cast<size_t>(channel)];
            buffer.setSample(channel, sample,
                output * (dryGain * dry[static_cast<size_t>(channel)]
                          + wetGain * wetSample));
        }
        if (++writePosition >= capacity)
            writePosition = 0;
    }
}

double VintageChorusModule::tailSeconds(const ControlValues& controls) const
{
    const auto delay = exponential(2.0f, 30.0f, controls[3]) * 0.001;
    const auto feedback = std::abs(lerp(-0.75f, 0.75f, controls[6])) * 0.93;
    if (feedback < 0.001)
        return juce::jlimit(0.04, 0.25, delay * 2.0);
    return juce::jlimit(
        0.04, 4.0, delay * std::log(0.001) / std::log(feedback));
}
} // namespace megadsp
