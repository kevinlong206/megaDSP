#pragma once

#include "DspModule.h"

namespace megadsp
{
class VinylSimulatorModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    enum Control
    {
        conditionControl,
        crackleControl,
        dustControl,
        scratchControl,
        surfaceNoiseControl,
        rumbleControl,
        wowControl,
        flutterControl,
        toneControl,
        stereoControl,
        mixControl,
        outputControl
    };

    enum TelemetryValue
    {
        transportPhase,
        leftCrackleActivity,
        rightCrackleActivity,
        surfaceNoiseActivity,
        rumbleActivity,
        modulationMilliseconds,
        scratchActivity,
        wetEnergy,
        telemetryValueCount
    };

    enum TelemetryHistory
    {
        leftCrackleHistory,
        rightCrackleHistory,
        scratchHistory,
        energyHistory,
        telemetryHistoryCount
    };

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    int latencySamples() const override { return fixedLatencySamples; }
    double tailSeconds(const ControlValues&) const override;
    double snapshotSpillSeconds(const ControlValues&) const override
    {
        return 1.5;
    }
    bool readContinuousTelemetry(
        ContinuousTelemetrySnapshot&) const noexcept override;
    ContinuousTelemetryCapability*
        continuousTelemetryCapability() noexcept override { return this; }
    const ContinuousTelemetryCapability*
        continuousTelemetryCapability() const noexcept override { return this; }

private:
    static constexpr int crackleVoiceCount = 12;

    struct CrackleVoice
    {
        float fast = 0.0f;
        float slow = 0.0f;
        float fastDecay = 0.0f;
        float slowDecay = 0.0f;
        float pan = 0.0f;
        bool active = false;
    };

    std::uint32_t nextRandom() noexcept;
    float randomUnit() noexcept;
    float randomBipolar() noexcept;
    float readTransport(int channel, float delaySamples) const noexcept;
    void startCrackle(float amplitude, float durationMilliseconds) noexcept;

    std::array<std::vector<float>, 2> transportHistory;
    std::array<std::vector<float>, 2> dryHistory;
    std::array<CrackleVoice, crackleVoiceCount> crackleVoices;
    std::array<float, 2> toneState {};
    std::array<float, 2> hissLowpass {};
    std::array<float, 2> rumbleFirst {};
    std::array<float, 2> rumbleSecond {};
    std::array<float, 2> dcState {};
    std::array<float, 2> dcInput {};
    std::array<juce::SmoothedValue<
        float, juce::ValueSmoothingTypes::Linear>, controlsPerSlot> smoothed;
    ContinuousTelemetrySnapshot telemetryState;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;

    std::uint32_t randomState = 0x6d2b79f5u;
    double sampleRate = 44100.0;
    double wowPhase = 0.0;
    double flutterPhase = 0.0;
    double platterPhase = 0.0;
    float scratchEnvelope = 0.0f;
    float scratchPhase = 0.0f;
    float scratchRate = 0.0f;
    int scratchSamplesRemaining = 0;
    int scratchTotalSamples = 0;
    int transportWritePosition = 0;
    int dryWritePosition = 0;
    int fixedLatencySamples = 1;
    bool initialized = false;
};
} // namespace megadsp
