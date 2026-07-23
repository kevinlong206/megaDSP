#pragma once

#include "DspModule.h"
#include "SpectralFoundation.h"

namespace megadsp
{
class TimeMosaicModule final
    : public DspModule,
      public ContinuousTelemetryCapability,
      public EventTelemetryCapability
{
public:
    enum ControlIndex
    {
        historyControl,
        tileWidthControl,
        tileTimeControl,
        ageControl,
        motionControl,
        coherenceControl,
        pitchDriftControl,
        freezeControl,
        stereoSpreadControl,
        mixControl,
        outputControl
    };

    enum TelemetryValue
    {
        activeHistorySeconds,
        actualMeanAgeSeconds,
        actualTileWidthOctaves,
        actualTileSeconds,
        actualMotion,
        actualCoherence,
        actualPitchCents,
        frozen,
        telemetryValueCount
    };

    enum TelemetryHistory
    {
        lowTileAgeHistory,
        midTileAgeHistory,
        highTileAgeHistory,
        wetEnergyHistory,
        telemetryHistoryValueCount
    };

    enum EventKind : std::uint32_t
    {
        tileReassigned = 1,
        freezeChanged = 2
    };

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    int latencySamples() const override
    {
        return FixedLatencyStft::reportedLatencySamples;
    }
    double tailSeconds(const ControlValues&) const override;
    float meterValue() const override
    {
        return wetMeter.load(std::memory_order_relaxed);
    }
    float detectorValue() const override
    {
        return ageMeter.load(std::memory_order_relaxed);
    }

    bool readContinuousTelemetry(
        ContinuousTelemetrySnapshot&) const noexcept override;
    ContinuousTelemetryCapability*
        continuousTelemetryCapability() noexcept override { return this; }
    const ContinuousTelemetryCapability*
        continuousTelemetryCapability() const noexcept override { return this; }
    bool readEventTelemetry(
        EventTelemetrySnapshot&) const noexcept override;
    EventTelemetryCapability*
        eventTelemetryCapability() noexcept override { return this; }
    const EventTelemetryCapability*
        eventTelemetryCapability() const noexcept override { return this; }

private:
    static constexpr float maximumHistorySeconds = 8.0f;

    static void processFrameCallback(
        void*, FixedLatencyStft::Complex* const*, int, int) noexcept;
    static float outputCallback(
        void*, int, float, float) noexcept;
    void processFrame(
        FixedLatencyStft::Complex* const*, int, int) noexcept;
    FixedLatencyStft::Complex readHistory(
        int channel, int bin, float framesAgo) const noexcept;
    void assignTiles(int bins) noexcept;
    void addTileEvent(
        int firstBin, float normalizedAge, float semitones,
        float energy) noexcept;
    void addFreezeEvent() noexcept;
    void publishTelemetry(bool capture) noexcept;
    static float hashSigned(std::uint32_t value) noexcept;

    FixedLatencyStft stft;
    std::array<FixedSpectralHistory, FixedLatencyStft::maxChannels> history;
    std::array<std::vector<float>, FixedLatencyStft::maxChannels> writeScratch;
    std::vector<float> ageStart;
    std::vector<float> ageTarget;
    std::vector<float> ageCurrent;
    std::vector<float> ageProgress;
    std::vector<float> pitchByBin;
    std::vector<float> binEnergy;
    std::vector<std::uint32_t> tileIdByBin;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
    double sampleRate = 44100.0;
    float historySeconds = 2.0f;
    float baseAge = 0.5f;
    float motion = 0.5f;
    float coherence = 0.75f;
    float pitchDriftSemitones = 0.0f;
    float stereoSpread = 0.5f;
    float tileSeconds = 0.15f;
    float tileWidthOctaves = 0.5f;
    float currentMix = 1.0f;
    float currentOutput = 1.0f;
    float meanAgeSeconds = 0.0f;
    float meanPitchSemitones = 0.0f;
    float currentWetEnergy = 0.0f;
    std::array<float, 3> bandMeanAge {};
    int tileBins = 8;
    int activeHistoryFrames = 1;
    int transitionFrames = 1;
    int framesUntilAssignment = 0;
    bool freeze = false;
    bool previousFreeze = false;
    bool parametersInitialised = false;
    bool captureCurrentBlock = false;
    std::uint32_t assignmentGeneration = 0;
    std::uint64_t continuousSequence = 0;
    std::uint64_t eventSequence = 0;
    std::uint64_t eventPublicationSequence = 0;
    ContinuousTelemetrySnapshot continuousState;
    EventTelemetrySnapshot eventWorking;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> continuousTelemetry;
    FixedAtomicSnapshot<EventTelemetrySnapshot> eventTelemetry;
    std::atomic<float> wetMeter { 0.0f };
    std::atomic<float> ageMeter { 0.0f };
};
} // namespace megadsp
