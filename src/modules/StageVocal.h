#pragma once

#include "DspModule.h"

namespace megadsp
{
class StageVocalModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    enum ControlIndex
    {
        lowCutControl,
        fastAmountControl,
        fastAttackControl,
        fastReleaseControl,
        slowAmountControl,
        slowAttackControl,
        slowReleaseControl,
        deEssFrequencyControl,
        deEssAmountControl,
        presenceControl,
        airControl,
        outputControl
    };

    enum TelemetryValue
    {
        inputLevelDb,
        fastReductionDb,
        deEssReductionDb,
        slowReductionDb,
        outputLevelDb,
        telemetryValueCount
    };

    enum TelemetryHistory
    {
        inputHistory,
        fastHistory,
        deEssHistory,
        slowHistory,
        telemetryHistoryCount
    };

    static_assert(
        telemetryHistoryCount
        <= continuousTelemetryHistoryValueCapacity);

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;

    float meterValue() const override
    {
        return totalReductionDb.load(std::memory_order_relaxed);
    }
    float detectorValue() const override
    {
        return detectorDb.load(std::memory_order_relaxed);
    }
    bool readContinuousTelemetry(
        ContinuousTelemetrySnapshot&) const noexcept override;
    ContinuousTelemetryCapability*
        continuousTelemetryCapability() noexcept override { return this; }
    const ContinuousTelemetryCapability*
        continuousTelemetryCapability() const noexcept override { return this; }

private:
    struct ChannelState
    {
        float lowCutFirst = 0.0f;
        float lowCutSecond = 0.0f;
        float deEssLow = 0.0f;
        float previousFastOutput = 0.0f;
        float previousSlowOutput = 0.0f;
        float presenceLow = 0.0f;
        float presenceHigh = 0.0f;
        float airLow = 0.0f;
    };

    static float onePoleCoefficient(
        double sampleRate, float frequency) noexcept;
    static float smoothStep(float value) noexcept;
    static float compressionReduction(
        float levelDb, float thresholdDb, float ratio,
        float kneeDb) noexcept;

    std::array<ChannelState, 2> channelStates;
    std::array<juce::SmoothedValue<float,
                                  juce::ValueSmoothingTypes::Linear>,
               controlsPerSlot>
        smoothed;
    double sampleRate = 44100.0;
    float fullEnvelope = 0.0f;
    float deEssEnvelope = 0.0f;
    float deEssGainDb = 0.0f;
    float fastGainDb = 0.0f;
    float slowGainDb = 0.0f;
    float opticalMemory = 0.0f;
    bool initialized = false;
    std::uint64_t telemetrySequence = 0;
    std::atomic<float> totalReductionDb { 0.0f };
    std::atomic<float> detectorDb { -100.0f };
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
};
} // namespace megadsp
