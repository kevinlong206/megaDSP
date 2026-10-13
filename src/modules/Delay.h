#pragma once

#include "DspModule.h"

namespace megadsp
{
class DelayModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    static constexpr int styleCount = 4;
    static constexpr int stereoModeCount = 4;

    enum TelemetryValue : size_t
    {
        intervalSeconds = 0,
        leftWetEnergy,
        rightWetEnergy,
        duckingGain,
        movementSamples,
        activeStyle,
        telemetryValueCount
    };

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    double tailSeconds(const ControlValues&) const override;
    bool readContinuousTelemetry(
        ContinuousTelemetrySnapshot&) const noexcept override;
    ContinuousTelemetryCapability*
        continuousTelemetryCapability() noexcept override
    {
        return this;
    }
    const ContinuousTelemetryCapability*
        continuousTelemetryCapability() const noexcept override
    {
        return this;
    }

private:
    struct ColourState
    {
        float lowpass = 0.0f;
        float highpass = 0.0f;
        float highpassInput = 0.0f;
        float memory = 0.0f;
    };

    float readDelay(int channel, float delaySamples) const noexcept;
    float processColour(
        int style, int channel, float input, float tone,
        float character) noexcept;
    float movementFor(
        int style, int channel, float amount) noexcept;
    float randomBipolar(int channel) noexcept;
    void startTimeTransition(
        const std::array<float, 2>& targets) noexcept;
    void publishTelemetry(
        bool capture, int sampleCount, float interval,
        const std::array<double, 2>& wetEnergy,
        float duckGain, float movement, int style) noexcept;

    std::array<std::vector<float>, 2> delayBuffer;
    std::array<std::array<ColourState, 2>, styleCount> colourStates;
    std::array<std::uint32_t, 2> randomState {
        0x9e3779b9u, 0x243f6a88u
    };
    std::array<float, 2> driftState {};
    std::array<float, 2> currentDelaySamples {};
    std::array<float, 2> oldDelaySamples {};
    std::array<float, 2> targetDelaySamples {};
    std::array<float, 2> lastRequestedDelaySamples {};
    int writePosition = 0;
    double sampleRate = 44100.0;
    double wowPhase = 0.0;
    double flutterPhase = 0.0;
    float timeTransition = 1.0f;
    float styleTransition = 1.0f;
    float stereoTransition = 1.0f;
    float inputEnvelope = 0.0f;
    float lastDuckGain = 1.0f;
    int activeStyleIndex = 0;
    int previousStyleIndex = 0;
    int activeStereoMode = 0;
    int previousStereoMode = 0;
    bool parametersInitialised = false;
    std::uint64_t telemetrySequence = 0;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        feedbackSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        characterSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        spreadSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        movementSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        toneSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        duckingSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        mixSmoothed;
};
} // namespace megadsp
