#pragma once

#include "DspModule.h"

namespace megadsp
{
class ResonantSculptureModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    static constexpr int modeCount = 12;
    static constexpr int materialCount = 4;

    enum Control
    {
        materialControl,
        shapeControl,
        tuningControl,
        excitationControl,
        decayControl,
        mutationControl,
        bowControl,
        strikeControl,
        stereoFieldControl,
        chaosControl,
        mixControl,
        outputControl
    };

    enum TelemetryValue
    {
        overallEnergy,
        transientAmount,
        bowAmount,
        chaosPosition,
        materialIndex,
        shapePosition,
        trackedFrequencyHz,
        trackingConfidence,
        firstModeEnergy,
        telemetryValueCount = firstModeEnergy + 8
    };

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    double tailSeconds(const ControlValues&) const override;
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
    struct Oscillator
    {
        float real = 0.0f;
        float imaginary = 0.0f;
    };

    static constexpr int pitchHistorySize = 1024;
    void pushPitchSample(float) noexcept;
    void analysePitch() noexcept;

    std::array<std::array<Oscillator, modeCount>, 2> modes {};
    std::array<float, modeCount> mutationPhases {};
    std::array<float, modeCount> modeEnergyState {};
    std::array<float, pitchHistorySize> pitchHistory {};
    std::array<juce::SmoothedValue<float>, controlsPerSlot> smoothed {};
    std::array<juce::SmoothedValue<float>, materialCount> materialWeights {};
    double sampleRate = 44100.0;
    float pitchDetectorRate = 6000.0f;
    float pitchLowPass = 0.0f;
    float pitchDc = 0.0f;
    float pitchLowPassCoefficient = 0.0f;
    float pitchDcCoefficient = 0.0f;
    float trackedFrequency = 0.0f;
    float targetFrequency = 0.0f;
    float pitchConfidence = 0.0f;
    float pitchGate = 0.0f;
    float fastEnvelope = 0.0f;
    float slowEnvelope = 0.0f;
    float chaosPhaseA = 0.0f;
    float chaosPhaseB = 0.0f;
    float meterEnvelope = 0.0f;
    int pitchDecimation = 8;
    int pitchDecimationCounter = 0;
    int pitchAnalysisCounter = 0;
    int pitchWritePosition = 0;
    int pitchSamplesWritten = 0;
    bool initialized = false;
    std::uint64_t telemetrySequence = 0;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
    std::atomic<float> meter { 0.0f };
};
} // namespace megadsp
