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
        expectEquals(
            togglePresentation(ModuleType::gateExpander, 9, true).buttonText,
            juce::String("Listen: On"));
        expectEquals(
            togglePresentation(
                ModuleType::transientDesigner, 5, false).buttonText,
            juce::String("Clip Guard: Off"));
        expectEquals(
            togglePresentation(
                ModuleType::multibandCompressor, 8, true).buttonText,
            juce::String("Auto Makeup: On"));
        expectEquals(
            togglePresentation(ModuleType::studioPhaser, 2, true).buttonText,
            juce::String("Sync: Tempo"));
        expectEquals(
            togglePresentation(
                ModuleType::diffusionDelay, 1, false).buttonText,
            juce::String("Sync: Free"));

        beginTest("Next-ten choices and readouts use musical language");
        expectEquals(
            controlOptions(ModuleType::studioPhaser, 0)[2],
            juce::String("6"));
        expectEquals(
            controlOptions(ModuleType::studioFlanger, 0)[1],
            juce::String("Through-Zero"));
        expectEquals(
            controlOptions(ModuleType::pitchBloom, 0)[3],
            juce::String("Octave + Fifth"));
        expectEquals(
            controlOptions(ModuleType::spatialOrbit, 0)[1],
            juce::String("Figure Eight"));
        expect(formatControlValue(
                   ModuleType::gateExpander, 0,
                   moduleDefaults(ModuleType::gateExpander)[0])
                   .endsWith("dB"));
        expect(formatControlValue(
                   ModuleType::studioFlanger, 5,
                   moduleDefaults(ModuleType::studioFlanger)[5])
                   .endsWith("ms"));
        expect(formatControlValue(
                   ModuleType::frequencyLab, 0,
                   moduleDefaults(ModuleType::frequencyLab)[0])
                   .endsWith("Hz"));
        expect(formatControlValue(
                   ModuleType::spatialOrbit, 6,
                   moduleDefaults(ModuleType::spatialOrbit)[6])
                   .endsWith("m"));
        expect(formatControlValue(
                   ModuleType::signalDecay, 0,
                   moduleDefaults(ModuleType::signalDecay)[0])
                   .endsWith("bits"));

        beginTest("Minimum editor leaves a usable full-panel graph");
        const auto modulePanelWidth = editorMinimumWidth - 20;
        const auto modulePanelHeight = editorMinimumHeight - 92 - 52 - 12;
        const auto graphWidth = modulePanelWidth - 28;
        const auto graphHeight = modulePanelHeight - 28 - 34 - 6;
        expect(graphWidth >= 760);
        expect(graphHeight >= 390);

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
                ModuleType::wavefoldGarden,
                ModuleType::multibandCompressor })
        {
            const auto creative = keyboardControlOrder(
                type, moduleDefaults(type), true, true);
            expectEquals(static_cast<int>(creative.size()), controlsPerSlot);
        }
        expectEquals(static_cast<int>(keyboardControlOrder(
            ModuleType::gateExpander,
            moduleDefaults(ModuleType::gateExpander), true, true).size()), 11);
        expectEquals(static_cast<int>(keyboardControlOrder(
            ModuleType::transientDesigner,
            moduleDefaults(ModuleType::transientDesigner), true, true).size()), 8);
        for (const auto type : {
                ModuleType::diffusionDelay,
                ModuleType::pitchBloom,
                ModuleType::spatialOrbit,
                ModuleType::signalDecay })
            expectEquals(static_cast<int>(keyboardControlOrder(
                type, moduleDefaults(type), true, true).size()), 11);
        for (const auto type : {
                ModuleType::studioPhaser,
                ModuleType::studioFlanger })
            expectEquals(static_cast<int>(keyboardControlOrder(
                type, moduleDefaults(type), true, true).size()), 10);
        expectEquals(static_cast<int>(keyboardControlOrder(
            ModuleType::frequencyLab,
            moduleDefaults(ModuleType::frequencyLab), true, true).size()), 10);

        beginTest("Next-ten keyboard order follows each visual workflow");
        expect(keyboardControlOrder(
                   ModuleType::gateExpander,
                   moduleDefaults(ModuleType::gateExpander), true, true)
               == std::vector<int>(
                   { 0, 1, 8, 9, 2, 3, 4, 5, 6, 7, 10 }));
        expect(keyboardControlOrder(
                   ModuleType::transientDesigner,
                   moduleDefaults(ModuleType::transientDesigner), true, true)
               == std::vector<int>({ 0, 1, 5, 2, 3, 4, 6, 7 }));
        expect(keyboardControlOrder(
                   ModuleType::multibandCompressor,
                   moduleDefaults(ModuleType::multibandCompressor), true, true)
               == std::vector<int>(
                   { 0, 1, 2, 3, 4, 8, 5, 6, 7, 9, 10, 11 }));
        expect(keyboardControlOrder(
                   ModuleType::studioPhaser,
                   moduleDefaults(ModuleType::studioPhaser), true, true)
               == std::vector<int>(
                   { 0, 5, 6, 4, 1, 2, 7, 8, 9, 10 }));
        expect(keyboardControlOrder(
                   ModuleType::studioFlanger,
                   moduleDefaults(ModuleType::studioFlanger), true, true)
               == std::vector<int>(
                   { 0, 5, 4, 1, 2, 6, 7, 8, 9, 10 }));
        expect(keyboardControlOrder(
                   ModuleType::diffusionDelay,
                   moduleDefaults(ModuleType::diffusionDelay), true, true)
               == std::vector<int>(
                   { 1, 2, 4, 3, 5, 6, 7, 8, 9, 10, 11 }));
        expect(keyboardControlOrder(
                   ModuleType::pitchBloom,
                   moduleDefaults(ModuleType::pitchBloom), true, true)
               == std::vector<int>(
                   { 0, 2, 4, 3, 5, 1, 6, 7, 8, 9, 10 }));
        expect(keyboardControlOrder(
                   ModuleType::spatialOrbit,
                   moduleDefaults(ModuleType::spatialOrbit), true, true)
               == std::vector<int>(
                   { 0, 4, 6, 5, 1, 2, 7, 8, 9, 10, 11 }));

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
        const auto gateValues = moduleDefaults(ModuleType::gateExpander);
        const auto gateWithoutSidechain = keyboardControlOrder(
            ModuleType::gateExpander, gateValues, true, false);
        const auto gateWithSidechain = keyboardControlOrder(
            ModuleType::gateExpander, gateValues, true, true);
        expect(std::find(gateWithoutSidechain.begin(),
                         gateWithoutSidechain.end(), 8)
               == gateWithoutSidechain.end());
        expect(std::find(gateWithSidechain.begin(),
                         gateWithSidechain.end(), 8)
               != gateWithSidechain.end());

        auto syncedPhaserValues = moduleDefaults(ModuleType::studioPhaser);
        syncedPhaserValues[2] = 1.0f;
        const auto syncedPhaser = keyboardControlOrder(
            ModuleType::studioPhaser, syncedPhaserValues, true, true);
        expect(std::find(syncedPhaser.begin(), syncedPhaser.end(), 1)
               == syncedPhaser.end());
        expect(std::find(syncedPhaser.begin(), syncedPhaser.end(), 3)
               != syncedPhaser.end());

        auto freeDelayValues = moduleDefaults(ModuleType::diffusionDelay);
        freeDelayValues[1] = 0.0f;
        const auto freeDiffusion = keyboardControlOrder(
            ModuleType::diffusionDelay, freeDelayValues, true, true);
        expect(std::find(freeDiffusion.begin(), freeDiffusion.end(), 0)
               != freeDiffusion.end());
        expect(std::find(freeDiffusion.begin(), freeDiffusion.end(), 2)
               == freeDiffusion.end());

        auto syncedOrbitValues = moduleDefaults(ModuleType::spatialOrbit);
        syncedOrbitValues[2] = 1.0f;
        const auto syncedOrbit = keyboardControlOrder(
            ModuleType::spatialOrbit, syncedOrbitValues, true, true);
        expect(std::find(syncedOrbit.begin(), syncedOrbit.end(), 1)
               == syncedOrbit.end());
        expect(std::find(syncedOrbit.begin(), syncedOrbit.end(), 3)
               != syncedOrbit.end());

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
