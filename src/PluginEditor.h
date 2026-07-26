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
#if defined(MEGADSP_TESTS)
    void clickCloseButtonForTesting(int slot);
    void simulateFocusGainDuringRefreshForTesting(int slot);
    int tabCountForTesting() const { return static_cast<int>(tabs.size()); }
#endif

private:
    class ModuleTab;
    class ModulePanel;
    class HeaderMeter;

    void timerCallback() override;
    void selectSlot(int slot);
    void refreshTabs();
    void showModuleBrowser();
    void showModuleSearch();
    void removeSlot(int slot);
    void reorderSlot(int source, int destination);
    void requestRemoveSlot(int slot);
    void requestReorderSlot(int source, int destination);
    void applyPendingTabMutation();
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
    juce::Component tabHost;
    std::vector<std::unique_ptr<ModuleTab>> tabs;
    juce::TextButton addButton { "+" };
    std::unique_ptr<ModulePanel> modulePanel;
    std::unique_ptr<juce::FileChooser> fileChooser;
    int selectedSlot = 0;
    int knownActiveSlots = -1;
    std::uint64_t knownTopologyGeneration = 0;
    std::uint64_t knownSnapshotGeneration = 0;
    int knownThemeIndex = -1;
    bool refreshingTabs = false;
    enum class PendingTabMutation
    {
        none,
        remove,
        reorder
    };
    PendingTabMutation pendingTabMutation = PendingTabMutation::none;
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
