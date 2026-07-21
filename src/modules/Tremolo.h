#pragma once

#include "DspModule.h"

namespace megadsp
{
class TremoloModule final : public DspModule
{
public:
    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;

private:
    float readDelay(int channel, float delaySamples) const;
    float lfoValue(float phase, float shape) const;

    std::array<std::vector<float>, 2> delayBuffer;
    std::array<float, 2> crossoverState {};
    int writePosition = 0;
    double sampleRate = 44100.0;
    float phase = 0.0f;
    bool initialized = false;
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>, 3>
        modeMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> rateSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> tremoloDepthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> pitchDepthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> shapeSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> stereoPhaseSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> crossoverSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
};
} // namespace megadsp
