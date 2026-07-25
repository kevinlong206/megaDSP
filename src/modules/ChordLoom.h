#pragma once

#include "DspModule.h"

namespace megadsp
{
class ChordLoomModule final
    : public DspModule,
      public ContinuousTelemetryCapability
{
public:
    static constexpr int layerCount = 3;
    static constexpr int grainsPerLayer = 6;
    static constexpr float maximumCaptureSeconds = 2.0f;

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    double tailSeconds(const ControlValues&) const override;

    bool readContinuousTelemetry(
        ContinuousTelemetrySnapshot&) const noexcept override;
    ContinuousTelemetryCapability*
        continuousTelemetryCapability() noexcept override { return this; }
    const ContinuousTelemetryCapability*
        continuousTelemetryCapability() const noexcept override { return this; }

    enum TelemetryValue
    {
        inputEnvelope,
        inputThreshold,
        onsetStrength,
        onsetThreshold,
        captureTriggered,
        captureProgress,
        activeLayer,
        layerGain0,
        layerGain1,
        layerGain2,
        wetEnvelope,
        telemetryValueCount
    };

private:
    struct Grain
    {
        float position = 0.0f;
        float octavePosition = 0.0f;
        float increment = 1.0f;
        float pan = 0.0f;
        int age = 0;
        int duration = 1;
        bool active = false;
    };

    struct Layer
    {
        std::array<std::vector<float>, 2> audio;
        std::array<Grain, grainsPerLayer> grains;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gain;
        std::uint32_t randomState = 0x9e3779b9u;
        int capturedSamples = 0;
        int captureLimit = 0;
        bool capturing = false;
        bool ready = false;
    };

    static float envelopeCoefficient(
        double sampleRate, float milliseconds) noexcept;
    static std::uint32_t nextRandom(Layer&) noexcept;
    static float randomUnit(Layer&) noexcept;
    static float readLinear(
        const Layer&, int channel, float position) noexcept;
    void beginCapture(int layer, int captureSamples);
    void startGrain(
        Layer&, Grain&, int voice, float texture, float motion);
    std::array<float, 2> renderLayer(
        Layer&, int channels, float texture, float motion,
        float bloom, float spread);

    std::array<Layer, layerCount> layers;
    std::array<float, 2> lowCutState {};
    std::array<float, 2> highCutState {};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        textureSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        motionSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        bloomSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        spreadSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        lowCutSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        highCutSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> drySmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        sustainSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
    double sampleRate = 44100.0;
    float fastEnvelope = 0.0f;
    float slowEnvelope = 0.0f;
    float fastAttackCoefficient = 0.0f;
    float fastReleaseCoefficient = 0.0f;
    float slowCoefficient = 0.0f;
    int samplesSinceCapture = 0;
    int nextCaptureLayer = 0;
    bool onsetArmed = true;
    bool initialized = false;
    std::uint64_t telemetrySequence = 0;
    FixedAtomicSnapshot<ContinuousTelemetrySnapshot> telemetry;
};
} // namespace megadsp
