#pragma once

#include "DspModule.h"

namespace megadsp
{
class AdaptiveClipperModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    static constexpr int oversamplingPathCount = 3;

    enum ControlIndex
    {
        driveControl,
        ceilingControl,
        styleControl,
        shapeControl,
        transientBiasControl,
        releaseControl,
        stereoLinkControl,
        oversamplingControl,
        autoTrimControl,
        mixControl,
        outputControl
    };

    enum TelemetryValue
    {
        inputPeak,
        measuredTruePeak,
        clippingDecibels,
        transientClassification,
        ceilingMarginDecibels,
        clippedEnergy,
        activeOversampling,
        adaptiveKnee,
        telemetryValueCount
    };

    enum TelemetryHistory
    {
        inputWaveformHistory,
        outputWaveformHistory,
        clippingHistory,
        classificationHistory,
        telemetryHistoryValueCount
    };

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    int latencySamples() const override { return fixedLatencySamples; }
    float meterValue() const override { return clippingMeterDb.load(); }
    bool readContinuousTelemetry(
        ContinuousTelemetrySnapshot&) const noexcept override;
    ContinuousTelemetryCapability*
        continuousTelemetryCapability() noexcept override { return this; }
    const ContinuousTelemetryCapability*
        continuousTelemetryCapability() const noexcept override { return this; }

private:
    struct PathState
    {
        std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
        juce::AudioBuffer<float> buffer;
        std::array<std::vector<float>, 2> alignmentDelay;
        std::array<float, 2> fastEnvelope {};
        std::array<float, 2> bodyEnvelope {};
        float driveGain = 1.0f;
        float ceilingGain = 1.0f;
        float shape = 0.5f;
        float transientBias = 0.5f;
        float measuredPeak = 0.0f;
        float measuredClippedPeak = 0.0f;
        float clippedEnergy = 0.0f;
        float classification = 0.0f;
        float knee = 0.0f;
        int alignmentPosition = 0;
        int latency = 0;
    };

    void processPath(PathState&, int pathIndex, int channels, int numSamples,
                     float driveTarget, float ceilingTarget, int style,
                     float shapeTarget, float transientBiasTarget,
                     float releaseMilliseconds, float stereoLink,
                     bool autoTrim);
    static float shapeSample(float input, float ceiling, float knee) noexcept;

    std::array<PathState, oversamplingPathCount> paths;
    juce::AudioBuffer<float> dryBuffer;
    std::array<std::vector<float>, 2> dryDelay;
    std::array<float, oversamplingPathCount> pathWeights { 1.0f, 0.0f, 0.0f };
    std::array<float, 2> previousOutput {};
    double sampleRate = 44100.0;
    float mixValue = 1.0f;
    float outputGain = 1.0f;
    int dryPosition = 0;
    int fixedLatencySamples = 1;
    int selectedPath = 0;
    bool initialized = false;
    std::atomic<float> clippingMeterDb { 0.0f };
    ContinuousTelemetrySnapshot telemetryState;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
};
} // namespace megadsp
