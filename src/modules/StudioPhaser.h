#pragma once

#include "DspModule.h"

namespace megadsp
{
class StudioPhaserModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    static constexpr int topologyCount = 5;
    static constexpr int maximumStages = 12;

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

    enum TelemetryValue
    {
        leftPhase,
        rightPhase,
        targetTopology,
        topologyMix0,
        topologyMix1,
        topologyMix2,
        topologyMix3,
        topologyMix4,
        telemetryValueCount
    };

private:
    using StageState = std::array<float, maximumStages>;

    float renderTopology(int topology, int channel, float input,
                         float centre, float sweep, float modulation);

    std::array<std::array<StageState, 2>, topologyCount> states {};
    std::array<std::array<float, 2>, topologyCount> feedbackState {};
    std::array<juce::SmoothedValue<float,
        juce::ValueSmoothingTypes::Linear>, topologyCount> topologyMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> rateSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> depthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        centreSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        sweepSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        feedbackSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        stereoPhaseSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
    double sampleRate = 44100.0;
    float phase = 0.0f;
    int activeTopology = 2;
    bool initialized = false;
    std::uint64_t telemetrySequence = 0;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
};
} // namespace megadsp
