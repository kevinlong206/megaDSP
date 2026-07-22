#include "StudioFlanger.h"
#include "DspHelpers.h"
#include "DspSafety.h"

#include <cmath>

namespace megadsp
{
using detail::exponential;
using detail::lerp;

namespace
{
constexpr std::array<float, 8> beatsPerCycle {
    16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.75f, 0.25f
};
}

void StudioFlangerModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    fixedLatency = static_cast<int>(std::ceil(sampleRate * 0.016));
    const auto capacity = static_cast<size_t>(
        fixedLatency + std::ceil(sampleRate * 0.032) + 16.0);
    for (auto& model : modelHistory)
        for (auto& channel : model)
            channel.assign(capacity, 0.0f);
    for (auto& channel : dryHistory)
        channel.assign(capacity, 0.0f);
    for (auto& smoother : modelMix)
        smoother.reset(sampleRate, 0.04);
    for (auto* smoother : {
             &rateSmoothed, &depthSmoothed, &feedbackSmoothed,
             &stereoPhaseSmoothed, &mixSmoothed })
        smoother->reset(sampleRate, 0.025);
    delaySmoothed.reset(sampleRate, 0.035);
    toneSmoothed.reset(sampleRate, 0.035);
    outputSmoothed.reset(sampleRate, 0.025);
    reset();
}

void StudioFlangerModule::reset()
{
    for (auto& model : modelHistory)
        for (auto& channel : model)
            std::fill(channel.begin(), channel.end(), 0.0f);
    for (auto& channel : dryHistory)
        std::fill(channel.begin(), channel.end(), 0.0f);
    for (auto& model : feedbackState)
        model.fill(0.0f);
    for (auto& model : toneState)
        model.fill(0.0f);
    writePosition = 0;
    phase = 0.0f;
    initialized = false;
    telemetrySequence = 0;
    telemetry.clear();
}

float StudioFlangerModule::readDelay(
    const std::vector<float>& history, float delaySamples) const
{
    const auto size = static_cast<int>(history.size());
    auto position = static_cast<float>(writePosition)
        - juce::jlimit(2.0f, static_cast<float>(size - 3), delaySamples);
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

float StudioFlangerModule::modelLfo(int model, float phaseValue) const
{
    const auto wrapped = phaseValue - std::floor(phaseValue);
    const auto sine = std::sin(
        juce::MathConstants<float>::twoPi * wrapped);
    if (model == 0)
        return 0.82f * sine + 0.18f * std::sin(
            juce::MathConstants<float>::twoPi * (2.0f * wrapped + 0.11f));
    if (model == 2)
        return std::tanh(2.2f * sine) / std::tanh(2.2f);
    if (model == 3)
        return 0.88f * sine + 0.12f * std::sin(
            juce::MathConstants<float>::twoPi * (3.0f * wrapped + 0.29f));
    return sine;
}

void StudioFlangerModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    if (dryHistory[0].empty() || buffer.getNumChannels() == 0)
        return;

    const auto model = discreteIndex(
        detail::normalizedControl(controls[0], 0.0f), modelCount);
    const auto freeRate = exponential(
        0.02f, 12.0f, detail::normalizedControl(controls[1], 0.0f));
    const auto division = beatsPerCycle[static_cast<size_t>(
        discreteIndex(detail::normalizedControl(controls[3], 0.0f),
                      static_cast<int>(beatsPerCycle.size())))];
    const auto bpm = static_cast<float>(
        juce::jlimit(20.0, 400.0,
            std::isfinite(environment.bpm) && environment.bpm > 0.0
                ? environment.bpm : 120.0));
    const auto rate = detail::normalizedControl(controls[2], 0.0f) >= 0.5f
        ? bpm / (60.0f * division) : freeRate;
    const auto depthMs = lerp(
        0.0f, 10.0f, detail::normalizedControl(controls[4], 0.0f));
    const auto manualMs = exponential(
        0.1f, 15.0f, detail::normalizedControl(controls[5], 0.0f));
    const auto feedback = lerp(
        -0.9f, 0.9f, detail::normalizedControl(controls[6], 0.5f));
    const auto stereoPhase = lerp(
        0.0f, 0.5f, detail::normalizedControl(controls[7], 0.0f));
    const auto tone = exponential(
        1000.0f, 20000.0f,
        detail::normalizedControl(controls[8], 1.0f));
    const auto output = juce::Decibels::decibelsToGain(
        lerp(-18.0f, 12.0f,
             detail::normalizedControl(controls[10], 0.6f)));

    for (int index = 0; index < modelCount; ++index)
    {
        const auto target = index == model ? 1.0f : 0.0f;
        if (!initialized)
            modelMix[static_cast<size_t>(index)]
                .setCurrentAndTargetValue(target);
        else
            modelMix[static_cast<size_t>(index)].setTargetValue(target);
    }
    auto setTarget = [this](auto& smoother, float target)
    {
        if (!initialized)
            smoother.setCurrentAndTargetValue(target);
        else
            smoother.setTargetValue(target);
    };
    setTarget(rateSmoothed, rate);
    setTarget(depthSmoothed, depthMs);
    setTarget(delaySmoothed, manualMs);
    setTarget(feedbackSmoothed, feedback);
    setTarget(stereoPhaseSmoothed, stereoPhase);
    setTarget(toneSmoothed, tone);
    setTarget(mixSmoothed, detail::normalizedControl(controls[9], 0.0f));
    setTarget(outputSmoothed, output);
    initialized = true;

    const auto channelCount = juce::jmin(2, buffer.getNumChannels());
    const auto capacity = static_cast<int>(dryHistory[0].size());
    std::array<float, modelCount> telemetryModelGains {};
    float telemetryDelayMs = 0.0f;
    for (int sampleIndex = 0; sampleIndex < buffer.getNumSamples();
         ++sampleIndex)
    {
        const auto currentRate = rateSmoothed.getNextValue();
        const auto depth = depthSmoothed.getNextValue();
        const auto manual = delaySmoothed.getNextValue();
        const auto currentFeedback = feedbackSmoothed.getNextValue();
        const auto phaseOffset = stereoPhaseSmoothed.getNextValue();
        const auto currentTone = toneSmoothed.getNextValue();
        const auto mix = mixSmoothed.getNextValue();
        const auto currentOutput = outputSmoothed.getNextValue();
        std::array<float, modelCount> modelGains {};
        for (int index = 0; index < modelCount; ++index)
            modelGains[static_cast<size_t>(index)] =
                modelMix[static_cast<size_t>(index)].getNextValue();
        telemetryModelGains = modelGains;

        const std::array<float, 2> input {
            detail::finiteSample(buffer.getSample(0, sampleIndex)),
            channelCount > 1
                ? detail::finiteSample(buffer.getSample(1, sampleIndex))
                : detail::finiteSample(buffer.getSample(0, sampleIndex))
        };
        std::array<float, 2> alignedDry {};
        std::array<float, 2> wet {};
        for (int channel = 0; channel < 2; ++channel)
        {
            auto& dryChannel = dryHistory[static_cast<size_t>(channel)];
            dryChannel[static_cast<size_t>(writePosition)] =
                input[static_cast<size_t>(channel)];
            alignedDry[static_cast<size_t>(channel)] =
                readDelay(dryChannel, static_cast<float>(fixedLatency));

            for (int index = 0; index < modelCount; ++index)
            {
                const auto localPhase =
                    phase + (channel == 1 ? phaseOffset : 0.0f);
                const auto lfo = modelLfo(index, localPhase);
                float relativeMs = 0.0f;
                if (index == 0)
                    relativeMs = manual + depth * (0.5f + 0.5f * lfo);
                else if (index == 1)
                    relativeMs = manual * 0.18f + depth * lfo;
                else if (index == 2)
                    relativeMs = 0.2f + manual * 0.32f
                        + depth * 0.58f * (0.5f + 0.5f * lfo);
                else
                    relativeMs = manual + depth * (0.5f + 0.5f * lfo);
                if (channel == 0 && index == model)
                    telemetryDelayMs = relativeMs;

                const auto delaySamples = static_cast<float>(fixedLatency)
                    + relativeMs * 0.001f * static_cast<float>(sampleRate);
                auto& history =
                    modelHistory[static_cast<size_t>(index)]
                                [static_cast<size_t>(channel)];
                auto rendered = readDelay(history, delaySamples);
                const auto modelCutoff = index == 3
                    ? currentTone * 0.52f
                    : index == 2 ? currentTone * 0.78f : currentTone;
                const auto coefficient = std::exp(
                    -juce::MathConstants<float>::twoPi
                    * juce::jlimit(300.0f,
                          static_cast<float>(sampleRate * 0.45), modelCutoff)
                    / static_cast<float>(sampleRate));
                auto& toneValue =
                    toneState[static_cast<size_t>(index)]
                             [static_cast<size_t>(channel)];
                toneValue = coefficient * toneValue
                    + (1.0f - coefficient) * rendered;
                rendered = toneValue;
                if (index == 2)
                    rendered = -rendered;
                else if (index == 3)
                    rendered = std::tanh(rendered * 1.35f) / 1.35f;
                wet[static_cast<size_t>(channel)] +=
                    modelGains[static_cast<size_t>(index)] * rendered;

                const auto feedbackScale =
                    index == 2 ? 0.94f : index == 3 ? 0.78f : 0.88f;
                auto& feedbackValue =
                    feedbackState[static_cast<size_t>(index)]
                                 [static_cast<size_t>(channel)];
                feedbackValue = detail::finiteSample(rendered);
                history[static_cast<size_t>(writePosition)] =
                    detail::finiteSample(
                        input[static_cast<size_t>(channel)]
                        + feedbackValue * currentFeedback * feedbackScale);
            }
        }

        const auto dryGain = std::cos(
            mix * juce::MathConstants<float>::halfPi);
        const auto wetGain = std::sin(
            mix * juce::MathConstants<float>::halfPi);
        for (int channel = 0; channel < channelCount; ++channel)
        {
            const auto wetSample = channelCount == 1
                ? 0.5f * (wet[0] + wet[1])
                : wet[static_cast<size_t>(channel)];
            buffer.setSample(
                channel, sampleIndex,
                detail::finiteSample(
                    currentOutput
                    * (dryGain * alignedDry[static_cast<size_t>(channel)]
                       + wetGain * wetSample)));
        }
        if (++writePosition >= capacity)
            writePosition = 0;
        phase += currentRate / static_cast<float>(sampleRate);
        phase -= std::floor(phase);
    }

    if (environment.captureTelemetry)
    {
        ContinuousTelemetrySnapshot snapshot;
        snapshot.sequence = ++telemetrySequence;
        snapshot.valueCount = telemetryValueCount;
        snapshot.values[leftPhase] = phase;
        snapshot.values[rightPhase] =
            phase + stereoPhaseSmoothed.getCurrentValue();
        snapshot.values[rightPhase] -=
            std::floor(snapshot.values[rightPhase]);
        snapshot.values[targetModel] = static_cast<float>(model);
        for (int index = 0; index < modelCount; ++index)
            snapshot.values[static_cast<size_t>(modelMix0 + index)] =
                telemetryModelGains[static_cast<size_t>(index)];
        snapshot.values[selectedDelayMs] = telemetryDelayMs;
        telemetry.publish(snapshot);
    }
}

bool StudioFlangerModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}

double StudioFlangerModule::tailSeconds(const ControlValues& controls) const
{
    const auto feedback =
        std::abs(lerp(-0.9f, 0.9f,
                      detail::normalizedControl(controls[6], 0.5f))) * 0.94f;
    const auto delay =
        (16.0
         + exponential(0.1f, 15.0f,
                       detail::normalizedControl(controls[5], 0.0f))
         + lerp(0.0f, 10.0f,
                detail::normalizedControl(controls[4], 0.0f)))
                       * 0.001;
    return feedback < 0.001f
        ? juce::jlimit(0.03, 0.1, delay * 2.0)
        : juce::jlimit(
              0.03, 4.0, delay * std::log(0.001) / std::log(feedback));
}
} // namespace megadsp
