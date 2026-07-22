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

struct BackgroundTheme
{
    const char* name;
    juce::Colour colour;
};

inline const std::array<BackgroundTheme, 16> backgroundThemes {{
    { "Midnight Blue", juce::Colour(0xff101b2b) },
    { "Crimson Red", juce::Colour(0xff2a1116) },
    { "Espresso Brown", juce::Colour(0xff291b14) },
    { "Forest Green", juce::Colour(0xff10251b) },
    { "Royal Purple", juce::Colour(0xff21152d) },
    { "Deep Teal", juce::Colour(0xff0e2527) },
    { "Slate", juce::Colour(0xff1b222c) },
    { "Aubergine", juce::Colour(0xff2a1424) },
    { "Burnished Copper", juce::Colour(0xff2b1d14) },
    { "Graphite", juce::Colour(0xff15171b) },
    { "Ocean Navy", juce::Colour(0xff10233a) },
    { "Emerald", juce::Colour(0xff0d2b24) },
    { "Burgundy", juce::Colour(0xff32131f) },
    { "Indigo", juce::Colour(0xff18183a) },
    { "Blue Charcoal", juce::Colour(0xff17232b) },
    { "Dark Olive", juce::Colour(0xff232817) }
}};

inline int backgroundThemeCount()
{
    return static_cast<int>(backgroundThemes.size());
}

inline int safeBackgroundThemeIndex(int index)
{
    return juce::jlimit(0, backgroundThemeCount() - 1, index);
}

inline const BackgroundTheme& backgroundTheme(int index)
{
    return backgroundThemes[static_cast<size_t>(
        safeBackgroundThemeIndex(index))];
}

inline juce::Colour backgroundColour(int index)
{
    return backgroundTheme(index).colour;
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
