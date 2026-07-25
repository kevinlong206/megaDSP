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

constexpr std::array<std::array<int, FrequencyLabModule::resonatorCount>,
                     FrequencyLabModule::harmonyCount> harmonyIntervals {{
    {{ 0, 4, 7, 12, 16, 19, 24, 31 }},
    {{ 0, 3, 7, 12, 15, 19, 24, 31 }},
    {{ 0, 2, 7, 12, 14, 19, 24, 31 }},
    {{ 0, 5, 7, 12, 17, 19, 24, 31 }},
    {{ 0, 7, 12, 19, 24, 31, 36, 43 }},
    {{ 0, 4, 7, 11, 12, 16, 19, 24 }},
    {{ 0, 3, 7, 10, 12, 15, 19, 24 }},
    {{ 0, 12, 19, 24, 31, 36, 43, 48 }}
}};

constexpr std::array<int, FrequencyLabModule::registerCount> registerMidi {
    36, 48, 60, 72
};
constexpr float defaultDecay = 0.6922378f;
constexpr float defaultMotionRate = 0.3802911f;
constexpr float defaultLowCut = 0.3543676f;
constexpr float defaultHighCut = 0.6989700f;

float onePoleCoefficient(double rate, float frequency)
{
    return 1.0f - std::exp(
        -juce::MathConstants<float>::twoPi * frequency
        / static_cast<float>(rate));
}
} // namespace

float FrequencyLabModule::Resonator::process(
    float input, float excitation, float feedback, float damping) noexcept
{
    const auto output =
        excitation * input + feedback * previous + damping * older;
    older = previous;
    previous = std::isfinite(output) ? output : 0.0f;
    if (!std::isfinite(output))
        older = 0.0f;
    return previous;
}

void FrequencyLabModule::Resonator::reset() noexcept
{
    previous = 0.0f;
    older = 0.0f;
}

void FrequencyLabModule::advance(
    Oscillator& oscillator, float radians) noexcept
{
    const auto sine = std::sin(radians);
    const auto cosine = std::cos(radians);
    const auto oldCosine = oscillator.cosine;
    oscillator.cosine = oldCosine * cosine - oscillator.sine * sine;
    oscillator.sine = oscillator.sine * cosine + oldCosine * sine;
}

float FrequencyLabModule::resonatorFrequency(
    int root, int harmony, int registerIndex, int resonator) noexcept
{
    const auto midi = registerMidi[static_cast<size_t>(
                          juce::jlimit(0, registerCount - 1, registerIndex))]
        + juce::jlimit(0, rootCount - 1, root)
        + harmonyIntervals[static_cast<size_t>(
              juce::jlimit(0, harmonyCount - 1, harmony))]
                          [static_cast<size_t>(
                              juce::jlimit(0, resonatorCount - 1, resonator))];
    return 440.0f * std::pow(2.0f, (static_cast<float>(midi) - 69.0f) / 12.0f);
}

void FrequencyLabModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    for (auto& smoother : frequencySmoothed)
        smoother.reset(sampleRate, 0.06);
    for (auto* smoother : { &toneSmoothed, &depthSmoothed, &spreadSmoothed,
                            &mixSmoothed })
        smoother->reset(sampleRate, 0.04);
    for (auto* smoother : { &decaySmoothed, &rateSmoothed, &lowCutSmoothed,
                            &highCutSmoothed, &outputSmoothed })
        smoother->reset(sampleRate, 0.04);
    reset();
}

void FrequencyLabModule::reset()
{
    for (auto& channel : resonators)
        for (auto& resonator : channel)
            resonator.reset();
    lowPassState.fill(0.0f);
    highPassState.fill(0.0f);
    motion = {};
    renormalizeCounter = 0;
    initialized = false;
    telemetrySequence = 0;
    telemetry.clear();
}

void FrequencyLabModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    const auto channels = juce::jlimit(0, 2, buffer.getNumChannels());
    if (channels == 0 || buffer.getNumSamples() <= 0)
        return;

    const auto root = discreteIndex(
        detail::normalizedControl(controls[0], discreteValue(0, rootCount)),
        rootCount);
    const auto harmony = discreteIndex(
        detail::normalizedControl(controls[1], discreteValue(0, harmonyCount)),
        harmonyCount);
    const auto registerIndex = discreteIndex(
        detail::normalizedControl(controls[2], discreteValue(1, registerCount)),
        registerCount);
    const auto decay = exponential(
        0.08f, 4.0f, detail::normalizedControl(controls[3], defaultDecay));
    const auto tone = detail::normalizedControl(controls[4], 0.5f);
    const auto rate = exponential(
        0.02f, 4.0f,
        detail::normalizedControl(controls[5], defaultMotionRate));
    const auto depth =
        detail::normalizedControl(controls[6], 0.12f) * 25.0f;
    const auto spread = detail::normalizedControl(controls[7], 0.6f);
    const auto lowCut = exponential(
        20.0f, 1000.0f,
        detail::normalizedControl(controls[8], defaultLowCut));
    const auto highCut = exponential(
        2000.0f, 20000.0f,
        detail::normalizedControl(controls[9], defaultHighCut));
    const auto mix = detail::normalizedControl(controls[10], 0.35f);
    const auto output = juce::Decibels::decibelsToGain(
        lerp(-18.0f, 12.0f, detail::normalizedControl(controls[11], 0.6f)));

    std::array<float, resonatorCount> frequencies {};
    for (int voice = 0; voice < resonatorCount; ++voice)
    {
        frequencies[static_cast<size_t>(voice)] =
            resonatorFrequency(root, harmony, registerIndex, voice);
        frequencySmoothed[static_cast<size_t>(voice)].setTargetValue(
            frequencies[static_cast<size_t>(voice)]);
    }
    decaySmoothed.setTargetValue(decay);
    toneSmoothed.setTargetValue(tone);
    rateSmoothed.setTargetValue(rate);
    depthSmoothed.setTargetValue(depth);
    spreadSmoothed.setTargetValue(spread);
    lowCutSmoothed.setTargetValue(lowCut);
    highCutSmoothed.setTargetValue(highCut);
    mixSmoothed.setTargetValue(mix);
    outputSmoothed.setTargetValue(output);

    if (!initialized)
    {
        for (int voice = 0; voice < resonatorCount; ++voice)
            frequencySmoothed[static_cast<size_t>(voice)]
                .setCurrentAndTargetValue(
                    frequencies[static_cast<size_t>(voice)]);
        decaySmoothed.setCurrentAndTargetValue(decay);
        toneSmoothed.setCurrentAndTargetValue(tone);
        rateSmoothed.setCurrentAndTargetValue(rate);
        depthSmoothed.setCurrentAndTargetValue(depth);
        spreadSmoothed.setCurrentAndTargetValue(spread);
        lowCutSmoothed.setCurrentAndTargetValue(lowCut);
        highCutSmoothed.setCurrentAndTargetValue(highCut);
        mixSmoothed.setCurrentAndTargetValue(mix);
        outputSmoothed.setCurrentAndTargetValue(output);
        initialized = true;
    }

    std::array<float, resonatorCount> telemetryFrequencies {};
    float maximumWet = 0.0f;
    float telemetryMotion = 0.0f;
    float telemetryDecay = decay;
    const auto inverseRate = 1.0f / static_cast<float>(sampleRate);
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto currentRate = rateSmoothed.getNextValue();
        advance(motion, juce::MathConstants<float>::twoPi
                            * currentRate * inverseRate);
        const auto currentDepth = depthSmoothed.getNextValue();
        const auto currentMotion = motion.sine * currentDepth;
        telemetryMotion = currentMotion;
        const auto currentDecay = decaySmoothed.getNextValue();
        telemetryDecay = currentDecay;
        const auto radius = std::exp(
            std::log(0.001f)
            / juce::jmax(1.0f, currentDecay * static_cast<float>(sampleRate)));
        const auto currentTone = toneSmoothed.getNextValue();
        const auto currentSpread = spreadSmoothed.getNextValue();
        const auto lowCoefficient = onePoleCoefficient(
            sampleRate, lowCutSmoothed.getNextValue());
        const auto highCoefficient = onePoleCoefficient(
            sampleRate, juce::jmin(
                highCutSmoothed.getNextValue(),
                static_cast<float>(sampleRate * 0.45)));
        const auto currentMix = mixSmoothed.getNextValue();
        const auto currentOutput = outputSmoothed.getNextValue();

        std::array<float, resonatorCount> currentFrequencies {};
        std::array<float, resonatorCount> weights {};
        std::array<float, resonatorCount> excitationCoefficients {};
        std::array<float, resonatorCount> feedbackCoefficients {};
        float weightEnergy = 0.0f;
        const auto motionRatio = std::pow(2.0f, currentMotion / 1200.0f);
        for (int voice = 0; voice < resonatorCount; ++voice)
        {
            const auto position = static_cast<float>(voice)
                                  / static_cast<float>(resonatorCount - 1);
            const auto tiltDb = (currentTone - 0.5f) * 18.0f
                                * (position * 2.0f - 1.0f);
            const auto weight = juce::Decibels::decibelsToGain(tiltDb);
            weights[static_cast<size_t>(voice)] = weight;
            weightEnergy += weight * weight;
            const auto frequency =
                frequencySmoothed[static_cast<size_t>(voice)].getNextValue()
                * motionRatio;
            currentFrequencies[static_cast<size_t>(voice)] = juce::jlimit(
                20.0f, static_cast<float>(sampleRate * 0.45), frequency);
            const auto radians = juce::MathConstants<float>::twoPi
                * currentFrequencies[static_cast<size_t>(voice)]
                / static_cast<float>(sampleRate);
            excitationCoefficients[static_cast<size_t>(voice)] =
                2.0f * (1.0f - radius) * std::sin(radians);
            feedbackCoefficients[static_cast<size_t>(voice)] =
                2.0f * radius * std::cos(radians);
        }
        const auto normalization =
            1.0f / std::sqrt(juce::jmax(1.0f, weightEnergy));

        for (int channel = 0; channel < channels; ++channel)
        {
            const auto dry =
                detail::finiteSample(buffer.getSample(channel, sample));
            auto& lowState = lowPassState[static_cast<size_t>(channel)];
            lowState += lowCoefficient * (dry - lowState);
            const auto highPassed = dry - lowState;
            auto& highState = highPassState[static_cast<size_t>(channel)];
            highState += highCoefficient * (highPassed - highState);

            float wet = 0.0f;
            for (int voice = 0; voice < resonatorCount; ++voice)
            {
                const auto pan = static_cast<float>(voice * 2)
                                     / static_cast<float>(resonatorCount - 1)
                                 - 1.0f;
                const auto panGain = channels > 1
                    ? std::sqrt(juce::jmax(
                          0.0f, 1.0f
                              + (channel == 0 ? -pan : pan) * currentSpread))
                    : 1.0f;
                wet += resonators[static_cast<size_t>(channel)]
                                 [static_cast<size_t>(voice)]
                           .process(
                               highState,
                               excitationCoefficients[static_cast<size_t>(voice)],
                               feedbackCoefficients[static_cast<size_t>(voice)],
                               -radius * radius)
                    * weights[static_cast<size_t>(voice)] * panGain;
            }
            wet *= normalization;
            wet = std::tanh(wet * 1.25f) / 1.25f;
            maximumWet = juce::jmax(maximumWet, std::abs(wet));
            const auto result =
                (dry + (wet - dry) * currentMix) * currentOutput;
            buffer.setSample(
                channel, sample, detail::finiteSample(result));
        }
        telemetryFrequencies = currentFrequencies;

        if (++renormalizeCounter >= 256)
        {
            renormalizeCounter = 0;
            const auto magnitude = std::sqrt(
                motion.cosine * motion.cosine + motion.sine * motion.sine);
            if (std::isfinite(magnitude) && magnitude > 0.5f)
            {
                motion.cosine /= magnitude;
                motion.sine /= magnitude;
            }
            else
                motion = {};
        }
    }

    if (environment.captureTelemetry)
    {
        ContinuousTelemetrySnapshot snapshot;
        snapshot.sequence = ++telemetrySequence;
        snapshot.valueCount = telemetryValueCount;
        snapshot.values[rootNote] = static_cast<float>(root);
        snapshot.values[selectedHarmony] = static_cast<float>(harmony);
        snapshot.values[selectedRegister] = static_cast<float>(registerIndex);
        snapshot.values[motionPosition] = motion.sine;
        snapshot.values[motionCents] = telemetryMotion;
        snapshot.values[decaySeconds] = telemetryDecay;
        for (int voice = 0; voice < resonatorCount; ++voice)
            snapshot.values[static_cast<size_t>(frequency0 + voice)] =
                telemetryFrequencies[static_cast<size_t>(voice)];
        snapshot.values[wetEnvelope] = maximumWet;
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
    return exponential(
        0.08f, 4.0f,
        detail::normalizedControl(controls[3], defaultDecay));
}
} // namespace megadsp
