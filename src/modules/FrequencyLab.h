#pragma once

#include "DspModule.h"

namespace megadsp
{
class FrequencyLabModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    static constexpr int hilbertTaps = 129;
    static constexpr int reportedLatencySamples = (hilbertTaps - 1) / 2;

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    int latencySamples() const override { return reportedLatencySamples; }
    double tailSeconds(const ControlValues&) const override;
    bool readContinuousTelemetry(
        ContinuousTelemetrySnapshot&) const noexcept override;
    ContinuousTelemetryCapability*
        continuousTelemetryCapability() noexcept override { return this; }
    const ContinuousTelemetryCapability*
        continuousTelemetryCapability() const noexcept override { return this; }

    enum TelemetryValue
    {
        commonShiftHz,
        leftShiftHz,
        rightShiftHz,
        lfoPosition,
        lfoRateHz,
        modulationDepthHz,
        stereoOffsetHz,
        telemetryValueCount
    };

private:
    struct Oscillator
    {
        float cosine = 1.0f;
        float sine = 0.0f;
    };

    static void advance(Oscillator&, float radians) noexcept;

    std::array<float, hilbertTaps> coefficients {};
    std::array<std::array<float, hilbertTaps>, 2> inputHistory {};
    std::array<std::array<float, reportedLatencySamples>, 2> dryHistory {};
    std::array<float, 2> highPassState {};
    std::array<float, 2> lowPassState {};
    std::array<float, 2> feedbackState {};
    std::array<Oscillator, 2> carrier {};
    Oscillator lfo;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> shiftSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> fineSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> feedbackSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        rateSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> depthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> offsetSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        lowCutSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        highCutSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
    double sampleRate = 44100.0;
    int historyPosition = 0;
    int dryPosition = 0;
    int renormalizeCounter = 0;
    bool initialized = false;
    std::uint64_t telemetrySequence = 0;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
};
} // namespace megadsp
