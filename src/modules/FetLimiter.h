#pragma once

#include "DspModule.h"

namespace megadsp
{
// Classic FET-style feedback limiter. The gain-reduction detector reads the
// already gain-reduced (fed back) signal rather than the raw input, which is
// what gives feedback-topology limiters their self-stabilizing, program
// dependent character. A bounded, fixed-latency 4x oversampled stage models
// the asymmetric FET/output-transformer coloration driven by the Output
// control, decoupled from the sample-accurate gain-reduction loop.
class FetLimiterModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    static constexpr int ratioCount = 5;
    static constexpr int allButtonsInIndex = ratioCount - 1;
    // Stepped 4:1, 8:1, 12:1, 20:1 ratios plus an aggressive "ALL" mode.
    static constexpr std::array<float, ratioCount> ratioTable {
        4.0f, 8.0f, 12.0f, 20.0f, 60.0f
    };
    static constexpr float softKneeDb = 6.0f;
    // Fixed internal detector reference: real FET limiters have no separate
    // Threshold control, only Input drive against a fixed threshold.
    static constexpr float internalThresholdDb = -10.0f;

    enum Control
    {
        inputControl,
        outputControl,
        attackControl,
        releaseControl,
        ratioControl,
        detectorLowCutControl,
        stereoLinkControl,
        mixControl,
        outputTrimControl
    };

    enum TelemetryValue
    {
        inputLevelDb,
        detectorLevelDbValue,
        gainReductionDbValue,
        outputLevelDb,
        ratioAmount,
        telemetryValueCount
    };

    enum TelemetryHistory
    {
        gainReductionHistory,
        detectorHistory,
        inputHistory,
        outputHistory,
        telemetryHistoryCount
    };

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    int latencySamples() const override { return fixedLatencySamples; }
    float meterValue() const override { return gainReductionMeterDb.load(); }
    float detectorValue() const override { return detectorMeterDb.load(); }
    bool readContinuousTelemetry(
        ContinuousTelemetrySnapshot&) const noexcept override;
    ContinuousTelemetryCapability*
        continuousTelemetryCapability() noexcept override { return this; }
    const ContinuousTelemetryCapability*
        continuousTelemetryCapability() const noexcept override { return this; }

    // Shared by the module view so the displayed transfer curve matches the
    // processed gain reduction exactly.
    static float kneeReductionDb(
        float overDb, float ratio, float kneeDb) noexcept;

private:
    struct DetectorHighPass
    {
        float state = 0.0f;
        float process(float input, float cutoffHz, double sampleRate) noexcept;
        void reset() noexcept { state = 0.0f; }
    };

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::AudioBuffer<float> dryBuffer;
    std::array<std::vector<float>, 2> dryDelay;
    std::array<DetectorHighPass, 2> detectorFilters;
    std::array<float, 2> previousPreTransformer {};
    std::array<float, 2> gainNormal { 1.0f, 1.0f };
    std::array<float, 2> gainFast { 1.0f, 1.0f };
    std::array<float, 2> gainSlow { 1.0f, 1.0f };
    std::array<float, 2> dcInput {};
    std::array<float, 2> dcOutput {};
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>,
               controlsPerSlot> smoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> ratioSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> kneeSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        allAmountSmoothed;
    double sampleRate = 44100.0;
    float dcRetention = 0.0f;
    int dryPosition = 0;
    int fixedLatencySamples = 1;
    bool initialized = false;
    std::atomic<float> gainReductionMeterDb { 0.0f };
    std::atomic<float> detectorMeterDb { -100.0f };
    ContinuousTelemetrySnapshot telemetryState;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
};
} // namespace megadsp
