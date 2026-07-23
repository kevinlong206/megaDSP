#pragma once

#include <juce_dsp/juce_dsp.h>

#include <cstddef>
#include <vector>

namespace megadsp
{
class FixedSpectralHistory
{
public:
    void prepare(std::size_t frameCapacity, std::size_t binCount);
    void reset() noexcept;
    void push(const float* frame, std::size_t valueCount) noexcept;
    const float* frame(std::size_t framesAgo) const noexcept;

    std::size_t capacity() const noexcept { return frameCapacity; }
    std::size_t bins() const noexcept { return binCount; }
    std::size_t size() const noexcept { return storedFrames; }

private:
    std::vector<float> storage;
    std::size_t frameCapacity = 0;
    std::size_t binCount = 0;
    std::size_t writePosition = 0;
    std::size_t storedFrames = 0;
};

class PerBinSmoother
{
public:
    void prepare(std::size_t binCount);
    void reset(float value = 0.0f) noexcept;
    float process(std::size_t bin, float target,
                  float coefficient) noexcept;
    float value(std::size_t bin) const noexcept;
    std::size_t size() const noexcept { return state.size(); }

private:
    std::vector<float> state;
};

class FixedLatencyStft
{
public:
    using Complex = juce::dsp::Complex<float>;
    using FrameCallback = void (*)(
        void* context, Complex* const* spectra, int channelCount,
        int binCount) noexcept;
    using OutputCallback = float (*)(
        void* context, int channel, float delayedDry, float wet) noexcept;

    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int hopSize = fftSize / 4;
    static constexpr int binCount = fftSize / 2 + 1;
    static constexpr int maxChannels = 2;
    static constexpr int reportedLatencySamples = fftSize;

    void prepare(const juce::dsp::ProcessSpec&);
    void reset() noexcept;
    void process(juce::AudioBuffer<float>&, void* context,
                 FrameCallback, OutputCallback) noexcept;

    int latencySamples() const noexcept { return reportedLatencySamples; }
    double sampleRate() const noexcept { return preparedSampleRate; }

private:
    static constexpr int wetRingSize = fftSize * 2;

    void processFrame(void* context, FrameCallback, int channelCount) noexcept;
    float& sample(std::vector<float>& storage, int channel, int index) noexcept;
    Complex* complexChannel(std::vector<Complex>& storage,
                            int channel) noexcept;

    juce::dsp::FFT fft { fftOrder };
    std::vector<float> analysisWindow;
    std::vector<float> synthesisWindow;
    std::vector<float> inputRing;
    std::vector<float> dryDelay;
    std::vector<float> wetRing;
    std::vector<Complex> fftInput;
    std::vector<Complex> spectrum;
    std::vector<Complex> inverseScratch;
    std::array<Complex*, maxChannels> spectrumPointers {};
    double preparedSampleRate = 44100.0;
    int inputWritePosition = 0;
    int dryPosition = 0;
    int wetReadPosition = 0;
    int samplesSinceFrame = 0;
    int validInputSamples = 0;
};
} // namespace megadsp
