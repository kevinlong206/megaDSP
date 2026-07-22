#pragma once

#include "DspModule.h"

namespace megadsp
{
class TransientDesignerModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    enum TelemetryValue : size_t
    {
        fastEnvelopeDb,
        slowEnvelopeDb,
        attackFeature,
        sustainFeature,
        attackShapingDb,
        sustainShapingDb,
        appliedShapingDb,
        telemetryValueCount
    };
    enum TelemetryHistory : size_t
    {
        fastEnvelopeHistory,
        slowEnvelopeHistory,
        attackShapingHistory,
        sustainShapingHistory,
        telemetryHistoryCount
    };

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    float meterValue() const override { return shapingAmountDb.load(); }
    float detectorValue() const override { return detectorLevelDb.load(); }
    bool readContinuousTelemetry(
        ContinuousTelemetrySnapshot&) const noexcept override;
    ContinuousTelemetryCapability*
        continuousTelemetryCapability() noexcept override { return this; }
    const ContinuousTelemetryCapability*
        continuousTelemetryCapability() const noexcept override { return this; }

private:
    struct BandPass
    {
        float process(float input);
        void set(double sampleRate, float frequency);
        void reset();

        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;
    };

    std::array<BandPass, 2> focusFilters;
    std::array<float, 2> fastEnvelope {};
    std::array<float, 2> slowEnvelope {};
    std::array<float, 2> gainDbState {};
    std::array<juce::SmoothedValue<float,
                                  juce::ValueSmoothingTypes::Linear>, 8>
        smoothed;
    double sampleRate = 44100.0;
    bool initialized = false;
    std::atomic<float> shapingAmountDb { 0.0f };
    std::atomic<float> detectorLevelDb { -100.0f };
    ContinuousTelemetrySnapshot telemetryState;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
};
} // namespace megadsp
