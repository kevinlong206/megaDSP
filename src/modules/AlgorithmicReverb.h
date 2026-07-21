#pragma once

#include "DspModule.h"

namespace megadsp
{
class AlgorithmicReverbModule final : public DspModule
{
public:
    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    double tailSeconds(const ControlValues&) const override;

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

    struct TankParameters
    {
        float decaySeconds = 2.0f;
        float size = 1.0f;
        float density = 0.55f;
        float movement = 0.2f;
        float damping = 0.5f;
    };

    class Tank
    {
    public:
        void prepare(double sampleRate);
        void reset();
        void setMode(int mode);
        void setTargets(const TankParameters&);
        std::array<float, 2> process(const std::array<float, 2>& input,
                                     float density, float movement);
        int getMode() const { return mode; }

    private:
        static constexpr int lineCount = 16;
        static void hadamard(std::array<float, lineCount>& values);
        float lineDelaySamples(int line, float size, float modulation) const;

        std::array<FractionalDelay, lineCount> delays;
        std::array<FractionalDelay, 2> injectionHistory;
        std::array<DecayFilter, lineCount> decayFilters;
        std::array<float, lineCount> lowGains {};
        std::array<float, lineCount> midGains {};
        std::array<float, lineCount> highGains {};
        std::array<float, lineCount> targetLowGains {};
        std::array<float, lineCount> targetMidGains {};
        std::array<float, lineCount> targetHighGains {};
        std::array<float, lineCount / 2> rotationCos {};
        std::array<float, lineCount / 2> rotationSin {};
        std::array<float, lineCount / 2> rotationDeltaCos {};
        std::array<float, lineCount / 2> rotationDeltaSin {};
        std::array<float, lineCount> modulationCos {};
        std::array<float, lineCount> modulationSin {};
        std::array<float, lineCount> modulationDeltaCos {};
        std::array<float, lineCount> modulationDeltaSin {};
        TankParameters parameters;
        double sampleRate = 44100.0;
        int mode = 0;
        float currentSize = 1.0f;
        float oldSize = 1.0f;
        float targetSize = 1.0f;
        float sizeFade = 1.0f;
        float lowCoefficient = 0.0f;
        float highCoefficient = 0.0f;
        float targetLowCoefficient = 0.0f;
        float targetHighCoefficient = 0.0f;
        float coefficientSmoothing = 0.001f;
        bool decayInitialized = false;
        int oscillatorCounter = 0;
    };

    float readInputHistory(int channel, float delaySamples) const;
    std::array<float, 2> earlyField(int tankIndex, int mode,
                                    float preDelaySamples, float size,
                                    float density);

    std::array<std::vector<float>, 2> inputHistory;
    int inputWritePosition = 0;
    std::array<Tank, 2> tanks;
    std::array<std::array<std::array<float, 2>, 16>, 2> earlyFilterState {};
    std::array<float, 2> inputLowState {};
    std::array<float, 2> inputHighState {};
    float widthLowState = 0.0f;
    double sampleRate = 44100.0;
    int activeTank = 0;
    int pendingMode = 0;
    bool modeTransition = false;
    float modeTransitionPosition = 1.0f;
    bool parametersInitialized = false;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> decaySmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> sizeSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> drySmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> widthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> preDelaySmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> diffusionSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> modulationSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> dampingSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> lowCoefficientSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> highCoefficientSmoothed;
};
} // namespace megadsp
