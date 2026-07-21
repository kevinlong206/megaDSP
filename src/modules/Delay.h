#pragma once

#include "DspModule.h"

namespace megadsp
{
class DelayModule final : public DspModule
{
public:
    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    double tailSeconds(const ControlValues&) const override;

private:
    std::array<std::vector<float>, 2> delayBuffer;
    std::array<float, 2> filterState {};
    int writePosition = 0;
    double sampleRate = 44100.0;
    float phase = 0.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> delaySamplesSmoothed;
};
} // namespace megadsp
