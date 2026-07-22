#pragma once

#include "DspModule.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace megadsp
{
class BeatPermuterModule final
    : public DspModule,
      public BeatPermutationVisualizationCapability
{
public:
    static constexpr int maximumWindowSlices = 8;
    static constexpr int maximumEventSlices = 8;

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    double tailSeconds(const ControlValues&) const override;
    float meterValue() const override
    {
        return outputMeter.load(std::memory_order_relaxed);
    }

    BeatPermutationVisualEvents
        beatPermutationVisualEvents() const noexcept override;
    BeatPermutationVisualEvents visualEvents() const noexcept
    {
        return beatPermutationVisualEvents();
    }
    BeatPermutationVisualizationCapability*
        beatPermutationVisualizationCapability() noexcept override
    {
        return this;
    }
    const BeatPermutationVisualizationCapability*
        beatPermutationVisualizationCapability() const noexcept override
    {
        return this;
    }

private:
    struct PermutationEvent
    {
        bool active = false;
        int pattern = 0;
        int windowSlices = 1;
        int lengthSlices = 1;
        int currentStep = 0;
        double sliceSamples = 1.0;
        std::uint64_t sourceBoundary = 0;
        float gate = 1.0f;
        float pitchRatio = 1.0f;
        float stereoOffset = 0.0f;
        float variation = 0.0f;
        std::uint32_t visualSequence = 0;
        std::array<std::array<int, maximumEventSlices>, 2> sourceOffsets {};
        std::array<std::array<float, maximumEventSlices>, 2> phaseOffsets {};
        std::array<std::array<bool, maximumEventSlices>, 2> reversePlayback {};
    };

    struct AtomicVisualEvent
    {
        std::atomic<std::uint32_t> publicationSequence { 0 };
        std::atomic<std::uint32_t> sequence { 0 };
        std::atomic<float> sourcePosition { 0.0f };
        std::atomic<float> progress { 1.0f };
        std::atomic<float> windowSize { 0.0f };
        std::atomic<float> repeatCount { 0.0f };
        std::atomic<float> gate { 1.0f };
        std::atomic<float> stereoBias { 0.0f };
        std::atomic<int> pattern { 0 };
        std::atomic<bool> reverse { false };
    };

    std::uint32_t nextRandom();
    float randomUnit();
    int randomInteger(int low, int high);
    double sliceDurationSamples(float normalizedGrid, double bpm) const;
    void beginSlice(const ControlValues&, double bpm);
    void finishActiveEvent();
    void startPermutationEvent(const ControlValues&, double bpm);
    float sampleAt(int channel, std::int64_t absoluteSample) const;
    float readHistory(int channel, double absoluteSample) const;
    std::array<float, 2> renderWetSample(double sliceOffset) const;
    void publishEvent(const PermutationEvent&);
    void updateVisualProgress();

    std::array<std::vector<float>, 2> history;
    std::array<AtomicVisualEvent, beatPermutationVisualEventCount> visual;
    std::array<float, 2> regenerationState {};
    std::array<float, 2> transitionFrom {};
    std::array<float, 2> lastWetFrame {};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        regenerationSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
    PermutationEvent activeEvent;
    double sampleRate = 44100.0;
    double currentSliceSamples = 1.0;
    double sliceSamplePosition = 0.0;
    std::uint64_t writtenSamples = 0;
    std::uint64_t sliceCounter = 0;
    std::uint32_t randomState = 0x9e3779b9u;
    int writePosition = 0;
    int validHistorySamples = 0;
    int transitionSamples = 0;
    int transitionPosition = 0;
    bool parametersInitialized = false;
    bool transportInitialized = false;
    std::atomic<float> outputMeter { 0.0f };
};
} // namespace megadsp
