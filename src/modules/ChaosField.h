#pragma once

#include "DspModule.h"

namespace megadsp
{
class ChaosFieldModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    static constexpr int attractorCount = 3;

    enum ControlIndex
    {
        attractorControl,
        rateControl,
        syncControl,
        divisionControl,
        depthControl,
        filterCenterControl,
        delayCenterControl,
        feedbackControl,
        panOrbitControl,
        stereoSpreadControl,
        mixControl,
        outputControl
    };

    enum TelemetryValue
    {
        actualX,
        actualY,
        actualZ,
        actualFilterHz,
        actualDelayMilliseconds,
        actualPan,
        feedbackRms,
        wetRms,
        telemetryValueCount
    };

    enum TelemetryHistory
    {
        xHistory,
        yHistory,
        zHistory,
        wetHistory,
        telemetryHistoryValueCount
    };

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
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
    struct AttractorState
    {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        double w = 0.0;
    };

    struct StateVariableFilter
    {
        float integrator1 = 0.0f;
        float integrator2 = 0.0f;

        float process(float input, float cutoff, double sampleRate) noexcept;
        void reset() noexcept;
    };

    void stepAttractors(float rate) noexcept;
    std::array<float, 3> normalizedAxes(int attractor) const noexcept;
    void validateAttractor(int attractor) noexcept;
    float readDelay(int channel, float delaySamples) const noexcept;

    std::array<AttractorState, attractorCount> attractors;
    std::array<std::vector<float>, 2> delayBuffer;
    std::array<StateVariableFilter, 2> inputFilter;
    std::array<float, 2> feedbackFilterState {};
    std::array<juce::SmoothedValue<
        float, juce::ValueSmoothingTypes::Linear>, attractorCount> attractorMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        rateSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> depthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        filterCenterSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        delayCenterSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        feedbackSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        panOrbitSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        stereoSpreadSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
    double sampleRate = 44100.0;
    int writePosition = 0;
    int activeAttractor = 0;
    bool initialized = false;
    std::atomic<float> outputMeter { 0.0f };
    std::atomic<float> detectorMeter { -100.0f };
    ContinuousTelemetrySnapshot telemetryState;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
};
} // namespace megadsp
