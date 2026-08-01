#pragma once

#include "DspModule.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace megadsp
{
class PortalPermuterModule final
    : public DspModule,
      public ImpulseResponseCapability,
      public ContinuousTelemetryCapability
{
public:
    enum TelemetryValue
    {
        eventActiveValue,
        sourceSliceValue,
        reverseValue,
        progressValue,
        wetLevelValue,
        telemetryValueCount
    };

    PortalPermuterModule();
    ~PortalPermuterModule() override;

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
    bool isImpulseResponseReady() const noexcept override;
    juce::String impulseResponseName() const override;
    ImpulseResponsePreview impulseResponsePreview() const override;
    double currentImpulseResponseTailSeconds() const noexcept override;
    ImpulseResponseTransform impulseResponseTransform(
        const float*, int) const noexcept override;
    juce::Result prepareImpulseResponse(
        int, const juce::File&, const ImpulseResponseTransform&,
        PreparedImpulseResponsePtr&) override;

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
    static constexpr int maximumHistorySlices = 8;
    static constexpr int maximumEventSteps = 8;

    struct Engine;
    struct PreparedLoad;

    std::uint32_t nextRandom() noexcept;
    float randomUnit() noexcept;
    int randomInteger(int, int) noexcept;
    double sliceSamples(float, double) const noexcept;
    void beginSlice(const ControlValues&, double);
    float historySample(int, std::int64_t) const noexcept;
    float readHistory(int, double) const noexcept;
    std::array<float, 2> renderExcitation() const noexcept;
    std::uint64_t beginRequest() noexcept;

    std::unique_ptr<Engine> convolution;
    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> excitationBuffer;
    std::array<std::vector<float>, 2> history;
    std::array<std::vector<float>, 2> feedbackDelay;
    std::array<float, 2> feedbackFilter {};
    std::array<float, 2> transitionFrom {};
    std::array<float, 2> lastExcitation {};
    std::array<int, maximumEventSteps> sourceOffsets {};
    std::array<bool, maximumEventSteps> reverseSteps {};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        feedbackSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
    juce::dsp::ProcessSpec preparedSpec { 44100.0, 512, 2 };
    double sampleRate = 44100.0;
    double currentSliceSamples = 1.0;
    double slicePosition = 0.0;
    double playbackLengthSamples = 1.0;
    double pitchRatio = 1.0;
    std::uint64_t writtenSamples = 0;
    std::uint64_t sourceBoundary = 0;
    std::uint32_t randomState = 0x51f15e5du;
    int historyWritePosition = 0;
    int validHistorySamples = 0;
    int feedbackPosition = 0;
    int eventStep = 0;
    int eventLength = 0;
    int transitionPosition = 0;
    int transitionSamples = 1;
    bool eventActive = false;
    bool transportInitialized = false;
    bool parametersInitialized = false;

    std::atomic<std::uint64_t> requestCounter { 0 };
    std::atomic<std::uint64_t> requestedGeneration { 0 };
    std::atomic<std::uint64_t> committedGeneration { 0 };
    std::atomic<bool> impulseAvailable { false };
    std::atomic<double> impulseLengthSeconds { 0.0 };
    mutable juce::CriticalSection metadataLock;
    juce::String loadedImpulseName;
    ImpulseResponsePreview preview {};
    ContinuousTelemetrySnapshot telemetryState;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
    int telemetryCountdown = 1;
    int telemetryInterval = 1;
};
} // namespace megadsp
