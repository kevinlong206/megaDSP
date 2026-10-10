#include "EffectGraph.h"

#include "GraphStyle.h"
#include "../modules/AnalogTape.h"
#include "../modules/AutoWah.h"
#include "../modules/ChaosField.h"
#include "../modules/ConvolutionReverb.h"
#include "../modules/MidnightCurrent.h"
#include "../modules/ResonantSculpture.h"
#include "../modules/StageVocal.h"
#include "../modules/VinylSimulator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace megadsp::ui
{
namespace
{
constexpr auto compactAssetHeaderHeight = 14.0f;
constexpr auto compactAssetSearchHeight = 21.0f;
constexpr auto compactAssetSearchGap = 3.0f;
constexpr auto compactAssetRowHeight = 26.0f;

juce::Path starPath(juce::Rectangle<float> bounds)
{
    juce::Path path;
    const auto centre = bounds.getCentre();
    const auto outerRadius =
        0.32f * juce::jmin(bounds.getWidth(), bounds.getHeight());
    const auto innerRadius = outerRadius * 0.43f;
    for (int point = 0; point < 10; ++point)
    {
        const auto angle =
            -juce::MathConstants<float>::halfPi
            + static_cast<float>(point)
                * juce::MathConstants<float>::pi / 5.0f;
        const auto radius = point % 2 == 0
            ? outerRadius : innerRadius;
        const juce::Point<float> position {
            centre.x + radius * std::cos(angle),
            centre.y + radius * std::sin(angle)
        };
        if (point == 0)
            path.startNewSubPath(position);
        else
            path.lineTo(position);
    }
    path.closeSubPath();
    return path;
}
}

EffectGraph::EffectGraph(
    MegaDSPAudioProcessor& processorToUse, int slotToUse,
    ModuleType typeToUse)
    : processor(processorToUse), slot(slotToUse), type(typeToUse),
      fft(graphFftOrder),
      window(graphFftSize, juce::dsp::WindowingFunction<float>::hann)
{
    setOpaque(true);
    const auto compactCompressor = type == ModuleType::compressor;
    const auto compactEqualizer = type == ModuleType::equalizer;
    const auto compactAssets = isCompactAssetLibrary();
    setWantsKeyboardFocus(
        compactCompressor || compactEqualizer || compactAssets);
    if (compactCompressor)
    {
        setTitle("Compressor threshold");
        setDescription(
            "Drag vertically to set the compression threshold.");
        setHelpText(
            "Drag the threshold line vertically. Up and Down adjust it; "
            "hold Shift for fine adjustment. Home or double-click resets.");
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    }
    if (compactEqualizer)
    {
        compactEqualizerBand = 1;
        setTitle("Equalizer graph");
        setDescription(
            "Drag the low, mid, and high nodes to set frequency and gain.");
        setHelpText(
            "Drag a node horizontally for frequency and vertically for "
            "gain. The mouse wheel adjusts Q. Double-click resets a band.");
    }
    if (compactAssets)
    {
        setTitle(
            isCompactNamLibrary()
                ? "NAM model library" : "Impulse response library");
        setDescription(
            "Searchable, scrollable library. Click an item to load it.");
        setHelpText(
            "Type to filter the indexed library, scroll to browse it, "
            "click an item to prepare and load it, or click FAV to "
            "favorite a NAM model.");
        compactAssetSearch.setMultiLine(false);
        compactAssetSearch.setReturnKeyStartsNewLine(false);
        compactAssetSearch.setTextToShowWhenEmpty(
            isCompactNamLibrary()
                ? "Filter NAM models..."
                : "Filter impulse responses...",
            juce::Colours::white.withAlpha(0.38f));
        compactAssetSearch.setColour(
            juce::TextEditor::backgroundColourId,
            juce::Colours::black.withAlpha(0.24f));
        compactAssetSearch.setColour(
            juce::TextEditor::outlineColourId,
            juce::Colours::white.withAlpha(0.12f));
        compactAssetSearch.setColour(
            juce::TextEditor::focusedOutlineColourId,
            modulePaletteColour(type).withAlpha(0.8f));
        compactAssetSearch.setColour(
            juce::TextEditor::textColourId,
            juce::Colours::white.withAlpha(0.92f));
        compactAssetSearch.setFont(juce::FontOptions(10.0f));
        compactAssetSearch.onTextChange =
            [this] { refreshCompactAssetLibrary(true); };
        compactAssetSearch.onReturnKey =
            [this]
            {
                if (compactAssetSelected < 0 && compactAssetCount() > 0)
                    compactAssetSelected = 0;
                activateCompactAsset(compactAssetSelected);
            };
        addAndMakeVisible(compactAssetSearch);
        compactNamFavoriteFilter.setButtonText("FAVORITES");
        compactNamFavoriteFilter.setClickingTogglesState(true);
        compactNamFavoriteFilter.setTooltip(
            isCompactNamLibrary()
                ? "Show only favorited NAM models."
                : "Show only favorited impulse responses.");
        compactNamFavoriteFilter.onClick =
            [this] { refreshCompactAssetLibrary(true); };
        addAndMakeVisible(compactNamFavoriteFilter);
    }
    inputSpectrum.fill(-100.0f);
    outputSpectrum.fill(-100.0f);
    inputLevels.fill(-60.0f);
    outputLevels.fill(-60.0f);
    visualizationActive =
        processor.getVisualizationData().isSlotActive(slot);
    if (compactAssets)
        refreshCompactAssetLibrary();
    startTimerHz(30);
}

EffectGraph::~EffectGraph()
{
    compactAssetSearch.onTextChange = nullptr;
    compactAssetSearch.onReturnKey = nullptr;
    compactNamFavoriteFilter.onClick = nullptr;
    endGestures();
}

void EffectGraph::resized()
{
    if (isCompactAssetLibrary())
    {
        auto area = getLocalBounds().toFloat().reduced(8.0f, 3.0f);
        area.removeFromTop(compactAssetHeaderHeight);
        auto searchRow = area.removeFromTop(compactAssetSearchHeight);
        compactNamFavoriteFilter.setBounds(
            searchRow.removeFromRight(72.0f).toNearestInt());
        compactAssetSearch.setBounds(
            searchRow.toNearestInt());
    }
    repaint();
}

bool EffectGraph::isCompactNamLibrary() const
{
    return type == ModuleType::neuralAmpModeler;
}

bool EffectGraph::isCompactAssetLibrary() const
{
    return isCompactNamLibrary()
        || type == ModuleType::convolutionReverb
        || type == ModuleType::guitarCabinet;
}

int EffectGraph::compactAssetCount() const
{
    return isCompactNamLibrary()
        ? static_cast<int>(compactNamModels.size())
        : static_cast<int>(compactImpulseResponses.size());
}

int EffectGraph::compactAssetVisibleRowCount() const
{
    const auto height =
        getLocalBounds().toFloat().reduced(8.0f, 3.0f).getHeight()
        - compactAssetHeaderHeight - compactAssetSearchHeight
        - compactAssetSearchGap;
    return juce::jmax(
        1, static_cast<int>(height / compactAssetRowHeight));
}

juce::String EffectGraph::compactAssetLoadedPath() const
{
    return isCompactNamLibrary()
        ? processor.namModelPath(slot)
        : processor.impulseResponsePath(slot);
}

void EffectGraph::refreshCompactAssetLibrary(bool resetScroll)
{
    compactAssetLibraryGeneration = processor.assetLibraryGeneration();
    const auto query = compactAssetSearch.getText();
    if (isCompactNamLibrary())
    {
        compactNamModels = processor.searchNamModels(
            query, std::numeric_limits<int>::max());
        if (compactNamFavoriteFilter.getToggleState())
            std::erase_if(
                compactNamModels,
                [](const auto& entry) { return !entry.favorite; });
        compactImpulseResponses.clear();
    }
    else
    {
        compactImpulseResponses = processor.searchImpulseResponses(
            query, std::numeric_limits<int>::max());
        if (compactNamFavoriteFilter.getToggleState())
            std::erase_if(
                compactImpulseResponses,
                [](const auto& entry) { return !entry.favorite; });
        compactNamModels.clear();
    }

    if (resetScroll)
    {
        compactAssetFirstVisible = 0;
        compactAssetSelected = compactAssetCount() > 0 ? 0 : -1;
        repaint();
        return;
    }

    const auto loadedPath = compactAssetLoadedPath();
    const auto entryPath =
        [this](int index)
        {
            return isCompactNamLibrary()
                ? compactNamModels[static_cast<size_t>(index)]
                      .file.getFullPathName()
                : compactImpulseResponses[static_cast<size_t>(index)]
                      .file.getFullPathName();
        };
    auto loadedIndex = -1;
    for (int index = 0; index < compactAssetCount(); ++index)
        if (entryPath(index) == loadedPath)
        {
            loadedIndex = index;
            break;
        }
    if (loadedIndex >= 0)
    {
        compactAssetSelected = loadedIndex;
        const auto visibleRows = compactAssetVisibleRowCount();
        if (compactAssetSelected < compactAssetFirstVisible)
            compactAssetFirstVisible = compactAssetSelected;
        else if (compactAssetSelected
                 >= compactAssetFirstVisible + visibleRows)
            compactAssetFirstVisible =
                compactAssetSelected - visibleRows + 1;
    }
    else if (compactAssetCount() == 0)
        compactAssetSelected = -1;

    const auto maximumFirst = juce::jmax(
        0, compactAssetCount() - compactAssetVisibleRowCount());
    compactAssetFirstVisible = juce::jlimit(
        0, maximumFirst, compactAssetFirstVisible);
}

void EffectGraph::activateCompactAsset(int index)
{
    if (compactAssetLoadPending
        || !juce::isPositiveAndBelow(index, compactAssetCount()))
        return;

    compactAssetSelected = index;
    compactAssetLoadPending = true;
    const juce::Component::SafePointer<EffectGraph> safeThis(this);
    const auto loadFinished =
        [safeThis](const juce::Result& result)
        {
            if (safeThis == nullptr)
                return;
            safeThis->compactAssetLoadPending = false;
            if (result.failed())
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    safeThis->isCompactNamLibrary()
                        ? "Could not load NAM model"
                        : "Could not load impulse response",
                    result.getErrorMessage());
            safeThis->repaint();
        };
    const auto result = isCompactNamLibrary()
        ? processor.loadNamModelAsync(
              slot,
              compactNamModels[static_cast<size_t>(index)].file,
              loadFinished)
        : processor.loadImpulseResponseAsync(
              slot, 0,
              compactImpulseResponses[static_cast<size_t>(index)].file,
              loadFinished);
    if (result.failed())
    {
        compactAssetLoadPending = false;
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            isCompactNamLibrary()
                ? "Could not load NAM model"
                : "Could not load impulse response",
            result.getErrorMessage());
    }
    repaint();
}

void EffectGraph::toggleCompactAssetFavorite(int index)
{
    if (!juce::isPositiveAndBelow(index, compactAssetCount()))
        return;
    const auto result = isCompactNamLibrary()
        ? processor.setNamModelFavorite(
              compactNamModels[static_cast<size_t>(index)].file,
              !compactNamModels[static_cast<size_t>(index)].favorite)
        : processor.setImpulseResponseFavorite(
              compactImpulseResponses[static_cast<size_t>(index)].file,
              !compactImpulseResponses[static_cast<size_t>(index)].favorite);
    if (result.failed())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            isCompactNamLibrary()
                ? "Could not update NAM favorite"
                : "Could not update impulse-response favorite",
            result.getErrorMessage());
        return;
    }
    refreshCompactAssetLibrary();
    repaint();
}

void EffectGraph::deleteCompactNamModel(int index)
{
    if (!isCompactNamLibrary()
        || !juce::isPositiveAndBelow(
            index, static_cast<int>(compactNamModels.size())))
        return;
    const auto file =
        compactNamModels[static_cast<size_t>(index)].file;
    const juce::Component::SafePointer<EffectGraph> safeThis(this);
    juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::WarningIcon,
        "Delete NAM model?",
        "Permanently delete " + file.getFileName()
            + " from disk? This cannot be undone.",
        "DELETE", "CANCEL", nullptr,
        juce::ModalCallbackFunction::create(
            [safeThis, file](int result)
            {
                if (safeThis == nullptr || result == 0)
                    return;
                const auto deleted =
                    safeThis->processor.deleteNamModel(file);
                if (deleted.failed())
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::WarningIcon,
                        "Could not delete NAM model",
                        deleted.getErrorMessage());
                safeThis->refreshCompactAssetLibrary();
                safeThis->repaint();
            }));
}

void EffectGraph::scrollCompactAssetLibrary(int rows)
{
    const auto maximumFirst = juce::jmax(
        0, compactAssetCount() - compactAssetVisibleRowCount());
    compactAssetFirstVisible = juce::jlimit(
        0, maximumFirst, compactAssetFirstVisible + rows);
    repaint();
}

int EffectGraph::compactAssetAt(juce::Point<float> position) const
{
    auto area = getLocalBounds().toFloat().reduced(8.0f, 3.0f);
    area.removeFromTop(
        compactAssetHeaderHeight + compactAssetSearchHeight
        + compactAssetSearchGap);
    if (!area.contains(position))
        return -1;
    const auto row = static_cast<int>(
        (position.y - area.getY()) / compactAssetRowHeight);
    if (row < 0 || row >= compactAssetVisibleRowCount())
        return -1;
    const auto index = compactAssetFirstVisible + row;
    return juce::isPositiveAndBelow(index, compactAssetCount())
        ? index : -1;
}

void EffectGraph::paintCompactAssetLibrary(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto header = area.removeFromTop(compactAssetHeaderHeight);
    area.removeFromTop(
        compactAssetSearchHeight + compactAssetSearchGap);
    graphics.setColour(juce::Colours::white.withAlpha(0.56f));
    graphics.setFont(juce::FontOptions(8.5f, juce::Font::bold));
    const auto indexing = isCompactNamLibrary()
        ? processor.isNamModelLibraryIndexing()
        : processor.isImpulseResponseLibraryIndexing();
    const auto status = indexing
        ? "INDEXING..."
        : juce::String(compactAssetCount())
            + (compactNamFavoriteFilter.getToggleState()
                   ? " FAVORITES"
                   : isCompactNamLibrary() ? " MODELS" : " IRS");
    graphics.drawText(
        isCompactNamLibrary() ? "NAM LIBRARY" : "IR LIBRARY",
        header.toNearestInt(),
        juce::Justification::centredLeft);
    graphics.drawText(
        status, header.toNearestInt(),
        juce::Justification::centredRight);

    if (compactAssetCount() == 0)
    {
        graphics.setColour(juce::Colours::white.withAlpha(0.52f));
        graphics.setFont(juce::FontOptions(10.0f));
        const auto hasRoot = isCompactNamLibrary()
            ? processor.namModelRootDirectory().isDirectory()
            : processor.impulseResponseRootDirectory().isDirectory();
        graphics.drawFittedText(
            compactAssetSearch.getText().isNotEmpty()
                ? "No library items match this filter."
                : hasRoot
                    ? "Indexing the asset library..."
                    : "Choose a library root in SETTINGS.",
            area.reduced(8.0f).toNearestInt(),
            juce::Justification::centred, 2, 0.8f);
        return;
    }

    const auto loadedPath = compactAssetLoadedPath();
    const auto visibleRows = compactAssetVisibleRowCount();
    for (int row = 0; row < visibleRows; ++row)
    {
        const auto index = compactAssetFirstVisible + row;
        if (!juce::isPositiveAndBelow(index, compactAssetCount()))
            break;
        const auto& file = isCompactNamLibrary()
            ? compactNamModels[static_cast<size_t>(index)].file
            : compactImpulseResponses[static_cast<size_t>(index)].file;
        const auto relativePath = isCompactNamLibrary()
            ? compactNamModels[static_cast<size_t>(index)].relativePath
            : compactImpulseResponses[static_cast<size_t>(index)]
                  .relativePath;
        const auto displayName = isCompactNamLibrary()
            && compactNamModels[static_cast<size_t>(index)]
                   .displayName.isNotEmpty()
            ? compactNamModels[static_cast<size_t>(index)].displayName
            : file.getFileNameWithoutExtension();
        auto rowBounds =
            area.removeFromTop(compactAssetRowHeight).reduced(1.0f);
        const auto loaded = file.getFullPathName() == loadedPath;
        const auto selected = index == compactAssetSelected;
        graphics.setColour(
            loaded ? accent.withAlpha(0.23f)
                   : selected ? accent.withAlpha(0.12f)
                   : juce::Colours::white.withAlpha(
                         row % 2 == 0 ? 0.035f : 0.06f));
        graphics.fillRoundedRectangle(rowBounds, 3.0f);
        if (selected)
        {
            graphics.setColour(accent.withAlpha(0.7f));
            graphics.drawRoundedRectangle(rowBounds, 3.0f, 1.0f);
        }

        auto text = rowBounds.reduced(6.0f, 2.0f);
        auto trashBounds = juce::Rectangle<float>();
        if (isCompactNamLibrary())
            trashBounds = rowBounds.removeFromRight(24.0f);
        const auto starBounds = rowBounds.removeFromRight(24.0f);
        text.removeFromRight(isCompactNamLibrary() ? 48.0f : 24.0f);
        graphics.setColour(
            loaded ? accent : juce::Colours::white.withAlpha(0.9f));
        graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        graphics.drawFittedText(
            displayName,
            text.removeFromTop(14.0f).toNearestInt(),
            juce::Justification::centredLeft, 1, 0.75f);
        graphics.setColour(juce::Colours::white.withAlpha(0.48f));
        graphics.setFont(juce::FontOptions(8.0f));
        graphics.drawFittedText(
            relativePath,
            text.toNearestInt(),
            juce::Justification::centredLeft, 1, 0.72f);
        const auto favorite = isCompactNamLibrary()
            ? compactNamModels[static_cast<size_t>(index)].favorite
            : compactImpulseResponses[static_cast<size_t>(index)].favorite;
        const auto star = starPath(starBounds.reduced(3.0f));
        graphics.setColour(
            favorite
                ? accent
                : juce::Colours::white.withAlpha(0.28f));
        if (favorite)
            graphics.fillPath(star);
        else
            graphics.strokePath(
                star, juce::PathStrokeType(1.0f));
        if (isCompactNamLibrary())
        {
            const auto icon = trashBounds.reduced(6.0f, 5.0f);
            juce::Path bin;
            bin.addRoundedRectangle(
                icon.getX() + 1.0f, icon.getY() + 3.0f,
                icon.getWidth() - 2.0f, icon.getHeight() - 3.0f,
                1.0f);
            bin.startNewSubPath(icon.getX(), icon.getY() + 2.0f);
            bin.lineTo(icon.getRight(), icon.getY() + 2.0f);
            bin.startNewSubPath(
                icon.getCentreX() - 2.0f, icon.getY());
            bin.lineTo(
                icon.getCentreX() + 2.0f, icon.getY());
            graphics.setColour(
                juce::Colours::white.withAlpha(0.34f));
            graphics.strokePath(
                bin, juce::PathStrokeType(
                         1.1f, juce::PathStrokeType::curved,
                         juce::PathStrokeType::rounded));
        }
        if (loaded || (selected && compactAssetLoadPending))
        {
            graphics.setColour(accent.withAlpha(0.9f));
            graphics.setFont(juce::FontOptions(8.0f, juce::Font::bold));
            juce::String statusText;
            if (loaded)
                statusText = "LOADED";
            else if (selected && compactAssetLoadPending)
                statusText = "LOADING...";
            graphics.drawText(
                statusText,
                rowBounds.reduced(6.0f, 0.0f).toNearestInt(),
                juce::Justification::centredRight);
        }
    }

    if (compactAssetCount() > visibleRows)
    {
        auto track =
            getLocalBounds().toFloat().reduced(8.0f, 3.0f);
        track.removeFromTop(
            compactAssetHeaderHeight + compactAssetSearchHeight
            + compactAssetSearchGap);
        track = track.removeFromRight(3.0f).reduced(0.0f, 2.0f);
        const auto thumbHeight = juce::jmax(
            10.0f,
            track.getHeight()
                * static_cast<float>(visibleRows)
                / static_cast<float>(compactAssetCount()));
        const auto maximumFirst = juce::jmax(
            1, compactAssetCount() - visibleRows);
        const auto thumbY = track.getY()
            + (track.getHeight() - thumbHeight)
                * static_cast<float>(compactAssetFirstVisible)
                / static_cast<float>(maximumFirst);
        graphics.setColour(juce::Colours::white.withAlpha(0.1f));
        graphics.fillRoundedRectangle(track, 1.5f);
        graphics.setColour(accent.withAlpha(0.6f));
        graphics.fillRoundedRectangle(
            track.withY(thumbY).withHeight(thumbHeight), 1.5f);
    }
}

void EffectGraph::paintCompact(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    if (isCompactAssetLibrary())
    {
        paintCompactAssetLibrary(graphics, area);
        return;
    }
    auto visualizationLabel = "";
    const auto energy = juce::jlimit(
        0.0f, 1.0f,
        outputSamples.empty()
            ? 0.0f
            : std::sqrt(std::accumulate(
                  outputSamples.begin(), outputSamples.end(), 0.0f,
                  [](float sum, float sample)
                  {
                      return sum + sample * sample;
                  }) / static_cast<float>(outputSamples.size())) * 4.0f);
    const auto drawTrace =
        [&](const auto& source, juce::Colour colour, bool bipolar,
            bool spectrum)
    {
        juce::Path path;
        for (int pixel = 0;
             pixel < static_cast<int>(area.getWidth()); pixel += 2)
        {
            const auto proportion =
                static_cast<float>(pixel)
                / juce::jmax(1.0f, area.getWidth());
            const auto index = static_cast<size_t>(
                proportion * static_cast<float>(source.size() - 1));
            float sample = source[index];
            if (spectrum)
                sample = juce::jlimit(
                    0.0f, 1.0f, (sample + 80.0f) / 86.0f);
            else if (!bipolar)
                sample = juce::jlimit(0.0f, 1.0f, std::abs(sample));
            else
                sample = juce::jlimit(-1.0f, 1.0f, sample);
            const auto y = bipolar
                ? area.getCentreY() - sample * area.getHeight() * 0.46f
                : area.getBottom() - sample * area.getHeight() * 0.9f;
            const auto x = area.getX() + static_cast<float>(pixel);
            if (pixel == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
        graphics.setColour(colour);
        graphics.strokePath(path, juce::PathStrokeType(1.6f));
    };
    const auto drawSpectrum =
        [&](juce::Colour colour, bool filled)
    {
        juce::Path path;
        path.startNewSubPath(area.getX(), area.getBottom());
        for (int pixel = 0;
             pixel < static_cast<int>(area.getWidth()); pixel += 2)
        {
            const auto proportion =
                static_cast<float>(pixel)
                / juce::jmax(1.0f, area.getWidth() - 1.0f);
            const auto index = spectrumBinForProportion(
                proportion, processor.getSampleRate());
            const auto magnitude = juce::jlimit(
                0.0f, 1.0f, (outputSpectrum[index] + 78.0f) / 78.0f);
            path.lineTo(
                area.getX() + static_cast<float>(pixel),
                area.getBottom() - magnitude * area.getHeight() * 0.82f);
        }
        if (filled)
        {
            path.lineTo(area.getRight(), area.getBottom());
            path.closeSubPath();
            graphics.setGradientFill(juce::ColourGradient(
                colour.withAlpha(0.42f), area.getCentreX(), area.getY(),
                colour.withAlpha(0.02f), area.getCentreX(), area.getBottom(),
                false));
            graphics.fillPath(path);
        }
        graphics.setColour(colour);
        graphics.strokePath(path, juce::PathStrokeType(1.5f));
    };
    const auto drawLevel =
        [&](const auto& levels, juce::Colour colour)
    {
        juce::Path path;
        for (size_t index = 0; index < levels.size(); ++index)
        {
            const auto x = area.getX()
                + static_cast<float>(index)
                    / static_cast<float>(levels.size() - 1)
                    * area.getWidth();
            const auto y = juce::jmap(
                juce::jlimit(-60.0f, 0.0f, levels[index]),
                -60.0f, 0.0f, area.getBottom(), area.getY());
            if (index == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
        graphics.setColour(colour);
        graphics.strokePath(path, juce::PathStrokeType(1.6f));
    };

    switch (compactVisualization(type))
    {
        case CompactVisualization::response:
        {
            if (type == ModuleType::equalizer)
            {
                visualizationLabel = "3-BAND PARAMETRIC EQ";
                drawSpectrum(outputColour.withAlpha(0.48f), true);
                const auto plot = compactEqualizerPlotArea();
                std::array<
                    juce::dsp::IIR::Coefficients<float>::Ptr, 3>
                    responseCoefficients;
                for (int band = 0; band < 3; ++band)
                    responseCoefficients[static_cast<size_t>(band)] =
                        compactEqualizerCoefficients(band);
                const auto sampleRate = processor.getSampleRate() > 1000.0
                    ? processor.getSampleRate() : 48000.0;
                const auto responseDbAt =
                    [&responseCoefficients, sampleRate](
                        float frequency, int onlyBand = -1)
                {
                    const auto measuredFrequency = juce::jlimit(
                        1.0, sampleRate * 0.475,
                        static_cast<double>(frequency));
                    auto responseDb = 0.0f;
                    for (int band = 0; band < 3; ++band)
                    {
                        if (onlyBand >= 0 && band != onlyBand)
                            continue;
                        responseDb += static_cast<float>(
                            juce::Decibels::gainToDecibels(
                                responseCoefficients[
                                    static_cast<size_t>(band)]
                                    ->getMagnitudeForFrequency(
                                        measuredFrequency, sampleRate),
                                -72.0));
                    }
                    return responseDb;
                };
                constexpr std::array<float, 5> guideFrequencies {
                    50.0f, 100.0f, 1000.0f, 10000.0f, 20000.0f
                };
                constexpr std::array<const char*, 5> guideLabels {
                    "50", "100", "1k", "10k", "20k"
                };
                for (size_t guide = 0; guide < guideFrequencies.size();
                     ++guide)
                {
                    const auto proportion = std::log(
                        guideFrequencies[guide] / 20.0f)
                        / std::log(1000.0f);
                    const auto x = plot.getX()
                        + proportion * plot.getWidth();
                    graphics.setColour(
                        juce::Colours::white.withAlpha(0.075f));
                    graphics.drawVerticalLine(
                        juce::roundToInt(x), plot.getY(),
                        plot.getBottom());
                    graphics.setColour(
                        juce::Colours::white.withAlpha(0.46f));
                    const auto justification =
                        guide == 0 ? juce::Justification::centredLeft
                        : guide + 1 == guideFrequencies.size()
                            ? juce::Justification::centredRight
                            : juce::Justification::centred;
                    graphics.setFont(juce::FontOptions(7.0f));
                    graphics.drawText(
                        guideLabels[guide],
                        juce::Rectangle<float>(
                            x - 12.0f, plot.getBottom() - 10.0f,
                            24.0f, 10.0f).toNearestInt(),
                        justification);
                }
                for (const auto gainDb : { -18.0f, 0.0f, 18.0f })
                {
                    const auto y = plot.getCentreY()
                        - gainDb / 36.0f * plot.getHeight();
                    graphics.setColour(
                        juce::Colours::white.withAlpha(
                            gainDb == 0.0f ? 0.18f : 0.07f));
                    graphics.drawHorizontalLine(
                        juce::roundToInt(y), plot.getX(),
                        plot.getRight());
                    graphics.setColour(
                        juce::Colours::white.withAlpha(0.42f));
                    graphics.setFont(juce::FontOptions(6.5f));
                    graphics.drawText(
                        gainDb > 0.0f ? "+18"
                                     : gainDb < 0.0f ? "-18" : "0",
                        juce::Rectangle<float>(
                            plot.getX() + 2.0f, y - 5.0f,
                            20.0f, 10.0f).toNearestInt(),
                        juce::Justification::centredLeft);
                }
                for (int band = 0; band < 3; ++band)
                {
                    juce::Path bandPath;
                    for (int pixel = 0;
                         pixel < static_cast<int>(plot.getWidth());
                         pixel += 2)
                    {
                        const auto proportion =
                            static_cast<float>(pixel)
                            / juce::jmax(1.0f, plot.getWidth() - 1.0f);
                        const auto frequency =
                            20.0f * std::pow(1000.0f, proportion);
                        const auto db = responseDbAt(frequency, band);
                        const auto y = juce::jlimit(
                            plot.getY(), plot.getBottom(),
                            plot.getCentreY()
                                - db / 36.0f * plot.getHeight());
                        if (pixel == 0)
                            bandPath.startNewSubPath(plot.getX(), y);
                        else
                            bandPath.lineTo(
                                plot.getX() + static_cast<float>(pixel), y);
                    }
                    graphics.setColour(
                        accent.withAlpha(
                            band == compactEqualizerBand ? 0.30f : 0.13f));
                    graphics.strokePath(
                        bandPath, juce::PathStrokeType(1.0f));
                }
                juce::Path responsePath;
                for (int pixel = 0;
                     pixel < static_cast<int>(plot.getWidth()); pixel += 2)
                {
                    const auto proportion =
                        static_cast<float>(pixel)
                        / juce::jmax(1.0f, plot.getWidth() - 1.0f);
                    const auto frequency =
                        20.0f * std::pow(1000.0f, proportion);
                    const auto db = responseDbAt(frequency);
                    const auto y = juce::jlimit(
                        plot.getY(), plot.getBottom(),
                        plot.getCentreY()
                            - db / 36.0f * plot.getHeight());
                    if (pixel == 0)
                        responsePath.startNewSubPath(plot.getX(), y);
                    else
                        responsePath.lineTo(
                            plot.getX() + static_cast<float>(pixel), y);
                }
                graphics.setColour(accent.brighter(0.22f));
                graphics.strokePath(
                    responsePath, juce::PathStrokeType(
                        2.2f, juce::PathStrokeType::curved,
                        juce::PathStrokeType::rounded));
                for (int band = 0; band < 3; ++band)
                {
                    const auto point = compactEqualizerPoint(band);
                    const auto selected = band == compactEqualizerBand;
                    graphics.setColour(
                        accent.withAlpha(selected ? 1.0f : 0.82f));
                    graphics.fillEllipse(
                        juce::Rectangle<float>(
                            selected ? 20.0f : 16.0f,
                            selected ? 20.0f : 16.0f)
                            .withCentre(point));
                    graphics.setColour(juce::Colours::black);
                    const auto mode = band == 1
                        ? EqualizerBandMode::bell
                        : equalizerBandMode(
                              value(band == 0 ? 10 : 11));
                    const auto label =
                        mode == EqualizerBandMode::cut
                        ? band == 0 ? "HP" : "LP"
                        : mode == EqualizerBandMode::shelf
                        ? band == 0 ? "LS" : "HS"
                        : band == 0 ? "L" : band == 1 ? "M" : "H";
                    graphics.setFont(
                        juce::FontOptions(7.5f, juce::Font::bold));
                    graphics.drawText(
                        label,
                        juce::Rectangle<float>(16.0f, 16.0f)
                            .withCentre(point).toNearestInt(),
                        juce::Justification::centred);
                    if (selected)
                    {
                        graphics.setColour(
                            juce::Colours::white.withAlpha(0.7f));
                        graphics.drawEllipse(
                            juce::Rectangle<float>(26.0f, 26.0f)
                                .withCentre(point),
                            1.25f);
                        auto readout = juce::Rectangle<float>(
                            juce::jmin(210.0f, plot.getWidth()), 16.0f)
                            .withCentre(point.translated(0.0f, -18.0f))
                            .constrainedWithin(plot);
                        graphics.setColour(
                            juce::Colour(0xdf111820));
                        graphics.fillRoundedRectangle(readout, 3.0f);
                        graphics.setColour(
                            juce::Colours::white.withAlpha(0.9f));
                        graphics.drawText(
                            compactEqualizerBandReadout(band),
                            readout.toNearestInt(),
                            juce::Justification::centred);
                    }
                }
                if (compactEqualizerBand == 0
                    || compactEqualizerBand == 2)
                {
                    const auto modeArea = compactEqualizerModeArea();
                    const std::array<const char*, 3> labels {
                        "BELL", "SHELF", "CUT"
                    };
                    const auto selectedMode = equalizerBandMode(
                        value(compactEqualizerBand == 0 ? 10 : 11));
                    for (int mode = 0; mode < 3; ++mode)
                    {
                        auto bounds = modeArea;
                        bounds.setX(
                            modeArea.getX()
                            + static_cast<float>(mode)
                                * modeArea.getWidth() / 3.0f);
                        bounds.setWidth(modeArea.getWidth() / 3.0f - 2.0f);
                        const auto active =
                            mode == static_cast<int>(selectedMode);
                        graphics.setColour(
                            active ? accent.withAlpha(0.42f)
                                   : juce::Colours::black.withAlpha(0.26f));
                        graphics.fillRoundedRectangle(bounds, 3.0f);
                        graphics.setColour(
                            juce::Colours::white.withAlpha(
                                active ? 0.95f : 0.62f));
                        graphics.drawText(
                            labels[static_cast<size_t>(mode)],
                            bounds.toNearestInt(),
                            juce::Justification::centred);
                    }
                }
                const auto outputArea = compactEqualizerOutputArea();
                graphics.setColour(juce::Colours::black.withAlpha(0.3f));
                graphics.fillRoundedRectangle(outputArea, 3.0f);
                const auto outputY = outputArea.getBottom()
                    - value(9) * outputArea.getHeight();
                graphics.setColour(accent.withAlpha(0.85f));
                graphics.drawHorizontalLine(
                    juce::roundToInt(outputY),
                    outputArea.getX(), outputArea.getRight());
                graphics.fillEllipse(
                    outputArea.getCentreX() - 3.5f, outputY - 3.5f,
                    7.0f, 7.0f);
                graphics.setColour(
                    juce::Colours::white.withAlpha(0.72f));
                graphics.drawText(
                    "OUT",
                    outputArea.toNearestInt(),
                    juce::Justification::centredBottom);
                break;
            }
            if (type == ModuleType::autoWah)
            {
                visualizationLabel = "LIVE PICK FILTER";
                ContinuousTelemetrySnapshot telemetry;
                const auto valid =
                    processor.readContinuousTelemetry(slot, telemetry)
                    && telemetry.sequence != 0
                    && telemetry.valueCount
                           >= AutoWahModule::telemetryValueCount;
                const auto frequency = valid
                    ? telemetry.values[AutoWahModule::currentFrequencyHz]
                    : 20.0f * std::pow(
                          1000.0f,
                          juce::jlimit(0.0f, 1.0f, value(1)));
                const auto frequencyPosition = juce::jlimit(
                    0.0f, 1.0f,
                    std::log(juce::jmax(20.0f, frequency) / 20.0f)
                        / std::log(1000.0f));
                const auto pickActivity = valid
                    ? juce::jlimit(
                          0.0f, 1.0f,
                          telemetry.values[AutoWahModule::pickActivity])
                    : 0.0f;
                const auto envelope = valid
                    ? juce::jlimit(
                          0.0f, 1.0f,
                          telemetry.values[AutoWahModule::envelopeAmount])
                    : 0.0f;
                drawSpectrum(accent.withAlpha(0.5f), true);
                juce::Path response;
                for (int pixel = 0;
                     pixel <= static_cast<int>(area.getWidth()); pixel += 2)
                {
                    const auto x = static_cast<float>(pixel)
                        / juce::jmax(1.0f, area.getWidth());
                    const auto shape = std::exp(
                        -std::pow(
                            (x - frequencyPosition)
                                * (4.0f + value(5) * 7.0f),
                            2.0f));
                    const auto y = area.getBottom()
                        - (0.12f + shape * 0.72f)
                            * area.getHeight();
                    if (pixel == 0)
                        response.startNewSubPath(area.getX(), y);
                    else
                        response.lineTo(
                            area.getX() + static_cast<float>(pixel), y);
                }
                graphics.setColour(accent.withAlpha(
                    0.68f + pickActivity * 0.32f));
                graphics.strokePath(
                    response,
                    juce::PathStrokeType(1.7f + pickActivity * 1.3f));
                const auto markerX =
                    area.getX() + frequencyPosition * area.getWidth();
                graphics.setColour(accent.withAlpha(
                    0.25f + envelope * 0.65f));
                graphics.drawVerticalLine(
                    juce::roundToInt(markerX),
                    area.getY() + 11.0f, area.getBottom() - 8.0f);
                graphics.fillEllipse(
                    markerX - 3.0f,
                    area.getY() + 12.0f
                        + (1.0f - envelope)
                            * juce::jmax(1.0f, area.getHeight() - 28.0f),
                    6.0f, 6.0f);
                if (valid)
                {
                    const auto level = juce::jlimit(
                        0.0f, 1.0f,
                        (telemetry.values[AutoWahModule::inputLevelDb]
                         + 60.0f) / 60.0f);
                    const auto threshold = juce::jlimit(
                        0.0f, 1.0f,
                        (telemetry.values[AutoWahModule::thresholdDb]
                         + 60.0f) / 60.0f);
                    auto meter = area.removeFromBottom(5.0f);
                    graphics.setColour(
                        juce::Colours::white.withAlpha(0.12f));
                    graphics.fillRoundedRectangle(meter, 2.0f);
                    graphics.setColour(accent.withAlpha(0.8f));
                    graphics.fillRoundedRectangle(
                        meter.withWidth(meter.getWidth() * level), 2.0f);
                    graphics.setColour(reductionColour);
                    graphics.drawVerticalLine(
                        juce::roundToInt(
                            meter.getX() + threshold * meter.getWidth()),
                        meter.getY() - 1.0f, meter.getBottom() + 1.0f);
                }
                break;
            }
            visualizationLabel = "RESPONSE";
            drawSpectrum(accent, true);
            juce::Path response;
            const auto amount = value(0);
            for (int pixel = 0;
                 pixel <= static_cast<int>(area.getWidth()); pixel += 2)
            {
                const auto x = static_cast<float>(pixel)
                    / juce::jmax(1.0f, area.getWidth());
                const auto centre = 0.12f + amount * 0.76f;
                const auto shape = std::exp(
                    -std::pow((x - centre) * 5.0f, 2.0f));
                const auto y = area.getCentreY()
                    - (shape - 0.2f) * area.getHeight() * 0.28f;
                if (pixel == 0)
                    response.startNewSubPath(area.getX(), y);
                else
                    response.lineTo(
                        area.getX() + static_cast<float>(pixel), y);
            }
            graphics.setColour(juce::Colours::white.withAlpha(0.76f));
            graphics.strokePath(response, juce::PathStrokeType(1.7f));
            break;
        }

        case CompactVisualization::dynamics:
        {
            if (type == ModuleType::stageVocal)
            {
                visualizationLabel = "VOCAL CHAIN - FET / ESS / OPTO";
                ContinuousTelemetrySnapshot telemetry;
                const auto valid =
                    processor.readContinuousTelemetry(slot, telemetry)
                    && telemetry.sequence != 0
                    && telemetry.valueCount
                           >= StageVocalModule::telemetryValueCount;
                drawLevel(
                    inputLevels, inputColour.withAlpha(0.45f));
                drawLevel(outputLevels, outputColour.withAlpha(0.8f));
                const std::array<const char*, 3> names {
                    "FET", "ESS", "OPTO"
                };
                const std::array<int, 3> valueIndices {
                    StageVocalModule::fastReductionDb,
                    StageVocalModule::deEssReductionDb,
                    StageVocalModule::slowReductionDb
                };
                auto lanes = area.reduced(4.0f, 15.0f);
                lanes = lanes.withHeight(
                    juce::jmin(42.0f, lanes.getHeight()));
                for (size_t lane = 0; lane < names.size(); ++lane)
                {
                    auto laneArea = lanes.removeFromTop(
                        lanes.getHeight()
                        / static_cast<float>(names.size() - lane));
                    laneArea.reduce(0.0f, 2.0f);
                    const auto reduction = valid
                        ? juce::jlimit(
                              0.0f, 24.0f,
                              telemetry.values[static_cast<size_t>(
                                  valueIndices[lane])])
                        : 0.0f;
                    graphics.setColour(
                        juce::Colours::black.withAlpha(0.28f));
                    graphics.fillRoundedRectangle(laneArea, 2.0f);
                    graphics.setColour(
                        reductionColour.withAlpha(
                            0.3f
                            + 0.18f * static_cast<float>(lane)));
                    graphics.fillRoundedRectangle(
                        laneArea.withWidth(
                            laneArea.getWidth() * reduction / 24.0f),
                        2.0f);
                    graphics.setColour(
                        juce::Colours::white.withAlpha(0.76f));
                    graphics.setFont(
                        juce::FontOptions(7.5f, juce::Font::bold));
                    graphics.drawText(
                        juce::String(names[lane]) + " "
                            + juce::String(reduction, 1) + " dB",
                        laneArea.toNearestInt(),
                        juce::Justification::centredLeft);
                }
                break;
            }
            if (type != ModuleType::compressor)
            {
                visualizationLabel = "DYNAMICS";
                drawLevel(inputLevels, inputColour.withAlpha(0.65f));
                drawLevel(outputLevels, outputColour);
                for (size_t index = 1;
                     index < gainReductionLevels.size(); ++index)
                {
                    const auto x = area.getX()
                        + static_cast<float>(index)
                            / static_cast<float>(
                                gainReductionLevels.size() - 1)
                            * area.getWidth();
                    const auto amount = juce::jlimit(
                        0.0f, 24.0f, gainReductionLevels[index]);
                    if (amount > 0.05f)
                    {
                        graphics.setColour(
                            reductionColour.withAlpha(0.16f));
                        graphics.drawVerticalLine(
                            juce::roundToInt(x), area.getY(),
                            area.getY()
                                + amount / 24.0f * area.getHeight());
                    }
                }
                break;
            }

            visualizationLabel =
                processor.hasExternalSidechain()
                ? value(7) >= 0.5f
                    ? "COMPRESSION - EXT DETECTOR"
                    : "COMPRESSION - INT DETECTOR"
                : "COMPRESSION - INT DETECTOR / NO SIDECHAIN";
            auto content = area;
            content.removeFromTop(14.0f);
            auto reductionArea =
                content.removeFromBottom(content.getHeight() * 0.3f);
            content.removeFromBottom(5.0f);
            auto levelArea = content;

            const auto levelY =
                [&levelArea](float levelDb)
                {
                    const auto normalized = juce::jlimit(
                        0.0f, 1.0f, (levelDb + 60.0f) / 60.0f);
                    return levelArea.getBottom()
                        - normalized * levelArea.getHeight();
                };
            const auto thresholdDb = -60.0f + value(0) * 60.0f;
            const auto thresholdY = levelY(thresholdDb);
            graphics.setColour(reductionColour.withAlpha(0.07f));
            graphics.fillRect(
                levelArea.withBottom(thresholdY));
            graphics.setColour(reductionColour.withAlpha(0.62f));
            const float dashLengths[] { 5.0f, 4.0f };
            graphics.drawDashedLine(
                { levelArea.getX(), thresholdY,
                  levelArea.getRight(), thresholdY },
                dashLengths, 2, dragPrimary == 0 ? 2.0f : 1.2f);
            graphics.fillEllipse(
                levelArea.getRight() - 4.0f, thresholdY - 4.0f,
                8.0f, 8.0f);
            graphics.setColour(reductionColour.withAlpha(0.78f));
            graphics.setFont(
                juce::FontOptions(7.5f, juce::Font::bold));
            auto thresholdLabelArea = levelArea;
            thresholdLabelArea.setTop(
                juce::jlimit(
                    levelArea.getY(), levelArea.getBottom() - 10.0f,
                    thresholdY - 10.0f));
            thresholdLabelArea.setHeight(10.0f);
            graphics.drawText(
                "THR " + juce::String(thresholdDb, 1) + " dB | DRAG",
                thresholdLabelArea.toNearestInt(),
                juce::Justification::centredRight);

            const auto drawLevelHistory =
                [&](const auto& levels, juce::Colour colour,
                    float thickness)
                {
                    juce::Path path;
                    for (size_t index = 0; index < levels.size(); ++index)
                    {
                        const auto x = levelArea.getX()
                            + static_cast<float>(index)
                                / static_cast<float>(levels.size() - 1)
                                * levelArea.getWidth();
                        const auto point = juce::Point<float>(
                            x, levelY(levels[index]));
                        if (index == 0)
                            path.startNewSubPath(point);
                        else
                            path.lineTo(point);
                    }
                    graphics.setColour(colour);
                    graphics.strokePath(
                        path, juce::PathStrokeType(thickness));
                };
            drawLevelHistory(
                inputLevels, inputColour.withAlpha(0.72f), 1.25f);
            drawLevelHistory(outputLevels, outputColour, 1.8f);

            juce::Path reductionPath;
            reductionPath.startNewSubPath(
                reductionArea.getX(), reductionArea.getY());
            for (size_t index = 0;
                 index < gainReductionLevels.size(); ++index)
            {
                const auto x = reductionArea.getX()
                    + static_cast<float>(index)
                        / static_cast<float>(
                            gainReductionLevels.size() - 1)
                        * reductionArea.getWidth();
                const auto amount = juce::jlimit(
                    0.0f, 24.0f, gainReductionLevels[index]);
                reductionPath.lineTo(
                    x, reductionArea.getY()
                        + amount / 24.0f * reductionArea.getHeight());
            }
            reductionPath.lineTo(
                reductionArea.getRight(), reductionArea.getY());
            reductionPath.closeSubPath();
            graphics.setColour(reductionColour.withAlpha(0.2f));
            graphics.fillPath(reductionPath);
            graphics.setColour(reductionColour.withAlpha(0.82f));
            graphics.strokePath(
                reductionPath, juce::PathStrokeType(1.25f));

            const auto latestInput = inputLevels.back();
            const auto latestOutput = outputLevels.back();
            const auto latestReduction = gainReductionLevels.back();
            graphics.setFont(
                juce::FontOptions(8.0f, juce::Font::bold));
            graphics.setColour(inputColour.withAlpha(0.88f));
            graphics.drawText(
                "IN " + juce::String(latestInput, 1) + " dB",
                levelArea.toNearestInt(),
                juce::Justification::topLeft);
            graphics.setColour(outputColour.withAlpha(0.92f));
            graphics.drawText(
                "OUT " + juce::String(latestOutput, 1) + " dB",
                levelArea.toNearestInt(),
                juce::Justification::topRight);
            graphics.setColour(reductionColour.withAlpha(0.9f));
            graphics.drawText(
                "GR " + juce::String(latestReduction, 1) + " dB",
                reductionArea.toNearestInt(),
                juce::Justification::bottomLeft);
            graphics.setColour(
                juce::Colours::white.withAlpha(0.58f));
            auto recoveryText =
                value(8) >= 0.5f ? juce::String("AUTO")
                                 : juce::String("MANUAL");
            if (value(5) > 0.0001f)
                recoveryText += " + "
                    + juce::String(value(5) * 24.0f, 1) + " dB TRIM";
            graphics.drawText(
                recoveryText, reductionArea.toNearestInt(),
                juce::Justification::bottomRight);
            break;
        }

        case CompactVisualization::transfer:
        {
            if (type == ModuleType::midnightCurrent)
            {
                visualizationLabel = "LIVE CURRENT";
                ContinuousTelemetrySnapshot telemetry;
                const auto valid =
                    processor.readContinuousTelemetry(slot, telemetry)
                    && telemetry.sequence != 0
                    && telemetry.valueCount
                           >= MidnightCurrentModule::telemetryValueCount;
                const auto leftPhase = valid
                    ? telemetry.values[MidnightCurrentModule::leftPhase]
                    : 0.0f;
                const auto rightPhase = valid
                    ? telemetry.values[MidnightCurrentModule::rightPhase]
                    : 0.25f;
                const auto cutoff = valid
                    ? telemetry.values[
                          MidnightCurrentModule::actualCutoffHz]
                    : 20.0f * std::pow(1000.0f, value(0));
                const auto cutoffPosition = juce::jlimit(
                    0.0f, 1.0f,
                    std::log(juce::jmax(20.0f, cutoff) / 20.0f)
                        / std::log(1000.0f));
                const auto intensity = valid
                    ? juce::jlimit(
                          0.0f, 1.0f,
                          telemetry.values[
                              MidnightCurrentModule::phaseIntensity])
                    : value(3);
                const auto wetEnergy = valid
                    ? juce::jlimit(
                          0.0f, 1.0f,
                          std::sqrt(juce::jmax(
                              0.0f,
                              telemetry.values[
                                  MidnightCurrentModule::wetEnergy]))
                              * 2.0f)
                    : 0.0f;
                juce::Path current;
                for (int pixel = 0;
                     pixel <= static_cast<int>(area.getWidth()); pixel += 2)
                {
                    const auto x = static_cast<float>(pixel)
                        / juce::jmax(1.0f, area.getWidth());
                    const auto phase = juce::MathConstants<float>::twoPi
                        * (x * 3.0f + 0.5f * (leftPhase + rightPhase));
                    const auto y = area.getCentreY()
                        - std::sin(phase)
                            * area.getHeight() * 0.3f * intensity
                        - std::sin(phase * 2.63f)
                            * area.getHeight() * 0.08f * wetEnergy;
                    if (pixel == 0)
                        current.startNewSubPath(area.getX(), y);
                    else
                        current.lineTo(
                            area.getX() + static_cast<float>(pixel), y);
                }
                graphics.setColour(accent.withAlpha(
                    0.16f + wetEnergy * 0.24f));
                graphics.strokePath(
                    current, juce::PathStrokeType(7.0f));
                graphics.setColour(accent.withAlpha(0.9f));
                graphics.strokePath(
                    current, juce::PathStrokeType(1.7f));
                const auto cutoffX =
                    area.getX() + cutoffPosition * area.getWidth();
                graphics.setColour(outputColour.withAlpha(0.82f));
                graphics.drawVerticalLine(
                    juce::roundToInt(cutoffX),
                    area.getY() + 12.0f, area.getBottom());
                graphics.fillEllipse(
                    cutoffX - 3.5f, area.getY() + 13.0f,
                    7.0f, 7.0f);
                break;
            }
            visualizationLabel = "TRANSFER";
            graphics.setColour(juce::Colour(0x50303a46));
            graphics.drawLine(
                area.getX(), area.getBottom(),
                area.getRight(), area.getY(), 1.0f);
            juce::Path curve;
            const auto drive = 1.0f + value(0) * 8.0f;
            for (int pixel = 0;
                 pixel <= static_cast<int>(area.getWidth()); pixel += 2)
            {
                const auto x = 2.0f * static_cast<float>(pixel)
                        / juce::jmax(1.0f, area.getWidth())
                    - 1.0f;
                const auto shaped =
                    std::tanh(x * drive) / std::tanh(drive);
                const auto point = juce::Point<float>(
                    area.getX() + static_cast<float>(pixel),
                    area.getCentreY()
                        - shaped * area.getHeight() * 0.44f);
                if (pixel == 0)
                    curve.startNewSubPath(point);
                else
                    curve.lineTo(point);
            }
            graphics.setColour(accent);
            graphics.strokePath(curve, juce::PathStrokeType(2.0f));
            drawTrace(
                outputSamples, outputColour.withAlpha(0.45f), true, false);
            break;
        }

        case CompactVisualization::delay:
        {
            visualizationLabel = "REPEAT FIELD";
            const auto feedback = 0.35f + value(1) * 0.58f;
            auto height = area.getHeight() * (0.58f + energy * 0.32f);
            for (int tap = 0; tap < 8; ++tap)
            {
                const auto x = area.getX()
                    + area.getWidth() * static_cast<float>(tap)
                        / 7.0f;
                graphics.setColour(accent.withAlpha(
                    0.25f + 0.65f * std::pow(
                        feedback, static_cast<float>(tap))));
                graphics.drawVerticalLine(
                    juce::roundToInt(x),
                    area.getCentreY() - height * 0.5f,
                    area.getCentreY() + height * 0.5f);
                height *= feedback;
            }
            drawTrace(
                outputSamples, juce::Colours::white.withAlpha(0.5f),
                true, false);
            break;
        }

        case CompactVisualization::space:
        {
            visualizationLabel = "DECAY SPACE";
            const auto centre = area.getCentre();
            const auto radius = juce::jmin(
                area.getWidth(), area.getHeight()) * 0.46f;
            for (int ring = 5; ring >= 1; --ring)
            {
                const auto scale = static_cast<float>(ring) / 5.0f;
                graphics.setColour(accent.withAlpha(
                    0.2f + energy * 0.18f
                        + static_cast<float>(5 - ring) * 0.025f));
                graphics.drawEllipse(
                    juce::Rectangle<float>(
                        radius * 2.0f * scale,
                        radius * 0.9f * scale)
                        .withCentre(centre.translated(
                            (ring & 1) == 0 ? radius * 0.08f : -radius * 0.08f,
                            0.0f)),
                    1.2f);
            }
            if (energy > 0.01f)
                drawTrace(
                    outputSamples, outputColour.withAlpha(0.75f),
                    true, false);
            break;
        }

        case CompactVisualization::impulse:
        {
            visualizationLabel = "IMPULSE SHAPE";
            const auto preview =
                processor.impulseResponsePreview(slot);
            juce::Path envelope;
            envelope.startNewSubPath(
                area.getX(), area.getCentreY());
            for (size_t index = 0; index < preview.size(); ++index)
            {
                const auto x = area.getX()
                    + static_cast<float>(index)
                        / static_cast<float>(preview.size() - 1)
                        * area.getWidth();
                const auto magnitude = juce::jlimit(
                    0.0f, 1.0f, preview[index]);
                envelope.lineTo(
                    x, area.getCentreY()
                           - magnitude * area.getHeight() * 0.43f);
            }
            for (size_t index = preview.size(); index-- > 0;)
            {
                const auto x = area.getX()
                    + static_cast<float>(index)
                        / static_cast<float>(preview.size() - 1)
                        * area.getWidth();
                envelope.lineTo(
                    x, area.getCentreY()
                           + juce::jlimit(
                                 0.0f, 1.0f, preview[index])
                               * area.getHeight() * 0.43f);
            }
            envelope.closeSubPath();
            graphics.setColour(accent.withAlpha(0.28f));
            graphics.fillPath(envelope);
            graphics.setColour(accent);
            graphics.strokePath(
                envelope, juce::PathStrokeType(1.4f));
            break;
        }

        case CompactVisualization::wetSpectrum:
        {
            visualizationLabel = "LIVE WET SPECTRUM";
            ContinuousTelemetrySnapshot telemetry;
            const auto valid =
                processor.readContinuousTelemetry(slot, telemetry)
                && telemetry.sequence != 0
                && telemetry.valueCount
                       >= ConvolutionReverbModule::telemetryValueCount;
            std::array<float,
                       ConvolutionReverbModule::spectrumBandCount>
                spectrum {};
            if (valid)
                for (size_t band = 0; band < spectrum.size(); ++band)
                    spectrum[band] = telemetry.values[
                        static_cast<size_t>(
                            ConvolutionReverbModule::spectrumBandStart)
                        + band];
            const auto maximum = *std::max_element(
                spectrum.begin(), spectrum.end());
            const auto scale =
                maximum > 1.0e-8f ? 1.0f / maximum : 0.0f;
            juce::Path wetSpectrum;
            wetSpectrum.startNewSubPath(
                area.getX(), area.getBottom());
            for (size_t band = 0; band < spectrum.size(); ++band)
            {
                const auto x = area.getX()
                    + static_cast<float>(band)
                        / static_cast<float>(spectrum.size() - 1)
                        * area.getWidth();
                const auto normalized = std::sqrt(
                    juce::jlimit(
                        0.0f, 1.0f, spectrum[band] * scale));
                wetSpectrum.lineTo(
                    x, area.getBottom()
                           - normalized * area.getHeight() * 0.86f);
            }
            wetSpectrum.lineTo(
                area.getRight(), area.getBottom());
            wetSpectrum.closeSubPath();
            graphics.setGradientFill(juce::ColourGradient(
                accent.withAlpha(0.5f),
                area.getCentreX(), area.getY(),
                accent.withAlpha(0.03f),
                area.getCentreX(), area.getBottom(), false));
            graphics.fillPath(wetSpectrum);
            graphics.setColour(accent);
            graphics.strokePath(
                wetSpectrum, juce::PathStrokeType(1.6f));
            break;
        }

        case CompactVisualization::stereo:
        {
            visualizationLabel = "STEREO FIELD";
            const auto scope = area.withSizeKeepingCentre(
                juce::jmin(area.getWidth(), area.getHeight()) * 0.95f,
                area.getHeight() * 0.9f);
            graphics.setColour(juce::Colour(0x50303a46));
            graphics.drawEllipse(scope, 1.0f);
            graphics.drawLine(
                scope.getCentreX(), scope.getY(),
                scope.getCentreX(), scope.getBottom(), 1.0f);
            juce::Path vectorscope;
            for (size_t index = 0;
                 index < stereoLeftSamples.size(); index += 8)
            {
                const auto left = stereoLeftSamples[index];
                const auto right = stereoRightSamples[index];
                const auto point = juce::Point<float>(
                    scope.getCentreX()
                        + (left - right) * scope.getWidth() * 0.24f,
                    scope.getCentreY()
                        - (left + right) * scope.getHeight() * 0.24f);
                if (index == 0)
                    vectorscope.startNewSubPath(point);
                else
                    vectorscope.lineTo(point);
            }
            graphics.setColour(accent.withAlpha(0.86f));
            graphics.strokePath(vectorscope, juce::PathStrokeType(1.3f));
            break;
        }

        case CompactVisualization::modulation:
        {
            visualizationLabel = "MODULATION";
            juce::Path ribbon;
            const auto cycles = 1.0f + value(0) * 4.0f;
            const auto depth = 0.15f + value(1) * 0.32f;
            for (int pixel = 0;
                 pixel <= static_cast<int>(area.getWidth()); pixel += 2)
            {
                const auto phase = static_cast<float>(pixel)
                    / juce::jmax(1.0f, area.getWidth())
                    * juce::MathConstants<float>::twoPi * cycles;
                const auto point = juce::Point<float>(
                    area.getX() + static_cast<float>(pixel),
                    area.getCentreY()
                        - std::sin(phase) * area.getHeight() * depth);
                if (pixel == 0)
                    ribbon.startNewSubPath(point);
                else
                    ribbon.lineTo(point);
            }
            graphics.setColour(accent.withAlpha(0.22f));
            graphics.strokePath(
                ribbon, juce::PathStrokeType(8.0f));
            graphics.setColour(accent);
            graphics.strokePath(ribbon, juce::PathStrokeType(1.6f));
            break;
        }

        case CompactVisualization::grains:
            if (type == ModuleType::randomGranulizer)
            {
                visualizationLabel = "LIVE GRAIN STREAM";
                const auto events =
                    processor.getRack().grainVisualEvents(slot);
                auto newestSequence = std::uint32_t {};
                for (const auto& event : events)
                    newestSequence =
                        juce::jmax(newestSequence, event.sequence);
                auto liveCount = 0;
                for (const auto& event : events)
                {
                    if (event.sequence == 0)
                        continue;
                    if (event.progress < 1.0f)
                        ++liveCount;
                    const auto recency = juce::jlimit(
                        0.0f, 1.0f,
                        1.0f
                            - static_cast<float>(
                                  newestSequence - event.sequence)
                                / static_cast<float>(
                                    grainVisualEventCount));
                    const auto duration = juce::jlimit(
                        50.0f, 2000.0f,
                        event.durationSeconds * 1000.0f);
                    const auto durationPosition = juce::jlimit(
                        0.0f, 1.0f,
                        static_cast<float>(
                            std::log(duration / 50.0f)
                            / std::log(40.0f)));
                    const auto width = juce::jmap(
                        durationPosition, 8.0f,
                        juce::jmax(14.0f, area.getWidth() * 0.18f));
                    const auto anchorX = area.getRight()
                        - event.historyPosition * area.getWidth() * 0.9f
                        - 4.0f;
                    const auto y = area.getCentreY()
                        + juce::jlimit(-1.0f, 1.0f, event.pan)
                            * area.getHeight() * 0.34f;
                    auto grain = juce::Rectangle<float>(
                        anchorX
                            - (event.reverse ? 0.0f : width),
                        y - 3.0f, width, 6.0f)
                                     .constrainedWithin(
                                         area.reduced(3.0f, 13.0f));
                    const auto colour = juce::Colour::fromHSV(
                        juce::jmap(
                            juce::jlimit(
                                0.0f, 1.0f, event.filter),
                            0.56f, 0.38f),
                        0.68f, 0.95f,
                        juce::jlimit(
                            0.16f, 0.92f,
                            recency
                                * (event.progress < 1.0f
                                       ? 1.0f : 0.5f)));
                    graphics.setColour(colour);
                    graphics.fillRoundedRectangle(grain, 2.5f);
                    const auto arrowX =
                        event.reverse ? grain.getX() : grain.getRight();
                    juce::Path arrow;
                    arrow.startNewSubPath(
                        arrowX, grain.getCentreY());
                    arrow.lineTo(
                        arrowX + (event.reverse ? 5.0f : -5.0f),
                        grain.getCentreY() - 3.0f);
                    arrow.lineTo(
                        arrowX + (event.reverse ? 5.0f : -5.0f),
                        grain.getCentreY() + 3.0f);
                    arrow.closeSubPath();
                    graphics.fillPath(arrow);
                    if (event.progress < 1.0f)
                    {
                        graphics.setColour(
                            juce::Colours::white.withAlpha(0.84f));
                        graphics.drawVerticalLine(
                            juce::roundToInt(
                                grain.getX()
                                + event.progress * grain.getWidth()),
                            grain.getY() - 2.0f,
                            grain.getBottom() + 2.0f);
                    }
                }
                graphics.setColour(
                    juce::Colours::white.withAlpha(0.48f));
                graphics.setFont(
                    juce::FontOptions(8.0f, juce::Font::bold));
                graphics.drawText(
                    liveCount > 0
                        ? juce::String(liveCount) + " LIVE"
                        : "WAITING FOR GRAINS",
                    area.toNearestInt(),
                    juce::Justification::bottomRight);
                break;
            }
            visualizationLabel = "GRAIN STREAM";
            for (int grain = 0; grain < 14; ++grain)
            {
                const auto seed =
                    static_cast<float>((grain * 37 + static_cast<int>(
                        value(2) * 97.0f)) % 101) / 101.0f;
                const auto width = area.getWidth()
                    * (0.025f + 0.08f * value(0));
                const auto x = area.getX()
                    + std::fmod(
                        seed + static_cast<float>(grain) * 0.083f, 1.0f)
                        * juce::jmax(1.0f, area.getWidth() - width);
                const auto y = area.getY() + 13.0f
                    + std::fmod(seed * 3.7f, 1.0f)
                        * juce::jmax(1.0f, area.getHeight() - 19.0f);
                graphics.setColour(accent.withAlpha(
                    0.22f + 0.55f * seed));
                graphics.fillRoundedRectangle(
                    { x, y, width, 3.0f + seed * 3.0f }, 2.0f);
            }
            break;

        case CompactVisualization::slices:
            if (type == ModuleType::beatPermuter)
            {
                visualizationLabel = "LIVE SLICE ROUTING";
                const auto events =
                    processor.getRack().beatPermutationVisualEvents(slot);
                const BeatPermutationVisualEvent* currentEvent = nullptr;
                for (const auto& event : events)
                    if (event.sequence != 0 && event.progress < 1.0f
                        && (currentEvent == nullptr
                            || event.sequence > currentEvent->sequence))
                        currentEvent = &event;

                auto content = area.reduced(2.0f, 5.0f);
                content.removeFromTop(10.0f);
                auto historyRail = content.removeFromTop(
                    juce::jmin(24.0f, content.getHeight() * 0.25f));
                content.removeFromTop(8.0f);
                const auto sourceCount = currentEvent != nullptr
                    ? juce::jlimit(1, 8, currentEvent->windowSlices)
                    : juce::jlimit(
                          1, 8, juce::roundToInt(1.0f + value(3) * 7.0f));
                const auto stepCount = currentEvent != nullptr
                    ? juce::jlimit(1, 8, currentEvent->stepCount)
                    : juce::jlimit(
                          1, 8, juce::roundToInt(1.0f + value(4) * 7.0f));
                const auto historyWidth =
                    historyRail.getWidth() / static_cast<float>(sourceCount);
                for (int source = 0; source < sourceCount; ++source)
                {
                    auto sourceCell = juce::Rectangle<float>(
                        historyRail.getX()
                            + historyWidth * static_cast<float>(source),
                        historyRail.getY(), historyWidth,
                        historyRail.getHeight()).reduced(1.5f, 1.0f);
                    const auto newest =
                        source == 0 ? accent : outputColour;
                    graphics.setColour(newest.withAlpha(
                        source == 0 ? 0.52f : 0.2f));
                    graphics.fillRoundedRectangle(sourceCell, 3.0f);
                    graphics.setColour(
                        juce::Colours::white.withAlpha(0.78f));
                    graphics.setFont(juce::FontOptions(
                        8.0f, juce::Font::bold));
                    graphics.drawText(
                        juce::String(source + 1),
                        sourceCell.toNearestInt(),
                        juce::Justification::centred);
                }

                const auto stepWidth =
                    content.getWidth() / static_cast<float>(stepCount);
                for (int step = 0; step < stepCount; ++step)
                {
                    auto stepCell = juce::Rectangle<float>(
                        content.getX()
                            + stepWidth * static_cast<float>(step),
                        content.getY(), stepWidth,
                        content.getHeight()).reduced(2.0f, 1.0f);
                    const auto isCurrent =
                        currentEvent != nullptr
                        && step == currentEvent->currentStep;
                    auto leftSource = step % sourceCount + 1;
                    auto rightSource = leftSource;
                    auto reversed = false;
                    if (currentEvent != nullptr)
                    {
                        leftSource = juce::jlimit(
                            1, sourceCount,
                            currentEvent->leftSourceSlices[
                                static_cast<size_t>(step)]);
                        rightSource = juce::jlimit(
                            1, sourceCount,
                            currentEvent->rightSourceSlices[
                                static_cast<size_t>(step)]);
                        reversed =
                            currentEvent->leftReverse[
                                static_cast<size_t>(step)]
                            || currentEvent->rightReverse[
                                static_cast<size_t>(step)];

                        const auto source = 0.5f
                            * static_cast<float>(
                                leftSource + rightSource - 2);
                        const auto sourceX = historyRail.getX()
                            + (source + 0.5f) * historyWidth;
                        juce::Path route;
                        route.startNewSubPath(
                            sourceX, historyRail.getBottom());
                        route.cubicTo(
                            sourceX, historyRail.getBottom() + 7.0f,
                            stepCell.getCentreX(), stepCell.getY() - 7.0f,
                            stepCell.getCentreX(), stepCell.getY());
                        graphics.setColour(
                            (isCurrent ? accent : outputColour)
                                .withAlpha(isCurrent ? 0.9f : 0.28f));
                        graphics.strokePath(
                            route, juce::PathStrokeType(
                                       isCurrent ? 2.0f : 1.0f));
                    }

                    graphics.setColour(
                        (isCurrent ? accent : outputColour)
                            .withAlpha(isCurrent ? 0.32f : 0.11f));
                    graphics.fillRoundedRectangle(stepCell, 4.0f);
                    graphics.setColour(
                        (isCurrent ? accent : outputColour)
                            .withAlpha(isCurrent ? 0.95f : 0.38f));
                    graphics.drawRoundedRectangle(
                        stepCell, 4.0f, isCurrent ? 1.8f : 0.8f);
                    graphics.setColour(
                        juce::Colours::white.withAlpha(
                            currentEvent != nullptr ? 0.9f : 0.38f));
                    graphics.setFont(juce::FontOptions(
                        juce::jlimit(8.0f, 13.0f, stepCell.getHeight() * 0.3f),
                        juce::Font::bold));
                    const auto sourceText = leftSource == rightSource
                        ? juce::String(leftSource)
                        : juce::String(leftSource) + "|"
                            + juce::String(rightSource);
                    graphics.drawText(
                        sourceText, stepCell.toNearestInt(),
                        juce::Justification::centred);
                    if (reversed)
                    {
                        graphics.setColour(accent.withAlpha(0.95f));
                        graphics.setFont(juce::FontOptions(
                            7.5f, juce::Font::bold));
                        graphics.drawText(
                            "REV",
                            stepCell.removeFromBottom(11.0f).toNearestInt(),
                            juce::Justification::centred);
                    }
                }
                if (currentEvent != nullptr)
                {
                    const auto playheadX = content.getX()
                        + content.getWidth()
                            * juce::jlimit(
                                0.0f, 1.0f, currentEvent->progress);
                    graphics.setColour(accent.withAlpha(0.9f));
                    graphics.drawVerticalLine(
                        juce::roundToInt(playheadX),
                        content.getY(), content.getBottom());
                    graphics.fillEllipse(
                        playheadX - 2.5f, content.getY() - 4.0f,
                        5.0f, 5.0f);
                }
                else
                {
                    graphics.setColour(
                        juce::Colours::white.withAlpha(0.35f));
                    graphics.setFont(juce::FontOptions(
                        8.0f, juce::Font::bold));
                    graphics.drawText(
                        "CAPTURING HISTORY",
                        content.toNearestInt(),
                        juce::Justification::centredBottom);
                }
            }
            else
            {
                visualizationLabel = "SLICE MAP";
                for (int slice = 0; slice < 8; ++slice)
                {
                    auto sliceArea = area.withWidth(area.getWidth() / 8.0f)
                        .translated(
                            static_cast<float>(slice)
                                * area.getWidth() / 8.0f,
                            0.0f)
                        .reduced(2.0f, 8.0f + (slice % 3) * 4.0f);
                    graphics.setColour(
                        ((slice * 5
                          + static_cast<int>(value(0) * 8.0f)) & 1)
                            != 0
                        ? accent.withAlpha(0.72f)
                        : outputColour.withAlpha(0.38f));
                    graphics.fillRoundedRectangle(sliceArea, 3.0f);
                }
            }
            break;

        case CompactVisualization::spectral:
        {
            visualizationLabel = "SPECTRAL FIELD";
            for (int band = 0; band < 32; ++band)
            {
                const auto firstBin = spectrumBinForProportion(
                    static_cast<float>(band) / 32.0f,
                    processor.getSampleRate());
                const auto lastBin = spectrumBinForProportion(
                    static_cast<float>(band + 1) / 32.0f,
                    processor.getSampleRate());
                const auto endBin = juce::jmin(
                    outputSpectrum.size(), lastBin + 1);
                const auto peak = *std::max_element(
                    outputSpectrum.begin()
                        + static_cast<std::ptrdiff_t>(firstBin),
                    outputSpectrum.begin()
                        + static_cast<std::ptrdiff_t>(endBin));
                const auto magnitude = juce::jlimit(
                    0.0f, 1.0f, (peak + 78.0f) / 78.0f);
                const auto x = area.getX()
                    + static_cast<float>(band)
                        * area.getWidth() / 32.0f;
                graphics.setColour(accent.withAlpha(
                    0.12f + magnitude * 0.78f));
                graphics.fillRect(
                    x, area.getBottom() - magnitude * area.getHeight(),
                    juce::jmax(1.0f, area.getWidth() / 32.0f - 1.0f),
                    magnitude * area.getHeight());
            }
            break;
        }

        case CompactVisualization::resonator:
            if (type == ModuleType::resonantSculpture)
            {
                visualizationLabel = "TRACKED MODE ENERGY";
                ContinuousTelemetrySnapshot telemetry;
                const auto valid =
                    processor.readContinuousTelemetry(slot, telemetry)
                    && telemetry.sequence != 0
                    && telemetry.valueCount
                           >= ResonantSculptureModule::telemetryValueCount;
                constexpr auto displayedModes = 8;
                for (int mode = 0; mode < displayedModes; ++mode)
                {
                    const auto energyValue = valid
                        ? telemetry.values[static_cast<size_t>(
                              ResonantSculptureModule::firstModeEnergy
                              + mode)]
                        : 0.0f;
                    const auto modeEnergy = juce::jlimit(
                        0.0f, 1.0f,
                        std::sqrt(juce::jmax(0.0f, energyValue)) * 2.5f);
                    const auto cellWidth =
                        area.getWidth() / displayedModes;
                    const auto x = area.getX()
                        + (static_cast<float>(mode) + 0.5f) * cellWidth;
                    const auto height =
                        juce::jmax(2.0f, modeEnergy * area.getHeight() * 0.72f);
                    graphics.setColour(accent.withAlpha(
                        0.2f + modeEnergy * 0.75f));
                    graphics.fillRoundedRectangle(
                        x - cellWidth * 0.24f,
                        area.getBottom() - height,
                        cellWidth * 0.48f, height, 2.0f);
                }
                if (valid)
                {
                    const auto confidence = juce::jlimit(
                        0.0f, 1.0f,
                        telemetry.values[
                            ResonantSculptureModule::trackingConfidence]);
                    const auto frequency = telemetry.values[
                        ResonantSculptureModule::trackedFrequencyHz];
                    graphics.setColour(
                        juce::Colours::white.withAlpha(
                            0.35f + confidence * 0.6f));
                    graphics.setFont(
                        juce::FontOptions(8.0f, juce::Font::bold));
                    graphics.drawText(
                        frequency > 0.0f
                            ? juce::String(juce::roundToInt(frequency))
                                  + " Hz  "
                                  + juce::String(
                                      juce::roundToInt(confidence * 100.0f))
                                  + "%"
                            : "TRACKING",
                        area.toNearestInt(),
                        juce::Justification::topRight);
                    const auto strike = juce::jlimit(
                        0.0f, 1.0f,
                        telemetry.values[
                            ResonantSculptureModule::transientAmount]);
                    const auto bow = juce::jlimit(
                        0.0f, 1.0f,
                        telemetry.values[
                            ResonantSculptureModule::bowAmount]);
                    graphics.setColour(
                        reductionColour.withAlpha(0.25f + strike * 0.75f));
                    graphics.fillEllipse(
                        area.getX() + 4.0f, area.getBottom() - 8.0f,
                        4.0f + strike * 7.0f,
                        4.0f + strike * 7.0f);
                    graphics.setColour(
                        outputColour.withAlpha(0.25f + bow * 0.7f));
                    graphics.fillRoundedRectangle(
                        area.getX() + 16.0f, area.getBottom() - 6.0f,
                        8.0f + bow * 28.0f, 3.0f, 1.5f);
                }
                break;
            }
            visualizationLabel = "RESONANT MODES";
            for (int mode = 1; mode <= 12; ++mode)
            {
                const auto x = area.getX()
                    + static_cast<float>(mode)
                        * area.getWidth() / 13.0f;
                const auto harmonic = std::pow(
                    0.86f, static_cast<float>(mode - 1))
                    * (0.45f + 0.55f * value(1));
                graphics.setColour(accent.withAlpha(
                    0.18f + harmonic * 0.72f));
                graphics.drawVerticalLine(
                    juce::roundToInt(x),
                    area.getCentreY()
                        - harmonic * area.getHeight() * 0.42f,
                    area.getCentreY()
                        + harmonic * area.getHeight() * 0.42f);
                graphics.fillEllipse(
                    x - 2.5f,
                    area.getCentreY()
                        - harmonic * area.getHeight() * 0.42f - 2.5f,
                    5.0f, 5.0f);
            }
            break;

        case CompactVisualization::transport:
        {
            ContinuousTelemetrySnapshot telemetry;
            const auto valid =
                processor.readContinuousTelemetry(slot, telemetry)
                && telemetry.sequence != 0;
            if (type == ModuleType::vinylSimulator)
            {
                visualizationLabel = "LIVE RECORD SURFACE";
                const auto hasTelemetry =
                    valid && telemetry.valueCount
                        >= VinylSimulatorModule::telemetryValueCount;
                const auto phase = hasTelemetry
                    ? telemetry.values[
                          VinylSimulatorModule::transportPhase]
                    : 0.0f;
                const auto record = area.withSizeKeepingCentre(
                    area.getHeight() * 0.82f,
                    area.getHeight() * 0.82f);
                for (int groove = 1; groove <= 5; ++groove)
                {
                    const auto scale =
                        static_cast<float>(groove) / 5.0f;
                    graphics.setColour(accent.withAlpha(
                        0.08f
                        + 0.04f * static_cast<float>(groove)));
                    graphics.drawEllipse(
                        record.withSizeKeepingCentre(
                            record.getWidth() * scale,
                            record.getHeight() * scale),
                        1.0f);
                }
                const auto angle =
                    phase * juce::MathConstants<float>::twoPi;
                const auto radius = record.getWidth() * 0.42f;
                graphics.setColour(accent.withAlpha(0.9f));
                graphics.drawLine(
                    record.getCentreX(), record.getCentreY(),
                    record.getCentreX() + std::cos(angle) * radius,
                    record.getCentreY() + std::sin(angle) * radius,
                    1.5f);
                if (hasTelemetry)
                {
                    const std::array<float, 3> activities {
                        juce::jmax(
                            telemetry.values[
                                VinylSimulatorModule::leftCrackleActivity],
                            telemetry.values[
                                VinylSimulatorModule::rightCrackleActivity]),
                        telemetry.values[
                            VinylSimulatorModule::scratchActivity],
                        telemetry.values[
                            VinylSimulatorModule::surfaceNoiseActivity]
                    };
                    for (size_t event = 0; event < activities.size(); ++event)
                    {
                        const auto activity = juce::jlimit(
                            0.0f, 1.0f, activities[event]);
                        if (activity <= 0.001f)
                            continue;
                        const auto eventAngle = angle
                            + static_cast<float>(event + 1) * 2.07f;
                        graphics.setColour(
                            (event == 1 ? reductionColour : outputColour)
                                .withAlpha(0.35f + activity * 0.65f));
                        graphics.fillEllipse(
                            record.getCentreX()
                                + std::cos(eventAngle) * radius * 0.72f
                                - 2.0f - activity * 3.0f,
                            record.getCentreY()
                                + std::sin(eventAngle) * radius * 0.72f
                                - 2.0f - activity * 3.0f,
                            4.0f + activity * 6.0f,
                            4.0f + activity * 6.0f);
                    }
                }
                break;
            }

            visualizationLabel = "LIVE TAPE TRANSPORT";
            const auto hasTelemetry =
                valid && telemetry.valueCount
                    >= AnalogTapeModule::telemetryValueCount;
            const auto phase = hasTelemetry
                ? telemetry.values[AnalogTapeModule::transportPhase]
                : 0.0f;
            const auto radius = area.getHeight() * 0.32f;
            for (const auto x : {
                     area.getX() + area.getWidth() * 0.32f,
                     area.getX() + area.getWidth() * 0.68f })
            {
                graphics.setColour(accent.withAlpha(0.28f));
                graphics.drawEllipse(
                    x - radius, area.getCentreY() - radius,
                    radius * 2.0f, radius * 2.0f, 2.0f);
                graphics.setColour(accent);
                graphics.fillEllipse(
                    x - 3.0f, area.getCentreY() - 3.0f, 6.0f, 6.0f);
                const auto angle =
                    phase * juce::MathConstants<float>::twoPi
                    + (x < area.getCentreX()
                           ? 0.0f : juce::MathConstants<float>::pi);
                graphics.drawLine(
                    x, area.getCentreY(),
                    x + std::cos(angle) * radius * 0.72f,
                    area.getCentreY()
                        + std::sin(angle) * radius * 0.72f,
                    1.4f);
            }
            const auto modulation = hasTelemetry
                ? telemetry.values[
                      AnalogTapeModule::modulationMilliseconds]
                : 0.0f;
            const auto dropout = hasTelemetry
                ? juce::jlimit(
                      0.0f, 1.0f,
                      telemetry.values[
                          AnalogTapeModule::currentDropoutGain])
                : 1.0f;
            juce::Path tape;
            for (int pixel = 0;
                 pixel <= static_cast<int>(area.getWidth() * 0.36f);
                 pixel += 2)
            {
                const auto proportion = static_cast<float>(pixel)
                    / juce::jmax(1.0f, area.getWidth() * 0.36f);
                const auto x = area.getX()
                    + area.getWidth() * 0.32f
                    + static_cast<float>(pixel);
                const auto y = area.getCentreY() - radius
                    + std::sin(
                          proportion * juce::MathConstants<float>::twoPi
                          + phase * juce::MathConstants<float>::twoPi)
                        * juce::jlimit(
                            0.0f, 6.0f, std::abs(modulation));
                if (pixel == 0)
                    tape.startNewSubPath(x, y);
                else
                    tape.lineTo(x, y);
            }
            graphics.setColour(accent.withAlpha(
                0.25f + dropout * 0.7f));
            graphics.strokePath(tape, juce::PathStrokeType(1.5f));
            break;
        }

        case CompactVisualization::pitch:
            visualizationLabel = "PITCH BLOOM";
            for (int voice = 0; voice < 5; ++voice)
            {
                const auto y = area.getBottom()
                    - static_cast<float>(voice + 1)
                        * area.getHeight() / 6.0f;
                graphics.setColour(accent.withAlpha(
                    0.18f + static_cast<float>(voice) * 0.12f));
                graphics.drawLine(
                    area.getX() + static_cast<float>(voice)
                        * area.getWidth() * 0.05f,
                    y,
                    area.getRight()
                        - static_cast<float>(4 - voice)
                            * area.getWidth() * 0.04f,
                    y - value(0) * area.getHeight() * 0.12f, 1.5f);
            }
            break;

        case CompactVisualization::envelope:
            visualizationLabel = "ENVELOPE";
            drawLevel(inputLevels, inputColour.withAlpha(0.5f));
            drawLevel(outputLevels, accent);
            break;

        case CompactVisualization::cabinet:
        {
            visualizationLabel = type == ModuleType::neuralAmpModeler
                ? "AMP RESPONSE" : "CABINET RESPONSE";
            drawSpectrum(accent, true);
            const auto cone = area.withSizeKeepingCentre(
                area.getHeight() * 0.66f, area.getHeight() * 0.66f);
            graphics.setColour(juce::Colour(0xff101820).withAlpha(0.78f));
            graphics.fillEllipse(cone);
            graphics.setColour(accent.withAlpha(0.7f));
            graphics.drawEllipse(cone, 1.6f);
            graphics.fillEllipse(
                cone.withSizeKeepingCentre(
                    cone.getWidth() * 0.22f, cone.getHeight() * 0.22f));
            break;
        }

        case CompactVisualization::bitcrusher:
        {
            visualizationLabel = "QUANTIZED SIGNAL";
            juce::Path steps;
            constexpr auto stepCount = 18;
            for (int step = 0; step < stepCount; ++step)
            {
                const auto x1 = area.getX()
                    + static_cast<float>(step)
                        * area.getWidth()
                        / static_cast<float>(stepCount);
                const auto x2 = area.getX()
                    + static_cast<float>(step + 1)
                        * area.getWidth()
                        / static_cast<float>(stepCount);
                const auto sample = outputSamples[static_cast<size_t>(
                    step * static_cast<int>(outputSamples.size())
                    / stepCount)];
                const auto levels = 2.0f + value(0) * 30.0f;
                const auto quantized =
                    std::round(sample * levels) / levels;
                const auto y = area.getCentreY()
                    - quantized * area.getHeight() * 0.44f;
                if (step == 0)
                    steps.startNewSubPath(x1, y);
                else
                    steps.lineTo(x1, y);
                steps.lineTo(x2, y);
            }
            graphics.setColour(accent);
            graphics.strokePath(steps, juce::PathStrokeType(1.8f));
            break;
        }

        case CompactVisualization::chaos:
        {
            visualizationLabel = "LIVE CHAOS ATTRACTOR";
            ContinuousTelemetrySnapshot telemetry;
            const auto valid =
                processor.readContinuousTelemetry(slot, telemetry)
                && telemetry.sequence != 0
                && telemetry.valueCount
                       >= ChaosFieldModule::telemetryValueCount
                && telemetry.historyValueCount
                       >= ChaosFieldModule::telemetryHistoryValueCount
                && telemetry.historyCount > 1;
            juce::Path attractor;
            if (valid)
            {
                for (std::uint32_t point = 0;
                     point < telemetry.historyCount; ++point)
                {
                    const auto x = continuousTelemetryHistoryValue(
                        telemetry, ChaosFieldModule::xHistory, point);
                    const auto y = continuousTelemetryHistoryValue(
                        telemetry, ChaosFieldModule::yHistory, point);
                    const auto position = juce::Point<float>(
                        area.getCentreX()
                            + juce::jlimit(-1.0f, 1.0f, x)
                                * area.getWidth() * 0.44f,
                        area.getCentreY()
                            - juce::jlimit(-1.0f, 1.0f, y)
                                * area.getHeight() * 0.4f);
                    if (point == 0)
                        attractor.startNewSubPath(position);
                    else
                        attractor.lineTo(position);
                }
            }
            else
            {
                for (int point = 0; point < 180; ++point)
                {
                    const auto t =
                        static_cast<float>(point) * 0.11f;
                    const auto x = std::sin(
                        t * (1.0f + value(0) * 2.0f))
                        * std::cos(t * 0.37f);
                    const auto y = std::sin(t * 1.63f)
                        * std::cos(t * (0.23f + value(1)));
                    const auto position = juce::Point<float>(
                        area.getCentreX()
                            + x * area.getWidth() * 0.42f,
                        area.getCentreY()
                            + y * area.getHeight() * 0.4f);
                    if (point == 0)
                        attractor.startNewSubPath(position);
                    else
                        attractor.lineTo(position);
                }
            }
            graphics.setColour(accent.withAlpha(0.82f));
            graphics.strokePath(attractor, juce::PathStrokeType(1.2f));
            if (valid)
            {
                const auto current = juce::Point<float>(
                    area.getCentreX()
                        + juce::jlimit(
                              -1.0f, 1.0f,
                              telemetry.values[
                                  ChaosFieldModule::actualX])
                            * area.getWidth() * 0.44f,
                    area.getCentreY()
                        - juce::jlimit(
                              -1.0f, 1.0f,
                              telemetry.values[
                                  ChaosFieldModule::actualY])
                            * area.getHeight() * 0.4f);
                const auto wet = juce::jlimit(
                    0.0f, 1.0f,
                    std::sqrt(juce::jmax(
                        0.0f,
                        telemetry.values[ChaosFieldModule::wetRms]))
                        * 2.4f);
                graphics.setColour(outputColour.withAlpha(
                    0.45f + wet * 0.5f));
                graphics.fillEllipse(
                    juce::Rectangle<float>(
                        5.0f + wet * 5.0f,
                        5.0f + wet * 5.0f)
                        .withCentre(current));
            }
            break;
        }

        case CompactVisualization::count:
            break;
    }
    const auto labelBounds = area.removeFromTop(12.0f);
    graphics.setColour(
        backgroundColour(processor.getBackgroundThemeIndex())
            .darker(0.18f).withAlpha(0.84f));
    graphics.fillRoundedRectangle(
        labelBounds.withWidth(
            juce::jmin(
                labelBounds.getWidth(),
                static_cast<float>(
                    juce::String(visualizationLabel).length() * 6 + 8))),
        2.0f);
    graphics.setColour(juce::Colour(0xff8995a4).withAlpha(0.72f));
    graphics.setFont(juce::FontOptions(8.5f, juce::Font::bold));
    graphics.drawText(
        visualizationLabel,
        labelBounds.toNearestInt(),
        juce::Justification::topLeft);
}

void EffectGraph::paint(juce::Graphics& graphics)
{
    graphics.fillAll(moduleSurfaceColour(
        backgroundColour(processor.getBackgroundThemeIndex()).darker(0.18f),
        type, 0.16f));
    auto area = getLocalBounds().toFloat().reduced(8.0f, 3.0f);
    paintCompact(graphics, area);
    if (type != ModuleType::compressor
        && compactVisualizationShowsGainReduction(type))
    {
        const auto reduction = juce::jlimit(
            0.0f, 24.0f,
            processor.getRack().slotMeter(slot));
        if (reduction > 0.01f)
        {
            graphics.setColour(reductionColour);
            graphics.setFont(juce::FontOptions(10.0f));
            graphics.drawText(
                "GR " + juce::String(reduction, 1) + " dB",
                area.removeFromTop(16.0f).toNearestInt(),
                juce::Justification::topRight);
        }
    }
    if (processor.isSlotBypassed(slot))
    {
        auto label = area.withSizeKeepingCentre(
            juce::jmin(260.0f, area.getWidth()), 34.0f);
        graphics.setColour(juce::Colour(0xe6202731));
        graphics.fillRoundedRectangle(label, 7.0f);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        graphics.drawText(
            "BYPASSED - DSP SUSPENDED", label.toNearestInt(),
            juce::Justification::centred);
    }
    const auto readout =
        dragPrimary >= 0 && dragReadout.isNotEmpty()
            ? dragReadout : juce::String {};
    if (readout.isNotEmpty())
    {
        auto bubble = area.withSizeKeepingCentre(
            juce::jmin(300.0f, area.getWidth()), 28.0f);
        bubble.setY(area.getY() + 8.0f);
        graphics.setColour(juce::Colour(0xdd242c37));
        graphics.fillRoundedRectangle(bubble, 5.0f);
        graphics.setColour(juce::Colours::white);
        graphics.drawFittedText(
            readout, bubble.reduced(7.0f, 0.0f).toNearestInt(),
            juce::Justification::centred, 1, 0.72f);
    }
    if (hasKeyboardFocus(false))
    {
        graphics.setColour(modulePaletteColour(type));
        graphics.drawRoundedRectangle(
            getLocalBounds().toFloat().reduced(2.5f), 7.0f, 2.0f);
    }
}

void EffectGraph::mouseDown(const juce::MouseEvent& event)
{
    if (isCompactAssetLibrary())
    {
        grabKeyboardFocus();
        const auto index = compactAssetAt(event.position);
        if (index >= 0)
        {
            if (isCompactNamLibrary()
                && event.position.x
                       >= static_cast<float>(getWidth() - 40))
            {
                deleteCompactNamModel(index);
            }
            else if (event.position.x
                         >= static_cast<float>(
                             getWidth()
                             - (isCompactNamLibrary() ? 64 : 40)))
            {
                toggleCompactAssetFavorite(index);
            }
            else
                activateCompactAsset(index);
        }
    }
    else if (type == ModuleType::compressor
             && compactCompressorLevelArea().contains(event.position))
    {
        grabKeyboardFocus();
        dragPrimary = 0;
        beginGestures();
        setCompactCompressorThresholdFromY(event.position.y);
    }
    else if (type == ModuleType::equalizer)
    {
        if (compactEqualizerOutputArea().contains(event.position))
        {
            grabKeyboardFocus();
            dragPrimary = 9;
            beginGestures();
            setCompactEqualizerOutputFromY(event.position.y);
            return;
        }
        const auto modeArea = compactEqualizerModeArea();
        if ((compactEqualizerBand == 0
             || compactEqualizerBand == 2)
            && modeArea.contains(event.position))
        {
            const auto mode = juce::jlimit(
                0, 2,
                static_cast<int>(
                    (event.position.x - modeArea.getX())
                    / juce::jmax(1.0f, modeArea.getWidth())
                    * 3.0f));
            dragPrimary =
                compactEqualizerBand == 0 ? 10 : 11;
            beginGestures();
            setValue(dragPrimary, static_cast<float>(mode) / 2.0f);
            endGestures();
            repaint();
            return;
        }
        const auto band = compactEqualizerBandAt(event.position);
        if (band >= 0)
        {
            grabKeyboardFocus();
            compactEqualizerBand = band;
            dragPrimary = band * 3;
            const auto mode = band == 1
                ? EqualizerBandMode::bell
                : equalizerBandMode(value(band == 0 ? 10 : 11));
            dragSecondary = band * 3
                + (mode == EqualizerBandMode::cut ? 2 : 1);
            beginGestures();
            setCompactEqualizerBandFromPoint(band, event.position);
        }
    }
}

void EffectGraph::mouseDrag(const juce::MouseEvent& event)
{
    if (type == ModuleType::compressor && dragPrimary == 0)
        setCompactCompressorThresholdFromY(event.position.y);
    else if (type == ModuleType::equalizer
             && dragPrimary >= 0)
    {
        if (dragPrimary == 9)
            setCompactEqualizerOutputFromY(event.position.y);
        else if (compactEqualizerBand >= 0)
            setCompactEqualizerBandFromPoint(
                compactEqualizerBand, event.position);
    }
}

void EffectGraph::mouseUp(const juce::MouseEvent&)
{
    endGestures();
    dragReadout.clear();
    repaint();
}

void EffectGraph::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (type == ModuleType::compressor
        && compactCompressorLevelArea().contains(event.position))
    {
        setValue(0, moduleDefaults(type)[0]);
        dragReadout = formatControlValue(type, 0, value(0));
        repaint();
    }
    else if (type == ModuleType::equalizer)
    {
        if (compactEqualizerOutputArea().contains(event.position))
        {
            setValue(9, moduleDefaults(type)[9]);
            repaint();
            return;
        }
        const auto band = compactEqualizerBandAt(event.position);
        if (band >= 0)
        {
            compactEqualizerBand = band;
            const auto defaults = moduleDefaults(type);
            for (int offset = 0; offset < 3; ++offset)
                setValue(
                    band * 3 + offset,
                    defaults[static_cast<size_t>(
                        band * 3 + offset)]);
            if (band != 1)
                setValue(
                    band == 0 ? 10 : 11,
                    defaults[static_cast<size_t>(
                        band == 0 ? 10 : 11)]);
            repaint();
        }
    }
}

void EffectGraph::mouseWheelMove(
    const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (isCompactAssetLibrary())
    {
        const auto movement =
            wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX;
        if (movement != 0.0f)
        {
            const auto rows = juce::jmax(
                1, juce::roundToInt(std::abs(movement) * 4.0f));
            scrollCompactAssetLibrary(movement > 0.0f ? -rows : rows);
            return;
        }
    }
    if (type == ModuleType::equalizer)
    {
        const auto hit = compactEqualizerBandAt(event.position);
        const auto band = hit >= 0 ? hit : compactEqualizerBand;
        if (band >= 0)
        {
            compactEqualizerBand = band;
            dragPrimary = band * 3 + 2;
            beginGestures();
            const auto movement =
                wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX;
            setValue(
                dragPrimary,
                value(dragPrimary) + movement * 0.12f);
            endGestures();
            repaint();
            return;
        }
    }
    juce::Component::mouseWheelMove(event, wheel);
}

bool EffectGraph::keyPressed(const juce::KeyPress& key)
{
    if (isCompactAssetLibrary())
    {
        if (key.getKeyCode() == juce::KeyPress::upKey
            || key.getKeyCode() == juce::KeyPress::downKey)
        {
            if (compactAssetCount() > 0)
            {
                const auto direction =
                    key.getKeyCode() == juce::KeyPress::upKey ? -1 : 1;
                compactAssetSelected = juce::jlimit(
                    0, compactAssetCount() - 1,
                    compactAssetSelected < 0
                        ? 0 : compactAssetSelected + direction);
                const auto visibleRows = compactAssetVisibleRowCount();
                if (compactAssetSelected < compactAssetFirstVisible)
                    compactAssetFirstVisible = compactAssetSelected;
                else if (compactAssetSelected
                         >= compactAssetFirstVisible + visibleRows)
                    compactAssetFirstVisible =
                        compactAssetSelected - visibleRows + 1;
                repaint();
            }
            return true;
        }
        if (key.getKeyCode() == juce::KeyPress::returnKey)
        {
            activateCompactAsset(compactAssetSelected);
            return true;
        }
    }
    if (type == ModuleType::compressor)
    {
        if (key.getKeyCode() == juce::KeyPress::upKey
            || key.getKeyCode() == juce::KeyPress::downKey)
        {
            const auto direction =
                key.getKeyCode() == juce::KeyPress::upKey
                ? 1.0f : -1.0f;
            const auto step =
                key.getModifiers().isShiftDown() ? 1.0f / 600.0f
                                                 : 1.0f / 120.0f;
            setValue(0, value(0) + direction * step);
            dragReadout = formatControlValue(type, 0, value(0));
            repaint();
            return true;
        }
        if (key.getKeyCode() == juce::KeyPress::homeKey)
        {
            setValue(0, moduleDefaults(type)[0]);
            dragReadout = formatControlValue(type, 0, value(0));
            repaint();
            return true;
        }
        return false;
    }
    if (type == ModuleType::equalizer)
    {
        if (key.getKeyCode() == juce::KeyPress::leftKey
            || key.getKeyCode() == juce::KeyPress::rightKey)
        {
            const auto direction =
                key.getKeyCode() == juce::KeyPress::rightKey ? 1 : -1;
            compactEqualizerBand = juce::jlimit(
                0, 2, compactEqualizerBand + direction);
            repaint();
            return true;
        }
        if (key.getKeyCode() == juce::KeyPress::upKey
            || key.getKeyCode() == juce::KeyPress::downKey)
        {
            const auto band = juce::jlimit(
                0, 2, compactEqualizerBand);
            const auto mode = band == 1
                ? EqualizerBandMode::bell
                : equalizerBandMode(value(band == 0 ? 10 : 11));
            const auto control =
                band * 3 + (mode == EqualizerBandMode::cut ? 2 : 1);
            const auto direction =
                key.getKeyCode() == juce::KeyPress::upKey ? 1.0f : -1.0f;
            const auto step =
                key.getModifiers().isShiftDown() ? 0.0025f : 0.01f;
            setValue(control, value(control) + direction * step);
            repaint();
            return true;
        }
        if (key.getKeyCode() == juce::KeyPress::spaceKey
            || key.getKeyCode() == juce::KeyPress::returnKey)
        {
            if (compactEqualizerBand == 0
                || compactEqualizerBand == 2)
            {
                const auto control =
                    compactEqualizerBand == 0 ? 10 : 11;
                const auto next =
                    (static_cast<int>(
                         equalizerBandMode(value(control)))
                     + 1) % 3;
                setValue(control, static_cast<float>(next) / 2.0f);
                repaint();
            }
            return true;
        }
        if (key.getKeyCode() == juce::KeyPress::homeKey)
        {
            const auto defaults = moduleDefaults(type);
            const auto band = juce::jlimit(
                0, 2, compactEqualizerBand);
            for (int offset = 0; offset < 3; ++offset)
                setValue(
                    band * 3 + offset,
                    defaults[static_cast<size_t>(band * 3 + offset)]);
            if (band != 1)
                setValue(
                    band == 0 ? 10 : 11,
                    defaults[static_cast<size_t>(
                        band == 0 ? 10 : 11)]);
            repaint();
            return true;
        }
        return false;
    }
    return false;
}

void EffectGraph::focusGained(FocusChangeType)
{
    repaint();
}

void EffectGraph::focusLost(FocusChangeType)
{
    repaint();
}

void EffectGraph::setVisualizationActive(bool active)
{
    const auto changed = visualizationActive != active;
    visualizationActive = active;
    const auto needsCapture =
        active && !isCompactAssetLibrary();
    processor.getVisualizationData().setSlotActive(slot, needsCapture);
    if (changed)
        repaint();
}

float EffectGraph::value(int control) const
{
    if (auto* raw = processor.parameters.getRawParameterValue(
            controlParameterId(slot, control)))
        return raw->load();
    return 0.5f;
}

juce::RangedAudioParameter* EffectGraph::parameter(int control) const
{
    return processor.parameters.getParameter(
        controlParameterId(slot, control));
}

void EffectGraph::setValue(int control, float normalized)
{
    if (auto* target = parameter(control))
        target->setValueNotifyingHost(
            juce::jlimit(0.0f, 1.0f, normalized));
}

void EffectGraph::beginGestures()
{
    if (auto* target = parameter(dragPrimary))
        target->beginChangeGesture();
    if (dragSecondary >= 0)
        if (auto* target = parameter(dragSecondary))
            target->beginChangeGesture();
}

void EffectGraph::endGestures()
{
    if (dragPrimary >= 0)
        if (auto* target = parameter(dragPrimary))
            target->endChangeGesture();
    if (dragSecondary >= 0)
        if (auto* target = parameter(dragSecondary))
            target->endChangeGesture();
    dragPrimary = dragSecondary = -1;
}

void EffectGraph::timerCallback()
{
    if (!isShowing())
        return;
    if (isCompactAssetLibrary()
        && compactAssetLibraryGeneration
               != processor.assetLibraryGeneration())
    {
        refreshCompactAssetLibrary();
        repaint();
    }
    if (visualizationActive
        && !isCompactAssetLibrary()
        && !processor.getVisualizationData().isSlotActive(slot))
        processor.getVisualizationData().setSlotActive(slot, true);
    refreshVisualization();
}

void EffectGraph::refreshVisualization()
{
    if (isCompactAssetLibrary())
    {
        repaint();
        return;
    }
    if (processor.isSlotBypassed(slot))
    {
        if (!bypassVisualsSuspended)
        {
            if (visualizationActive)
                processor.getVisualizationData().slotData(slot).clear();
            inputSamples.fill(0.0f);
            outputSamples.fill(0.0f);
            stereoLeftSamples.fill(0.0f);
            stereoRightSamples.fill(0.0f);
            gainHistory.fill(0.0f);
            inputLevels.fill(-60.0f);
            outputLevels.fill(-60.0f);
            gainReductionLevels.fill(0.0f);
            inputSpectrum.fill(-100.0f);
            outputSpectrum.fill(-100.0f);
            bypassVisualsSuspended = true;
            repaint();
        }
        return;
    }
    bypassVisualsSuspended = false;

    if (!visualizationActive)
    {
        repaint();
        return;
    }

    auto& data = processor.getVisualizationData().slotData(slot);
    const auto inputHistoryPosition = data.input.position();
    const auto outputHistoryPosition = data.output.position();
    const auto inputAdvanced =
        inputHistoryPosition != lastInputHistoryPosition;
    const auto outputAdvanced =
        outputHistoryPosition != lastOutputHistoryPosition;
    lastInputHistoryPosition = inputHistoryPosition;
    lastOutputHistoryPosition = outputHistoryPosition;
    data.input.copyLatest(inputSamples);
    data.output.copyLatest(outputSamples);
    data.outputLeft.copyLatest(stereoLeftSamples);
    data.outputRight.copyLatest(stereoRightSamples);
    data.gainReduction.copyLatest(gainHistory);
    data.inputLevel.copyLatest(inputLevels, -60.0f);
    data.outputLevel.copyLatest(outputLevels, -60.0f);
    data.gainReductionLevel.copyLatest(gainReductionLevels);
    const auto compactVisualizationType =
        compactVisualization(type);
    const auto compactNeedsSpectrum =
        compactVisualizationType
            == CompactVisualization::response
        || compactVisualizationType
            == CompactVisualization::spectral
        || compactVisualizationType
            == CompactVisualization::cabinet;
    if (compactNeedsSpectrum)
    {
        const auto decayStaleSpectrum =
            [](auto& spectrum)
            {
                for (auto& magnitude : spectrum)
                    magnitude = juce::jmax(
                        -100.0f, magnitude - 5.0f);
            };
        if (type == ModuleType::neuralAmpModeler && !inputAdvanced)
            decayStaleSpectrum(inputSpectrum);
        else
            calculateSpectrum(inputSamples, inputSpectrum);
        if (type == ModuleType::neuralAmpModeler && !outputAdvanced)
            decayStaleSpectrum(outputSpectrum);
        else
            calculateSpectrum(outputSamples, outputSpectrum);
    }
    repaint();
}

#if defined(MEGADSP_TESTS)
void EffectGraph::refreshVisualizationForTesting()
{
    refreshVisualization();
}

void EffectGraph::activateCompactNamModelForTesting(int index)
{
    activateCompactAsset(index);
}

void EffectGraph::scrollCompactNamLibraryForTesting(int rows)
{
    scrollCompactAssetLibrary(rows);
}

void EffectGraph::setCompactAssetFilterForTesting(
    const juce::String& filter)
{
    compactAssetSearch.setText(filter, false);
    refreshCompactAssetLibrary(true);
}

void EffectGraph::setCompactNamFavoritesOnlyForTesting(bool favoritesOnly)
{
    compactNamFavoriteFilter.setToggleState(
        favoritesOnly, juce::dontSendNotification);
    refreshCompactAssetLibrary(true);
}

void EffectGraph::toggleCompactNamFavoriteForTesting(int index)
{
    toggleCompactAssetFavorite(index);
}

void EffectGraph::setCompactImpulseFavoritesOnlyForTesting(bool favoritesOnly)
{
    compactNamFavoriteFilter.setToggleState(
        favoritesOnly, juce::dontSendNotification);
    refreshCompactAssetLibrary(true);
}

void EffectGraph::toggleCompactImpulseFavoriteForTesting(int index)
{
    toggleCompactAssetFavorite(index);
}

void EffectGraph::activateCompactImpulseResponseForTesting(int index)
{
    activateCompactAsset(index);
}
#endif

juce::Rectangle<float> EffectGraph::compactCompressorLevelArea() const
{
    auto content =
        getLocalBounds().toFloat().reduced(8.0f, 3.0f);
    content.removeFromTop(14.0f);
    content.removeFromBottom(content.getHeight() * 0.3f);
    content.removeFromBottom(5.0f);
    return content;
}

void EffectGraph::setCompactCompressorThresholdFromY(float y)
{
    const auto area = compactCompressorLevelArea();
    if (area.isEmpty())
        return;
    const auto normalized = 1.0f
        - (y - area.getY()) / area.getHeight();
    setValue(0, juce::jlimit(0.0f, 1.0f, normalized));
    dragReadout = formatControlValue(type, 0, value(0));
    repaint();
}

juce::Rectangle<float> EffectGraph::compactEqualizerPlotArea() const
{
    auto area = getLocalBounds().toFloat().reduced(8.0f, 3.0f);
    area.removeFromTop(14.0f);
    area.removeFromRight(28.0f);
    area.removeFromBottom(20.0f);
    return area.reduced(8.0f, 4.0f);
}

juce::Rectangle<float> EffectGraph::compactEqualizerModeArea() const
{
    auto area = getLocalBounds().toFloat().reduced(8.0f, 3.0f);
    area.removeFromTop(14.0f);
    area.removeFromRight(28.0f);
    return area.removeFromBottom(18.0f).reduced(8.0f, 1.0f);
}

juce::Rectangle<float> EffectGraph::compactEqualizerOutputArea() const
{
    auto area = getLocalBounds().toFloat().reduced(8.0f, 3.0f);
    area.removeFromTop(14.0f);
    return area.removeFromRight(24.0f).reduced(2.0f, 4.0f);
}

juce::Point<float> EffectGraph::compactEqualizerPoint(int band) const
{
    const auto plot = compactEqualizerPlotArea();
    if (!juce::isPositiveAndBelow(band, 3) || plot.isEmpty())
        return plot.getCentre();
    static constexpr std::array<float, 3> lows {
        30.0f, 150.0f, 1500.0f
    };
    static constexpr std::array<float, 3> highs {
        1200.0f, 7000.0f, 20000.0f
    };
    const auto frequency = exponential(
        lows[static_cast<size_t>(band)],
        highs[static_cast<size_t>(band)],
        value(band * 3));
    const auto frequencyPosition =
        std::log(frequency / 20.0f) / std::log(1000.0f);
    const auto x = plot.getX()
        + juce::jlimit(0.0f, 1.0f, frequencyPosition)
            * plot.getWidth();
    const auto mode = band == 1
        ? EqualizerBandMode::bell
        : equalizerBandMode(value(band == 0 ? 10 : 11));
    const auto verticalControl =
        band * 3 + (mode == EqualizerBandMode::cut ? 2 : 1);
    const auto y = plot.getBottom()
        - juce::jlimit(0.0f, 1.0f, value(verticalControl))
            * plot.getHeight();
    return { x, y };
}

juce::dsp::IIR::Coefficients<float>::Ptr
EffectGraph::compactEqualizerCoefficients(int band) const
{
    static constexpr std::array<float, 3> lows {
        30.0f, 150.0f, 1500.0f
    };
    static constexpr std::array<float, 3> highs {
        1200.0f, 7000.0f, 20000.0f
    };
    const auto sampleRate = processor.getSampleRate() > 1000.0
        ? processor.getSampleRate() : 48000.0;
    band = juce::jlimit(0, 2, band);
    const auto centre = exponential(
        lows[static_cast<size_t>(band)],
        juce::jmin(
            highs[static_cast<size_t>(band)],
            static_cast<float>(sampleRate * 0.475)),
        value(band * 3));
    const auto gainDb =
        lerp(-18.0f, 18.0f, value(band * 3 + 1));
    const auto q =
        exponential(0.2f, 10.0f, value(band * 3 + 2));
    const auto mode = band == 1
        ? EqualizerBandMode::bell
        : equalizerBandMode(value(band == 0 ? 10 : 11));
    if (mode == EqualizerBandMode::cut)
        return band == 0
            ? juce::dsp::IIR::Coefficients<float>::makeHighPass(
                  sampleRate, centre, q)
            : juce::dsp::IIR::Coefficients<float>::makeLowPass(
                  sampleRate, centre, q);
    if (mode == EqualizerBandMode::shelf)
        return band == 0
            ? juce::dsp::IIR::Coefficients<float>::makeLowShelf(
                  sampleRate, centre, q,
                  juce::Decibels::decibelsToGain(gainDb))
            : juce::dsp::IIR::Coefficients<float>::makeHighShelf(
                  sampleRate, centre, q,
                  juce::Decibels::decibelsToGain(gainDb));
    return juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        sampleRate, centre, q,
        juce::Decibels::decibelsToGain(gainDb));
}

float EffectGraph::compactEqualizerResponseDb(
    float frequency, int onlyBand) const
{
    const auto sampleRate = processor.getSampleRate() > 1000.0
        ? processor.getSampleRate() : 48000.0;
    const auto measuredFrequency = juce::jlimit(
        1.0, sampleRate * 0.475,
        static_cast<double>(frequency));
    auto responseDb = 0.0f;
    for (int band = 0; band < 3; ++band)
    {
        if (onlyBand >= 0 && band != onlyBand)
            continue;
        responseDb += static_cast<float>(
            juce::Decibels::gainToDecibels(
                compactEqualizerCoefficients(band)
                    ->getMagnitudeForFrequency(
                        measuredFrequency, sampleRate),
                -72.0));
    }
    return responseDb;
}

juce::String EffectGraph::compactEqualizerBandReadout(int band) const
{
    if (!juce::isPositiveAndBelow(band, 3))
        return {};
    const auto mode = band == 1
        ? EqualizerBandMode::bell
        : equalizerBandMode(value(band == 0 ? 10 : 11));
    const auto bandName =
        band == 0 ? "LOW" : band == 1 ? "MID" : "HIGH";
    const auto modeName =
        mode == EqualizerBandMode::cut
            ? band == 0 ? "HP" : "LP"
        : mode == EqualizerBandMode::shelf
            ? band == 0 ? "LS" : "HS"
            : "BELL";
    auto readout = juce::String(bandName) + " " + modeName + "  "
        + formatControlValue(type, band * 3, value(band * 3));
    if (mode != EqualizerBandMode::cut)
        readout += "  "
            + formatControlValue(
                type, band * 3 + 1, value(band * 3 + 1));
    readout += "  Q "
        + formatControlValue(
            type, band * 3 + 2, value(band * 3 + 2));
    return readout;
}

int EffectGraph::compactEqualizerBandAt(
    juce::Point<float> point) const
{
    int closest = -1;
    auto distance = 18.0f;
    for (int band = 0; band < 3; ++band)
    {
        const auto candidate =
            compactEqualizerPoint(band).getDistanceFrom(point);
        if (candidate <= distance)
        {
            closest = band;
            distance = candidate;
        }
    }
    return closest;
}

void EffectGraph::setCompactEqualizerBandFromPoint(
    int band, juce::Point<float> point)
{
    const auto plot = compactEqualizerPlotArea();
    if (!juce::isPositiveAndBelow(band, 3) || plot.isEmpty())
        return;
    const auto horizontal = juce::jlimit(
        0.0f, 1.0f,
        (point.x - plot.getX()) / plot.getWidth());
    static constexpr std::array<float, 3> lows {
        30.0f, 150.0f, 1500.0f
    };
    static constexpr std::array<float, 3> highs {
        1200.0f, 7000.0f, 20000.0f
    };
    const auto frequency = 20.0f * std::pow(1000.0f, horizontal);
    const auto normalizedFrequency = juce::jlimit(
        0.0f, 1.0f,
        std::log(frequency / lows[static_cast<size_t>(band)])
            / std::log(highs[static_cast<size_t>(band)]
                       / lows[static_cast<size_t>(band)]));
    const auto vertical = juce::jlimit(
        0.0f, 1.0f,
        1.0f - (point.y - plot.getY()) / plot.getHeight());
    setValue(band * 3, normalizedFrequency);
    const auto mode = band == 1
        ? EqualizerBandMode::bell
        : equalizerBandMode(value(band == 0 ? 10 : 11));
    const auto verticalControl =
        band * 3 + (mode == EqualizerBandMode::cut ? 2 : 1);
    setValue(
        verticalControl, vertical);
    dragReadout = formatControlValue(
        type, band * 3, value(band * 3))
        + "  "
        + formatControlValue(
            type, verticalControl, value(verticalControl));
    repaint();
}

void EffectGraph::setCompactEqualizerOutputFromY(float y)
{
    const auto area = compactEqualizerOutputArea();
    if (area.isEmpty())
        return;
    setValue(
        9,
        1.0f - (y - area.getY()) / area.getHeight());
    dragReadout = formatControlValue(type, 9, value(9));
    repaint();
}

#if defined(MEGADSP_TESTS)
void EffectGraph::setCompactCompressorThresholdFromYForTesting(
    float y)
{
    setCompactCompressorThresholdFromY(y);
}

void EffectGraph::setCompactEqualizerBandFromPointForTesting(
    int band, juce::Point<float> point)
{
    setCompactEqualizerBandFromPoint(band, point);
}

void EffectGraph::setCompactEqualizerOutputFromYForTesting(float y)
{
    setCompactEqualizerOutputFromY(y);
}
#endif

std::unique_ptr<juce::AccessibilityHandler>
EffectGraph::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler>(
        *this, juce::AccessibilityRole::image);
}

void EffectGraph::calculateSpectrum(
    const std::array<float, graphFftSize>& samples,
    std::array<float, graphFftSize / 2>& destination)
{
    fftBuffer.fill(0.0f);
    const auto mean =
        std::accumulate(samples.begin(), samples.end(), 0.0)
        / static_cast<double>(samples.size());
    auto peak = 0.0f;
    for (size_t index = 0; index < samples.size(); ++index)
    {
        const auto centred = std::isfinite(samples[index])
            ? samples[index] - static_cast<float>(mean) : 0.0f;
        fftBuffer[index] = centred;
        peak = juce::jmax(peak, std::abs(centred));
    }
    if (peak <= juce::Decibels::decibelsToGain(-100.0f))
    {
        destination.fill(-100.0f);
        return;
    }
    window.multiplyWithWindowingTable(fftBuffer.data(), graphFftSize);
    fft.performFrequencyOnlyForwardTransform(fftBuffer.data());
    for (int bin = 0; bin < graphFftSize / 2; ++bin)
    {
        const auto magnitude =
            fftBuffer[static_cast<size_t>(bin)]
            / static_cast<float>(graphFftSize);
        destination[static_cast<size_t>(bin)] =
            std::isfinite(magnitude)
            ? juce::Decibels::gainToDecibels(magnitude, -100.0f)
            : -100.0f;
    }
}

size_t EffectGraph::spectrumBinForProportion(
    float proportion, double sampleRate)
{
    const auto effectiveSampleRate =
        sampleRate > 1000.0 ? sampleRate : 48000.0;
    constexpr auto minimumFrequency = 20.0;
    const auto maximumFrequency =
        juce::jmax(minimumFrequency,
                      juce::jmin(20000.0, effectiveSampleRate * 0.5));
    const auto frequency = minimumFrequency
        * std::pow(maximumFrequency / minimumFrequency,
                      static_cast<double>(juce::jlimit(
                          0.0f, 1.0f, proportion)));
    const auto bin = static_cast<size_t>(std::lround(
        frequency * static_cast<double>(graphFftSize)
        / effectiveSampleRate));
    return juce::jlimit(
        size_t { 1 }, size_t { graphFftSize / 2 - 1 }, bin);
}

} // namespace megadsp::ui
