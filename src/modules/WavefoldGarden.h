#pragma once

#include "DspModule.h"

namespace megadsp
{
class WavefoldGardenModule final : public DspModule
{
public:
    static constexpr int characterCount = 4;
    static constexpr int foldCount = 8;
    static constexpr int oversamplingFactor = 4;

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    int latencySamples() const override { return oversamplingLatency; }
    float meterValue() const override
    {
        return activityMeter.load(std::memory_order_relaxed);
    }

private:
    struct ParameterFrame
    {
        std::array<float, characterCount> characterWeights {};
        std::array<float, foldCount> foldWeights {};
        float driveGain = 1.0f;
        float symmetry = 0.0f;
        float shape = 0.0f;
        float dynamics = 0.0f;
        float dynamicThreshold = 0.2f;
        float attackCoefficient = 0.0f;
        float releaseCoefficient = 0.0f;
        float toneCoefficient = 0.0f;
        float stereoBloom = 0.0f;
        float mix = 0.0f;
        float output = 1.0f;
    };

    float processCharacter(int character, float input, int folds, float symmetry,
                           float shape, float opening) const;
    static float petalFold(float input, int folds, float symmetry, float shape);
    static float prismFold(float input, int folds, float symmetry, float shape);
    static float chebyshevFold(float input, int folds, float symmetry,
                               float shape);
    static float bloomFold(float input, int folds, float symmetry, float shape,
                           float opening);
    static float chebyshevPolynomial(int order, float x);

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::AudioBuffer<float> dryBuffer;
    std::vector<ParameterFrame> parameterFrames;
    std::array<std::vector<float>, 2> dryLatencyBuffer;
    std::array<juce::SmoothedValue<float,
        juce::ValueSmoothingTypes::Linear>, characterCount> characterMix;
    std::array<juce::SmoothedValue<float,
        juce::ValueSmoothingTypes::Linear>, foldCount> foldMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        driveSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        symmetrySmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> shapeSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        dynamicsSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        attackSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        releaseSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        toneSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        stereoBloomSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
    std::array<float, 2> dcInputState {};
    std::array<float, 2> dcOutputState {};
    std::array<float, 2> toneState {};
    std::array<float, 2> focusState {};
    std::array<float, 2> bloomLowState {};
    std::atomic<float> activityMeter { 0.0f };
    double sampleRate = 44100.0;
    int channels = 2;
    int dryLatencyPosition = 0;
    int oversamplingLatency = 0;
    float linkedEnvelope = 0.0f;
    float inputMeanSquare = 1.0e-8f;
    float wetMeanSquare = 1.0e-8f;
    float compensationGain = 1.0f;
    bool initialized = false;
};
} // namespace megadsp
