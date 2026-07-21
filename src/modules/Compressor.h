#pragma once

#include "DspModule.h"

namespace megadsp
{
class CompressorModule final : public DspModule
{
public:
    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    float meterValue() const override { return gainReductionDb.load(); }

private:
    double sampleRate = 44100.0;
    float envelope = 0.0f;
    float gain = 1.0f;
    float averageReductionDb = 0.0f;
    float autoMakeupDb = 0.0f;
    std::atomic<float> gainReductionDb { 0.0f };
};
} // namespace megadsp
