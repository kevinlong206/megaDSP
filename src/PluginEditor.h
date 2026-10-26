#pragma once

#include "PluginProcessor.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <cmath>
#include <cstdint>

namespace megadsp::ui
{
enum class WidgetSize;
}

class HorizontalRackViewport final : public juce::Viewport
{
public:
    void mouseWheelMove(
        const juce::MouseEvent& event,
        const juce::MouseWheelDetails& wheel) override
    {
        auto axisWheel = wheel;
        if (!verticalScrolling)
            axisWheel.deltaY = 0.0f;
        juce::Viewport::mouseWheelMove(event, axisWheel);
    }

    void setVerticalScrolling(bool vertical) { verticalScrolling = vertical; }

private:
    bool verticalScrolling = false;
};

class MegaDSPAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                          public juce::DragAndDropContainer,
                                          private juce::Timer
{
public:
    MegaDSPAudioProcessorEditor(MegaDSPAudioProcessor&, int width, int height);
    ~MegaDSPAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void moved() override;
    void parentHierarchyChanged() override;
    void visibilityChanged() override;
    bool keyPressed(const juce::KeyPress&) override;
#if defined(MEGADSP_TESTS)
    void rebuildLibraryForTesting(MegaDSPAudioProcessor::Library library)
    {
        rebuildLibrary(library);
    }
    juce::AlertWindow* libraryRebuildWindowForTesting() const
    {
        return libraryRebuildWindow.get();
    }
    void refreshLibraryRebuildProgressForTesting()
    {
        refreshLibraryRebuildProgress();
    }
    void clickCloseButtonForTesting(int slot);
    void simulateFocusGainDuringRefreshForTesting(int slot);
    void simulateDescendantFocusForTesting(int slot);
    void simulateCardHoverForTesting(int slot);
    void simulateCardHoverExitForTesting(int slot);
    void reorderCardForTesting(int source, int destination);
    void reorderChainNavigatorForTesting(
        int source, int target, bool dropAfter);
    void insertModuleFromChainForTesting(
        megadsp::ModuleType, int position);
    void doubleClickChainNavigatorForTesting(int slot);
    void removeModuleFromChainForTesting(int slot);
    void clickChainNavigatorForTesting(int slot);
    void refreshCardsForTesting();
    juce::Component* cardForTesting(int slot) const;
    void applyPendingCardMutationForTesting() { applyPendingCardMutation(); }
    bool cardAcceptsAssetDropForTesting(
        int slot, const juce::String& path) const;
    bool cardHasAssetSearchForTesting(int slot) const;
    int cardCountForTesting() const { return static_cast<int>(cards.size()); }
    int cardHeightForTesting(int slot) const;
    int cardWidthForTesting(int slot) const;
    int cardGraphHeightForTesting(int slot) const;
    int cardGraphWidthForTesting(int slot) const;
    int selectedCardForTesting() const { return selectedSlot; }
    int chainNavigatorButtonCountForTesting() const;
    int chainInsertButtonCountForTesting() const;
    bool selectedCardVisibleForTesting() const;
    int activeVisualizationCardCountForTesting() const;
    void setCardsEditorVisibleForTesting(bool);
    bool cardChildrenFitForTesting() const;
    bool cardGraphsAvoidControlsForTesting() const;
    bool cardPresentsAllActiveControlsForTesting() const;
    bool cardHeightsMatchPreferencesForTesting() const;
    juce::String cardControlTextForTesting(int slot, int control) const;
    bool cardRotaryControlsWorkForTesting(int slot);
    bool rackOverflowsVerticallyForTesting() const;
    bool rackOverflowsHorizontallyForTesting() const;
    int headerMeterWidthForTesting() const;
    void setWidgetSizeForTesting(megadsp::ui::WidgetSize);
    juce::String commitHashForTesting() const
    {
        return commitHashButton.getButtonText();
    }
    int tabCountForTesting() const { return cardCountForTesting(); }
#endif

private:
    class ModuleCard;
    class HeaderMeter;
    class ChainIconButton;
    class ChainSlotButton;

    void timerCallback() override;
    void selectSlot(int slot);
    void focusSlot(int slot);
    void restoreFocusedCardAfterHover();
    void refreshCards(bool followProcessorSelection = false);
    void refreshChainNavigator();
    void revealSelectedCard(bool focus);
    void addModuleAt(megadsp::ModuleType, int position);
    void showModuleBrowser(int insertPosition = -1);
    void showModuleSearch(int insertPosition = -1);
    void removeSlot(int slot);
    void reorderSlot(int source, int destination);
    void requestRemoveSlot(int slot);
    void requestReorderSlot(int source, int destination);
    void applyPendingCardMutation();
    void choosePreset(bool save);
    void commitInstanceName();
    void showThemePalette();
    void showSettingsMenu();
    void rebuildLibrary(MegaDSPAudioProcessor::Library);
    void refreshLibraryRebuildProgress();
    void chooseLibraryRoot(int library);
    void refreshIdentityPresentation();
    void refreshSnapshots();
    void showPerformancePanel();
    void showResult(const juce::Result&);
    void setWidgetSize(megadsp::ui::WidgetSize, bool persist);
    void refreshWidgetSizeButtons();
    void updateEditorResizeLimits();
#if defined(MEGADSP_CAPTURE_SCREENSHOTS)
    void advanceScreenshotCapture();
    void captureScreenshot(const juce::String&);
#endif

    MegaDSPAudioProcessor& audioProcessor;
    juce::Label title;
    juce::TextButton commitHashButton;
    juce::Label instanceLabel;
    juce::TextEditor instanceName;
    juce::TextButton themeButton { "COLOR" };
    juce::TextButton settingsButton { "SETTINGS" };
    double libraryRebuildFraction = -1.0;
    std::shared_ptr<MegaDSPAudioProcessor::LibraryRebuildProgress>
        libraryRebuildProgress;
    std::unique_ptr<juce::AlertWindow> libraryRebuildWindow;
    juce::Label status;
    juce::TextButton saveButton { "Save" };
    juce::TextButton loadButton { "Load" };
    juce::ComboBox factoryPresets;
    juce::Slider inputGain;
    juce::Slider outputGain;
    juce::Label inputLabel;
    juce::Label outputLabel;
    std::unique_ptr<HeaderMeter> headerMeter;
    juce::Label snapshotsLabel;
    juce::ToggleButton liveSafeButton { "LIVE SAFE" };
    juce::TextButton smallWidgetsButton { "S" };
    juce::TextButton ultraCompactButton { "ULTRA" };
    juce::TextButton mediumWidgetsButton { "M" };
    juce::TextButton largeWidgetsButton { "L" };
    juce::Component snapshotsHost;
    std::array<std::unique_ptr<juce::TextButton>,
               megadsp::snapshotCount> snapshotButtons;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;
    HorizontalRackViewport rackViewport;
    juce::Component chainNavigatorHost;
    juce::Label chainNavigatorLabel;
    std::array<std::unique_ptr<ChainSlotButton>,
               megadsp::slotCount> chainNavigatorButtons;
    std::array<std::unique_ptr<ChainIconButton>,
               megadsp::slotCount + 1> chainInsertButtons;
    juce::Component rackContent;
    std::vector<std::unique_ptr<ModuleCard>> cards;
    juce::TextButton addButton { "+ ADD MODULE" };
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<juce::FileChooser> settingsFileChooser;
    megadsp::ui::WidgetSize widgetSize;
    int selectedSlot = 0;
    int focusedSlot = -1;
    int knownActiveSlots = -1;
    std::uint64_t knownTopologyGeneration = 0;
    std::uint64_t knownSnapshotGeneration = 0;
    int knownThemeIndex = -1;
    juce::Rectangle<int> knownDisplayUserArea;
    bool refreshingCards = false;
    enum class PendingCardMutation
    {
        none,
        remove,
        reorder
    };
    PendingCardMutation pendingCardMutation = PendingCardMutation::none;
    int pendingSourceSlot = -1;
    int pendingDestinationSlot = -1;
    std::uint64_t pendingTopologyGeneration = 0;
    std::uint64_t pendingTopologyRequestGeneration = 0;
#if defined(MEGADSP_CAPTURE_SCREENSHOTS)
    int screenshotPhase = 0;
    int screenshotDelay = 0;
    std::unique_ptr<juce::Component> screenshotOverlay;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MegaDSPAudioProcessorEditor)
};
