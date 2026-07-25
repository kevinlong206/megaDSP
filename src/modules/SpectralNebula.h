#pragma once

#include "DspModule.h"
#include "SpectralFoundation.h"

namespace megadsp
{
class SpectralNebulaModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    enum ControlIndex
    {
        smearControl,
        decayControl,
        driftControl,
        shimmerControl,
        focusControl,
        freezeControl,
        widthControl,
        lowCutControl,
        highCutControl,
        dryControl,
        cloudControl,
        outputControl
    };

    enum TelemetryValue
    {
        inputEnergy,
        cloudEnergy,
        lowEnergy,
        midEnergy,
        highEnergy,
        actualDriftCents,
        frozen,
        focusAmount,
        telemetryValueCount
    };

    enum TelemetryHistory
    {
        lowHistory,
        midHistory,
        highHistory,
        cloudHistory,
        telemetryHistoryValueCount
    };

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    int latencySamples() const override
    {
        return FixedLatencyStft::reportedLatencySamples;
    }
    DryPathTiming dryPathTiming() const override
    {
        return DryPathTiming::immediate;
    }
    double tailSeconds(const ControlValues&) const override;
    float meterValue() const override
    {
        return wetMeter.load(std::memory_order_relaxed);
    }

    bool readContinuousTelemetry(
        ContinuousTelemetrySnapshot&) const noexcept override;
    ContinuousTelemetryCapability*
        continuousTelemetryCapability() noexcept override { return this; }
    const ContinuousTelemetryCapability*
        continuousTelemetryCapability() const noexcept override { return this; }

private:
    using Complex = FixedLatencyStft::Complex;

    static void processFrameCallback(
        void*, Complex* const*, int, int) noexcept;
    static float outputCallback(
        void*, int, float, float, float) noexcept;
    void processFrame(Complex* const*, int, int) noexcept;
    void publishTelemetry(bool capture) noexcept;
    static Complex safeComplex(Complex) noexcept;
    static Complex polar(float magnitude, float phase) noexcept;

    FixedLatencyStft stft;
    std::array<std::vector<float>, 2> inputMagnitude;
    std::array<std::vector<float>, 2> cloudMagnitude;
    std::array<std::vector<float>, 2> frameMagnitude;
    std::array<std::vector<float>, 2> cloudTonality;
    std::array<std::vector<float>, 2> outputPhase;
    std::array<std::vector<float>, 2> cloudRelativePhase;
    std::array<std::vector<float>, 2> previousInputPhase;
    std::array<std::vector<float>, 2> cloudPhaseAdvance;
    std::array<std::vector<float>, 2> inputPhaseConfidence;
    std::array<std::vector<int>, 2> cloudPeakOwner;
    std::array<std::vector<Complex>, 2> inputDomain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> drySmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> cloudSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
    double sampleRate = 44100.0;
    double driftPhase = 0.0;
    float smear = 0.72f;
    float decaySeconds = 6.0f;
    float driftDepthCents = 8.0f;
    float shimmer = 0.18f;
    float focus = 0.0f;
    float width = 0.65f;
    float lowCutHz = 80.0f;
    float highCutHz = 12000.0f;
    float smoothedWidth = 0.65f;
    float smoothedLowCutHz = 80.0f;
    float smoothedHighCutHz = 12000.0f;
    float currentDry = 1.0f;
    float currentCloud = 0.55f;
    float currentOutput = 1.0f;
    float measuredInputEnergy = 0.0f;
    float measuredCloudEnergy = 0.0f;
    float currentDriftCents = 0.0f;
    std::array<float, 3> bandEnergy {};
    bool freeze = false;
    bool parametersInitialised = false;
    std::uint64_t telemetrySequence = 0;
    ContinuousTelemetrySnapshot telemetryState;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
    std::atomic<float> wetMeter { 0.0f };
};
} // namespace megadsp
