#include "AdaptiveClipper.h"
#include "DspHelpers.h"
#include "DspSafety.h"

#include <cmath>

namespace megadsp
{
namespace
{
using detail::exponential;
using detail::lerp;

constexpr std::array<int, AdaptiveClipperModule::oversamplingPathCount>
    oversamplingFactors { 2, 4, 8 };
} // namespace

float AdaptiveClipperModule::shapeSample(
    float input, float ceiling, float knee) noexcept
{
    const auto magnitude = std::abs(input);
    const auto kneeWidth = juce::jlimit(0.015f, 0.65f, knee);
    const auto threshold = ceiling * (1.0f - kneeWidth);
    if (magnitude <= threshold)
        return input;
    const auto span = juce::jmax(ceiling - threshold, 1.0e-6f);
    const auto curved = threshold
        + span * std::tanh((magnitude - threshold) / span);
    return std::copysign(juce::jmin(curved, ceiling), input);
}

void AdaptiveClipperModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    const auto channels = static_cast<size_t>(
        juce::jlimit(1, 2, static_cast<int>(spec.numChannels)));
    const auto maximumBlock = juce::jmax(
        1, static_cast<int>(spec.maximumBlockSize));

    fixedLatencySamples = 1;
    for (int pathIndex = 0; pathIndex < oversamplingPathCount; ++pathIndex)
    {
        auto& path = paths[static_cast<size_t>(pathIndex)];
        path.oversampling = std::make_unique<juce::dsp::Oversampling<float>>(
            channels, static_cast<size_t>(pathIndex + 1),
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
            true, true);
        path.oversampling->initProcessing(
            static_cast<size_t>(maximumBlock));
        path.latency = juce::jmax(
            0, juce::roundToInt(
                path.oversampling->getLatencyInSamples()));
        fixedLatencySamples = juce::jmax(
            fixedLatencySamples, path.latency);
        path.buffer.setSize(
            static_cast<int>(channels), maximumBlock, false, true, true);
    }

    for (auto& path : paths)
    {
        const auto alignment = fixedLatencySamples - path.latency;
        for (auto& delay : path.alignmentDelay)
            delay.assign(static_cast<size_t>(alignment + 1), 0.0f);
    }
    dryBuffer.setSize(
        static_cast<int>(channels), maximumBlock, false, true, true);
    for (auto& delay : dryDelay)
        delay.assign(static_cast<size_t>(fixedLatencySamples + 1), 0.0f);
    reset();
}

void AdaptiveClipperModule::reset()
{
    for (auto& path : paths)
    {
        if (path.oversampling != nullptr)
            path.oversampling->reset();
        path.buffer.clear();
        for (auto& delay : path.alignmentDelay)
            std::fill(delay.begin(), delay.end(), 0.0f);
        path.fastEnvelope.fill(0.0f);
        path.bodyEnvelope.fill(0.0f);
        path.driveGain = 1.0f;
        path.ceilingGain = 1.0f;
        path.shape = 0.5f;
        path.transientBias = 0.5f;
        path.measuredPeak = 0.0f;
        path.measuredClippedPeak = 0.0f;
        path.clippedEnergy = 0.0f;
        path.classification = 0.0f;
        path.knee = 0.0f;
        path.alignmentPosition = 0;
    }
    dryBuffer.clear();
    for (auto& delay : dryDelay)
        std::fill(delay.begin(), delay.end(), 0.0f);
    pathWeights = { 1.0f, 0.0f, 0.0f };
    previousOutput.fill(0.0f);
    mixValue = 1.0f;
    outputGain = 1.0f;
    dryPosition = 0;
    selectedPath = 0;
    initialized = false;
    clippingMeterDb.store(0.0f);
    telemetryState = {};
    telemetry.clear();
}

void AdaptiveClipperModule::processPath(
    PathState& path, int pathIndex, int channels, int numSamples,
    float driveTarget, float ceilingTarget, int style, float shapeTarget,
    float transientBiasTarget, float releaseMilliseconds, float stereoLink,
    bool autoTrim)
{
    if (path.oversampling == nullptr)
        return;
    juce::dsp::AudioBlock<float> fullBlock(path.buffer);
    auto block = fullBlock.getSubBlock(0, static_cast<size_t>(numSamples));
    auto upsampled = path.oversampling->processSamplesUp(block);
    const auto processingRate = sampleRate
        * static_cast<double>(oversamplingFactors[static_cast<size_t>(pathIndex)]);
    const auto parameterCoefficient = std::exp(
        -1.0f / static_cast<float>(processingRate * 0.020));
    const auto fastAttack = std::exp(
        -1.0f / static_cast<float>(processingRate * 0.00035));
    const auto fastRelease = std::exp(
        -1.0f / static_cast<float>(processingRate * 0.018));
    const auto bodyAttack = std::exp(
        -1.0f / static_cast<float>(processingRate * 0.012));
    const auto bodyRelease = std::exp(
        -1.0f / static_cast<float>(
            processingRate * juce::jmax(0.020f, releaseMilliseconds * 0.001f)));
    static constexpr std::array<float, 3> styleKnee {
        -0.16f, 0.0f, 0.18f
    };
    const auto styledShape = juce::jlimit(
        0.0f, 1.0f,
        shapeTarget + styleKnee[static_cast<size_t>(style)]);
    path.measuredPeak = 0.0f;
    path.measuredClippedPeak = 0.0f;
    path.clippedEnergy = 0.0f;

    for (size_t sample = 0; sample < upsampled.getNumSamples(); ++sample)
    {
        path.driveGain = parameterCoefficient * path.driveGain
            + (1.0f - parameterCoefficient) * driveTarget;
        path.ceilingGain = parameterCoefficient * path.ceilingGain
            + (1.0f - parameterCoefficient) * ceilingTarget;
        path.shape = parameterCoefficient * path.shape
            + (1.0f - parameterCoefficient) * styledShape;
        path.transientBias = parameterCoefficient * path.transientBias
            + (1.0f - parameterCoefficient) * transientBiasTarget;

        std::array<float, 2> magnitude {};
        auto linkedFast = 0.0f;
        auto linkedBody = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            const auto input = detail::finiteSample(
                upsampled.getChannelPointer(index)[sample]);
            magnitude[index] = std::abs(input * path.driveGain);
            const auto fastCoefficient = magnitude[index]
                    > path.fastEnvelope[index]
                ? fastAttack : fastRelease;
            const auto bodyCoefficient = magnitude[index]
                    > path.bodyEnvelope[index]
                ? bodyAttack : bodyRelease;
            path.fastEnvelope[index] =
                fastCoefficient * path.fastEnvelope[index]
                + (1.0f - fastCoefficient) * magnitude[index];
            path.bodyEnvelope[index] =
                bodyCoefficient * path.bodyEnvelope[index]
                + (1.0f - bodyCoefficient) * magnitude[index];
            linkedFast = juce::jmax(
                linkedFast, path.fastEnvelope[index]);
            linkedBody = juce::jmax(
                linkedBody, path.bodyEnvelope[index]);
        }

        const auto trim = autoTrim
            ? 1.0f / std::sqrt(juce::jmax(1.0f, path.driveGain)) : 1.0f;
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            const auto fast = lerp(
                path.fastEnvelope[index], linkedFast, stereoLink);
            const auto body = lerp(
                path.bodyEnvelope[index], linkedBody, stereoLink);
            const auto classification = juce::jlimit(
                0.0f, 1.0f, (fast - body) / juce::jmax(fast, 1.0e-6f));
            const auto transientProtection =
                classification * path.transientBias;
            const auto knee = juce::jlimit(
                0.015f, 0.62f,
                0.58f - path.shape * 0.48f
                    - transientProtection * 0.20f);
            auto* samples = upsampled.getChannelPointer(index);
            const auto driven = detail::finiteSample(
                samples[sample]) * path.driveGain;
            // The oversampled guard includes reconstruction margin so the
            // downsampled signal remains below the requested dBTP ceiling.
            const auto guardedCeiling = path.ceilingGain * 0.94f;
            const auto shaped = detail::finiteSample(
                shapeSample(driven, guardedCeiling, knee));
            const auto clipped = detail::finiteSample(shaped * trim);
            path.measuredPeak = juce::jmax(
                path.measuredPeak, std::abs(clipped));
            path.measuredClippedPeak = juce::jmax(
                path.measuredClippedPeak, std::abs(shaped));
            const auto difference = driven - shaped;
            path.clippedEnergy += difference * difference;
            path.classification = classification;
            path.knee = knee;
            samples[sample] = clipped;
        }
    }
    path.oversampling->processSamplesDown(block);

    const auto alignment = fixedLatencySamples - path.latency;
    for (int sample = 0; sample < numSamples; ++sample)
    {
        for (int channel = 0; channel < channels; ++channel)
        {
            auto& delay = path.alignmentDelay[static_cast<size_t>(channel)];
            const auto current = detail::finiteSample(
                path.buffer.getSample(channel, sample));
            if (alignment <= 0)
            {
                path.buffer.setSample(channel, sample, current);
                continue;
            }
            auto readPosition = path.alignmentPosition - alignment;
            while (readPosition < 0)
                readPosition += static_cast<int>(delay.size());
            const auto aligned = delay[static_cast<size_t>(readPosition)];
            delay[static_cast<size_t>(path.alignmentPosition)] = current;
            path.buffer.setSample(channel, sample, aligned);
        }
        path.alignmentPosition = (path.alignmentPosition + 1)
            % static_cast<int>(path.alignmentDelay[0].size());
    }
}

void AdaptiveClipperModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    const auto channels = juce::jlimit(0, 2, buffer.getNumChannels());
    const auto numSamples = buffer.getNumSamples();
    if (channels == 0 || numSamples <= 0 || paths[0].oversampling == nullptr
        || numSamples > dryBuffer.getNumSamples())
        return;
    const auto normalized = [&controls](int index, float fallback)
    {
        return detail::normalizedControl(
            controls[static_cast<size_t>(index)], fallback);
    };

    const auto driveDb =
        normalized(driveControl, 0.35f) * 24.0f;
    const auto driveTarget =
        juce::Decibels::decibelsToGain(driveDb);
    const auto ceilingDb = lerp(
        -12.0f, 0.0f, normalized(ceilingControl, 0.92f));
    const auto ceilingTarget =
        juce::Decibels::decibelsToGain(ceilingDb);
    const auto style = discreteIndex(
        normalized(styleControl, 0.0f), 3);
    const auto shape = normalized(shapeControl, 0.65f);
    const auto transientBias =
        normalized(transientBiasControl, 0.55f);
    const auto releaseMilliseconds = exponential(
        20.0f, 1000.0f, normalized(releaseControl, 0.35f));
    const auto stereoLink =
        normalized(stereoLinkControl, 1.0f);
    const auto targetPath = discreteIndex(
        normalized(oversamplingControl, 0.5f),
        oversamplingPathCount);
    const auto autoTrim =
        normalized(autoTrimControl, 1.0f) >= 0.5f;
    const auto mixTarget = normalized(mixControl, 1.0f);
    const auto outputTarget = juce::Decibels::decibelsToGain(
        lerp(-18.0f, 12.0f, normalized(outputControl, 0.6f)));

    if (!initialized)
    {
        selectedPath = targetPath;
        pathWeights.fill(0.0f);
        pathWeights[static_cast<size_t>(targetPath)] = 1.0f;
        mixValue = mixTarget;
        outputGain = outputTarget;
        for (auto& path : paths)
        {
            path.driveGain = driveTarget;
            path.ceilingGain = ceilingTarget;
            path.shape = shape;
            path.transientBias = transientBias;
        }
        initialized = true;
    }
    selectedPath = targetPath;

    float maximumInput = 0.0f;
    for (int channel = 0; channel < channels; ++channel)
    {
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto input = detail::finiteSample(
                buffer.getSample(channel, sample));
            dryBuffer.setSample(channel, sample, input);
            maximumInput = juce::jmax(maximumInput, std::abs(input));
        }
        for (auto& path : paths)
            path.buffer.copyFrom(
                channel, 0, dryBuffer, channel, 0, numSamples);
    }

    for (int pathIndex = 0; pathIndex < oversamplingPathCount; ++pathIndex)
        processPath(
            paths[static_cast<size_t>(pathIndex)], pathIndex,
            channels, numSamples, driveTarget, ceilingTarget, style,
            shape, transientBias, releaseMilliseconds, stereoLink, autoTrim);

    const auto weightCoefficient = std::exp(
        -1.0f / (0.030f * static_cast<float>(sampleRate)));
    const auto mixCoefficient = std::exp(
        -1.0f / (0.020f * static_cast<float>(sampleRate)));
    float maximumOutput = 0.0f;
    for (int sample = 0; sample < numSamples; ++sample)
    {
        float weightSum = 0.0f;
        for (int pathIndex = 0; pathIndex < oversamplingPathCount; ++pathIndex)
        {
            auto& weight = pathWeights[static_cast<size_t>(pathIndex)];
            const auto target = pathIndex == targetPath ? 1.0f : 0.0f;
            weight = weightCoefficient * weight
                     + (1.0f - weightCoefficient) * target;
            weightSum += weight;
        }
        weightSum = juce::jmax(weightSum, 1.0e-6f);
        mixValue = mixCoefficient * mixValue
                   + (1.0f - mixCoefficient) * mixTarget;
        outputGain = mixCoefficient * outputGain
                     + (1.0f - mixCoefficient) * outputTarget;

        for (int channel = 0; channel < channels; ++channel)
        {
            float wet = 0.0f;
            for (int pathIndex = 0;
                 pathIndex < oversamplingPathCount; ++pathIndex)
                wet += paths[static_cast<size_t>(pathIndex)]
                           .buffer.getSample(channel, sample)
                       * pathWeights[static_cast<size_t>(pathIndex)]
                       / weightSum;

            auto& delay = dryDelay[static_cast<size_t>(channel)];
            auto readPosition = dryPosition - fixedLatencySamples;
            while (readPosition < 0)
                readPosition += static_cast<int>(delay.size());
            const auto dry = delay[static_cast<size_t>(readPosition)];
            delay[static_cast<size_t>(dryPosition)] =
                dryBuffer.getSample(channel, sample);
            auto result = (dry + (wet - dry) * mixValue) * outputGain;
            if (mixValue > 0.999f)
                result = juce::jlimit(
                    -ceilingTarget * 0.94f,
                    ceilingTarget * 0.94f, result);
            result = detail::finiteSample(result);
            maximumOutput = juce::jmax(maximumOutput, std::abs(result));
            previousOutput[static_cast<size_t>(channel)] = result;
            buffer.setSample(channel, sample, result);
        }
        dryPosition = (dryPosition + 1)
            % static_cast<int>(dryDelay[0].size());
    }

    const auto& measuredPath =
        paths[static_cast<size_t>(targetPath)];
    const auto maximumClippingDb = juce::jmax(
        0.0f, juce::Decibels::gainToDecibels(
            maximumInput * driveTarget
                / juce::jmax(measuredPath.measuredClippedPeak, 1.0e-8f),
            0.0f));
    clippingMeterDb.store(
        maximumClippingDb, std::memory_order_relaxed);
    if (environment.captureTelemetry)
    {
        const auto measuredPeak = juce::jmax(
            maximumOutput, measuredPath.measuredPeak);
        const auto marginDb = juce::Decibels::gainToDecibels(
            ceilingTarget / juce::jmax(measuredPeak, 1.0e-8f), -100.0f);
        telemetryState.sequence += 1;
        telemetryState.valueCount = telemetryValueCount;
        telemetryState.values[inputPeak] = maximumInput;
        telemetryState.values[measuredTruePeak] = measuredPeak;
        telemetryState.values[clippingDecibels] = maximumClippingDb;
        telemetryState.values[transientClassification] =
            measuredPath.classification;
        telemetryState.values[ceilingMarginDecibels] = marginDb;
        telemetryState.values[clippedEnergy] =
            measuredPath.clippedEnergy
            / static_cast<float>(juce::jmax(
                1, channels * numSamples
                    * oversamplingFactors[static_cast<size_t>(targetPath)]));
        telemetryState.values[activeOversampling] =
            static_cast<float>(
                oversamplingFactors[static_cast<size_t>(targetPath)]);
        telemetryState.values[adaptiveKnee] = measuredPath.knee;
        appendContinuousTelemetryHistory(
            telemetryState,
            { maximumInput, measuredPeak, maximumClippingDb,
              measuredPath.classification },
            telemetryHistoryValueCount);
        telemetry.publish(telemetryState);
    }
}

bool AdaptiveClipperModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}
} // namespace megadsp
