#include "FormantForge.h"
#include "DspHelpers.h"
#include "DspSafety.h"

#include <cmath>

namespace megadsp
{
namespace
{
struct VowelFormant
{
    float frequency;
    float bandwidth;
    float gainDb;
};

using Vowel = std::array<VowelFormant, FormantForgeModule::formantCount>;

// A, E, O, U occupy the four corners of the XY field.
constexpr std::array<Vowel, 4> humanVowels {{
    {{{ 800.0f, 90.0f, 0.0f }, { 1150.0f, 110.0f, -5.0f },
      { 2900.0f, 170.0f, -9.0f }, { 3900.0f, 240.0f, -13.0f }}},
    {{{ 500.0f, 75.0f, 0.0f }, { 1900.0f, 130.0f, -4.0f },
      { 2600.0f, 160.0f, -9.0f }, { 3500.0f, 230.0f, -13.0f }}},
    {{{ 500.0f, 80.0f, 0.0f }, { 900.0f, 100.0f, -4.0f },
      { 2450.0f, 160.0f, -10.0f }, { 3300.0f, 220.0f, -14.0f }}},
    {{{ 325.0f, 65.0f, 0.0f }, { 700.0f, 85.0f, -4.0f },
      { 2530.0f, 170.0f, -10.0f }, { 3500.0f, 240.0f, -15.0f }}}
}};

constexpr std::array<std::array<float, FormantForgeModule::formantCount>,
                     FormantForgeModule::modelCount> frequencyScale {{
    {{ 1.0f, 1.0f, 1.0f, 1.0f }},
    {{ 0.92f, 0.96f, 1.02f, 1.06f }},
    {{ 0.78f, 1.08f, 1.20f, 1.32f }},
    {{ 1.16f, 1.40f, 1.68f, 1.96f }}
}};

constexpr std::array<float, FormantForgeModule::modelCount> bandwidthScale {
    1.0f, 1.35f, 0.72f, 0.46f
};

constexpr std::array<std::array<float, FormantForgeModule::formantCount>,
                     FormantForgeModule::modelCount> gainOffsetDb {{
    {{ 0.0f, 0.0f, 0.0f, 0.0f }},
    {{ -0.5f, 0.5f, 1.0f, 1.5f }},
    {{ 0.0f, 1.0f, 2.0f, 2.5f }},
    {{ -2.0f, 1.5f, 4.0f, 5.0f }}
}};

float bilinear(float bottomLeft, float bottomRight, float topLeft,
               float topRight, float x, float y) noexcept
{
    const auto bottom = bottomLeft + (bottomRight - bottomLeft) * x;
    const auto top = topLeft + (topRight - topLeft) * x;
    return bottom + (top - bottom) * y;
}

float outputGain(float normalized) noexcept
{
    const auto gainDb = detail::lerp(-18.0f, 12.0f, normalized);
    return std::abs(gainDb) < 0.0001f
        ? 1.0f : juce::Decibels::decibelsToGain(gainDb);
}
} // namespace

float FormantForgeModule::Resonator::process(
    float input, const Coefficients& coefficients) noexcept
{
    const auto output = coefficients.b0 * input + z1;
    z1 = coefficients.b1 * input - coefficients.a1 * output + z2;
    z2 = coefficients.b2 * input - coefficients.a2 * output;
    if (std::isfinite(output) && std::isfinite(z1) && std::isfinite(z2))
        return output;
    reset();
    return 0.0f;
}

void FormantForgeModule::Resonator::reset() noexcept
{
    z1 = 0.0f;
    z2 = 0.0f;
}

void FormantForgeModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    for (auto& smoother : modelMix)
        smoother.reset(sampleRate, 0.055);
    for (auto* smoother : {
             &vowelXSmoothed, &vowelYSmoothed, &shiftSmoothed,
             &resonanceSmoothed, &breathSmoothed, &motionDepthSmoothed,
             &stereoSpreadSmoothed, &mixSmoothed })
        smoother->reset(sampleRate, 0.04);
    motionRateSmoothed.reset(sampleRate, 0.06);
    outputSmoothed.reset(sampleRate, 0.03);
    reset();
}

void FormantForgeModule::reset()
{
    for (auto& model : filters)
        for (auto& channel : model)
            for (auto& filter : channel)
                filter.reset();
    breathLowState.fill(0.0f);
    breathHighState.fill(0.0f);
    commonNoiseState = 0x6d2b79f5u;
    sideNoiseState = 0x9e3779b9u;
    motionPhase = 0.0;
    signalEnvelope = 0.0f;
    activeModel = 0;
    initialized = false;
    outputMeter.store(0.0f, std::memory_order_relaxed);
    detectorMeter.store(-100.0f, std::memory_order_relaxed);
    telemetryState = {};
    telemetry.clear();
}

FormantForgeModule::FormantParameters
FormantForgeModule::interpolateFormant(
    int model, int formant, float x, float y, float shiftRatio,
    float resonance) const noexcept
{
    const auto m = static_cast<size_t>(juce::jlimit(0, modelCount - 1, model));
    const auto f = static_cast<size_t>(
        juce::jlimit(0, formantCount - 1, formant));
    const auto frequency = std::exp(bilinear(
        std::log(humanVowels[0][f].frequency),
        std::log(humanVowels[1][f].frequency),
        std::log(humanVowels[2][f].frequency),
        std::log(humanVowels[3][f].frequency), x, y));
    const auto bandwidth = bilinear(
        humanVowels[0][f].bandwidth, humanVowels[1][f].bandwidth,
        humanVowels[2][f].bandwidth, humanVowels[3][f].bandwidth, x, y);
    const auto gainDb = bilinear(
        humanVowels[0][f].gainDb, humanVowels[1][f].gainDb,
        humanVowels[2][f].gainDb, humanVowels[3][f].gainDb, x, y);
    const auto maximumFrequency = static_cast<float>(sampleRate * 0.44);
    const auto resonanceScale = detail::lerp(1.35f, 0.32f, resonance);
    return {
        juce::jlimit(
            45.0f, maximumFrequency,
            frequency * frequencyScale[m][f] * shiftRatio),
        juce::jlimit(
            18.0f, maximumFrequency * 0.7f,
            bandwidth * bandwidthScale[m] * resonanceScale),
        juce::Decibels::decibelsToGain(gainDb + gainOffsetDb[m][f])
    };
}

FormantForgeModule::Coefficients FormantForgeModule::coefficientsFor(
    float frequency, float bandwidth) const noexcept
{
    const auto rate = static_cast<float>(sampleRate);
    const auto safeFrequency = juce::jlimit(45.0f, rate * 0.44f, frequency);
    const auto q = juce::jlimit(
        0.35f, 48.0f, safeFrequency / juce::jmax(12.0f, bandwidth));
    const auto omega =
        juce::MathConstants<float>::twoPi * safeFrequency / rate;
    const auto alpha = std::sin(omega) / (2.0f * q);
    const auto a0 = 1.0f + alpha;
    return {
        alpha / a0,
        0.0f,
        -alpha / a0,
        -2.0f * std::cos(omega) / a0,
        (1.0f - alpha) / a0
    };
}

float FormantForgeModule::nextNoise(std::uint32_t& state) noexcept
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    if (state == 0)
        state = 0x6d2b79f5u;
    return static_cast<float>(state >> 8) * (1.0f / 8388608.0f) - 1.0f;
}

std::array<float, 2> FormantForgeModule::colouredBreath(
    float amount, float spread) noexcept
{
    const std::array<float, 2> noise {
        nextNoise(commonNoiseState), nextNoise(sideNoiseState)
    };
    const auto lowCoefficient = 1.0f - std::exp(
        -juce::MathConstants<float>::twoPi * 450.0f
        / static_cast<float>(sampleRate));
    const auto highCoefficient = 1.0f - std::exp(
        -juce::MathConstants<float>::twoPi
        * juce::jmin(11000.0f, static_cast<float>(sampleRate * 0.42))
        / static_cast<float>(sampleRate));
    std::array<float, 2> coloured {};
    for (size_t source = 0; source < noise.size(); ++source)
    {
        breathLowState[source] +=
            lowCoefficient * (noise[source] - breathLowState[source]);
        const auto highPassed = noise[source] - breathLowState[source];
        breathHighState[source] +=
            highCoefficient * (highPassed - breathHighState[source]);
        coloured[source] = breathHighState[source];
    }
    const auto gate = std::sqrt(juce::jmax(0.0f, signalEnvelope));
    const auto gain = amount * gate * 0.16f;
    return {
        gain * (coloured[0] + coloured[1] * spread),
        gain * (coloured[0] - coloured[1] * spread)
    };
}

void FormantForgeModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    const auto channels = juce::jlimit(0, 2, buffer.getNumChannels());
    if (channels == 0 || buffer.getNumSamples() <= 0)
        return;

    const auto normalized = [&controls](int index, float fallback)
    {
        return detail::normalizedControl(
            controls[static_cast<size_t>(index)], fallback);
    };
    const auto model =
        discreteIndex(normalized(modelControl, 0.0f), modelCount);
    const auto vowelX = normalized(vowelXControl, 0.35f);
    const auto vowelY = normalized(vowelYControl, 0.45f);
    const auto shiftSemitones = detail::lerp(
        -24.0f, 24.0f, normalized(formantShiftControl, 0.5f));
    const auto resonance = normalized(resonanceControl, 0.45f);
    const auto breath = normalized(breathControl, 0.12f);
    const auto motionRate = detail::exponential(
        0.02f, 6.0f, normalized(motionRateControl, 0.35f));
    const auto motionDepth = normalized(motionDepthControl, 0.15f);
    const auto stereoSpread = normalized(stereoSpreadControl, 0.35f);
    const auto mix = normalized(mixControl, 1.0f);
    const auto output = outputGain(normalized(outputControl, 0.60f));

    if (!initialized)
    {
        activeModel = model;
        for (int index = 0; index < modelCount; ++index)
            modelMix[static_cast<size_t>(index)].setCurrentAndTargetValue(
                index == model ? 1.0f : 0.0f);
        vowelXSmoothed.setCurrentAndTargetValue(vowelX);
        vowelYSmoothed.setCurrentAndTargetValue(vowelY);
        shiftSmoothed.setCurrentAndTargetValue(shiftSemitones);
        resonanceSmoothed.setCurrentAndTargetValue(resonance);
        breathSmoothed.setCurrentAndTargetValue(breath);
        motionRateSmoothed.setCurrentAndTargetValue(motionRate);
        motionDepthSmoothed.setCurrentAndTargetValue(motionDepth);
        stereoSpreadSmoothed.setCurrentAndTargetValue(stereoSpread);
        mixSmoothed.setCurrentAndTargetValue(mix);
        outputSmoothed.setCurrentAndTargetValue(output);
        initialized = true;
    }
    if (model != activeModel)
    {
        activeModel = model;
        for (int index = 0; index < modelCount; ++index)
            modelMix[static_cast<size_t>(index)].setTargetValue(
                index == model ? 1.0f : 0.0f);
    }
    vowelXSmoothed.setTargetValue(vowelX);
    vowelYSmoothed.setTargetValue(vowelY);
    shiftSmoothed.setTargetValue(shiftSemitones);
    resonanceSmoothed.setTargetValue(resonance);
    breathSmoothed.setTargetValue(breath);
    motionRateSmoothed.setTargetValue(motionRate);
    motionDepthSmoothed.setTargetValue(motionDepth);
    stereoSpreadSmoothed.setTargetValue(stereoSpread);
    mixSmoothed.setTargetValue(mix);
    outputSmoothed.setTargetValue(output);

    const auto envelopeAttack =
        1.0f - std::exp(-1.0f / static_cast<float>(sampleRate * 0.008));
    const auto envelopeRelease =
        1.0f - std::exp(-1.0f / static_cast<float>(sampleRate * 0.090));
    double breathEnergy = 0.0;
    double outputEnergy = 0.0;
    std::array<float, formantCount> telemetryFormants {};
    float telemetryX = vowelX;
    float telemetryY = vowelY;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        std::array<float, 2> dry {
            detail::finiteSample(buffer.getSample(0, sample)),
            channels > 1
                ? detail::finiteSample(buffer.getSample(1, sample))
                : detail::finiteSample(buffer.getSample(0, sample))
        };
        const auto level = 0.5f * (dry[0] * dry[0] + dry[1] * dry[1]);
        signalEnvelope += (level - signalEnvelope)
            * (level > signalEnvelope ? envelopeAttack : envelopeRelease);
        if (!std::isfinite(signalEnvelope))
            signalEnvelope = 0.0f;

        const auto rate = motionRateSmoothed.getNextValue();
        motionPhase += static_cast<double>(rate) / sampleRate;
        motionPhase -= std::floor(motionPhase);
        const auto depth = motionDepthSmoothed.getNextValue();
        const auto phase = static_cast<float>(
            motionPhase * juce::MathConstants<double>::twoPi);
        const auto x = juce::jlimit(
            0.0f, 1.0f,
            vowelXSmoothed.getNextValue()
                + std::sin(phase) * depth * 0.42f);
        const auto y = juce::jlimit(
            0.0f, 1.0f,
            vowelYSmoothed.getNextValue()
                + std::sin(phase * 0.731f + 1.37f) * depth * 0.42f);
        const auto shiftRatio =
            std::exp2(shiftSmoothed.getNextValue() / 12.0f);
        const auto currentResonance = resonanceSmoothed.getNextValue();
        const auto spread = stereoSpreadSmoothed.getNextValue();
        const auto breathSamples = colouredBreath(
            breathSmoothed.getNextValue(), channels > 1 ? spread : 0.0f);
        breathEnergy += 0.5 * (
            static_cast<double>(breathSamples[0]) * breathSamples[0]
            + static_cast<double>(breathSamples[1]) * breathSamples[1]);

        std::array<float, modelCount> weights {};
        float weightSum = 0.0f;
        for (int modelIndex = 0; modelIndex < modelCount; ++modelIndex)
        {
            weights[static_cast<size_t>(modelIndex)] =
                modelMix[static_cast<size_t>(modelIndex)].getNextValue();
            weightSum += weights[static_cast<size_t>(modelIndex)];
        }
        const auto inverseWeight =
            weightSum > 0.000001f ? 1.0f / weightSum : 1.0f;

        std::array<float, 2> wet {};
        telemetryFormants.fill(0.0f);
        for (int modelIndex = 0; modelIndex < modelCount; ++modelIndex)
        {
            const auto modelWeight =
                weights[static_cast<size_t>(modelIndex)] * inverseWeight;
            if (modelWeight <= 0.000001f)
                continue;
            std::array<FormantParameters, formantCount> parameters;
            std::array<Coefficients, formantCount> coefficients;
            float gainSum = 0.0f;
            for (int formant = 0; formant < formantCount; ++formant)
            {
                parameters[static_cast<size_t>(formant)] =
                    interpolateFormant(
                        modelIndex, formant, x, y, shiftRatio,
                        currentResonance);
                gainSum += parameters[static_cast<size_t>(formant)].gain;
                coefficients[static_cast<size_t>(formant)] = coefficientsFor(
                    parameters[static_cast<size_t>(formant)].frequency,
                    parameters[static_cast<size_t>(formant)].bandwidth);
                telemetryFormants[static_cast<size_t>(formant)] +=
                    modelWeight
                    * parameters[static_cast<size_t>(formant)].frequency;
            }
            const auto bankGain = 1.6f / juce::jmax(0.5f, gainSum);
            for (int channel = 0; channel < 2; ++channel)
            {
                const auto excitation =
                    dry[static_cast<size_t>(channel)]
                    + breathSamples[static_cast<size_t>(channel)];
                float bankOutput = 0.0f;
                for (int formant = 0; formant < formantCount; ++formant)
                {
                    const auto& formantParameters =
                        parameters[static_cast<size_t>(formant)];
                    bankOutput +=
                        filters[static_cast<size_t>(modelIndex)]
                               [static_cast<size_t>(channel)]
                               [static_cast<size_t>(formant)]
                            .process(
                                excitation,
                                coefficients[static_cast<size_t>(formant)])
                        * formantParameters.gain;
                }
                wet[static_cast<size_t>(channel)] +=
                    modelWeight * bankOutput * bankGain;
            }
        }

        if (channels > 1)
        {
            const auto mid = 0.5f * (wet[0] + wet[1]);
            const auto side =
                0.5f * (wet[0] - wet[1]) * spread * 2.0f;
            wet = { mid + side, mid - side };
        }
        const auto currentMix = mixSmoothed.getNextValue();
        const auto currentOutput = outputSmoothed.getNextValue();
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto result = detail::finiteSample(
                (dry[static_cast<size_t>(channel)]
                 + (wet[static_cast<size_t>(channel)]
                    - dry[static_cast<size_t>(channel)]) * currentMix)
                * currentOutput);
            buffer.setSample(channel, sample, result);
            outputEnergy += static_cast<double>(result) * result;
        }
        telemetryX = x;
        telemetryY = y;
    }

    const auto sampleCount = static_cast<double>(
        juce::jmax(1, buffer.getNumSamples()));
    const auto currentBreathRms =
        static_cast<float>(std::sqrt(breathEnergy / sampleCount));
    const auto currentOutputRms = static_cast<float>(std::sqrt(
        outputEnergy / (sampleCount * static_cast<double>(channels))));
    outputMeter.store(currentOutputRms, std::memory_order_relaxed);
    detectorMeter.store(
        juce::Decibels::gainToDecibels(
            std::sqrt(juce::jmax(0.0f, signalEnvelope)), -100.0f),
        std::memory_order_relaxed);

    if (environment.captureTelemetry)
    {
        ++telemetryState.sequence;
        telemetryState.valueCount = telemetryValueCount;
        telemetryState.values[actualVowelX] = telemetryX;
        telemetryState.values[actualVowelY] = telemetryY;
        telemetryState.values[actualFormant1Hz] = telemetryFormants[0];
        telemetryState.values[actualFormant2Hz] = telemetryFormants[1];
        telemetryState.values[actualFormant3Hz] = telemetryFormants[2];
        telemetryState.values[actualFormant4Hz] = telemetryFormants[3];
        telemetryState.values[breathRms] = currentBreathRms;
        telemetryState.values[actualOutputRms] = currentOutputRms;
        appendContinuousTelemetryHistory(
            telemetryState,
            { telemetryX, telemetryY, currentBreathRms, currentOutputRms },
            telemetryHistoryValueCount);
        telemetry.publish(telemetryState);
    }
}

double FormantForgeModule::tailSeconds(const ControlValues& controls) const
{
    const auto resonance =
        detail::normalizedControl(controls[resonanceControl], 0.45f);
    return 0.04 + 0.42 * resonance * resonance;
}

float FormantForgeModule::meterValue() const
{
    return outputMeter.load(std::memory_order_relaxed);
}

float FormantForgeModule::detectorValue() const
{
    return detectorMeter.load(std::memory_order_relaxed);
}

bool FormantForgeModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}
} // namespace megadsp
