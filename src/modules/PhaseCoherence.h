#pragma once

#include "DspModule.h"

namespace megadsp
{
class PhaseCoherenceModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    enum ControlIndex
    {
        rangeControl,
        crossoverControl,
        correctionControl,
        maxAlignmentControl,
        phaseRotationControl,
        stereoPreserveControl,
        monoBelowControl,
        outputControl
    };

    enum TelemetryValue
    {
        correlationBefore,
        correlationAfter,
        estimatedDelayMilliseconds,
        appliedDelayMilliseconds,
        estimatedPhaseDegrees,
        appliedRotationDegrees,
        analysisConfidence,
        sidePreservation,
        telemetryValueCount
    };

    enum TelemetryHistory
    {
        beforeCorrelationHistory,
        afterCorrelationHistory,
        delayHistory,
        rotationHistory,
        telemetryHistoryValueCount
    };

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    int latencySamples() const override { return fixedLatencySamples; }
    bool readContinuousTelemetry(
        ContinuousTelemetrySnapshot&) const noexcept override;
    ContinuousTelemetryCapability*
        continuousTelemetryCapability() noexcept override { return this; }
    const ContinuousTelemetryCapability*
        continuousTelemetryCapability() const noexcept override { return this; }

private:
    float readDelay(int channel, float delaySamples) const noexcept;
    void analyse(float maximumAlignmentSamples, float crossoverHz) noexcept;

    std::array<std::vector<float>, 2> audioHistory;
    std::array<std::vector<float>, 2> analysisHistory;
    std::array<float, 2> detectorLowPass {};
    std::array<float, 2> monoLowPass {};
    std::array<float, 2> previousAnalysis {};
    std::array<float, 2> currentAnalysis {};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        alignmentSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        correctionSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        preserveSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        monoBelowSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
    double sampleRate = 44100.0;
    float estimatedDelaySamples = 0.0f;
    float estimatedPhase = 0.0f;
    float confidence = 0.0f;
    float beforeCorrelation = 1.0f;
    float afterCorrelation = 1.0f;
    int writePosition = 0;
    int analysisPosition = 0;
    int analysisCountdown = 1;
    int fixedLatencySamples = 1;
    int analysisWindowSamples = 512;
    bool initialized = false;
    ContinuousTelemetrySnapshot telemetryState;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
};
} // namespace megadsp
