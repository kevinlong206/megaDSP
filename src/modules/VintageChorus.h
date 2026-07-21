#pragma once

#include "DspModule.h"

namespace megadsp
{
class VintageChorusModule final : public DspModule
{
public:
    static constexpr int maximumVoices = 6;
    static constexpr int modelCount = 4;

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    double tailSeconds(const ControlValues&) const override;

private:
    float readDelay(int channel, float delaySamples) const;
    std::array<float, 2> renderModel(int model, float depth,
                                     float delayMs, float age);
    static float lfo(float phase, int model, float age);

    std::array<std::vector<float>, 2> delayBuffer;
    std::array<std::array<float, 2>, modelCount> toneState {};
    std::array<float, 2> feedbackState {};
    std::array<float, 2> inputColourState {};
    std::array<std::array<float, maximumVoices>, modelCount> phases {};
    std::array<juce::SmoothedValue<float,
        juce::ValueSmoothingTypes::Linear>, modelCount> modelMix;
    std::array<juce::SmoothedValue<float,
        juce::ValueSmoothingTypes::Linear>, maximumVoices> voiceMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> rateSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> depthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> delaySmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> widthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> feedbackSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> toneSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> ageSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> stereoPhaseSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
    int writePosition = 0;
    int channels = 2;
    int activeModel = 0;
    double sampleRate = 44100.0;
    float signalEnergy = 0.0f;
    float currentStereoPhase = 0.5f;
    bool initialized = false;
};
} // namespace megadsp
