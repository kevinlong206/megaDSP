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
constexpr int analysisHop = 384;
constexpr int analysisWindow = 768;

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

GuitarTunerModule::GuitarTunerModule()
    : juce::Thread("megaDSP tuner analysis")
{
}

GuitarTunerModule::~GuitarTunerModule()
{
    signalThreadShouldExit();
    stopThread(2000);
}

void GuitarTunerModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    signalThreadShouldExit();
    stopThread(2000);
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
    reset();
    startThread(juce::Thread::Priority::low);
}

void GuitarTunerModule::reset()
{
    lowpassState = 0.0f;
    dcState = 0.0f;
    decimationCounter = 0;
    captureWasEnabled = false;
    const auto write = inputWrite.load(std::memory_order_relaxed);
    inputRead.store(write, std::memory_order_release);
    resetGeneration.fetch_add(1, std::memory_order_release);
    telemetry.clear();
    meter.store(-100.0f, std::memory_order_relaxed);
}

void GuitarTunerModule::resetAnalysisState() noexcept
{
    history.fill(0.0f);
    correlations.fill(0.0f);
    inputEnvelope = 0.0f;
    trackedFrequency = 0.0f;
    targetFrequency = 0.0f;
    currentCents = 0.0f;
    confidence = 0.0f;
    holdRemainingSeconds = 0.0f;
    targetMidi = 69;
    detectedMidi = 69;
    analysisCounter = 0;
    writePosition = 0;
    samplesWritten = 0;
    telemetrySequence = 0;
    telemetryState = {};
}

float GuitarTunerModule::sampleAtAge(int age) const noexcept
{
    auto index = writePosition - 1 - age;
    if (index < 0)
        index += historySize * (1 + (-index / historySize));
    return history[static_cast<size_t>(index % historySize)];
}

void GuitarTunerModule::consumeAnalysisSample(float input) noexcept
{
    history[static_cast<size_t>(writePosition)] =
        detail::finiteSample(input);
    writePosition = (writePosition + 1) % historySize;
    samplesWritten = juce::jmin(historySize, samplesWritten + 1);
    ++analysisCounter;
    const auto magnitude = std::abs(input);
    const auto envelopeAttack = 1.0f
        - std::exp(-1.0f / (detectorRate * 0.004f));
    const auto envelopeRelease = 1.0f
        - std::exp(-1.0f / (detectorRate * 0.18f));
    inputEnvelope += (magnitude > inputEnvelope
            ? envelopeAttack : envelopeRelease)
        * (magnitude - inputEnvelope);

    if (analysisCounter >= analysisHop)
        analysePitch();

    if (confidence < 0.12f)
    {
        holdRemainingSeconds = juce::jmax(
            0.0f, holdRemainingSeconds - 1.0f / detectorRate);
        if (holdRemainingSeconds <= 0.0f)
        {
            trackedFrequency = 0.0f;
            targetFrequency = 0.0f;
            currentCents = 0.0f;
        }
    }
}

void GuitarTunerModule::analysePitch() noexcept
{
    analysisCounter = 0;
    const auto currentGateDb = gateDb.load(std::memory_order_relaxed);
    const auto currentResponseSeconds =
        responseSeconds.load(std::memory_order_relaxed);
    const auto currentReferenceHz =
        referenceHz.load(std::memory_order_relaxed);
    const auto levelDb = juce::Decibels::gainToDecibels(
        juce::jmax(1.0e-9f, inputEnvelope), -120.0f);
    const auto minimumLag = juce::jmax(
        2, static_cast<int>(detectorRate / maximumFrequency));
    const auto maximumLag = juce::jmin(
        maximumLagCount - 2,
        static_cast<int>(detectorRate / minimumFrequency));
    if (samplesWritten < maximumLag + analysisWindow + 2
        || levelDb < currentGateDb)
    {
        confidence *= 0.82f;
        publishTelemetry();
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
        0.0f, 1.0f, (levelDb - currentGateDb) / 18.0f);
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
        publishTelemetry();
        return;
    }

    const auto responseBlend = juce::jlimit(
        0.02f, 1.0f,
        1.0f - std::exp(
            -static_cast<float>(analysisHop)
            / (detectorRate * currentResponseSeconds)));
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
            midiForFrequency(trackedFrequency, currentReferenceHz)));
    targetMidi = detectedMidi;
    targetFrequency =
        frequencyForMidi(targetMidi, currentReferenceHz);
    currentCents = 1200.0f * std::log2(
        trackedFrequency / targetFrequency);
    holdRemainingSeconds = holdSeconds.load(std::memory_order_relaxed);
    publishTelemetry();
}

void GuitarTunerModule::publishTelemetry() noexcept
{
    const auto levelDb = juce::Decibels::gainToDecibels(
        juce::jmax(1.0e-9f, inputEnvelope), -120.0f);
    meter.store(levelDb, std::memory_order_relaxed);
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
    telemetryState.values[strobePhase] = 0.0f;
    telemetryState.values[activeView] = 1.0f;
    telemetryState.values[activeTuning] = 0.0f;
    telemetryState.values[displayRangeCents] =
        displayRange.load(std::memory_order_relaxed);
    telemetryState.values[inTune] =
        confidence >= 0.45f && std::abs(currentCents) <= 0.5f
        ? 1.0f : 0.0f;
    appendContinuousTelemetryHistory(
        telemetryState,
        { currentCents, confidence, levelDb, 0.0f },
        telemetryHistoryValueCount);
    telemetry.publish(telemetryState);
}

void GuitarTunerModule::run()
{
    auto observedReset =
        resetGeneration.load(std::memory_order_acquire);
    resetAnalysisState();
    while (!threadShouldExit())
    {
        const auto requestedReset =
            resetGeneration.load(std::memory_order_acquire);
        if (requestedReset != observedReset)
        {
            observedReset = requestedReset;
            resetAnalysisState();
        }

        auto read = inputRead.load(std::memory_order_relaxed);
        const auto write = inputWrite.load(std::memory_order_acquire);
        if (read == write)
        {
            juce::Thread::sleep(2);
            continue;
        }
        while (read < write)
        {
            consumeAnalysisSample(
                inputQueue[static_cast<size_t>(
                    read % inputQueueCapacity)]);
            ++read;
        }
        inputRead.store(read, std::memory_order_release);
    }
}

void GuitarTunerModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    juce::ScopedNoDenormals noDenormals;
    const auto channels = juce::jmin(2, buffer.getNumChannels());
    if (channels <= 0 || buffer.getNumSamples() <= 0)
        return;

    referenceHz.store(detail::lerp(
        430.0f, 450.0f,
        detail::normalizedControl(controls[referenceControl], 0.5f)),
        std::memory_order_relaxed);
    inputGain.store(juce::Decibels::decibelsToGain(detail::lerp(
        -24.0f, 24.0f,
        detail::normalizedControl(controls[inputGainControl], 0.5f))),
        std::memory_order_relaxed);
    gateDb.store(detail::lerp(
        -72.0f, -24.0f,
        detail::normalizedControl(controls[gateControl], 0.25f)),
        std::memory_order_relaxed);
    responseSeconds.store(detail::exponential(
        0.04f, 0.60f,
        detail::normalizedControl(controls[responseControl], 0.42f)),
        std::memory_order_relaxed);
    displayRange.store(displayRanges[static_cast<size_t>(discreteIndex(
        detail::normalizedControl(controls[rangeControl], 0.9f),
        rangeCount))], std::memory_order_relaxed);
    holdSeconds.store(detail::lerp(
        0.0f, 2.0f,
        detail::normalizedControl(controls[holdControl], 0.3f)),
        std::memory_order_relaxed);

    if (!environment.captureTelemetry)
    {
        captureWasEnabled = false;
        return;
    }
    if (!captureWasEnabled)
    {
        captureWasEnabled = true;
        resetGeneration.fetch_add(1, std::memory_order_release);
    }

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
    auto write = inputWrite.load(std::memory_order_relaxed);
    const auto read = inputRead.load(std::memory_order_acquire);
    const auto gain = inputGain.load(std::memory_order_relaxed);
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        if (++decimationCounter < decimation)
            continue;
        decimationCounter = 0;
        const auto input = detail::finiteSample(
            buffer.getSample(detectorChannel, sample)) * gain;
        lowpassState = lowpassPole * lowpassState
            + (1.0f - lowpassPole) * input;
        dcState = dcPole * dcState
            + (1.0f - dcPole) * lowpassState;
        if (write - read >= inputQueueCapacity)
            continue;
        inputQueue[static_cast<size_t>(write % inputQueueCapacity)] =
            detail::finiteSample(lowpassState - dcState);
        ++write;
    }
    inputWrite.store(write, std::memory_order_release);
}

bool GuitarTunerModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}
} // namespace megadsp
