#include "OptoLeveler.h"

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

float smoothStep(float value) noexcept
{
    value = juce::jlimit(0.0f, 1.0f, value);
    return value * value * (3.0f - 2.0f * value);
}

float onePoleCoefficient(double rate, float frequency) noexcept
{
    return 1.0f - std::exp(
        -juce::MathConstants<float>::twoPi
        * juce::jlimit(5.0f, static_cast<float>(rate * 0.45), frequency)
        / static_cast<float>(rate));
}
} // namespace

void OptoLevelerModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    for (auto& value : smoothed)
        value.reset(sampleRate, 0.03);

    const auto channelCount = juce::jmax(1u, spec.numChannels);
    oversampling = std::make_unique<juce::dsp::Oversampling<float>>(
        static_cast<size_t>(channelCount), 1,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true, true);
    oversampling->initProcessing(
        juce::jmax(static_cast<juce::uint32>(1), spec.maximumBlockSize));
    oversamplingLatency =
        juce::roundToInt(oversampling->getLatencyInSamples());
    dryBuffer.setSize(
        static_cast<int>(channelCount),
        static_cast<int>(juce::jmax(static_cast<juce::uint32>(1),
                                    spec.maximumBlockSize)),
        false, true, true);
    for (auto& channel : dryLatencyBuffer)
        channel.assign(
            static_cast<size_t>(juce::jmax(1, oversamplingLatency)), 0.0f);
    reset();
}

void OptoLevelerModule::reset()
{
    cells = {};
    for (auto& value : smoothed)
        value.setCurrentAndTargetValue(0.0f);
    if (oversampling != nullptr)
        oversampling->reset();
    for (auto& channel : dryLatencyBuffer)
        std::fill(channel.begin(), channel.end(), 0.0f);
    dryLatencyPosition = 0;
    initialized = false;
    telemetrySequence = 0;
    meterReductionDb.store(0.0f, std::memory_order_relaxed);
    meterDetectorDb.store(-100.0f, std::memory_order_relaxed);
    telemetryState = {};
    telemetry.clear();
}

bool OptoLevelerModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}

void OptoLevelerModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    if (oversampling == nullptr)
        return;

    const auto channels = juce::jlimit(0, 2, buffer.getNumChannels());
    const auto samples = buffer.getNumSamples();
    if (channels <= 0 || samples <= 0)
        return;

    std::array<float, activeControlCount> targets {};
    for (int control = 0; control < activeControlCount; ++control)
        targets[static_cast<size_t>(control)] = detail::normalizedControl(
            controls[static_cast<size_t>(control)], 0.0f);
    if (!initialized)
    {
        for (int control = 0; control < activeControlCount; ++control)
            smoothed[static_cast<size_t>(control)].setCurrentAndTargetValue(
                targets[static_cast<size_t>(control)]);
        initialized = true;
    }
    else
    {
        for (int control = 0; control < activeControlCount; ++control)
            smoothed[static_cast<size_t>(control)].setTargetValue(
                targets[static_cast<size_t>(control)]);
    }

    // Keep an aligned dry copy for the latency-aligned Mix stage below; the
    // leveling loop itself is zero-latency, only the analog coloration
    // oversampling stage adds fixed latency.
    for (int channel = 0; channel < channels; ++channel)
        dryBuffer.copyFrom(channel, 0, buffer, channel, 0, samples);

    constexpr auto thresholdDb = -18.0f;
    constexpr auto attackFastMs = 1.5f;
    constexpr auto attackSlowMs = 15.0f;
    constexpr auto releaseFastMs = 60.0f;
    const auto memoryRiseCoefficient = coefficient(sampleRate, 40.0f);

    auto maximumInput = 0.0f;
    auto maximumReduction = 0.0f;
    auto maximumDetector = 0.0f;
    auto maximumMemory = 0.0f;

    for (int sample = 0; sample < samples; ++sample)
    {
        // Only the controls the leveling stage needs are advanced here; the
        // Mix and Output smoothers are advanced once each in the mix loop
        // below so every ramp step is consumed exactly once per sample.
        std::array<float, activeControlCount> current {};
        for (int control = 0; control < mixControl; ++control)
            current[static_cast<size_t>(control)] =
                smoothed[static_cast<size_t>(control)].getNextValue();

        const auto driveDb = lerp(0.0f, 40.0f, current[peakReductionControl]);
        const auto makeupDb = lerp(-12.0f, 24.0f, current[gainControl]);
        const auto compressLimit = current[compressLimitControl];
        const auto ratio = lerp(3.0f, 20.0f, compressLimit);
        const auto kneeDb = lerp(10.0f, 2.0f, compressLimit);
        const auto hfEmphasisGain = juce::Decibels::decibelsToGain(
            lerp(0.0f, 18.0f, current[hfEmphasisControl])) - 1.0f;
        const auto lowCutHz = exponential(
            20.0f, 300.0f, current[sidechainLowCutControl]);
        const auto cellAge = current[cellAgeControl];
        const auto linkAmount = current[stereoLinkControl];

        const auto releaseSlowMs = lerp(1500.0f, 6000.0f, cellAge);
        const auto memoryWeightMax = lerp(0.6f, 1.0f, cellAge);
        const auto memoryDecayCoefficient = coefficient(
            sampleRate, lerp(2000.0f, 10000.0f, cellAge));
        const auto hfEmphasisCoefficient = onePoleCoefficient(
            sampleRate, 3000.0f);
        const auto lowCutCoefficient = onePoleCoefficient(
            sampleRate, lowCutHz);
        const auto releaseFastCoefficient =
            coefficient(sampleRate, releaseFastMs);
        const auto releaseSlowCoefficient =
            coefficient(sampleRate, releaseSlowMs);

        std::array<float, 2> rawInput {};
        std::array<float, 2> detectorLevel {};
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            auto& cell = cells[index];
            rawInput[index] =
                detail::finiteSample(buffer.getSample(channel, sample));
            maximumInput = juce::jmax(maximumInput, std::abs(rawInput[index]));

            // Feedback detector: the cell "sees" its own prior output, one
            // sample delayed, which is the standard causal approximation of
            // an analog feedback compression loop.
            const auto feedbackSample = cell.previousCellOutput;
            cell.hfEmphasisLow += hfEmphasisCoefficient
                                  * (feedbackSample - cell.hfEmphasisLow);
            const auto highBand = feedbackSample - cell.hfEmphasisLow;
            const auto emphasized =
                feedbackSample + highBand * hfEmphasisGain;

            cell.lowCutFirst +=
                lowCutCoefficient * (emphasized - cell.lowCutFirst);
            const auto firstHighPass = emphasized - cell.lowCutFirst;
            cell.lowCutSecond +=
                lowCutCoefficient * (firstHighPass - cell.lowCutSecond);
            const auto filtered = firstHighPass - cell.lowCutSecond;

            detectorLevel[index] = std::abs(filtered);
        }

        auto linkedLevel = detectorLevel[0];
        for (int channel = 1; channel < channels; ++channel)
            linkedLevel = juce::jmax(linkedLevel, detectorLevel[static_cast<size_t>(channel)]);

        for (int channel = 0; channel < channels; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            auto& cell = cells[index];
            const auto effectiveLevel = detectorLevel[index]
                + (linkedLevel - detectorLevel[index]) * linkAmount;
            const auto detectorDb =
                juce::Decibels::gainToDecibels(effectiveLevel, -100.0f)
                + driveDb;
            maximumDetector = juce::jmax(maximumDetector, detectorDb);

            const auto over = detectorDb - thresholdDb;
            float reductionDb = 0.0f;
            if (kneeDb > 0.0f && over > -kneeDb * 0.5f
                && over < kneeDb * 0.5f)
            {
                const auto kneeInput = over + kneeDb * 0.5f;
                reductionDb = (1.0f / ratio - 1.0f) * kneeInput * kneeInput
                              / (2.0f * kneeDb);
            }
            else if (over >= kneeDb * 0.5f)
            {
                reductionDb = (1.0f / ratio - 1.0f) * over;
            }
            const auto targetGainDb =
                juce::jlimit(-60.0f, 0.0f, reductionDb);

            // Program-dependent attack: deeper reduction demands are let
            // through faster than shallow, incidental ones.
            const auto overshootAmount =
                juce::jlimit(0.0f, 1.0f, -targetGainDb / 24.0f);
            const auto attackMs =
                lerp(attackSlowMs, attackFastMs, overshootAmount);
            const auto attackCoefficient = coefficient(sampleRate, attackMs);

            // Fast release plus a slow "history memory" stage that mimics
            // the persistence of an aging photoresistive cell.
            const auto releaseCoefficient = lerp(
                releaseFastCoefficient, releaseSlowCoefficient,
                cell.memory * memoryWeightMax);
            const auto smoothing = targetGainDb < cell.gainDb
                                       ? attackCoefficient
                                       : releaseCoefficient;
            cell.gainDb =
                smoothing * cell.gainDb + (1.0f - smoothing) * targetGainDb;
            if (!std::isfinite(cell.gainDb))
                cell.gainDb = 0.0f;
            maximumReduction = juce::jmax(maximumReduction, -cell.gainDb);

            const auto memoryTarget =
                smoothStep(-cell.gainDb / 24.0f);
            const auto memoryCoefficient = memoryTarget > cell.memory
                                                ? memoryRiseCoefficient
                                                : memoryDecayCoefficient;
            cell.memory = memoryCoefficient * cell.memory
                          + (1.0f - memoryCoefficient) * memoryTarget;
            cell.memory = juce::jlimit(0.0f, 1.0f, cell.memory);
            maximumMemory = juce::jmax(maximumMemory, cell.memory);

            const auto cellGain =
                juce::Decibels::decibelsToGain(cell.gainDb);
            const auto cellOutput = rawInput[index] * cellGain;
            cell.previousCellOutput =
                std::isfinite(cellOutput) ? cellOutput : 0.0f;

            const auto leveled = cellOutput
                * juce::Decibels::decibelsToGain(makeupDb);
            buffer.setSample(channel, sample, detail::finiteSample(leveled));
        }
    }

    // Bounded, fixed-latency oversampled analog coloration: a gentle,
    // slightly asymmetric soft clip applied to the leveled signal that
    // approaches a fixed asymptote rather than boosting or clamping level.
    juce::dsp::AudioBlock<float> block(buffer);
    auto upsampled = oversampling->processSamplesUp(block);
    constexpr auto drive = 1.35f;
    constexpr auto bias = 0.015f;
    const auto normalization = 1.0f / std::tanh(drive);
    for (size_t channel = 0; channel < upsampled.getNumChannels(); ++channel)
    {
        auto* data = upsampled.getChannelPointer(channel);
        for (size_t index = 0; index < upsampled.getNumSamples(); ++index)
        {
            const auto shaped =
                (std::tanh(data[index] * drive + bias) - std::tanh(bias))
                * normalization;
            data[index] = shaped;
        }
    }
    oversampling->processSamplesDown(block);

    auto maximumOutput = 0.0f;
    for (int sample = 0; sample < samples; ++sample)
    {
        const auto mix = juce::jlimit(
            0.0f, 1.0f, smoothed[mixControl].getNextValue());
        const auto outputGain = juce::Decibels::decibelsToGain(
            lerp(-18.0f, 12.0f, smoothed[outputControl].getNextValue()));

        for (int channel = 0; channel < channels; ++channel)
        {
            const auto index = static_cast<size_t>(juce::jmin(channel, 1));
            auto& delayedDry = dryLatencyBuffer[index][
                static_cast<size_t>(dryLatencyPosition)];
            const auto dry = delayedDry;
            delayedDry = dryBuffer.getSample(channel, sample);

            const auto wet = buffer.getSample(channel, sample);
            const auto blended = dry + (wet - dry) * mix;
            const auto output = detail::finiteSample(blended * outputGain);
            maximumOutput = juce::jmax(maximumOutput, std::abs(output));
            buffer.setSample(channel, sample, output);
        }
        if (++dryLatencyPosition >= juce::jmax(1, oversamplingLatency))
            dryLatencyPosition = 0;
    }

    meterReductionDb.store(maximumReduction, std::memory_order_relaxed);
    meterDetectorDb.store(maximumDetector, std::memory_order_relaxed);

    if (environment.captureTelemetry)
    {
        telemetrySequence += 1;
        telemetryState.sequence = telemetrySequence;
        telemetryState.valueCount = telemetryValueCount;
        telemetryState.values[inputLevelDb] =
            juce::Decibels::gainToDecibels(maximumInput, -100.0f);
        telemetryState.values[outputLevelDb] =
            juce::Decibels::gainToDecibels(maximumOutput, -100.0f);
        telemetryState.values[gainReductionDb] = maximumReduction;
        telemetryState.values[cellMemory] = maximumMemory;
        telemetryState.values[detectorLevelDb] = maximumDetector;
        appendContinuousTelemetryHistory(
            telemetryState,
            { maximumDetector, maximumReduction, maximumMemory,
              telemetryState.values[outputLevelDb] },
            telemetryHistoryCount);
        telemetry.publish(telemetryState);
    }
}
} // namespace megadsp
