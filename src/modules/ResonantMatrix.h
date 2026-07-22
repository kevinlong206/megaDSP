#pragma once

#include "DspModule.h"

namespace megadsp
{
class ResonantMatrixModule final : public DspModule
{
public:
    static constexpr int resonatorCount = 8;
    static constexpr int topologyCount = 4;
    static constexpr int scaleCount = 6;

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    double tailSeconds(const ControlValues&) const override;
    float meterValue() const override
    {
        return activityMeter.load(std::memory_order_relaxed);
    }

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

    struct ResonatorLine
    {
        std::array<FractionalDelay, 2> delay;
        std::array<float, 2> dampingState {};
    };

    struct NetworkOutput
    {
        float main = 0.0f;
        float trueSide = 0.0f;
        float decorSide = 0.0f;
        float energy = 0.0f;
    };

    class NetworkState
    {
    public:
        void prepare(double sampleRate, int topologyIndex);
        void reset();
        NetworkOutput processSample(
            float leftInput, float rightInput, float dampingCoefficient,
            float motionRateHz, float motionDepthCents,
            const std::array<float, resonatorCount>& baseDelaySamples,
            const std::array<float, resonatorCount>& feedbackGains);

    private:
        void applyTopology(std::array<float, resonatorCount>& values) const;

        std::array<ResonatorLine, resonatorCount> lines;
        std::array<float, resonatorCount> phases {};
        double sampleRate = 44100.0;
        int topology = 0;
        float maximumDelaySamples = 4.0f;
    };

    std::array<NetworkState, topologyCount> networks;
    std::array<juce::SmoothedValue<float,
        juce::ValueSmoothingTypes::Linear>, topologyCount> topologyMix;
    std::array<juce::SmoothedValue<float,
        juce::ValueSmoothingTypes::Linear>, resonatorCount> pitchDelaySmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> decaySmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        dampingCoefficientSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        detuneSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        motionRateSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        motionDepthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> widthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float,
        juce::ValueSmoothingTypes::Multiplicative> outputSmoothed;
    std::atomic<float> activityMeter { 0.0f };
    double sampleRate = 44100.0;
    float sideLowState = 0.0f;
    float activityEnvelope = 0.0f;
    float maximumDelaySamples = 4.0f;
    bool initialized = false;
};
} // namespace megadsp
