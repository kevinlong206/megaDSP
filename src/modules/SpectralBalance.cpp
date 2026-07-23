#include "SpectralBalance.h"
#include "DspHelpers.h"
#include "DspSafety.h"

#include <algorithm>
#include <cmath>

namespace megadsp
{
namespace
{
float safeMagnitude(FixedLatencyStft::Complex value) noexcept
{
    return std::isfinite(value.real()) && std::isfinite(value.imag())
        ? std::abs(value) : 0.0f;
}

float bell(float frequency, float centre, float widthOctaves) noexcept
{
    if (frequency <= 0.0f)
        return 0.0f;
    const auto distance = std::log2(frequency / centre) / widthOctaves;
    return std::exp(-0.5f * distance * distance);
}
} // namespace

void SpectralBalanceModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    stft.prepare(spec);
    sampleRate = stft.sampleRate();
    const auto bins = static_cast<std::size_t>(FixedLatencyStft::binCount);
    correctionSmoother.prepare(bins);
    currentMagnitude.resize(bins);
    previousMagnitude.resize(bins);
    trackedPower.resize(bins);
    trackedDb.resize(bins);
    broadMeasuredDb.resize(bins);
    targetDb.resize(bins);
    rawCorrectionDb.resize(bins);
    appliedCorrectionDb.resize(bins);
    powerPrefixSum.resize(bins + 1);
    trackingInitialised.resize(bins);
    outputSmoothed.reset(sampleRate, 0.03);
    reset();
}

void SpectralBalanceModule::reset()
{
    stft.reset();
    correctionSmoother.reset();
    std::fill(currentMagnitude.begin(), currentMagnitude.end(), 0.0f);
    std::fill(previousMagnitude.begin(), previousMagnitude.end(), 0.0f);
    std::fill(trackedPower.begin(), trackedPower.end(), 0.0f);
    std::fill(trackedDb.begin(), trackedDb.end(), -120.0f);
    std::fill(broadMeasuredDb.begin(), broadMeasuredDb.end(), -120.0f);
    std::fill(targetDb.begin(), targetDb.end(), 0.0f);
    std::fill(rawCorrectionDb.begin(), rawCorrectionDb.end(), 0.0f);
    std::fill(appliedCorrectionDb.begin(), appliedCorrectionDb.end(), 0.0f);
    std::fill(powerPrefixSum.begin(), powerPrefixSum.end(), 0.0);
    std::fill(trackingInitialised.begin(), trackingInitialised.end(), 0);
    for (auto& lane : telemetrySpectrum)
        lane.fill(0.0f);
    outputSmoothed.setCurrentAndTargetValue(1.0f);
    currentAmount = 0.0f;
    transientState = 0.0f;
    meanMeasuredDb = -100.0f;
    meanTargetDb = 0.0f;
    maximumCorrection = 0.0f;
    currentOutputGain = 1.0f;
    parametersInitialised = false;
    telemetrySequence = 0;
    telemetry.clear();
    correctionMeter.store(0.0f, std::memory_order_relaxed);
    measuredMeter.store(-100.0f, std::memory_order_relaxed);
}

void SpectralBalanceModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    contour = juce::jlimit(
        0, 4, static_cast<int>(
                  detail::normalizedControl(controls[0], 0.0f) * 5.0f));
    currentAmount = detail::normalizedControl(controls[1], 0.5f);
    currentLowWeightDb = detail::lerp(
        -6.0f, 6.0f, detail::normalizedControl(controls[2], 0.5f));
    currentPresenceDb = detail::lerp(
        -6.0f, 6.0f, detail::normalizedControl(controls[3], 0.5f));
    currentAirDb = detail::lerp(
        -6.0f, 6.0f, detail::normalizedControl(controls[4], 0.5f));
    currentAdaptationSeconds = detail::exponential(
        0.5f, 30.0f, detail::normalizedControl(controls[5], 0.45f));
    currentDetail = detail::normalizedControl(controls[6], 0.4f);
    currentTransientPreserve =
        detail::normalizedControl(controls[7], 0.5f);
    const auto output = juce::Decibels::decibelsToGain(detail::lerp(
        -18.0f, 12.0f, detail::normalizedControl(controls[8], 0.6f)));
    if (!parametersInitialised)
    {
        outputSmoothed.setCurrentAndTargetValue(output);
        parametersInitialised = true;
    }
    else
        outputSmoothed.setTargetValue(output);

    stft.process(
        buffer, this, processFrameCallback, outputCallback);
    publishTelemetry(environment.captureTelemetry);
}

void SpectralBalanceModule::processFrameCallback(
    void* context, FixedLatencyStft::Complex* const* spectra,
    int channels, int bins) noexcept
{
    static_cast<SpectralBalanceModule*>(context)->processFrame(
        spectra, channels, bins);
}

float SpectralBalanceModule::outputCallback(
    void* context, int channel, float dry, float wet) noexcept
{
    auto& module = *static_cast<SpectralBalanceModule*>(context);
    if (channel == 0)
        module.currentOutputGain = module.outputSmoothed.getNextValue();
    return module.outputSample(dry, wet);
}

void SpectralBalanceModule::processFrame(
    FixedLatencyStft::Complex* const* spectra, int channels, int bins) noexcept
{
    float fluxNumerator = 0.0f;
    float fluxDenominator = 1.0e-9f;
    const auto adaptationAlpha = 1.0f - std::exp(
        -static_cast<float>(FixedLatencyStft::hopSize)
        / (currentAdaptationSeconds * static_cast<float>(sampleRate)));
    const auto binHz =
        static_cast<float>(sampleRate / FixedLatencyStft::fftSize);

    for (int bin = 0; bin < bins; ++bin)
    {
        float power = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto magnitude = safeMagnitude(spectra[channel][bin]);
            power += magnitude * magnitude;
        }
        power /= static_cast<float>(juce::jmax(1, channels));
        const auto magnitude = std::sqrt(power);
        currentMagnitude[static_cast<std::size_t>(bin)] = magnitude;
        const auto previous = previousMagnitude[static_cast<std::size_t>(bin)];
        fluxNumerator += juce::jmax(0.0f, magnitude - previous);
        fluxDenominator += magnitude + previous;
        previousMagnitude[static_cast<std::size_t>(bin)] = magnitude;

        const auto normalizedPower =
            power / static_cast<float>(
                        FixedLatencyStft::fftSize
                        * FixedLatencyStft::fftSize);
        auto& tracked = trackedPower[static_cast<std::size_t>(bin)];
        auto& initialized =
            trackingInitialised[static_cast<std::size_t>(bin)];
        if (normalizedPower > 1.0e-12f && initialized == 0)
        {
            tracked = normalizedPower;
            initialized = 1;
        }
        else
            tracked += adaptationAlpha * (normalizedPower - tracked);
        if (!std::isfinite(tracked) || tracked < 0.0f)
            tracked = 0.0f;
        trackedDb[static_cast<std::size_t>(bin)] =
            juce::Decibels::gainToDecibels(
                std::sqrt(tracked), -120.0f);
    }

    const auto flux = juce::jlimit(
        0.0f, 1.0f, (fluxNumerator / fluxDenominator - 0.025f) * 7.0f);
    transientState += 0.25f * (flux - transientState);
    const auto transientScale =
        1.0f - currentTransientPreserve * transientState;
    const auto smoothingOctaves =
        detail::lerp(1.15f, 0.28f, currentDetail);
    powerPrefixSum[0] = 0.0;
    for (int bin = 0; bin < bins; ++bin)
        powerPrefixSum[static_cast<std::size_t>(bin + 1)] =
            powerPrefixSum[static_cast<std::size_t>(bin)]
            + trackedPower[static_cast<std::size_t>(bin)];

    double measuredSum = 0.0;
    double targetSum = 0.0;
    double weightSum = 0.0;
    for (int bin = 1; bin < bins; ++bin)
    {
        const auto frequency = static_cast<float>(bin) * binHz;
        const auto ratio = std::pow(2.0f, smoothingOctaves);
        const auto lowBin = juce::jmax(
            1, static_cast<int>(std::floor(
                   static_cast<float>(bin) / ratio)));
        const auto highBin = juce::jmin(
            bins - 1, static_cast<int>(std::ceil(
                          static_cast<float>(bin) * ratio)));
        const auto powerSum =
            powerPrefixSum[static_cast<std::size_t>(highBin + 1)]
            - powerPrefixSum[static_cast<std::size_t>(lowBin)];
        const auto count = highBin - lowBin + 1;
        const auto meanPower =
            static_cast<float>(powerSum / juce::jmax(1, count));
        broadMeasuredDb[static_cast<std::size_t>(bin)] =
            juce::Decibels::gainToDecibels(
                std::sqrt(juce::jmax(0.0f, meanPower)), -120.0f);
        targetDb[static_cast<std::size_t>(bin)] = targetAt(frequency);
        const auto weight = std::sqrt(juce::jmax(
            0.0f, trackedPower[static_cast<std::size_t>(bin)]));
        if (frequency >= 30.0f && frequency <= 20000.0f
            && weight > 1.0e-6f)
        {
            measuredSum +=
                broadMeasuredDb[static_cast<std::size_t>(bin)] * weight;
            targetSum += targetDb[static_cast<std::size_t>(bin)] * weight;
            weightSum += weight;
        }
    }
    const auto measuredMean = weightSum > 0.0
        ? static_cast<float>(measuredSum / weightSum) : -100.0f;
    const auto targetMean = weightSum > 0.0
        ? static_cast<float>(targetSum / weightSum) : 0.0f;
    meanMeasuredDb = measuredMean;
    meanTargetDb = targetMean;

    double correctionSum = 0.0;
    double correctionWeight = 0.0;
    for (int bin = 1; bin < bins; ++bin)
    {
        const auto frequency = static_cast<float>(bin) * binHz;
        const auto active =
            frequency >= 30.0f && frequency <= 20000.0f
            && trackedDb[static_cast<std::size_t>(bin)] > -90.0f
            && weightSum > 0.0;
        auto correction = active
            ? (targetDb[static_cast<std::size_t>(bin)] - targetMean)
                  - (broadMeasuredDb[static_cast<std::size_t>(bin)]
                     - measuredMean)
            : 0.0f;
        correction = juce::jlimit(-9.0f, 9.0f, correction);
        rawCorrectionDb[static_cast<std::size_t>(bin)] = correction;
        const auto weight = currentMagnitude[static_cast<std::size_t>(bin)];
        correctionSum += correction * weight;
        correctionWeight += weight;
    }
    const auto correctionMean = correctionWeight > 1.0e-9
        ? static_cast<float>(correctionSum / correctionWeight) : 0.0f;

    maximumCorrection = 0.0f;
    const auto correctionCoefficient = std::exp(
        -static_cast<float>(FixedLatencyStft::hopSize)
        / (0.12f * static_cast<float>(sampleRate)));
    for (int bin = 0; bin < bins; ++bin)
    {
        auto desired =
            (rawCorrectionDb[static_cast<std::size_t>(bin)] - correctionMean)
            * currentAmount * transientScale;
        if (trackedDb[static_cast<std::size_t>(bin)] <= -90.0f)
            desired = 0.0f;
        desired = juce::jlimit(-9.0f, 9.0f, desired);
        const auto correction = correctionSmoother.process(
            static_cast<std::size_t>(bin), desired,
            correctionCoefficient);
        appliedCorrectionDb[static_cast<std::size_t>(bin)] = correction;
        maximumCorrection =
            juce::jmax(maximumCorrection, std::abs(correction));
        const auto gain = juce::Decibels::decibelsToGain(correction);
        for (int channel = 0; channel < channels; ++channel)
            spectra[channel][bin] *= gain;
    }
    correctionMeter.store(
        detail::finiteSample(maximumCorrection), std::memory_order_relaxed);
    measuredMeter.store(
        detail::finiteSample(meanMeasuredDb), std::memory_order_relaxed);

    for (std::size_t point = 0;
         point < continuousTelemetryHistoryCapacity; ++point)
    {
        const auto normalized = static_cast<float>(point)
                                / static_cast<float>(
                                    continuousTelemetryHistoryCapacity - 1);
        const auto frequency = 20.0f * std::pow(1000.0f, normalized);
        const auto bin = juce::jlimit(
            0, bins - 1, static_cast<int>(std::lround(frequency / binHz)));
        telemetrySpectrum[0][point] =
            trackedDb[static_cast<std::size_t>(bin)];
        telemetrySpectrum[1][point] =
            targetDb[static_cast<std::size_t>(bin)] + meanMeasuredDb;
        telemetrySpectrum[2][point] =
            appliedCorrectionDb[static_cast<std::size_t>(bin)];
        telemetrySpectrum[3][point] =
            broadMeasuredDb[static_cast<std::size_t>(bin)];
    }
}

float SpectralBalanceModule::targetAt(float frequency) const noexcept
{
    if (frequency <= 0.0f)
        return 0.0f;
    const auto octave = std::log2(frequency / 1000.0f);
    float target = 0.0f;
    switch (contour)
    {
        case 0: // Natural
            target = -1.2f * octave;
            break;
        case 1: // Warm
            target = -1.5f * octave + 2.0f * bell(frequency, 180.0f, 1.1f);
            break;
        case 2: // Clear
            target = -0.7f * octave - 1.5f * bell(frequency, 240.0f, 1.0f)
                     + 1.5f * bell(frequency, 4200.0f, 1.2f);
            break;
        case 3: // Vocal
            target = -1.0f * octave - 2.0f * bell(frequency, 120.0f, 1.0f)
                     + 2.5f * bell(frequency, 2200.0f, 0.9f);
            break;
        case 4: // Flat
        default:
            target = 0.0f;
            break;
    }
    target += currentLowWeightDb * bell(frequency, 140.0f, 1.15f);
    target += currentPresenceDb * bell(frequency, 3200.0f, 0.85f);
    target += currentAirDb * bell(frequency, 12000.0f, 0.85f);
    return detail::finiteSample(target);
}

float SpectralBalanceModule::outputSample(
    float delayedDry, float wet) noexcept
{
    const auto source = currentAmount <= 0.0f ? delayedDry : wet;
    return detail::finiteSample(source * currentOutputGain);
}

void SpectralBalanceModule::publishTelemetry(bool capture) noexcept
{
    if (!capture)
        return;
    ContinuousTelemetrySnapshot snapshot;
    snapshot.sequence = ++telemetrySequence;
    snapshot.valueCount = telemetryValueCount;
    snapshot.values[measuredLevelDb] = meanMeasuredDb;
    snapshot.values[targetMeanDb] = meanTargetDb;
    snapshot.values[maximumCorrectionDb] = maximumCorrection;
    snapshot.values[adaptationSeconds] = currentAdaptationSeconds;
    snapshot.values[lowTargetDb] = targetAt(120.0f);
    snapshot.values[presenceTargetDb] = targetAt(3000.0f);
    snapshot.values[airTargetDb] = targetAt(12000.0f);
    snapshot.values[transientActivity] = transientState;
    snapshot.historyValueCount = 4;
    snapshot.historyCount = continuousTelemetryHistoryCapacity;
    snapshot.historyWritePosition = 0;
    snapshot.history = telemetrySpectrum;
    telemetry.publish(snapshot);
}

bool SpectralBalanceModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}
} // namespace megadsp
