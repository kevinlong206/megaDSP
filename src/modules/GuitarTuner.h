#pragma once

#include "DspModule.h"

namespace megadsp
{
class GuitarTunerModule final
    : public DspModule,
      public ContinuousTelemetryCapability,
      private juce::Thread
{
public:
    static constexpr int viewCount = 3;
    static constexpr int tuningCount = 7;
    static constexpr int rangeCount = 5;

    GuitarTunerModule();
    ~GuitarTunerModule() override;

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
    static constexpr int inputQueueCapacity = 16384;

    void run() override;
    void resetAnalysisState() noexcept;
    void consumeAnalysisSample(float) noexcept;
    void analysePitch() noexcept;
    void publishTelemetry() noexcept;
    float sampleAtAge(int age) const noexcept;

    std::array<float, inputQueueCapacity> inputQueue {};
    std::array<float, historySize> history {};
    std::array<float, maximumLagCount + 1> correlations {};
    std::atomic<std::uint64_t> inputWrite { 0 };
    std::atomic<std::uint64_t> inputRead { 0 };
    std::atomic<std::uint32_t> resetGeneration { 0 };
    std::atomic<float> referenceHz { 440.0f };
    std::atomic<float> inputGain { 1.0f };
    std::atomic<float> gateDb { -60.0f };
    std::atomic<float> responseSeconds { 0.12f };
    std::atomic<float> displayRange { 50.0f };
    std::atomic<float> holdSeconds { 0.6f };
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
    float holdRemainingSeconds = 0.0f;
    int targetMidi = 69;
    int detectedMidi = 69;
    int decimation = 4;
    int decimationCounter = 0;
    int analysisCounter = 0;
    int writePosition = 0;
    int samplesWritten = 0;
    bool captureWasEnabled = false;
    std::uint64_t telemetrySequence = 0;
    ContinuousTelemetrySnapshot telemetryState;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
    std::atomic<float> meter { -100.0f };
};
} // namespace megadsp
