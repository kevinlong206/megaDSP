#pragma once

#include "DspModule.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace megadsp
{
class IRMosaicModule final
    : public DspModule,
      public ImpulseResponseCapability,
      public ContinuousTelemetryCapability
{
public:
    static constexpr int patternCount = 4;

    enum TelemetryValue
    {
        primaryPattern,
        secondaryPattern,
        patternBlend,
        reshuffleActivity,
        wetLevel,
        telemetryValueCount
    };

    IRMosaicModule();
    ~IRMosaicModule() override;
    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    DryPathTiming dryPathTiming() const override
    {
        return DryPathTiming::immediate;
    }
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
    bool isImpulseResponseReady() const noexcept override
    {
        return hasImpulseResponse();
    }
    juce::String impulseResponseName() const override;
    ImpulseResponsePreview impulseResponsePreview() const override;
    double currentImpulseResponseTailSeconds() const noexcept override;
    ImpulseResponseCapability* impulseResponseCapability() noexcept override
    {
        return this;
    }
    const ImpulseResponseCapability*
        impulseResponseCapability() const noexcept override
    {
        return this;
    }

    bool readContinuousTelemetry(
        ContinuousTelemetrySnapshot&) const noexcept override;
    ContinuousTelemetryCapability*
        continuousTelemetryCapability() noexcept override { return this; }
    const ContinuousTelemetryCapability*
        continuousTelemetryCapability() const noexcept override { return this; }

private:
    struct Engine;
    struct PreparedLoad;

    std::uint64_t beginRequest() noexcept;
    std::uint32_t nextRandom() noexcept;
    void beginReshuffle(int style, double rate, float crossfade);

    std::array<std::unique_ptr<Engine>, patternCount> engines;
    std::array<juce::AudioBuffer<float>, patternCount> wetBuffers;
    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> excitationBuffer;
    juce::dsp::StateVariableTPTFilter<float> lowCut;
    juce::dsp::StateVariableTPTFilter<float> highCut;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        wetLevelSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        widthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        feedbackSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
    std::array<float, 2> feedbackState {};
    juce::dsp::ProcessSpec preparedSpec { 44100.0, 512, 2 };
    double sampleRate = 44100.0;
    double reshufflePhase = 0.0;
    std::uint32_t randomState = 0x9e3779b9u;
    int currentPattern = 0;
    int nextPattern = 1;
    int pingPongDirection = 1;
    int transitionPosition = 0;
    int transitionSamples = 1;
    bool transitioning = false;
    std::atomic<std::uint64_t> requestCounter { 0 };
    std::atomic<std::uint64_t> requestedGeneration { 0 };
    std::atomic<std::uint64_t> committedGeneration { 0 };
    std::atomic<bool> available { false };
    std::atomic<double> lengthSeconds { 0.0 };
    mutable juce::CriticalSection metadataLock;
    juce::String loadedName;
    ImpulseResponsePreview preview {};
    ContinuousTelemetrySnapshot telemetryState;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
    int telemetryCountdown = 1;
    int telemetryInterval = 1;
    bool parametersInitialized = false;
};
} // namespace megadsp
