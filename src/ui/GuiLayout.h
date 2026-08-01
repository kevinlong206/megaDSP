#pragma once

#include "../modules/ModuleRegistry.h"

#include <juce_graphics/juce_graphics.h>

#include <cstddef>
#include <vector>

namespace megadsp::ui
{
inline constexpr int editorMinimumWidth = 820;
inline constexpr int editorMinimumHeight = 620;
inline constexpr int editorMaximumWidth = 1800;
inline constexpr int editorMaximumHeight = 1200;
inline constexpr int instanceNameMaximumLength = 32;
inline constexpr int moduleCardHeight = 320;
inline constexpr int moduleCardGap = 8;
inline constexpr int addModuleCardHeight = 42;

juce::String normalizeInstanceName(const juce::String&);

struct IdentityHeaderLayout
{
    int titleWidth = 112;
    int labelWidth = 58;
    int nameWidth = 104;
    int themeWidth = 62;
    int presetWidth = 132;
    int saveWidth = 56;
    int loadWidth = 56;

    int usedWidth() const
    {
        return titleWidth + labelWidth + nameWidth + themeWidth
               + presetWidth + saveWidth + loadWidth;
    }
};

IdentityHeaderLayout calculateIdentityHeaderLayout(int editorWidth);

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
int calculateRackContentHeight(int activeModules, bool showAddButton);
juce::String compactTabName(ModuleType);

struct StereoBalanceMeasurement
{
    float leftRmsDb = -60.0f;
    float rightRmsDb = -60.0f;
    float leftPeakDb = -60.0f;
    float rightPeakDb = -60.0f;
    float balance = 0.0f;
};

StereoBalanceMeasurement measureStereoBalance(
    const float* left, const float* right, std::size_t sampleCount);

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
