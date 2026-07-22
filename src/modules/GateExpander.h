#pragma once

#include "DspModule.h"

namespace megadsp
{
class GateExpanderModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    enum TelemetryValue : size_t
    {
        detectorDb,
        gainEnvelopeDb,
        attenuationDb,
        openFraction,
        telemetryValueCount
    };
    enum TelemetryHistory : size_t
    {
        detectorHistory,
        attenuationHistory,
        openHistory,
        envelopeHistory,
        telemetryHistoryCount
    };

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    double tailSeconds(const ControlValues&) const override;
    float meterValue() const override { return gainReductionDb.load(); }
    float detectorValue() const override { return detectorLevelDb.load(); }
    bool readContinuousTelemetry(
        ContinuousTelemetrySnapshot&) const noexcept override;
    ContinuousTelemetryCapability*
        continuousTelemetryCapability() noexcept override { return this; }
    const ContinuousTelemetryCapability*
        continuousTelemetryCapability() const noexcept override { return this; }

private:
    struct DetectorFilter
    {
        float process(float input, float lowCut, float highCut,
                      double sampleRate);
        void reset();

        float highPassState = 0.0f;
        float lowPassState = 0.0f;
    };

    std::array<DetectorFilter, 2> detectorFilters;
    std::array<float, 2> rmsPower {};
    std::array<float, 2> gainDbState {};
    std::array<int, 2> holdSamplesRemaining {};
    std::array<bool, 2> open {};
    std::array<juce::SmoothedValue<float,
                                  juce::ValueSmoothingTypes::Linear>, 11>
        smoothed;
    double sampleRate = 44100.0;
    bool initialized = false;
    std::atomic<float> gainReductionDb { 0.0f };
    std::atomic<float> detectorLevelDb { -100.0f };
    ContinuousTelemetrySnapshot telemetryState;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
};
} // namespace megadsp
