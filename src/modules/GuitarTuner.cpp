#include "GuitarTuner.h"
#include "DspHelpers.h"
#include "DspSafety.h"

#include <cmath>

namespace megadsp
{
namespace
{
constexpr float minimumFrequency = 20.0f;
constexpr float maximumFrequency = 1600.0f;
constexpr int analysisHop = 96;
constexpr int analysisWindow = 768;

constexpr std::array<std::array<int, 6>, 6> tuningNotes {{
    { 40, 45, 50, 55, 59, 64 },
    { 28, 33, 38, 43, 47, 52 },
    { 38, 45, 50, 55, 59, 64 },
    { 38, 45, 50, 54, 57, 62 },
    { 38, 43, 50, 55, 59, 62 },
    { 38, 45, 50, 55, 57, 62 }
}};

constexpr std::array<float, GuitarTunerModule::rangeCount> displayRanges {
    2.0f, 5.0f, 10.0f, 25.0f, 50.0f
};

float midiForFrequency(float frequency, float referenceHz) noexcept
{
    return 69.0f + 12.0f * std::log2(
        juce::jmax(1.0e-6f, frequency)
        / juce::jmax(1.0f, referenceHz));
}

float frequencyForMidi(int midi, float referenceHz) noexcept
{
    return referenceHz
        * std::exp2((static_cast<float>(midi) - 69.0f) / 12.0f);
}
} // namespace

void GuitarTunerModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    decimation = juce::jmax(
        1, juce::roundToInt(sampleRate / 12000.0));
    detectorRate =
        static_cast<float>(sampleRate / static_cast<double>(decimation));
    lowpassPole = std::exp(
        -juce::MathConstants<float>::twoPi * 1800.0f
        / detectorRate);
    dcPole = std::exp(
        -juce::MathConstants<float>::twoPi * 18.0f
        / detectorRate);
    outputGainSmoothed.reset(sampleRate, 0.012);
    reset();
}

void GuitarTunerModule::reset()
{
    history.fill(0.0f);
    correlations.fill(0.0f);
    lowpassState = 0.0f;
    dcState = 0.0f;
    inputEnvelope = 0.0f;
    trackedFrequency = 0.0f;
    targetFrequency = 0.0f;
    currentCents = 0.0f;
    confidence = 0.0f;
    strobePosition = 0.0f;
    holdRemainingSeconds = 0.0f;
    targetMidi = 69;
    detectedMidi = 69;
    decimationCounter = 0;
    analysisCounter = 0;
    writePosition = 0;
    samplesWritten = 0;
    initialized = false;
    telemetrySequence = 0;
    telemetryState = {};
    telemetry.clear();
    outputGainSmoothed.setCurrentAndTargetValue(1.0f);
    meter.store(-100.0f, std::memory_order_relaxed);
}

float GuitarTunerModule::sampleAtAge(int age) const noexcept
{
    auto index = writePosition - 1 - age;
    if (index < 0)
        index += historySize * (1 + (-index / historySize));
    return history[static_cast<size_t>(index % historySize)];
}

void GuitarTunerModule::pushAnalysisSample(float input) noexcept
{
    if (++decimationCounter < decimation)
        return;
    decimationCounter = 0;

    lowpassState = lowpassPole * lowpassState
        + (1.0f - lowpassPole) * input;
    dcState = dcPole * dcState
        + (1.0f - dcPole) * lowpassState;
    history[static_cast<size_t>(writePosition)] =
        detail::finiteSample(lowpassState - dcState);
    writePosition = (writePosition + 1) % historySize;
    samplesWritten = juce::jmin(historySize, samplesWritten + 1);
    ++analysisCounter;
}

int GuitarTunerModule::nearestTargetMidi(
    float frequency, float referenceHz, int tuning,
    int transpose) const noexcept
{
    const auto chromatic =
        juce::roundToInt(midiForFrequency(frequency, referenceHz));
    if (tuning <= 0)
        return juce::jlimit(0, 127, chromatic + transpose);

    const auto& notes = tuningNotes[
        static_cast<size_t>(juce::jlimit(1, tuningCount - 1, tuning) - 1)];
    auto best = notes[0] + transpose;
    auto bestDistance = std::abs(chromatic - best);
    for (const auto note : notes)
    {
        const auto candidate = note + transpose;
        const auto distance = std::abs(chromatic - candidate);
        if (distance < bestDistance)
        {
            best = candidate;
            bestDistance = distance;
        }
    }
    return juce::jlimit(0, 127, best);
}

void GuitarTunerModule::analysePitch(
    float gateDb, float responseSeconds, float referenceHz,
    int tuning, int transpose, float holdSeconds) noexcept
{
    analysisCounter = 0;
    const auto levelDb = juce::Decibels::gainToDecibels(
        juce::jmax(1.0e-9f, inputEnvelope), -120.0f);
    const auto minimumLag = juce::jmax(
        2, static_cast<int>(detectorRate / maximumFrequency));
    const auto maximumLag = juce::jmin(
        maximumLagCount - 2,
        static_cast<int>(detectorRate / minimumFrequency));
    if (samplesWritten < maximumLag + analysisWindow + 2
        || levelDb < gateDb)
    {
        confidence *= 0.82f;
        return;
    }

    auto strongest = -1.0f;
    auto strongestLag = minimumLag;
    for (int lag = minimumLag; lag <= maximumLag; ++lag)
    {
        double product = 0.0;
        double energyA = 1.0e-18;
        double energyB = 1.0e-18;
        for (int sample = 0; sample < analysisWindow; ++sample)
        {
            const auto current = sampleAtAge(sample);
            const auto delayed = sampleAtAge(sample + lag);
            product += static_cast<double>(current) * delayed;
            energyA += static_cast<double>(current) * current;
            energyB += static_cast<double>(delayed) * delayed;
        }
        const auto correlation = detail::finiteSample(static_cast<float>(
            product / std::sqrt(energyA * energyB)));
        correlations[static_cast<size_t>(lag)] = correlation;
        if (correlation > strongest)
        {
            strongest = correlation;
            strongestLag = lag;
        }
    }

    auto selectedLag = strongestLag;
    const auto threshold = juce::jmax(0.68f, strongest * 0.90f);
    for (int lag = minimumLag + 1; lag < maximumLag; ++lag)
    {
        const auto centre = correlations[static_cast<size_t>(lag)];
        if (centre >= threshold
            && centre > correlations[static_cast<size_t>(lag - 1)]
            && centre >= correlations[static_cast<size_t>(lag + 1)])
        {
            selectedLag = lag;
            break;
        }
    }

    const auto centre = correlations[static_cast<size_t>(selectedLag)];
    const auto left = correlations[static_cast<size_t>(selectedLag - 1)];
    const auto right = correlations[static_cast<size_t>(selectedLag + 1)];
    const auto denominator = left - 2.0f * centre + right;
    const auto fraction = std::abs(denominator) > 1.0e-8f
        ? juce::jlimit(
              -0.5f, 0.5f,
              0.5f * (left - right) / denominator)
        : 0.0f;
    const auto candidate = detectorRate
        / (static_cast<float>(selectedLag) + fraction);
    const auto levelConfidence = juce::jlimit(
        0.0f, 1.0f, (levelDb - gateDb) / 18.0f);
    const auto periodicConfidence = juce::jlimit(
        0.0f, 1.0f, (centre - 0.55f) / 0.43f);
    const auto candidateConfidence =
        juce::jmin(levelConfidence, periodicConfidence);
    if (!std::isfinite(candidate)
        || candidate < minimumFrequency
        || candidate > maximumFrequency
        || candidateConfidence < 0.18f)
    {
        confidence *= 0.82f;
        return;
    }

    const auto responseBlend = juce::jlimit(
        0.02f, 1.0f,
        1.0f - std::exp(
            -static_cast<float>(analysisHop)
            / (detectorRate * responseSeconds)));
    if (trackedFrequency <= 0.0f
        || confidence < 0.12f
        || std::abs(
               1200.0f * std::log2(candidate / trackedFrequency))
               > 140.0f)
        trackedFrequency = candidate;
    else
        trackedFrequency *= std::exp(
            responseBlend
            * std::log(candidate / trackedFrequency));
    confidence += juce::jlimit(
        0.12f, 0.72f, responseBlend * 5.0f)
        * (candidateConfidence - confidence);
    detectedMidi = juce::jlimit(
        0, 127,
        juce::roundToInt(
            midiForFrequency(trackedFrequency, referenceHz)));
    targetMidi = nearestTargetMidi(
        trackedFrequency, referenceHz, tuning, transpose);
    targetFrequency = frequencyForMidi(targetMidi, referenceHz);
    currentCents = 1200.0f * std::log2(
        trackedFrequency / targetFrequency);
    holdRemainingSeconds = holdSeconds;
}

void GuitarTunerModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    juce::ScopedNoDenormals noDenormals;
    const auto channels = juce::jmin(2, buffer.getNumChannels());
    if (channels <= 0 || buffer.getNumSamples() <= 0)
        return;

    const auto referenceHz = detail::lerp(
        430.0f, 450.0f,
        detail::normalizedControl(controls[referenceControl], 0.5f));
    const auto view = discreteIndex(
        detail::normalizedControl(controls[viewControl], 0.1667f),
        viewCount);
    const auto tuning = discreteIndex(
        detail::normalizedControl(controls[tuningControl], 0.0714f),
        tuningCount);
    const auto transpose = juce::roundToInt(detail::lerp(
        -12.0f, 12.0f,
        detail::normalizedControl(controls[transposeControl], 0.5f)));
    const auto inputGain = juce::Decibels::decibelsToGain(detail::lerp(
        -24.0f, 24.0f,
        detail::normalizedControl(controls[inputGainControl], 0.5f)));
    const auto gateDb = detail::lerp(
        -72.0f, -24.0f,
        detail::normalizedControl(controls[gateControl], 0.25f));
    const auto responseSeconds = detail::exponential(
        0.04f, 0.60f,
        detail::normalizedControl(controls[responseControl], 0.42f));
    const auto range = displayRanges[static_cast<size_t>(discreteIndex(
        detail::normalizedControl(controls[rangeControl], 0.9f),
        rangeCount))];
    const auto strobeSpeed = detail::lerp(
        0.5f, 5.0f,
        detail::normalizedControl(controls[strobeSpeedControl], 0.45f));
    const auto muted =
        detail::normalizedControl(controls[muteControl], 0.0f) >= 0.5f;
    const auto outputGain = muted ? 0.0f
        : juce::Decibels::decibelsToGain(detail::lerp(
              -24.0f, 6.0f,
              detail::normalizedControl(controls[outputControl], 0.8f)));
    const auto holdSeconds = detail::lerp(
        0.0f, 2.0f,
        detail::normalizedControl(controls[holdControl], 0.3f));
    auto detectorChannel = 0;
    if (channels > 1)
    {
        double leftEnergy = 0.0;
        double rightEnergy = 0.0;
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto left =
                detail::finiteSample(buffer.getSample(0, sample));
            const auto right =
                detail::finiteSample(buffer.getSample(1, sample));
            leftEnergy += static_cast<double>(left) * left;
            rightEnergy += static_cast<double>(right) * right;
        }
        detectorChannel = rightEnergy > leftEnergy ? 1 : 0;
    }
    outputGainSmoothed.setTargetValue(outputGain);
    if (!initialized)
    {
        outputGainSmoothed.setCurrentAndTargetValue(outputGain);
        initialized = true;
    }

    const auto envelopeAttack = 1.0f
        - static_cast<float>(std::exp(-1.0 / (sampleRate * 0.004)));
    const auto envelopeRelease = 1.0f
        - static_cast<float>(std::exp(-1.0 / (sampleRate * 0.18)));
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto mono = detail::finiteSample(
            buffer.getSample(detectorChannel, sample)) * inputGain;
        const auto magnitude = std::abs(mono);
        inputEnvelope += (magnitude > inputEnvelope
                ? envelopeAttack : envelopeRelease)
            * (magnitude - inputEnvelope);
        pushAnalysisSample(mono);
        if (analysisCounter >= analysisHop)
            analysePitch(
                gateDb, responseSeconds, referenceHz,
                tuning, transpose, holdSeconds);

        if (confidence < 0.12f)
        {
            holdRemainingSeconds = juce::jmax(
                0.0f,
                holdRemainingSeconds
                    - 1.0f / static_cast<float>(sampleRate));
            if (holdRemainingSeconds <= 0.0f)
            {
                trackedFrequency = 0.0f;
                targetFrequency = 0.0f;
                currentCents = 0.0f;
            }
        }
        const auto centsVelocity =
            trackedFrequency > 0.0f ? currentCents / range : 0.0f;
        strobePosition = std::fmod(
            strobePosition
                + centsVelocity * strobeSpeed
                    / static_cast<float>(sampleRate),
            1.0f);
        if (strobePosition < 0.0f)
            strobePosition += 1.0f;

        const auto gain = outputGainSmoothed.getNextValue();
        for (int channel = 0; channel < channels; ++channel)
            buffer.setSample(
                channel, sample,
                detail::finiteSample(
                    buffer.getSample(channel, sample) * gain));
    }

    const auto levelDb = juce::Decibels::gainToDecibels(
        juce::jmax(1.0e-9f, inputEnvelope), -120.0f);
    meter.store(levelDb, std::memory_order_relaxed);
    if (environment.captureTelemetry)
    {
        telemetryState.sequence = ++telemetrySequence;
        telemetryState.valueCount = telemetryValueCount;
        telemetryState.values[detectedFrequencyHz] = trackedFrequency;
        telemetryState.values[targetFrequencyHz] = targetFrequency;
        telemetryState.values[centsError] = currentCents;
        telemetryState.values[trackingConfidence] = confidence;
        telemetryState.values[inputLevelDb] = levelDb;
        telemetryState.values[targetMidiNote] =
            static_cast<float>(targetMidi);
        telemetryState.values[detectedMidiNote] =
            static_cast<float>(detectedMidi);
        telemetryState.values[strobePhase] = strobePosition;
        telemetryState.values[activeView] = static_cast<float>(view);
        telemetryState.values[activeTuning] =
            static_cast<float>(tuning);
        telemetryState.values[displayRangeCents] = range;
        telemetryState.values[inTune] =
            confidence >= 0.45f && std::abs(currentCents) <= 0.5f
            ? 1.0f : 0.0f;
        appendContinuousTelemetryHistory(
            telemetryState,
            { currentCents, confidence, levelDb, strobePosition },
            telemetryHistoryValueCount);
        telemetry.publish(telemetryState);
    }
}

bool GuitarTunerModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}
} // namespace megadsp
