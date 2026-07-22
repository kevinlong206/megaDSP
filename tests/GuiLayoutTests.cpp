#include "ui/GuiLayout.h"
#include "ui/GraphStyle.h"

#include <juce_core/juce_core.h>

#include <algorithm>

namespace
{
class GuiLayoutTests final : public juce::UnitTest
{
public:
    GuiLayoutTests()
        : juce::UnitTest("GUI layout policy", "megaDSP")
    {
    }

    void runTest() override
    {
        using namespace megadsp;
        using namespace megadsp::ui;

        beginTest("Instance names are normalized for saved display");
        expectEquals(
            normalizeInstanceName("  Vocal\nLead\t "),
            juce::String("Vocal Lead"));
        expectEquals(
            normalizeInstanceName("Vocal\r\nLead"),
            juce::String("Vocal Lead"));
        expectEquals(
            normalizeInstanceName(juce::String::repeatedString("x", 40)).length(),
            instanceNameMaximumLength);
        expect(normalizeInstanceName("\r\n\t").isEmpty());

        beginTest("Legacy theme identities remain stable");
        expectEquals(backgroundThemeCount(), 16);
        expectEquals(
            juce::String(backgroundTheme(0).name),
            juce::String("Midnight Blue"));
        expectEquals(
            juce::String(backgroundTheme(9).name),
            juce::String("Graphite"));
        expectEquals(
            static_cast<juce::int64>(
                backgroundTheme(0).colour.getARGB()),
            static_cast<juce::int64>(0xff101b2b));
        expectEquals(safeBackgroundThemeIndex(-1), 0);
        expectEquals(safeBackgroundThemeIndex(999), 15);

        beginTest("Instance identity fits the minimum editor header");
        const auto identity = calculateIdentityHeaderLayout(
            editorMinimumWidth);
        expectEquals(identity.nameWidth, 104);
        expect(identity.usedWidth() <= editorMinimumWidth - 20);
        expect(editorMinimumWidth - 20 - identity.usedWidth() >= 200);

        beginTest("Toggle text keeps semantic label and state together");
        expectEquals(
            togglePresentation(ModuleType::delay, 5, true).buttonText,
            juce::String("Sync: Tempo"));
        expectEquals(
            togglePresentation(ModuleType::delay, 4, false).buttonText,
            juce::String("Ping Pong: Off"));
        expectEquals(
            togglePresentation(ModuleType::compressor, 7, true).buttonText,
            juce::String("Detector: External"));
        expectEquals(
            togglePresentation(ModuleType::stereoWidth, 7, true).buttonText,
            juce::String("Mono Safe: On"));
        expectEquals(
            togglePresentation(ModuleType::midSideDecoder, 1, false)
                .buttonText,
            juce::String("Swap Channels: Normal"));
        expectEquals(
            togglePresentation(ModuleType::limiter, 4, true).buttonText,
            juce::String("Auto Gain: Matched"));
        const auto unavailable = togglePresentation(
            ModuleType::compressor, 7, false,
            "External sidechain is unavailable.");
        expect(unavailable.tooltip.contains("Current state: Internal."));
        expect(unavailable.tooltip.contains(
            "External sidechain is unavailable."));
        expectEquals(
            unavailable.accessibilityDescription,
            unavailable.buttonText);

        beginTest("Full-panel keyboard order includes every active control");
        const auto eq = keyboardControlOrder(
            ModuleType::equalizer, moduleDefaults(ModuleType::equalizer),
            true, true);
        expectEquals(static_cast<int>(eq.size()), controlsPerSlot);
        expectEquals(eq[3], 10);
        expectEquals(eq[10], 11);
        expectEquals(eq.back(), 9);
        auto rolloffValues = moduleDefaults(ModuleType::equalizer);
        rolloffValues[10] = 1.0f;
        rolloffValues[11] = 1.0f;
        const auto rolloffEq = keyboardControlOrder(
            ModuleType::equalizer, rolloffValues, true, true);
        expect(std::find(rolloffEq.begin(), rolloffEq.end(), 1)
               == rolloffEq.end());
        expect(std::find(rolloffEq.begin(), rolloffEq.end(), 7)
               == rolloffEq.end());
        expect(std::find(rolloffEq.begin(), rolloffEq.end(), 2)
               != rolloffEq.end());
        expect(std::find(rolloffEq.begin(), rolloffEq.end(), 8)
               != rolloffEq.end());
        const auto limiter = keyboardControlOrder(
            ModuleType::limiter, moduleDefaults(ModuleType::limiter),
            true, true);
        expectEquals(static_cast<int>(limiter.size()), 5);

        const auto grain = keyboardControlOrder(
            ModuleType::randomGranulizer,
            moduleDefaults(ModuleType::randomGranulizer), true, true);
        expectEquals(static_cast<int>(grain.size()), controlsPerSlot);
        expectEquals(grain.front(), 3);

        const auto chorus = keyboardControlOrder(
            ModuleType::vintageChorus,
            moduleDefaults(ModuleType::vintageChorus), true, true);
        expectEquals(static_cast<int>(chorus.size()), controlsPerSlot);
        expectEquals(chorus.front(), 0);
        for (const auto type : {
                ModuleType::beatPermuter,
                ModuleType::spectralPrism,
                ModuleType::resonantMatrix,
                ModuleType::wavefoldGarden })
        {
            const auto creative = keyboardControlOrder(
                type, moduleDefaults(type), true, true);
            expectEquals(static_cast<int>(creative.size()), controlsPerSlot);
        }

        beginTest("Keyboard order respects contextual availability");
        const auto dynamicValues =
            moduleDefaults(ModuleType::dynamicEqualizer);
        const auto withoutSidechain = keyboardControlOrder(
            ModuleType::dynamicEqualizer, dynamicValues, true, false);
        const auto withSidechain = keyboardControlOrder(
            ModuleType::dynamicEqualizer, dynamicValues, true, true);
        expect(std::find(
                   withoutSidechain.begin(), withoutSidechain.end(), 9)
               == withoutSidechain.end());
        expect(std::find(withSidechain.begin(), withSidechain.end(), 9)
               != withSidechain.end());
        expectEquals(
            static_cast<int>(withSidechain.size()), controlsPerSlot);

        const auto tremoloValues =
            moduleDefaults(ModuleType::tremolo);
        const auto tremolo = keyboardControlOrder(
            ModuleType::tremolo, tremoloValues, true, true);
        expect(std::find(tremolo.begin(), tremolo.end(), 1)
               != tremolo.end());
        expect(std::find(tremolo.begin(), tremolo.end(), 3)
               == tremolo.end());
        expect(std::find(tremolo.begin(), tremolo.end(), 5)
               == tremolo.end());
        expect(std::find(tremolo.begin(), tremolo.end(), 8)
               == tremolo.end());
        auto syncedTremoloValues = tremoloValues;
        syncedTremoloValues[2] = 1.0f;
        const auto syncedTremolo = keyboardControlOrder(
            ModuleType::tremolo, syncedTremoloValues, true, true);
        expect(std::find(
                   syncedTremolo.begin(), syncedTremolo.end(), 1)
               == syncedTremolo.end());
        expect(std::find(
                   syncedTremolo.begin(), syncedTremolo.end(), 3)
               != syncedTremolo.end());

        beginTest("Tab widths never overflow available space");
        const auto full = calculateTabLayout(800, 8, false);
        expectEquals(full.tabWidth, 100);
        expectEquals(full.usedWidth, 800);
        const auto narrow = calculateTabLayout(760, 8, false);
        expectEquals(narrow.tabWidth, 95);
        expect(narrow.usedWidth <= 760);
        const auto withAdd = calculateTabLayout(800, 7, true);
        expectEquals(withAdd.addButtonWidth, 42);
        expect(withAdd.usedWidth <= 800);
        expect(withAdd.tabWidth < 180);

        beginTest("Wheel direction matches JUCE sliders");
        expectWithinAbsoluteError(
            normalizedWheelDelta(0.1f, 0.8f, false), 0.8f, 0.00001f);
        expectWithinAbsoluteError(
            normalizedWheelDelta(0.1f, 0.8f, true), -0.8f, 0.00001f);
        expectWithinAbsoluteError(
            normalizedWheelDelta(0.9f, 0.2f, false), -0.9f, 0.00001f);
        expectWithinAbsoluteError(
            normalizedWheelDelta(0.9f, 0.2f, true), 0.9f, 0.00001f);

        beginTest("Chorus geometry round trips at the field edge");
        const juce::Rectangle<float> field {
            10.0f, 20.0f, 400.0f, 180.0f
        };
        const auto handles = calculateChorusHandleGeometry(
            field, 1.0f, 0.72f, 0.84f);
        expect(handles.delayAndWidth.x >= field.getX()
               && handles.delayAndWidth.x <= field.getRight()
               && handles.delayAndWidth.y >= field.getY()
               && handles.delayAndWidth.y <= field.getBottom());
        expect(field.contains(handles.depth));
        expect(handles.depthDirection < 0.0f);
        expectWithinAbsoluteError(
            chorusDelayNormalizedAtX(
                field, handles.delayAndWidth.x),
            1.0f, 0.00001f);
        expectWithinAbsoluteError(
            chorusWidthNormalizedAtY(
                field, handles.delayAndWidth.y),
            0.72f, 0.00001f);
        expectWithinAbsoluteError(
            chorusDepthNormalizedAtX(handles, handles.depth.x),
            0.84f, 0.00001f);

        beginTest("Chorus overlapping hit zones remain deterministic");
        const auto zeroDepth = calculateChorusHandleGeometry(
            field, 0.55f, 0.5f, 0.0f);
        expect(zeroDepth.delayAndWidth.getDistanceFrom(
                   zeroDepth.depth)
               > 1.0f);
        expect(hitTestChorusHandles(
                   zeroDepth, zeroDepth.delayAndWidth, false)
               == ChorusHandleTarget::delayAndWidth);
        expect(hitTestChorusHandles(
                   zeroDepth, zeroDepth.depth, false)
               == ChorusHandleTarget::depth);
        const auto midpoint = (zeroDepth.delayAndWidth
                               + zeroDepth.depth)
                              * 0.5f;
        expect(hitTestChorusHandles(zeroDepth, midpoint, false)
               == ChorusHandleTarget::delayAndWidth);
        expect(hitTestChorusHandles(zeroDepth, midpoint, true)
               == ChorusHandleTarget::depth);
    }
};

GuiLayoutTests guiLayoutTests;
} // namespace
