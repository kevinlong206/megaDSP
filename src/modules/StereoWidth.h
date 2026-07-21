#pragma once

#include "DspModule.h"

namespace megadsp
{
class StereoWidthModule final : public DspModule
{
public:
    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;

private:
    struct AllpassStage
    {
        void prepare(int delaySamples, float newCoefficient);
        void reset();
        float process(float input);

        std::vector<float> buffer;
        int position = 0;
        float coefficient = 0.5f;
    };

    std::array<AllpassStage, 4> decorrelator;
    double sampleRate = 44100.0;
    float sideLowState = 0.0f;
    float dimensionLowState = 0.0f;
    float midEnergy = 0.0f;
    float sideEnergy = 0.0f;
    float safeGain = 1.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> widthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> dimensionSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> monoCoefficientSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> focusCoefficientSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> balanceSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputSmoothed;
};
} // namespace megadsp
