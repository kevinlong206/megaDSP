#pragma once

#include "DspModule.h"

namespace megadsp
{
class ConvolutionReverbModule final
    : public DspModule,
      public ImpulseResponseCapability
{
public:
    ConvolutionReverbModule();
    ~ConvolutionReverbModule() override;
    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    double tailSeconds(const ControlValues&) const override
    {
        return currentImpulseResponseTailSeconds();
    }
    juce::Result loadImpulseResponse(const juce::File&) override;
    juce::Result prepareImpulseResponse(
        const juce::File&, PreparedImpulseResponsePtr&) override;
    bool commitPreparedImpulseResponse(
        PreparedImpulseResponsePtr&) override;
    void cancelPreparedImpulseResponse(
        PreparedImpulseResponsePtr&) noexcept override;
    void clearImpulseResponse() override;
    bool hasImpulseResponse() const noexcept override
    {
        return hasCurrentImpulseResponse();
    }
    bool isImpulseResponseReady() const noexcept override
    {
        return hasCurrentImpulseResponse();
    }
    bool isImpulseReady() const noexcept { return isImpulseResponseReady(); }
    juce::String impulseResponseName() const override;
    ImpulseResponsePreview impulseResponsePreview() const override;
    juce::String impulseName() const { return impulseResponseName(); }
    ImpulseResponsePreview impulsePreview() const
    {
        return impulseResponsePreview();
    }
    double currentImpulseResponseTailSeconds() const noexcept override
    {
        return hasCurrentImpulseResponse()
                   ? impulseLengthSeconds.load(std::memory_order_relaxed)
                   : 0.0;
    }
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

    bool hasCurrentImpulseResponse() const noexcept;
    std::uint64_t beginImpulseResponseRequest() noexcept;

    std::unique_ptr<Engine> convolution;
    juce::dsp::StateVariableTPTFilter<float> lowCut;
    juce::dsp::StateVariableTPTFilter<float> highCut;
    juce::AudioBuffer<float> wetBuffer;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> drySmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
    double sampleRate = 44100.0;
    juce::dsp::ProcessSpec preparedSpec { 44100.0, 512, 2 };
    std::atomic<std::uint64_t> requestCounter { 0 };
    std::atomic<std::uint64_t> requestedGeneration { 0 };
    std::atomic<std::uint64_t> committedGeneration { 0 };
    std::atomic<bool> committedImpulseAvailable { false };
    std::atomic<double> impulseLengthSeconds { 0.0 };
    mutable juce::CriticalSection metadataLock;
    juce::String loadedImpulseName;
    ImpulseResponsePreview preview {};
};
} // namespace megadsp
