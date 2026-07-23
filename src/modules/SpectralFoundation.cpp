#include "SpectralFoundation.h"
#include "DspSafety.h"

#include <algorithm>
#include <cmath>

namespace megadsp
{
void FixedSpectralHistory::prepare(
    std::size_t requestedFrames, std::size_t requestedBins)
{
    frameCapacity = requestedFrames;
    binCount = requestedBins;
    storage.assign(frameCapacity * binCount, 0.0f);
    reset();
}

void FixedSpectralHistory::reset() noexcept
{
    std::fill(storage.begin(), storage.end(), 0.0f);
    writePosition = 0;
    storedFrames = 0;
}

void FixedSpectralHistory::push(
    const float* source, std::size_t valueCount) noexcept
{
    if (frameCapacity == 0 || binCount == 0 || source == nullptr)
        return;
    auto* destination = storage.data() + writePosition * binCount;
    const auto count = juce::jmin(binCount, valueCount);
    for (std::size_t index = 0; index < count; ++index)
        destination[index] = detail::finiteSample(source[index]);
    std::fill(destination + count, destination + binCount, 0.0f);
    writePosition = (writePosition + 1) % frameCapacity;
    storedFrames = juce::jmin(storedFrames + 1, frameCapacity);
}

const float* FixedSpectralHistory::frame(std::size_t framesAgo) const noexcept
{
    if (framesAgo >= storedFrames || frameCapacity == 0 || binCount == 0)
        return nullptr;
    const auto index =
        (writePosition + frameCapacity - 1 - framesAgo) % frameCapacity;
    return storage.data() + index * binCount;
}

void PerBinSmoother::prepare(std::size_t binCount)
{
    state.assign(binCount, 0.0f);
}

void PerBinSmoother::reset(float initialValue) noexcept
{
    initialValue = detail::finiteSample(initialValue);
    std::fill(state.begin(), state.end(), initialValue);
}

float PerBinSmoother::process(
    std::size_t bin, float target, float coefficient) noexcept
{
    if (bin >= state.size())
        return 0.0f;
    target = detail::finiteSample(target);
    coefficient = juce::jlimit(
        0.0f, 0.999999f, detail::finiteSample(coefficient));
    auto& current = state[bin];
    current = coefficient * current + (1.0f - coefficient) * target;
    if (!std::isfinite(current))
        current = 0.0f;
    return current;
}

float PerBinSmoother::value(std::size_t bin) const noexcept
{
    return bin < state.size() ? state[bin] : 0.0f;
}

void FixedLatencyStft::prepare(const juce::dsp::ProcessSpec& spec)
{
    preparedSampleRate = detail::safeSampleRate(spec.sampleRate);
    analysisWindow.resize(fftSize);
    synthesisWindow.resize(fftSize);
    inputRing.resize(maxChannels * fftSize);
    dryDelay.resize(maxChannels * reportedLatencySamples);
    wetRing.resize(maxChannels * wetRingSize);
    fftInput.resize(maxChannels * fftSize);
    spectrum.resize(maxChannels * fftSize);
    inverseScratch.resize(maxChannels * fftSize);

    constexpr auto twoPi = juce::MathConstants<float>::twoPi;
    for (int index = 0; index < fftSize; ++index)
        analysisWindow[static_cast<std::size_t>(index)] =
            0.5f - 0.5f * std::cos(
                              twoPi * static_cast<float>(index)
                              / static_cast<float>(fftSize));
    for (int index = 0; index < fftSize; ++index)
    {
        float overlapPower = 0.0f;
        for (int overlap = 0; overlap < fftSize / hopSize; ++overlap)
        {
            const auto wrapped = (index + overlap * hopSize) % fftSize;
            const auto value =
                analysisWindow[static_cast<std::size_t>(wrapped)];
            overlapPower += value * value;
        }
        synthesisWindow[static_cast<std::size_t>(index)] =
            overlapPower > 1.0e-12f
            ? analysisWindow[static_cast<std::size_t>(index)] / overlapPower
            : 0.0f;
    }
    reset();
}

void FixedLatencyStft::reset() noexcept
{
    std::fill(inputRing.begin(), inputRing.end(), 0.0f);
    std::fill(dryDelay.begin(), dryDelay.end(), 0.0f);
    std::fill(wetRing.begin(), wetRing.end(), 0.0f);
    std::fill(fftInput.begin(), fftInput.end(), Complex {});
    std::fill(spectrum.begin(), spectrum.end(), Complex {});
    std::fill(inverseScratch.begin(), inverseScratch.end(), Complex {});
    inputWritePosition = 0;
    dryPosition = 0;
    wetReadPosition = 0;
    samplesSinceFrame = 0;
    validInputSamples = 0;
}

void FixedLatencyStft::process(
    juce::AudioBuffer<float>& buffer, void* context,
    FrameCallback frameCallback, OutputCallback outputCallback) noexcept
{
    juce::ScopedNoDenormals noDenormals;
    const auto channels = juce::jlimit(
        0, maxChannels, buffer.getNumChannels());
    if (channels == 0 || buffer.getNumSamples() == 0)
        return;

    for (int frameSample = 0; frameSample < buffer.getNumSamples();
         ++frameSample)
    {
        std::array<float, maxChannels> input {};
        std::array<float, maxChannels> delayed {};
        std::array<float, maxChannels> wet {};
        for (int channel = 0; channel < channels; ++channel)
        {
            input[static_cast<std::size_t>(channel)] = detail::finiteSample(
                buffer.getSample(channel, frameSample));
            delayed[static_cast<std::size_t>(channel)] =
                sample(dryDelay, channel, dryPosition);
            sample(dryDelay, channel, dryPosition) =
                input[static_cast<std::size_t>(channel)];
            wet[static_cast<std::size_t>(channel)] =
                sample(wetRing, channel, wetReadPosition);
            sample(wetRing, channel, wetReadPosition) = 0.0f;
            sample(inputRing, channel, inputWritePosition) =
                input[static_cast<std::size_t>(channel)];
        }

        inputWritePosition = (inputWritePosition + 1) % fftSize;
        validInputSamples = juce::jmin(validInputSamples + 1, fftSize);
        if (++samplesSinceFrame >= hopSize)
        {
            samplesSinceFrame = 0;
            if (validInputSamples == fftSize)
                processFrame(context, frameCallback, channels);
        }

        for (int channel = 0; channel < channels; ++channel)
        {
            const auto output = outputCallback != nullptr
                ? outputCallback(
                      context, channel,
                      delayed[static_cast<std::size_t>(channel)],
                      wet[static_cast<std::size_t>(channel)])
                : wet[static_cast<std::size_t>(channel)];
            buffer.setSample(
                channel, frameSample, detail::finiteSample(output));
        }
        dryPosition = (dryPosition + 1) % reportedLatencySamples;
        wetReadPosition = (wetReadPosition + 1) % wetRingSize;
    }
}

void FixedLatencyStft::processFrame(
    void* context, FrameCallback callback, int channelCount) noexcept
{
    for (int channel = 0; channel < channelCount; ++channel)
    {
        auto* time = complexChannel(fftInput, channel);
        auto* frequency = complexChannel(spectrum, channel);
        for (int index = 0; index < fftSize; ++index)
        {
            const auto ringIndex = (inputWritePosition + index) % fftSize;
            time[index] = {
                sample(inputRing, channel, ringIndex)
                    * analysisWindow[static_cast<std::size_t>(index)],
                0.0f
            };
        }
        fft.perform(time, frequency, false);
        spectrumPointers[static_cast<std::size_t>(channel)] = frequency;
    }

    if (callback != nullptr)
        callback(context, spectrumPointers.data(), channelCount, binCount);

    const auto outputStart = (wetReadPosition + 1) % wetRingSize;
    for (int channel = 0; channel < channelCount; ++channel)
    {
        auto* frequency = complexChannel(spectrum, channel);
        frequency[0] = { detail::finiteSample(frequency[0].real()), 0.0f };
        frequency[fftSize / 2] = {
            detail::finiteSample(frequency[fftSize / 2].real()), 0.0f
        };
        for (int bin = 1; bin < fftSize / 2; ++bin)
        {
            auto value = frequency[bin];
            if (!std::isfinite(value.real()) || !std::isfinite(value.imag()))
                value = {};
            frequency[bin] = value;
            frequency[fftSize - bin] = std::conj(value);
        }

        auto* inverse = complexChannel(inverseScratch, channel);
        fft.perform(frequency, inverse, true);
        for (int index = 0; index < fftSize; ++index)
        {
            const auto ringIndex = (outputStart + index) % wetRingSize;
            auto value = inverse[index].real()
                         * synthesisWindow[static_cast<std::size_t>(index)];
            value = detail::finiteSample(value);
            auto& destination = sample(wetRing, channel, ringIndex);
            destination = detail::finiteSample(destination + value);
        }
    }
}

float& FixedLatencyStft::sample(
    std::vector<float>& storage, int channel, int index) noexcept
{
    const auto stride =
        storage.size() / static_cast<std::size_t>(maxChannels);
    return storage[static_cast<std::size_t>(channel) * stride
                   + static_cast<std::size_t>(index)];
}

FixedLatencyStft::Complex* FixedLatencyStft::complexChannel(
    std::vector<Complex>& storage, int channel) noexcept
{
    return storage.data() + static_cast<std::size_t>(channel * fftSize);
}
} // namespace megadsp
