#pragma once

#include "DspModule.h"

namespace megadsp
{
class RandomGranulizerModule final
    : public DspModule,
      public GrainVisualizationCapability
{
public:
    static constexpr int maximumVoices = 16;

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    double tailSeconds(const ControlValues&) const override;
    float meterValue() const override
    {
        return static_cast<float>(activeVoices.load(std::memory_order_relaxed));
    }
    GrainVisualEvents grainVisualEvents() const noexcept override;
    GrainVisualEvents visualEvents() const noexcept
    {
        return grainVisualEvents();
    }
    GrainVisualizationCapability*
        grainVisualizationCapability() noexcept override
    {
        return this;
    }
    const GrainVisualizationCapability*
        grainVisualizationCapability() const noexcept override
    {
        return this;
    }
    int activeVoiceCount() const
    {
        return activeVoices.load(std::memory_order_relaxed);
    }
    int maximumObservedVoiceCount() const
    {
        return maximumObservedVoices.load(std::memory_order_relaxed);
    }

private:
    struct Voice
    {
        bool active = false;
        double readPosition = 0.0;
        float increment = 1.0f;
        int duration = 1;
        int age = 0;
        int preLaunchDelay = 0;
        float pan = 0.0f;
        float leftGain = 1.0f;
        float rightGain = 1.0f;
        float gain = 1.0f;
        float filterCoefficient = 0.0f;
        std::array<float, 2> filterState {};
        std::uint32_t visualSequence = 0;
    };

    struct PendingGrain
    {
        bool active = false;
        int remainingDelay = 0;
        int originalDelay = 0;
        int waitSamples = 0;
        int duration = 1;
        float scatter = 0.0f;
        float pan = 0.0f;
        float cutoff = 12000.0f;
        bool reverse = false;
        int voiceCap = 1;
    };

    struct AtomicVisualEvent
    {
        std::atomic<std::uint32_t> publicationSequence { 0 };
        std::atomic<std::uint32_t> sequence { 0 };
        std::atomic<float> historyPosition { 0.0f };
        std::atomic<float> durationSeconds { 0.0f };
        std::atomic<float> progress { 1.0f };
        std::atomic<float> pan { 0.0f };
        std::atomic<float> filter { 1.0f };
        std::atomic<bool> reverse { false };
    };

    std::uint32_t nextRandom();
    float randomUnit();
    int randomInteger(int low, int high);
    float readHistory(int channel, double position) const;
    void scheduleGrain(const ControlValues&, double bpm);
    bool launchGrain(PendingGrain&);
    void publishEvent(Voice&, float historyPosition, float cutoff, bool reverse);

    std::array<std::vector<float>, 2> history;
    std::array<Voice, maximumVoices> voices;
    std::array<PendingGrain, 64> pending;
    std::array<AtomicVisualEvent, grainVisualEventCount> visual;
    std::array<float, 2> feedbackState {};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        feedbackSmoothed;
    double sampleRate = 44100.0;
    std::uint64_t writtenSamples = 0;
    int writePosition = 0;
    int validHistorySamples = 0;
    int inputChannels = 2;
    double samplesUntilLaunch = 0.0;
    std::uint32_t randomState = 0x6d2b79f5u;
    std::uint32_t eventSequence = 0;
    bool parametersInitialized = false;
    std::atomic<int> activeVoices { 0 };
    std::atomic<int> maximumObservedVoices { 0 };
};
} // namespace megadsp
