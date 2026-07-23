#pragma once

#include "DspModule.h"

namespace megadsp
{
class LoudnessRiderModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    enum ControlIndex
    {
        targetControl,
        rangeControl,
        windowControl,
        reactionControl,
        lookaheadControl,
        transientHoldControl,
        crestPreserveControl,
        gateControl,
        outputControl
    };

    enum TelemetryValue
    {
        momentaryLoudness,
        targetLoudness,
        gateLoudness,
        rideGainDecibels,
        requestedGainDecibels,
        crestDecibels,
        gatedState,
        activeLookaheadMilliseconds,
        telemetryValueCount
    };

    enum TelemetryHistory
    {
        loudnessHistory,
        targetHistory,
        gateHistory,
        rideHistory,
        telemetryHistoryValueCount
    };

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    int latencySamples() const override { return fixedLatencySamples; }
    float meterValue() const override { return rideMeterDb.load(); }
    float detectorValue() const override { return loudnessMeter.load(); }
    bool readContinuousTelemetry(
        ContinuousTelemetrySnapshot&) const noexcept override;
    ContinuousTelemetryCapability*
        continuousTelemetryCapability() noexcept override { return this; }
    const ContinuousTelemetryCapability*
        continuousTelemetryCapability() const noexcept override { return this; }

private:
    std::array<std::vector<float>, 2> audioDelay;
    std::vector<float> gainDelay;
    std::array<float, 2> highPassState {};
    std::array<float, 2> previousInput {};
    std::array<float, 2> shelfState {};
    double sampleRate = 44100.0;
    double loudnessPower = 0.0;
    float fastEnvelope = 0.0f;
    float rideGainDb = 0.0f;
    float requestedGainDb = 0.0f;
    float outputGain = 1.0f;
    float currentLoudness = -100.0f;
    float currentCrest = 0.0f;
    int writePosition = 0;
    int fixedLatencySamples = 1;
    int telemetryCountdown = 1;
    int telemetryInterval = 22050;
    bool telemetryPointDue = false;
    std::atomic<float> rideMeterDb { 0.0f };
    std::atomic<float> loudnessMeter { -100.0f };
    ContinuousTelemetrySnapshot telemetryState;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
};
} // namespace megadsp
