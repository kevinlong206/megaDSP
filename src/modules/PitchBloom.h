#pragma once

#include "DspModule.h"

namespace megadsp
{
enum class PitchBloomTelemetryEventKind : std::uint32_t
{
    shiftedRepeat = 1
};

enum class PitchBloomTelemetryValue : size_t
{
    intervalSemitones = 0,
    intervalSeconds,
    energy,
    stereoSpread
};

class PitchBloomModule final
    : public DspModule,
      public EventTelemetryCapability
{
public:
    static constexpr int reportedLatencySamples = 1024;
    static constexpr int pitchSpanSamples = 1984;
    static constexpr int pitchMinimumDelaySamples = 32;
    static constexpr int diffuserStageCount = 3;

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    int latencySamples() const override { return reportedLatencySamples; }
    double tailSeconds(const ControlValues&) const override;
    bool readEventTelemetry(
        EventTelemetrySnapshot&) const noexcept override;
    EventTelemetryCapability* eventTelemetryCapability() noexcept override
    {
        return this;
    }
    const EventTelemetryCapability*
        eventTelemetryCapability() const noexcept override
    {
        return this;
    }

private:
    struct AllpassStage
    {
        std::array<std::vector<float>, 2> buffer;
        std::array<int, 2> writePosition {};
    };

    float readCircular(const std::vector<float>&, int writePosition,
                       float delaySamples) const;
    float processPitch(int channel, float input, float ratio,
                       float oldRatio, float transition);
    float processBloom(int channel, float input, float amount);
    void beginTelemetryBlock(bool capture) noexcept;
    void captureTelemetrySample(
        const std::array<float, 2>& shifted, float delaySamples,
        float ratio, bool stereo) noexcept;
    void addTelemetryEvent(
        float intervalSemitones, float intervalSeconds, float energy,
        float pan, float spread, bool stereo) noexcept;
    std::array<std::vector<float>, 2> repeatDelay;
    std::array<std::vector<float>, 2> pitchBuffer;
    std::array<std::array<float, reportedLatencySamples + 1>, 2> dryDelay {};
    std::array<AllpassStage, diffuserStageCount> diffuser;
    std::array<float, 2> lowpassState {};
    std::array<float, 2> highpassState {};
    std::array<float, 2> highpassInput {};
    std::array<float, 2> feedbackState {};
    std::array<float, 2> pitchPhase { 0.0f, 0.25f };
    std::array<float, 2> previousPitchPhase { 0.0f, 0.25f };
    std::array<float, 2> pitchInputEnvelope {};
    std::array<int, 2> transientResetCooldown {};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        ratioSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        intervalTransition;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        delaySamplesSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        feedbackSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        bloomSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        spreadSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        lowCutSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        highCutSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        duckingSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
    double sampleRate = 44100.0;
    float inputEnvelope = 0.0f;
    float currentRatio = 1.0f;
    float previousRatio = 1.0f;
    int repeatWritePosition = 0;
    int pitchWritePosition = 0;
    int dryWritePosition = 0;
    int currentInterval = -1;
    bool parametersInitialised = false;
    EventTelemetrySnapshot telemetryWorking;
    FixedAtomicSnapshot<EventTelemetrySnapshot> telemetry;
    std::array<double, 2> telemetryEnergy {};
    double telemetryDifferenceEnergy = 0.0;
    float telemetryPhaseSamples = 0.0f;
    std::uint32_t telemetrySampleCount = 0;
    std::uint64_t telemetryEventSequence = 0;
    std::uint64_t telemetryPublicationSequence = 0;
    bool telemetryWasCapturing = false;
};
} // namespace megadsp
