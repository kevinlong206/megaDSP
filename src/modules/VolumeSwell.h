#pragma once

#include "DspModule.h"

namespace megadsp
{
class VolumeSwellModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    int latencySamples() const override { return lookaheadSamples; }

    bool readContinuousTelemetry(
        ContinuousTelemetrySnapshot&) const noexcept override;
    ContinuousTelemetryCapability*
        continuousTelemetryCapability() noexcept override { return this; }
    const ContinuousTelemetryCapability*
        continuousTelemetryCapability() const noexcept override { return this; }

    enum TelemetryValue
    {
        inputEnvelope,
        inputThreshold,
        onsetStrength,
        onsetThreshold,
        swellProgress,
        swellGain,
        triggered,
        telemetryValueCount
    };

private:
    static float envelopeCoefficient(
        double sampleRate, float milliseconds) noexcept;
    static float curveExponent(float normalized) noexcept;

    std::array<std::vector<float>, 2> delay;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        timeSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        curveSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        floorSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
    double sampleRate = 44100.0;
    float fastEnergy = 0.0f;
    float slowEnergy = 0.0f;
    float fastCoefficient = 0.0f;
    float slowCoefficient = 0.0f;
    float envelopeGain = 1.0f;
    float duckStartGain = 1.0f;
    float progress = 1.0f;
    int lookaheadSamples = 1;
    int duckLengthSamples = 1;
    int writePosition = 0;
    int duckSamplesRemaining = 0;
    int samplesSinceTrigger = 0;
    bool onsetArmed = true;
    bool swelling = false;
    bool initialized = false;
    std::uint64_t telemetrySequence = 0;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
};
} // namespace megadsp
