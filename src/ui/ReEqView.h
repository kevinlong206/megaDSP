// Native ReEQ2 UI: (c) 2021-2026 Justin Johnson; (c) 2025-2026 Yuriy Ivantsov.
// MIT; see src/third_party/reeq2/LICENSE and THIRD_PARTY_NOTICES.md.
// ReJJ reference revision 281bf08f9d0b5e25a0c7db05d37b7a2f3afa6b41.
#pragma once

#include "../modules/ReEqSettings.h"
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <bitset>
#include <functional>

class MegaDSPAudioProcessor;

namespace megadsp::ui
{
/** Compact native ReEQ2 editor. The owner drives refresh; there is no UI timer. */
class ReEqView final : public juce::Component
{
  public:
    ReEqView(MegaDSPAudioProcessor&, int slot);
    ~ReEqView() override;

    void setVisualizationActive(bool);
    void refresh();
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void modifierKeysChanged(const juce::ModifierKeys&) override;
    void focusLost(FocusChangeType) override;
    void visibilityChanged() override;

#if defined(MEGADSP_TESTS)
    juce::Rectangle<float> plotAreaForTesting() const { return plot; }
    juce::Point<float> bandPointForTesting(int band) const { return bandPoint(band); }
    int selectedBandForTesting() const { return selectedBand; }
    void duplicateSelectionForTesting(bool split) { duplicateSelection(split); }
#endif

  private:
    class NumericField;
    struct Analyzer;
    using Mutation = std::function<void(ReEqSettings&)>;
    void apply(const juce::String&, const Mutation&);
    void applySelected(const juce::String&, const std::function<void(ReEqBand&)>&);
    void publish();
    void beginGesture(const juce::String&);
    void endGesture();
    void syncControls();
    void initializeChoices();
    void configureFields();
    void rebuildResponse();
    void updateAnalyzer();
    void paintAnalyzer(juce::Graphics&);
    void paintPiano(juce::Graphics&);
    void paintNodesAndLabels(juce::Graphics&);
    void select(int band, bool additive);
    void createBand(juce::Point<float>);
    void deleteSelection();
    void duplicateSelection(bool split);
    void showBandMenu();
    void showAnalyzerMenu();
    void showSettingsMenu();
    void showMenu(juce::PopupMenu, std::function<void(int)>);
    void setListen(int band, bool detector);
    void stopListen();
    void updateListen(juce::Point<float>, juce::ModifierKeys);
    float retainedOutputDb() const;
    float xForFrequency(float) const;
    float frequencyForX(float) const;
    float yForGain(float) const;
    float gainForY(float) const;
    juce::Point<float> bandPoint(int) const;
    int bandAt(juce::Point<float>) const;
    int detectorAt(juce::Point<float>) const;
    bool hasSelection() const;
    static bool isCut(ReEqShape);
    static juce::Colour bandColour(int);

    MegaDSPAudioProcessor& processor;
    const int slot;
    ReEqSettings settings, dragStart, responseSettings;
    std::bitset<reEqBandCount> selected, selectionBeforeBox;
    int selectedBand = -1;
    int hoverBand = -1;
    int panelPage = 0;
    int dragBand = -1;
    enum class Drag
    {
        none,
        nodes,
        box,
        detector,
        range
    };
    Drag drag = Drag::none;
    bool gesture = false, syncing = false, active = false;
    bool momentaryListen = false, responseDirty = true;
    juce::Point<float> dragOrigin, lastMouse;
    juce::Rectangle<float> plot, selectionBox;
    double responseSampleRate = 0.0;
    std::array<juce::Path, reEqBandCount> bandCurves;
    std::array<juce::Path, 5> groupCurves;
    std::array<bool, 5> groupPresent{};
    std::unique_ptr<Analyzer> analyzer;
    std::array<std::unique_ptr<NumericField>, 3> globalFields;
    std::array<std::unique_ptr<NumericField>, 6> bandFields;
    juce::TextButton autoGain{"AG"}, quality{"HQ"}, limit{"Limit"};
    juce::TextButton analyzerButton{"Analyzer"}, settingsButton{"Options"};
    juce::TextButton actions{"Band..."}, dynamicButton{"DYN"};
    juce::ComboBox bandChoice, shapeChoice, groupChoice, slopeChoice, pageChoice;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReEqView)
};
} // namespace megadsp::ui
