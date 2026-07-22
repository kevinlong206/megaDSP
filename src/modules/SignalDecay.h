#pragma once

#include "DspModule.h"

namespace megadsp
{
class SignalDecayModule final
    : public DspModule,
      public ContinuousTelemetryCapability,
      public EventTelemetryCapability
{
public:
    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    int latencySamples() const override { return fixedLatencySamples; }
    double tailSeconds(const ControlValues&) const override;
    bool readContinuousTelemetry(
        ContinuousTelemetrySnapshot&) const noexcept override;
    bool readEventTelemetry(
        EventTelemetrySnapshot&) const noexcept override;
    ContinuousTelemetryCapability*
        continuousTelemetryCapability() noexcept override { return this; }
    const ContinuousTelemetryCapability*
        continuousTelemetryCapability() const noexcept override { return this; }
    EventTelemetryCapability*
        eventTelemetryCapability() noexcept override { return this; }
    const EventTelemetryCapability*
        eventTelemetryCapability() const noexcept override { return this; }

    enum TelemetryValue
    {
        leftDropoutGain,
        rightDropoutGain,
        leftClockPhase,
        rightClockPhase,
        leftClockJitter,
        rightClockJitter,
        stereoWearAmount,
        currentSampleRate,
        telemetryValueCount
    };

    enum TelemetryEventKind
    {
        dropoutStarted = 1
    };

private:
    float randomBipolar() noexcept;
    float readDelay(int channel, float delaySamples) const noexcept;

    std::array<std::vector<float>, 2> wetHistory;
    std::array<std::vector<float>, 2> dryHistory;
    std::array<float, 2> heldSample {};
    std::array<float, 2> bandwidthState {};
    std::array<float, 2> noiseState {};
    std::array<float, 2> clockPhase {};
    std::array<float, 2> jitterState {};
    std::array<float, 2> dropoutGain { 1.0f, 1.0f };
    std::array<int, 2> dropoutRemaining {};
    std::array<int, 2> dropoutLength {};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        resolutionSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        sampleRateSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> jitterSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> dropoutSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        bandwidthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        noiseSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wowSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> flutterSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        stereoWearSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
    std::uint32_t randomState = 0x9e3779b9u;
    double sampleRate = 44100.0;
    double wowPhase = 0.0;
    double flutterPhase = 0.0;
    int writePosition = 0;
    int dryPosition = 0;
    int fixedLatencySamples = 1;
    bool initialized = false;
    std::uint64_t telemetrySequence = 0;
    std::uint64_t telemetryEventSequence = 0;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
    FixedAtomicSnapshot<EventTelemetrySnapshot> eventTelemetry;
};
} // namespace megadsp
