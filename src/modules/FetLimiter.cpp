#include "FetLimiter.h"
#include "DspHelpers.h"
#include "DspSafety.h"

#include <cmath>

namespace megadsp
{
using detail::coefficient;
using detail::exponential;
using detail::lerp;

namespace
{
// Fixed 4x oversampling for the transformer coloration stage (two half-band
// polyphase IIR stages), matching the convention used by Analog Tape and
// Guitar Cabinet's oversampled nonlinear paths.
constexpr int oversamplingStages = 2;
constexpr int oversamplingFactor = 4;

// Fixed, bounded FET/output-transformer asymmetry and saturation character.
constexpr float fetAsymmetryAmount = 0.22f;
constexpr float fetHardness = 1.35f;
} // namespace

float FetLimiterModule::DetectorHighPass::process(
    float input, float cutoffHz, double rate) noexcept
{
    if (!std::isfinite(input))
        input = 0.0f;
    const auto pole = std::exp(
        -juce::MathConstants<float>::twoPi * cutoffHz
        / static_cast<float>(rate));
    state = (1.0f - pole) * input + pole * state;
    if (!std::isfinite(state))
    {
        reset();
        return 0.0f;
    }
    return input - state;
}

float FetLimiterModule::kneeReductionDb(
    float overDb, float ratio, float kneeDb) noexcept
{
    const auto safeRatio = juce::jmax(1.0001f, ratio);
    const auto slope = 1.0f / safeRatio - 1.0f;
    if (kneeDb > 0.01f && overDb > -kneeDb * 0.5f && overDb < kneeDb * 0.5f)
    {
        const auto kneeInput = overDb + kneeDb * 0.5f;
        return slope * kneeInput * kneeInput / (2.0f * kneeDb);
    }
    if (overDb >= kneeDb * 0.5f)
        return slope * overDb;
    return 0.0f;
}

void FetLimiterModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    const auto channels = static_cast<size_t>(
        juce::jlimit(1, 2, static_cast<int>(spec.numChannels)));
    const auto maximumBlock = juce::jmax(
        1, static_cast<int>(spec.maximumBlockSize));

    oversampling = std::make_unique<juce::dsp::Oversampling<float>>(
        channels, oversamplingStages,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true, true);
    oversampling->initProcessing(static_cast<size_t>(maximumBlock));
    fixedLatencySamples = juce::jmax(
        1, juce::roundToInt(oversampling->getLatencyInSamples()));

    dryBuffer.setSize(
        static_cast<int>(channels), maximumBlock, false, true, true);
    for (auto& delay : dryDelay)
        delay.assign(static_cast<size_t>(fixedLatencySamples + 1), 0.0f);

    const auto oversampledRate =
        sampleRate * static_cast<double>(oversamplingFactor);
    dcRetention = std::exp(
        -juce::MathConstants<float>::twoPi * 8.0f
        / static_cast<float>(oversampledRate));

    const auto rampSeconds = 0.02;
    for (auto& value : smoothed)
        value.reset(sampleRate, rampSeconds);
    ratioSmoothed.reset(sampleRate, rampSeconds);
    kneeSmoothed.reset(sampleRate, rampSeconds);
    allAmountSmoothed.reset(sampleRate, rampSeconds);
    reset();
}

void FetLimiterModule::reset()
{
    if (oversampling != nullptr)
        oversampling->reset();
    dryBuffer.clear();
    for (auto& delay : dryDelay)
        std::fill(delay.begin(), delay.end(), 0.0f);
    for (auto& filter : detectorFilters)
        filter.reset();
    previousPreTransformer.fill(0.0f);
    gainNormal.fill(1.0f);
    gainFast.fill(1.0f);
    gainSlow.fill(1.0f);
    dcInput.fill(0.0f);
    dcOutput.fill(0.0f);
    for (auto& value : smoothed)
        value.setCurrentAndTargetValue(0.0f);
    ratioSmoothed.setCurrentAndTargetValue(FetLimiterModule::ratioTable[0]);
    kneeSmoothed.setCurrentAndTargetValue(FetLimiterModule::softKneeDb);
    allAmountSmoothed.setCurrentAndTargetValue(0.0f);
    dryPosition = 0;
    initialized = false;
    gainReductionMeterDb.store(0.0f, std::memory_order_relaxed);
    detectorMeterDb.store(-100.0f, std::memory_order_relaxed);
    telemetryState = {};
    telemetry.clear();
}

bool FetLimiterModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}

void FetLimiterModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    const auto channels = juce::jlimit(0, 2, buffer.getNumChannels());
    const auto numSamples = buffer.getNumSamples();
    if (oversampling == nullptr || channels == 0 || numSamples <= 0
        || numSamples > dryBuffer.getNumSamples())
        return;

    const auto normalized = [&controls](int index, float fallback)
    {
        return detail::normalizedControl(
            controls[static_cast<size_t>(index)], fallback);
    };

    std::array<float, controlsPerSlot> targets {};
    targets[static_cast<size_t>(inputControl)] = normalized(inputControl, 0.5f);
    targets[static_cast<size_t>(outputControl)] = normalized(outputControl, 0.5f);
    targets[static_cast<size_t>(attackControl)] = normalized(attackControl, 0.24f);
    targets[static_cast<size_t>(releaseControl)] = normalized(releaseControl, 0.34f);
    targets[static_cast<size_t>(detectorLowCutControl)] =
        normalized(detectorLowCutControl, 0.35f);
    targets[static_cast<size_t>(stereoLinkControl)] =
        normalized(stereoLinkControl, 1.0f);
    targets[static_cast<size_t>(mixControl)] = normalized(mixControl, 1.0f);
    targets[static_cast<size_t>(outputTrimControl)] =
        normalized(outputTrimControl, 0.6f);

    const auto ratioIndex = discreteIndex(
        normalized(ratioControl, 0.0f), ratioCount);
    const auto ratioTarget =
        ratioTable[static_cast<size_t>(ratioIndex)];
    const auto isAllButtonsIn = ratioIndex == allButtonsInIndex;
    const auto kneeTarget =
        isAllButtonsIn ? 0.0f : FetLimiterModule::softKneeDb;
    const auto allTarget = isAllButtonsIn ? 1.0f : 0.0f;

    if (!initialized)
    {
        for (int index = 0; index < controlsPerSlot; ++index)
            smoothed[static_cast<size_t>(index)].setCurrentAndTargetValue(
                targets[static_cast<size_t>(index)]);
        ratioSmoothed.setCurrentAndTargetValue(ratioTarget);
        kneeSmoothed.setCurrentAndTargetValue(kneeTarget);
        allAmountSmoothed.setCurrentAndTargetValue(allTarget);
        initialized = true;
    }
    else
    {
        for (int index = 0; index < controlsPerSlot; ++index)
            smoothed[static_cast<size_t>(index)].setTargetValue(
                targets[static_cast<size_t>(index)]);
        ratioSmoothed.setTargetValue(ratioTarget);
        kneeSmoothed.setTargetValue(kneeTarget);
        allAmountSmoothed.setTargetValue(allTarget);
    }

    for (int channel = 0; channel < channels; ++channel)
        dryBuffer.copyFrom(channel, 0, buffer, channel, 0, numSamples);

    float maximumInput = 0.0f;
    float maximumDetector = 0.0f;
    float maximumReduction = 0.0f;

    // Base-rate feedback gain-reduction loop. The detector reads the
    // previous sample's pre-transformer output (the feedback tap), not the
    // raw input, which is the defining characteristic of feedback-topology
    // limiters. The result is written back into `buffer` in place so the
    // second, oversampled pass can apply the transformer coloration.
    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto inputDb = lerp(
            -24.0f, 24.0f, smoothed[static_cast<size_t>(inputControl)]
                               .getNextValue());
        const auto inputGain = juce::Decibels::decibelsToGain(inputDb);
        const auto outputDb = lerp(
            -24.0f, 24.0f, smoothed[static_cast<size_t>(outputControl)]
                               .getNextValue());
        const auto outputGain = juce::Decibels::decibelsToGain(outputDb);
        const auto attackMsBase = exponential(
            0.02f, 0.8f, smoothed[static_cast<size_t>(attackControl)]
                             .getNextValue());
        const auto releaseMsBase = exponential(
            50.0f, 1100.0f, smoothed[static_cast<size_t>(releaseControl)]
                                .getNextValue());
        const auto lowCutHz = exponential(
            20.0f, 300.0f,
            smoothed[static_cast<size_t>(detectorLowCutControl)]
                .getNextValue());
        const auto stereoLink =
            smoothed[static_cast<size_t>(stereoLinkControl)].getNextValue();
        const auto ratio = ratioSmoothed.getNextValue();
        const auto knee = kneeSmoothed.getNextValue();
        const auto allAmount = allAmountSmoothed.getNextValue();

        // The classic "all buttons in" mode forces a near-instant attack
        // regardless of the Attack control, and blends toward a two-stage
        // (fast then slow) release recovery instead of a single time
        // constant, distinguishing its curve and timing from the stepped
        // 4/8/12/20 ratios.
        const auto attackMs = lerp(attackMsBase, 0.02f, allAmount);
        const auto attackCoefficient = coefficient(sampleRate, attackMs);
        const auto releaseCoefficient = coefficient(sampleRate, releaseMsBase);
        const auto releaseFastCoefficient = coefficient(
            sampleRate, releaseMsBase * 0.35f);
        const auto releaseSlowCoefficient = coefficient(
            sampleRate, releaseMsBase * 2.4f);

        std::array<float, 2> level {};
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            const auto filtered = detectorFilters[index].process(
                previousPreTransformer[index], lowCutHz, sampleRate);
            level[index] = std::abs(filtered);
        }
        auto linkedLevel = level[0];
        if (channels > 1)
            linkedLevel = juce::jmax(level[0], level[1]);
        maximumDetector = juce::jmax(maximumDetector, linkedLevel);

        for (int channel = 0; channel < channels; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            const auto effectiveLevel =
                lerp(level[index], linkedLevel, stereoLink);
            const auto levelDb = juce::Decibels::gainToDecibels(
                effectiveLevel, -100.0f);
            const auto over = levelDb - FetLimiterModule::internalThresholdDb;
            const auto reductionDb = kneeReductionDb(over, ratio, knee);
            const auto targetGain = juce::Decibels::decibelsToGain(reductionDb);

            auto& normalGain = gainNormal[index];
            auto& fastGain = gainFast[index];
            auto& slowGain = gainSlow[index];
            const auto normalCoefficient = targetGain < normalGain
                                                ? attackCoefficient
                                                : releaseCoefficient;
            normalGain = normalCoefficient * normalGain
                         + (1.0f - normalCoefficient) * targetGain;
            const auto fastCoefficient = targetGain < fastGain
                                             ? attackCoefficient
                                             : releaseFastCoefficient;
            fastGain = fastCoefficient * fastGain
                       + (1.0f - fastCoefficient) * targetGain;
            const auto slowCoefficient = targetGain < slowGain
                                             ? attackCoefficient
                                             : releaseSlowCoefficient;
            slowGain = slowCoefficient * slowGain
                       + (1.0f - slowCoefficient) * targetGain;
            if (!std::isfinite(normalGain))
                normalGain = 1.0f;
            if (!std::isfinite(fastGain))
                fastGain = 1.0f;
            if (!std::isfinite(slowGain))
                slowGain = 1.0f;

            const auto dualGain = juce::jmin(fastGain, slowGain);
            const auto gain = lerp(normalGain, dualGain, allAmount);
            maximumReduction = juce::jmax(
                maximumReduction,
                -juce::Decibels::gainToDecibels(gain, -100.0f));

            const auto input = detail::finiteSample(
                buffer.getSample(channel, sample));
            maximumInput = juce::jmax(maximumInput, std::abs(input));
            const auto driven = input * inputGain;
            const auto preTransformer = driven * gain;
            previousPreTransformer[index] = preTransformer;
            const auto transformerInput = detail::finiteSample(
                preTransformer * outputGain);
            buffer.setSample(channel, sample, transformerInput);
        }
    }

    juce::dsp::AudioBlock<float> fullBlock(buffer);
    auto block = fullBlock.getSubBlock(0, static_cast<size_t>(numSamples));
    auto upsampled = oversampling->processSamplesUp(block);
    for (size_t channel = 0; channel < upsampled.getNumChannels(); ++channel)
    {
        const auto index = juce::jmin(static_cast<size_t>(1), channel);
        auto* samples = upsampled.getChannelPointer(channel);
        for (size_t sample = 0; sample < upsampled.getNumSamples(); ++sample)
        {
            const auto x = detail::finiteSample(samples[sample]);
            // Asymmetric second-harmonic bias followed by a bounded soft
            // clip models FET/transformer coloration; a one-pole blocker
            // removes the resulting DC while retaining the asymmetry.
            const auto shaped = x
                + fetAsymmetryAmount * x * x / (1.0f + std::abs(x));
            const auto colored = std::tanh(fetHardness * shaped) / fetHardness;
            auto& input = dcInput[index];
            auto& output = dcOutput[index];
            const auto blocked = colored - input + dcRetention * output;
            input = colored;
            output = std::isfinite(blocked) ? blocked : 0.0f;
            samples[sample] = output;
        }
    }
    oversampling->processSamplesDown(block);

    float maximumOutput = 0.0f;
    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto mixAmount =
            smoothed[static_cast<size_t>(mixControl)].getNextValue();
        const auto outputTrimDb = lerp(
            -18.0f, 12.0f, smoothed[static_cast<size_t>(outputTrimControl)]
                               .getNextValue());
        const auto outputTrimGain =
            juce::Decibels::decibelsToGain(outputTrimDb);

        for (int channel = 0; channel < channels; ++channel)
        {
            auto& delay = dryDelay[static_cast<size_t>(channel)];
            auto readPosition = dryPosition - fixedLatencySamples;
            while (readPosition < 0)
                readPosition += static_cast<int>(delay.size());
            const auto dry = delay[static_cast<size_t>(readPosition)];
            delay[static_cast<size_t>(dryPosition)] =
                dryBuffer.getSample(channel, sample);

            const auto wet = detail::finiteSample(
                buffer.getSample(channel, sample));
            const auto mixed = dry + (wet - dry) * mixAmount;
            const auto result = detail::finiteSample(mixed * outputTrimGain);
            maximumOutput = juce::jmax(maximumOutput, std::abs(result));
            buffer.setSample(channel, sample, result);
        }
        dryPosition = (dryPosition + 1)
            % static_cast<int>(dryDelay[0].size());
    }

    const auto detectorDb = juce::Decibels::gainToDecibels(
        maximumDetector, -100.0f);
    gainReductionMeterDb.store(maximumReduction, std::memory_order_relaxed);
    detectorMeterDb.store(detectorDb, std::memory_order_relaxed);
    if (environment.captureTelemetry)
    {
        const auto inputDbMeasured = juce::Decibels::gainToDecibels(
            maximumInput, -100.0f);
        const auto outputDbMeasured = juce::Decibels::gainToDecibels(
            maximumOutput, -100.0f);
        telemetryState.sequence += 1;
        telemetryState.valueCount = telemetryValueCount;
        telemetryState.values[inputLevelDb] = inputDbMeasured;
        telemetryState.values[detectorLevelDbValue] = detectorDb;
        telemetryState.values[gainReductionDbValue] = maximumReduction;
        telemetryState.values[outputLevelDb] = outputDbMeasured;
        telemetryState.values[ratioAmount] = ratioTarget;
        appendContinuousTelemetryHistory(
            telemetryState,
            { maximumReduction, detectorDb, inputDbMeasured, outputDbMeasured },
            telemetryHistoryCount);
        telemetry.publish(telemetryState);
    }
}
} // namespace megadsp
