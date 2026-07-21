#pragma once

#include "DspModule.h"

namespace megadsp
{
class MidSideDecoderModule final : public DspModule
{
public:
    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;

private:
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> widthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> sideMuteSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> swapSmoothed;
};
} // namespace megadsp
