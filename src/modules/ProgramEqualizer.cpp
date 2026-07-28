#include "ProgramEqualizer.h"

#include "DspHelpers.h"
#include "DspSafety.h"

#include <cmath>

namespace megadsp
{
namespace
{
// Classic passive program-equalizer stepped frequency sets, shared by the
// low boost and low attenuation networks and independently selected for the
// high boost and high attenuation networks.
constexpr std::array<float, ProgramEqualizerModule::lowFrequencyCount>
    lowFrequencies { 20.0f, 30.0f, 60.0f, 100.0f };
constexpr std::array<float, ProgramEqualizerModule::highBoostFrequencyCount>
    highBoostFrequencies {
        3000.0f, 4000.0f, 5000.0f, 8000.0f, 10000.0f, 12000.0f, 16000.0f
    };
constexpr std::array<
    float, ProgramEqualizerModule::highAttenuationFrequencyCount>
    highAttenuationFrequencies { 5000.0f, 10000.0f, 20000.0f };

// The low boost is a broad shelf; the low attenuation is a narrower bell at
// the same selected frequency. Because the two curves have different
// shapes, dialing boost and attenuation together at the same frequency
// leaves a resonant hump below the corner and a scooped dip near it instead
// of cancelling flat, matching the passive-network interaction of a classic
// program equalizer.
constexpr float lowShelfQ = 0.85f;
constexpr float lowAttenuationQ = 1.35f;
constexpr float highAttenuationShelfQ = 0.7f;
constexpr float minimumHighBoostQ = 0.5f;
constexpr float maximumHighBoostQ = 4.0f;
// Proportional-Q: the high boost narrows as more gain is dialed in, a
// common trait of musical analog boost networks.
constexpr float proportionalQAmount = 0.65f;

// Fixed passive-network insertion loss recovered by the makeup amplifier,
// plus a small additional loss as more of the network is engaged.
constexpr float baseInsertionLossDb = -3.0f;
constexpr float engagementLossScale = -0.03f;
constexpr float makeupCompensationDb = 3.0f;
// Small even-harmonic asymmetry bias standing in for transformer coloration.
constexpr float transformerAsymmetry = 0.05f;
constexpr float smoothingSeconds = 0.02f;
constexpr float crossfadeSeconds = 0.025f;

float safeFilterFrequency(double rate, float frequency)
{
    return juce::jlimit(
        5.0f, static_cast<float>(rate) * 0.45f, frequency);
}
} // namespace

float ProgramEqualizerModule::Biquad::process(float input) noexcept
{
    const auto output = b0 * input + z1;
    z1 = b1 * input - a1 * output + z2;
    z2 = b2 * input - a2 * output;
    if (std::isfinite(output) && std::isfinite(z1) && std::isfinite(z2))
        return output;
    reset();
    return 0.0f;
}

void ProgramEqualizerModule::Biquad::setLowShelf(
    double rate, float frequency, float q, float gainDb)
{
    frequency = safeFilterFrequency(rate, frequency);
    const auto a = std::pow(10.0f, gainDb / 40.0f);
    const auto omega = juce::MathConstants<float>::twoPi * frequency
                       / static_cast<float>(rate);
    const auto cosine = std::cos(omega);
    const auto alpha = std::sin(omega) / (2.0f * q);
    const auto rootA = std::sqrt(a);
    const auto twoRootAAlpha = 2.0f * rootA * alpha;
    const auto a0 = (a + 1.0f) + (a - 1.0f) * cosine + twoRootAAlpha;
    b0 = a * ((a + 1.0f) - (a - 1.0f) * cosine + twoRootAAlpha) / a0;
    b1 = 2.0f * a * ((a - 1.0f) - (a + 1.0f) * cosine) / a0;
    b2 = a * ((a + 1.0f) - (a - 1.0f) * cosine - twoRootAAlpha) / a0;
    a1 = -2.0f * ((a - 1.0f) + (a + 1.0f) * cosine) / a0;
    a2 = ((a + 1.0f) + (a - 1.0f) * cosine - twoRootAAlpha) / a0;
}

void ProgramEqualizerModule::Biquad::setPeak(
    double rate, float frequency, float q, float gainDb)
{
    frequency = safeFilterFrequency(rate, frequency);
    const auto a = std::pow(10.0f, gainDb / 40.0f);
    const auto omega = juce::MathConstants<float>::twoPi * frequency
                       / static_cast<float>(rate);
    const auto alpha = std::sin(omega) / (2.0f * q);
    const auto cosOmega = std::cos(omega);
    const auto a0 = 1.0f + alpha / a;
    b0 = (1.0f + alpha * a) / a0;
    b1 = (-2.0f * cosOmega) / a0;
    b2 = (1.0f - alpha * a) / a0;
    a1 = (-2.0f * cosOmega) / a0;
    a2 = (1.0f - alpha / a) / a0;
}

void ProgramEqualizerModule::Biquad::setHighShelf(
    double rate, float frequency, float q, float gainDb)
{
    frequency = safeFilterFrequency(rate, frequency);
    const auto a = std::pow(10.0f, gainDb / 40.0f);
    const auto omega = juce::MathConstants<float>::twoPi * frequency
                       / static_cast<float>(rate);
    const auto cosine = std::cos(omega);
    const auto alpha = std::sin(omega) / (2.0f * q);
    const auto rootA = std::sqrt(a);
    const auto twoRootAAlpha = 2.0f * rootA * alpha;
    const auto a0 = (a + 1.0f) - (a - 1.0f) * cosine + twoRootAAlpha;
    b0 = a * ((a + 1.0f) + (a - 1.0f) * cosine + twoRootAAlpha) / a0;
    b1 = -2.0f * a * ((a - 1.0f) + (a + 1.0f) * cosine) / a0;
    b2 = a * ((a + 1.0f) + (a - 1.0f) * cosine - twoRootAAlpha) / a0;
    a1 = 2.0f * ((a - 1.0f) - (a + 1.0f) * cosine) / a0;
    a2 = ((a + 1.0f) - (a - 1.0f) * cosine - twoRootAAlpha) / a0;
}

void ProgramEqualizerModule::Biquad::reset() noexcept
{
    z1 = 0.0f;
    z2 = 0.0f;
}

void ProgramEqualizerModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    const auto channelCount = static_cast<size_t>(juce::jmax(1u, spec.numChannels));
    oversampling = std::make_unique<juce::dsp::Oversampling<float>>(
        channelCount, 1,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    oversampling->initProcessing(
        static_cast<size_t>(juce::jmax(1u, spec.maximumBlockSize)));
    processingLatency = juce::jmax(
        1, juce::roundToInt(oversampling->getLatencyInSamples()));

    lowBoostSmoothed.reset(sampleRate, smoothingSeconds);
    lowAttenuationSmoothed.reset(sampleRate, smoothingSeconds);
    highBoostSmoothed.reset(sampleRate, smoothingSeconds);
    bandwidthSmoothed.reset(sampleRate, smoothingSeconds);
    highAttenuationSmoothed.reset(sampleRate, smoothingSeconds);
    driveSmoothed.reset(sampleRate, smoothingSeconds);
    outputSmoothed.reset(sampleRate, smoothingSeconds);
    for (auto& weight : lowFrequencyMix)
        weight.reset(sampleRate, crossfadeSeconds);
    for (auto& weight : highBoostFrequencyMix)
        weight.reset(sampleRate, crossfadeSeconds);
    for (auto& weight : highAttenuationFrequencyMix)
        weight.reset(sampleRate, crossfadeSeconds);

    reset();
}

void ProgramEqualizerModule::reset()
{
    for (auto& channel : lowBoostFilters)
        for (auto& filter : channel)
            filter.reset();
    for (auto& channel : lowAttenuationFilters)
        for (auto& filter : channel)
            filter.reset();
    for (auto& channel : highBoostFilters)
        for (auto& filter : channel)
            filter.reset();
    for (auto& channel : highAttenuationFilters)
        for (auto& filter : channel)
            filter.reset();
    if (oversampling != nullptr)
        oversampling->reset();
    transformerDcState = {};
    transformerDcInput = {};
    telemetryState = {};
    telemetry.clear();
    initialized = false;
    crossfadeInitialized = false;
}

void ProgramEqualizerModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    if (oversampling == nullptr)
        return;
    const auto channels = juce::jlimit(0, 2, buffer.getNumChannels());
    const auto numSamples = buffer.getNumSamples();
    if (channels <= 0 || numSamples <= 0)
        return;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < numSamples; ++sample)
            buffer.setSample(
                channel, sample,
                detail::finiteSample(buffer.getSample(channel, sample)));

    const auto normalized = [&controls](int index, float fallback)
    {
        return detail::normalizedControl(
            controls[static_cast<size_t>(index)], fallback);
    };

    const auto lowFrequencyIndex = discreteIndex(
        normalized(lowFrequencyControl, discreteValue(1, lowFrequencyCount)),
        lowFrequencyCount);
    const auto highBoostFrequencyIndex = discreteIndex(
        normalized(
            highBoostFrequencyControl, discreteValue(3, highBoostFrequencyCount)),
        highBoostFrequencyCount);
    const auto highAttenuationFrequencyIndex = discreteIndex(
        normalized(
            highAttenuationFrequencyControl,
            discreteValue(1, highAttenuationFrequencyCount)),
        highAttenuationFrequencyCount);

    const auto lowBoostDbTarget =
        detail::lerp(0.0f, 14.0f, normalized(lowBoostControl, 0.0f));
    const auto lowAttenuationDbTarget =
        detail::lerp(0.0f, 18.0f, normalized(lowAttenuationControl, 0.0f));
    const auto highBoostDbTarget =
        detail::lerp(0.0f, 18.0f, normalized(highBoostControl, 0.0f));
    const auto bandwidthTarget = normalized(bandwidthControl, 0.45f);
    const auto highAttenuationDbTarget =
        detail::lerp(0.0f, 18.0f, normalized(highAttenuationControl, 0.0f));
    const auto driveDbTarget =
        detail::lerp(-12.0f, 18.0f, normalized(driveControl, 0.4f));
    const auto outputDbTarget =
        detail::lerp(-18.0f, 12.0f, normalized(outputControl, 0.6f));
    const auto outputGainTarget =
        juce::Decibels::decibelsToGain(outputDbTarget);

    lowBoostSmoothed.setTargetValue(lowBoostDbTarget);
    lowAttenuationSmoothed.setTargetValue(lowAttenuationDbTarget);
    highBoostSmoothed.setTargetValue(highBoostDbTarget);
    bandwidthSmoothed.setTargetValue(bandwidthTarget);
    highAttenuationSmoothed.setTargetValue(highAttenuationDbTarget);
    driveSmoothed.setTargetValue(driveDbTarget);
    outputSmoothed.setTargetValue(outputGainTarget);

    for (int index = 0; index < lowFrequencyCount; ++index)
    {
        auto& weight = lowFrequencyMix[static_cast<size_t>(index)];
        const auto target = index == lowFrequencyIndex ? 1.0f : 0.0f;
        if (!crossfadeInitialized)
            weight.setCurrentAndTargetValue(target);
        else
            weight.setTargetValue(target);
    }
    for (int index = 0; index < highBoostFrequencyCount; ++index)
    {
        auto& weight = highBoostFrequencyMix[static_cast<size_t>(index)];
        const auto target = index == highBoostFrequencyIndex ? 1.0f : 0.0f;
        if (!crossfadeInitialized)
            weight.setCurrentAndTargetValue(target);
        else
            weight.setTargetValue(target);
    }
    for (int index = 0; index < highAttenuationFrequencyCount; ++index)
    {
        auto& weight = highAttenuationFrequencyMix[static_cast<size_t>(index)];
        const auto target = index == highAttenuationFrequencyIndex ? 1.0f : 0.0f;
        if (!crossfadeInitialized)
            weight.setCurrentAndTargetValue(target);
        else
            weight.setTargetValue(target);
    }
    if (!initialized)
    {
        lowBoostSmoothed.setCurrentAndTargetValue(lowBoostDbTarget);
        lowAttenuationSmoothed.setCurrentAndTargetValue(lowAttenuationDbTarget);
        highBoostSmoothed.setCurrentAndTargetValue(highBoostDbTarget);
        bandwidthSmoothed.setCurrentAndTargetValue(bandwidthTarget);
        highAttenuationSmoothed.setCurrentAndTargetValue(highAttenuationDbTarget);
        driveSmoothed.setCurrentAndTargetValue(driveDbTarget);
        outputSmoothed.setCurrentAndTargetValue(outputGainTarget);
        initialized = true;
    }
    crossfadeInitialized = true;

    // Coefficients are recomputed once per block from the smoothed control
    // values; the crossfade weights and output gain remain smoothed at
    // sample or block rate below to stay click-safe between blocks.
    const auto lowBoostDbBlock = lowBoostSmoothed.getCurrentValue();
    const auto lowAttenuationDbBlock = lowAttenuationSmoothed.getCurrentValue();
    const auto highBoostDbBlock = highBoostSmoothed.getCurrentValue();
    const auto bandwidthBlock = bandwidthSmoothed.getCurrentValue();
    const auto highAttenuationDbBlock = highAttenuationSmoothed.getCurrentValue();

    const auto highBoostBaseQ = detail::exponential(
        minimumHighBoostQ, maximumHighBoostQ, bandwidthBlock);
    const auto highBoostQ = highBoostBaseQ
        * (1.0f + proportionalQAmount * (highBoostDbBlock / 18.0f));

    for (int channel = 0; channel < channels; ++channel)
    {
        const auto ch = static_cast<size_t>(channel);
        for (int index = 0; index < lowFrequencyCount; ++index)
        {
            const auto frequency = lowFrequencies[static_cast<size_t>(index)];
            lowBoostFilters[ch][static_cast<size_t>(index)].setLowShelf(
                sampleRate, frequency, lowShelfQ, lowBoostDbBlock);
            lowAttenuationFilters[ch][static_cast<size_t>(index)].setPeak(
                sampleRate, frequency, lowAttenuationQ, -lowAttenuationDbBlock);
        }
        for (int index = 0; index < highBoostFrequencyCount; ++index)
            highBoostFilters[ch][static_cast<size_t>(index)].setPeak(
                sampleRate,
                highBoostFrequencies[static_cast<size_t>(index)],
                highBoostQ, highBoostDbBlock);
        for (int index = 0; index < highAttenuationFrequencyCount; ++index)
            highAttenuationFilters[ch][static_cast<size_t>(index)].setHighShelf(
                sampleRate,
                highAttenuationFrequencies[static_cast<size_t>(index)],
                highAttenuationShelfQ, -highAttenuationDbBlock);
    }

    const auto totalEngagementDb = lowBoostDbBlock + lowAttenuationDbBlock
        + highBoostDbBlock + highAttenuationDbBlock;
    const auto insertionLossGain = juce::Decibels::decibelsToGain(
        baseInsertionLossDb + engagementLossScale * totalEngagementDb);

    float lowInteractionPeak = 0.0f;
    float highInteractionPeak = 0.0f;
    for (int sample = 0; sample < numSamples; ++sample)
    {
        std::array<float, lowFrequencyCount> lowMix {};
        for (int index = 0; index < lowFrequencyCount; ++index)
            lowMix[static_cast<size_t>(index)] =
                lowFrequencyMix[static_cast<size_t>(index)].getNextValue();
        std::array<float, highBoostFrequencyCount> highBoostMix {};
        for (int index = 0; index < highBoostFrequencyCount; ++index)
            highBoostMix[static_cast<size_t>(index)] =
                highBoostFrequencyMix[static_cast<size_t>(index)].getNextValue();
        std::array<float, highAttenuationFrequencyCount> highAttenuationMixValues {};
        for (int index = 0; index < highAttenuationFrequencyCount; ++index)
            highAttenuationMixValues[static_cast<size_t>(index)] =
                highAttenuationFrequencyMix[static_cast<size_t>(index)]
                    .getNextValue();

        for (int channel = 0; channel < channels; ++channel)
        {
            const auto ch = static_cast<size_t>(channel);
            const auto x = buffer.getSample(channel, sample);

            auto lowBoosted = 0.0f;
            for (int index = 0; index < lowFrequencyCount; ++index)
                lowBoosted +=
                    lowBoostFilters[ch][static_cast<size_t>(index)].process(x)
                    * lowMix[static_cast<size_t>(index)];

            auto lowShaped = 0.0f;
            for (int index = 0; index < lowFrequencyCount; ++index)
                lowShaped += lowAttenuationFilters[ch][static_cast<size_t>(index)]
                                 .process(lowBoosted)
                             * lowMix[static_cast<size_t>(index)];

            auto highBoosted = 0.0f;
            for (int index = 0; index < highBoostFrequencyCount; ++index)
                highBoosted +=
                    highBoostFilters[ch][static_cast<size_t>(index)].process(
                        lowShaped)
                    * highBoostMix[static_cast<size_t>(index)];

            auto highShaped = 0.0f;
            for (int index = 0; index < highAttenuationFrequencyCount; ++index)
                highShaped +=
                    highAttenuationFilters[ch][static_cast<size_t>(index)]
                        .process(highBoosted)
                    * highAttenuationMixValues[static_cast<size_t>(index)];

            lowInteractionPeak = juce::jmax(
                lowInteractionPeak, std::abs(lowBoosted - lowShaped));
            highInteractionPeak = juce::jmax(
                highInteractionPeak, std::abs(highBoosted - highShaped));

            buffer.setSample(
                channel, sample,
                detail::finiteSample(highShaped) * insertionLossGain);
        }
    }

    // Bounded, fixed-latency oversampled makeup amplifier and transformer
    // coloration stage that recovers the passive network's insertion loss.
    const auto driveDbBlock = driveSmoothed.skip(numSamples);
    const auto outputGainBlock = outputSmoothed.skip(numSamples);
    const auto driveGainLinear = juce::Decibels::decibelsToGain(driveDbBlock);
    const auto driveNormalized =
        juce::jlimit(0.0f, 1.0f, (driveDbBlock + 12.0f) / 30.0f);
    const auto biasAmount = transformerAsymmetry * driveNormalized;
    const auto biasNull = std::tanh(biasAmount);
    const auto driveMakeup = 1.0f / juce::jmax(1.0f, driveGainLinear);
    const auto makeupCompensationGain =
        juce::Decibels::decibelsToGain(makeupCompensationDb);

    juce::dsp::AudioBlock<float> block(buffer);
    auto upsampled = oversampling->processSamplesUp(block);
    const auto processingRate = sampleRate
        * static_cast<double>(upsampled.getNumSamples())
        / static_cast<double>(juce::jmax(1, numSamples));
    const auto dcCoefficient = std::exp(
        -juce::MathConstants<float>::twoPi * 6.0f
        / static_cast<float>(processingRate));
    double distortionEnergy = 0.0;
    for (size_t channel = 0; channel < upsampled.getNumChannels(); ++channel)
    {
        auto* samples = upsampled.getChannelPointer(channel);
        const auto index =
            static_cast<size_t>(juce::jmin(static_cast<int>(channel), 1));
        auto& dcState = transformerDcState[index];
        auto& dcInput = transformerDcInput[index];
        for (size_t sample = 0; sample < upsampled.getNumSamples(); ++sample)
        {
            const auto x = samples[sample];
            const auto driven = x * driveGainLinear;
            const auto nonlinear = std::tanh(driven + biasAmount) - biasNull;
            const auto coloured =
                nonlinear * driveMakeup * makeupCompensationGain;
            dcState = dcCoefficient * (dcState + coloured - dcInput);
            dcInput = coloured;
            const auto output = detail::finiteSample(dcState);
            const auto delta = static_cast<double>(output - x);
            distortionEnergy += delta * delta;
            samples[sample] = output;
        }
    }
    oversampling->processSamplesDown(block);
    buffer.applyGain(outputGainBlock);

    if (environment.captureTelemetry)
    {
        telemetryState.sequence += 1;
        telemetryState.valueCount = telemetryValueCount;
        const auto sampleCount =
            upsampled.getNumChannels() * upsampled.getNumSamples();
        const auto driveRms = static_cast<float>(std::sqrt(
            distortionEnergy
            / static_cast<double>(sampleCount > 0 ? sampleCount : 1)));
        const auto makeupGainDbCurrent = makeupCompensationDb
            + juce::Decibels::gainToDecibels(driveMakeup, -60.0f);
        telemetryState.values[driveActivity] = driveRms;
        telemetryState.values[lowInteractionActivity] = lowInteractionPeak;
        telemetryState.values[highInteractionActivity] = highInteractionPeak;
        telemetryState.values[makeupGainDb] = makeupGainDbCurrent;
        appendContinuousTelemetryHistory(
            telemetryState,
            { driveRms, lowInteractionPeak, highInteractionPeak, 0.0f },
            telemetryHistoryCount);
        telemetry.publish(telemetryState);
    }
}

bool ProgramEqualizerModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}
} // namespace megadsp
