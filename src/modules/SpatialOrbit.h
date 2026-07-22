#pragma once

#include "DspModule.h"

namespace megadsp
{
class SpatialOrbitModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    int latencySamples() const override { return fixedLatencySamples; }
    double tailSeconds(const ControlValues&) const override;
    bool readContinuousTelemetry(
        ContinuousTelemetrySnapshot&) const noexcept override;
    ContinuousTelemetryCapability*
        continuousTelemetryCapability() noexcept override { return this; }
    const ContinuousTelemetryCapability*
        continuousTelemetryCapability() const noexcept override { return this; }

    enum TelemetryValue
    {
        xPosition,
        yPosition,
        distanceMetres,
        activePath,
        pathPhase,
        trajectoryPosition,
        radialPosition,
        telemetryValueCount
    };

    enum TelemetryHistory
    {
        xHistory,
        yHistory,
        distanceHistory,
        telemetryHistoryValueCount
    };

private:
    float readDelay(int channel, float delaySamples) const noexcept;
    float wanderPosition() noexcept;

    std::array<std::vector<float>, 2> motionHistory;
    std::array<std::vector<float>, 2> dryHistory;
    std::array<float, 2> dampingState {};
    float sideFoundationState = 0.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        rateSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> spanSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> widthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        distanceSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> dopplerSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        dampingSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        monoBelowSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
    std::uint32_t randomState = 0x4f1bbcddu;
    double sampleRate = 44100.0;
    double motionPhase = 0.0;
    float previousTrajectory = 0.0f;
    float wanderStart = 0.0f;
    float wanderTarget = 0.0f;
    float wanderProgress = 1.0f;
    int writePosition = 0;
    int dryPosition = 0;
    int fixedLatencySamples = 1;
    bool initialized = false;
    ContinuousTelemetrySnapshot telemetryState;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
};
} // namespace megadsp
