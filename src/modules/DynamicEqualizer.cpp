#include "DynamicEqualizer.h"
#include "DspHelpers.h"

#include <cmath>

namespace megadsp
{
using detail::coefficient;
using detail::exponential;
using detail::lerp;

void DynamicEqualizerModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    frequencySmoothed.reset(sampleRate, 0.03);
    qSmoothed.reset(sampleRate, 0.03);
    detectorMix.reset(sampleRate, 0.01);
    externalMix.reset(sampleRate, 0.01);
    listenMix.reset(sampleRate, 0.01);
    stereoLink.reset(sampleRate, 0.02);
    for (auto& mix : shapeMix)
        mix.reset(sampleRate, 0.02);
    reset();
}

void DynamicEqualizerModule::reset()
{
    for (auto& channel : programFilters)
        for (auto& filter : channel)
            filter.reset();
    for (auto& channel : detectorFilters)
        for (auto& filter : channel)
            filter.reset();
    rmsPower.fill(0.0f);
    gainDbState.fill(0.0f);
    frequencySmoothed.setCurrentAndTargetValue(0.0f);
    qSmoothed.setCurrentAndTargetValue(0.0f);
    detectorMix.setCurrentAndTargetValue(1.0f);
    externalMix.setCurrentAndTargetValue(0.0f);
    listenMix.setCurrentAndTargetValue(0.0f);
    stereoLink.setCurrentAndTargetValue(1.0f);
    for (int shape = 0; shape < 3; ++shape)
        shapeMix[static_cast<size_t>(shape)].setCurrentAndTargetValue(
            shape == 0 ? 1.0f : 0.0f);
    initialized = false;
    activeShape = 0;
    dynamicGainDb.store(0.0f);
    detectorLevelDb.store(-100.0f);
}

float DynamicEqualizerModule::Biquad::process(float input)
{
    if (!std::isfinite(input))
        input = 0.0f;
    const auto output = b0 * input + z1;
    z1 = b1 * input - a1 * output + z2;
    z2 = b2 * input - a2 * output;
    if (!std::isfinite(output) || !std::isfinite(z1) || !std::isfinite(z2))
    {
        reset();
        return 0.0f;
    }
    return output;
}

void DynamicEqualizerModule::Biquad::setDynamic(
    double rate, float frequency, float q, float gainDb, int shape)
{
    const auto omega = juce::MathConstants<float>::twoPi * frequency
                       / static_cast<float>(rate);
    const auto cosine = std::cos(omega);
    const auto alpha = std::sin(omega) / (2.0f * juce::jmax(0.05f, q));
    const auto a = std::pow(10.0f, gainDb / 40.0f);
    float denominator = 1.0f;
    if (shape == 0)
    {
        denominator = 1.0f + alpha / a;
        b0 = (1.0f + alpha * a) / denominator;
        b1 = -2.0f * cosine / denominator;
        b2 = (1.0f - alpha * a) / denominator;
        a1 = -2.0f * cosine / denominator;
        a2 = (1.0f - alpha / a) / denominator;
        return;
    }

    const auto rootA = std::sqrt(a);
    const auto term = 2.0f * rootA * alpha;
    if (shape == 1)
    {
        denominator = (a + 1.0f) + (a - 1.0f) * cosine + term;
        b0 = a * ((a + 1.0f) - (a - 1.0f) * cosine + term) / denominator;
        b1 = 2.0f * a * ((a - 1.0f) - (a + 1.0f) * cosine)
             / denominator;
        b2 = a * ((a + 1.0f) - (a - 1.0f) * cosine - term) / denominator;
        a1 = -2.0f * ((a - 1.0f) + (a + 1.0f) * cosine)
             / denominator;
        a2 = ((a + 1.0f) + (a - 1.0f) * cosine - term) / denominator;
        return;
    }

    denominator = (a + 1.0f) - (a - 1.0f) * cosine + term;
    b0 = a * ((a + 1.0f) + (a - 1.0f) * cosine + term) / denominator;
    b1 = -2.0f * a * ((a - 1.0f) + (a + 1.0f) * cosine)
         / denominator;
    b2 = a * ((a + 1.0f) + (a - 1.0f) * cosine - term) / denominator;
    a1 = 2.0f * ((a - 1.0f) - (a + 1.0f) * cosine)
         / denominator;
    a2 = ((a + 1.0f) - (a - 1.0f) * cosine - term) / denominator;
}

void DynamicEqualizerModule::Biquad::setDetector(
    double rate, float frequency, float q, int shape)
{
    const auto omega = juce::MathConstants<float>::twoPi * frequency
                       / static_cast<float>(rate);
    const auto cosine = std::cos(omega);
    const auto detectorQ = shape == 0 ? q : juce::jlimit(0.5f, 1.0f, q);
    const auto alpha = std::sin(omega) / (2.0f * detectorQ);
    const auto denominator = 1.0f + alpha;
    if (shape == 0)
    {
        b0 = alpha / denominator;
        b1 = 0.0f;
        b2 = -alpha / denominator;
    }
    else if (shape == 1)
    {
        b0 = (1.0f - cosine) * 0.5f / denominator;
        b1 = (1.0f - cosine) / denominator;
        b2 = b0;
    }
    else
    {
        b0 = (1.0f + cosine) * 0.5f / denominator;
        b1 = -(1.0f + cosine) / denominator;
        b2 = b0;
    }
    a1 = -2.0f * cosine / denominator;
    a2 = (1.0f - alpha) / denominator;
}

void DynamicEqualizerModule::Biquad::reset()
{
    z1 = 0.0f;
    z2 = 0.0f;
}

void DynamicEqualizerModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    const auto channelCount = juce::jmin(2, buffer.getNumChannels());
    if (channelCount <= 0)
        return;

    const auto selectedShape = discreteIndex(controls[7], 3);
    const auto useExternal = controls[9] >= 0.5f
        && environment.sidechain != nullptr
        && environment.sidechain->getNumChannels() > 0
        && environment.sidechain->getNumSamples() >= buffer.getNumSamples();
    const auto detectorTarget =
        discreteIndex(controls[8], 2) == 1 ? 1.0f : 0.0f;
    if (!initialized)
    {
        frequencySmoothed.setCurrentAndTargetValue(controls[0]);
        qSmoothed.setCurrentAndTargetValue(controls[1]);
        detectorMix.setCurrentAndTargetValue(detectorTarget);
        externalMix.setCurrentAndTargetValue(useExternal ? 1.0f : 0.0f);
        listenMix.setCurrentAndTargetValue(controls[10] >= 0.5f ? 1.0f : 0.0f);
        stereoLink.setCurrentAndTargetValue(controls[11]);
        for (int shape = 0; shape < 3; ++shape)
            shapeMix[static_cast<size_t>(shape)].setCurrentAndTargetValue(
                shape == selectedShape ? 1.0f : 0.0f);
        initialized = true;
    }
    else
    {
        if (selectedShape != activeShape)
        {
            for (int channel = 0; channel < channelCount; ++channel)
            {
                programFilters[static_cast<size_t>(channel)][
                    static_cast<size_t>(selectedShape)].reset();
                detectorFilters[static_cast<size_t>(channel)][
                    static_cast<size_t>(selectedShape)].reset();
            }
        }
        frequencySmoothed.setTargetValue(controls[0]);
        qSmoothed.setTargetValue(controls[1]);
        detectorMix.setTargetValue(detectorTarget);
        externalMix.setTargetValue(useExternal ? 1.0f : 0.0f);
        listenMix.setTargetValue(controls[10] >= 0.5f ? 1.0f : 0.0f);
        stereoLink.setTargetValue(controls[11]);
        for (int shape = 0; shape < 3; ++shape)
            shapeMix[static_cast<size_t>(shape)].setTargetValue(
                shape == selectedShape ? 1.0f : 0.0f);
    }
    activeShape = selectedShape;

    const auto rangeDb = lerp(-18.0f, 12.0f, controls[2]);
    const auto thresholdDb = lerp(-60.0f, 0.0f, controls[3]);
    const auto ratio = exponential(1.0f, 10.0f, controls[4]);
    const auto attackCoefficient = coefficient(
        sampleRate, exponential(0.1f, 100.0f, controls[5]));
    const auto releaseCoefficient = coefficient(
        sampleRate, exponential(10.0f, 1000.0f, controls[6]));
    const auto rmsCoefficient = coefficient(sampleRate, 10.0f);
    const auto nyquistLimit = static_cast<float>(
        juce::jmin(20000.0, sampleRate * 0.475));
    float maximumDynamicGain = 0.0f;
    float maximumDetector = 0.0f;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto frequency = exponential(
            20.0f, nyquistLimit, frequencySmoothed.getNextValue());
        const auto q = exponential(0.2f, 12.0f, qSmoothed.getNextValue());
        const auto rmsAmount = detectorMix.getNextValue();
        const auto externalAmount = externalMix.getNextValue();
        const auto linkAmount = stereoLink.getNextValue();
        const auto listenAmount = listenMix.getNextValue();
        std::array<float, 3> shapeAmounts {};
        for (int shape = 0; shape < 3; ++shape)
            shapeAmounts[static_cast<size_t>(shape)] =
                shapeMix[static_cast<size_t>(shape)].getNextValue();

        std::array<float, 2> dry {};
        std::array<float, 2> focused {};
        std::array<float, 2> detectorAmplitude {};
        for (int channel = 0; channel < channelCount; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            dry[index] = buffer.getSample(channel, sample);
            auto detectorInput = dry[index];
            if (environment.sidechain != nullptr
                && environment.sidechain->getNumChannels() > 0
                && environment.sidechain->getNumSamples() > sample)
            {
                const auto detectorChannel = juce::jmin(
                    channel, environment.sidechain->getNumChannels() - 1);
                const auto externalInput =
                    environment.sidechain->getSample(detectorChannel, sample);
                detectorInput +=
                    (externalInput - detectorInput) * externalAmount;
            }
            for (int shape = 0; shape < 3; ++shape)
            {
                if (shapeAmounts[static_cast<size_t>(shape)] <= 1.0e-6f)
                    continue;
                auto& filter = detectorFilters[index][static_cast<size_t>(shape)];
                filter.setDetector(sampleRate, frequency, q, shape);
                focused[index] += filter.process(detectorInput)
                                  * shapeAmounts[static_cast<size_t>(shape)];
            }
            const auto peak = std::abs(focused[index]);
            rmsPower[index] = rmsCoefficient * rmsPower[index]
                              + (1.0f - rmsCoefficient) * focused[index]
                                    * focused[index];
            const auto rms = std::sqrt(juce::jmax(0.0f, rmsPower[index]));
            detectorAmplitude[index] = peak + (rms - peak) * rmsAmount;
        }

        float linkedDetector = 0.0f;
        for (int channel = 0; channel < channelCount; ++channel)
            linkedDetector = juce::jmax(
                linkedDetector, detectorAmplitude[static_cast<size_t>(channel)]);
        maximumDetector = juce::jmax(maximumDetector, linkedDetector);

        for (int channel = 0; channel < channelCount; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            const auto detector = detectorAmplitude[index]
                                  + (linkedDetector - detectorAmplitude[index])
                                        * linkAmount;
            const auto levelDb =
                juce::Decibels::gainToDecibels(detector, -100.0f);
            const auto overDb = juce::jmax(0.0f, levelDb - thresholdDb);
            const auto driveDb = overDb * (1.0f - 1.0f / ratio);
            const auto desiredDb = std::copysign(
                juce::jmin(std::abs(rangeDb), driveDb), rangeDb);
            const auto smoothing = std::abs(desiredDb) > std::abs(gainDbState[index])
                                       ? attackCoefficient : releaseCoefficient;
            gainDbState[index] = smoothing * gainDbState[index]
                                 + (1.0f - smoothing) * desiredDb;
            maximumDynamicGain = juce::jmax(
                maximumDynamicGain, std::abs(gainDbState[index]));

            float dynamicOutput = 0.0f;
            for (int shape = 0; shape < 3; ++shape)
            {
                if (shapeAmounts[static_cast<size_t>(shape)] <= 1.0e-6f)
                    continue;
                auto& filter = programFilters[index][static_cast<size_t>(shape)];
                filter.setDynamic(sampleRate, frequency, q,
                                  gainDbState[index], shape);
                dynamicOutput += filter.process(dry[index])
                                 * shapeAmounts[static_cast<size_t>(shape)];
            }
            const auto listenOutput = juce::jlimit(-4.0f, 4.0f, focused[index]);
            const auto output = dynamicOutput
                                + (listenOutput - dynamicOutput) * listenAmount;
            buffer.setSample(channel, sample,
                             std::isfinite(output) ? output : 0.0f);
        }
    }

    dynamicGainDb.store(maximumDynamicGain, std::memory_order_relaxed);
    detectorLevelDb.store(
        juce::Decibels::gainToDecibels(maximumDetector, -100.0f),
        std::memory_order_relaxed);
}
} // namespace megadsp
