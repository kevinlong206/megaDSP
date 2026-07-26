#include "StageVocal.h"

#include "DspHelpers.h"
#include "DspSafety.h"

#include <cmath>

namespace megadsp
{
namespace
{
using detail::coefficient;
using detail::exponential;
using detail::lerp;

float followEnvelope(
    float current, float target,
    float attackCoefficient, float releaseCoefficient) noexcept
{
    const auto smoothing =
        target > current ? attackCoefficient : releaseCoefficient;
    return smoothing * current + (1.0f - smoothing) * target;
}
} // namespace

float StageVocalModule::onePoleCoefficient(
    double rate, float frequency) noexcept
{
    return 1.0f - std::exp(
        -juce::MathConstants<float>::twoPi
        * juce::jlimit(
            5.0f, static_cast<float>(rate * 0.45), frequency)
        / static_cast<float>(rate));
}

float StageVocalModule::smoothStep(float value) noexcept
{
    value = juce::jlimit(0.0f, 1.0f, value);
    return value * value * (3.0f - 2.0f * value);
}

void StageVocalModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    for (auto& value : smoothed)
        value.reset(sampleRate, 0.03);
    reset();
}

void StageVocalModule::reset()
{
    channelStates = {};
    expanderEnvelope = 0.0f;
    deEssEnvelope = 0.0f;
    compressorEnvelope = 0.0f;
    expanderGainDb = 0.0f;
    deEssGainDb = 0.0f;
    compressorGainDb = 0.0f;
    for (auto& value : smoothed)
        value.setCurrentAndTargetValue(0.0f);
    initialized = false;
    telemetrySequence = 0;
    totalReductionDb.store(0.0f, std::memory_order_relaxed);
    detectorDb.store(-100.0f, std::memory_order_relaxed);
    telemetry.clear();
}

bool StageVocalModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}

void StageVocalModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    const auto channels = juce::jlimit(0, 2, buffer.getNumChannels());
    const auto samples = buffer.getNumSamples();
    if (channels <= 0 || samples <= 0)
        return;

    std::array<float, controlsPerSlot> targets {};
    for (int control = 0; control < controlsPerSlot; ++control)
        targets[static_cast<size_t>(control)] =
            detail::normalizedControl(
                controls[static_cast<size_t>(control)], 0.0f);
    if (!initialized)
    {
        for (int control = 0; control < controlsPerSlot; ++control)
            smoothed[static_cast<size_t>(control)]
                .setCurrentAndTargetValue(
                    targets[static_cast<size_t>(control)]);
        initialized = true;
    }
    else
    {
        for (int control = 0; control < controlsPerSlot; ++control)
            smoothed[static_cast<size_t>(control)]
                .setTargetValue(targets[static_cast<size_t>(control)]);
    }

    const auto expanderAttack = coefficient(sampleRate, 3.0f);
    const auto expanderRelease = coefficient(sampleRate, 140.0f);
    const auto deEssAttack = coefficient(sampleRate, 1.2f);
    const auto deEssRelease = coefficient(sampleRate, 75.0f);
    const auto expanderGainAttack = coefficient(sampleRate, 5.0f);
    const auto expanderGainRelease = coefficient(sampleRate, 120.0f);
    const auto deEssGainAttack = coefficient(sampleRate, 2.0f);
    const auto deEssGainRelease = coefficient(sampleRate, 90.0f);

    auto maximumInput = 0.0f;
    auto maximumOutput = 0.0f;
    auto maximumExpanderReduction = 0.0f;
    auto maximumDeEssReduction = 0.0f;
    auto maximumCompressorReduction = 0.0f;

    for (int sample = 0; sample < samples; ++sample)
    {
        std::array<float, controlsPerSlot> current {};
        for (int control = 0; control < controlsPerSlot; ++control)
            current[static_cast<size_t>(control)] =
                smoothed[static_cast<size_t>(control)].getNextValue();

        const auto lowCut = exponential(40.0f, 220.0f, current[lowCutControl]);
        const auto expanderThreshold =
            lerp(-70.0f, -20.0f, current[expanderThresholdControl]);
        const auto expanderRange =
            lerp(0.0f, 36.0f, current[expanderRangeControl]);
        const auto deEssFrequency = exponential(
            3000.0f, 10000.0f, current[deEssFrequencyControl]);
        const auto deEssAmount =
            lerp(0.0f, 12.0f, current[deEssAmountControl]);
        const auto compressorThreshold =
            lerp(-40.0f, 0.0f, current[compressorThresholdControl]);
        const auto ratio =
            exponential(1.0f, 10.0f, current[compressorRatioControl]);
        const auto attackMs =
            exponential(0.5f, 50.0f, current[attackControl]);
        const auto releaseMs =
            exponential(30.0f, 800.0f, current[releaseControl]);
        const auto presenceGain = juce::Decibels::decibelsToGain(
            lerp(-6.0f, 6.0f, current[presenceControl]));
        const auto airGain = juce::Decibels::decibelsToGain(
            lerp(-6.0f, 6.0f, current[airControl]));
        const auto outputGain = juce::Decibels::decibelsToGain(
            lerp(-18.0f, 12.0f, current[outputControl]));

        const auto lowCutCoefficient =
            onePoleCoefficient(sampleRate, lowCut);
        const auto presenceLowCoefficient =
            onePoleCoefficient(sampleRate, 1200.0f);
        const auto presenceHighCoefficient =
            onePoleCoefficient(sampleRate, 5000.0f);
        const auto airCoefficient =
            onePoleCoefficient(sampleRate, 8000.0f);
        const auto deEssCoefficient =
            onePoleCoefficient(sampleRate, deEssFrequency);

        std::array<float, 2> filteredSamples {};
        std::array<float, 2> deEssLow {};
        std::array<float, 2> deEssHigh {};
        auto linkedInput = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            auto& state = channelStates[index];
            const auto input =
                detail::finiteSample(buffer.getSample(channel, sample));
            maximumInput = juce::jmax(maximumInput, std::abs(input));

            state.lowCutFirst +=
                lowCutCoefficient * (input - state.lowCutFirst);
            const auto firstHighPass = input - state.lowCutFirst;
            state.lowCutSecond += lowCutCoefficient
                                  * (firstHighPass - state.lowCutSecond);
            const auto filtered = firstHighPass - state.lowCutSecond;
            linkedInput = juce::jmax(linkedInput, std::abs(filtered));

            filteredSamples[index] = filtered;
        }

        expanderEnvelope = followEnvelope(
            expanderEnvelope, linkedInput,
            expanderAttack, expanderRelease);
        const auto expanderLevelDb = juce::Decibels::gainToDecibels(
            expanderEnvelope, -100.0f);
        const auto expanderDepth = smoothStep(
            (expanderThreshold - expanderLevelDb) / 18.0f);
        const auto targetExpanderGainDb =
            -expanderRange * expanderDepth;
        expanderGainDb = followEnvelope(
            expanderGainDb, targetExpanderGainDb,
            expanderGainAttack, expanderGainRelease);
        const auto expanderGain =
            juce::Decibels::decibelsToGain(expanderGainDb);
        maximumExpanderReduction = juce::jmax(
            maximumExpanderReduction, -expanderGainDb);

        auto linkedDeEss = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            filteredSamples[index] *= expanderGain;
            auto& state = channelStates[index];
            state.deEssLow += deEssCoefficient
                              * (filteredSamples[index] - state.deEssLow);
            deEssLow[index] = state.deEssLow;
            deEssHigh[index] =
                filteredSamples[index] - state.deEssLow;
            linkedDeEss =
                juce::jmax(linkedDeEss, std::abs(deEssHigh[index]));
        }

        deEssEnvelope = followEnvelope(
            deEssEnvelope, linkedDeEss, deEssAttack, deEssRelease);
        const auto fullDb = juce::Decibels::gainToDecibels(
            juce::jmax(expanderEnvelope, 1.0e-5f), -100.0f);
        const auto highDb = juce::Decibels::gainToDecibels(
            deEssEnvelope, -100.0f);
        const auto spectralActivity =
            smoothStep((highDb - fullDb + 18.0f) / 12.0f);
        const auto levelActivity =
            smoothStep((highDb + 48.0f) / 24.0f);
        const auto targetDeEssGainDb =
            -deEssAmount * spectralActivity * levelActivity;
        deEssGainDb = followEnvelope(
            deEssGainDb, targetDeEssGainDb,
            deEssGainRelease, deEssGainAttack);
        const auto deEssGain =
            juce::Decibels::decibelsToGain(deEssGainDb);
        maximumDeEssReduction =
            juce::jmax(maximumDeEssReduction, -deEssGainDb);

        std::array<float, 2> deEssed {};
        auto linkedCompressor = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            deEssed[index] =
                deEssLow[index] + deEssHigh[index] * deEssGain;
            linkedCompressor = juce::jmax(
                linkedCompressor, std::abs(deEssed[index]));
        }

        const auto compressorAttack =
            coefficient(sampleRate, attackMs);
        const auto compressorRelease =
            coefficient(sampleRate, releaseMs);
        compressorEnvelope = followEnvelope(
            compressorEnvelope, linkedCompressor,
            compressorAttack, compressorRelease);
        const auto compressorLevelDb = juce::Decibels::gainToDecibels(
            compressorEnvelope, -100.0f);
        const auto over =
            juce::jmax(0.0f, compressorLevelDb - compressorThreshold);
        const auto targetCompressorGainDb =
            -(1.0f - 1.0f / ratio) * over;
        compressorGainDb = followEnvelope(
            compressorGainDb, targetCompressorGainDb,
            compressorRelease, compressorAttack);
        const auto compressorGain =
            juce::Decibels::decibelsToGain(compressorGainDb);
        maximumCompressorReduction = juce::jmax(
            maximumCompressorReduction, -compressorGainDb);

        for (int channel = 0; channel < channels; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            auto& state = channelStates[index];
            const auto compressed = deEssed[index] * compressorGain;
            state.presenceLow += presenceLowCoefficient
                                 * (compressed - state.presenceLow);
            state.presenceHigh += presenceHighCoefficient
                                  * (compressed - state.presenceHigh);
            const auto presenceBand =
                state.presenceHigh - state.presenceLow;
            state.airLow +=
                airCoefficient * (compressed - state.airLow);
            const auto airBand = compressed - state.airLow;
            const auto output = detail::finiteSample(
                (compressed
                 + presenceBand * (presenceGain - 1.0f)
                 + airBand * (airGain - 1.0f))
                * outputGain);
            maximumOutput = juce::jmax(maximumOutput, std::abs(output));
            buffer.setSample(channel, sample, output);
        }
    }

    detectorDb.store(
        juce::Decibels::gainToDecibels(maximumInput, -100.0f),
        std::memory_order_relaxed);
    totalReductionDb.store(
        maximumExpanderReduction + maximumDeEssReduction
            + maximumCompressorReduction,
        std::memory_order_relaxed);

    if (environment.captureTelemetry)
    {
        ContinuousTelemetrySnapshot snapshot;
        snapshot.sequence = ++telemetrySequence;
        snapshot.valueCount = telemetryValueCount;
        snapshot.values[inputLevelDb] =
            juce::Decibels::gainToDecibels(maximumInput, -100.0f);
        snapshot.values[expanderReductionDb] =
            maximumExpanderReduction;
        snapshot.values[deEssReductionDb] =
            maximumDeEssReduction;
        snapshot.values[compressorReductionDb] =
            maximumCompressorReduction;
        snapshot.values[outputLevelDb] =
            juce::Decibels::gainToDecibels(maximumOutput, -100.0f);
        snapshot.historyValueCount = telemetryHistoryCount;
        snapshot.historyCount = 1;
        snapshot.historyWritePosition = 1;
        snapshot.history[inputHistory][0] =
            snapshot.values[inputLevelDb];
        snapshot.history[expanderHistory][0] =
            maximumExpanderReduction;
        snapshot.history[deEssHistory][0] =
            maximumDeEssReduction;
        snapshot.history[compressorHistory][0] =
            maximumCompressorReduction;
        telemetry.publish(snapshot);
    }
}
} // namespace megadsp
