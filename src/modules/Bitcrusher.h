#pragma once

#include "DspModule.h"

namespace megadsp
{
class BitcrusherModule final : public DspModule
{
public:
    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;

private:
    float randomUnit(int channel) noexcept;
    static float quantize(float input, float step, float mode) noexcept;

    std::array<std::array<float, 2>, 2> antiAliasState {};
    std::array<float, 2> heldSample {};
    std::array<float, 2> slewState {};
    std::array<float, 2> errorState {};
    std::array<std::uint32_t, 2> randomState {
        0x9e3779b9u, 0x243f6a88u
    };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        bitsSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        rateSmoothed;
    std::array<juce::SmoothedValue<float,
        juce::ValueSmoothingTypes::Linear>, 9> linearSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
    double sampleRate = 44100.0;
    float samplesUntilUpdate = 0.0f;
    bool initialized = false;
};
} // namespace megadsp
