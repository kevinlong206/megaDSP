#pragma once

#include "DspModule.h"
#include "ReEqProcessor.h"

namespace megadsp
{
class EqualizerModule final : public DspModule
{
public:
    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    void setReEqSettings(const ReEqSettings& settings) noexcept
    {
        nativeEnabled = settings.native;
        nativeLookaheadMs = settings.lookaheadMs;
        nativeProcessor.setLookahead(nativeLookaheadMs);
    }
    int latencySamples() const override
    {
        return nativeEnabled ? nativeProcessor.latencySamples() : 0;
    }
    int maximumNativeLatencySamples() const
    {
        return nativeProcessor.maximumLatencySamples();
    }
    const ReEqProcessor& reEqProcessor() const { return nativeProcessor; }

private:
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;
        float process(float input);
        void setPeak(double sampleRate, float frequency, float q, float gainDb);
        void setLowShelf(
            double sampleRate, float frequency, float q, float gainDb);
        void setHighShelf(
            double sampleRate, float frequency, float q, float gainDb);
        void setHighPass(double sampleRate, float frequency, float q);
        void setLowPass(double sampleRate, float frequency, float q);
        void reset();
    };
    static constexpr int topologyCount = 3;
    std::array<std::array<std::array<Biquad, topologyCount>, 3>, 2> filters;
    std::array<std::array<
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>,
        topologyCount>, 3> topologyMix;
    double sampleRate = 44100.0;
    bool topologyInitialized = false;
    ReEqProcessor nativeProcessor;
    bool nativeEnabled = false;
    float nativeLookaheadMs = 0.0f;
};
} // namespace megadsp
