#pragma once

#include "DspModule.h"

namespace megadsp
{
class RotarySpeakerModule final : public DspModule
{
public:
    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;

private:
    float readHistory(const std::vector<float>& history, float delaySamples) const;
    float rotorSample(const std::vector<float>& history, float rotorPhase,
                      float microphoneAngle, float radiusMetres,
                      float microphoneDistanceMetres, float doppler,
                      bool horn) const;

    std::vector<float> hornHistory;
    std::vector<float> drumHistory;
    std::array<std::vector<float>, 2> roomHistory;
    std::array<float, 2> cabinetState {};
    float crossoverState = 0.0f;
    float previousDrivenInput = 0.0f;
    int historyWritePosition = 0;
    int roomWritePosition = 0;
    double sampleRate = 44100.0;
    float hornPhase = 0.0f;
    float drumPhase = juce::MathConstants<float>::pi;
    float hornRpm = 0.0f;
    float drumRpm = 0.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> driveSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> balanceSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> crossoverSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> dopplerSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> distanceSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> spreadSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> cabinetSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> roomSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
};
} // namespace megadsp
