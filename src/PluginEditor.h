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

private:
    class ModuleTab;
    class ModulePanel;
    class HeaderMeter;

    void timerCallback() override;
    void selectSlot(int slot);
    void refreshTabs();
    void showModuleBrowser();
    void removeSlot(int slot);
    void reorderSlot(int source, int destination);
    void choosePreset(bool save);
    void showResult(const juce::Result&);

    MegaDSPAudioProcessor& audioProcessor;
    juce::Label title;
    juce::Label status;
    juce::TextButton saveButton { "Save" };
    juce::TextButton loadButton { "Load" };
    juce::ComboBox factoryPresets;
    juce::ComboBox backgroundTheme;
    juce::Slider inputGain;
    juce::Slider outputGain;
    juce::Label inputLabel;
    juce::Label outputLabel;
    std::unique_ptr<HeaderMeter> headerMeter;
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MegaDSPAudioProcessorEditor)
};
