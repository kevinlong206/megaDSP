#pragma once

#include "modules/ModuleRegistry.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_graphics/juce_graphics.h>

JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE(
    "-Wshadow-field-in-constructor")
#include <pluginterfaces/base/ftypes.h>
#include <pluginterfaces/base/funknown.h>
#include "presonus_sdk/ipslgainreduction.h"
JUCE_END_IGNORE_WARNINGS_GCC_LIKE

#include <array>
#include <atomic>

namespace reaper
{
using namespace Steinberg;
using INT_PTR = Steinberg::TPtrInt;

#include "reaper_sdk/reaper_plugin_fx_embed.h"
#include "reaper_sdk/reaper_vst3_interfaces.h"
} // namespace reaper

class MegaDSPAudioProcessor;

namespace megadsp
{
class ReaperEmbeddedUI final : public juce::VST3ClientExtensions,
                               public reaper::IReaperUIEmbedInterface,
                               public Presonus::IGainReductionInfo
{
public:
    explicit ReaperEmbeddedUI(MegaDSPAudioProcessor&);

    int32_t queryIEditController(
        const Steinberg::TUID, void**) override;

    Steinberg::TPtrInt embed_message(
        int message, Steinberg::TPtrInt parameter2,
        Steinberg::TPtrInt parameter3) override;

    Steinberg::uint32 PLUGIN_API addRef() override;
    Steinberg::uint32 PLUGIN_API release() override;
    Steinberg::tresult PLUGIN_API queryInterface(
        const Steinberg::TUID, void**) override;
    double PLUGIN_API getGainReductionValueInDb() override;

#if defined(MEGADSP_TESTS)
    juce::Image renderForTesting(
        int width, int height, int context, float scale = 1.0f);
    std::array<juce::Rectangle<int>, slotCount>
        tileBoundsForTesting(
            int width, int height, int context,
            float scale = 1.0f) const;
    int primaryControlForTesting(int slot) const;
    bool dragPrimaryForTesting(int slot, int verticalDistance);
#endif

private:
    struct Layout
    {
        std::array<juce::Rectangle<int>, slotCount> tiles;
        int count = 0;
        bool vertical = false;
    };

    Layout createLayout(
        juce::Rectangle<int> bounds, int context, float scale) const;
    int primaryControl(int slot) const;
    juce::Rectangle<int> bypassBounds(
        juce::Rectangle<int> tile, float scale) const;
    juce::Rectangle<int> editorOpenBounds(
        juce::Rectangle<int> tile, float scale) const;
    int tileAt(juce::Point<int>, const Layout&) const;
    void paint(
        juce::Graphics&, juce::Rectangle<int>, int context,
        float scale);
    void paintTile(
        juce::Graphics&, juce::Rectangle<int>, int slot,
        bool verticalLayout, float scale);
    Steinberg::TPtrInt paintToBitmap(
        reaper::REAPER_FXEMBED_IBitmap*,
        const reaper::REAPER_FXEMBED_DrawInfo*);
    Steinberg::TPtrInt sizeHints(
        reaper::REAPER_FXEMBED_SizeHints*) const;
    Steinberg::TPtrInt mouseMessage(
        int message, const reaper::REAPER_FXEMBED_DrawInfo*);
    void beginPrimaryDrag(int slot, int mouseY);
    void updatePrimaryDrag(int mouseY);
    void endPrimaryDrag();
    bool adjustPrimary(int slot, float normalizedDelta);
    static float scaleFor(
        const reaper::REAPER_FXEMBED_DrawInfo*,
        reaper::REAPER_FXEMBED_IBitmap* = nullptr);

    MegaDSPAudioProcessor& processor;
    std::atomic<Steinberg::uint32> referenceCount { 1 };
    int lastContext = 0;
    int lastWidth = 0;
    int lastHeight = 0;
    float lastScale = 1.0f;
    int dragSlot = -1;
    int dragControl = -1;
    int dragStartY = 0;
    float dragStartValue = 0.0f;
    juce::Image paintImage;
};
} // namespace megadsp
