#pragma once

#include <juce_dsp/juce_dsp.h>

namespace megadsp::detail
{
juce::dsp::ConvolutionMessageQueue& convolutionMessageQueue();
float lerp(float low, float high, float value);
float exponential(float low, float high, float value);
float softClip(float input, int mode);
float coefficient(double sampleRate, float milliseconds);
} // namespace megadsp::detail
