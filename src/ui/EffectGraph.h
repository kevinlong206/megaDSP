#pragma once

#include "../PluginProcessor.h"

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <array>
namespace megadsp::ui
{
class ReEqView;
constexpr int graphFftOrder = 10;
constexpr int graphFftSize = 1 << graphFftOrder;
constexpr int graphLevelHistorySize = 500;

class EffectGraph final : public juce::Component,
                          private juce::Timer
{
public:
    EffectGraph(MegaDSPAudioProcessor&, int slot, ModuleType);
    ~EffectGraph() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&,
                        const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void focusGained(FocusChangeType) override;
    void focusLost(FocusChangeType) override;

    void setVisualizationActive(bool);
    bool isVisualizationActive() const { return visualizationActive; }
    static size_t spectrumBinForProportion(
        float proportion, double sampleRate);
#if defined(MEGADSP_TESTS)
    void refreshVisualizationForTesting();
    void setCompactCompressorThresholdFromYForTesting(float y);
    void setCompactEqualizerBandFromPointForTesting(
        int band, juce::Point<float>);
    void setCompactEqualizerOutputFromYForTesting(float);
    float compactDelayIntervalForTesting() const
    {
        return compactDelayPreview().interval;
    }
    float compactDelayFirstRepeatPositionForTesting() const
    {
        const auto preview = compactDelayPreview();
        return preview.interval / preview.window;
    }
    int compactDelayRepeatCountForTesting() const
    {
        return compactDelayPreview().repeats;
    }
    std::array<float, 2> compactDelayWetLevelsForTesting()
    {
        return compactDelayWetLevels();
    }
    juce::Rectangle<float> compactEnvelopePlotAreaForTesting() const
    {
        return compactEnvelopePlotArea();
    }
    juce::Point<float> compactEnvelopeNodeForTesting(int node) const
    {
        return compactEnvelopeNode(node);
    }
    void setCompactEnvelopeNodeForTesting(int node, juce::Point<float> point)
    {
        setCompactEnvelopeNode(node, point);
    }
    void reverseCompactEnvelopeSpeedsForTesting()
    {
        reverseCompactEnvelopeSpeeds();
    }
    juce::Rectangle<float> compactEqualizerPlotAreaForTesting() const
    {
        return compactEqualizerPlotArea();
    }
    float compactEqualizerResponseDbForTesting(float frequency) const
    {
        return compactEqualizerResponseDb(frequency);
    }
    int compactNamLibraryModelCountForTesting() const
    {
        return static_cast<int>(compactNamModels.size());
    }
    int compactImpulseResponseCountForTesting() const
    {
        return static_cast<int>(compactImpulseResponses.size());
    }
    int compactNamFirstVisibleModelForTesting() const
    {
        return compactAssetFirstVisible;
    }
    void activateCompactNamModelForTesting(int index);
    void scrollCompactNamLibraryForTesting(int rows);
    void setCompactAssetFilterForTesting(const juce::String&);
    void setCompactNamFavoritesOnlyForTesting(bool);
    void setCompactNamNewestSortForTesting(bool);
    juce::String compactNamModelPathForTesting(int index) const;
    juce::String compactNamModelNameForTesting(int index) const
    {
        return juce::isPositiveAndBelow(
                   index, static_cast<int>(compactNamModels.size()))
            ? compactAssetDisplayName(index) : juce::String {};
    }
    void toggleCompactNamFavoriteForTesting(int index);
    void setCompactImpulseFavoritesOnlyForTesting(bool);
    void toggleCompactImpulseFavoriteForTesting(int index);
    void activateCompactImpulseResponseForTesting(int index);
#endif

    float value(int control) const;
    juce::RangedAudioParameter* parameter(int control) const;
    void setValue(int control, float normalized);
    void beginGestures();
    void endGestures();
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
private:
    void paintCompact(juce::Graphics&, juce::Rectangle<float>);
    void paintCompactAssetLibrary(
        juce::Graphics&, juce::Rectangle<float>);
    void refreshCompactAssetLibrary(bool resetScroll = false);
    void activateCompactAsset(int index);
    void toggleCompactAssetFavorite(int index);
    void deleteCompactNamModel(int index);
    int compactAssetCount() const;
    int compactAssetVisibleRowCount() const;
    int compactAssetAt(juce::Point<float>) const;
    void scrollCompactAssetLibrary(int rows);
    bool isCompactAssetLibrary() const;
    bool isCompactNamLibrary() const;
    juce::Rectangle<float> compactAssetLibraryArea() const;
    juce::String compactAssetLoadedPath() const;
    juce::String compactAssetDisplayName(int index) const;
    juce::Rectangle<float> compactCompressorLevelArea() const;
    void setCompactCompressorThresholdFromY(float);
    juce::Rectangle<float> compactEqualizerPlotArea() const;
    juce::Rectangle<float> compactEqualizerModeArea() const;
    juce::Rectangle<float> compactEqualizerOutputArea() const;
    juce::Point<float> compactEqualizerPoint(int band) const;
    juce::dsp::IIR::Coefficients<float>::Ptr
        compactEqualizerCoefficients(int band) const;
    float compactEqualizerResponseDb(
        float frequency, int onlyBand = -1) const;
    juce::String compactEqualizerBandReadout(int band) const;
    int compactEqualizerBandAt(juce::Point<float>) const;
    void setCompactEqualizerBandFromPoint(
        int band, juce::Point<float>);
    void setCompactEqualizerOutputFromY(float);
    struct DelayPreview
    {
        float interval;
        float window;
        int repeats;
    };
    DelayPreview compactDelayPreview() const;
    std::array<float, 2> compactDelayWetLevels();
    juce::Rectangle<float> compactDelayTimelineArea() const;
    void paintCompactEnvelopeMapping(juce::Graphics&);
    juce::Rectangle<float> compactEnvelopePlotArea() const;
    juce::Point<float> compactEnvelopeNode(int node) const;
    void setCompactEnvelopeNode(int node, juce::Point<float>);
    void reverseCompactEnvelopeSpeeds();
    void timerCallback() override;
    void refreshVisualization();
    void calculateSpectrum(
        const std::array<float, graphFftSize>&,
        std::array<float, graphFftSize / 2>&);
    std::unique_ptr<juce::AccessibilityHandler>
        createAccessibilityHandler() override;

    MegaDSPAudioProcessor& processor;
    int slot;
    ModuleType type;
    std::uint64_t delayTelemetrySequence = 0;
    double delayTelemetryTime = 0.0;
    bool delayTelemetryAvailable = false;
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
    int compactEqualizerBand = -1;
    int compactEnvelopeSelection = 0;
    uint64_t lastInputHistoryPosition = 0;
    uint64_t lastOutputHistoryPosition = 0;
    juce::String dragReadout;
    juce::TextEditor compactAssetSearch;
    juce::TextButton compactNamFavoriteFilter;
    juce::TextButton compactNamNewestSort;
    std::vector<NamModelLibraryEntry> compactNamModels;
    std::vector<ImpulseResponseLibraryEntry> compactImpulseResponses;
    std::uint64_t compactAssetLibraryGeneration = 0;
    int compactAssetFirstVisible = 0;
    int compactAssetSelected = -1;
    bool compactAssetLoadPending = false;
    bool compactEnvelopeKeyboard = true;
    bool bypassVisualsSuspended = false;
    bool visualizationActive = false;
    std::unique_ptr<ReEqView> reEqView;
    juce::TextButton upgradeEqualizerButton { "UPGRADE TO ReEQ2" };
};
} // namespace megadsp::ui
