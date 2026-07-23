#include "HarmonicMirage.h"
#include "DspHelpers.h"
#include "DspSafety.h"

#include <algorithm>
#include <cmath>

namespace megadsp
{
namespace
{
constexpr float minimumTrackedFrequency = 45.0f;
constexpr float maximumTrackedFrequency = 5000.0f;

float safeMagnitude(FixedLatencyStft::Complex value) noexcept
{
    return std::isfinite(value.real()) && std::isfinite(value.imag())
        ? std::abs(value) : 0.0f;
}

int localDiscreteIndex(float normalized, int count) noexcept
{
    return juce::jlimit(
        0, count - 1,
        static_cast<int>(normalized * static_cast<float>(count)));
}

float outputGain(float normalized) noexcept
{
    const auto decibels = detail::lerp(-18.0f, 12.0f, normalized);
    return std::abs(decibels) < 0.0001f
        ? 1.0f : juce::Decibels::decibelsToGain(decibels);
}
} // namespace

void HarmonicMirageModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    stft.prepare(spec);
    sampleRate = stft.sampleRate();
    const auto bins = static_cast<std::size_t>(FixedLatencyStft::binCount);
    linkedMagnitude.resize(bins);
    previousMagnitude.resize(bins);
    linkedPhase.resize(bins);
    previousPhase.resize(bins);
    candidateScore.resize(bins);
    mixSmoothed.reset(sampleRate, 0.025);
    outputSmoothed.reset(sampleRate, 0.025);
    reset();
}

void HarmonicMirageModule::reset()
{
    stft.reset();
    std::fill(linkedMagnitude.begin(), linkedMagnitude.end(), 0.0f);
    std::fill(previousMagnitude.begin(), previousMagnitude.end(), 0.0f);
    std::fill(linkedPhase.begin(), linkedPhase.end(), 0.0f);
    std::fill(previousPhase.begin(), previousPhase.end(), 0.0f);
    std::fill(candidateScore.begin(), candidateScore.end(), 0.0f);
    oscillators = {};
    for (int index = 0; index < maximumPartials; ++index)
    {
        auto& oscillator = oscillators[static_cast<std::size_t>(index)];
        oscillator.driftPhase = std::fmod(
            0.1732050807568877 * static_cast<double>(index + 1)
                * static_cast<double>(index + 3),
            1.0);
    }
    mixSmoothed.setCurrentAndTargetValue(1.0f);
    outputSmoothed.setCurrentAndTargetValue(1.0f);
    blockSourceEnergy = 0.0;
    blockGeneratedEnergy = 0.0;
    blockOutputEnergy = 0.0;
    renderedSample = {};
    trackedFrequency = 0.0f;
    trackedAmplitude = 0.0f;
    confidence = 0.0f;
    transientState = 0.0f;
    generatedLevel = 0.0f;
    sourceLevel = 0.0f;
    generatedPartialCount = 0;
    releaseFrames = 0;
    trackingLocked = false;
    phaseHistoryValid = false;
    parametersInitialised = false;
    outputMeter.store(0.0f, std::memory_order_relaxed);
    detectorMeter.store(-100.0f, std::memory_order_relaxed);
    telemetryState = {};
    telemetry.clear();
}

void HarmonicMirageModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    const auto channels = juce::jlimit(0, 2, buffer.getNumChannels());
    if (channels == 0 || buffer.getNumSamples() <= 0)
        return;
    currentChannelCount = channels;

    const auto normalized = [&controls](int index, float fallback)
    {
        return detail::normalizedControl(
            controls[static_cast<std::size_t>(index)], fallback);
    };
    currentMode = localDiscreteIndex(
        normalized(modeControl, 0.0f), modeCount);
    currentTrackingStyle = localDiscreteIndex(
        normalized(trackingControl, 0.5f), trackingStyleCount);
    currentPartialCount = juce::roundToInt(
        static_cast<float>(minimumPartials)
        + static_cast<float>(maximumPartials - minimumPartials)
            * normalized(partialsControl, 6.0f / 22.0f));
    currentPartialCount = juce::jlimit(
        minimumPartials, maximumPartials, currentPartialCount);
    currentEvenOdd = normalized(evenOddControl, 0.5f);
    currentInharmonicity = normalized(inharmonicityControl, 0.1f);
    currentDriftCents =
        30.0f * normalized(fineDriftControl, 0.1f);
    currentResponseSeconds = detail::exponential(
        0.02f, 2.0f, normalized(responseControl, 0.5f));
    currentTransientPreserve =
        normalized(transientPreserveControl, 0.65f);
    currentStereoSpread = normalized(stereoSpreadControl, 0.35f);
    currentMix = normalized(mixControl, 0.35f);
    currentOutput = outputGain(normalized(outputControl, 0.6f));
    smoothingCoefficient = std::exp(
        -1.0f / juce::jmax(
            1.0f, currentResponseSeconds * static_cast<float>(sampleRate)));

    if (!parametersInitialised)
    {
        mixSmoothed.setCurrentAndTargetValue(currentMix);
        outputSmoothed.setCurrentAndTargetValue(currentOutput);
        parametersInitialised = true;
    }
    else
    {
        mixSmoothed.setTargetValue(currentMix);
        outputSmoothed.setTargetValue(currentOutput);
    }

    blockSourceEnergy = 0.0;
    blockGeneratedEnergy = 0.0;
    blockOutputEnergy = 0.0;
    stft.process(buffer, this, processFrameCallback, outputCallback);

    const auto channelSamples = static_cast<double>(
        juce::jmax(1, channels * buffer.getNumSamples()));
    sourceLevel = detail::finiteSample(static_cast<float>(
        std::sqrt(blockSourceEnergy / channelSamples)));
    generatedLevel = detail::finiteSample(static_cast<float>(
        std::sqrt(blockGeneratedEnergy / channelSamples)));
    const auto outputLevel = detail::finiteSample(static_cast<float>(
        std::sqrt(blockOutputEnergy / channelSamples)));
    outputMeter.store(outputLevel, std::memory_order_relaxed);
    detectorMeter.store(
        juce::Decibels::gainToDecibels(sourceLevel, -100.0f),
        std::memory_order_relaxed);
    publishTelemetry(environment.captureTelemetry);
}

void HarmonicMirageModule::processFrameCallback(
    void* context, FixedLatencyStft::Complex* const* spectra,
    int channels, int bins) noexcept
{
    static_cast<HarmonicMirageModule*>(context)->processFrame(
        spectra, channels, bins);
}

float HarmonicMirageModule::outputCallback(
    void* context, int channel, float dry, float) noexcept
{
    auto& module = *static_cast<HarmonicMirageModule*>(context);
    const auto output = module.outputSample(channel, dry);
    if (channel == module.currentChannelCount - 1)
        module.renderOscillators();
    return output;
}

void HarmonicMirageModule::processFrame(
    FixedLatencyStft::Complex* const* spectra, int channels, int bins) noexcept
{
    const auto binHz =
        static_cast<float>(sampleRate / FixedLatencyStft::fftSize);
    const auto minimumBin = juce::jmax(
        2, static_cast<int>(std::ceil(minimumTrackedFrequency / binHz)));
    const auto maximumBin = juce::jmin(
        bins - 2,
        static_cast<int>(std::floor(
            juce::jmin(maximumTrackedFrequency,
                       static_cast<float>(sampleRate * 0.45))
            / binHz)));

    float fluxNumerator = 0.0f;
    float fluxDenominator = 1.0e-9f;
    double totalMagnitude = 0.0;
    for (int bin = 0; bin < bins; ++bin)
    {
        float power = 0.0f;
        float strongestChannelMagnitude = -1.0f;
        float phase = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto magnitude = safeMagnitude(spectra[channel][bin]);
            power += magnitude * magnitude;
            if (magnitude > strongestChannelMagnitude)
            {
                strongestChannelMagnitude = magnitude;
                phase = std::arg(spectra[channel][bin]);
            }
        }
        const auto magnitude = std::sqrt(
            power / static_cast<float>(juce::jmax(1, channels)));
        linkedMagnitude[static_cast<std::size_t>(bin)] = magnitude;
        linkedPhase[static_cast<std::size_t>(bin)] =
            detail::finiteSample(phase);
        const auto previous =
            previousMagnitude[static_cast<std::size_t>(bin)];
        fluxNumerator += juce::jmax(0.0f, magnitude - previous);
        fluxDenominator += magnitude + previous;
        previousMagnitude[static_cast<std::size_t>(bin)] = magnitude;
        if (bin >= minimumBin)
            totalMagnitude += magnitude;
    }

    const auto flux = juce::jlimit(
        0.0f, 1.0f, (fluxNumerator / fluxDenominator - 0.025f) * 5.0f);
    constexpr auto transientAttack = 1.0f;
    const auto transientRelease = 1.0f - std::exp(
        -static_cast<float>(FixedLatencyStft::hopSize)
        / static_cast<float>(sampleRate * 0.11));
    transientState += (flux - transientState)
        * (flux > transientState ? transientAttack : transientRelease);
    transientState = juce::jlimit(
        0.0f, 1.0f, detail::finiteSample(transientState));

    int bestBin = minimumBin;
    float bestScore = 0.0f;
    float bestPeak = 0.0f;
    for (int candidate = minimumBin; candidate <= maximumBin; ++candidate)
    {
        float score = 0.0f;
        float weightSum = 0.0f;
        for (int harmonic = 1; harmonic <= 12; ++harmonic)
        {
            const auto harmonicBin = candidate * harmonic;
            if (harmonicBin >= bins - 1)
                break;
            const auto weight = harmonic == 1
                ? 1.0f : 0.8f / std::sqrt(static_cast<float>(harmonic));
            const auto localPeak = juce::jmax(
                linkedMagnitude[static_cast<std::size_t>(harmonicBin - 1)],
                linkedMagnitude[static_cast<std::size_t>(harmonicBin)],
                linkedMagnitude[static_cast<std::size_t>(harmonicBin + 1)]);
            score += localPeak * weight;
            weightSum += weight;
        }
        score /= juce::jmax(1.0f, weightSum);
        candidateScore[static_cast<std::size_t>(candidate)] = score;
        const auto peak =
            linkedMagnitude[static_cast<std::size_t>(candidate)];
        if (score > bestScore
            && (peak >= linkedMagnitude[static_cast<std::size_t>(candidate - 1)]
                || score > bestScore * 1.15f))
        {
            bestScore = score;
            bestPeak = peak;
            bestBin = candidate;
        }
    }

    const auto centre = linkedMagnitude[static_cast<std::size_t>(bestBin)];
    const auto left = linkedMagnitude[static_cast<std::size_t>(bestBin - 1)];
    const auto right = linkedMagnitude[static_cast<std::size_t>(bestBin + 1)];
    const auto logLeft = std::log(juce::jmax(left, 1.0e-12f));
    const auto logCentre = std::log(juce::jmax(centre, 1.0e-12f));
    const auto logRight = std::log(juce::jmax(right, 1.0e-12f));
    const auto denominator = logLeft - 2.0f * logCentre + logRight;
    const auto binOffset = std::abs(denominator) > 1.0e-9f
        ? juce::jlimit(
              -0.5f, 0.5f,
              0.5f * (logLeft - logRight) / denominator)
        : 0.0f;
    auto refinedBin = static_cast<float>(bestBin) + binOffset;
    if (phaseHistoryValid)
    {
        constexpr auto twoPi = juce::MathConstants<float>::twoPi;
        const auto expectedAdvance =
            twoPi * static_cast<float>(bestBin * FixedLatencyStft::hopSize)
            / static_cast<float>(FixedLatencyStft::fftSize);
        auto phaseDelta =
            linkedPhase[static_cast<std::size_t>(bestBin)]
            - previousPhase[static_cast<std::size_t>(bestBin)]
            - expectedAdvance;
        phaseDelta -= twoPi * std::round(phaseDelta / twoPi);
        const auto instantaneousBin = static_cast<float>(bestBin)
            + phaseDelta
                * static_cast<float>(FixedLatencyStft::fftSize)
                / (twoPi * static_cast<float>(FixedLatencyStft::hopSize));
        if (std::isfinite(instantaneousBin)
            && std::abs(instantaneousBin - static_cast<float>(bestBin)) < 1.5f)
            refinedBin = instantaneousBin;
    }
    const auto candidateFrequency = refinedBin * binHz;

    float localFloor = 0.0f;
    int floorCount = 0;
    for (int offset = -10; offset <= 10; ++offset)
    {
        if (std::abs(offset) <= 2)
            continue;
        const auto bin = juce::jlimit(0, bins - 1, bestBin + offset);
        localFloor += linkedMagnitude[static_cast<std::size_t>(bin)];
        ++floorCount;
    }
    localFloor /= static_cast<float>(juce::jmax(1, floorCount));
    const auto prominence = bestPeak > 1.0e-9f
        ? 1.0f - localFloor / bestPeak : 0.0f;
    const auto concentration = totalMagnitude > 1.0e-9
        ? static_cast<float>(
              juce::jmin(1.0, static_cast<double>(bestScore * 6.0f)
                                / totalMagnitude))
        : 0.0f;
    auto candidateConfidence = juce::jlimit(
        0.0f, 1.0f,
        0.78f * prominence + 0.22f * std::sqrt(concentration));
    const auto candidateAmplitude = juce::jlimit(
        0.0f, 2.0f,
        4.0f * juce::jmax(bestPeak, bestScore)
            / static_cast<float>(FixedLatencyStft::fftSize));
    if (candidateAmplitude < 1.0e-5f
        || !std::isfinite(candidateFrequency))
        candidateConfidence = 0.0f;

    constexpr std::array<float, trackingStyleCount> trackingThresholds {
        0.20f, 0.38f, 0.58f
    };
    const auto acquireThreshold = trackingThresholds[
        static_cast<std::size_t>(currentTrackingStyle)];
    const auto releaseThreshold = acquireThreshold * 0.45f;
    const auto accepted = candidateConfidence >=
        (trackingLocked ? releaseThreshold : acquireThreshold);
    const auto frameBlend = 1.0f - std::exp(
        -static_cast<float>(FixedLatencyStft::hopSize)
        / juce::jmax(
            1.0f, currentResponseSeconds * static_cast<float>(sampleRate)));

    std::array<float, 2> channelGain { 1.0f, 1.0f };
    if (accepted)
    {
        if (!trackingLocked || trackedFrequency <= 0.0f)
            trackedFrequency = candidateFrequency;
        else
            trackedFrequency += frameBlend
                * (candidateFrequency - trackedFrequency);
        trackedFrequency = juce::jlimit(
            minimumTrackedFrequency,
            juce::jmin(maximumTrackedFrequency,
                       static_cast<float>(sampleRate * 0.45)),
            detail::finiteSample(trackedFrequency));
        trackedAmplitude += frameBlend
            * (candidateAmplitude - trackedAmplitude);
        confidence += frameBlend * (candidateConfidence - confidence);
        trackingLocked = true;
        releaseFrames = 0;

        float gainSum = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto channelMagnitude =
                safeMagnitude(spectra[channel][bestBin]);
            channelGain[static_cast<std::size_t>(channel)] =
                std::sqrt(juce::jmax(
                    0.0f, channelMagnitude / juce::jmax(bestPeak, 1.0e-9f)));
            gainSum += channelGain[static_cast<std::size_t>(channel)];
        }
        if (channels == 1)
            channelGain[1] = channelGain[0];
        else if (gainSum > 1.0e-6f)
        {
            const auto normalization = 2.0f / gainSum;
            channelGain[0] *= normalization;
            channelGain[1] *= normalization;
        }
    }
    else
    {
        ++releaseFrames;
        const auto releaseBlend = juce::jmax(
            frameBlend,
            1.0f - std::exp(
                -static_cast<float>(FixedLatencyStft::hopSize)
                / static_cast<float>(sampleRate * 0.35)));
        trackedAmplitude += releaseBlend * (0.0f - trackedAmplitude);
        confidence += releaseBlend * (0.0f - confidence);
        const auto framesToUnlock = juce::jmax(
            2, static_cast<int>(
                   std::ceil(0.5 * sampleRate
                             / FixedLatencyStft::hopSize)));
        if (releaseFrames >= framesToUnlock || trackedAmplitude < 1.0e-6f)
            trackingLocked = false;
    }

    updateOscillatorTargets(
        trackedFrequency,
        trackingLocked ? trackedAmplitude : 0.0f,
        channelGain);

    std::copy(linkedPhase.begin(), linkedPhase.end(), previousPhase.begin());
    phaseHistoryValid = true;
    for (int channel = 0; channel < channels; ++channel)
        std::fill(spectra[channel], spectra[channel] + bins, 
                  FixedLatencyStft::Complex {});
}

void HarmonicMirageModule::updateOscillatorTargets(
    float fundamental, float amplitude,
    const std::array<float, 2>& inputChannelGain) noexcept
{
    const auto maximumFrequency = static_cast<float>(sampleRate * 0.45);
    float weightSum = 0.0f;
    std::array<float, maximumPartials> ratios {};
    std::array<float, maximumPartials> weights {};

    for (int index = 0; index < currentPartialCount; ++index)
    {
        const auto harmonic = index + 1;
        float ratio = static_cast<float>(harmonic);
        if (currentMode == subharmonicMode)
            ratio = 1.0f / static_cast<float>(harmonic);
        else if (currentMode == hollowMode)
            ratio = static_cast<float>(2 * index + 1);

        const auto order = currentMode == hollowMode ? 2 * index + 1 : harmonic;
        const auto parityWeight = (order & 1) != 0
            ? std::sqrt(juce::jmax(0.0f, currentEvenOdd))
            : std::sqrt(juce::jmax(0.0f, 1.0f - currentEvenOdd));
        const auto spectralDecay =
            1.0f / std::pow(static_cast<float>(harmonic), 0.72f);
        auto weight = parityWeight * spectralDecay;
        if (currentMode == hollowMode && (order & 1) == 0)
            weight = 0.0f;

        const auto bendScale =
            currentMode == metallicMode ? 0.28f : 0.08f;
        const auto bend = bendScale * currentInharmonicity
            * (std::pow(static_cast<float>(harmonic), 1.35f) - 1.0f)
            / std::pow(static_cast<float>(maximumPartials), 1.35f);
        ratio *= 1.0f + bend;
        const auto frequency = fundamental * ratio;
        if (frequency < 28.0f || frequency > maximumFrequency
            || !std::isfinite(frequency))
            weight = 0.0f;
        ratios[static_cast<std::size_t>(index)] = ratio;
        weights[static_cast<std::size_t>(index)] = weight;
        weightSum += weight * weight;
    }

    const auto normalization =
        weightSum > 1.0e-9f ? 0.82f / std::sqrt(weightSum) : 0.0f;
    generatedPartialCount = 0;
    for (int index = 0; index < maximumPartials; ++index)
    {
        auto& oscillator = oscillators[static_cast<std::size_t>(index)];
        if (index < currentPartialCount
            && weights[static_cast<std::size_t>(index)] > 0.0f
            && amplitude > 0.0f)
        {
            oscillator.targetFrequency = juce::jlimit(
                28.0f, maximumFrequency,
                fundamental * ratios[static_cast<std::size_t>(index)]);
            oscillator.targetAmplitude = juce::jlimit(
                0.0f, 1.5f,
                amplitude * weights[static_cast<std::size_t>(index)]
                    * normalization);
            const auto pan = currentChannelCount > 1
                ? ((index & 1) == 0
                       ? -currentStereoSpread : currentStereoSpread)
                : 0.0f;
            oscillator.channelGain[0] =
                inputChannelGain[0] * std::sqrt(juce::jmax(0.0f, 1.0f - pan));
            oscillator.channelGain[1] =
                inputChannelGain[1] * std::sqrt(juce::jmax(0.0f, 1.0f + pan));
            ++generatedPartialCount;
        }
        else
        {
            oscillator.targetAmplitude = 0.0f;
            if (index >= currentPartialCount)
                oscillator.targetFrequency = 0.0f;
        }
    }
}

void HarmonicMirageModule::renderOscillators() noexcept
{
    renderedSample = {};
    constexpr auto twoPi = juce::MathConstants<double>::twoPi;
    for (int index = 0; index < maximumPartials; ++index)
    {
        auto& oscillator = oscillators[static_cast<std::size_t>(index)];
        oscillator.frequency = detail::finiteSample(
            smoothingCoefficient * oscillator.frequency
            + (1.0f - smoothingCoefficient) * oscillator.targetFrequency);
        oscillator.amplitude = detail::finiteSample(
            smoothingCoefficient * oscillator.amplitude
            + (1.0f - smoothingCoefficient) * oscillator.targetAmplitude);
        if (oscillator.amplitude < 1.0e-9f
            && oscillator.targetAmplitude <= 0.0f)
        {
            oscillator.amplitude = 0.0f;
            continue;
        }

        const auto driftRate =
            0.071 + 0.019 * static_cast<double>((index * 7) % 11);
        oscillator.driftPhase += driftRate / sampleRate;
        oscillator.driftPhase -= std::floor(oscillator.driftPhase);
        const auto driftCents = static_cast<double>(currentDriftCents)
            * std::sin(twoPi * oscillator.driftPhase);
        const auto driftRatio = std::exp2(driftCents / 1200.0);
        const auto frequency = juce::jlimit(
            0.0, sampleRate * 0.45,
            static_cast<double>(oscillator.frequency) * driftRatio);
        const auto value = oscillator.amplitude
            * static_cast<float>(std::sin(twoPi * oscillator.phase));
        renderedSample[0] += value * oscillator.channelGain[0];
        renderedSample[1] += value * oscillator.channelGain[1];
        oscillator.phase += frequency / sampleRate;
        oscillator.phase -= std::floor(oscillator.phase);
    }
    renderedSample[0] = detail::finiteSample(renderedSample[0]);
    renderedSample[1] = detail::finiteSample(renderedSample[1]);
}

float HarmonicMirageModule::outputSample(
    int channel, float delayedDry) noexcept
{
    delayedDry = detail::finiteSample(delayedDry);
    const auto generated =
        renderedSample[static_cast<std::size_t>(juce::jlimit(0, 1, channel))];
    const auto preservedTransient =
        delayedDry * currentTransientPreserve * transientState;
    const auto wet = detail::finiteSample(generated + preservedTransient);
    const auto mix = channel == 0
        ? mixSmoothed.getNextValue() : mixSmoothed.getCurrentValue();
    const auto output = channel == 0
        ? outputSmoothed.getNextValue() : outputSmoothed.getCurrentValue();
    const auto result = detail::finiteSample(
        (delayedDry + (wet - delayedDry) * mix) * output);
    blockSourceEnergy += static_cast<double>(delayedDry) * delayedDry;
    blockGeneratedEnergy += static_cast<double>(generated) * generated;
    blockOutputEnergy += static_cast<double>(result) * result;
    return result;
}

void HarmonicMirageModule::publishTelemetry(bool capture) noexcept
{
    if (!capture)
        return;
    ++telemetryState.sequence;
    telemetryState.valueCount = telemetryValueCount;
    telemetryState.values[trackedFrequencyHz] =
        trackingLocked ? trackedFrequency : 0.0f;
    float firstGeneratedFrequency = 0.0f;
    for (const auto& oscillator : oscillators)
    {
        if (oscillator.amplitude > 1.0e-6f)
        {
            firstGeneratedFrequency = oscillator.frequency;
            break;
        }
    }
    telemetryState.values[generatedFrequencyHz] = firstGeneratedFrequency;
    telemetryState.values[trackingConfidence] = confidence;
    telemetryState.values[activePartialCount] =
        static_cast<float>(generatedPartialCount);
    telemetryState.values[transientActivity] = transientState;
    telemetryState.values[generatedRms] = generatedLevel;
    telemetryState.values[sourceRms] = sourceLevel;
    telemetryState.values[activeMode] = static_cast<float>(currentMode);
    appendContinuousTelemetryHistory(
        telemetryState,
        {
            telemetryState.values[trackedFrequencyHz],
            telemetryState.values[generatedFrequencyHz],
            confidence,
            generatedLevel
        },
        telemetryHistoryValueCount);
    telemetry.publish(telemetryState);
}

double HarmonicMirageModule::tailSeconds(const ControlValues& controls) const
{
    const auto response = detail::exponential(
        0.02f, 2.0f,
        detail::normalizedControl(controls[responseControl], 0.5f));
    return static_cast<double>(FixedLatencyStft::fftSize) / sampleRate
           + 6.0 * static_cast<double>(response);
}

float HarmonicMirageModule::meterValue() const
{
    return outputMeter.load(std::memory_order_relaxed);
}

float HarmonicMirageModule::detectorValue() const
{
    return detectorMeter.load(std::memory_order_relaxed);
}

bool HarmonicMirageModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}
} // namespace megadsp
