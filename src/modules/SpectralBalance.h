#pragma once

#include "DspModule.h"
#include "SpectralFoundation.h"

namespace megadsp
{
class SpectralBalanceModule final
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
        return correctionMeter.load(std::memory_order_relaxed);
    }
    float detectorValue() const override
    {
        return measuredMeter.load(std::memory_order_relaxed);
    }
    bool readContinuousTelemetry(
        ContinuousTelemetrySnapshot&) const noexcept override;
    ContinuousTelemetryCapability*
        continuousTelemetryCapability() noexcept override { return this; }
    const ContinuousTelemetryCapability*
        continuousTelemetryCapability() const noexcept override { return this; }

    enum TelemetryValue
    {
        measuredLevelDb,
        targetMeanDb,
        maximumCorrectionDb,
        adaptationSeconds,
        lowTargetDb,
        presenceTargetDb,
        airTargetDb,
        transientActivity,
        telemetryValueCount
    };

private:
    static void processFrameCallback(
        void*, FixedLatencyStft::Complex* const*, int, int) noexcept;
    static float outputCallback(
        void*, int, float, float) noexcept;
    void processFrame(
        FixedLatencyStft::Complex* const*, int, int) noexcept;
    float targetAt(float frequency) const noexcept;
    float outputSample(float delayedDry, float wet) noexcept;
    void publishTelemetry(bool capture) noexcept;

    FixedLatencyStft stft;
    PerBinSmoother correctionSmoother;
    std::vector<float> currentMagnitude;
    std::vector<float> previousMagnitude;
    std::vector<float> trackedPower;
    std::vector<float> trackedDb;
    std::vector<float> broadMeasuredDb;
    std::vector<float> targetDb;
    std::vector<float> rawCorrectionDb;
    std::vector<float> appliedCorrectionDb;
    std::vector<double> powerPrefixSum;
    std::vector<std::uint8_t> trackingInitialised;
    std::array<std::array<float, continuousTelemetryHistoryCapacity>, 4>
        telemetrySpectrum {};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
    double sampleRate = 44100.0;
    float currentAmount = 0.0f;
    float currentLowWeightDb = 0.0f;
    float currentPresenceDb = 0.0f;
    float currentAirDb = 0.0f;
    float currentAdaptationSeconds = 5.0f;
    float currentDetail = 0.5f;
    float currentTransientPreserve = 0.5f;
    float transientState = 0.0f;
    float meanMeasuredDb = -100.0f;
    float meanTargetDb = 0.0f;
    float maximumCorrection = 0.0f;
    float currentOutputGain = 1.0f;
    int contour = 0;
    bool parametersInitialised = false;
    std::uint64_t telemetrySequence = 0;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
    std::atomic<float> correctionMeter { 0.0f };
    std::atomic<float> measuredMeter { -100.0f };
};
} // namespace megadsp
