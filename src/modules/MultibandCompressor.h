#pragma once

#include "DspModule.h"

namespace megadsp
{
class MultibandCompressorModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    enum TelemetryValue : size_t
    {
        lowReductionDb,
        midReductionDb,
        highReductionDb,
        lowActive,
        midActive,
        highActive,
        telemetryValueCount
    };
    enum TelemetryHistory : size_t
    {
        lowReductionHistory,
        midReductionHistory,
        highReductionHistory,
        activeBandHistory,
        telemetryHistoryCount
    };

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    float meterValue() const override { return gainReductionDb.load(); }
    bool readContinuousTelemetry(
        ContinuousTelemetrySnapshot&) const noexcept override;
    ContinuousTelemetryCapability*
        continuousTelemetryCapability() noexcept override { return this; }
    const ContinuousTelemetryCapability*
        continuousTelemetryCapability() const noexcept override { return this; }

private:
    struct Coefficients
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
    };

    struct Biquad
    {
        float process(float input);
        void set(const Coefficients&);
        void reset();

        Coefficients coefficients;
        float z1 = 0.0f, z2 = 0.0f;
    };

    struct ChannelFilters
    {
        std::array<Biquad, 2> lowPass;
        std::array<Biquad, 2> upperHighPass;
        std::array<Biquad, 2> midLowPass;
        std::array<Biquad, 2> highPass;
        std::array<Biquad, 2> lowAlignLowPass;
        std::array<Biquad, 2> lowAlignHighPass;

        void reset();
    };

    static Coefficients lowPassCoefficients(double, float);
    static Coefficients highPassCoefficients(double, float);
    static float processCascade(std::array<Biquad, 2>&, float);

    std::array<ChannelFilters, 2> filters;
    std::array<std::array<float, 3>, 2> envelopes {};
    std::array<std::array<float, 3>, 2> gainDbState {};
    std::array<std::array<float, 3>, 2> averageReductionDb {};
    std::array<std::array<float, 3>, 2> makeupDbState {};
    std::array<juce::SmoothedValue<float,
                                  juce::ValueSmoothingTypes::Linear>, 12>
        smoothed;
    double sampleRate = 44100.0;
    bool initialized = false;
    std::atomic<float> gainReductionDb { 0.0f };
    ContinuousTelemetrySnapshot telemetryState;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
};
} // namespace megadsp
