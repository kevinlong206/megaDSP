#pragma once

#include "DspModule.h"

namespace megadsp
{
class GuitarTunerModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    static constexpr int viewCount = 3;
    static constexpr int tuningCount = 7;
    static constexpr int rangeCount = 5;

    enum Control
    {
        referenceControl,
        viewControl,
        tuningControl,
        transposeControl,
        inputGainControl,
        gateControl,
        responseControl,
        rangeControl,
        strobeSpeedControl,
        muteControl,
        outputControl,
        holdControl
    };

    enum TelemetryValue
    {
        detectedFrequencyHz,
        targetFrequencyHz,
        centsError,
        trackingConfidence,
        inputLevelDb,
        targetMidiNote,
        detectedMidiNote,
        strobePhase,
        activeView,
        activeTuning,
        displayRangeCents,
        inTune,
        telemetryValueCount
    };

    enum TelemetryHistory
    {
        centsHistory,
        confidenceHistory,
        levelHistory,
        phaseHistory,
        telemetryHistoryValueCount
    };

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    bool readContinuousTelemetry(
        ContinuousTelemetrySnapshot&) const noexcept override;
    ContinuousTelemetryCapability*
        continuousTelemetryCapability() noexcept override { return this; }
    const ContinuousTelemetryCapability*
        continuousTelemetryCapability() const noexcept override { return this; }
    float meterValue() const override
    {
        return meter.load(std::memory_order_relaxed);
    }

private:
    static constexpr int historySize = 4096;
    static constexpr int maximumLagCount = historySize / 2;

    void pushAnalysisSample(float) noexcept;
    void analysePitch(
        float gateDb, float responseSeconds, float referenceHz,
        int tuning, int transpose, float holdSeconds) noexcept;
    int nearestTargetMidi(
        float frequency, float referenceHz, int tuning,
        int transpose) const noexcept;
    float sampleAtAge(int age) const noexcept;

    std::array<float, historySize> history {};
    std::array<float, maximumLagCount + 1> correlations {};
    double sampleRate = 44100.0;
    float detectorRate = 11025.0f;
    float lowpassState = 0.0f;
    float dcState = 0.0f;
    float lowpassPole = 0.0f;
    float dcPole = 0.0f;
    float inputEnvelope = 0.0f;
    float trackedFrequency = 0.0f;
    float targetFrequency = 0.0f;
    float currentCents = 0.0f;
    float confidence = 0.0f;
    float strobePosition = 0.0f;
    float holdRemainingSeconds = 0.0f;
    int targetMidi = 69;
    int detectedMidi = 69;
    int decimation = 4;
    int decimationCounter = 0;
    int analysisCounter = 0;
    int writePosition = 0;
    int samplesWritten = 0;
    bool initialized = false;
    std::uint64_t telemetrySequence = 0;
    ContinuousTelemetrySnapshot telemetryState;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        outputGainSmoothed;
    std::atomic<float> meter { -100.0f };
};
} // namespace megadsp
