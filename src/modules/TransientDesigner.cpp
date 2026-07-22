#include "TransientDesigner.h"
#include "DspHelpers.h"
#include "DspSafety.h"

#include <cmath>

namespace megadsp
{
using detail::coefficient;
using detail::exponential;
using detail::lerp;

void TransientDesignerModule::prepare(
    const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    for (auto& value : smoothed)
        value.reset(sampleRate, 0.02);
    reset();
}

void TransientDesignerModule::reset()
{
    for (auto& filter : focusFilters)
        filter.reset();
    fastEnvelope.fill(0.0f);
    slowEnvelope.fill(0.0f);
    gainDbState.fill(0.0f);
    for (auto& value : smoothed)
        value.setCurrentAndTargetValue(0.0f);
    initialized = false;
    shapingAmountDb.store(0.0f, std::memory_order_relaxed);
    detectorLevelDb.store(-100.0f, std::memory_order_relaxed);
    telemetryState = {};
    telemetry.clear();
}

bool TransientDesignerModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}

float TransientDesignerModule::BandPass::process(float input)
{
    if (!std::isfinite(input))
        input = 0.0f;
    const auto output = b0 * input + z1;
    z1 = b1 * input - a1 * output + z2;
    z2 = b2 * input - a2 * output;
    if (!std::isfinite(output) || !std::isfinite(z1) || !std::isfinite(z2))
    {
        reset();
        return 0.0f;
    }
    return output;
}

void TransientDesignerModule::BandPass::set(
    double rate, float frequency)
{
    const auto centre = juce::jlimit(
        20.0f, static_cast<float>(rate * 0.475), frequency);
    const auto omega = juce::MathConstants<float>::twoPi * centre
                       / static_cast<float>(rate);
    constexpr auto q = 0.70710678f;
    const auto alpha = std::sin(omega) / (2.0f * q);
    const auto denominator = 1.0f + alpha;
    b0 = alpha / denominator;
    b1 = 0.0f;
    b2 = -b0;
    a1 = -2.0f * std::cos(omega) / denominator;
    a2 = (1.0f - alpha) / denominator;
}

void TransientDesignerModule::BandPass::reset()
{
    z1 = 0.0f;
    z2 = 0.0f;
}

void TransientDesignerModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    const auto channelCount = juce::jmin(2, buffer.getNumChannels());
    const auto sampleCount = buffer.getNumSamples();
    if (channelCount <= 0 || sampleCount <= 0)
        return;

    std::array<float, 8> targets {};
    for (int control = 0; control < 8; ++control)
        targets[static_cast<size_t>(control)] = detail::normalizedControl(
            controls[static_cast<size_t>(control)], 0.0f);
    targets[5] = targets[5] >= 0.5f ? 1.0f : 0.0f;
    if (!initialized)
    {
        for (int control = 0; control < 8; ++control)
            smoothed[static_cast<size_t>(control)]
                .setCurrentAndTargetValue(targets[static_cast<size_t>(control)]);
        initialized = true;
    }
    else
    {
        for (int control = 0; control < 8; ++control)
            smoothed[static_cast<size_t>(control)]
                .setTargetValue(targets[static_cast<size_t>(control)]);
    }

    float maximumShaping = 0.0f;
    float maximumDetector = 0.0f;
    std::array<float, 2> latestAttackFeature {};
    std::array<float, 2> latestSustainFeature {};
    float strongestAttackShaping = 0.0f;
    float strongestSustainShaping = 0.0f;
    float strongestAppliedShaping = 0.0f;
    for (int sample = 0; sample < sampleCount; ++sample)
    {
        std::array<float, 8> current {};
        for (int control = 0; control < 8; ++control)
            current[static_cast<size_t>(control)] =
                smoothed[static_cast<size_t>(control)].getNextValue();

        const auto attackAmount = lerp(-1.0f, 1.0f, current[0]);
        const auto sustainAmount = lerp(-1.0f, 1.0f, current[1]);
        const auto sensitivity = current[2];
        const auto speedMs = exponential(5.0f, 200.0f, current[3]);
        const auto focus = juce::jmin(
            exponential(80.0f, 8000.0f, current[4]),
            static_cast<float>(sampleRate * 0.45));
        const auto guardMix = current[5];
        const auto wetMix = current[6];
        const auto outputGain = juce::Decibels::decibelsToGain(
            lerp(-18.0f, 18.0f, current[7]));
        const auto fastAttack = coefficient(sampleRate, 0.35f);
        const auto fastRelease = coefficient(
            sampleRate, juce::jmax(2.0f, speedMs * 0.35f));
        const auto slowAttack = coefficient(sampleRate, speedMs);
        const auto slowRelease = coefficient(sampleRate, speedMs * 2.5f);
        const auto gainSmoothing = coefficient(sampleRate, 0.5f);

        for (int channel = 0; channel < channelCount; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            const auto dry =
                detail::finiteSample(buffer.getSample(channel, sample));
            focusFilters[index].set(sampleRate, focus);
            const auto detectorSample = focusFilters[index].process(dry);
            const auto magnitude = std::abs(detectorSample);
            maximumDetector = juce::jmax(maximumDetector, magnitude);

            const auto fastCoefficient =
                magnitude > fastEnvelope[index] ? fastAttack : fastRelease;
            const auto slowCoefficient =
                magnitude > slowEnvelope[index] ? slowAttack : slowRelease;
            fastEnvelope[index] = fastCoefficient * fastEnvelope[index]
                                  + (1.0f - fastCoefficient) * magnitude;
            slowEnvelope[index] = slowCoefficient * slowEnvelope[index]
                                  + (1.0f - slowCoefficient) * magnitude;

            const auto fastDb = juce::Decibels::gainToDecibels(
                fastEnvelope[index], -100.0f);
            const auto slowDb = juce::Decibels::gainToDecibels(
                slowEnvelope[index], -100.0f);
            const auto ratioDb = juce::jmax(0.0f, fastDb - slowDb);
            const auto onsetDb = lerp(9.0f, 1.0f, sensitivity);
            auto attackFeature = juce::jlimit(
                0.0f, 1.0f, (ratioDb - onsetDb) / 12.0f);
            attackFeature =
                attackFeature * attackFeature * (3.0f - 2.0f * attackFeature);
            const auto activityThreshold = lerp(-42.0f, -72.0f, sensitivity);
            const auto activity = juce::jlimit(
                0.0f, 1.0f, (slowDb - activityThreshold) / 12.0f);
            const auto sustainFeature =
                activity * (1.0f - attackFeature * 0.8f);
            latestAttackFeature[index] = attackFeature;
            latestSustainFeature[index] = sustainFeature;
            const auto attackShaping =
                attackAmount * attackFeature * 18.0f;
            const auto sustainShaping =
                sustainAmount * sustainFeature * 12.0f;
            const auto targetGainDb = juce::jlimit(
                -24.0f, 24.0f,
                attackShaping + sustainShaping);
            gainDbState[index] = gainSmoothing * gainDbState[index]
                                 + (1.0f - gainSmoothing) * targetGainDb;
            if (!std::isfinite(gainDbState[index]))
                gainDbState[index] = 0.0f;
            maximumShaping = juce::jmax(
                maximumShaping, std::abs(gainDbState[index]));
            if (std::abs(attackShaping) > std::abs(strongestAttackShaping))
                strongestAttackShaping = attackShaping;
            if (std::abs(sustainShaping) > std::abs(strongestSustainShaping))
                strongestSustainShaping = sustainShaping;
            if (std::abs(gainDbState[index])
                > std::abs(strongestAppliedShaping))
                strongestAppliedShaping = gainDbState[index];

            const auto shaped = dry * juce::Decibels::decibelsToGain(
                                          gainDbState[index]);
            const auto absoluteShaped = std::abs(shaped);
            const auto guarded = absoluteShaped <= 1.0f
                ? shaped
                : std::copysign(
                      1.0f + 0.5f * std::tanh(
                                        (absoluteShaped - 1.0f) * 2.0f),
                      shaped);
            const auto safeWet = shaped + (guarded - shaped) * guardMix;
            const auto output =
                (dry + (safeWet - dry) * wetMix) * outputGain;
            buffer.setSample(channel, sample,
                             std::isfinite(output) ? output : 0.0f);
        }
    }

    shapingAmountDb.store(maximumShaping, std::memory_order_relaxed);
    detectorLevelDb.store(
        juce::Decibels::gainToDecibels(maximumDetector, -100.0f),
        std::memory_order_relaxed);
    if (environment.captureTelemetry)
    {
        const auto fast = juce::jmax(
            fastEnvelope[0],
            channelCount > 1 ? fastEnvelope[1] : 0.0f);
        const auto slow = juce::jmax(
            slowEnvelope[0],
            channelCount > 1 ? slowEnvelope[1] : 0.0f);
        const auto attack = juce::jmax(
            latestAttackFeature[0],
            channelCount > 1 ? latestAttackFeature[1] : 0.0f);
        const auto sustain = juce::jmax(
            latestSustainFeature[0],
            channelCount > 1 ? latestSustainFeature[1] : 0.0f);
        const auto fastDb = juce::Decibels::gainToDecibels(
            fast, -100.0f);
        const auto slowDb = juce::Decibels::gainToDecibels(
            slow, -100.0f);
        telemetryState.sequence += 1;
        telemetryState.valueCount = telemetryValueCount;
        telemetryState.values[fastEnvelopeDb] = fastDb;
        telemetryState.values[slowEnvelopeDb] = slowDb;
        telemetryState.values[attackFeature] = attack;
        telemetryState.values[sustainFeature] = sustain;
        telemetryState.values[attackShapingDb] = strongestAttackShaping;
        telemetryState.values[sustainShapingDb] = strongestSustainShaping;
        telemetryState.values[appliedShapingDb] = strongestAppliedShaping;
        appendContinuousTelemetryHistory(
            telemetryState,
            { fastDb, slowDb, strongestAttackShaping,
              strongestSustainShaping },
            telemetryHistoryCount);
        telemetry.publish(telemetryState);
    }
}
} // namespace megadsp
