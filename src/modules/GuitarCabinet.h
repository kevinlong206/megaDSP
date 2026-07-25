#pragma once

#include "DspModule.h"

namespace megadsp
{
class GuitarCabinetModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    static constexpr int speakerModelCount = 4;
    static constexpr int cabinetModelCount = 4;
    static constexpr int microphoneModelCount = 3;
    static constexpr int oversamplingFactor = 4;

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    int latencySamples() const override { return processingLatency; }
    double tailSeconds(const ControlValues&) const override;

    bool readContinuousTelemetry(
        ContinuousTelemetrySnapshot&) const noexcept override;
    ContinuousTelemetryCapability*
        continuousTelemetryCapability() noexcept override { return this; }
    const ContinuousTelemetryCapability*
        continuousTelemetryCapability() const noexcept override { return this; }

    enum TelemetryValue
    {
        coneExcursion,
        powerCompression,
        breakupEnergy,
        cabinetEnergy,
        roomEnergy,
        telemetryValueCount
    };

private:
    struct StateVariableFilter
    {
        struct Output
        {
            float low = 0.0f;
            float band = 0.0f;
            float high = 0.0f;
        };

        Output process(float input, float g, float inverseQ) noexcept;
        void reset() noexcept;

        float state1 = 0.0f;
        float state2 = 0.0f;
    };

    struct SpeakerChannelState
    {
        StateVariableFilter breakupFilter;
        float excursion = 0.0f;
        float dcInput = 0.0f;
        float dcOutput = 0.0f;
    };

    struct SpeakerState
    {
        std::array<SpeakerChannelState, 2> channels;
        float powerEnvelope = 0.0f;
    };

    struct CabinetChannelState
    {
        std::array<StateVariableFilter, 2> modes;
        float lowCutState = 0.0f;
        float highCutState = 0.0f;
    };

    struct CabinetState
    {
        std::array<CabinetChannelState, 2> channels;
    };

    struct MicrophoneChannelState
    {
        StateVariableFilter presenceFilter;
        float lowCutState = 0.0f;
        float proximityState = 0.0f;
        float highCutState = 0.0f;
    };

    struct MicrophoneState
    {
        std::array<MicrophoneChannelState, 2> channels;
    };

    struct ParameterFrame
    {
        std::array<float, speakerModelCount> speakerWeights {};
        std::array<float, cabinetModelCount> cabinetWeights {};
        std::array<float, microphoneModelCount> microphoneWeights {};
        float driveGain = 1.0f;
        float driveAmount = 0.0f;
        float breakup = 0.0f;
        float cabinetResonance = 0.0f;
        float damping = 0.5f;
        float micPosition = 0.5f;
        float micDistanceCm = 5.0f;
        float room = 0.0f;
        float roomWidth = 0.5f;
        float output = 1.0f;
    };

    float renderSpeaker(int model, int channel, float input,
                        const ParameterFrame&, float compressionGain,
                        float& excursionActivity,
                        float& breakupActivity) noexcept;
    float renderCabinet(int model, int channel, float input,
                        const ParameterFrame&,
                        float& cabinetActivity) noexcept;
    float renderMicrophone(int model, int channel, float input,
                           const ParameterFrame&) noexcept;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    std::vector<ParameterFrame> parameterFrames;
    std::vector<float> roomHistory;
    std::array<SpeakerState, speakerModelCount> speakerStates;
    std::array<CabinetState, cabinetModelCount> cabinetStates;
    std::array<MicrophoneState, microphoneModelCount> microphoneStates;

    std::array<juce::SmoothedValue<float,
        juce::ValueSmoothingTypes::Linear>, speakerModelCount> speakerMix;
    std::array<juce::SmoothedValue<float,
        juce::ValueSmoothingTypes::Linear>, cabinetModelCount> cabinetMix;
    std::array<juce::SmoothedValue<float,
        juce::ValueSmoothingTypes::Linear>, microphoneModelCount> microphoneMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        driveSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        breakupSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        resonanceSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        dampingSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        positionSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        distanceSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        roomSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        widthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;

    std::array<float, speakerModelCount> excursionCoefficients {};
    std::array<float, speakerModelCount> breakupFilterG {};
    std::array<std::array<float, 2>, cabinetModelCount> cabinetModeG {};
    std::array<float, cabinetModelCount> cabinetLowCutCoefficients {};
    std::array<float, cabinetModelCount> cabinetHighCutCoefficients {};
    std::array<float, microphoneModelCount> microphonePresenceG {};

    double sampleRate = 44100.0;
    float oversampledAttackCoefficient = 0.0f;
    float oversampledReleaseCoefficient = 0.0f;
    float dcRetention = 0.0f;
    float roomDampingState = 0.0f;
    int roomWritePosition = 0;
    int processingLatency = 0;
    bool initialized = false;
    ContinuousTelemetrySnapshot telemetryState;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
};
} // namespace megadsp
