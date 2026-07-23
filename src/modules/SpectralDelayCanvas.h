#pragma once

#include "DspModule.h"
#include "SpectralFoundation.h"

namespace megadsp
{
class SpectralDelayCanvasModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    enum ControlIndex
    {
        syncControl,
        baseTimeControl,
        divisionControl,
        lowDelayControl,
        midDelayControl,
        highDelayControl,
        feedbackControl,
        diffusionControl,
        stereoSpreadControl,
        freezeControl,
        mixControl,
        outputControl
    };

    enum TelemetryValue
    {
        lowEnergy,
        midEnergy,
        highEnergy,
        lowDelaySeconds,
        midDelaySeconds,
        highDelaySeconds,
        actualFeedback,
        frozen,
        telemetryValueCount
    };

    enum TelemetryHistory
    {
        lowEnergyHistory,
        midEnergyHistory,
        highEnergyHistory,
        delayHistory,
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
    double tailSeconds(const ControlValues&) const override;
    float meterValue() const override
    {
        return wetMeter.load(std::memory_order_relaxed);
    }
    float detectorValue() const override
    {
        return historyMeter.load(std::memory_order_relaxed);
    }

    bool readContinuousTelemetry(
        ContinuousTelemetrySnapshot&) const noexcept override;
    ContinuousTelemetryCapability*
        continuousTelemetryCapability() noexcept override { return this; }
    const ContinuousTelemetryCapability*
        continuousTelemetryCapability() const noexcept override { return this; }
private:
    static constexpr float maximumDelaySeconds = 8.0f;

    static void processFrameCallback(
        void*, FixedLatencyStft::Complex* const*, int, int) noexcept;
    static float outputCallback(
        void*, int, float, float) noexcept;
    void processFrame(
        FixedLatencyStft::Complex* const*, int, int) noexcept;
    FixedLatencyStft::Complex readHistory(
        int channel, int bin, float framesAgo) const noexcept;
    float delayAt(float frequency) const noexcept;
    void publishTelemetry(bool capture) noexcept;

    FixedLatencyStft stft;
    std::array<FixedSpectralHistory, FixedLatencyStft::maxChannels> history;
    PerBinSmoother delaySmoother;
    std::array<std::vector<float>, FixedLatencyStft::maxChannels> writeScratch;
    std::array<std::vector<FixedLatencyStft::Complex>,
               FixedLatencyStft::maxChannels> rendered;
    std::array<std::vector<FixedLatencyStft::Complex>,
               FixedLatencyStft::maxChannels> previousWet;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
    double sampleRate = 44100.0;
    std::array<float, 3> anchorDelaySeconds {};
    std::array<float, 3> bandEnergy {};
    float feedback = 0.0f;
    float diffusion = 0.0f;
    float stereoSpread = 0.0f;
    float currentMix = 1.0f;
    float currentOutput = 1.0f;
    bool freeze = false;
    bool previousFreeze = false;
    bool delayStateInitialised = false;
    bool parametersInitialised = false;
    std::uint64_t continuousSequence = 0;
    ContinuousTelemetrySnapshot continuousState;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> continuousTelemetry;
    std::atomic<float> wetMeter { 0.0f };
    std::atomic<float> historyMeter { -100.0f };
};
} // namespace megadsp
