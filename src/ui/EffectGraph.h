#pragma once

#include "../PluginProcessor.h"

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <array>
#include <memory>

namespace megadsp::ui
{
class ModuleView;

constexpr int graphFftOrder = 10;
constexpr int graphFftSize = 1 << graphFftOrder;
constexpr int graphLevelHistorySize = 500;

class EffectGraph final : public juce::Component,
                          public juce::FileDragAndDropTarget,
                          private juce::Timer
{
public:
    EffectGraph(MegaDSPAudioProcessor&, int slot, ModuleType);
    ~EffectGraph() override;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&,
                        const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void focusGained(FocusChangeType) override;
    void focusLost(FocusChangeType) override;

    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int x, int y) override;
    bool usesFullPanel() const;

    float value(int control) const;
    juce::RangedAudioParameter* parameter(int control) const;
    void setValue(int control, float normalized);
    void beginGestures();
    void endGestures();
    void focusKeyboardControl(int control);

    MegaDSPAudioProcessor& moduleProcessor() { return processor; }
    int moduleSlot() const { return slot; }
    ModuleType moduleType() const { return type; }
    std::array<float, graphFftSize>& inputSampleData() { return inputSamples; }
    std::array<float, graphFftSize>& outputSampleData() { return outputSamples; }
    std::array<float, graphFftSize>& stereoLeftSampleData()
    {
        return stereoLeftSamples;
    }
    std::array<float, graphFftSize>& stereoRightSampleData()
    {
        return stereoRightSamples;
    }
    std::array<float, 256>& gainHistoryData() { return gainHistory; }
    std::array<float, graphLevelHistorySize>& inputLevelData()
    {
        return inputLevels;
    }
    std::array<float, graphLevelHistorySize>& outputLevelData()
    {
        return outputLevels;
    }
    std::array<float, graphLevelHistorySize>& gainReductionLevelData()
    {
        return gainReductionLevels;
    }
    std::array<float, graphFftSize / 2>& inputSpectrumData()
    {
        return inputSpectrum;
    }
    std::array<float, graphFftSize / 2>& outputSpectrumData()
    {
        return outputSpectrum;
    }
    int& primaryDragControl() { return dragPrimary; }
    int& secondaryDragControl() { return dragSecondary; }
    juce::String& dragValueReadout() { return dragReadout; }

private:
    class GraphAccessibilityHandler;

    void timerCallback() override;
    void calculateSpectrum(
        const std::array<float, graphFftSize>&,
        std::array<float, graphFftSize / 2>&);
    static void drawGrid(juce::Graphics&, juce::Rectangle<float>);
    std::unique_ptr<juce::AccessibilityHandler>
        createAccessibilityHandler() override;
    void refreshFocusedControl(bool notifyAccessibility);
    bool moveFocusedControl(int direction);
    bool adjustFocusedControl(int direction, bool fine);
    bool activateFocusedControl();
    bool resetFocusedControl();
    bool setFocusedAccessibilityValue(double);
    bool setFocusedAccessibilityValueAsString(const juce::String&);
    juce::String focusedControlLabel() const;
    juce::String focusedControlValueText() const;
    juce::String focusedControlReadout() const;
    void notifyFocusedControlChanged(bool titleChanged);

    MegaDSPAudioProcessor& processor;
    int slot;
    ModuleType type;
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    std::array<float, graphFftSize> inputSamples {};
    std::array<float, graphFftSize> outputSamples {};
    std::array<float, graphFftSize> stereoLeftSamples {};
    std::array<float, graphFftSize> stereoRightSamples {};
    std::array<float, 256> gainHistory {};
    std::array<float, graphLevelHistorySize> inputLevels {};
    std::array<float, graphLevelHistorySize> outputLevels {};
    std::array<float, graphLevelHistorySize> gainReductionLevels {};
    std::array<float, graphFftSize * 2> fftBuffer {};
    std::array<float, graphFftSize / 2> inputSpectrum {};
    std::array<float, graphFftSize / 2> outputSpectrum {};
    int dragPrimary = -1;
    int dragSecondary = -1;
    int focusedControl = -1;
    juce::String dragReadout;
    std::unique_ptr<ModuleView> moduleView;
};
} // namespace megadsp::ui
