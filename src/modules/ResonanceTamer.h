#pragma once

#include "DspModule.h"
#include "SpectralFoundation.h"

namespace megadsp
{
class ResonanceTamerModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    int latencySamples() const override
    {
        return FixedLatencyStft::reportedLatencySamples;
    }
    double tailSeconds(const ControlValues&) const override
    {
        return static_cast<double>(FixedLatencyStft::fftSize)
               / sampleRate;
    }
    float meterValue() const override
    {
        return reductionMeter.load(std::memory_order_relaxed);
    }
    float detectorValue() const override
    {
        return detectorMeter.load(std::memory_order_relaxed);
    }
    bool readContinuousTelemetry(
        ContinuousTelemetrySnapshot&) const noexcept override;
    ContinuousTelemetryCapability*
        continuousTelemetryCapability() noexcept override { return this; }
    const ContinuousTelemetryCapability*
        continuousTelemetryCapability() const noexcept override { return this; }

    enum TelemetryValue
    {
        inputLevelDb,
        baselineLevelDb,
        detectedExcessDb,
        actualReductionDb,
        strongestFrequencyHz,
        transientActivity,
        lowLimitHz,
        highLimitHz,
        telemetryValueCount
    };

private:
    static void processFrameCallback(
        void*, FixedLatencyStft::Complex* const*, int, int) noexcept;
    static float outputCallback(
        void*, int, float, float) noexcept;
    void processFrame(
        FixedLatencyStft::Complex* const*, int, int) noexcept;
    float mixOutput(float delayedDry, float wet) noexcept;
    void updateTelemetry(bool capture) noexcept;

    FixedLatencyStft stft;
    PerBinSmoother reductionSmoother;
    std::vector<float> linkedMagnitude;
    std::vector<float> previousMagnitude;
    std::vector<float> levelDb;
    std::vector<float> prefix;
    std::vector<float> baselineDb;
    std::vector<float> desiredReduction;
    std::vector<float> smoothedReduction;
    std::array<std::array<float, continuousTelemetryHistoryCapacity>, 4>
        telemetrySpectrum {};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
    double sampleRate = 44100.0;
    float maximumReductionDb = 0.0f;
    float maximumExcessDb = 0.0f;
    float meanInputDb = -100.0f;
    float meanBaselineDb = -100.0f;
    float strongestHz = 0.0f;
    float transientState = 0.0f;
    float currentReductionDb = 0.0f;
    float currentToneBias = 0.0f;
    float currentLowHz = 20.0f;
    float currentHighHz = 20000.0f;
    float currentTransientPreserve = 0.5f;
    float currentMixGain = 1.0f;
    float currentOutputGain = 1.0f;
    float attackCoefficient = 0.0f;
    float releaseCoefficient = 0.0f;
    int selectivity = 1;
    bool parametersInitialised = false;
    std::uint64_t telemetrySequence = 0;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
    std::atomic<float> reductionMeter { 0.0f };
    std::atomic<float> detectorMeter { -100.0f };
};
} // namespace megadsp
