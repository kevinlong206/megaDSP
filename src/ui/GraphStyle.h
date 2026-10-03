#pragma once

#include "../modules/ModuleRegistry.h"

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

inline float moduleCategoryHue(ModuleCategory category)
{
    switch (category)
    {
        case ModuleCategory::eqAndFilters: return 0.52f;
        case ModuleCategory::dynamics: return 0.085f;
        case ModuleCategory::saturationAndColor: return 0.015f;
        case ModuleCategory::delayAndEcho: return 0.59f;
        case ModuleCategory::reverbAndSpace: return 0.72f;
        case ModuleCategory::modulation: return 0.86f;
        case ModuleCategory::stereoAndUtility: return 0.42f;
        case ModuleCategory::glitchAndCreative: return 0.95f;
        case ModuleCategory::guitar: return 0.14f;
        case ModuleCategory::none: return 0.0f;
    }
    return 0.0f;
}

inline juce::Colour modulePaletteColour(ModuleType type)
{
    const auto& definition = moduleDefinition(type);
    const auto stableIndex = static_cast<int>(type);
    const auto variation =
        (static_cast<float>((stableIndex * 37) % 17) - 8.0f) * 0.0045f;
    auto hue = moduleCategoryHue(definition.category) + variation;
    hue -= std::floor(hue);
    const auto saturation =
        0.48f + static_cast<float>((stableIndex * 11) % 5) * 0.025f;
    const auto brightness =
        0.72f + static_cast<float>((stableIndex * 7) % 4) * 0.025f;
    return juce::Colour::fromHSV(
        hue, saturation, brightness, 1.0f);
}

inline juce::Colour moduleSurfaceColour(
    juce::Colour editorBackground, ModuleType type,
    float colourAmount = 0.22f)
{
    return editorBackground
        .interpolatedWith(
            modulePaletteColour(type),
            juce::jlimit(0.0f, 1.0f, colourAmount))
        .brighter(0.08f);
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
