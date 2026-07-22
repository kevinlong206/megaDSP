#include "FrequencyLab.h"
#include "DspHelpers.h"
#include "DspSafety.h"

#include <cmath>

namespace megadsp
{
namespace
{
using detail::exponential;
using detail::lerp;

float onePoleCoefficient(double sampleRate, float frequency)
{
    return 1.0f - std::exp(
        -juce::MathConstants<float>::twoPi * frequency
        / static_cast<float>(sampleRate));
}
} // namespace

void FrequencyLabModule::advance(Oscillator& oscillator, float radians) noexcept
{
    const auto x2 = radians * radians;
    const auto x4 = x2 * x2;
    const auto x6 = x4 * x2;
    const auto x8 = x4 * x4;
    const auto sine = radians * (1.0f - x2 * (1.0f / 6.0f)
                                  + x4 * (1.0f / 120.0f)
                                  - x6 * (1.0f / 5040.0f)
                                  + x8 * (1.0f / 362880.0f));
    const auto cosine = 1.0f - x2 * 0.5f + x2 * x2 * (1.0f / 24.0f)
                        - x6 * (1.0f / 720.0f)
                        + x8 * (1.0f / 40320.0f)
                        - x8 * x2 * (1.0f / 3628800.0f);
    const auto oldCosine = oscillator.cosine;
    oscillator.cosine = oldCosine * cosine - oscillator.sine * sine;
    oscillator.sine = oscillator.sine * cosine + oldCosine * sine;
}

void FrequencyLabModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    for (auto* smoother : { &shiftSmoothed, &fineSmoothed, &feedbackSmoothed,
                            &depthSmoothed, &offsetSmoothed, &mixSmoothed })
        smoother->reset(sampleRate, 0.035);
    for (auto* smoother : { &rateSmoothed, &lowCutSmoothed, &highCutSmoothed,
                            &outputSmoothed })
        smoother->reset(sampleRate, 0.035);

    coefficients.fill(0.0f);
    constexpr auto centre = reportedLatencySamples;
    for (int tap = 0; tap < hilbertTaps; ++tap)
    {
        const auto offset = tap - centre;
        if (offset != 0 && (std::abs(offset) & 1) != 0)
        {
            const auto phase = juce::MathConstants<float>::twoPi
                               * static_cast<float>(tap)
                               / static_cast<float>(hilbertTaps - 1);
            const auto window = 0.42f - 0.5f * std::cos(phase)
                                + 0.08f * std::cos(2.0f * phase);
            coefficients[static_cast<size_t>(tap)] =
                2.0f * window
                / (juce::MathConstants<float>::pi
                   * static_cast<float>(offset));
        }
    }
    reset();
}

void FrequencyLabModule::reset()
{
    for (auto& history : inputHistory)
        history.fill(0.0f);
    for (auto& history : dryHistory)
        history.fill(0.0f);
    highPassState.fill(0.0f);
    lowPassState.fill(0.0f);
    feedbackState.fill(0.0f);
    carrier = {};
    lfo = {};
    historyPosition = 0;
    dryPosition = 0;
    renormalizeCounter = 0;
    initialized = false;
    telemetrySequence = 0;
    telemetry.clear();
}

void FrequencyLabModule::process(juce::AudioBuffer<float>& buffer,
                                 const ControlValues& controls,
                                 const ProcessEnvironment& environment)
{
    const auto channels = juce::jlimit(0, 2, buffer.getNumChannels());
    if (channels == 0)
        return;

    const auto shift = lerp(
        -5000.0f, 5000.0f, detail::normalizedControl(controls[0], 0.51f));
    const auto fine = lerp(
        -50.0f, 50.0f, detail::normalizedControl(controls[1], 0.5f));
    const auto feedback = lerp(-0.8f, 0.8f,
        detail::normalizedControl(controls[2], 0.5f));
    const auto rate = exponential(
        0.02f, 20.0f, detail::normalizedControl(controls[3], 0.0f));
    const auto depth =
        detail::normalizedControl(controls[4], 0.0f) * 2000.0f;
    const auto offset = lerp(
        -500.0f, 500.0f, detail::normalizedControl(controls[5], 0.5f));
    const auto lowCut = exponential(20.0f, 2000.0f,
        detail::normalizedControl(controls[6], 0.15f));
    const auto highCut = exponential(1000.0f, 20000.0f,
        detail::normalizedControl(controls[7], 0.9f));
    const auto mix = detail::normalizedControl(controls[8], 1.0f);
    const auto output = juce::Decibels::decibelsToGain(
        lerp(-18.0f, 12.0f,
             detail::normalizedControl(controls[9], 0.6f)));

    if (!initialized)
    {
        shiftSmoothed.setCurrentAndTargetValue(shift);
        fineSmoothed.setCurrentAndTargetValue(fine);
        feedbackSmoothed.setCurrentAndTargetValue(feedback);
        rateSmoothed.setCurrentAndTargetValue(rate);
        depthSmoothed.setCurrentAndTargetValue(depth);
        offsetSmoothed.setCurrentAndTargetValue(offset);
        lowCutSmoothed.setCurrentAndTargetValue(lowCut);
        highCutSmoothed.setCurrentAndTargetValue(highCut);
        mixSmoothed.setCurrentAndTargetValue(mix);
        outputSmoothed.setCurrentAndTargetValue(output);
        initialized = true;
    }
    shiftSmoothed.setTargetValue(shift);
    fineSmoothed.setTargetValue(fine);
    feedbackSmoothed.setTargetValue(feedback);
    rateSmoothed.setTargetValue(rate);
    depthSmoothed.setTargetValue(depth);
    offsetSmoothed.setTargetValue(offset);
    lowCutSmoothed.setTargetValue(lowCut);
    highCutSmoothed.setTargetValue(highCut);
    mixSmoothed.setTargetValue(mix);
    outputSmoothed.setTargetValue(output);

    const auto inverseSampleRate = 1.0f / static_cast<float>(sampleRate);
    float telemetryCommonShift = 0.0f;
    float telemetryOffset = 0.0f;
    float telemetryRate = 0.0f;
    float telemetryDepth = 0.0f;
    std::array<float, 2> telemetryFrequency {};
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto currentRate = rateSmoothed.getNextValue();
        advance(lfo, juce::MathConstants<float>::twoPi
                         * currentRate * inverseSampleRate);
        const auto commonShift = shiftSmoothed.getNextValue()
            + fineSmoothed.getNextValue()
            + depthSmoothed.getNextValue() * lfo.sine;
        const auto currentOffset = offsetSmoothed.getNextValue();
        telemetryCommonShift = commonShift;
        telemetryOffset = currentOffset;
        telemetryRate = currentRate;
        telemetryDepth = depthSmoothed.getCurrentValue();
        const auto currentFeedback = feedbackSmoothed.getNextValue();
        const auto lowCoefficient = onePoleCoefficient(
            sampleRate, lowCutSmoothed.getNextValue());
        const auto highCoefficient = onePoleCoefficient(
            sampleRate, juce::jmin(
                highCutSmoothed.getNextValue(),
                static_cast<float>(sampleRate * 0.45)));
        const auto currentMix = mixSmoothed.getNextValue();
        const auto currentOutput = outputSmoothed.getNextValue();

        for (int channel = 0; channel < channels; ++channel)
        {
            const auto dryInput =
                detail::finiteSample(buffer.getSample(channel, sample));
            auto& history = inputHistory[static_cast<size_t>(channel)];
            history[static_cast<size_t>(historyPosition)] =
                dryInput + feedbackState[static_cast<size_t>(channel)]
                               * currentFeedback;

            float quadrature = 0.0f;
            for (int tap = 0; tap < hilbertTaps; ++tap)
            {
                auto index = historyPosition - tap;
                if (index < 0)
                    index += hilbertTaps;
                quadrature += coefficients[static_cast<size_t>(tap)]
                              * history[static_cast<size_t>(index)];
            }
            auto realIndex = historyPosition - reportedLatencySamples;
            if (realIndex < 0)
                realIndex += hilbertTaps;
            const auto delayedReal = history[static_cast<size_t>(realIndex)];

            const auto channelOffset = channels > 1
                ? (channel == 0 ? -0.5f : 0.5f) * currentOffset : 0.0f;
            const auto frequency = juce::jlimit(
                -0.45f * static_cast<float>(sampleRate),
                0.45f * static_cast<float>(sampleRate),
                commonShift + channelOffset);
            telemetryFrequency[static_cast<size_t>(channel)] = frequency;
            auto& oscillator = carrier[static_cast<size_t>(channel)];
            const auto translated = delayedReal * oscillator.cosine
                                     - quadrature * oscillator.sine;
            advance(oscillator, juce::MathConstants<float>::twoPi
                                    * frequency * inverseSampleRate);

            auto& lowState = highPassState[static_cast<size_t>(channel)];
            lowState += lowCoefficient * (translated - lowState);
            const auto highPassed = translated - lowState;
            auto& highState = lowPassState[static_cast<size_t>(channel)];
            highState += highCoefficient * (highPassed - highState);
            const auto wet = std::isfinite(highState) ? highState : 0.0f;
            feedbackState[static_cast<size_t>(channel)] = wet;

            auto& dryDelay = dryHistory[static_cast<size_t>(channel)];
            const auto delayedDry = dryDelay[static_cast<size_t>(dryPosition)];
            dryDelay[static_cast<size_t>(dryPosition)] = dryInput;
            buffer.setSample(
                channel, sample,
                detail::finiteSample(
                    (delayedDry + (wet - delayedDry) * currentMix)
                    * currentOutput));
        }
        historyPosition = (historyPosition + 1) % hilbertTaps;
        dryPosition = (dryPosition + 1) % reportedLatencySamples;

        if (++renormalizeCounter >= 128)
        {
            renormalizeCounter = 0;
            auto normalize = [](Oscillator& oscillator)
            {
                const auto magnitude = std::sqrt(
                    oscillator.cosine * oscillator.cosine
                    + oscillator.sine * oscillator.sine);
                if (std::isfinite(magnitude) && magnitude > 0.5f)
                {
                    oscillator.cosine /= magnitude;
                    oscillator.sine /= magnitude;
                }
                else
                    oscillator = {};
            };
            normalize(lfo);
            for (auto& oscillator : carrier)
                normalize(oscillator);
        }
    }

    if (environment.captureTelemetry)
    {
        ContinuousTelemetrySnapshot snapshot;
        snapshot.sequence = ++telemetrySequence;
        snapshot.valueCount = telemetryValueCount;
        snapshot.values[commonShiftHz] = telemetryCommonShift;
        snapshot.values[leftShiftHz] = telemetryFrequency[0];
        snapshot.values[rightShiftHz] =
            channels > 1 ? telemetryFrequency[1] : telemetryFrequency[0];
        snapshot.values[lfoPosition] = lfo.sine;
        snapshot.values[lfoRateHz] = telemetryRate;
        snapshot.values[modulationDepthHz] = telemetryDepth;
        snapshot.values[stereoOffsetHz] = telemetryOffset;
        telemetry.publish(snapshot);
    }
}

bool FrequencyLabModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}

double FrequencyLabModule::tailSeconds(const ControlValues& controls) const
{
    const auto feedback = std::abs(lerp(
        -0.8f, 0.8f, detail::normalizedControl(controls[2], 0.5f)));
    if (feedback < 0.001f)
        return static_cast<double>(reportedLatencySamples) / sampleRate;
    return juce::jlimit(
        0.0, 8.0,
        std::log(0.001) / std::log(static_cast<double>(feedback))
            * static_cast<double>(hilbertTaps) / sampleRate
            + static_cast<double>(hilbertTaps) / sampleRate);
}
} // namespace megadsp
