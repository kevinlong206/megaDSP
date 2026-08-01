#pragma once

#include "DspModule.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace megadsp
{
class GuitarCabinetModule final
    : public DspModule,
      public ImpulseResponseCapability,
      public ContinuousTelemetryCapability
{
public:
    static constexpr int builtInCount = 23;
    static constexpr int defaultBuiltInIndex = 7;

    enum TelemetryValue
    {
        inputLevel,
        wetLevel,
        outputLevel,
        driveActivity,
        telemetryValueCount
    };

    GuitarCabinetModule();
    ~GuitarCabinetModule() override;
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
    bool isImpulseResponseReady() const noexcept override
    {
        return hasImpulseResponse();
    }
    juce::String impulseResponseName() const override;
    ImpulseResponsePreview impulseResponsePreview() const override;
    double currentImpulseResponseTailSeconds() const noexcept override;
    int builtInImpulseResponseCount() const noexcept override
    {
        return builtInCount;
    }
    juce::String builtInImpulseResponseName(int) const override;
    juce::Result prepareBuiltInImpulseResponse(
        int, PreparedImpulseResponsePtr&) override;
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

    juce::Result prepareDecodedImpulse(
        std::unique_ptr<juce::AudioFormatReader>, const juce::String&,
        PreparedImpulseResponsePtr&, std::uint64_t);
    std::uint64_t beginImpulseResponseRequest() noexcept;

    std::unique_ptr<Engine> convolution;
    juce::dsp::StateVariableTPTFilter<float> lowCut;
    juce::dsp::StateVariableTPTFilter<float> highCut;
    juce::AudioBuffer<float> wetBuffer;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        driveSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        bodySmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        presenceSmoothed;
    std::array<float, 2> bodyState {};
    std::array<float, 2> presenceState {};
    juce::dsp::ProcessSpec preparedSpec { 44100.0, 512, 2 };
    double sampleRate = 44100.0;
    std::atomic<std::uint64_t> requestCounter { 0 };
    std::atomic<std::uint64_t> requestedGeneration { 0 };
    std::atomic<std::uint64_t> committedGeneration { 0 };
    std::atomic<bool> committedImpulseAvailable { false };
    std::atomic<double> impulseLengthSeconds { 0.0 };
    mutable juce::CriticalSection metadataLock;
    juce::String loadedImpulseName;
    ImpulseResponsePreview preview {};
    ContinuousTelemetrySnapshot telemetryState;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
};
} // namespace megadsp
