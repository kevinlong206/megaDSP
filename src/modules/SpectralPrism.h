#pragma once

#include "DspModule.h"

namespace megadsp
{
class SpectralPrismModule final : public DspModule
{
public:
    static constexpr int fftOrder = 10;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int hopSize = 256;
    static constexpr int spectrumBins = fftSize / 2 + 1;
    static constexpr int maxChannels = 2;
    static constexpr int spectralDomains = 2;
    static constexpr int reportedLatencySamples = fftSize;
    static constexpr int dryDelaySize = reportedLatencySamples;
    static constexpr int wetRingSize = fftSize * 4;

    void prepare(const juce::dsp::ProcessSpec&) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    int latencySamples() const override { return reportedLatencySamples; }
    double tailSeconds(const ControlValues&) const override;
    float meterValue() const override
    {
        return outputMeter.load(std::memory_order_relaxed);
    }

private:
    using Complex = juce::dsp::Complex<float>;

    void initialiseWindows();
    void initialiseParameters(const ControlValues&);
    void updateParameterTargets(const ControlValues&);
    void processSpectralFrame(bool stereoInput, bool freezeTarget);
    float mappedSourceBin(int destinationBin, float pivotBin, float warp,
                          float shiftSemitones) const;

    static float normalized(float value, float fallback);
    static float bipolar(float value);
    static float semitones(float value);
    static float sanitizeSample(float value);
    static float principalPhase(float phase);
    static float hashUnit(std::uint32_t seed);
    static Complex polar(float magnitude, float phase);

    juce::dsp::FFT fft { fftOrder };
    std::array<float, fftSize> analysisWindow {};
    std::array<float, fftSize> synthesisWindow {};
    std::array<std::array<float, fftSize>, maxChannels> inputRing {};
    std::array<std::array<float, dryDelaySize>, maxChannels> dryDelay {};
    std::array<std::array<float, wetRingSize>, maxChannels> wetRing {};
    std::array<std::array<Complex, fftSize>, maxChannels> fftTimeDomain {};
    std::array<std::array<Complex, fftSize>, maxChannels> fftFrequencyDomain {};
    std::array<std::array<float, spectrumBins>, spectralDomains>
        previousAnalysisPhase {};
    std::array<std::array<float, spectrumBins>, spectralDomains>
        previousFluxMagnitude {};
    std::array<std::array<float, spectrumBins>, spectralDomains>
        smearMagnitudeState {};
    std::array<std::array<float, spectrumBins>, spectralDomains>
        liveOutputPhase {};
    std::array<std::array<float, spectrumBins>, spectralDomains>
        freezeMagnitude {};
    std::array<std::array<float, spectrumBins>, spectralDomains>
        freezePhaseAdvance {};
    std::array<std::array<float, spectrumBins>, spectralDomains>
        freezeOutputPhase {};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> warpSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        pivotSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> shiftSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smearSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        motionRateSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        motionDepthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        diffusionSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> spreadSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        transientSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
        outputSmoothed;
    double sampleRate = 44100.0;
    double motionPhase = 0.0;
    float currentWarp = 0.0f;
    float currentPivotHz = 1000.0f;
    float currentShiftSemitones = 0.0f;
    float currentSmear = 0.0f;
    float currentMotionRate = 0.15f;
    float currentMotionDepth = 0.0f;
    float currentDiffusion = 0.0f;
    float currentSpread = 0.0f;
    float currentTransientPreserve = 0.0f;
    float freezeBlend = 0.0f;
    float transientEnvelope = 0.0f;
    float meterEnvelope = 0.0f;
    int inputWritePosition = 0;
    int dryDelayPosition = 0;
    int wetReadPosition = 0;
    int samplesSinceFrame = 0;
    int validInputSamples = 0;
    int activeChannels = 2;
    bool parametersInitialised = false;
    bool spectralInitialised = false;
    bool freezeLatched = false;
    std::atomic<float> outputMeter { 0.0f };
};
} // namespace megadsp
