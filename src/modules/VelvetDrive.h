#pragma once

#include "DspModule.h"

#include <cmath>

namespace megadsp
{
// Velvet Drive is a fully modeled, legally distinct D-style guitar amplifier.
// It has no neural, impulse-response, capture, or external-asset dependency:
// every stage is analytic DSP. Signal flow is a Normal/FET input voicing, a
// four-stage cascaded triode-like preamp with interstage RC coupling,
// asymmetry, soft grid conduction and bounded bias shift, a passive
// interactive Bass/Mid/Treble tone stack (pre-clip for Classic/High Headroom,
// post-clip for Modern), a phase inverter and push-pull Class-AB power stage
// with frequency-dependent presence feedback and a stereo-linked dynamic
// supply sag, then analytic speaker voicings and a calibrated master output.
// The nonlinear preamp and power stages run through prepared 4x oversampling
// and report one fixed latency. There is no dry/wet blend: this is a complete
// amp model, so silent input stays silent.
class VelvetDriveModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    static constexpr int inputModeCount = 2;
    static constexpr int voiceCount = 3;
    static constexpr int speakerModelCount = 4;
    static constexpr int preampStageCount = 4;
    static constexpr int oversamplingFactor = 4;

    enum Control
    {
        inputControl,
        voiceControl,
        driveControl,
        ratioControl,
        levelControl,
        bassControl,
        midControl,
        trebleControl,
        presenceControl,
        feelControl,
        speakerControl,
        masterControl
    };

    enum TelemetryValue
    {
        inputEnvelope,
        preampActivity,
        sagReduction,
        powerActivity,
        outputEnvelope,
        telemetryValueCount
    };

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    int latencySamples() const override { return processingLatency; }

    bool readContinuousTelemetry(
        ContinuousTelemetrySnapshot&) const noexcept override;
    ContinuousTelemetryCapability*
        continuousTelemetryCapability() noexcept override { return this; }
    const ContinuousTelemetryCapability*
        continuousTelemetryCapability() const noexcept override
    {
        return this;
    }

private:
    struct OnePole
    {
        float state = 0.0f;
        float processLowpass(float input, float coefficient) noexcept
        {
            state += coefficient * (input - state);
            if (!std::isfinite(state))
                state = 0.0f;
            return state;
        }
        void reset() noexcept { state = 0.0f; }
    };

    struct StateVariableFilter
    {
        float state1 = 0.0f;
        float state2 = 0.0f;
        float processBand(float input, float g, float inverseQ) noexcept;
        void reset() noexcept { state1 = 0.0f; state2 = 0.0f; }
    };

    struct InputStageState
    {
        OnePole blocking;
        OnePole brightState;
    };

    struct PreampStageState
    {
        OnePole coupling;
        OnePole miller;
        float biasState = 0.0f;
    };

    struct ToneState
    {
        OnePole low;
        OnePole high;
    };

    struct PowerState
    {
        OnePole feedbackLow;
        float feedbackShaped = 0.0f;
        float dcInput = 0.0f;
        float dcOutput = 0.0f;
    };

    struct SpeakerChannelState
    {
        OnePole highPass;
        StateVariableFilter resonanceA;
        StateVariableFilter resonanceB;
        OnePole sparkle;
        OnePole lowPass;
        float dcInput = 0.0f;
        float dcOutput = 0.0f;
    };

    struct ChannelState
    {
        InputStageState input;
        std::array<PreampStageState, preampStageCount> stages;
        ToneState preTone;
        ToneState postTone;
        PowerState power;
        std::array<SpeakerChannelState, speakerModelCount> speakers;
        OnePole outputDcLow;
    };

    struct ParameterFrame
    {
        float inputFetWeight = 0.0f;
        std::array<float, voiceCount> voiceWeight { 1.0f, 0.0f, 0.0f };
        float driveScale = 1.0f;
        float driveAmount = 0.0f;
        float ratio = 0.0f;
        float odLevel = 1.0f;
        float bass = 0.5f;
        float mid = 0.5f;
        float treble = 0.5f;
        float presence = 0.5f;
        float feelDepth = 0.0f;
        std::array<float, speakerModelCount> speakerWeight {};
        float master = 1.0f;
        // Voice-blended scalars.
        float stageGainScale = 1.0f;
        float knee = 1.0f;
        float asymmetry = 0.2f;
        float compression = 0.4f;
        float preToneWeight = 1.0f;
        float postToneWeight = 0.0f;
        float sagCoefficient = 0.0f;
    };

    float triodeStage(int stage, ChannelState&, float input,
                      const ParameterFrame&, float gain) noexcept;
    float toneStack(ToneState&, float input,
                    const ParameterFrame&) noexcept;
    float runPreamp(ChannelState&, float input,
                    const ParameterFrame&, float& activity) noexcept;
    float runPowerStage(ChannelState&, float input,
                        const ParameterFrame&, float sagGain) noexcept;
    float renderSpeaker(int model, ChannelState&, float input) noexcept;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    std::vector<ParameterFrame> parameterFrames;
    std::array<ChannelState, 2> channels;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        inputFetMix;
    std::array<juce::SmoothedValue<float,
        juce::ValueSmoothingTypes::Linear>, voiceCount> voiceMix;
    std::array<juce::SmoothedValue<float,
        juce::ValueSmoothingTypes::Linear>, speakerModelCount> speakerMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        driveSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        ratioSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        levelSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        bassSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        midSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        trebleSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        presenceSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        feelSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        masterSmoothed;

    // Fixed coefficients computed in prepare().
    std::array<float, preampStageCount> stageCouplingCoefficients {};
    std::array<float, preampStageCount> stageMillerCoefficients {};
    float biasAttackCoefficient = 0.0f;
    float biasReleaseCoefficient = 0.0f;
    float toneLowCoefficient = 0.0f;
    float toneHighCoefficient = 0.0f;
    float postToneLowCoefficient = 0.0f;
    float postToneHighCoefficient = 0.0f;
    float presenceFeedbackCoefficient = 0.0f;
    float inputBrightCoefficient = 0.0f;
    float sagAttackCoefficient = 0.0f;
    float oversampledDcCoefficient = 0.0f;
    float baseDcCoefficient = 0.0f;

    std::array<float, speakerModelCount> speakerHighPassCoefficients {};
    std::array<float, speakerModelCount> speakerLowPassCoefficients {};
    std::array<float, speakerModelCount> speakerSparkleCoefficients {};
    std::array<float, speakerModelCount> speakerResonanceAG {};
    std::array<float, speakerModelCount> speakerResonanceBG {};

    // Stereo-linked supply state.
    float supplyEnvelope = 0.0f;

    double sampleRate = 44100.0;
    double oversampledRate = 176400.0;
    int processingLatency = 0;
    bool initialized = false;

    ContinuousTelemetrySnapshot telemetryState;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
};
} // namespace megadsp
