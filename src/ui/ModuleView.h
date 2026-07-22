#pragma once

#include "EffectGraph.h"

#include <array>
#include <vector>

namespace megadsp::ui
{
class ModuleView
{
public:
    explicit ModuleView(EffectGraph&);
    virtual ~ModuleView() = default;

    virtual void configureAccessibility(juce::Component&) const {}
    virtual bool usesFullPanel() const { return false; }
    virtual void paint(juce::Graphics&, juce::Rectangle<float>) = 0;
    virtual void mouseDown(const juce::MouseEvent&) {}
    virtual void mouseDrag(const juce::MouseEvent&) {}
    virtual void mouseDoubleClick(const juce::MouseEvent&) {}
    virtual void mouseWheelMove(const juce::MouseEvent&,
                                const juce::MouseWheelDetails&) {}
    virtual void timerCallback() {}
    virtual bool isInterestedInFileDrag(const juce::StringArray&) const
    {
        return false;
    }
    virtual void filesDropped(const juce::StringArray&, int, int) {}

    virtual std::vector<int> keyboardControls() const;
    virtual ControlKind keyboardKind(int control) const;
    virtual int keyboardOptionCount(int control) const;
    virtual juce::String keyboardLabel(int control) const;
    virtual juce::String keyboardValueText(int control) const;
    bool adjustKeyboardControl(int control, int direction, bool fine);
    bool pressKeyboardControl(int control);
    bool resetKeyboardControl(int control);
    double keyboardAccessibilityValue(int control) const;
    double keyboardAccessibilityMaximum(int control) const;
    double keyboardAccessibilityInterval(int control) const;
    bool setKeyboardAccessibilityValue(int control, double value);
    bool setKeyboardAccessibilityValueAsString(
        int control, const juce::String&);

protected:
    static constexpr int fftSize = graphFftSize;

    float value(int control) const;
    juce::RangedAudioParameter* parameter(int control) const;
    void setValue(int control, float normalized);
    void beginGestures();
    void repaint();
    juce::Rectangle<int> getLocalBounds() const;
    void toggleParameter(int control);
    void cycleChoice(int control, int optionCount);
    void resetToDefault(int control);
    void updateDefaultDragReadout();
    bool readContinuousTelemetry(
        ContinuousTelemetrySnapshot&) const noexcept;
    bool readEventTelemetry(EventTelemetrySnapshot&) const noexcept;

    static float dbToY(float db, juce::Rectangle<float>);
    void drawSpectrum(juce::Graphics&, juce::Rectangle<float>,
                      const std::array<float, graphFftSize / 2>&,
                      juce::Colour);
    void drawWaveforms(juce::Graphics&, juce::Rectangle<float>);
    static void drawLevelHistory(
        juce::Graphics&, juce::Rectangle<float>,
        const std::array<float, graphLevelHistorySize>&, juce::Colour);
    void drawGainReductionOverlay(juce::Graphics&, juce::Rectangle<float>);

    EffectGraph& graph;
    MegaDSPAudioProcessor& processor;
    const int slot;
    const ModuleType type;
    std::array<float, graphFftSize>& inputSamples;
    std::array<float, graphFftSize>& outputSamples;
    std::array<float, graphFftSize>& stereoLeftSamples;
    std::array<float, graphFftSize>& stereoRightSamples;
    std::array<float, 256>& gainHistory;
    std::array<float, graphLevelHistorySize>& inputLevels;
    std::array<float, graphLevelHistorySize>& outputLevels;
    std::array<float, graphLevelHistorySize>& gainReductionLevels;
    std::array<float, graphFftSize / 2>& inputSpectrum;
    std::array<float, graphFftSize / 2>& outputSpectrum;
    int& dragPrimary;
    int& dragSecondary;
    juce::String& dragReadout;

private:
    bool isKeyboardControlAvailable(int control) const;
    bool setKeyboardNormalizedValue(int control, float normalized);
};
} // namespace megadsp::ui
