#pragma once

#include "DspModule.h"

namespace megadsp
{
// Emulates a classic passive program equalizer: a shared-frequency low
// boost/attenuation network whose boost (shelf) and attenuation (bell) use
// different curves so engaging both together creates a resonant interaction
// rather than cancelling, a separately addressed broad high boost with
// proportional-Q bandwidth, a high-shelf attenuation, and a bounded
// oversampled makeup amplifier/transformer coloration stage that recovers
// the network's insertion loss.
class ProgramEqualizerModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    static constexpr int lowFrequencyCount = 4;
    static constexpr int highBoostFrequencyCount = 7;
    static constexpr int highAttenuationFrequencyCount = 3;

    enum Control
    {
        lowFrequencyControl,
        lowBoostControl,
        lowAttenuationControl,
        highBoostFrequencyControl,
        highBoostControl,
        bandwidthControl,
        highAttenuationFrequencyControl,
        highAttenuationControl,
        driveControl,
        outputControl
    };

    enum TelemetryValue
    {
        driveActivity,
        lowInteractionActivity,
        highInteractionActivity,
        makeupGainDb,
        telemetryValueCount
    };

    enum TelemetryHistory
    {
        driveHistory,
        lowInteractionHistory,
        highInteractionHistory,
        telemetryHistoryCount
    };

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    int latencySamples() const override { return processingLatency; }
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
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;
        float process(float input) noexcept;
        void setLowShelf(
            double sampleRate, float frequency, float q, float gainDb);
        void setPeak(
            double sampleRate, float frequency, float q, float gainDb);
        void setHighShelf(
            double sampleRate, float frequency, float q, float gainDb);
        void reset() noexcept;
    };

    template <int Count>
    using FilterBank = std::array<std::array<Biquad, Count>, 2>;
    template <int Count>
    using CrossfadeWeights = std::array<
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>, Count>;

    // Shared low-frequency selector: a broad shelf boost network and a
    // narrower bell attenuation network kept structurally distinct so
    // simultaneous boost and cut interact rather than cancel.
    FilterBank<lowFrequencyCount> lowBoostFilters;
    FilterBank<lowFrequencyCount> lowAttenuationFilters;
    // Independently stepped high boost (bell) and high attenuation (shelf).
    FilterBank<highBoostFrequencyCount> highBoostFilters;
    FilterBank<highAttenuationFrequencyCount> highAttenuationFilters;

    CrossfadeWeights<lowFrequencyCount> lowFrequencyMix;
    CrossfadeWeights<highBoostFrequencyCount> highBoostFrequencyMix;
    CrossfadeWeights<highAttenuationFrequencyCount> highAttenuationFrequencyMix;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        lowBoostSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        lowAttenuationSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        highBoostSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        bandwidthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        highAttenuationSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        driveSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    std::array<float, 2> transformerDcState {};
    std::array<float, 2> transformerDcInput {};

    ContinuousTelemetrySnapshot telemetryState;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;

    double sampleRate = 44100.0;
    int processingLatency = 0;
    bool initialized = false;
    bool crossfadeInitialized = false;
};
} // namespace megadsp
