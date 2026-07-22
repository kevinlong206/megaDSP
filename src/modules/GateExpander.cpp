#include "GateExpander.h"
#include "DspHelpers.h"
#include "DspSafety.h"

#include <cmath>

namespace megadsp
{
using detail::coefficient;
using detail::exponential;
using detail::lerp;

void GateExpanderModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    for (auto& value : smoothed)
        value.reset(sampleRate, 0.025);
    reset();
}

void GateExpanderModule::reset()
{
    for (auto& filter : detectorFilters)
        filter.reset();
    rmsPower.fill(0.0f);
    gainDbState.fill(0.0f);
    holdSamplesRemaining.fill(0);
    open.fill(true);
    for (auto& value : smoothed)
        value.setCurrentAndTargetValue(0.0f);
    initialized = false;
    gainReductionDb.store(0.0f, std::memory_order_relaxed);
    detectorLevelDb.store(-100.0f, std::memory_order_relaxed);
    telemetryState = {};
    telemetry.clear();
}

bool GateExpanderModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}

float GateExpanderModule::DetectorFilter::process(
    float input, float lowCut, float highCut, double rate)
{
    if (!std::isfinite(input))
        input = 0.0f;
    const auto hpPole = std::exp(
        -juce::MathConstants<float>::twoPi * lowCut
        / static_cast<float>(rate));
    highPassState = (1.0f - hpPole) * input + hpPole * highPassState;
    const auto highPassed = input - highPassState;
    const auto lpPole = std::exp(
        -juce::MathConstants<float>::twoPi * highCut
        / static_cast<float>(rate));
    lowPassState = (1.0f - lpPole) * highPassed
                   + lpPole * lowPassState;
    if (!std::isfinite(highPassState) || !std::isfinite(lowPassState))
    {
        reset();
        return 0.0f;
    }
    return lowPassState;
}

void GateExpanderModule::DetectorFilter::reset()
{
    highPassState = 0.0f;
    lowPassState = 0.0f;
}

double GateExpanderModule::tailSeconds(
    const ControlValues& controls) const
{
    return static_cast<double>(
               lerp(0.0f, 500.0f,
                    detail::normalizedControl(controls[3], 0.0f))
               + exponential(
                   5.0f, 2000.0f,
                   detail::normalizedControl(controls[4], 0.0f)))
           * 0.001;
}

void GateExpanderModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    const auto channelCount = juce::jmin(2, buffer.getNumChannels());
    const auto sampleCount = buffer.getNumSamples();
    if (channelCount <= 0 || sampleCount <= 0)
        return;

    const auto externalAvailable =
        environment.sidechain != nullptr
        && environment.sidechain->getNumChannels() > 0
        && environment.sidechain->getNumSamples() >= sampleCount;
    std::array<float, 11> targets {};
    for (int control = 0; control < 11; ++control)
        targets[static_cast<size_t>(control)] = detail::normalizedControl(
            controls[static_cast<size_t>(control)], 0.0f);
    targets[8] = targets[8] >= 0.5f && externalAvailable ? 1.0f : 0.0f;
    targets[9] = targets[9] >= 0.5f ? 1.0f : 0.0f;

    if (!initialized)
    {
        for (int control = 0; control < 11; ++control)
            smoothed[static_cast<size_t>(control)]
                .setCurrentAndTargetValue(targets[static_cast<size_t>(control)]);
        initialized = true;
    }
    else
    {
        for (int control = 0; control < 11; ++control)
            smoothed[static_cast<size_t>(control)]
                .setTargetValue(targets[static_cast<size_t>(control)]);
    }

    const auto rmsCoefficient = coefficient(sampleRate, 12.0f);
    float maximumReduction = 0.0f;
    float maximumDetector = 0.0f;

    for (int sample = 0; sample < sampleCount; ++sample)
    {
        std::array<float, 11> current {};
        for (int control = 0; control < 11; ++control)
            current[static_cast<size_t>(control)] =
                smoothed[static_cast<size_t>(control)].getNextValue();

        const auto thresholdDb = lerp(-80.0f, 0.0f, current[0]);
        const auto rangeDb = lerp(0.0f, 80.0f, current[1]);
        const auto attackMs = exponential(0.05f, 100.0f, current[2]);
        const auto holdSamples = juce::roundToInt(
            lerp(0.0f, 500.0f, current[3])
            * static_cast<float>(sampleRate) * 0.001f);
        const auto releaseMs = exponential(5.0f, 2000.0f, current[4]);
        const auto hysteresisDb = lerp(0.0f, 18.0f, current[5]);
        const auto lowCut = juce::jmin(
            exponential(20.0f, 2000.0f, current[6]),
            static_cast<float>(sampleRate * 0.225));
        const auto highCut = juce::jlimit(
            lowCut * 1.05f, static_cast<float>(sampleRate * 0.475),
            exponential(1000.0f, 20000.0f, current[7]));
        const auto externalMix = current[8];
        const auto listenMix = current[9];
        const auto stereoLink = current[10];
        const auto openingCoefficient = coefficient(sampleRate, attackMs);
        const auto closingCoefficient = coefficient(sampleRate, releaseMs);

        std::array<float, 2> dry {};
        std::array<float, 2> focused {};
        std::array<float, 2> detector {};
        for (int channel = 0; channel < channelCount; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            dry[index] =
                detail::finiteSample(buffer.getSample(channel, sample));
            auto detectorInput = dry[index];
            if (externalAvailable)
            {
                const auto externalChannel = juce::jmin(
                    channel, environment.sidechain->getNumChannels() - 1);
                const auto sidechainSample = detail::finiteSample(
                    environment.sidechain->getSample(externalChannel, sample));
                detectorInput +=
                    (sidechainSample - detectorInput) * externalMix;
            }
            focused[index] = detectorFilters[index].process(
                detectorInput, lowCut, highCut, sampleRate);
            const auto peak = std::abs(focused[index]);
            rmsPower[index] = rmsCoefficient * rmsPower[index]
                              + (1.0f - rmsCoefficient)
                                    * focused[index] * focused[index];
            const auto rms = std::sqrt(juce::jmax(0.0f, rmsPower[index]));
            detector[index] = peak * 0.65f + rms * 0.35f;
        }

        auto linkedDetector = detector[0];
        if (channelCount > 1)
            linkedDetector = juce::jmax(detector[0], detector[1]);
        maximumDetector = juce::jmax(maximumDetector, linkedDetector);

        for (int channel = 0; channel < channelCount; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            const auto effectiveDetector =
                detector[index] + (linkedDetector - detector[index])
                                      * stereoLink;
            const auto levelDb = juce::Decibels::gainToDecibels(
                effectiveDetector, -100.0f);
            const auto closeThresholdDb = thresholdDb - hysteresisDb;

            if (levelDb >= thresholdDb)
            {
                open[index] = true;
                holdSamplesRemaining[index] = holdSamples;
            }
            else if (open[index])
            {
                if (levelDb >= closeThresholdDb)
                    holdSamplesRemaining[index] = holdSamples;
                else if (holdSamplesRemaining[index] > 0)
                    --holdSamplesRemaining[index];
                else
                    open[index] = false;
            }

            float targetGainDb = 0.0f;
            if (!open[index] && rangeDb > 0.0f)
            {
                const auto depth = juce::jlimit(
                    0.0f, 1.0f,
                    (closeThresholdDb - levelDb) / 24.0f);
                const auto smoothDepth = depth * depth * (3.0f - 2.0f * depth);
                targetGainDb = -rangeDb * smoothDepth;
            }
            const auto smoothing = targetGainDb > gainDbState[index]
                                       ? openingCoefficient
                                       : closingCoefficient;
            gainDbState[index] = smoothing * gainDbState[index]
                                 + (1.0f - smoothing) * targetGainDb;
            if (!std::isfinite(gainDbState[index]))
                gainDbState[index] = -rangeDb;
            maximumReduction = juce::jmax(
                maximumReduction, -gainDbState[index]);

            const auto shaped = dry[index]
                * juce::Decibels::decibelsToGain(gainDbState[index]);
            const auto output =
                shaped + (focused[index] - shaped) * listenMix;
            buffer.setSample(channel, sample,
                             std::isfinite(output) ? output : 0.0f);
        }
    }

    gainReductionDb.store(maximumReduction, std::memory_order_relaxed);
    detectorLevelDb.store(
        juce::Decibels::gainToDecibels(maximumDetector, -100.0f),
        std::memory_order_relaxed);
    if (environment.captureTelemetry)
    {
        const auto detector = juce::Decibels::gainToDecibels(
            maximumDetector, -100.0f);
        auto envelope = gainDbState[0];
        auto openChannels = open[0] ? 1.0f : 0.0f;
        if (channelCount > 1)
        {
            envelope = juce::jmin(envelope, gainDbState[1]);
            openChannels += open[1] ? 1.0f : 0.0f;
        }
        const auto openAmount =
            openChannels / static_cast<float>(channelCount);
        telemetryState.sequence += 1;
        telemetryState.valueCount = telemetryValueCount;
        telemetryState.values[detectorDb] = detector;
        telemetryState.values[gainEnvelopeDb] = envelope;
        telemetryState.values[attenuationDb] = maximumReduction;
        telemetryState.values[openFraction] = openAmount;
        appendContinuousTelemetryHistory(
            telemetryState,
            { detector, maximumReduction, openAmount, envelope },
            telemetryHistoryCount);
        telemetry.publish(telemetryState);
    }
}
} // namespace megadsp
