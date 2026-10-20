#pragma once

#include "DspModule.h"

namespace megadsp
{
class EnvelopeModulatorModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    enum Control
    {
        modeControl,
        quietSpeedControl,
        loudSpeedControl,
        quietLevelControl,
        loudLevelControl,
        attackControl,
        releaseControl,
        tremoloDepthControl,
        pitchDepthControl,
        waveformControl,
        mixControl,
        outputControl
    };

    enum TelemetryValue
    {
        inputLevelDb,
        quietThresholdDb,
        loudThresholdDb,
        currentRateHz,
        currentLfo,
        oscillatorPhase,
        amplitudeGain,
        pitchShiftCents,
        ratePosition,
        wetEnergy,
        telemetryValueCount
    };

    enum HistoryValue { levelHistory, rateHistory, lfoHistory };

    static constexpr float minimumRateHz = 0.1f;
    static constexpr float maximumRateHz = 20.0f;
    static constexpr float minimumLevelDb = -72.0f;
    static constexpr float maximumLevelDb = 0.0f;
    static constexpr float minimumLevelSpanDb = 1.0f;

    static std::array<float, 2> orderedThresholds(
        float quietDb, float loudDb) noexcept;

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    DryPathTiming dryPathTiming() const override
    {
        return DryPathTiming::immediate;
    }
    double tailSeconds(const ControlValues& controls) const override
    {
        return discreteIndex(controls[modeControl], 3) == 2 ? 0.25 : 0.0;
    }
    float meterValue() const override
    {
        return meter.load(std::memory_order_relaxed);
    }
    ContinuousTelemetryCapability*
        continuousTelemetryCapability() noexcept override { return this; }
    const ContinuousTelemetryCapability*
        continuousTelemetryCapability() const noexcept override { return this; }
    bool readContinuousTelemetry(
        ContinuousTelemetrySnapshot&) const noexcept override;

private:
    float readDelay(int channel, float delaySamples) const noexcept;

    std::array<std::vector<float>, 2> delayBuffer;
    std::array<float, 2> lowState {};
    std::array<juce::SmoothedValue<float>, controlsPerSlot> smoothed;
    std::array<juce::SmoothedValue<float>, 3> modeWeights;
    std::array<juce::SmoothedValue<float>, 3> waveformWeights;
    juce::SmoothedValue<float> vibratoBlend;
    double sampleRate = 44100.0;
    float crossoverCoefficient = 0.0f;
    float envelope = 0.0f;
    double phase = 0.0;
    float vibratoDelay = 3.0f;
    int writePosition = 0;
    int historySamples = 0;
    int historyInterval = 1;
    bool initialized = false;
    bool wasCapturing = false;
    std::uint64_t telemetrySequence = 0;
    ContinuousTelemetrySnapshot liveTelemetry;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
    std::atomic<float> meter { 0.0f };
};
} // namespace megadsp
