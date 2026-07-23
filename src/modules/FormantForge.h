#pragma once

#include "DspModule.h"

namespace megadsp
{
class FormantForgeModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    static constexpr int modelCount = 4;
    static constexpr int formantCount = 4;

    enum ControlIndex
    {
        modelControl,
        vowelXControl,
        vowelYControl,
        formantShiftControl,
        resonanceControl,
        breathControl,
        motionRateControl,
        motionDepthControl,
        stereoSpreadControl,
        mixControl,
        outputControl
    };

    enum TelemetryValue
    {
        actualVowelX,
        actualVowelY,
        actualFormant1Hz,
        actualFormant2Hz,
        actualFormant3Hz,
        actualFormant4Hz,
        breathRms,
        actualOutputRms,
        telemetryValueCount
    };

    enum TelemetryHistory
    {
        vowelXHistory,
        vowelYHistory,
        breathHistory,
        outputHistory,
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
    struct Coefficients
    {
        float b0 = 0.0f;
        float b1 = 0.0f;
        float b2 = 0.0f;
        float a1 = 0.0f;
        float a2 = 0.0f;
    };

    struct Resonator
    {
        float z1 = 0.0f;
        float z2 = 0.0f;

        float process(float input, const Coefficients&) noexcept;
        void reset() noexcept;
    };

    struct FormantParameters
    {
        float frequency = 1000.0f;
        float bandwidth = 100.0f;
        float gain = 1.0f;
    };

    FormantParameters interpolateFormant(
        int model, int formant, float x, float y, float shiftRatio,
        float resonance) const noexcept;
    Coefficients coefficientsFor(
        float frequency, float bandwidth) const noexcept;
    float nextNoise(std::uint32_t&) noexcept;
    std::array<float, 2> colouredBreath(float amount, float spread) noexcept;

    std::array<
        std::array<std::array<Resonator, formantCount>, 2>,
        modelCount> filters;
    std::array<juce::SmoothedValue<
        float, juce::ValueSmoothingTypes::Linear>, modelCount> modelMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        vowelXSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        vowelYSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        shiftSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        resonanceSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        breathSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        motionRateSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        motionDepthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        stereoSpreadSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
    std::array<float, 2> breathLowState {};
    std::array<float, 2> breathHighState {};
    std::uint32_t commonNoiseState = 0x6d2b79f5u;
    std::uint32_t sideNoiseState = 0x9e3779b9u;
    double sampleRate = 44100.0;
    double motionPhase = 0.0;
    float signalEnvelope = 0.0f;
    int activeModel = 0;
    bool initialized = false;
    std::atomic<float> outputMeter { 0.0f };
    std::atomic<float> detectorMeter { -100.0f };
    ContinuousTelemetrySnapshot telemetryState;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
};
} // namespace megadsp
