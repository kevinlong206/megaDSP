#pragma once

#include "DspModule.h"

namespace megadsp
{
// Opto Leveler models a feedback-topology optical leveling amplifier: the
// electro-luminescent "cell" is driven by the already gain-reduced output
// (not the raw input), giving the smoothed, program-dependent behavior of a
// photoresistive compressor rather than a straightforward feedforward VCA.
class OptoLevelerModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    enum ControlIndex
    {
        peakReductionControl,
        gainControl,
        compressLimitControl,
        hfEmphasisControl,
        sidechainLowCutControl,
        cellAgeControl,
        stereoLinkControl,
        mixControl,
        outputControl,
        activeControlCount
    };

    enum TelemetryValue : size_t
    {
        inputLevelDb,
        outputLevelDb,
        gainReductionDb,
        cellMemory,
        detectorLevelDb,
        telemetryValueCount
    };
    enum TelemetryHistory : size_t
    {
        detectorHistory,
        reductionHistory,
        memoryHistory,
        outputHistory,
        telemetryHistoryCount
    };
    static_assert(
        telemetryHistoryCount <= continuousTelemetryHistoryValueCapacity);

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    int latencySamples() const override { return oversamplingLatency; }

    float meterValue() const override
    {
        return meterReductionDb.load(std::memory_order_relaxed);
    }
    float detectorValue() const override
    {
        return meterDetectorDb.load(std::memory_order_relaxed);
    }
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
    struct CellState
    {
        float hfEmphasisLow = 0.0f;
        float lowCutFirst = 0.0f;
        float lowCutSecond = 0.0f;
        float gainDb = 0.0f;
        float memory = 0.0f;
        float previousCellOutput = 0.0f;
    };

    std::array<CellState, 2> cells;
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>,
               activeControlCount>
        smoothed;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::AudioBuffer<float> dryBuffer;
    std::array<std::vector<float>, 2> dryLatencyBuffer;
    int dryLatencyPosition = 0;
    int oversamplingLatency = 0;
    double sampleRate = 44100.0;
    bool initialized = false;
    std::uint64_t telemetrySequence = 0;
    std::atomic<float> meterReductionDb { 0.0f };
    std::atomic<float> meterDetectorDb { -100.0f };
    ContinuousTelemetrySnapshot telemetryState;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
};
} // namespace megadsp
