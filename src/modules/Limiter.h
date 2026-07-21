#pragma once

#include "DspModule.h"

namespace megadsp
{
class LimiterModule final : public DspModule
{
public:
    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    int latencySamples() const override { return maximumLookaheadSamples; }
    float meterValue() const override { return gainReductionDb.load(); }
    void setLookaheadControl(float normalizedValue);

private:
    std::array<std::vector<float>, 2> delayBuffer;
    std::vector<float> gainBuffer;
    int writePosition = 0;
    int maximumLookaheadSamples = 0;
    std::atomic<int> activeLookaheadSamples { 0 };
    double sampleRate = 44100.0;
    float gain = 1.0f;
    float autoGain = 1.0f;
    std::atomic<float> gainReductionDb { 0.0f };
};
} // namespace megadsp
