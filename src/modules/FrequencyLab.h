#pragma once

#include "DspModule.h"

namespace megadsp
{
class FrequencyLabModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    static constexpr int rootCount = 12;
    static constexpr int harmonyCount = 8;
    static constexpr int registerCount = 4;
    static constexpr int resonatorCount = 8;

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
        rootNote,
        selectedHarmony,
        selectedRegister,
        motionPosition,
        motionCents,
        decaySeconds,
        frequency0,
        frequency1,
        frequency2,
        frequency3,
        frequency4,
        frequency5,
        frequency6,
        frequency7,
        wetEnvelope,
        telemetryValueCount
    };

private:
    struct Resonator
    {
        float previous = 0.0f;
        float older = 0.0f;

        float process(
            float input, float excitation, float feedback,
            float damping) noexcept;
        void reset() noexcept;
    };

    struct Oscillator
    {
        float cosine = 1.0f;
        float sine = 0.0f;
    };

    static void advance(Oscillator&, float radians) noexcept;
    static float resonatorFrequency(
        int root, int harmony, int registerIndex, int resonator) noexcept;

    std::array<std::array<Resonator, resonatorCount>, 2> resonators;
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>,
               resonatorCount> frequencySmoothed;
    std::array<float, 2> lowPassState {};
    std::array<float, 2> highPassState {};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        decaySmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> toneSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        rateSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> depthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> spreadSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        lowCutSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        highCutSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
    Oscillator motion;
    double sampleRate = 44100.0;
    int renormalizeCounter = 0;
    bool initialized = false;
    std::uint64_t telemetrySequence = 0;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
};
} // namespace megadsp
