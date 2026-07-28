#pragma once

#include "PluginProcessor.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <cstdint>

class MegaDSPAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                          public juce::DragAndDropContainer,
                                          private juce::Timer
{
public:
    MegaDSPAudioProcessorEditor(MegaDSPAudioProcessor&, int width, int height);
    ~MegaDSPAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
#if defined(MEGADSP_TESTS)
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
    bool cardAcceptsAssetDropForTesting(
        int slot, const juce::String& path) const;
    int cardCountForTesting() const { return static_cast<int>(cards.size()); }
    int selectedCardForTesting() const { return selectedSlot; }
    int chainNavigatorButtonCountForTesting() const;
    int chainInsertButtonCountForTesting() const;
    bool selectedCardVisibleForTesting() const;
    int activeVisualizationCardCountForTesting() const;
    bool cardChildrenFitForTesting() const;
    bool cardHeightsAreUniformForTesting() const;
    bool rackOverflowsVerticallyForTesting() const;
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
    void showDiagnosticsMenu();
    void refreshIdentityPresentation();
    void refreshSnapshots();
    void showResult(const juce::Result&);
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
    juce::TextButton diagnosticsButton { "DIAG" };
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
    juce::Component snapshotsHost;
    std::array<std::unique_ptr<juce::TextButton>,
               megadsp::snapshotCount> snapshotButtons;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;
    juce::Viewport rackViewport;
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
    int selectedSlot = 0;
    int focusedSlot = -1;
    int knownActiveSlots = -1;
    std::uint64_t knownTopologyGeneration = 0;
    std::uint64_t knownSnapshotGeneration = 0;
    int knownThemeIndex = -1;
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
