#include "AutoWah.h"

#include "DspHelpers.h"
#include "DspSafety.h"

#include <cmath>

namespace megadsp
{
namespace
{
float envelopeCoefficient(double sampleRate, float milliseconds) noexcept
{
    return std::exp(
        -1.0f / juce::jmax(
            1.0f, milliseconds * 0.001f * static_cast<float>(sampleRate)));
}
} // namespace

void AutoWahModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    for (auto& value : smoothed)
        value.reset(sampleRate, 0.03);
    for (auto& weight : filterWeights)
        weight.reset(sampleRate, 0.03);
    reset();
}

void AutoWahModule::reset()
{
    channels = {};
    for (auto& value : smoothed)
        value.setCurrentAndTargetValue(0.0f);
    for (auto& weight : filterWeights)
        weight.setCurrentAndTargetValue(0.0f);
    initialized = false;
    meterEnvelope = 0.0f;
    telemetrySequence = 0;
    telemetry.clear();
    meter.store(0.0f, std::memory_order_relaxed);
}

float AutoWahModule::renderFilter(
    ChannelState& state, float input, float frequency, float resonance,
    float drive, const std::array<float, 4>& weights) const noexcept
{
    const auto driveGain = 1.0f + drive * 9.0f;
    const auto referenceLevel = 0.25f;
    const auto saturated =
        std::tanh(input * driveGain)
        * (referenceLevel
           / std::tanh(referenceLevel * driveGain));
    const auto driven = detail::lerp(input, saturated, drive);
    const auto cutoff = juce::jlimit(
        35.0f, static_cast<float>(sampleRate * 0.45), frequency);
    const auto g = std::tan(
        juce::MathConstants<float>::pi * cutoff
        / static_cast<float>(sampleRate));
    const auto damping = detail::lerp(1.55f, 0.08f, resonance);
    const auto a1 = 1.0f / (1.0f + g * (g + damping));
    const auto a2 = g * a1;
    const auto a3 = g * a2;
    const auto v3 = driven - state.integrator2;
    const auto band = a1 * state.integrator1 + a2 * v3;
    const auto low =
        state.integrator2 + a2 * state.integrator1 + a3 * v3;
    state.integrator1 = detail::finiteSample(
        2.0f * band - state.integrator1);
    state.integrator2 = detail::finiteSample(
        2.0f * low - state.integrator2);

    const std::array<float, 4> voices {
        low * detail::lerp(1.0f, 1.25f, resonance),
        band * detail::lerp(1.10f, 2.80f, resonance),
        driven + band * detail::lerp(0.40f, 2.40f, resonance),
        low * 0.25f + band * detail::lerp(1.05f, 3.20f, resonance)
    };
    auto voiced = 0.0f;
    for (size_t type = 0; type < voices.size(); ++type)
        voiced += voices[type] * weights[type];
    constexpr auto outputDrive = 0.85f;
    return detail::finiteSample(
        std::tanh(voiced * outputDrive) / std::tanh(outputDrive));
}

void AutoWahModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    juce::ScopedNoDenormals noDenormals;
    const auto channelCount = juce::jmin(2, buffer.getNumChannels());
    const auto sampleCount = buffer.getNumSamples();
    if (channelCount <= 0 || sampleCount <= 0)
        return;

    std::array<float, controlsPerSlot> targets {};
    for (int control = 0; control < controlsPerSlot; ++control)
        targets[static_cast<size_t>(control)] =
            detail::normalizedControl(
                controls[static_cast<size_t>(control)], 0.5f);
    targets[directionControl] =
        targets[directionControl] >= 0.5f ? 1.0f : 0.0f;
    const auto selectedFilterType = juce::jlimit(
        0, 3,
        static_cast<int>(targets[filterTypeControl] * 4.0f));

    for (int control = 0; control < controlsPerSlot; ++control)
    {
        auto& value = smoothed[static_cast<size_t>(control)];
        if (initialized)
            value.setTargetValue(targets[static_cast<size_t>(control)]);
        else
            value.setCurrentAndTargetValue(
                targets[static_cast<size_t>(control)]);
    }
    for (int type = 0; type < 4; ++type)
    {
        const auto target = type == selectedFilterType ? 1.0f : 0.0f;
        if (initialized)
            filterWeights[static_cast<size_t>(type)].setTargetValue(target);
        else
            filterWeights[static_cast<size_t>(type)]
                .setCurrentAndTargetValue(target);
    }
    initialized = true;

    const auto fastAttack = envelopeCoefficient(sampleRate, 0.20f);
    const auto fastRelease = envelopeCoefficient(sampleRate, 18.0f);
    const auto slowCoefficient = envelopeCoefficient(sampleRate, 55.0f);
    float maximumDetector = 0.0f;
    float maximumPick = 0.0f;
    float latestEnvelope = 0.0f;
    float latestFrequency = 0.0f;
    float telemetryDetector = -1.0f;
    double wetEnergyAccumulator = 0.0;

    for (int sample = 0; sample < sampleCount; ++sample)
    {
        std::array<float, controlsPerSlot> current {};
        for (int control = 0; control < controlsPerSlot; ++control)
            current[static_cast<size_t>(control)] =
                smoothed[static_cast<size_t>(control)].getNextValue();

        const auto thresholdDb = detail::lerp(-18.0f, -66.0f, current[0]);
        const auto centerFrequency =
            detail::exponential(120.0f, 1600.0f, current[1]);
        const auto sweepOctaves = detail::lerp(0.5f, 5.0f, current[2]);
        const auto attackCoefficient = envelopeCoefficient(
            sampleRate, detail::exponential(0.5f, 80.0f, current[3]));
        const auto releaseCoefficient = envelopeCoefficient(
            sampleRate, detail::exponential(30.0f, 1400.0f, current[4]));
        const auto resonance = current[5];
        const auto direction = current[6];
        const auto pickFocus = current[7];
        const auto drive = current[8];
        std::array<float, 4> currentFilterWeights {};
        for (size_t type = 0; type < currentFilterWeights.size(); ++type)
            currentFilterWeights[type] = filterWeights[type].getNextValue();
        const auto mix = current[10];
        const auto output = juce::Decibels::decibelsToGain(
            detail::lerp(-18.0f, 12.0f, current[11]));

        std::array<float, 2> dry {};
        std::array<float, 2> detector {};
        std::array<float, 2> pick {};
        for (int channel = 0; channel < channelCount; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            dry[index] = detail::finiteSample(
                buffer.getSample(channel, sample));
            const auto magnitude = std::abs(dry[index]);
            const auto fastCoefficient =
                magnitude > channels[index].fastEnvelope
                    ? fastAttack : fastRelease;
            channels[index].fastEnvelope =
                fastCoefficient * channels[index].fastEnvelope
                + (1.0f - fastCoefficient) * magnitude;
            channels[index].slowEnvelope =
                slowCoefficient * channels[index].slowEnvelope
                + (1.0f - slowCoefficient) * magnitude;
            detector[index] = channels[index].fastEnvelope;
            pick[index] = juce::jmax(
                0.0f,
                channels[index].fastEnvelope - channels[index].slowEnvelope);
        }
        if (channelCount == 1)
        {
            dry[1] = dry[0];
            detector[1] = detector[0];
            pick[1] = pick[0];
        }

        const auto linkedDetector = juce::jmax(detector[0], detector[1]);
        const auto linkedPick = juce::jmax(pick[0], pick[1]);
        maximumDetector = juce::jmax(maximumDetector, linkedDetector);
        maximumPick = juce::jmax(maximumPick, linkedPick);

        std::array<float, 2> wet {};
        for (int channel = 0; channel < channelCount; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            const auto effectiveDetector = linkedDetector;
            const auto effectivePick = linkedPick;
            const auto levelDb = juce::Decibels::gainToDecibels(
                effectiveDetector, -100.0f);
            const auto levelAmount = juce::jlimit(
                0.0f, 1.0f, (levelDb - thresholdDb) / 24.0f);
            const auto transientAmount = juce::jlimit(
                0.0f, 1.0f,
                effectivePick
                    / juce::Decibels::decibelsToGain(thresholdDb) * 1.8f);
            const auto targetEnvelope = juce::jlimit(
                0.0f, 1.0f,
                levelAmount + transientAmount * pickFocus);
            const auto coefficient =
                targetEnvelope > channels[index].sweepEnvelope
                    ? attackCoefficient : releaseCoefficient;
            channels[index].sweepEnvelope =
                coefficient * channels[index].sweepEnvelope
                + (1.0f - coefficient) * targetEnvelope;

            const auto centeredEnvelope =
                channels[index].sweepEnvelope - 0.5f;
            const auto upwardFrequency =
                centerFrequency * std::exp2(
                    sweepOctaves * centeredEnvelope);
            const auto downwardFrequency =
                centerFrequency * std::exp2(
                    -sweepOctaves * centeredEnvelope);
            const auto frequency = detail::lerp(
                upwardFrequency, downwardFrequency, direction);
            wet[index] = renderFilter(
                channels[index], dry[index], frequency, resonance, drive,
                currentFilterWeights);
            if (effectiveDetector >= telemetryDetector)
            {
                telemetryDetector = effectiveDetector;
                latestEnvelope = channels[index].sweepEnvelope;
                latestFrequency = frequency;
            }
        }
        if (channelCount == 1)
            wet[1] = wet[0];

        wetEnergyAccumulator += 0.5 * (
            static_cast<double>(wet[0]) * wet[0]
            + static_cast<double>(wet[1]) * wet[1]);
        const auto dryGain =
            std::cos(mix * juce::MathConstants<float>::halfPi);
        const auto wetGain =
            std::sin(mix * juce::MathConstants<float>::halfPi);
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto index =
                static_cast<size_t>(juce::jmin(channel, 1));
            buffer.setSample(
                channel, sample,
                detail::finiteSample(
                    output
                    * (dry[index] * dryGain + wet[index] * wetGain)));
        }
    }

    const auto wetRms = static_cast<float>(std::sqrt(
        wetEnergyAccumulator / juce::jmax(1, sampleCount)));
    meterEnvelope += 0.16f * (wetRms - meterEnvelope);
    meter.store(
        juce::jlimit(0.0f, 1.0f, meterEnvelope),
        std::memory_order_relaxed);

    if (environment.captureTelemetry)
    {
        ContinuousTelemetrySnapshot snapshot;
        snapshot.sequence = ++telemetrySequence;
        snapshot.valueCount = telemetryValueCount;
        snapshot.values[inputLevelDb] = juce::Decibels::gainToDecibels(
            maximumDetector, -100.0f);
        snapshot.values[thresholdDb] =
            detail::lerp(-18.0f, -66.0f, targets[0]);
        snapshot.values[envelopeAmount] = latestEnvelope;
        snapshot.values[currentFrequencyHz] = latestFrequency;
        snapshot.values[pickActivity] = maximumPick;
        snapshot.values[wetEnergy] = wetRms;
        telemetry.publish(snapshot);
    }
}

bool AutoWahModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}
} // namespace megadsp
