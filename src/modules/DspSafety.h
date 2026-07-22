#pragma once

#include <juce_core/juce_core.h>

#include <cmath>

namespace megadsp::detail
{
inline double safeSampleRate(double sampleRate) noexcept
{
    return std::isfinite(sampleRate)
        ? juce::jlimit(8000.0, 768000.0, sampleRate)
        : 44100.0;
}

inline float normalizedControl(float value, float fallback) noexcept
{
    return std::isfinite(value) ? juce::jlimit(0.0f, 1.0f, value) : fallback;
}

inline float finiteSample(float value) noexcept
{
    return std::isfinite(value) ? value : 0.0f;
}
} // namespace megadsp::detail
