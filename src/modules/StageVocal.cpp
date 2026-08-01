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

float followGainDb(
    float current, float target,
    float attackCoefficient, float releaseCoefficient) noexcept
{
    const auto smoothing =
        target < current ? attackCoefficient : releaseCoefficient;
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

float StageVocalModule::compressionReduction(
    float levelDb, float thresholdDb, float ratio,
    float kneeDb) noexcept
{
    const auto over = levelDb - thresholdDb;
    const auto slope =
        1.0f / juce::jmax(1.0001f, ratio) - 1.0f;
    if (over <= -kneeDb * 0.5f)
        return 0.0f;
    if (over >= kneeDb * 0.5f)
        return slope * over;
    const auto kneeInput = over + kneeDb * 0.5f;
    return slope * kneeInput * kneeInput / (2.0f * kneeDb);
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
    fullEnvelope = 0.0f;
    deEssEnvelope = 0.0f;
    deEssGainDb = 0.0f;
    fastGainDb = 0.0f;
    slowGainDb = 0.0f;
    opticalMemory = 0.0f;
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

    const auto fullAttack = coefficient(sampleRate, 1.5f);
    const auto fullRelease = coefficient(sampleRate, 90.0f);
    const auto deEssAttack = coefficient(sampleRate, 0.8f);
    const auto deEssRelease = coefficient(sampleRate, 65.0f);
    const auto deEssGainAttack = coefficient(sampleRate, 1.2f);
    const auto deEssGainRelease = coefficient(sampleRate, 80.0f);
    const auto memoryRise = coefficient(sampleRate, 45.0f);

    auto maximumInput = 0.0f;
    auto maximumOutput = 0.0f;
    auto maximumFastReduction = 0.0f;
    auto maximumDeEssReduction = 0.0f;
    auto maximumSlowReduction = 0.0f;

    for (int sample = 0; sample < samples; ++sample)
    {
        std::array<float, controlsPerSlot> current {};
        for (int control = 0; control < controlsPerSlot; ++control)
            current[static_cast<size_t>(control)] =
                smoothed[static_cast<size_t>(control)].getNextValue();

        const auto lowCut = exponential(
            40.0f, 220.0f, current[lowCutControl]);
        const auto fastAmount = current[fastAmountControl];
        const auto fastAttackMs = exponential(
            0.02f, 0.8f, current[fastAttackControl]);
        const auto fastReleaseMs = exponential(
            40.0f, 800.0f, current[fastReleaseControl]);
        const auto slowAmount = current[slowAmountControl];
        const auto slowAttackMs = exponential(
            2.0f, 40.0f, current[slowAttackControl]);
        const auto slowReleaseMs = exponential(
            200.0f, 3000.0f, current[slowReleaseControl]);
        const auto deEssFrequency = exponential(
            3000.0f, 10000.0f, current[deEssFrequencyControl]);
        const auto deEssAmount =
            lerp(0.0f, 12.0f, current[deEssAmountControl]);
        const auto presenceGain = juce::Decibels::decibelsToGain(
            lerp(-6.0f, 6.0f, current[presenceControl]));
        const auto airGain = juce::Decibels::decibelsToGain(
            lerp(-6.0f, 6.0f, current[airControl]));
        const auto outputGain = juce::Decibels::decibelsToGain(
            lerp(-18.0f, 12.0f, current[outputControl]));

        const auto lowCutCoefficient =
            onePoleCoefficient(sampleRate, lowCut);
        const auto deEssCoefficient =
            onePoleCoefficient(sampleRate, deEssFrequency);
        const auto presenceLowCoefficient =
            onePoleCoefficient(sampleRate, 1200.0f);
        const auto presenceHighCoefficient =
            onePoleCoefficient(sampleRate, 5000.0f);
        const auto airCoefficient =
            onePoleCoefficient(sampleRate, 8000.0f);

        std::array<float, 2> filtered {};
        std::array<float, 2> deEssLow {};
        std::array<float, 2> deEssHigh {};
        auto linkedFull = 0.0f;
        auto linkedDeEss = 0.0f;
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
            filtered[index] = firstHighPass - state.lowCutSecond;
            state.deEssLow += deEssCoefficient
                              * (filtered[index] - state.deEssLow);
            deEssLow[index] = state.deEssLow;
            deEssHigh[index] = filtered[index] - state.deEssLow;
            linkedFull = juce::jmax(linkedFull, std::abs(filtered[index]));
            linkedDeEss =
                juce::jmax(linkedDeEss, std::abs(deEssHigh[index]));
        }

        fullEnvelope = followEnvelope(
            fullEnvelope, linkedFull, fullAttack, fullRelease);
        deEssEnvelope = followEnvelope(
            deEssEnvelope, linkedDeEss, deEssAttack, deEssRelease);
        const auto fullDb = juce::Decibels::gainToDecibels(
            fullEnvelope, -100.0f);
        const auto highDb = juce::Decibels::gainToDecibels(
            deEssEnvelope, -100.0f);
        const auto spectralActivity =
            smoothStep((highDb - fullDb + 18.0f) / 12.0f);
        const auto levelActivity =
            smoothStep((highDb + 48.0f) / 24.0f);
        const auto targetDeEssGainDb =
            -deEssAmount * spectralActivity * levelActivity;
        deEssGainDb = followGainDb(
            deEssGainDb, targetDeEssGainDb,
            deEssGainAttack, deEssGainRelease);
        const auto deEssGain =
            juce::Decibels::decibelsToGain(deEssGainDb);
        maximumDeEssReduction =
            juce::jmax(maximumDeEssReduction, -deEssGainDb);

        std::array<float, 2> deEssed {};
        auto linkedFastFeedback = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            deEssed[index] =
                deEssLow[index] + deEssHigh[index] * deEssGain;
            linkedFastFeedback = juce::jmax(
                linkedFastFeedback,
                std::abs(channelStates[index].previousFastOutput));
        }

        const auto fastDetectorDb = juce::Decibels::gainToDecibels(
            linkedFastFeedback, -100.0f)
            - fastGainDb + fastAmount * 30.0f;
        const auto targetFastGainDb = juce::jlimit(
            -36.0f, 0.0f,
            compressionReduction(
                fastDetectorDb, -18.0f, 8.0f, 3.0f)
                * smoothStep(fastAmount));
        fastGainDb = followGainDb(
            fastGainDb, targetFastGainDb,
            coefficient(sampleRate, fastAttackMs),
            coefficient(sampleRate, fastReleaseMs));
        if (!std::isfinite(fastGainDb))
            fastGainDb = 0.0f;
        const auto fastGain =
            juce::Decibels::decibelsToGain(fastGainDb);
        maximumFastReduction =
            juce::jmax(maximumFastReduction, -fastGainDb);

        std::array<float, 2> fastOutput {};
        auto linkedSlowFeedback = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            const auto compressed = deEssed[index] * fastGain;
            const auto fetDrive = 1.0f + fastAmount * 0.7f;
            const auto colored = lerp(
                compressed,
                std::tanh(compressed * fetDrive) / std::tanh(fetDrive),
                fastAmount * 0.18f);
            fastOutput[index] = detail::finiteSample(colored);
            channelStates[index].previousFastOutput = fastOutput[index];
            linkedSlowFeedback = juce::jmax(
                linkedSlowFeedback,
                std::abs(channelStates[index].previousSlowOutput));
        }

        const auto slowDetectorDb = juce::Decibels::gainToDecibels(
            linkedSlowFeedback, -100.0f)
            - slowGainDb + slowAmount * 30.0f;
        const auto targetSlowGainDb = juce::jlimit(
            -30.0f, 0.0f,
            compressionReduction(
                slowDetectorDb, -18.0f, 3.5f, 10.0f)
                * smoothStep(slowAmount));
        const auto memoryTarget =
            smoothStep(-targetSlowGainDb / 18.0f);
        const auto memoryDecay = coefficient(
            sampleRate, slowReleaseMs * 3.5f);
        const auto memoryCoefficient =
            memoryTarget > opticalMemory ? memoryRise : memoryDecay;
        opticalMemory = juce::jlimit(
            0.0f, 1.0f,
            memoryCoefficient * opticalMemory
                + (1.0f - memoryCoefficient) * memoryTarget);
        const auto opticalReleaseMs =
            slowReleaseMs * lerp(1.0f, 3.5f, opticalMemory);
        slowGainDb = followGainDb(
            slowGainDb, targetSlowGainDb,
            coefficient(sampleRate, slowAttackMs),
            coefficient(sampleRate, opticalReleaseMs));
        if (!std::isfinite(slowGainDb))
            slowGainDb = 0.0f;
        const auto slowGain =
            juce::Decibels::decibelsToGain(slowGainDb);
        maximumSlowReduction =
            juce::jmax(maximumSlowReduction, -slowGainDb);

        for (int channel = 0; channel < channels; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            auto& state = channelStates[index];
            const auto leveled = fastOutput[index] * slowGain;
            state.previousSlowOutput = detail::finiteSample(leveled);
            state.presenceLow += presenceLowCoefficient
                                 * (leveled - state.presenceLow);
            state.presenceHigh += presenceHighCoefficient
                                  * (leveled - state.presenceHigh);
            const auto presenceBand =
                state.presenceHigh - state.presenceLow;
            state.airLow +=
                airCoefficient * (leveled - state.airLow);
            const auto airBand = leveled - state.airLow;
            const auto output = detail::finiteSample(
                (leveled
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
        maximumFastReduction + maximumDeEssReduction
            + maximumSlowReduction,
        std::memory_order_relaxed);

    if (environment.captureTelemetry)
    {
        ContinuousTelemetrySnapshot snapshot;
        snapshot.sequence = ++telemetrySequence;
        snapshot.valueCount = telemetryValueCount;
        snapshot.values[inputLevelDb] =
            juce::Decibels::gainToDecibels(maximumInput, -100.0f);
        snapshot.values[fastReductionDb] =
            maximumFastReduction;
        snapshot.values[deEssReductionDb] =
            maximumDeEssReduction;
        snapshot.values[slowReductionDb] =
            maximumSlowReduction;
        snapshot.values[outputLevelDb] =
            juce::Decibels::gainToDecibels(maximumOutput, -100.0f);
        snapshot.historyValueCount = telemetryHistoryCount;
        snapshot.historyCount = 1;
        snapshot.historyWritePosition = 1;
        snapshot.history[inputHistory][0] =
            snapshot.values[inputLevelDb];
        snapshot.history[fastHistory][0] =
            maximumFastReduction;
        snapshot.history[deEssHistory][0] =
            maximumDeEssReduction;
        snapshot.history[slowHistory][0] =
            maximumSlowReduction;
        telemetry.publish(snapshot);
    }
}
} // namespace megadsp
