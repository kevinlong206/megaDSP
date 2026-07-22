#pragma once

#include "DspModule.h"

namespace megadsp
{
enum class DiffusionDelayTelemetryEventKind : std::uint32_t
{
    primaryRepeat = 1,
    diffusionCloud = 2
};

enum class DiffusionDelayTelemetryValue : size_t
{
    energy = 0,
    intervalSeconds,
    diffusion,
    stereoSpread
};

class DiffusionDelayModule final
    : public DspModule,
      public EventTelemetryCapability
{
public:
    static constexpr int diffuserStageCount = 4;

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
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
        float phase = 0.0f;
    };

    float readPrimary(int channel, float delaySamples) const;
    float processDiffuser(int channel, float input, float amount,
                          float movement);
    void beginTelemetryBlock(bool capture) noexcept;
    void captureTelemetrySample(
        const std::array<float, 2>& primary,
        const std::array<float, 2>& cloud, float delaySamples,
        float diffusion, bool stereo) noexcept;
    void addTelemetryEvent(
        DiffusionDelayTelemetryEventKind, float energy, float intervalSeconds,
        float diffusion, float pan, float spread, bool stereo) noexcept;
    std::array<std::vector<float>, 2> primaryDelay;
    std::array<AllpassStage, diffuserStageCount> diffuser;
    std::array<float, 2> lowpassState {};
    std::array<float, 2> highpassState {};
    std::array<float, 2> highpassInput {};
    float sideLowpassState = 0.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        delaySamplesSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        feedbackSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        diffusionSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        movementSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        lowCutSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        highCutSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        widthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        duckingSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
    double sampleRate = 44100.0;
    float inputEnvelope = 0.0f;
    int primaryWritePosition = 0;
    bool parametersInitialised = false;
    EventTelemetrySnapshot telemetryWorking;
    FixedAtomicSnapshot<EventTelemetrySnapshot> telemetry;
    std::array<double, 2> telemetryPrimaryEnergy {};
    std::array<double, 2> telemetryCloudEnergy {};
    double telemetryPrimaryDifferenceEnergy = 0.0;
    double telemetryCloudDifferenceEnergy = 0.0;
    float telemetryPhaseSamples = 0.0f;
    std::uint32_t telemetrySampleCount = 0;
    std::uint64_t telemetryEventSequence = 0;
    std::uint64_t telemetryPublicationSequence = 0;
    bool telemetryWasCapturing = false;
};
} // namespace megadsp
