#pragma once

#include "DspModule.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace megadsp
{
class WormholeConvolutionModule final
    : public DspModule,
      public ImpulseResponseCapability
{
public:
    static constexpr int laneCount = 2;

    WormholeConvolutionModule();
    ~WormholeConvolutionModule() override;
    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    double tailSeconds(const ControlValues&) const override;

    juce::Result loadImpulseResponse(const juce::File&) override;
    juce::Result prepareImpulseResponse(
        const juce::File&, PreparedImpulseResponsePtr&) override;
    bool commitPreparedImpulseResponse(
        PreparedImpulseResponsePtr&) override;
    void cancelPreparedImpulseResponse(
        PreparedImpulseResponsePtr&) noexcept override;
    void clearImpulseResponse() override;
    bool hasImpulseResponse() const noexcept override;
    bool isImpulseResponseReady() const noexcept override;
    juce::String impulseResponseName() const override;
    ImpulseResponsePreview impulseResponsePreview() const override;
    double currentImpulseResponseTailSeconds() const noexcept override;

    int impulseResponseLaneCount() const noexcept override
    {
        return laneCount;
    }
    ImpulseResponseTransform impulseResponseTransform(
        const float*, int) const noexcept override;
    juce::Result prepareImpulseResponse(
        int, const juce::File&, const ImpulseResponseTransform&,
        PreparedImpulseResponsePtr&) override;
    bool commitPreparedImpulseResponse(
        int, PreparedImpulseResponsePtr&) override;
    void cancelPreparedImpulseResponse(
        int, PreparedImpulseResponsePtr&) noexcept override;
    void clearImpulseResponse(int) override;
    juce::String impulseResponseName(int) const override;
    ImpulseResponsePreview impulseResponsePreview(int) const override;

    ImpulseResponseCapability* impulseResponseCapability() noexcept override
    {
        return this;
    }
    const ImpulseResponseCapability*
        impulseResponseCapability() const noexcept override
    {
        return this;
    }

private:
    struct Engine;
    struct PreparedLoad;
    struct Lane;

    std::uint64_t beginRequest(int) noexcept;
    bool laneAvailable(int) const noexcept;

    std::array<std::unique_ptr<Lane>, laneCount> lanes;
    juce::AudioBuffer<float> dryBuffer;
    juce::dsp::StateVariableTPTFilter<float> lowCut;
    juce::dsp::StateVariableTPTFilter<float> highCut;
    std::array<juce::SmoothedValue<float,
        juce::ValueSmoothingTypes::Linear>, laneCount> laneGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> dryGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputGain;
    juce::dsp::ProcessSpec preparedSpec { 44100.0, 512, 2 };
    double sampleRate = 44100.0;
};
} // namespace megadsp
