#include "SpectralPrism.h"
#include "DspHelpers.h"

#include <cmath>

namespace megadsp
{
using detail::exponential;
using detail::lerp;

namespace
{
constexpr auto pi = juce::MathConstants<float>::pi;
constexpr auto twoPi = juce::MathConstants<float>::twoPi;

float smoothStep(float value)
{
    const auto clamped = juce::jlimit(0.0f, 1.0f, value);
    return clamped * clamped * (3.0f - 2.0f * clamped);
}

juce::dsp::Complex<float> sanitizeComplex(juce::dsp::Complex<float> value)
{
    if (!std::isfinite(value.real()) || !std::isfinite(value.imag()))
        return {};
    return value;
}
} // namespace

void SpectralPrismModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = juce::jmax(8000.0, spec.sampleRate);
    activeChannels = juce::jlimit(
        1, maxChannels, static_cast<int>(spec.numChannels));
    initialiseWindows();
    warpSmoothed.reset(sampleRate, 0.05);
    pivotSmoothed.reset(sampleRate, 0.08);
    shiftSmoothed.reset(sampleRate, 0.05);
    smearSmoothed.reset(sampleRate, 0.06);
    motionRateSmoothed.reset(sampleRate, 0.08);
    motionDepthSmoothed.reset(sampleRate, 0.06);
    diffusionSmoothed.reset(sampleRate, 0.06);
    spreadSmoothed.reset(sampleRate, 0.05);
    transientSmoothed.reset(sampleRate, 0.03);
    mixSmoothed.reset(sampleRate, 0.03);
    outputSmoothed.reset(sampleRate, 0.03);
    reset();
}

void SpectralPrismModule::reset()
{
    for (auto& channel : inputRing)
        channel.fill(0.0f);
    for (auto& channel : dryDelay)
        channel.fill(0.0f);
    for (auto& channel : wetRing)
        channel.fill(0.0f);
    for (auto& values : previousAnalysisPhase)
        values.fill(0.0f);
    for (auto& values : previousFluxMagnitude)
        values.fill(0.0f);
    for (auto& values : smearMagnitudeState)
        values.fill(0.0f);
    for (auto& values : liveOutputPhase)
        values.fill(0.0f);
    for (auto& values : freezeMagnitude)
        values.fill(0.0f);
    for (auto& values : freezePhaseAdvance)
        values.fill(0.0f);
    for (auto& values : freezeOutputPhase)
        values.fill(0.0f);
    inputWritePosition = 0;
    dryDelayPosition = 0;
    wetReadPosition = 0;
    samplesSinceFrame = 0;
    validInputSamples = 0;
    motionPhase = 0.0;
    currentWarp = 0.0f;
    currentPivotHz = 1000.0f;
    currentShiftSemitones = 0.0f;
    currentSmear = 0.0f;
    currentMotionRate = 0.15f;
    currentMotionDepth = 0.0f;
    currentDiffusion = 0.0f;
    currentSpread = 0.0f;
    currentTransientPreserve = 0.0f;
    freezeBlend = 0.0f;
    transientEnvelope = 0.0f;
    meterEnvelope = 0.0f;
    parametersInitialised = false;
    spectralInitialised = false;
    freezeLatched = false;
    warpSmoothed.setCurrentAndTargetValue(0.0f);
    pivotSmoothed.setCurrentAndTargetValue(1000.0f);
    shiftSmoothed.setCurrentAndTargetValue(0.0f);
    smearSmoothed.setCurrentAndTargetValue(0.0f);
    motionRateSmoothed.setCurrentAndTargetValue(0.15f);
    motionDepthSmoothed.setCurrentAndTargetValue(0.0f);
    diffusionSmoothed.setCurrentAndTargetValue(0.0f);
    spreadSmoothed.setCurrentAndTargetValue(0.0f);
    transientSmoothed.setCurrentAndTargetValue(0.0f);
    mixSmoothed.setCurrentAndTargetValue(0.0f);
    outputSmoothed.setCurrentAndTargetValue(1.0f);
    outputMeter.store(0.0f, std::memory_order_relaxed);
}

void SpectralPrismModule::process(juce::AudioBuffer<float>& buffer,
                                  const ControlValues& controls,
                                  const ProcessEnvironment&)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() <= 0 || buffer.getNumSamples() <= 0)
        return;

    activeChannels = juce::jlimit(1, maxChannels, buffer.getNumChannels());
    updateParameterTargets(controls);
    if (!parametersInitialised)
        initialiseParameters(controls);

    const auto freezeTarget = normalized(controls[4], 0.0f) >= 0.5f;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const std::array<float, 2> input {
            sanitizeSample(buffer.getSample(0, sample)),
            sanitizeSample(activeChannels > 1 ? buffer.getSample(1, sample)
                                             : buffer.getSample(0, sample))
        };

        std::array<float, 2> delayedDry {};
        std::array<float, 2> wet {};
        for (int channel = 0; channel < maxChannels; ++channel)
        {
            delayedDry[static_cast<size_t>(channel)] =
                dryDelay[static_cast<size_t>(channel)]
                        [static_cast<size_t>(dryDelayPosition)];
            dryDelay[static_cast<size_t>(channel)]
                    [static_cast<size_t>(dryDelayPosition)] =
                input[static_cast<size_t>(channel)];

            wet[static_cast<size_t>(channel)] =
                wetRing[static_cast<size_t>(channel)]
                       [static_cast<size_t>(wetReadPosition)];
            wetRing[static_cast<size_t>(channel)]
                   [static_cast<size_t>(wetReadPosition)] = 0.0f;

            inputRing[static_cast<size_t>(channel)]
                     [static_cast<size_t>(inputWritePosition)] =
                input[static_cast<size_t>(channel)];
        }

        currentWarp = warpSmoothed.getNextValue();
        currentPivotHz = pivotSmoothed.getNextValue();
        currentShiftSemitones = shiftSmoothed.getNextValue();
        currentSmear = smearSmoothed.getNextValue();
        currentMotionRate = motionRateSmoothed.getNextValue();
        currentMotionDepth = motionDepthSmoothed.getNextValue();
        currentDiffusion = diffusionSmoothed.getNextValue();
        currentSpread = spreadSmoothed.getNextValue();
        currentTransientPreserve = transientSmoothed.getNextValue();
        const auto mix = mixSmoothed.getNextValue();
        const auto output = outputSmoothed.getNextValue();

        inputWritePosition = (inputWritePosition + 1) % fftSize;
        validInputSamples = juce::jmin(validInputSamples + 1, fftSize);
        if (++samplesSinceFrame >= hopSize)
        {
            samplesSinceFrame = 0;
            if (validInputSamples >= fftSize)
                processSpectralFrame(activeChannels > 1, freezeTarget);
        }

        const auto dryGain = std::cos(
            juce::jlimit(0.0f, 1.0f, mix) * juce::MathConstants<float>::halfPi);
        const auto wetGain = std::sin(
            juce::jlimit(0.0f, 1.0f, mix) * juce::MathConstants<float>::halfPi);
        const auto wetMid = 0.5f * (wet[0] + wet[1]);

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto index = static_cast<size_t>(juce::jmin(channel, 1));
            const auto wetSample = activeChannels > 1
                ? wet[index]
                : wetMid;
            const auto result = output
                * (dryGain * delayedDry[index] + wetGain * wetSample);
            buffer.setSample(channel, sample, sanitizeSample(result));
        }

        const auto meterSample = activeChannels > 1
            ? 0.5f * (wet[0] * wet[0] + wet[1] * wet[1])
            : wetMid * wetMid;
        meterEnvelope = 0.995f * meterEnvelope
                        + 0.005f * juce::jlimit(0.0f, 64.0f, meterSample);
        outputMeter.store(
            juce::jlimit(0.0f, 1.0f, std::sqrt(meterEnvelope)),
            std::memory_order_relaxed);

        dryDelayPosition = (dryDelayPosition + 1) % dryDelaySize;
        wetReadPosition = (wetReadPosition + 1) % wetRingSize;

        if (!freezeTarget && freezeBlend <= 0.0001f)
            freezeLatched = false;
    }
}

double SpectralPrismModule::tailSeconds(const ControlValues& controls) const
{
    if (normalized(controls[4], 0.0f) >= 0.5f)
        return 60.0;
    const auto smear = normalized(controls[3], 0.2f);
    const auto smearCoefficient = juce::jlimit(
        0.0f, 0.995f, 0.12f + 0.86f * smear * smear);
    const auto decayFrames = smearCoefficient <= 0.0001f
        ? 0.0
        : std::log(0.001) / std::log(smearCoefficient);
    return static_cast<double>(reportedLatencySamples + fftSize
                               + hopSize * juce::jmax(0.0, decayFrames))
           / sampleRate;
}

void SpectralPrismModule::initialiseWindows()
{
    for (int index = 0; index < fftSize; ++index)
        analysisWindow[static_cast<size_t>(index)] =
            0.5f - 0.5f * std::cos(
                              twoPi * static_cast<float>(index)
                              / static_cast<float>(fftSize));

    for (int index = 0; index < fftSize; ++index)
    {
        auto sum = 0.0f;
        for (int overlap = 0; overlap < fftSize / hopSize; ++overlap)
        {
            const auto wrapped = (index + overlap * hopSize) % fftSize;
            const auto value = analysisWindow[static_cast<size_t>(wrapped)];
            sum += value * value;
        }
        synthesisWindow[static_cast<size_t>(index)] =
            sum > 0.0f ? analysisWindow[static_cast<size_t>(index)] / sum : 0.0f;
    }
}

void SpectralPrismModule::initialiseParameters(const ControlValues& controls)
{
    updateParameterTargets(controls);
    currentWarp = bipolar(controls[0]);
    currentPivotHz = juce::jlimit(
        80.0f, static_cast<float>(sampleRate * 0.45),
        exponential(80.0f, 8000.0f, normalized(controls[1], 0.55f)));
    currentShiftSemitones = semitones(controls[2]);
    currentSmear = normalized(controls[3], 0.2f);
    currentMotionRate = exponential(
        0.02f, 4.0f, normalized(controls[5], 0.38f));
    currentMotionDepth = normalized(controls[6], 0.15f);
    currentDiffusion = normalized(controls[7], 0.15f);
    currentSpread = normalized(controls[8], 0.35f);
    currentTransientPreserve = normalized(controls[9], 0.65f);
    warpSmoothed.setCurrentAndTargetValue(currentWarp);
    pivotSmoothed.setCurrentAndTargetValue(currentPivotHz);
    shiftSmoothed.setCurrentAndTargetValue(currentShiftSemitones);
    smearSmoothed.setCurrentAndTargetValue(currentSmear);
    motionRateSmoothed.setCurrentAndTargetValue(currentMotionRate);
    motionDepthSmoothed.setCurrentAndTargetValue(currentMotionDepth);
    diffusionSmoothed.setCurrentAndTargetValue(currentDiffusion);
    spreadSmoothed.setCurrentAndTargetValue(currentSpread);
    transientSmoothed.setCurrentAndTargetValue(currentTransientPreserve);
    mixSmoothed.setCurrentAndTargetValue(normalized(controls[10], 0.4f));
    outputSmoothed.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(
        lerp(-18.0f, 12.0f, normalized(controls[11], 0.6f))));
    parametersInitialised = true;
}

void SpectralPrismModule::updateParameterTargets(const ControlValues& controls)
{
    const auto safePivot = juce::jlimit(
        80.0f, static_cast<float>(sampleRate * 0.45),
        exponential(80.0f, 8000.0f, normalized(controls[1], 0.55f)));
    warpSmoothed.setTargetValue(bipolar(controls[0]));
    pivotSmoothed.setTargetValue(safePivot);
    shiftSmoothed.setTargetValue(semitones(controls[2]));
    smearSmoothed.setTargetValue(normalized(controls[3], 0.2f));
    motionRateSmoothed.setTargetValue(
        exponential(0.02f, 4.0f, normalized(controls[5], 0.38f)));
    motionDepthSmoothed.setTargetValue(normalized(controls[6], 0.15f));
    diffusionSmoothed.setTargetValue(normalized(controls[7], 0.15f));
    spreadSmoothed.setTargetValue(normalized(controls[8], 0.35f));
    transientSmoothed.setTargetValue(normalized(controls[9], 0.65f));
    mixSmoothed.setTargetValue(normalized(controls[10], 0.4f));
    outputSmoothed.setTargetValue(juce::Decibels::decibelsToGain(
        lerp(-18.0f, 12.0f, normalized(controls[11], 0.6f))));
}

void SpectralPrismModule::processSpectralFrame(
    bool stereoInput, bool freezeTarget)
{
    std::array<std::array<Complex, spectrumBins>, spectralDomains> inputDomains {};
    std::array<std::array<Complex, spectrumBins>, spectralDomains> liveDomains {};
    std::array<std::array<float, spectrumBins>, spectralDomains> analysisAdvance {};

    for (int channel = 0; channel < maxChannels; ++channel)
    {
        for (int index = 0; index < fftSize; ++index)
        {
            const auto sourceIndex = (inputWritePosition + index) % fftSize;
            fftTimeDomain[static_cast<size_t>(channel)]
                         [static_cast<size_t>(index)] = {
                inputRing[static_cast<size_t>(channel)]
                         [static_cast<size_t>(sourceIndex)]
                    * analysisWindow[static_cast<size_t>(index)],
                0.0f
            };
        }
        fft.perform(fftTimeDomain[static_cast<size_t>(channel)].data(),
                    fftFrequencyDomain[static_cast<size_t>(channel)].data(),
                    false);
    }

    for (int bin = 0; bin < spectrumBins; ++bin)
    {
        const auto left =
            sanitizeComplex(fftFrequencyDomain[0][static_cast<size_t>(bin)]);
        const auto right = stereoInput
            ? sanitizeComplex(
                  fftFrequencyDomain[1][static_cast<size_t>(bin)])
            : left;
        inputDomains[0][static_cast<size_t>(bin)] = (left + right) * 0.5f;
        inputDomains[1][static_cast<size_t>(bin)] =
            stereoInput ? (left - right) * 0.5f : Complex {};
    }

    auto fluxNumerator = 0.0f;
    auto fluxDenominator = 1.0e-6f;
    for (int domain = 0; domain < spectralDomains; ++domain)
    {
        for (int bin = 0; bin < spectrumBins; ++bin)
        {
            const auto source =
                inputDomains[static_cast<size_t>(domain)]
                            [static_cast<size_t>(bin)];
            const auto magnitude = std::isfinite(source.real())
                    && std::isfinite(source.imag())
                ? std::abs(source)
                : 0.0f;
            const auto phase = (bin == 0 || bin == spectrumBins - 1
                                || magnitude <= 1.0e-9f)
                ? 0.0f
                : std::atan2(source.imag(), source.real());
            auto advance = 0.0f;
            if (spectralInitialised && bin > 0 && bin < spectrumBins - 1)
            {
                const auto expected = twoPi * static_cast<float>(hopSize * bin)
                    / static_cast<float>(fftSize);
                advance = principalPhase(
                              phase
                              - previousAnalysisPhase[static_cast<size_t>(
                                  domain)][static_cast<size_t>(bin)]
                              - expected)
                    + expected;
            }
            analysisAdvance[static_cast<size_t>(domain)]
                           [static_cast<size_t>(bin)] = advance;

            const auto previous =
                previousFluxMagnitude[static_cast<size_t>(domain)]
                                     [static_cast<size_t>(bin)];
            const auto weight = domain == 0 ? 1.0f : 0.5f;
            fluxNumerator += weight * juce::jmax(0.0f, magnitude - previous);
            fluxDenominator += weight * (magnitude + previous);
            previousFluxMagnitude[static_cast<size_t>(domain)]
                                 [static_cast<size_t>(bin)] = magnitude;
            previousAnalysisPhase[static_cast<size_t>(domain)]
                                 [static_cast<size_t>(bin)] = phase;
        }
    }

    const auto fluxRatio = fluxNumerator / fluxDenominator;
    const auto transientFrame = juce::jlimit(
        0.0f, 1.0f, (fluxRatio - 0.03f) * 8.0f);
    transientEnvelope += (transientFrame - transientEnvelope) * 0.35f;
    const auto preserve = juce::jlimit(
        0.0f, 1.0f, currentTransientPreserve * transientEnvelope);

    motionPhase += twoPi * static_cast<double>(currentMotionRate)
                   * static_cast<double>(hopSize) / sampleRate;
    while (motionPhase >= static_cast<double>(twoPi))
        motionPhase -= static_cast<double>(twoPi);
    const auto motionPhaseNow = static_cast<float>(motionPhase);

    const auto freezeTime = 0.03f;
    const auto freezeCoefficient = 1.0f - std::exp(
        -static_cast<float>(hopSize) / (freezeTime * static_cast<float>(sampleRate)));
    const auto captureFreeze = freezeTarget && !freezeLatched;
    const auto effectiveSmear = currentSmear * (1.0f - 0.8f * preserve);
    const auto smearCoefficient = juce::jlimit(
        0.0f, 0.995f, 0.12f + 0.86f * effectiveSmear * effectiveSmear);
    const auto effectiveDiffusion =
        currentDiffusion * (1.0f - 0.85f * preserve);

    const auto pivotBin = juce::jlimit(
        1.0f, static_cast<float>(spectrumBins - 2),
        currentPivotHz * static_cast<float>(fftSize)
            / static_cast<float>(sampleRate));

    for (int domain = 0; domain < spectralDomains; ++domain)
    {
        for (int bin = 0; bin < spectrumBins; ++bin)
        {
            const auto normalizedBin =
                static_cast<float>(bin)
                / static_cast<float>(juce::jmax(1, spectrumBins - 1));
            const auto hash = hashUnit(static_cast<std::uint32_t>(
                0x9e3779b9u
                + static_cast<std::uint32_t>(domain * 977 + bin * 131)));
            const auto motionCarrier = 0.67f * std::sin(
                                           motionPhaseNow
                                           + 0.045f * static_cast<float>(bin)
                                           + 0.8f * static_cast<float>(domain))
                + 0.33f * std::sin(
                    0.53f * motionPhaseNow + twoPi * hash);
            const auto localWarp = juce::jlimit(
                -1.0f, 1.0f,
                currentWarp
                    + currentMotionDepth
                          * smoothStep(normalizedBin) * 0.75f * motionCarrier);
            const auto sourceBin = mappedSourceBin(
                bin, pivotBin, localWarp, currentShiftSemitones);
            const auto lowBin = juce::jlimit(
                0, spectrumBins - 1,
                static_cast<int>(std::floor(sourceBin)));
            const auto highBin = juce::jlimit(
                0, spectrumBins - 1, lowBin + 1);
            const auto fraction =
                juce::jlimit(0.0f, 1.0f, sourceBin - static_cast<float>(lowBin));
            const auto source = inputDomains[static_cast<size_t>(domain)]
                                      [static_cast<size_t>(lowBin)]
                              + (inputDomains[static_cast<size_t>(domain)]
                                                [static_cast<size_t>(highBin)]
                                 - inputDomains[static_cast<size_t>(domain)]
                                                 [static_cast<size_t>(lowBin)])
                                    * fraction;
            auto magnitude = std::abs(source);
            auto& smearState =
                smearMagnitudeState[static_cast<size_t>(domain)]
                                   [static_cast<size_t>(bin)];
            smearState = juce::jmax(
                magnitude,
                smearCoefficient * smearState
                    + (1.0f - smearCoefficient) * magnitude);
            magnitude += (smearState - magnitude) * effectiveSmear;

            const auto advance = bin == 0 || bin == spectrumBins - 1
                ? 0.0f
                : lerp(
                    analysisAdvance[static_cast<size_t>(domain)]
                                   [static_cast<size_t>(lowBin)],
                    analysisAdvance[static_cast<size_t>(domain)]
                                   [static_cast<size_t>(highBin)],
                    fraction);
            const auto diffusionAdvance =
                effectiveDiffusion * smoothStep(normalizedBin) * 0.32f
                * std::sin(twoPi * hash + 0.37f * motionPhaseNow);

            auto phase = 0.0f;
            if (!spectralInitialised)
            {
                phase = (bin == 0 || bin == spectrumBins - 1 || magnitude <= 1.0e-9f)
                    ? 0.0f
                    : std::atan2(source.imag(), source.real());
                liveOutputPhase[static_cast<size_t>(domain)]
                               [static_cast<size_t>(bin)] = phase;
            }
            else
            {
                phase = principalPhase(
                    liveOutputPhase[static_cast<size_t>(domain)]
                                   [static_cast<size_t>(bin)]
                    + advance + diffusionAdvance);
                liveOutputPhase[static_cast<size_t>(domain)]
                               [static_cast<size_t>(bin)] = phase;
            }

            liveDomains[static_cast<size_t>(domain)]
                       [static_cast<size_t>(bin)] = polar(magnitude, phase);
            if (captureFreeze)
            {
                freezeMagnitude[static_cast<size_t>(domain)]
                               [static_cast<size_t>(bin)] = magnitude;
                freezePhaseAdvance[static_cast<size_t>(domain)]
                                  [static_cast<size_t>(bin)] =
                    advance + diffusionAdvance;
                freezeOutputPhase[static_cast<size_t>(domain)]
                                 [static_cast<size_t>(bin)] = phase;
            }
        }
    }

    spectralInitialised = true;
    if (freezeTarget)
        freezeLatched = true;
    freezeBlend += ((freezeTarget ? 1.0f : 0.0f) - freezeBlend)
                   * freezeCoefficient;
    const auto effectiveFreeze = freezeBlend * (1.0f - 0.9f * preserve);

    std::array<Complex, spectrumBins> leftSpectrum {};
    std::array<Complex, spectrumBins> rightSpectrum {};
    for (int bin = 0; bin < spectrumBins; ++bin)
    {
        auto mid = liveDomains[0][static_cast<size_t>(bin)];
        auto side = liveDomains[1][static_cast<size_t>(bin)];
        if (effectiveFreeze > 0.0001f)
        {
            for (int domain = 0; domain < spectralDomains; ++domain)
            {
                freezeOutputPhase[static_cast<size_t>(domain)]
                                 [static_cast<size_t>(bin)] = principalPhase(
                    freezeOutputPhase[static_cast<size_t>(domain)]
                                     [static_cast<size_t>(bin)]
                    + freezePhaseAdvance[static_cast<size_t>(domain)]
                                        [static_cast<size_t>(bin)]);
            }
            const auto frozenMid = polar(
                freezeMagnitude[0][static_cast<size_t>(bin)],
                freezeOutputPhase[0][static_cast<size_t>(bin)]);
            const auto frozenSide = polar(
                freezeMagnitude[1][static_cast<size_t>(bin)],
                freezeOutputPhase[1][static_cast<size_t>(bin)]);
            mid = mid + (frozenMid - mid) * effectiveFreeze;
            side = side + (frozenSide - side) * effectiveFreeze;
        }

        const auto transientBlend = preserve * smoothStep(
            static_cast<float>(bin) / static_cast<float>(spectrumBins - 1));
        mid = mid + (inputDomains[0][static_cast<size_t>(bin)] - mid)
                      * transientBlend;
        side = side + (inputDomains[1][static_cast<size_t>(bin)] - side)
                        * transientBlend * 0.75f;

        const auto normalizedBin =
            static_cast<float>(bin) / static_cast<float>(spectrumBins - 1);
        const auto spreadWeight =
            smoothStep((normalizedBin - 0.05f) / 0.35f)
            * currentSpread * (1.0f - 0.7f * preserve);
        const auto stereoHash = hashUnit(
            0x85ebca6bu + static_cast<std::uint32_t>(bin) * 193u);
        const auto stereoRotation = 0.55f * effectiveDiffusion
            * std::sin(twoPi * stereoHash + 0.41f * motionPhaseNow);
        const auto syntheticSide =
            mid * polar(1.0f, stereoRotation) * (spreadWeight * 0.26f);
        side *= 1.0f + 0.75f * spreadWeight;
        side += syntheticSide;

        leftSpectrum[static_cast<size_t>(bin)] = sanitizeComplex(mid + side);
        rightSpectrum[static_cast<size_t>(bin)] =
            sanitizeComplex(mid - side);

        if (bin == 0 || bin == spectrumBins - 1)
        {
            leftSpectrum[static_cast<size_t>(bin)] = {
                leftSpectrum[static_cast<size_t>(bin)].real(), 0.0f
            };
            rightSpectrum[static_cast<size_t>(bin)] = {
                rightSpectrum[static_cast<size_t>(bin)].real(), 0.0f
            };
        }
    }

    for (int channel = 0; channel < maxChannels; ++channel)
        for (auto& value : fftFrequencyDomain[static_cast<size_t>(channel)])
            value = {};

    for (int bin = 0; bin < spectrumBins; ++bin)
    {
        fftFrequencyDomain[0][static_cast<size_t>(bin)] =
            leftSpectrum[static_cast<size_t>(bin)];
        fftFrequencyDomain[1][static_cast<size_t>(bin)] =
            rightSpectrum[static_cast<size_t>(bin)];
    }
    for (int bin = 1; bin < spectrumBins - 1; ++bin)
    {
        fftFrequencyDomain[0][static_cast<size_t>(fftSize - bin)] =
            std::conj(leftSpectrum[static_cast<size_t>(bin)]);
        fftFrequencyDomain[1][static_cast<size_t>(fftSize - bin)] =
            std::conj(rightSpectrum[static_cast<size_t>(bin)]);
    }

    for (int channel = 0; channel < maxChannels; ++channel)
    {
        fft.perform(fftFrequencyDomain[static_cast<size_t>(channel)].data(),
                    fftTimeDomain[static_cast<size_t>(channel)].data(),
                    true);
        for (int index = 0; index < fftSize; ++index)
        {
            const auto sample = sanitizeSample(
                fftTimeDomain[static_cast<size_t>(channel)]
                             [static_cast<size_t>(index)]
                    .real()
                * synthesisWindow[static_cast<size_t>(index)]);
            const auto ringIndex =
                (wetReadPosition + 1 + index) % wetRingSize;
            auto& destination =
                wetRing[static_cast<size_t>(channel)]
                       [static_cast<size_t>(ringIndex)];
            destination = sanitizeSample(destination + sample);
        }
    }
}

float SpectralPrismModule::mappedSourceBin(
    int destinationBin, float pivotBin, float warp, float shiftSemitones) const
{
    if (destinationBin <= 0)
        return 0.0f;

    const auto safePivot = juce::jlimit(
        1.0f, static_cast<float>(spectrumBins - 2), pivotBin);
    const auto destination = juce::jlimit(
        1.0f, static_cast<float>(spectrumBins - 1),
        static_cast<float>(destinationBin));
    const auto relative = std::log2(destination / safePivot);
    const auto amount = juce::jlimit(-1.0f, 1.0f, warp);
    const auto upperScale = std::exp2(amount * 1.2f);
    const auto lowerScale = std::exp2(-amount * 1.2f);
    const auto mappedRelative = (relative >= 0.0f
                                     ? relative * upperScale
                                     : relative * lowerScale)
        - shiftSemitones / 12.0f;
    return juce::jlimit(
        0.0f, static_cast<float>(spectrumBins - 1),
        safePivot * std::exp2(mappedRelative));
}

float SpectralPrismModule::normalized(float value, float fallback)
{
    return std::isfinite(value) ? juce::jlimit(0.0f, 1.0f, value) : fallback;
}

float SpectralPrismModule::bipolar(float value)
{
    return lerp(-1.0f, 1.0f, normalized(value, 0.5f));
}

float SpectralPrismModule::semitones(float value)
{
    return lerp(-24.0f, 24.0f, normalized(value, 0.5f));
}

float SpectralPrismModule::sanitizeSample(float value)
{
    if (!std::isfinite(value))
        return 0.0f;
    return juce::jlimit(-64.0f, 64.0f, value);
}

float SpectralPrismModule::principalPhase(float phase)
{
    while (phase > pi)
        phase -= twoPi;
    while (phase < -pi)
        phase += twoPi;
    return phase;
}

float SpectralPrismModule::hashUnit(std::uint32_t seed)
{
    seed ^= seed >> 16;
    seed *= 0x7feb352du;
    seed ^= seed >> 15;
    seed *= 0x846ca68bu;
    seed ^= seed >> 16;
    return static_cast<float>(seed & 0x00ffffffu) * (1.0f / 16777216.0f);
}

SpectralPrismModule::Complex SpectralPrismModule::polar(
    float magnitude, float phase)
{
    return { magnitude * std::cos(phase), magnitude * std::sin(phase) };
}
} // namespace megadsp
