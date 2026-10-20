#pragma once

#include "DspModule.h"

namespace megadsp
{
class ConvolutionReverbModule
    : public DspModule,
      public ImpulseResponseCapability,
      public ContinuousTelemetryCapability
{
public:
    static constexpr int spectrumBandCount = 12;
    static constexpr int impulseBankCount = 3;
    static constexpr int defaultWetProcessingLatencySamples = 2048;
    static constexpr std::array<int, 4> wetProcessingLatencyChoices {
        256, 512, 1024, 2048
    };

    enum TelemetryValue
    {
        leftWetLevel,
        rightWetLevel,
        outputChannelCount,
        spectrumBandStart,
        telemetryValueCount = spectrumBandStart + spectrumBandCount
    };

    enum TelemetryHistory
    {
        leftWetHistory,
        rightWetHistory,
        leftBrightnessHistory,
        rightBrightnessHistory,
        telemetryHistoryCount
    };
    static_assert(
        telemetryValueCount <= continuousTelemetryValueCapacity);

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
    juce::Result prepareImpulseResponseForBank(
        const juce::File&, PreparedImpulseResponsePtr&);
    bool commitPreparedImpulseResponse(
        PreparedImpulseResponsePtr&) override;
    bool commitPreparedImpulseResponseToBank(
        int bank, PreparedImpulseResponsePtr&);
    bool selectImpulseResponseBank(int bank);
    void clearAllImpulseResponseBanks();
    void setWetProcessingLatencySamples(int samples) noexcept;
    int getWetProcessingLatencySamples() const noexcept
    {
        return configuredWetProcessingLatencySamples.load(
            std::memory_order_acquire);
    }
    int activeImpulseResponseBank() const noexcept
    {
        return activeBank.load(std::memory_order_acquire);
    }
    juce::String impulseResponseBankName(int bank) const;
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

protected:
    bool renderConvolution(const juce::AudioBuffer<float>&,
                           const ProcessEnvironment&);
    const juce::AudioBuffer<float>& convolvedBuffer() const noexcept
    {
        return wetBuffer;
    }

private:
    struct Engine;
    struct Bank;
    struct PreparedLoad;

    bool hasCurrentImpulseResponse() const noexcept;
    std::uint64_t beginImpulseResponseRequest() noexcept;
    juce::Result prepareImpulseResponseInternal(
        const juce::File&, PreparedImpulseResponsePtr&,
        bool markActivePending);

    std::array<std::unique_ptr<Bank>, impulseBankCount> banks;
    std::atomic<int> activeBank { 0 };
    std::atomic<int> configuredWetProcessingLatencySamples {
        defaultWetProcessingLatencySamples
    };
    juce::dsp::StateVariableTPTFilter<float> lowCut;
    juce::dsp::StateVariableTPTFilter<float> highCut;
    juce::AudioBuffer<float> wetBuffer;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> drySmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        duckAmountSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
    double sampleRate = 44100.0;
    juce::dsp::ProcessSpec preparedSpec { 44100.0, 512, 2 };
    std::atomic<std::uint64_t> requestCounter { 0 };
    std::atomic<std::uint64_t> requestedGeneration { 0 };
    std::atomic<bool> activeReplacementPending { false };
    float duckEnvelope = 0.0f;
    float duckGainDb = 0.0f;
    ContinuousTelemetrySnapshot telemetryState;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
    std::array<double, 2> telemetryEnergy {};
    std::array<std::array<double, spectrumBandCount>, 2>
        telemetrySpectrumEnergy {};
    std::array<
        std::array<float, spectrumBandCount - 1>, 2>
        telemetrySpectrumLowpass {};
    std::array<float, spectrumBandCount - 1>
        telemetrySpectrumCoefficients {};
    int telemetrySampleCount = 0;
    int telemetryCountdown = 1;
    int telemetryInterval = 1;
    bool telemetryWasCapturing = false;
    mutable juce::CriticalSection metadataLock;
};
} // namespace megadsp
