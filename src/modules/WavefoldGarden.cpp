#include "WavefoldGarden.h"
#include "DspHelpers.h"

#include <cmath>

namespace megadsp
{
namespace
{
using detail::coefficient;
using detail::exponential;
using detail::lerp;

float safe(float value, float fallback)
{
    return std::isfinite(value) ? juce::jlimit(0.0f, 1.0f, value)
                                : fallback;
}

float softBound(float value, float range)
{
    const auto safeRange = juce::jmax(0.25f, range);
    return safeRange * std::tanh(value / safeRange);
}

float reflectedTriangle(float value)
{
    auto wrapped = std::fmod(value + 1.0f, 4.0f);
    if (wrapped < 0.0f)
        wrapped += 4.0f;
    return wrapped < 2.0f ? wrapped - 1.0f : 3.0f - wrapped;
}
} // namespace

void WavefoldGardenModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = juce::jmax(8000.0, spec.sampleRate);
    channels = juce::jlimit(1, 2, static_cast<int>(spec.numChannels));
    oversampling = std::make_unique<juce::dsp::Oversampling<float>>(
        static_cast<size_t>(spec.numChannels), 2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    oversampling->initProcessing(spec.maximumBlockSize);
    oversamplingLatency = juce::roundToInt(oversampling->getLatencyInSamples());
    dryBuffer.setSize(static_cast<int>(spec.numChannels),
                      static_cast<int>(spec.maximumBlockSize), false, true, true);
    parameterFrames.assign(static_cast<size_t>(spec.maximumBlockSize), {});
    for (auto& channel : dryLatencyBuffer)
        channel.assign(static_cast<size_t>(juce::jmax(1, oversamplingLatency)), 0.0f);

    for (auto& smoother : characterMix)
        smoother.reset(sampleRate, 0.03);
    for (auto& smoother : foldMix)
        smoother.reset(sampleRate, 0.025);
    driveSmoothed.reset(sampleRate, 0.03);
    symmetrySmoothed.reset(sampleRate, 0.03);
    shapeSmoothed.reset(sampleRate, 0.03);
    dynamicsSmoothed.reset(sampleRate, 0.04);
    attackSmoothed.reset(sampleRate, 0.04);
    releaseSmoothed.reset(sampleRate, 0.04);
    toneSmoothed.reset(sampleRate, 0.03);
    stereoBloomSmoothed.reset(sampleRate, 0.03);
    mixSmoothed.reset(sampleRate, 0.02);
    outputSmoothed.reset(sampleRate, 0.02);
    reset();
}

void WavefoldGardenModule::reset()
{
    if (oversampling != nullptr)
        oversampling->reset();
    dryBuffer.clear();
    for (auto& channel : dryLatencyBuffer)
        std::fill(channel.begin(), channel.end(), 0.0f);
    for (auto& frame : parameterFrames)
        frame = {};
    for (int character = 0; character < characterCount; ++character)
        characterMix[static_cast<size_t>(character)].setCurrentAndTargetValue(
            character == 0 ? 1.0f : 0.0f);
    for (int fold = 0; fold < foldCount; ++fold)
        foldMix[static_cast<size_t>(fold)].setCurrentAndTargetValue(
            fold == 0 ? 1.0f : 0.0f);
    driveSmoothed.setCurrentAndTargetValue(1.0f);
    symmetrySmoothed.setCurrentAndTargetValue(0.0f);
    shapeSmoothed.setCurrentAndTargetValue(0.35f);
    dynamicsSmoothed.setCurrentAndTargetValue(0.0f);
    attackSmoothed.setCurrentAndTargetValue(12.0f);
    releaseSmoothed.setCurrentAndTargetValue(160.0f);
    toneSmoothed.setCurrentAndTargetValue(12000.0f);
    stereoBloomSmoothed.setCurrentAndTargetValue(0.0f);
    mixSmoothed.setCurrentAndTargetValue(0.65f);
    outputSmoothed.setCurrentAndTargetValue(1.0f);
    dcInputState.fill(0.0f);
    dcOutputState.fill(0.0f);
    toneState.fill(0.0f);
    focusState.fill(0.0f);
    bloomLowState.fill(0.0f);
    dryLatencyPosition = 0;
    linkedEnvelope = 0.0f;
    inputMeanSquare = 1.0e-8f;
    wetMeanSquare = 1.0e-8f;
    compensationGain = 1.0f;
    activityMeter.store(0.0f, std::memory_order_relaxed);
    initialized = false;
}

float WavefoldGardenModule::chebyshevPolynomial(int order, float x)
{
    if (order <= 0)
        return 1.0f;
    if (order == 1)
        return x;
    auto previous = 1.0f;
    auto current = x;
    for (int index = 2; index <= order; ++index)
    {
        const auto next = 2.0f * x * current - previous;
        previous = current;
        current = next;
    }
    return current;
}

float WavefoldGardenModule::petalFold(
    float input, int folds, float symmetry, float shape)
{
    auto value = softBound(input, 6.0f);
    const auto blend = 0.42f + 0.48f * shape;
    for (int stage = 0; stage < folds; ++stage)
    {
        const auto asymmetry = 1.0f + (value >= 0.0f ? symmetry : -symmetry) * 0.55f;
        const auto stageDrive = (1.15f + 2.45f * shape)
            + static_cast<float>(stage) * (0.16f + 0.08f * shape);
        const auto folded = std::sin(value * asymmetry * stageDrive);
        value += (folded - value) * blend;
        value *= 0.92f - 0.02f * static_cast<float>(juce::jmin(stage, 4));
    }
    return value / (1.0f + 0.16f * static_cast<float>(folds - 1));
}

float WavefoldGardenModule::prismFold(
    float input, int folds, float symmetry, float shape)
{
    auto value = softBound(input, 5.0f);
    const auto cornerBlend = 0.12f + 0.36f * shape;
    for (int stage = 0; stage < folds; ++stage)
    {
        const auto asymmetry = 1.0f + (value >= 0.0f ? symmetry : -symmetry) * 0.45f;
        const auto span = (1.2f + 1.65f * shape)
            + static_cast<float>(stage) * (0.22f + 0.10f * shape);
        value = reflectedTriangle(value * asymmetry * span);
        const auto rounded =
            std::sin(juce::MathConstants<float>::halfPi * value);
        value += (rounded - value) * cornerBlend;
    }
    return value / std::sqrt(1.0f + 0.22f * static_cast<float>(folds));
}

float WavefoldGardenModule::chebyshevFold(
    float input, int folds, float symmetry, float shape)
{
    auto value = softBound(input, 4.0f);
    value *= value >= 0.0f ? 1.0f + symmetry * 0.35f
                           : 1.0f - symmetry * 0.35f;
    const auto bounded = std::tanh(value * (0.85f + 2.7f * shape));
    const auto order = juce::jlimit(2, 9, folds + 1);
    const auto base = chebyshevPolynomial(order, bounded);
    const auto companion = chebyshevPolynomial(juce::jmin(10, order + 1), bounded);
    const auto anchor = chebyshevPolynomial(juce::jmax(1, order - 1), bounded);
    const auto blend = 0.25f + 0.55f * shape;
    auto result = bounded * (1.0f - 0.45f * blend)
        + base * (0.72f + 0.12f * blend)
        + companion * (0.18f * blend)
        + anchor * (0.12f * symmetry * shape);
    result /= 1.15f + 0.12f * static_cast<float>(order) + 0.10f * blend;
    return result;
}

float WavefoldGardenModule::bloomFold(
    float input, int folds, float symmetry, float shape, float opening)
{
    auto value = softBound(input, 6.0f);
    const auto motion = 1.0f + shape * (1.2f + 0.8f * opening);
    value *= motion;
    auto positiveThreshold = lerp(0.88f, 0.16f, opening);
    auto negativeThreshold = lerp(0.88f, 0.16f, opening);
    positiveThreshold *= 1.0f - 0.42f * symmetry;
    negativeThreshold *= 1.0f + 0.42f * symmetry;
    positiveThreshold = juce::jmax(0.05f, positiveThreshold);
    negativeThreshold = juce::jmax(0.05f, negativeThreshold);

    for (int stage = 0; stage < folds; ++stage)
    {
        const auto threshold =
            value >= 0.0f ? positiveThreshold : negativeThreshold;
        const auto magnitude = std::abs(value);
        if (magnitude > threshold)
        {
            const auto excess = magnitude - threshold;
            const auto folded = threshold
                - std::sin(excess * (1.0f + 2.4f * shape))
                    * (0.34f + 0.42f * opening);
            value = std::copysign(folded, value);
        }
        value = std::tanh(value * (0.95f + 0.5f * opening));
        positiveThreshold *= 0.97f;
        negativeThreshold *= 0.97f;
    }
    return value / (0.94f + 0.06f * static_cast<float>(folds));
}

float WavefoldGardenModule::processCharacter(
    int character, float input, int folds, float symmetry, float shape,
    float opening) const
{
    switch (character)
    {
        case 1: return prismFold(input, folds, symmetry, shape);
        case 2: return chebyshevFold(input, folds, symmetry, shape);
        case 3: return bloomFold(input, folds, symmetry, shape, opening);
        default: return petalFold(input, folds, symmetry, shape);
    }
}

void WavefoldGardenModule::process(juce::AudioBuffer<float>& buffer,
                                   const ControlValues& controls,
                                   const ProcessEnvironment&)
{
    juce::ScopedNoDenormals noDenormals;
    if (oversampling == nullptr || buffer.getNumChannels() <= 0
        || buffer.getNumSamples() <= 0)
        return;

    const auto sampleCount = buffer.getNumSamples();
    const auto channelCount = juce::jmin(2, buffer.getNumChannels());
    if (sampleCount > static_cast<int>(parameterFrames.size()))
    {
        const auto chunkCapacity =
            juce::jmax(1, static_cast<int>(parameterFrames.size()));
        for (int offset = 0; offset < sampleCount; offset += chunkCapacity)
        {
            const auto chunkSamples =
                juce::jmin(chunkCapacity, sampleCount - offset);
            std::array<float*, 2> pointers {};
            for (int channel = 0; channel < channelCount; ++channel)
                pointers[static_cast<size_t>(channel)] =
                    buffer.getWritePointer(channel, offset);
            juce::AudioBuffer<float> chunk(
                pointers.data(), channelCount, chunkSamples);
            process(chunk, controls, {});
        }
        return;
    }

    const auto character = discreteIndex(safe(controls[0], 0.0f), characterCount);
    const auto folds = juce::jlimit(
        1, foldCount, juce::roundToInt(1.0f + safe(controls[2], 0.0f) * 7.0f));
    const auto driveGain = juce::Decibels::decibelsToGain(
        lerp(0.0f, 36.0f, safe(controls[1], 0.0f)));
    const auto symmetry = lerp(-1.0f, 1.0f, safe(controls[3], 0.5f));
    const auto shape = safe(controls[4], 0.35f);
    const auto dynamics = lerp(-1.0f, 1.0f, safe(controls[5], 0.5f));
    const auto attackMs = exponential(0.1f, 100.0f, safe(controls[6], 0.52f));
    const auto releaseMs = exponential(10.0f, 1000.0f, safe(controls[7], 0.40f));
    const auto toneHz = exponential(
        500.0f, juce::jmin(20000.0f, static_cast<float>(sampleRate * 0.45)),
        safe(controls[8], 0.78f));
    const auto stereoBloom = safe(controls[9], 0.0f);
    const auto mix = safe(controls[10], 0.65f);
    const auto output = juce::Decibels::decibelsToGain(
        lerp(-18.0f, 12.0f, safe(controls[11], 0.6f)));

    if (!initialized)
    {
        for (int index = 0; index < characterCount; ++index)
            characterMix[static_cast<size_t>(index)].setCurrentAndTargetValue(
                index == character ? 1.0f : 0.0f);
        for (int index = 0; index < foldCount; ++index)
            foldMix[static_cast<size_t>(index)].setCurrentAndTargetValue(
                index == folds - 1 ? 1.0f : 0.0f);
        driveSmoothed.setCurrentAndTargetValue(driveGain);
        symmetrySmoothed.setCurrentAndTargetValue(symmetry);
        shapeSmoothed.setCurrentAndTargetValue(shape);
        dynamicsSmoothed.setCurrentAndTargetValue(dynamics);
        attackSmoothed.setCurrentAndTargetValue(attackMs);
        releaseSmoothed.setCurrentAndTargetValue(releaseMs);
        toneSmoothed.setCurrentAndTargetValue(toneHz);
        stereoBloomSmoothed.setCurrentAndTargetValue(stereoBloom);
        mixSmoothed.setCurrentAndTargetValue(mix);
        outputSmoothed.setCurrentAndTargetValue(output);
        initialized = true;
    }
    else
    {
        for (int index = 0; index < characterCount; ++index)
            characterMix[static_cast<size_t>(index)].setTargetValue(
                index == character ? 1.0f : 0.0f);
        for (int index = 0; index < foldCount; ++index)
            foldMix[static_cast<size_t>(index)].setTargetValue(
                index == folds - 1 ? 1.0f : 0.0f);
        driveSmoothed.setTargetValue(driveGain);
        symmetrySmoothed.setTargetValue(symmetry);
        shapeSmoothed.setTargetValue(shape);
        dynamicsSmoothed.setTargetValue(dynamics);
        attackSmoothed.setTargetValue(attackMs);
        releaseSmoothed.setTargetValue(releaseMs);
        toneSmoothed.setTargetValue(toneHz);
        stereoBloomSmoothed.setTargetValue(stereoBloom);
        mixSmoothed.setTargetValue(mix);
        outputSmoothed.setTargetValue(output);
    }

    for (int sample = 0; sample < sampleCount; ++sample)
    {
        auto& frame = parameterFrames[static_cast<size_t>(sample)];
        float characterWeightSum = 0.0f;
        for (int index = 0; index < characterCount; ++index)
        {
            const auto weight =
                characterMix[static_cast<size_t>(index)].getNextValue();
            frame.characterWeights[static_cast<size_t>(index)] = weight;
            characterWeightSum += weight;
        }
        if (characterWeightSum > 1.0e-6f)
            for (auto& weight : frame.characterWeights)
                weight /= characterWeightSum;

        float foldWeightSum = 0.0f;
        for (int index = 0; index < foldCount; ++index)
        {
            const auto weight = foldMix[static_cast<size_t>(index)].getNextValue();
            frame.foldWeights[static_cast<size_t>(index)] = weight;
            foldWeightSum += weight;
        }
        if (foldWeightSum > 1.0e-6f)
            for (auto& weight : frame.foldWeights)
                weight /= foldWeightSum;

        frame.driveGain = driveSmoothed.getNextValue();
        frame.symmetry = symmetrySmoothed.getNextValue();
        frame.shape = shapeSmoothed.getNextValue();
        frame.dynamics = dynamicsSmoothed.getNextValue();
        frame.dynamicThreshold = juce::jlimit(
            0.03f, 0.88f,
            0.08f + 0.50f * std::abs(frame.dynamics)
                + 0.12f * (1.0f - frame.shape));
        frame.attackCoefficient = coefficient(
            sampleRate * static_cast<double>(oversamplingFactor),
            attackSmoothed.getNextValue());
        frame.releaseCoefficient = coefficient(
            sampleRate * static_cast<double>(oversamplingFactor),
            releaseSmoothed.getNextValue());
        const auto tone = toneSmoothed.getNextValue();
        frame.toneCoefficient = std::exp(
            -juce::MathConstants<float>::twoPi * tone
            / static_cast<float>(sampleRate));
        frame.stereoBloom = stereoBloomSmoothed.getNextValue();
        frame.mix = mixSmoothed.getNextValue();
        frame.output = outputSmoothed.getNextValue();
    }

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        dryBuffer.copyFrom(channel, 0, buffer, channel, 0, sampleCount);

    juce::dsp::AudioBlock<float> inputBlock(buffer);
    auto upsampled = oversampling->processSamplesUp(inputBlock);
    const auto processingSampleRate =
        sampleRate * static_cast<double>(oversamplingFactor);
    const auto dcRetention = std::exp(
        -juce::MathConstants<float>::twoPi * 18.0f
        / static_cast<float>(processingSampleRate));

    for (int sample = 0; sample < sampleCount; ++sample)
    {
        const auto& frame = parameterFrames[static_cast<size_t>(sample)];
        for (int phase = 0; phase < oversamplingFactor; ++phase)
        {
            const auto oversampledSample = sample * oversamplingFactor + phase;
            float detector = 0.0f;
            for (int channel = 0; channel < channelCount; ++channel)
                detector = juce::jmax(
                    detector,
                    std::abs(upsampled.getSample(channel, oversampledSample)));
            const auto envelopeCoefficient =
                detector > linkedEnvelope ? frame.attackCoefficient
                                          : frame.releaseCoefficient;
            linkedEnvelope = envelopeCoefficient * linkedEnvelope
                             + (1.0f - envelopeCoefficient) * detector;
            const auto threshold = frame.dynamicThreshold;
            const auto active = linkedEnvelope > threshold
                ? (linkedEnvelope - threshold) / juce::jmax(0.02f, 1.0f - threshold)
                : 0.0f;
            const auto dynamicDrive = juce::Decibels::decibelsToGain(
                9.0f * frame.dynamics * juce::jlimit(0.0f, 1.0f, active));
            const auto opening = juce::jlimit(
                0.08f, 1.0f,
                frame.dynamics >= 0.0f
                    ? 0.16f + active * (0.84f * (0.35f + 0.65f * frame.dynamics))
                    : 1.0f - active * (0.78f * std::abs(frame.dynamics)));

            for (int channel = 0; channel < channelCount; ++channel)
            {
                auto sampleValue =
                    upsampled.getSample(channel, oversampledSample);
                sampleValue *= frame.driveGain * dynamicDrive;

                float folded = 0.0f;
                float weightSum = 0.0f;
                for (int characterIndex = 0; characterIndex < characterCount;
                     ++characterIndex)
                {
                    const auto characterWeight =
                        frame.characterWeights[static_cast<size_t>(characterIndex)];
                    if (characterWeight <= 1.0e-5f)
                        continue;
                    for (int foldIndex = 0; foldIndex < foldCount; ++foldIndex)
                    {
                        const auto foldWeight =
                            frame.foldWeights[static_cast<size_t>(foldIndex)];
                        if (foldWeight <= 1.0e-5f)
                            continue;
                        const auto weight = characterWeight * foldWeight;
                        folded += weight * processCharacter(
                            characterIndex, sampleValue, foldIndex + 1,
                            frame.symmetry, frame.shape, opening);
                        weightSum += weight;
                    }
                }
                if (weightSum > 1.0e-6f)
                    folded /= weightSum;

                auto& x1 = dcInputState[static_cast<size_t>(channel)];
                auto& y1 = dcOutputState[static_cast<size_t>(channel)];
                auto dcBlocked = folded - x1 + dcRetention * y1;
                x1 = folded;
                y1 = std::isfinite(dcBlocked) ? dcBlocked : 0.0f;
                if (!std::isfinite(y1))
                    y1 = 0.0f;
                upsampled.setSample(channel, oversampledSample, y1);
            }
        }
    }

    oversampling->processSamplesDown(inputBlock);

    const auto focusCoefficient = std::exp(
        -juce::MathConstants<float>::twoPi * 220.0f
        / static_cast<float>(sampleRate));
    double blockInputEnergy = 0.0;
    double blockWetEnergy = 0.0;
    float blockActivity = 0.0f;

    for (int sample = 0; sample < sampleCount; ++sample)
    {
        const auto& frame = parameterFrames[static_cast<size_t>(sample)];
        std::array<float, 2> toned {};
        for (int channel = 0; channel < channelCount; ++channel)
        {
            auto wet = buffer.getSample(channel, sample);
            auto& wetTone = toneState[static_cast<size_t>(channel)];
            wetTone = frame.toneCoefficient * wetTone
                + (1.0f - frame.toneCoefficient) * wet;
            toned[static_cast<size_t>(channel)] =
                std::isfinite(wetTone) ? wetTone : 0.0f;
            if (!std::isfinite(wetTone))
                wetTone = 0.0f;
        }

        std::array<float, 2> wetFrame {};
        if (channelCount == 1)
        {
            wetFrame[0] = toned[0];
        }
        else
        {
            std::array<float, 2> low {};
            std::array<float, 2> high {};
            for (int channel = 0; channel < 2; ++channel)
            {
                auto& focus = focusState[static_cast<size_t>(channel)];
                focus = focusCoefficient * focus
                    + (1.0f - focusCoefficient) * toned[static_cast<size_t>(channel)];
                low[static_cast<size_t>(channel)] = focus;
                high[static_cast<size_t>(channel)] =
                    toned[static_cast<size_t>(channel)] - focus;
            }

            const auto lowMid = 0.5f * (low[0] + low[1]);
            const auto highMid = 0.5f * (high[0] + high[1]);
            auto trueSide = 0.5f * (high[0] - high[1]);
            auto complementary = 0.18f * frame.stereoBloom
                * (petalFold(highMid * (0.8f + 0.2f * frame.shape), 1,
                             frame.symmetry * 0.35f, frame.shape * 0.65f)
                   - prismFold(highMid * (0.8f + 0.2f * frame.shape), 1,
                               -frame.symmetry * 0.25f, frame.shape * 0.45f));
            auto& bloomLow = bloomLowState[0];
            bloomLow = focusCoefficient * bloomLow
                + (1.0f - focusCoefficient) * complementary;
            complementary -= bloomLow;
            const auto side = trueSide * (1.0f + 0.85f * frame.stereoBloom)
                + complementary;
            wetFrame = { lowMid + highMid + side, lowMid + highMid - side };
        }

        for (int channel = 0; channel < channelCount; ++channel)
        {
            const auto index = static_cast<size_t>(juce::jmin(channel, 1));
            auto& delayedDry =
                dryLatencyBuffer[index][static_cast<size_t>(dryLatencyPosition)];
            const auto dry = delayedDry;
            delayedDry = dryBuffer.getSample(channel, sample);
            dryBuffer.setSample(channel, sample, dry);
            blockInputEnergy += static_cast<double>(dry) * dry;
            const auto wet = wetFrame[static_cast<size_t>(channel)];
            blockWetEnergy += static_cast<double>(wet) * wet;
            blockActivity = juce::jmax(
                blockActivity, std::abs(wet - dry) / (0.08f + std::abs(dry)));
            buffer.setSample(channel, sample, wet);
        }
        if (++dryLatencyPosition >= juce::jmax(1, oversamplingLatency))
            dryLatencyPosition = 0;
    }

    const auto measuredSamples = juce::jmax(1, sampleCount * channelCount);
    const auto detectorRetention = std::exp(
        -static_cast<float>(sampleCount) / static_cast<float>(sampleRate * 0.12));
    inputMeanSquare = detectorRetention * inputMeanSquare
        + (1.0f - detectorRetention)
              * static_cast<float>(blockInputEnergy / measuredSamples);
    wetMeanSquare = detectorRetention * wetMeanSquare
        + (1.0f - detectorRetention)
              * static_cast<float>(blockWetEnergy / measuredSamples);

    auto targetCompensation = 1.0f;
    if (wetMeanSquare > inputMeanSquare * 1.10f)
        targetCompensation = juce::jlimit(
            0.10f, 1.0f, 0.92f * std::sqrt(inputMeanSquare / wetMeanSquare));

    const auto compensationAttack = std::exp(
        -1.0f / static_cast<float>(sampleRate * 0.02));
    const auto compensationRelease = std::exp(
        -1.0f / static_cast<float>(sampleRate * 0.20));
    for (int sample = 0; sample < sampleCount; ++sample)
    {
        const auto& frame = parameterFrames[static_cast<size_t>(sample)];
        const auto mixValue = frame.mix;
        const auto dryGain = mixValue <= 1.0e-6f
            ? 1.0f
            : std::cos(mixValue * juce::MathConstants<float>::halfPi);
        const auto wetGain = mixValue <= 1.0e-6f
            ? 0.0f
            : std::sin(mixValue * juce::MathConstants<float>::halfPi);
        const auto compensationCoefficient =
            targetCompensation < compensationGain ? compensationAttack
                                                  : compensationRelease;
        compensationGain = compensationCoefficient * compensationGain
                           + (1.0f - compensationCoefficient) * targetCompensation;

        for (int channel = 0; channel < channelCount; ++channel)
        {
            const auto dry = dryBuffer.getSample(channel, sample);
            const auto wet = buffer.getSample(channel, sample) * compensationGain;
            buffer.setSample(channel, sample,
                             frame.output * (dryGain * dry + wetGain * wet));
        }
    }

    const auto activity = juce::jlimit(
        0.0f, 1.0f,
        0.45f * juce::jlimit(0.0f, 1.0f, linkedEnvelope)
            + 0.55f * std::tanh(blockActivity * 0.85f));
    activityMeter.store(activity, std::memory_order_relaxed);
}
} // namespace megadsp
