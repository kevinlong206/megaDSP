#pragma once

#include "DspModule.h"

namespace megadsp
{
class StudioFlangerModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    static constexpr int modelCount = 4;

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    int latencySamples() const override { return fixedLatency; }
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
        targetModel,
        modelMix0,
        modelMix1,
        modelMix2,
        modelMix3,
        selectedDelayMs,
        telemetryValueCount
    };

private:
    float readDelay(const std::vector<float>&, float delaySamples) const;
    float modelLfo(int model, float phaseValue) const;

    std::array<std::array<std::vector<float>, 2>, modelCount> modelHistory;
    std::array<std::vector<float>, 2> dryHistory;
    std::array<std::array<float, 2>, modelCount> feedbackState {};
    std::array<std::array<float, 2>, modelCount> toneState {};
    std::array<juce::SmoothedValue<float,
        juce::ValueSmoothingTypes::Linear>, modelCount> modelMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> rateSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> depthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        delaySmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        feedbackSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        stereoPhaseSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        toneSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
    double sampleRate = 44100.0;
    int writePosition = 0;
    int fixedLatency = 0;
    float phase = 0.0f;
    bool initialized = false;
    std::uint64_t telemetrySequence = 0;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
};
} // namespace megadsp
