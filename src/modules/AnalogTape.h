#pragma once

#include "DspModule.h"

namespace megadsp
{
class AnalogTapeModule final : public DspModule
{
public:
    static constexpr int machineCount = 4;
    static constexpr int speedCount = 4;

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    int latencySamples() const override { return processingLatency; }

private:
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;
        float process(float input);
        void setPeak(double sampleRate, float frequency, float q, float gainDb);
        void reset();
    };

    float readWowFlutter(int channel, float delaySamples) const;
    std::uint32_t nextRandom(int channel);
    float randomUnit(int channel);

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::AudioBuffer<float> dryBuffer;
    std::array<std::vector<float>, 2> dryLatencyBuffer;
    std::array<std::vector<float>, 2> wowFlutterBuffer;
    std::array<Biquad, 2> headBumpFilters;
    std::array<float, 2> hfLossState {};
    std::array<float, 2> tapeMemoryState {};
    std::array<float, 2> compressionEnvelope {};
    std::array<float, 2> noiseShapeState {};
    std::array<float, 2> noiseEnvelope {};
    std::array<float, 2> wearDropoutState {};
    std::array<std::uint32_t, 2> noiseState { 0x9e3779b9u, 0x85ebca6bu };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        inputSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> driveSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> biasSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        driveHardnessSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        biasSensitivitySmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        hysteresisSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        compressionSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        hfCutoffSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        headBumpFreqSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        headBumpQSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        wowFlutterScaleSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        noiseFloorSmoothed;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> headBumpSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wowSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> flutterSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wearSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> noiseSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;

    double sampleRate = 44100.0;
    int dryLatencyPosition = 0;
    int processingLatency = 0;
    int wowFlutterWritePosition = 0;
    float wowPhase = 0.0f;
    float flutterPhase = 0.0f;
    bool initialized = false;
};
} // namespace megadsp
