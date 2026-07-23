#pragma once

#include "DspModule.h"
#include "SpectralFoundation.h"

namespace megadsp
{
class HarmonicMirageModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    static constexpr int modeCount = 4;
    static constexpr int trackingStyleCount = 3;
    static constexpr int minimumPartials = 2;
    static constexpr int maximumPartials = 24;

    enum ControlIndex
    {
        modeControl,
        trackingControl,
        partialsControl,
        evenOddControl,
        inharmonicityControl,
        fineDriftControl,
        responseControl,
        transientPreserveControl,
        stereoSpreadControl,
        mixControl,
        outputControl
    };

    enum Mode
    {
        harmonicMode,
        subharmonicMode,
        hollowMode,
        metallicMode
    };

    enum TrackingStyle
    {
        looseTracking,
        musicalTracking,
        tightTracking
    };

    enum TelemetryValue
    {
        trackedFrequencyHz,
        generatedFrequencyHz,
        trackingConfidence,
        activePartialCount,
        transientActivity,
        generatedRms,
        sourceRms,
        activeMode,
        telemetryValueCount
    };

    enum TelemetryHistory
    {
        trackedFrequencyHistory,
        generatedFrequencyHistory,
        confidenceHistory,
        generatedLevelHistory,
        telemetryHistoryValueCount
    };

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    int latencySamples() const override
    {
        return FixedLatencyStft::reportedLatencySamples;
    }
    double tailSeconds(const ControlValues&) const override;
    float meterValue() const override;
    float detectorValue() const override;
    bool readContinuousTelemetry(
        ContinuousTelemetrySnapshot&) const noexcept override;
    ContinuousTelemetryCapability*
        continuousTelemetryCapability() noexcept override { return this; }
    const ContinuousTelemetryCapability*
        continuousTelemetryCapability() const noexcept override { return this; }

private:
    struct Oscillator
    {
        double phase = 0.0;
        double driftPhase = 0.0;
        float frequency = 0.0f;
        float targetFrequency = 0.0f;
        float amplitude = 0.0f;
        float targetAmplitude = 0.0f;
        std::array<float, 2> channelGain { 1.0f, 1.0f };
    };

    static void processFrameCallback(
        void*, FixedLatencyStft::Complex* const*, int, int) noexcept;
    static float outputCallback(void*, int, float, float) noexcept;
    void processFrame(
        FixedLatencyStft::Complex* const*, int, int) noexcept;
    void updateOscillatorTargets(float, float,
                                 const std::array<float, 2>&) noexcept;
    void renderOscillators() noexcept;
    float outputSample(int, float) noexcept;
    void publishTelemetry(bool) noexcept;

    FixedLatencyStft stft;
    std::vector<float> linkedMagnitude;
    std::vector<float> previousMagnitude;
    std::vector<float> linkedPhase;
    std::vector<float> previousPhase;
    std::vector<float> candidateScore;
    std::array<Oscillator, maximumPartials> oscillators;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
    double sampleRate = 44100.0;
    double blockSourceEnergy = 0.0;
    double blockGeneratedEnergy = 0.0;
    double blockOutputEnergy = 0.0;
    std::array<float, 2> renderedSample {};
    float trackedFrequency = 0.0f;
    float trackedAmplitude = 0.0f;
    float confidence = 0.0f;
    float transientState = 0.0f;
    float currentEvenOdd = 0.5f;
    float currentInharmonicity = 0.1f;
    float currentDriftCents = 3.0f;
    float currentResponseSeconds = 0.2f;
    float currentTransientPreserve = 0.65f;
    float currentStereoSpread = 0.35f;
    float currentMix = 0.35f;
    float currentOutput = 1.0f;
    float smoothingCoefficient = 0.0f;
    float generatedLevel = 0.0f;
    float sourceLevel = 0.0f;
    int currentMode = harmonicMode;
    int currentTrackingStyle = musicalTracking;
    int currentPartialCount = 8;
    int currentChannelCount = 2;
    int generatedPartialCount = 0;
    int releaseFrames = 0;
    bool trackingLocked = false;
    bool phaseHistoryValid = false;
    bool parametersInitialised = false;
    std::atomic<float> outputMeter { 0.0f };
    std::atomic<float> detectorMeter { -100.0f };
    ContinuousTelemetrySnapshot telemetryState;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
};
} // namespace megadsp
