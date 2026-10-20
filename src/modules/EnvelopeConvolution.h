#pragma once

#include "ConvolutionReverb.h"

namespace megadsp
{
class EnvelopeConvolutionModule final : public ConvolutionReverbModule
{
public:
    enum Control
    {
        quietBlendControl,
        loudBlendControl,
        quietLevelControl,
        loudLevelControl,
        attackControl,
        releaseControl,
        lowCutControl,
        highCutControl,
        outputControl,
        activeControlCount
    };

    enum TelemetryValue
    {
        inputLevelDb,
        quietThresholdDb,
        loudThresholdDb,
        currentBlend,
        envelopePosition,
        wetLevelDb,
        impulseReady,
        telemetryValueCount
    };
    enum HistoryValue { levelHistory, blendHistory, wetHistory };

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    DryPathTiming dryPathTiming() const override
    {
        return DryPathTiming::immediate;
    }
    bool readContinuousTelemetry(
        ContinuousTelemetrySnapshot&) const noexcept override;

private:
    juce::dsp::StateVariableTPTFilter<float> envelopeLowCut;
    juce::dsp::StateVariableTPTFilter<float> envelopeHighCut;
    std::array<juce::SmoothedValue<float>, activeControlCount> smoothed;
    juce::SmoothedValue<float> availability;
    double sampleRate = 44100.0;
    float envelope = 0.0f;
    float previousLowCut = -1.0f;
    float previousHighCut = -1.0f;
    int maximumBlockSize = 512;
    int preparedChannels = 2;
    int historySamples = 0;
    int historyInterval = 1;
    bool initialized = false;
    bool wasCapturing = false;
    std::uint64_t telemetrySequence = 0;
    double wetEnergy = 0.0;
    ContinuousTelemetrySnapshot liveTelemetry;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
};
} // namespace megadsp
