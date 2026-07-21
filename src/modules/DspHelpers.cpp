#include "DspHelpers.h"

#include <cmath>

namespace megadsp::detail
{
juce::dsp::ConvolutionMessageQueue& convolutionMessageQueue()
{
    static juce::dsp::ConvolutionMessageQueue queue;
    return queue;
}
float lerp(float low, float high, float value)
{
    return low + (high - low) * juce::jlimit(0.0f, 1.0f, value);
}

float exponential(float low, float high, float value)
{
    return low * std::pow(high / low, juce::jlimit(0.0f, 1.0f, value));
}

float softClip(float input, int mode)
{
    switch (mode)
    {
        case 1: return (2.0f / juce::MathConstants<float>::pi) * std::atan(input);
        case 2: return juce::jlimit(-1.0f, 1.0f, input);
        default: return std::tanh(input);
    }
}

float coefficient(double sampleRate, float milliseconds)
{
    return std::exp(-1.0f / static_cast<float>(
        0.001 * juce::jmax(0.01f, milliseconds) * sampleRate));
}
} // namespace megadsp::detail
