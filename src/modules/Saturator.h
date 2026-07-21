#pragma once

#include "DspModule.h"

namespace megadsp
{
class SaturatorModule final : public DspModule
{
public:
    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;

private:
    std::array<float, 2> toneState {};
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::AudioBuffer<float> dryBuffer;
    std::array<std::vector<float>, 2> dryLatencyBuffer;
    int dryLatencyPosition = 0;
    int oversamplingLatency = 0;
    double sampleRate = 44100.0;
    float inputMeanSquare = 1.0e-8f;
    float wetMeanSquare = 1.0e-8f;
    float compensationGain = 1.0f;

public:
    int latencySamples() const override { return oversamplingLatency; }
};
} // namespace megadsp
