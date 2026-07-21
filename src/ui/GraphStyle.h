#pragma once

#include <juce_graphics/juce_graphics.h>

#include <array>
#include <cmath>

namespace megadsp::ui
{
inline const auto accent = juce::Colour(0xff54c9da);
inline const auto inputColour = juce::Colour(0xff6f879d);
inline const auto outputColour = juce::Colour(0xff63df9a);
inline const auto reductionColour = juce::Colour(0xffffa85b);

inline const std::array<juce::Colour, 10> backgroundColours {
    juce::Colour(0xff101b2b),
    juce::Colour(0xff2a1116),
    juce::Colour(0xff291b14),
    juce::Colour(0xff10251b),
    juce::Colour(0xff21152d),
    juce::Colour(0xff0e2527),
    juce::Colour(0xff1b222c),
    juce::Colour(0xff2a1424),
    juce::Colour(0xff2b1d14),
    juce::Colour(0xff15171b)
};

inline juce::Colour backgroundColour(int index)
{
    return backgroundColours[static_cast<size_t>(
        juce::jlimit(0, static_cast<int>(backgroundColours.size()) - 1,
                     index))];
}

inline float lerp(float low, float high, float value)
{
    return low + (high - low) * juce::jlimit(0.0f, 1.0f, value);
}

inline float exponential(float low, float high, float value)
{
    return low * std::pow(high / low, juce::jlimit(0.0f, 1.0f, value));
}
} // namespace megadsp::ui
