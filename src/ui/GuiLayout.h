#pragma once

#include "../modules/ModuleRegistry.h"

#include <juce_graphics/juce_graphics.h>

#include <vector>

namespace megadsp::ui
{
inline constexpr int editorMinimumWidth = 820;
inline constexpr int editorMinimumHeight = 620;
inline constexpr int editorMaximumWidth = 1800;
inline constexpr int editorMaximumHeight = 1200;

struct TogglePresentation
{
    juce::String semanticLabel;
    juce::String stateText;
    juce::String buttonText;
    juce::String tooltip;
    juce::String accessibilityDescription;
};

TogglePresentation togglePresentation(
    ModuleType, int control, bool state,
    const juce::String& unavailableReason = {});

struct TabLayout
{
    int tabWidth = 0;
    int addButtonWidth = 0;
    int usedWidth = 0;
};

TabLayout calculateTabLayout(
    int availableWidth, int activeTabs, bool showAddButton);
juce::String compactTabName(ModuleType);
float normalizedWheelDelta(float deltaX, float deltaY, bool isReversed);

std::vector<int> keyboardControlOrder(
    ModuleType, const std::array<float, controlsPerSlot>& values,
    bool hasStereoOutput, bool hasExternalSidechain);
ControlKind keyboardControlKind(ModuleType, int control);
int keyboardControlOptionCount(ModuleType, int control);
juce::String keyboardControlLabel(ModuleType, int control);
juce::String keyboardControlValueText(
    ModuleType, int control, float normalized);

enum class ChorusHandleTarget
{
    none,
    delayAndWidth,
    depth
};

struct ChorusHandleGeometry
{
    juce::Rectangle<float> field;
    juce::Point<float> delayAndWidth;
    juce::Point<float> depthOrigin;
    juce::Point<float> depth;
    float depthSpan = 1.0f;
    float depthDirection = 1.0f;
};

ChorusHandleGeometry calculateChorusHandleGeometry(
    juce::Rectangle<float> field, float delayNormalized,
    float widthNormalized, float depthNormalized);
float chorusDelayNormalizedAtX(
    juce::Rectangle<float> field, float x);
float chorusWidthNormalizedAtY(
    juce::Rectangle<float> field, float y);
float chorusDepthNormalizedAtX(
    const ChorusHandleGeometry&, float x);
ChorusHandleTarget hitTestChorusHandles(
    const ChorusHandleGeometry&, juce::Point<float>,
    bool preferDepthOnTie);
} // namespace megadsp::ui
