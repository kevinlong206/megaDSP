#include "GuiLayout.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace megadsp::ui
{
namespace
{
bool isEqualizerModeControl(ModuleType type, int control)
{
    return type == ModuleType::equalizer
           && (control == 10 || control == 11);
}

juce::String toggleLabel(ModuleType type, int control)
{
    if (type == ModuleType::equalizer && control == 10)
        return "Low Mode";
    if (type == ModuleType::equalizer && control == 11)
        return "High Mode";
    if (type == ModuleType::dynamicEqualizer && control == 9)
        return "Sidechain";
    return controlMetadata(type, control).label;
}

juce::String toggleState(
    ModuleType type, int control, bool state)
{
    if (type == ModuleType::equalizer && control == 10)
        return state ? "High Pass" : "Bell";
    if (type == ModuleType::equalizer && control == 11)
        return state ? "Low Pass" : "Bell";
    if (type == ModuleType::compressor && control == 7)
        return state ? "External" : "Internal";
    if ((type == ModuleType::delay && control == 5)
        || (type == ModuleType::tremolo && control == 2))
        return state ? "Tempo" : "Free";
    if (type == ModuleType::midSideDecoder && control == 1)
        return state ? "Swapped" : "Normal";
    if (type == ModuleType::midSideDecoder && control == 2)
        return state ? "Muted" : "Open";
    if (type == ModuleType::dynamicEqualizer && control == 9)
        return state ? "External" : "Internal";
    if (type == ModuleType::limiter && control == 4)
        return state ? "Matched" : "Off";
    return state ? "On" : "Off";
}

juce::String toggleHelp(ModuleType type, int control)
{
    if (type == ModuleType::equalizer && control == 10)
        return "Switches the low band between bell and high-pass response.";
    if (type == ModuleType::equalizer && control == 11)
        return "Switches the high band between bell and low-pass response.";
    return controlMetadata(type, control).tooltip;
}

bool isSemanticKeyboardControl(ModuleType type, int control)
{
    if (!juce::isPositiveAndBelow(control, controlsPerSlot))
        return false;
    return isEqualizerModeControl(type, control)
           || moduleDefinition(type)
                  .controls[static_cast<size_t>(control)]
                  .isActive();
}
} // namespace

TogglePresentation togglePresentation(
    ModuleType type, int control, bool state,
    const juce::String& unavailableReason)
{
    TogglePresentation result;
    result.semanticLabel = toggleLabel(type, control);
    result.stateText = toggleState(type, control, state);
    result.buttonText =
        result.semanticLabel + ": " + result.stateText;
    result.accessibilityDescription = result.buttonText;
    result.tooltip = toggleHelp(type, control).trim();
    if (result.tooltip.isNotEmpty()
        && !result.tooltip.endsWithChar('.'))
        result.tooltip += ".";
    result.tooltip += " Current state: " + result.stateText + ".";
    if (unavailableReason.isNotEmpty())
        result.tooltip += " " + unavailableReason.trim();
    return result;
}

TabLayout calculateTabLayout(
    int availableWidth, int activeTabs, bool showAddButton)
{
    const auto available = juce::jmax(0, availableWidth);
    const auto tabs = juce::jmax(0, activeTabs);
    TabLayout result;
    result.addButtonWidth =
        showAddButton ? juce::jmin(42, available) : 0;
    const auto tabSpace =
        juce::jmax(0, available - result.addButtonWidth);
    if (tabs > 0)
        result.tabWidth = juce::jmin(180, tabSpace / tabs);
    result.usedWidth =
        result.tabWidth * tabs + result.addButtonWidth;
    return result;
}

juce::String compactTabName(ModuleType type)
{
    switch (type)
    {
        case ModuleType::equalizer: return "EQ";
        case ModuleType::compressor: return "Comp";
        case ModuleType::saturator: return "Sat";
        case ModuleType::algorithmicReverb: return "ARev";
        case ModuleType::stereoWidth: return "Width";
        case ModuleType::midSideDecoder: return "M/S";
        case ModuleType::tremolo: return "Trem";
        case ModuleType::rotarySpeaker: return "Rot";
        case ModuleType::convolutionReverb: return "CRev";
        case ModuleType::dynamicEqualizer: return "DynEQ";
        case ModuleType::randomGranulizer: return "Grains";
        case ModuleType::vintageChorus: return "Chorus";
        case ModuleType::empty:
        case ModuleType::delay:
        case ModuleType::limiter:
            return moduleDefinition(type).displayName;
    }
    return moduleDefinition(type).displayName;
}

float normalizedWheelDelta(
    float deltaX, float deltaY, bool isReversed)
{
    const auto dominant =
        std::abs(deltaX) > std::abs(deltaY) ? -deltaX : deltaY;
    return dominant * (isReversed ? -1.0f : 1.0f);
}

std::vector<int> keyboardControlOrder(
    ModuleType type,
    const std::array<float, controlsPerSlot>& values,
    bool hasStereoOutput, bool hasExternalSidechain)
{
    std::array<int, controlsPerSlot> candidates {};
    for (int control = 0; control < controlsPerSlot; ++control)
        candidates[static_cast<size_t>(control)] = control;

    switch (type)
    {
        case ModuleType::equalizer:
            candidates = { 0, 1, 2, 10, 3, 4, 5, 6, 7, 8, 11, 9 };
            break;
        case ModuleType::randomGranulizer:
            candidates = { 3, 6, 7, 1, 4, 0, 2, 5, 8, 9, 10, 11 };
            break;
        case ModuleType::vintageChorus:
            candidates = { 0, 1, 2, 3, 5, 4, 6, 10, 7, 8, 9, 11 };
            break;
        case ModuleType::empty:
        case ModuleType::compressor:
        case ModuleType::saturator:
        case ModuleType::delay:
        case ModuleType::limiter:
        case ModuleType::algorithmicReverb:
        case ModuleType::stereoWidth:
        case ModuleType::midSideDecoder:
        case ModuleType::tremolo:
        case ModuleType::rotarySpeaker:
        case ModuleType::convolutionReverb:
        case ModuleType::dynamicEqualizer:
            break;
    }

    std::vector<int> result;
    result.reserve(controlsPerSlot);
    for (const auto control : candidates)
    {
        if (!isSemanticKeyboardControl(type, control))
            continue;
        if (type == ModuleType::equalizer
            && ((control == 1
                 && equalizerLowIsHighPass(values[10]))
                || (control == 7
                    && equalizerHighIsLowPass(values[11]))))
            continue;
        if (!isEqualizerModeControl(type, control)
            && (!isControlContextuallyVisible(type, control, values)
                || !isControlContextuallyEnabled(
                    type, control, values, hasStereoOutput,
                    hasExternalSidechain)))
            continue;
        result.push_back(control);
    }
    return result;
}

ControlKind keyboardControlKind(ModuleType type, int control)
{
    if (isEqualizerModeControl(type, control))
        return ControlKind::toggle;
    return controlMetadata(type, control).kind;
}

int keyboardControlOptionCount(ModuleType type, int control)
{
    const auto kind = keyboardControlKind(type, control);
    if (kind == ControlKind::toggle)
        return 2;
    if (kind == ControlKind::choice)
        return controlOptions(type, control).size();
    return 0;
}

juce::String keyboardControlLabel(ModuleType type, int control)
{
    if (keyboardControlKind(type, control) == ControlKind::toggle)
        return toggleLabel(type, control);
    return controlMetadata(type, control).label;
}

juce::String keyboardControlValueText(
    ModuleType type, int control, float normalized)
{
    if (keyboardControlKind(type, control) == ControlKind::toggle)
        return toggleState(type, control, normalized >= 0.5f);
    return formatControlValue(type, control, normalized);
}

ChorusHandleGeometry calculateChorusHandleGeometry(
    juce::Rectangle<float> field, float delayNormalized,
    float widthNormalized, float depthNormalized)
{
    ChorusHandleGeometry result;
    result.field = field;
    const auto safeDelay =
        juce::jlimit(0.0f, 1.0f, delayNormalized);
    const auto safeWidth =
        juce::jlimit(0.0f, 1.0f, widthNormalized);
    const auto safeDepth =
        juce::jlimit(0.0f, 1.0f, depthNormalized);
    const auto delayMilliseconds =
        2.0f * std::pow(15.0f, safeDelay);
    result.delayAndWidth = {
        field.getX()
            + (delayMilliseconds - 2.0f) / 28.0f
                  * field.getWidth(),
        field.getBottom() - safeWidth * field.getHeight()
    };

    const auto offset =
        juce::jmin(14.0f, field.getHeight() * 0.25f);
    const auto depthY =
        result.delayAndWidth.y - field.getY() >= offset
            ? result.delayAndWidth.y - offset
            : result.delayAndWidth.y + offset;
    result.depthOrigin = {
        result.delayAndWidth.x,
        juce::jlimit(field.getY(), field.getBottom(), depthY)
    };

    const auto desiredSpan = field.getWidth() * 0.24f;
    const auto rightSpace =
        juce::jmax(0.0f, field.getRight() - result.depthOrigin.x);
    const auto leftSpace =
        juce::jmax(0.0f, result.depthOrigin.x - field.getX());
    if (rightSpace >= desiredSpan)
        result.depthDirection = 1.0f;
    else if (leftSpace >= desiredSpan)
        result.depthDirection = -1.0f;
    else
        result.depthDirection =
            rightSpace >= leftSpace ? 1.0f : -1.0f;
    const auto available = result.depthDirection > 0.0f
        ? rightSpace : leftSpace;
    result.depthSpan =
        juce::jmax(1.0f, juce::jmin(desiredSpan, available));
    result.depth = {
        result.depthOrigin.x
            + result.depthDirection * safeDepth * result.depthSpan,
        result.depthOrigin.y
    };
    result.depth.x =
        juce::jlimit(field.getX(), field.getRight(), result.depth.x);
    return result;
}

float chorusDelayNormalizedAtX(
    juce::Rectangle<float> field, float x)
{
    const auto proportion = juce::jlimit(
        0.0f, 1.0f,
        (x - field.getX()) / juce::jmax(1.0f, field.getWidth()));
    const auto milliseconds = 2.0f + proportion * 28.0f;
    return juce::jlimit(
        0.0f, 1.0f,
        std::log(milliseconds / 2.0f) / std::log(15.0f));
}

float chorusWidthNormalizedAtY(
    juce::Rectangle<float> field, float y)
{
    return juce::jlimit(
        0.0f, 1.0f,
        (field.getBottom() - y)
            / juce::jmax(1.0f, field.getHeight()));
}

float chorusDepthNormalizedAtX(
    const ChorusHandleGeometry& geometry, float x)
{
    return juce::jlimit(
        0.0f, 1.0f,
        geometry.depthDirection
            * (x - geometry.depthOrigin.x)
            / juce::jmax(1.0f, geometry.depthSpan));
}

ChorusHandleTarget hitTestChorusHandles(
    const ChorusHandleGeometry& geometry,
    juce::Point<float> position, bool preferDepthOnTie)
{
    const auto delayDistance =
        position.getDistanceFrom(geometry.delayAndWidth);
    const auto depthDistance =
        position.getDistanceFrom(geometry.depth);
    const auto delayHit = delayDistance <= 18.0f;
    const auto depthHit = depthDistance <= 18.0f;
    if (!delayHit && !depthHit)
        return ChorusHandleTarget::none;
    if (delayHit && !depthHit)
        return ChorusHandleTarget::delayAndWidth;
    if (depthHit && !delayHit)
        return ChorusHandleTarget::depth;
    if (std::abs(delayDistance - depthDistance) <= 0.75f)
        return preferDepthOnTie ? ChorusHandleTarget::depth
                                : ChorusHandleTarget::delayAndWidth;
    return depthDistance < delayDistance
        ? ChorusHandleTarget::depth
        : ChorusHandleTarget::delayAndWidth;
}
} // namespace megadsp::ui
