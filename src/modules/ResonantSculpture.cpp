#include "ResonantSculpture.h"

#include "DspHelpers.h"
#include "DspSafety.h"

#include <cmath>

namespace megadsp
{
namespace
{
constexpr float twoPi = juce::MathConstants<float>::twoPi;
constexpr float minimumTrackedFrequency = 45.0f;
constexpr float maximumTrackedFrequency = 1000.0f;
constexpr std::array<float, ResonantSculptureModule::modeCount> membraneRatios {
    1.0f, 1.59f, 2.14f, 2.30f, 2.65f, 2.92f,
    3.16f, 3.50f, 3.60f, 3.92f, 4.06f, 4.30f
};
constexpr std::array<float, ResonantSculptureModule::modeCount> modeSigns {
    1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f,
    -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f
};

float coefficient(double sampleRate, float milliseconds) noexcept
{
    return std::exp(
        -1.0f / juce::jmax(
            1.0f, milliseconds * 0.001f * static_cast<float>(sampleRate)));
}

float materialRatio(int material, int mode) noexcept
{
    const auto harmonic = static_cast<float>(mode + 1);
    if (material == 0)
        return harmonic;
    if (material == 1)
        return std::pow(harmonic, 1.16f);
    if (material == 2)
        return std::pow(harmonic, 1.31f)
            * (mode % 2 == 0 ? 1.0f : 1.035f);
    return membraneRatios[static_cast<size_t>(mode)];
}

float materialBrightness(int material, int mode) noexcept
{
    const auto position = static_cast<float>(mode)
        / static_cast<float>(ResonantSculptureModule::modeCount - 1);
    if (material == 0)
        return std::exp(-position * 2.2f);
    if (material == 1)
        return std::exp(-position * 0.85f);
    if (material == 2)
        return 0.58f + 0.42f * std::sin(
            0.73f + static_cast<float>(mode) * 1.91f);
    return std::exp(-position * 1.35f);
}

float materialDecayScale(int material, int mode) noexcept
{
    const auto position = static_cast<float>(mode)
        / static_cast<float>(ResonantSculptureModule::modeCount - 1);
    if (material == 0)
        return 1.0f - position * 0.42f;
    if (material == 1)
        return 1.18f - position * 0.18f;
    if (material == 2)
        return 1.30f - position * 0.08f;
    return 0.92f - position * 0.35f;
}
} // namespace

void ResonantSculptureModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    for (int material = 0; material < materialCount; ++material)
        for (int mode = 0; mode < modeCount; ++mode)
        {
            const auto materialIndex = static_cast<size_t>(material);
            const auto modeIndex = static_cast<size_t>(mode);
            materialRatios[materialIndex][modeIndex] =
                materialRatio(material, mode);
            materialBrightnesses[materialIndex][modeIndex] =
                materialBrightness(material, mode);
            materialDecayScales[materialIndex][modeIndex] =
                materialDecayScale(material, mode);
        }
    pitchDecimation = juce::jmax(
        1, juce::roundToInt(static_cast<float>(sampleRate) / 6000.0f));
    pitchDetectorRate =
        static_cast<float>(sampleRate) / static_cast<float>(pitchDecimation);
    pitchLowPassCoefficient = std::exp(
        -twoPi * 1200.0f / static_cast<float>(sampleRate));
    pitchDcCoefficient = std::exp(
        -twoPi * 20.0f / static_cast<float>(sampleRate));
    for (auto& value : smoothed)
        value.reset(sampleRate, 0.045);
    for (auto& weight : materialWeights)
        weight.reset(sampleRate, 0.08);
    reset();
}

void ResonantSculptureModule::reset()
{
    modes = {};
    for (int mode = 0; mode < modeCount; ++mode)
        mutationPhases[static_cast<size_t>(mode)] =
            std::fmod(0.137f + static_cast<float>(mode) * 0.173f, 1.0f);
    modeEnergyState = {};
    pitchHistory = {};
    for (auto& value : smoothed)
        value.setCurrentAndTargetValue(0.0f);
    for (auto& weight : materialWeights)
        weight.setCurrentAndTargetValue(0.0f);
    fastEnvelope = 0.0f;
    slowEnvelope = 0.0f;
    chaosPhaseA = 0.173f;
    chaosPhaseB = 0.619f;
    meterEnvelope = 0.0f;
    pitchLowPass = 0.0f;
    pitchDc = 0.0f;
    trackedFrequency = 0.0f;
    targetFrequency = 0.0f;
    pitchConfidence = 0.0f;
    pitchGate = 0.0f;
    pitchDecimationCounter = 0;
    pitchAnalysisCounter = 0;
    pitchWritePosition = 0;
    pitchSamplesWritten = 0;
    initialized = false;
    telemetrySequence = 0;
    telemetry.clear();
    meter.store(0.0f, std::memory_order_relaxed);
}

void ResonantSculptureModule::pushPitchSample(float input) noexcept
{
    pitchLowPass = pitchLowPassCoefficient * pitchLowPass
        + (1.0f - pitchLowPassCoefficient) * input;
    pitchDc = pitchDcCoefficient * pitchDc
        + (1.0f - pitchDcCoefficient) * pitchLowPass;
    if (++pitchDecimationCounter < pitchDecimation)
        return;
    pitchDecimationCounter = 0;

    pitchHistory[static_cast<size_t>(pitchWritePosition)] =
        detail::finiteSample(pitchLowPass - pitchDc);
    pitchWritePosition = (pitchWritePosition + 1) % pitchHistorySize;
    pitchSamplesWritten = juce::jmin(
        pitchHistorySize, pitchSamplesWritten + 1);
    if (++pitchAnalysisCounter >= 48)
    {
        pitchAnalysisCounter = 0;
        analysePitch();
    }
}

void ResonantSculptureModule::analysePitch() noexcept
{
    const auto minimumLag = juce::jmax(
        2, static_cast<int>(pitchDetectorRate / maximumTrackedFrequency));
    const auto maximumLag = juce::jmin(
        pitchHistorySize / 3,
        static_cast<int>(pitchDetectorRate / minimumTrackedFrequency));
    const auto availableWindow = pitchSamplesWritten - maximumLag;
    const auto window = juce::jmin(160, availableWindow);
    if (window < 48)
        return;

    const auto sampleAt = [this](int age) noexcept
    {
        auto index = pitchWritePosition - 1 - age;
        if (index < 0)
            index += pitchHistorySize * (1 + (-index / pitchHistorySize));
        return pitchHistory[static_cast<size_t>(index % pitchHistorySize)];
    };

    std::array<float, pitchHistorySize / 3 + 1> correlations {};
    auto strongestCorrelation = -1.0f;
    auto strongestLag = minimumLag;
    for (int lag = minimumLag; lag <= maximumLag; ++lag)
    {
        double product = 0.0;
        double currentEnergy = 1.0e-12;
        double delayedEnergy = 1.0e-12;
        for (int sample = 0; sample < window; ++sample)
        {
            const auto current = sampleAt(sample);
            const auto delayed = sampleAt(sample + lag);
            product += static_cast<double>(current) * delayed;
            currentEnergy += static_cast<double>(current) * current;
            delayedEnergy += static_cast<double>(delayed) * delayed;
        }
        const auto correlation = detail::finiteSample(static_cast<float>(
            product / std::sqrt(currentEnergy * delayedEnergy)));
        correlations[static_cast<size_t>(lag)] = correlation;
        if (correlation > strongestCorrelation)
        {
            strongestCorrelation = correlation;
            strongestLag = lag;
        }
    }

    auto selectedLag = strongestLag;
    const auto strongPeakThreshold = juce::jmax(
        0.58f, strongestCorrelation * 0.90f);
    for (int lag = minimumLag + 1; lag < maximumLag; ++lag)
        if (correlations[static_cast<size_t>(lag)] >= strongPeakThreshold
            && correlations[static_cast<size_t>(lag)]
                >= correlations[static_cast<size_t>(lag - 1)]
            && correlations[static_cast<size_t>(lag)]
                > correlations[static_cast<size_t>(lag + 1)])
        {
            selectedLag = lag;
            break;
        }

    const auto centre = correlations[static_cast<size_t>(selectedLag)];
    const auto left = correlations[
        static_cast<size_t>(juce::jmax(minimumLag, selectedLag - 1))];
    const auto right = correlations[
        static_cast<size_t>(juce::jmin(maximumLag, selectedLag + 1))];
    const auto denominator = left - 2.0f * centre + right;
    const auto offset = std::abs(denominator) > 1.0e-6f
        ? juce::jlimit(-0.5f, 0.5f, 0.5f * (left - right) / denominator)
        : 0.0f;
    const auto candidate = pitchDetectorRate
        / (static_cast<float>(selectedLag) + offset);
    const auto confidence = juce::jlimit(
        0.0f, 1.0f, (centre - 0.45f) / 0.50f);

    if (confidence >= 0.28f && std::isfinite(candidate))
    {
        targetFrequency = juce::jlimit(
            minimumTrackedFrequency, maximumTrackedFrequency, candidate);
        if (trackedFrequency <= 0.0f)
            trackedFrequency = targetFrequency;
        pitchConfidence += 0.55f * (confidence - pitchConfidence);
    }
    else
    {
        pitchConfidence *= 0.72f;
    }
}

void ResonantSculptureModule::process(
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
        targets[static_cast<size_t>(control)] = detail::normalizedControl(
            controls[static_cast<size_t>(control)], 0.5f);
    const auto selectedMaterial = juce::jlimit(
        0, materialCount - 1,
        static_cast<int>(targets[materialControl] * materialCount));
    for (int control = 0; control < controlsPerSlot; ++control)
    {
        auto& value = smoothed[static_cast<size_t>(control)];
        if (initialized)
            value.setTargetValue(targets[static_cast<size_t>(control)]);
        else
            value.setCurrentAndTargetValue(targets[static_cast<size_t>(control)]);
    }
    for (int material = 0; material < materialCount; ++material)
    {
        const auto target = material == selectedMaterial ? 1.0f : 0.0f;
        if (initialized)
            materialWeights[static_cast<size_t>(material)].setTargetValue(target);
        else
            materialWeights[static_cast<size_t>(material)]
                .setCurrentAndTargetValue(target);
    }
    initialized = true;

    const auto fastAttack = coefficient(sampleRate, 0.35f);
    const auto fastRelease = coefficient(sampleRate, 22.0f);
    const auto slowCoefficient = coefficient(sampleRate, 75.0f);
    double wetEnergyAccumulator = 0.0;
    float maximumTransient = 0.0f;
    float latestBow = 0.0f;
    float latestChaos = 0.0f;
    float latestShape = targets[shapeControl];

    for (int sample = 0; sample < sampleCount; ++sample)
    {
        std::array<float, controlsPerSlot> current {};
        for (int control = 0; control < controlsPerSlot; ++control)
            current[static_cast<size_t>(control)] =
                smoothed[static_cast<size_t>(control)].getNextValue();
        std::array<float, materialCount> weights {};
        for (int material = 0; material < materialCount; ++material)
            weights[static_cast<size_t>(material)] =
                materialWeights[static_cast<size_t>(material)].getNextValue();

        const auto shape = current[shapeControl];
        const auto pitchOffset =
            detail::lerp(-24.0f, 24.0f, current[tuningControl]);
        const auto excitation = current[excitationControl];
        const auto decaySeconds = detail::exponential(
            0.15f, 12.0f, current[decayControl]);
        const auto mutation = current[mutationControl];
        const auto bow = current[bowControl];
        const auto strike = current[strikeControl];
        const auto stereoField = current[stereoFieldControl];
        const auto chaos = current[chaosControl];
        const auto mix = current[mixControl];
        const auto outputGain = juce::Decibels::decibelsToGain(
            detail::lerp(-18.0f, 12.0f, current[outputControl]));

        const std::array<float, 2> dry {
            detail::finiteSample(buffer.getSample(0, sample)),
            channelCount > 1
                ? detail::finiteSample(buffer.getSample(1, sample))
                : detail::finiteSample(buffer.getSample(0, sample))
        };
        const auto mid = 0.5f * (dry[0] + dry[1]);
        const auto side = 0.5f * (dry[0] - dry[1]);
        pushPitchSample(mid);
        if (targetFrequency > 0.0f && trackedFrequency > 0.0f)
        {
            const auto trackingBlend = 1.0f - std::exp(
                -1.0f / (0.035f * static_cast<float>(sampleRate)));
            trackedFrequency *= std::exp(
                trackingBlend * std::log(targetFrequency / trackedFrequency));
        }
        const auto gateTarget = pitchConfidence >= 0.20f ? 1.0f : 0.0f;
        const auto gateCoefficient = gateTarget > pitchGate
            ? 1.0f : 1.0f - std::exp(
                -1.0f / (0.30f * static_cast<float>(sampleRate)));
        pitchGate += gateCoefficient * (gateTarget - pitchGate);
        const auto tuning = juce::jmax(
            minimumTrackedFrequency,
            trackedFrequency * std::exp2(pitchOffset / 12.0f));
        const auto magnitude = juce::jmax(std::abs(dry[0]), std::abs(dry[1]));
        const auto fastCoefficient =
            magnitude > fastEnvelope ? fastAttack : fastRelease;
        fastEnvelope = fastCoefficient * fastEnvelope
            + (1.0f - fastCoefficient) * magnitude;
        slowEnvelope = slowCoefficient * slowEnvelope
            + (1.0f - slowCoefficient) * magnitude;
        const auto transient = juce::jlimit(
            0.0f, 1.0f,
            (fastEnvelope - slowEnvelope)
                / juce::jmax(0.015f, fastEnvelope));
        maximumTransient = juce::jmax(maximumTransient, transient);

        chaosPhaseA += (0.071f + mutation * 0.11f)
            / static_cast<float>(sampleRate);
        chaosPhaseB += (0.113f + chaos * 0.17f)
            / static_cast<float>(sampleRate);
        chaosPhaseA -= std::floor(chaosPhaseA);
        chaosPhaseB -= std::floor(chaosPhaseB);
        const auto chaosSignal =
            std::sin(twoPi * chaosPhaseA)
            * std::sin(twoPi * chaosPhaseB + 1.7f * std::sin(twoPi * chaosPhaseA));
        latestChaos = chaosSignal;
        latestBow = bow;
        latestShape = shape;

        const auto bowDrive = std::tanh(mid * (1.0f + bow * 14.0f))
            * bow * excitation * 0.16f;
        const auto strikeGain =
            excitation * strike * transient * 0.34f;
        const std::array<float, 2> domainInput {
            pitchGate
                * (mid * excitation * 0.014f + mid * strikeGain + bowDrive),
            pitchGate * side * excitation
                * (0.018f + strike * transient * 0.22f)
        };

        auto wetMid = 0.0f;
        auto trueSide = 0.0f;
        auto syntheticSide = 0.0f;
        for (int mode = 0; mode < modeCount; ++mode)
        {
            const auto index = static_cast<size_t>(mode);
            auto ratio = 0.0f;
            auto brightness = 0.0f;
            auto decayScale = 0.0f;
            for (int material = 0; material < materialCount; ++material)
            {
                const auto weight = weights[static_cast<size_t>(material)];
                const auto materialIndex = static_cast<size_t>(material);
                ratio += weight
                    * materialRatios[materialIndex][index];
                brightness += weight
                    * materialBrightnesses[materialIndex][index];
                decayScale += weight
                    * materialDecayScales[materialIndex][index];
            }
            const auto modePosition =
                static_cast<float>(mode) / static_cast<float>(modeCount - 1);
            const auto shapeWarp = std::exp2(
                (shape - 0.5f)
                * (modePosition - 0.35f) * 1.8f);
            const auto mutationSignal = std::sin(
                twoPi * mutationPhases[index]
                + chaosSignal * (0.3f + modePosition * 1.2f));
            const auto cents =
                mutation * mutationSignal * 28.0f
                + chaos * chaosSignal * modeSigns[index] * 52.0f;
            const auto frequency = juce::jlimit(
                25.0f, static_cast<float>(sampleRate * 0.45),
                tuning * ratio * shapeWarp * std::exp2(cents / 1200.0f));
            const auto angle = twoPi * frequency
                / static_cast<float>(sampleRate);
            const auto cosine = std::cos(angle);
            const auto sine = std::sin(angle);
            const auto modeDecay = juce::jmax(
                0.04f, decaySeconds * juce::jmax(0.20f, decayScale));
            const auto radius = std::exp(
                -1.0f / (modeDecay * static_cast<float>(sampleRate)));
            const auto modeInputGain =
                brightness * (0.55f + 0.45f * (1.0f - modePosition))
                / static_cast<float>(modeCount);

            std::array<float, 2> outputs {};
            for (int domain = 0; domain < 2; ++domain)
            {
                auto& state = modes[static_cast<size_t>(domain)][index];
                const auto previousReal = state.real;
                const auto previousImaginary = state.imaginary;
                const auto injected =
                    domainInput[static_cast<size_t>(domain)] * modeInputGain
                    * std::sqrt(juce::jmax(0.0000001f, 1.0f - radius));
                state.real = detail::finiteSample(
                    radius * (
                        previousReal * cosine - previousImaginary * sine)
                    + injected);
                state.imaginary = detail::finiteSample(
                    radius * (
                        previousReal * sine + previousImaginary * cosine));
                outputs[static_cast<size_t>(domain)] = state.real;
            }
            wetMid += outputs[0] * brightness;
            trueSide += outputs[1] * brightness;
            syntheticSide +=
                outputs[0] * modeSigns[index] * brightness * 0.28f;
            modeEnergyState[index] += 0.08f * (
                std::abs(outputs[0]) - modeEnergyState[index]);
            mutationPhases[index] +=
                (0.019f + static_cast<float>(mode) * 0.0037f)
                / static_cast<float>(sampleRate);
            mutationPhases[index] -= std::floor(mutationPhases[index]);
        }

        wetMid = std::tanh(wetMid * 8.0f);
        const auto wetSide = std::tanh(
            trueSide * 7.0f + syntheticSide * stereoField * 7.0f);
        const std::array<float, 2> wet {
            wetMid + wetSide, wetMid - wetSide
        };
        wetEnergyAccumulator += static_cast<double>(wetMid) * wetMid
            + static_cast<double>(wetSide) * wetSide;
        const auto dryGain =
            std::cos(mix * juce::MathConstants<float>::halfPi);
        const auto wetGain =
            std::sin(mix * juce::MathConstants<float>::halfPi);
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto index =
                static_cast<size_t>(juce::jmin(channel, 1));
            const auto wetSample = channelCount == 1 ? wetMid : wet[index];
            buffer.setSample(
                channel, sample,
                detail::finiteSample(
                    outputGain
                    * (dry[index] * dryGain + wetSample * wetGain)));
        }
    }

    const auto wetRms = static_cast<float>(std::sqrt(
        wetEnergyAccumulator / juce::jmax(1, sampleCount)));
    meterEnvelope += 0.14f * (wetRms - meterEnvelope);
    meter.store(
        juce::jlimit(0.0f, 1.0f, meterEnvelope),
        std::memory_order_relaxed);

    if (environment.captureTelemetry)
    {
        ContinuousTelemetrySnapshot snapshot;
        snapshot.sequence = ++telemetrySequence;
        snapshot.valueCount = telemetryValueCount;
        snapshot.values[overallEnergy] = wetRms;
        snapshot.values[transientAmount] = maximumTransient;
        snapshot.values[bowAmount] = latestBow;
        snapshot.values[chaosPosition] = latestChaos;
        snapshot.values[materialIndex] = static_cast<float>(selectedMaterial);
        snapshot.values[shapePosition] = latestShape;
        snapshot.values[trackedFrequencyHz] = trackedFrequency;
        snapshot.values[trackingConfidence] = pitchConfidence;
        for (int mode = 0; mode < 8; ++mode)
            snapshot.values[
                static_cast<size_t>(firstModeEnergy + mode)] =
                modeEnergyState[static_cast<size_t>(mode)];
        telemetry.publish(snapshot);
    }
}

double ResonantSculptureModule::tailSeconds(
    const ControlValues& controls) const
{
    return detail::exponential(
        0.15f, 12.0f,
        detail::normalizedControl(controls[decayControl], 0.5f));
}

bool ResonantSculptureModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}
} // namespace megadsp
