#pragma once

#include "DspModule.h"

namespace megadsp
{
class AutoWahModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    enum Control
    {
        sensitivityControl,
        centerFrequencyControl,
        sweepRangeControl,
        attackControl,
        releaseControl,
        resonanceControl,
        directionControl,
        pickFocusControl,
        driveControl,
        filterTypeControl,
        mixControl,
        outputControl
    };

    enum TelemetryValue
    {
        inputLevelDb,
        thresholdDb,
        envelopeAmount,
        currentFrequencyHz,
        pickActivity,
        wetEnergy,
        telemetryValueCount
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
    struct ChannelState
    {
        float fastEnvelope = 0.0f;
        float slowEnvelope = 0.0f;
        float sweepEnvelope = 0.0f;
        float integrator1 = 0.0f;
        float integrator2 = 0.0f;
    };

    float renderFilter(
        ChannelState&, float input, float frequency, float resonance,
        float drive, const std::array<float, 4>& filterWeights) const noexcept;

    std::array<ChannelState, 2> channels {};
    std::array<juce::SmoothedValue<float>, controlsPerSlot> smoothed {};
    std::array<juce::SmoothedValue<float>, 4> filterWeights {};
    double sampleRate = 44100.0;
    bool initialized = false;
    float meterEnvelope = 0.0f;
    std::uint64_t telemetrySequence = 0;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
    std::atomic<float> meter { 0.0f };
};
} // namespace megadsp
