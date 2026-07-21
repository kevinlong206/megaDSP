#pragma once

#include "DspModule.h"

namespace megadsp
{
class DynamicEqualizerModule final : public DspModule
{
public:
    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    float meterValue() const override { return dynamicGainDb.load(); }
    float detectorValue() const override { return detectorLevelDb.load(); }

private:
    struct Biquad
    {
        float process(float input);
        void setDynamic(double sampleRate, float frequency, float q,
                        float gainDb, int shape);
        void setDetector(double sampleRate, float frequency, float q, int shape);
        void reset();

        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;
    };

    std::array<std::array<Biquad, 3>, 2> programFilters;
    std::array<std::array<Biquad, 3>, 2> detectorFilters;
    std::array<float, 2> rmsPower {};
    std::array<float, 2> gainDbState {};
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>, 3>
        shapeMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        frequencySmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> qSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> detectorMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> externalMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> listenMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> stereoLink;
    double sampleRate = 44100.0;
    bool initialized = false;
    int activeShape = 0;
    std::atomic<float> dynamicGainDb { 0.0f };
    std::atomic<float> detectorLevelDb { -100.0f };
};
} // namespace megadsp
