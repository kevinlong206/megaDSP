#pragma once

#include "DspModule.h"

namespace megadsp
{
class MidnightCurrentModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    static constexpr int stageCount = 8;

    enum Control
    {
        filterCenterControl,
        filterSweepControl,
        motionRateControl,
        phaseDepthControl,
        regenerationControl,
        resonanceControl,
        warmthControl,
        softnessControl,
        driftControl,
        stereoSpreadControl,
        mixControl,
        outputControl
    };

    enum TelemetryValue
    {
        leftPhase,
        rightPhase,
        actualCutoffHz,
        phaseIntensity,
        wetEnergy,
        driveAmount,
        telemetryValueCount
    };

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    double tailSeconds(const ControlValues&) const override;
    float meterValue() const override
    {
        return meter.load(std::memory_order_relaxed);
    }
    bool readContinuousTelemetry(
        ContinuousTelemetrySnapshot&) const noexcept override;
    ContinuousTelemetryCapability*
        continuousTelemetryCapability() noexcept override { return this; }
    const ContinuousTelemetryCapability*
        continuousTelemetryCapability() const noexcept override { return this; }

private:
    struct ChannelState
    {
        float softener = 0.0f;
        float filterIntegrator1 = 0.0f;
        float filterIntegrator2 = 0.0f;
        std::array<float, stageCount> allpass {};
        float feedback = 0.0f;
    };

    float renderChannel(int channel, float input, float phase,
                        float cutoffHz, float filterSweep, float phaseDepth,
                        float regeneration, float resonance, float warmth,
                        float softness, float drift) noexcept;

    std::array<ChannelState, 2> channels {};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        centerSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        sweepSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        rateSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        phaseDepthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        regenerationSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        resonanceSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        warmthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        softnessSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        driftSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        spreadSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
    double sampleRate = 44100.0;
    double motionPhase = 0.0;
    double driftPhase = 0.0;
    float meterEnvelope = 0.0f;
    bool initialized = false;
    std::uint64_t telemetrySequence = 0;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
    std::atomic<float> meter { 0.0f };
};
} // namespace megadsp
