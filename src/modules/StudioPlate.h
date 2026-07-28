#pragma once

#include "DspModule.h"

namespace megadsp
{
// Studio Plate hybrid: a small bank of physically informed modal
// resonators forms the metallic onset character, feeding a dense
// orthogonal (Hadamard) feedback delay network that supplies the
// diffuse late field. A single mono excitation drives both stages;
// stereo emerges only from two fixed-orthogonal pickup projections of
// the shared FDN state, so the wet field is always exactly mono-safe
// on fold-down regardless of Pickup Spread or Width.
class StudioPlateModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    static constexpr int modeCount = 8;
    static constexpr int lineCount = 8;
    static constexpr int plateSizeCount = 3;

    enum Control
    {
        plateSizeControl,
        decayControl,
        dampingControl,
        preDelayControl,
        lowCutControl,
        highCutControl,
        pickupSpreadControl,
        widthControl,
        duckingControl,
        dryControl,
        wetControl,
        outputControl
    };

    enum TelemetryValue
    {
        excitationLevel,
        leftPickupLevel,
        rightPickupLevel,
        lowBandLevel,
        midBandLevel,
        highBandLevel,
        modalActivityLevel,
        telemetryValueCount
    };

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    double tailSeconds(const ControlValues&) const override;
    float meterValue() const override
    {
        return meter.load(std::memory_order_relaxed);
    }
    bool readContinuousTelemetry(
        ContinuousTelemetrySnapshot&) const noexcept override;
    ContinuousTelemetryCapability*
        continuousTelemetryCapability() noexcept override { return this; }
    const ContinuousTelemetryCapability*
        continuousTelemetryCapability() const noexcept override
    {
        return this;
    }

    // Self-contained defaults: the shared module registry entry for
    // Studio Plate is not yet wired, so this keeps the module and its
    // tests/view usable without depending on that shared wiring.
    static ControlValues defaultControlValues();

private:
    class FractionalDelay
    {
    public:
        void prepare(int capacity);
        void reset();
        float read(float delaySamples) const;
        void write(float value);

    private:
        std::vector<float> buffer;
        int writePosition = 0;
    };

    struct DecayFilter
    {
        void reset();
        float process(float input, float lowCoefficient, float highCoefficient,
                      float lowGain, float midGain, float highGain);

        float lowState = 0.0f;
        float highState = 0.0f;
    };

    struct ModalMode
    {
        float y1 = 0.0f;
        float y2 = 0.0f;
    };

    float lineDelaySamples(int line, float sizeScale) const;
    void updateDecayTargets(
        float decaySeconds, float damping, float sizeScale);
    void updateModalTargets(
        float decaySeconds, float damping, int plateSizeIndex);

    std::array<FractionalDelay, lineCount> delays;
    FractionalDelay preDelayLine;
    std::array<DecayFilter, lineCount> decayFilters;
    std::array<ModalMode, modeCount> modalModes;
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>,
               modeCount> modeFrequencySmoothed;
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>,
               modeCount> modeT60Smoothed;
    std::array<float, lineCount> lowGains {};
    std::array<float, lineCount> midGains {};
    std::array<float, lineCount> highGains {};
    std::array<float, lineCount> targetLowGains {};
    std::array<float, lineCount> targetMidGains {};
    std::array<float, lineCount> targetHighGains {};
    std::array<float, lineCount> baseDelaySamples {};

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        preDelaySmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        excitationLowCoefficientSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        excitationHighCoefficientSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        pickupSpreadSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        widthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        duckAmountSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> drySmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;

    float excitationLowState = 0.0f;
    float excitationHighState = 0.0f;
    float duckEnvelope = 0.0f;
    float duckGainDb = 0.0f;

    float currentSizeScale = 1.0f;
    float oldSizeScale = 1.0f;
    float targetSizeScale = 1.0f;
    float sizeFade = 1.0f;
    int activePlateSizeIndex = 1;

    float decayCoefficientSmoothing = 0.001f;
    float decayLowCoefficient = 0.0f;
    float decayHighCoefficient = 0.0f;

    double sampleRate = 44100.0;
    bool initialized = false;

    std::atomic<float> meter { 0.0f };
    std::uint64_t telemetrySequence = 0;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
};
} // namespace megadsp
