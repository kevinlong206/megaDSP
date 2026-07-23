#include "ResonanceTamer.h"
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

float frameCoefficient(double sampleRate, float milliseconds) noexcept
{
    return std::exp(
        -static_cast<float>(FixedLatencyStft::hopSize)
        / (0.001f * milliseconds * static_cast<float>(sampleRate)));
}
} // namespace

void ResonanceTamerModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    stft.prepare(spec);
    sampleRate = stft.sampleRate();
    const auto bins = static_cast<std::size_t>(FixedLatencyStft::binCount);
    reductionSmoother.prepare(bins);
    linkedMagnitude.resize(bins);
    previousMagnitude.resize(bins);
    levelDb.resize(bins);
    prefix.resize(bins + 1);
    baselineDb.resize(bins);
    desiredReduction.resize(bins);
    smoothedReduction.resize(bins);
    mixSmoothed.reset(sampleRate, 0.03);
    outputSmoothed.reset(sampleRate, 0.03);
    reset();
}

void ResonanceTamerModule::reset()
{
    stft.reset();
    reductionSmoother.reset();
    std::fill(linkedMagnitude.begin(), linkedMagnitude.end(), 0.0f);
    std::fill(previousMagnitude.begin(), previousMagnitude.end(), 0.0f);
    std::fill(levelDb.begin(), levelDb.end(), -100.0f);
    std::fill(prefix.begin(), prefix.end(), 0.0f);
    std::fill(baselineDb.begin(), baselineDb.end(), -100.0f);
    std::fill(desiredReduction.begin(), desiredReduction.end(), 0.0f);
    std::fill(smoothedReduction.begin(), smoothedReduction.end(), 0.0f);
    for (auto& lane : telemetrySpectrum)
        lane.fill(0.0f);
    mixSmoothed.setCurrentAndTargetValue(1.0f);
    outputSmoothed.setCurrentAndTargetValue(1.0f);
    maximumReductionDb = 0.0f;
    maximumExcessDb = 0.0f;
    meanInputDb = -100.0f;
    meanBaselineDb = -100.0f;
    strongestHz = 0.0f;
    transientState = 0.0f;
    currentMixGain = 1.0f;
    currentOutputGain = 1.0f;
    parametersInitialised = false;
    telemetrySequence = 0;
    telemetry.clear();
    reductionMeter.store(0.0f, std::memory_order_relaxed);
    detectorMeter.store(-100.0f, std::memory_order_relaxed);
}

void ResonanceTamerModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    const auto reduction = 18.0f
        * detail::normalizedControl(controls[0], 0.5f);
    selectivity = juce::jlimit(
        0, 2, static_cast<int>(
                  detail::normalizedControl(controls[1], 0.5f) * 3.0f));
    const auto reaction = juce::jlimit(
        0, 2, static_cast<int>(
                  detail::normalizedControl(controls[2], 0.5f) * 3.0f));
    currentToneBias =
        detail::lerp(-3.0f, 3.0f,
                     detail::normalizedControl(controls[3], 0.5f));
    currentLowHz = detail::exponential(
        20.0f, 2000.0f, detail::normalizedControl(controls[4], 0.0f));
    currentHighHz = juce::jmin(
        static_cast<float>(sampleRate * 0.475),
        detail::exponential(
            1000.0f, 20000.0f,
            detail::normalizedControl(controls[5], 1.0f)));
    if (currentHighHz < currentLowHz)
        std::swap(currentHighHz, currentLowHz);
    currentTransientPreserve =
        detail::normalizedControl(controls[6], 0.5f);
    currentReductionDb = reduction;

    constexpr std::array<float, 3> attacks { 140.0f, 45.0f, 10.0f };
    constexpr std::array<float, 3> releases { 650.0f, 260.0f, 90.0f };
    attackCoefficient = frameCoefficient(
        sampleRate, attacks[static_cast<std::size_t>(reaction)]);
    releaseCoefficient = frameCoefficient(
        sampleRate, releases[static_cast<std::size_t>(reaction)]);
    const auto mix = detail::normalizedControl(controls[7], 1.0f);
    const auto output = juce::Decibels::decibelsToGain(detail::lerp(
        -18.0f, 12.0f, detail::normalizedControl(controls[8], 0.6f)));
    if (!parametersInitialised)
    {
        mixSmoothed.setCurrentAndTargetValue(mix);
        outputSmoothed.setCurrentAndTargetValue(output);
        parametersInitialised = true;
    }
    else
    {
        mixSmoothed.setTargetValue(mix);
        outputSmoothed.setTargetValue(output);
    }

    stft.process(
        buffer, this, processFrameCallback, outputCallback);
    updateTelemetry(environment.captureTelemetry);
}

void ResonanceTamerModule::processFrameCallback(
    void* context, FixedLatencyStft::Complex* const* spectra,
    int channels, int bins) noexcept
{
    static_cast<ResonanceTamerModule*>(context)->processFrame(
        spectra, channels, bins);
}

float ResonanceTamerModule::outputCallback(
    void* context, int channel, float dry, float wet) noexcept
{
    auto& module = *static_cast<ResonanceTamerModule*>(context);
    if (channel == 0)
    {
        module.currentMixGain = module.mixSmoothed.getNextValue();
        module.currentOutputGain = module.outputSmoothed.getNextValue();
    }
    return module.mixOutput(dry, wet);
}

void ResonanceTamerModule::processFrame(
    FixedLatencyStft::Complex* const* spectra, int channels, int bins) noexcept
{
    float fluxNumerator = 0.0f;
    float fluxDenominator = 1.0e-9f;
    for (int bin = 0; bin < bins; ++bin)
    {
        float power = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto magnitude = safeMagnitude(spectra[channel][bin]);
            power += magnitude * magnitude;
        }
        const auto magnitude = std::sqrt(
            power / static_cast<float>(juce::jmax(1, channels)));
        linkedMagnitude[static_cast<std::size_t>(bin)] = magnitude;
        const auto previous = previousMagnitude[static_cast<std::size_t>(bin)];
        fluxNumerator += juce::jmax(0.0f, magnitude - previous);
        fluxDenominator += magnitude + previous;
        previousMagnitude[static_cast<std::size_t>(bin)] = magnitude;
        levelDb[static_cast<std::size_t>(bin)] =
            juce::Decibels::gainToDecibels(
                magnitude / static_cast<float>(FixedLatencyStft::fftSize),
                -120.0f);
    }
    const auto flux = juce::jlimit(
        0.0f, 1.0f, (fluxNumerator / fluxDenominator - 0.025f) * 7.0f);
    transientState += 0.35f * (flux - transientState);

    prefix[0] = 0.0f;
    for (int bin = 0; bin < bins; ++bin)
        prefix[static_cast<std::size_t>(bin + 1)] =
            prefix[static_cast<std::size_t>(bin)]
            + levelDb[static_cast<std::size_t>(bin)];

    constexpr std::array<int, 3> radii { 10, 22, 42 };
    constexpr std::array<int, 3> guards { 2, 3, 4 };
    constexpr std::array<float, 3> thresholds { 3.5f, 2.5f, 1.5f };
    const auto radius = radii[static_cast<std::size_t>(selectivity)];
    const auto guard = guards[static_cast<std::size_t>(selectivity)];
    const auto threshold = thresholds[static_cast<std::size_t>(selectivity)];
    const auto binHz =
        static_cast<float>(sampleRate / FixedLatencyStft::fftSize);
    maximumExcessDb = 0.0f;
    maximumReductionDb = 0.0f;
    strongestHz = 0.0f;
    double inputSum = 0.0;
    double baselineSum = 0.0;
    int activeBins = 0;

    for (int bin = 0; bin < bins; ++bin)
    {
        const auto frequency = static_cast<float>(bin) * binHz;
        const auto low = juce::jmax(1, bin - radius);
        const auto high = juce::jmin(bins - 1, bin + radius);
        const auto leftHigh = juce::jmax(low, bin - guard);
        const auto rightLow = juce::jmin(high + 1, bin + guard + 1);
        const auto leftCount = juce::jmax(0, leftHigh - low);
        const auto rightCount = juce::jmax(0, high + 1 - rightLow);
        const auto count = leftCount + rightCount;
        auto baseline = levelDb[static_cast<std::size_t>(bin)];
        if (count > 0)
        {
            const auto leftSum = prefix[static_cast<std::size_t>(leftHigh)]
                                 - prefix[static_cast<std::size_t>(low)];
            const auto rightSum = prefix[static_cast<std::size_t>(high + 1)]
                                  - prefix[static_cast<std::size_t>(rightLow)];
            baseline = (leftSum + rightSum) / static_cast<float>(count);
        }
        baselineDb[static_cast<std::size_t>(bin)] = baseline;
        const auto bias = frequency > 1.0f
            ? currentToneBias * std::log2(frequency / 1000.0f) : 0.0f;
        const auto inRange =
            frequency >= currentLowHz && frequency <= currentHighHz;
        const auto excess = inRange
            ? juce::jmax(
                  0.0f, levelDb[static_cast<std::size_t>(bin)]
                            - baseline - threshold - bias)
            : 0.0f;
        desiredReduction[static_cast<std::size_t>(bin)] =
            juce::jmin(currentReductionDb, excess)
            * (1.0f - currentTransientPreserve * transientState);
        if (inRange)
        {
            inputSum += levelDb[static_cast<std::size_t>(bin)];
            baselineSum += baseline;
            ++activeBins;
        }
        if (excess > maximumExcessDb)
        {
            maximumExcessDb = excess;
            strongestHz = frequency;
        }
    }

    for (int bin = 0; bin < bins; ++bin)
    {
        const auto low = juce::jmax(0, bin - 1);
        const auto high = juce::jmin(bins - 1, bin + 1);
        float target = 0.0f;
        float weights = 0.0f;
        for (int neighbour = low; neighbour <= high; ++neighbour)
        {
            const auto weight = neighbour == bin ? 2.0f : 1.0f;
            target += weight
                * desiredReduction[static_cast<std::size_t>(neighbour)];
            weights += weight;
        }
        target /= juce::jmax(1.0f, weights);
        const auto previous = reductionSmoother.value(
            static_cast<std::size_t>(bin));
        const auto coefficient =
            target > previous ? attackCoefficient : releaseCoefficient;
        const auto reduction = reductionSmoother.process(
            static_cast<std::size_t>(bin), target, coefficient);
        smoothedReduction[static_cast<std::size_t>(bin)] = reduction;
        maximumReductionDb = juce::jmax(maximumReductionDb, reduction);
        const auto gain = juce::Decibels::decibelsToGain(-reduction);
        for (int channel = 0; channel < channels; ++channel)
            spectra[channel][bin] *= gain;
    }

    meanInputDb = activeBins > 0
        ? static_cast<float>(inputSum / activeBins) : -100.0f;
    meanBaselineDb = activeBins > 0
        ? static_cast<float>(baselineSum / activeBins) : -100.0f;
    detectorMeter.store(
        detail::finiteSample(maximumExcessDb), std::memory_order_relaxed);
    reductionMeter.store(
        detail::finiteSample(maximumReductionDb), std::memory_order_relaxed);

    for (std::size_t point = 0;
         point < continuousTelemetryHistoryCapacity; ++point)
    {
        const auto normalized = static_cast<float>(point)
                                / static_cast<float>(
                                    continuousTelemetryHistoryCapacity - 1);
        const auto frequency = 20.0f
            * std::pow(1000.0f, normalized);
        const auto bin = juce::jlimit(
            0, bins - 1, static_cast<int>(std::lround(frequency / binHz)));
        telemetrySpectrum[0][point] =
            levelDb[static_cast<std::size_t>(bin)];
        telemetrySpectrum[1][point] =
            baselineDb[static_cast<std::size_t>(bin)];
        telemetrySpectrum[2][point] = juce::jmax(
            0.0f, levelDb[static_cast<std::size_t>(bin)]
                      - baselineDb[static_cast<std::size_t>(bin)]);
        telemetrySpectrum[3][point] =
            smoothedReduction[static_cast<std::size_t>(bin)];
    }
}

float ResonanceTamerModule::mixOutput(
    float delayedDry, float wet) noexcept
{
    const auto mix = juce::jlimit(0.0f, 1.0f, currentMixGain);
    const auto dryGain =
        std::cos(mix * juce::MathConstants<float>::halfPi);
    const auto wetGain =
        std::sin(mix * juce::MathConstants<float>::halfPi);
    return detail::finiteSample(
        currentOutputGain * (dryGain * delayedDry + wetGain * wet));
}

void ResonanceTamerModule::updateTelemetry(bool capture) noexcept
{
    if (!capture)
        return;
    ContinuousTelemetrySnapshot snapshot;
    snapshot.sequence = ++telemetrySequence;
    snapshot.valueCount = telemetryValueCount;
    snapshot.values[inputLevelDb] = meanInputDb;
    snapshot.values[baselineLevelDb] = meanBaselineDb;
    snapshot.values[detectedExcessDb] = maximumExcessDb;
    snapshot.values[actualReductionDb] = maximumReductionDb;
    snapshot.values[strongestFrequencyHz] = strongestHz;
    snapshot.values[transientActivity] = transientState;
    snapshot.values[lowLimitHz] = currentLowHz;
    snapshot.values[highLimitHz] = currentHighHz;
    snapshot.historyValueCount = 4;
    snapshot.historyCount = continuousTelemetryHistoryCapacity;
    snapshot.historyWritePosition = 0;
    snapshot.history = telemetrySpectrum;
    telemetry.publish(snapshot);
}

bool ResonanceTamerModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}
} // namespace megadsp
