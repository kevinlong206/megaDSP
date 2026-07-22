#include "MultibandCompressor.h"
#include "DspHelpers.h"
#include "DspSafety.h"

#include <cmath>

namespace megadsp
{
using detail::coefficient;
using detail::exponential;
using detail::lerp;

void MultibandCompressorModule::prepare(
    const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    for (auto& value : smoothed)
        value.reset(sampleRate, 0.03);
    reset();
}

void MultibandCompressorModule::reset()
{
    for (auto& channel : filters)
        channel.reset();
    for (auto& channel : envelopes)
        channel.fill(0.0f);
    for (auto& channel : gainDbState)
        channel.fill(0.0f);
    for (auto& channel : averageReductionDb)
        channel.fill(0.0f);
    for (auto& channel : makeupDbState)
        channel.fill(0.0f);
    for (auto& value : smoothed)
        value.setCurrentAndTargetValue(0.0f);
    initialized = false;
    gainReductionDb.store(0.0f, std::memory_order_relaxed);
    telemetryState = {};
    telemetry.clear();
}

bool MultibandCompressorModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}

float MultibandCompressorModule::Biquad::process(float input)
{
    if (!std::isfinite(input))
        input = 0.0f;
    const auto output = coefficients.b0 * input + z1;
    z1 = coefficients.b1 * input - coefficients.a1 * output + z2;
    z2 = coefficients.b2 * input - coefficients.a2 * output;
    if (!std::isfinite(output) || !std::isfinite(z1) || !std::isfinite(z2))
    {
        reset();
        return 0.0f;
    }
    return output;
}

void MultibandCompressorModule::Biquad::set(
    const Coefficients& newCoefficients)
{
    coefficients = newCoefficients;
}

void MultibandCompressorModule::Biquad::reset()
{
    z1 = 0.0f;
    z2 = 0.0f;
}

void MultibandCompressorModule::ChannelFilters::reset()
{
    for (auto* cascade : { &lowPass, &upperHighPass, &midLowPass,
                           &highPass, &lowAlignLowPass,
                           &lowAlignHighPass })
        for (auto& filter : *cascade)
            filter.reset();
}

MultibandCompressorModule::Coefficients
MultibandCompressorModule::lowPassCoefficients(
    double rate, float frequency)
{
    const auto omega = juce::MathConstants<float>::twoPi * frequency
                       / static_cast<float>(rate);
    const auto cosine = std::cos(omega);
    const auto sine = std::sin(omega);
    constexpr auto inverseQ = 1.41421356237f;
    const auto alpha = sine * inverseQ * 0.5f;
    const auto denominator = 1.0f + alpha;
    return {
        (1.0f - cosine) * 0.5f / denominator,
        (1.0f - cosine) / denominator,
        (1.0f - cosine) * 0.5f / denominator,
        -2.0f * cosine / denominator,
        (1.0f - alpha) / denominator
    };
}

MultibandCompressorModule::Coefficients
MultibandCompressorModule::highPassCoefficients(
    double rate, float frequency)
{
    const auto omega = juce::MathConstants<float>::twoPi * frequency
                       / static_cast<float>(rate);
    const auto cosine = std::cos(omega);
    const auto sine = std::sin(omega);
    constexpr auto inverseQ = 1.41421356237f;
    const auto alpha = sine * inverseQ * 0.5f;
    const auto denominator = 1.0f + alpha;
    return {
        (1.0f + cosine) * 0.5f / denominator,
        -(1.0f + cosine) / denominator,
        (1.0f + cosine) * 0.5f / denominator,
        -2.0f * cosine / denominator,
        (1.0f - alpha) / denominator
    };
}

float MultibandCompressorModule::processCascade(
    std::array<Biquad, 2>& cascade, float input)
{
    return cascade[1].process(cascade[0].process(input));
}

void MultibandCompressorModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    const auto channelCount = juce::jmin(2, buffer.getNumChannels());
    const auto sampleCount = buffer.getNumSamples();
    if (channelCount <= 0 || sampleCount <= 0)
        return;

    std::array<float, 12> targets {};
    for (int control = 0; control < 12; ++control)
        targets[static_cast<size_t>(control)] = detail::normalizedControl(
            controls[static_cast<size_t>(control)], 0.0f);
    targets[8] = targets[8] >= 0.5f ? 1.0f : 0.0f;
    if (!initialized)
    {
        for (int control = 0; control < 12; ++control)
            smoothed[static_cast<size_t>(control)]
                .setCurrentAndTargetValue(targets[static_cast<size_t>(control)]);
        initialized = true;
    }
    else
    {
        for (int control = 0; control < 12; ++control)
            smoothed[static_cast<size_t>(control)]
                .setTargetValue(targets[static_cast<size_t>(control)]);
    }

    const auto averageRiseCoefficient = coefficient(sampleRate, 1000.0f);
    const auto averageFallCoefficient = coefficient(sampleRate, 3000.0f);
    const auto makeupCoefficient = coefficient(sampleRate, 200.0f);
    float maximumReduction = 0.0f;
    std::array<std::array<float, 3>, 2> latestReduction {};
    std::array<std::array<float, 3>, 2> latestActive {};

    for (int sample = 0; sample < sampleCount; ++sample)
    {
        std::array<float, 12> current {};
        for (int control = 0; control < 12; ++control)
            current[static_cast<size_t>(control)] =
                smoothed[static_cast<size_t>(control)].getNextValue();

        const auto lowCrossover = juce::jmin(
            exponential(40.0f, 800.0f, current[0]),
            static_cast<float>(sampleRate * 0.20));
        const auto highCrossover = juce::jlimit(
            lowCrossover * 1.5f, static_cast<float>(sampleRate * 0.45),
            exponential(1000.0f, 12000.0f, current[1]));
        const auto lowPass = lowPassCoefficients(sampleRate, lowCrossover);
        const auto lowHighPass =
            highPassCoefficients(sampleRate, lowCrossover);
        const auto highLowPass =
            lowPassCoefficients(sampleRate, highCrossover);
        const auto highPass =
            highPassCoefficients(sampleRate, highCrossover);
        for (int channel = 0; channel < channelCount; ++channel)
        {
            auto& channelFilters = filters[static_cast<size_t>(channel)];
            for (auto& filter : channelFilters.lowPass)
                filter.set(lowPass);
            for (auto& filter : channelFilters.upperHighPass)
                filter.set(lowHighPass);
            for (auto& filter : channelFilters.midLowPass)
                filter.set(highLowPass);
            for (auto& filter : channelFilters.highPass)
                filter.set(highPass);
            for (auto& filter : channelFilters.lowAlignLowPass)
                filter.set(highLowPass);
            for (auto& filter : channelFilters.lowAlignHighPass)
                filter.set(highPass);
        }

        std::array<float, 3> thresholds {
            lerp(-60.0f, 0.0f, current[2]),
            lerp(-60.0f, 0.0f, current[3]),
            lerp(-60.0f, 0.0f, current[4])
        };
        const auto ratio = exponential(1.0f, 20.0f, current[5]);
        const auto attackCoefficient = coefficient(
            sampleRate, exponential(0.1f, 100.0f, current[6]));
        const auto releaseCoefficient = coefficient(
            sampleRate, exponential(20.0f, 2000.0f, current[7]));
        const auto autoMakeup = current[8];
        const auto stereoLink = current[9];
        const auto wetMix = current[10];
        const auto outputGain = juce::Decibels::decibelsToGain(
            lerp(-18.0f, 12.0f, current[11]));

        std::array<float, 2> dry {};
        std::array<std::array<float, 3>, 2> bands {};
        for (int channel = 0; channel < channelCount; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            dry[index] =
                detail::finiteSample(buffer.getSample(channel, sample));
            auto& channelFilters = filters[index];
            const auto low = processCascade(
                channelFilters.lowPass, dry[index]);
            const auto upper = processCascade(
                channelFilters.upperHighPass, dry[index]);
            bands[index][0] =
                processCascade(channelFilters.lowAlignLowPass, low)
                + processCascade(channelFilters.lowAlignHighPass, low);
            bands[index][1] =
                processCascade(channelFilters.midLowPass, upper);
            bands[index][2] =
                processCascade(channelFilters.highPass, upper);

            for (int band = 0; band < 3; ++band)
            {
                const auto bandIndex = static_cast<size_t>(band);
                const auto magnitude = std::abs(bands[index][bandIndex]);
                const auto envelopeCoefficient =
                    magnitude > envelopes[index][bandIndex]
                        ? attackCoefficient : releaseCoefficient;
                envelopes[index][bandIndex] =
                    envelopeCoefficient * envelopes[index][bandIndex]
                    + (1.0f - envelopeCoefficient) * magnitude;
            }
        }

        std::array<float, 3> linkedEnvelopes {};
        for (int band = 0; band < 3; ++band)
        {
            const auto bandIndex = static_cast<size_t>(band);
            linkedEnvelopes[bandIndex] = envelopes[0][bandIndex];
            if (channelCount > 1)
                linkedEnvelopes[bandIndex] = juce::jmax(
                    linkedEnvelopes[bandIndex],
                    envelopes[1][bandIndex]);
        }

        for (int channel = 0; channel < channelCount; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            float wet = 0.0f;
            for (int band = 0; band < 3; ++band)
            {
                const auto bandIndex = static_cast<size_t>(band);
                const auto detector = envelopes[index][bandIndex]
                    + (linkedEnvelopes[bandIndex]
                       - envelopes[index][bandIndex]) * stereoLink;
                const auto levelDb = juce::Decibels::gainToDecibels(
                    detector, -100.0f);
                const auto overDb = juce::jmax(
                    0.0f, levelDb - thresholds[bandIndex]);
                const auto targetGainDb =
                    -overDb * (1.0f - 1.0f / ratio);
                const auto gainCoefficient =
                    targetGainDb < gainDbState[index][bandIndex]
                        ? attackCoefficient : releaseCoefficient;
                gainDbState[index][bandIndex] =
                    gainCoefficient * gainDbState[index][bandIndex]
                    + (1.0f - gainCoefficient) * targetGainDb;
                const auto reduction =
                    juce::jmax(0.0f, -gainDbState[index][bandIndex]);
                maximumReduction =
                    juce::jmax(maximumReduction, reduction);

                const auto active =
                    detector > juce::Decibels::decibelsToGain(-72.0f)
                    && reduction > 0.001f;
                latestReduction[index][bandIndex] = reduction;
                latestActive[index][bandIndex] = active ? 1.0f : 0.0f;
                const auto averageTarget = active ? reduction : 0.0f;
                const auto averageCoefficient =
                    averageTarget > averageReductionDb[index][bandIndex]
                        ? averageRiseCoefficient : averageFallCoefficient;
                averageReductionDb[index][bandIndex] =
                    averageCoefficient
                        * averageReductionDb[index][bandIndex]
                    + (1.0f - averageCoefficient) * averageTarget;
                const auto makeupTarget = autoMakeup
                    * juce::jmin(
                        12.0f, averageReductionDb[index][bandIndex]);
                makeupDbState[index][bandIndex] =
                    makeupCoefficient * makeupDbState[index][bandIndex]
                    + (1.0f - makeupCoefficient) * makeupTarget;
                const auto totalGainDb =
                    gainDbState[index][bandIndex]
                    + makeupDbState[index][bandIndex];
                wet += bands[index][bandIndex]
                       * juce::Decibels::decibelsToGain(totalGainDb);
            }
            const auto output =
                (dry[index] + (wet - dry[index]) * wetMix) * outputGain;
            buffer.setSample(channel, sample,
                             std::isfinite(output) ? output : 0.0f);
        }
    }
    gainReductionDb.store(maximumReduction, std::memory_order_relaxed);
    if (environment.captureTelemetry)
    {
        std::array<float, 3> reduction {};
        std::array<float, 3> active {};
        for (size_t band = 0; band < reduction.size(); ++band)
        {
            reduction[band] = latestReduction[0][band];
            active[band] = latestActive[0][band];
            if (channelCount > 1)
            {
                reduction[band] = juce::jmax(
                    reduction[band], latestReduction[1][band]);
                active[band] =
                    (active[band] + latestActive[1][band]) * 0.5f;
            }
        }
        telemetryState.sequence += 1;
        telemetryState.valueCount = telemetryValueCount;
        telemetryState.values[lowReductionDb] = reduction[0];
        telemetryState.values[midReductionDb] = reduction[1];
        telemetryState.values[highReductionDb] = reduction[2];
        telemetryState.values[lowActive] = active[0];
        telemetryState.values[midActive] = active[1];
        telemetryState.values[highActive] = active[2];
        appendContinuousTelemetryHistory(
            telemetryState,
            { reduction[0], reduction[1], reduction[2],
              active[0] + active[1] + active[2] },
            telemetryHistoryCount);
        telemetry.publish(telemetryState);
    }
}
} // namespace megadsp
