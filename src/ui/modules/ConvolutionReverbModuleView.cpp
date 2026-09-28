#include "../GraphStyle.h"
#include "../ModuleView.h"

#include "ModuleViewCreators.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace megadsp::ui
{
namespace
{
class ConvolutionReverbModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    void configureAccessibility(juce::Component&) const override;
    bool usesFullPanel() const override { return true; }
    int preferredCardHeight() const override
    {
        return moduleCardMaximumHeight;
    }
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void timerCallback() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    enum class ViewMode
    {
        room,
        analyzer,
        history
    };

    static juce::Rectangle<float> waveformColumn(
        juce::Rectangle<float>);
    static juce::Rectangle<float> libraryBounds(
        juce::Rectangle<float>);
    static juce::Rectangle<float> waveformBounds(
        juce::Rectangle<float>);
    static juce::Rectangle<float> passbandBounds(
        juce::Rectangle<float>);
    static juce::Rectangle<float> controlsBounds(
        juce::Rectangle<float>);
    static juce::Rectangle<float> railBounds(
        juce::Rectangle<float>, int);
    static juce::Rectangle<float> railTrackBounds(
        juce::Rectangle<float>, int);
    juce::Rectangle<float> searchBounds(
        juce::Rectangle<float>) const;
    juce::Rectangle<float> rootBounds(
        juce::Rectangle<float>) const;
    juce::Rectangle<float> bankBounds(
        juce::Rectangle<float>, int) const;
    juce::Rectangle<float> filterBounds(
        juce::Rectangle<float>, int) const;
    juce::Rectangle<float> bufferBounds(
        juce::Rectangle<float>, int) const;
    juce::Rectangle<float> resultBounds(
        juce::Rectangle<float>, int) const;
    juce::String searchQuery() const;
    void refreshResults();
    void chooseRootDirectory();
    void paintLibrary(
        juce::Graphics&, juce::Rectangle<float>);

    static constexpr int spectrogramFrameCount = 120;
    static constexpr int browserRowsPerPage = 10;
    using SpectrumFrame =
        std::array<float, ConvolutionReverbModule::spectrumBandCount>;
    std::array<SpectrumFrame, spectrogramFrameCount> spectrogram {};
    std::uint64_t lastTelemetrySequence = 0;
    int spectrogramWritePosition = 0;
    int spectrogramCount = 0;
    ViewMode viewMode = ViewMode::history;
    juce::String searchText;
    std::array<bool, 5> filters {};
    std::vector<ImpulseResponseLibraryEntry> results;
    std::unique_ptr<juce::FileChooser> rootChooser;
    int resultPage = 0;
    bool searchFocused = false;
    bool searchDirty = true;
};

void ConvolutionReverbModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Convolution reverb impulse response");
    component.setHelpText(
        "Set a persisted root directory, type in the inline search field, "
        "filter by channel layout or duration, and click a result to load it "
        "into the selected A, B, or C audition bank. Switching banks uses "
        "already prepared convolution engines. You can also drop a WAV, "
        "AIFF, or FLAC impulse response file here. "
        "The live wet spectrogram shows the reverb's measured frequency "
        "energy over the last four seconds, independent of the Wet level. "
        "Low frequencies are at the bottom and newest audio is at the right. "
        "It has a separate Low Cut and High Cut passband strip. "
        "The adjacent Dry, Wet, Ducking, and Output Trim rails are directly "
        "editable. Double-click any rail or passband handle to restore its "
        "default.");
}

juce::Rectangle<float> ConvolutionReverbModuleView::controlsBounds(
    juce::Rectangle<float> area)
{
    const auto width = juce::jlimit(
        220.0f, 300.0f, area.getWidth() * 0.29f);
    return area.removeFromRight(width);
}

juce::Rectangle<float> ConvolutionReverbModuleView::waveformColumn(
    juce::Rectangle<float> area)
{
    const auto controls = controlsBounds(area);
    area.removeFromRight(controls.getWidth() + 10.0f);
    const auto browserWidth = juce::jlimit(
        260.0f, 340.0f, area.getWidth() * 0.38f);
    area.removeFromLeft(browserWidth + 10.0f);
    return area;
}

juce::Rectangle<float> ConvolutionReverbModuleView::libraryBounds(
    juce::Rectangle<float> area)
{
    const auto controls = controlsBounds(area);
    area.removeFromRight(controls.getWidth() + 10.0f);
    const auto browserWidth = juce::jlimit(
        260.0f, 340.0f, area.getWidth() * 0.38f);
    return area.removeFromLeft(browserWidth);
}

juce::Rectangle<float> ConvolutionReverbModuleView::waveformBounds(
    juce::Rectangle<float> area)
{
    auto column = waveformColumn(area);
    column.removeFromTop(23.0f);
    column.removeFromBottom(42.0f);
    return column;
}

juce::Rectangle<float> ConvolutionReverbModuleView::passbandBounds(
    juce::Rectangle<float> area)
{
    auto column = waveformColumn(area);
    return column.removeFromBottom(36.0f);
}

juce::Rectangle<float> ConvolutionReverbModuleView::railBounds(
    juce::Rectangle<float> area, int index)
{
    auto rails = controlsBounds(area).reduced(7.0f);
    rails.removeFromTop(22.0f);
    const auto width = rails.getWidth() * 0.25f;
    return juce::Rectangle<float>(
        rails.getX() + width * static_cast<float>(index),
        rails.getY(), width, rails.getHeight()).reduced(4.0f, 0.0f);
}

juce::Rectangle<float> ConvolutionReverbModuleView::railTrackBounds(
    juce::Rectangle<float> area, int index)
{
    auto rail = railBounds(area, index);
    rail.removeFromTop(22.0f);
    rail.removeFromBottom(21.0f);
    return rail.reduced(rail.getWidth() * 0.34f, 2.0f);
}

juce::Rectangle<float> ConvolutionReverbModuleView::rootBounds(
    juce::Rectangle<float> area) const
{
    auto library = libraryBounds(area).reduced(10.0f);
    return library.removeFromTop(30.0f);
}

juce::Rectangle<float> ConvolutionReverbModuleView::bankBounds(
    juce::Rectangle<float> area, int bank) const
{
    auto library = libraryBounds(area).reduced(10.0f);
    library.removeFromTop(38.0f);
    auto row = library.removeFromTop(38.0f);
    const auto width = row.getWidth() / 3.0f;
    return { row.getX() + width * static_cast<float>(bank),
             row.getY(), width - 3.0f, row.getHeight() };
}

juce::Rectangle<float> ConvolutionReverbModuleView::searchBounds(
    juce::Rectangle<float> area) const
{
    auto library = libraryBounds(area).reduced(10.0f);
    library.removeFromTop(84.0f);
    return library.removeFromTop(38.0f);
}

juce::Rectangle<float> ConvolutionReverbModuleView::filterBounds(
    juce::Rectangle<float> area, int filter) const
{
    auto library = libraryBounds(area).reduced(10.0f);
    library.removeFromTop(130.0f);
    auto row = library.removeFromTop(30.0f);
    const auto width = row.getWidth() / 5.0f;
    return { row.getX() + width * static_cast<float>(filter),
             row.getY(), width - 2.0f, row.getHeight() };
}

juce::Rectangle<float> ConvolutionReverbModuleView::resultBounds(
    juce::Rectangle<float> area, int rowIndex) const
{
    auto library = libraryBounds(area).reduced(10.0f);
    library.removeFromTop(218.0f);
    library.removeFromBottom(28.0f);
    const auto rowHeight =
        library.getHeight() / static_cast<float>(browserRowsPerPage);
    return { library.getX(),
             library.getY()
                 + rowHeight * static_cast<float>(rowIndex),
             library.getWidth(), juce::jmax(1.0f, rowHeight - 3.0f) };
}

juce::Rectangle<float> ConvolutionReverbModuleView::bufferBounds(
    juce::Rectangle<float> area, int choice) const
{
    auto library = libraryBounds(area).reduced(10.0f);
    library.removeFromTop(180.0f);
    auto row = library.removeFromTop(30.0f);
    const auto width = row.getWidth() / 4.0f;
    return { row.getX() + width * static_cast<float>(choice),
             row.getY(), width - 3.0f, row.getHeight() };
}

juce::String ConvolutionReverbModuleView::searchQuery() const
{
    static constexpr std::array<const char*, 5> tokens {
        "mono", "stereo", "short", "medium", "long"
    };
    auto query = searchText.trim();
    for (size_t index = 0; index < filters.size(); ++index)
        if (filters[index])
            query += (query.isEmpty() ? "" : " ")
                + juce::String(tokens[index]);
    return query;
}

void ConvolutionReverbModuleView::refreshResults()
{
    results = processor.searchImpulseResponses(searchQuery(), 200);
    const auto pageCount = juce::jmax(
        1, static_cast<int>(
               (results.size() + browserRowsPerPage - 1)
               / browserRowsPerPage));
    resultPage = juce::jlimit(0, pageCount - 1, resultPage);
    searchDirty = false;
}

void ConvolutionReverbModuleView::chooseRootDirectory()
{
    rootChooser = std::make_unique<juce::FileChooser>(
        "Choose impulse response root",
        processor.impulseResponseRootDirectory());
    juce::Component::SafePointer<EffectGraph> safeGraph(&graph);
    rootChooser->launchAsync(
        juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectDirectories,
        [this, safeGraph](const juce::FileChooser& chooser)
        {
            if (safeGraph == nullptr)
                return;
            const auto root = chooser.getResult();
            if (root.isDirectory())
            {
                const auto result =
                    processor.setImpulseResponseRootDirectory(root);
                if (result.failed())
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::WarningIcon,
                        "Could not index IR library",
                        result.getErrorMessage());
                else
                {
                    resultPage = 0;
                    searchDirty = true;
                    repaint();
                }
            }
        });
}

void ConvolutionReverbModuleView::mouseDown(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto library = libraryBounds(area);
    if (library.contains(event.position))
    {
        if (rootBounds(area).contains(event.position))
        {
            chooseRootDirectory();
            return;
        }
        for (int bank = 0;
             bank < ConvolutionReverbModule::impulseBankCount; ++bank)
            if (bankBounds(area, bank).contains(event.position))
            {
                processor.selectConvolutionImpulseResponseBank(
                    slot, bank);
                repaint();
                return;
            }
        if (searchBounds(area).contains(event.position))
        {
            searchFocused = true;
            repaint();
            return;
        }
        searchFocused = false;
        for (int filter = 0; filter < 5; ++filter)
            if (filterBounds(area, filter).contains(event.position))
            {
                filters[static_cast<size_t>(filter)] =
                    !filters[static_cast<size_t>(filter)];
                resultPage = 0;
                searchDirty = true;
                repaint();
                return;
            }
        for (int choice = 0;
             choice < static_cast<int>(
                 ConvolutionReverbModule::
                     wetProcessingLatencyChoices.size());
             ++choice)
            if (bufferBounds(area, choice).contains(event.position))
            {
                const auto samples =
                    ConvolutionReverbModule::
                        wetProcessingLatencyChoices[
                            static_cast<size_t>(choice)];
                const auto result =
                    processor.setConvolutionWetBufferSamples(
                        slot, samples);
                if (result.failed())
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::WarningIcon,
                        "Could not change wet buffer",
                        result.getErrorMessage());
                repaint();
                return;
            }
        for (int row = 0; row < browserRowsPerPage; ++row)
            if (resultBounds(area, row).contains(event.position))
            {
                const auto index =
                    resultPage * browserRowsPerPage + row;
                if (juce::isPositiveAndBelow(
                        index, static_cast<int>(results.size())))
                {
                    const auto result =
                        processor.loadConvolutionImpulseResponseBank(
                        slot,
                        processor.activeConvolutionImpulseResponseBank(
                            slot),
                        results[static_cast<size_t>(index)].file);
                    if (result.failed())
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::MessageBoxIconType::WarningIcon,
                            "Could not load impulse response",
                            result.getErrorMessage());
                }
                repaint();
                return;
            }
        auto paging = library.reduced(10.0f).removeFromBottom(28.0f);
        if (paging.contains(event.position))
        {
            const auto direction =
                event.position.x < paging.getCentreX() ? -1 : 1;
            const auto pageCount = juce::jmax(
                1, static_cast<int>(
                       (results.size() + browserRowsPerPage - 1)
                       / browserRowsPerPage));
            resultPage = juce::jlimit(
                0, pageCount - 1, resultPage + direction);
            repaint();
        }
        return;
    }
    searchFocused = false;
    const auto passband = passbandBounds(area);
    const auto frequencyX = [&passband](float frequency)
    {
        return passband.getX()
            + std::log(frequency / 20.0f) / std::log(1000.0f)
                  * passband.getWidth();
    };
    const auto lowX = frequencyX(
        exponential(20.0f, 1000.0f, value(0)));
    const auto highX = frequencyX(
        exponential(2000.0f, 20000.0f, value(1)));
    const auto lowDistance = std::abs(event.position.x - lowX);
    const auto highDistance = std::abs(event.position.x - highX);
    if (passband.expanded(0.0f, 5.0f).contains(event.position)
        && juce::jmin(lowDistance, highDistance) <= 17.0f)
        dragPrimary = lowDistance <= highDistance ? 0 : 1;
    else
    {
        static constexpr std::array<int, 4> controls { 4, 2, 5, 3 };
        for (int index = 0; index < 4; ++index)
            if (railBounds(area, index).contains(event.position))
                dragPrimary = controls[static_cast<size_t>(index)];
    }

    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

bool ConvolutionReverbModuleView::keyPressed(
    const juce::KeyPress& key)
{
    if (!searchFocused)
        return false;
    if (key.getKeyCode() == juce::KeyPress::escapeKey)
    {
        searchFocused = false;
        repaint();
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::backspaceKey)
    {
        if (searchText.isNotEmpty())
            searchText = searchText.dropLastCharacters(1);
    }
    else if (key.getKeyCode() == juce::KeyPress::returnKey)
    {
        searchFocused = false;
        repaint();
        return true;
    }
    else
    {
        const auto character = key.getTextCharacter();
        if (key.getModifiers().isCommandDown()
            || key.getModifiers().isCtrlDown()
            || key.getModifiers().isAltDown()
            || character < 32)
            return false;
        searchText += juce::String::charToString(character);
    }
    resultPage = 0;
    searchDirty = true;
    repaint();
    return true;
}

void ConvolutionReverbModuleView::timerCallback()
{
    ContinuousTelemetrySnapshot telemetry;
    if (!readContinuousTelemetry(telemetry)
        || telemetry.sequence == 0
        || telemetry.sequence == lastTelemetrySequence
        || telemetry.valueCount
               < ConvolutionReverbModule::telemetryValueCount)
        return;

    lastTelemetrySequence = telemetry.sequence;
    auto& frame = spectrogram[
        static_cast<size_t>(spectrogramWritePosition)];
    for (size_t band = 0; band < frame.size(); ++band)
    {
        const auto magnitude = telemetry.values[
            ConvolutionReverbModule::spectrumBandStart + band];
        frame[band] = std::isfinite(magnitude)
            ? juce::jmax(0.0f, magnitude) : 0.0f;
    }
    spectrogramWritePosition =
        (spectrogramWritePosition + 1) % spectrogramFrameCount;
    spectrogramCount = juce::jmin(
        spectrogramCount + 1, spectrogramFrameCount);
}

void ConvolutionReverbModuleView::mouseDrag(
    const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (dragPrimary == 0 || dragPrimary == 1)
    {
        const auto passband = passbandBounds(area);
        const auto proportion = juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - passband.getX()) / passband.getWidth());
        const auto frequency = 20.0f * std::pow(1000.0f, proportion);
        if (dragPrimary == 0)
            setValue(0, juce::jlimit(
                0.0f, 1.0f,
                std::log(
                    juce::jlimit(20.0f, 1000.0f, frequency) / 20.0f)
                    / std::log(50.0f)));
        else
            setValue(1, juce::jlimit(
                0.0f, 1.0f,
                std::log(
                    juce::jlimit(2000.0f, 20000.0f, frequency) / 2000.0f)
                    / std::log(10.0f)));
    }
    else
    {
        static constexpr std::array<int, 4> controls { 4, 2, 5, 3 };
        const auto position = std::find(
            controls.begin(), controls.end(), dragPrimary);
        if (position == controls.end())
            return;
        const auto track = railTrackBounds(
            area, static_cast<int>(std::distance(controls.begin(), position)));
        setValue(dragPrimary, juce::jlimit(
            0.0f, 1.0f,
            (track.getBottom() - event.position.y) / track.getHeight()));
    }
    updateDefaultDragReadout();
}

void ConvolutionReverbModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto defaults = moduleDefaults(type);
    static constexpr std::array<int, 4> controls { 4, 2, 5, 3 };
    for (int index = 0; index < 4; ++index)
        if (railBounds(area, index).contains(event.position))
        {
            setValue(
                controls[static_cast<size_t>(index)],
                defaults[static_cast<size_t>(
                    controls[static_cast<size_t>(index)])]);
            repaint();
            return;
        }

    const auto passband = passbandBounds(area);
    const auto frequencyX = [&passband](float frequency)
    {
        return passband.getX()
            + std::log(frequency / 20.0f) / std::log(1000.0f)
                  * passband.getWidth();
    };
    const auto lowX = frequencyX(
        exponential(20.0f, 1000.0f, value(0)));
    const auto highX = frequencyX(
        exponential(2000.0f, 20000.0f, value(1)));
    const auto lowDistance = std::abs(event.position.x - lowX);
    const auto highDistance = std::abs(event.position.x - highX);
    if (!passband.expanded(0.0f, 5.0f).contains(event.position)
        || juce::jmin(lowDistance, highDistance) > 17.0f)
        return;
    const auto control = lowDistance <= highDistance ? 0 : 1;
    setValue(control, defaults[static_cast<size_t>(control)]);
    repaint();
}

void ConvolutionReverbModuleView::paintLibrary(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto library = libraryBounds(area);
    graphics.setColour(juce::Colour(0xd918202a));
    graphics.fillRoundedRectangle(library, 7.0f);

    const auto root = processor.impulseResponseRootDirectory();
    graphics.setColour(juce::Colours::white.withAlpha(0.08f));
    graphics.fillRoundedRectangle(rootBounds(area), 4.0f);
    graphics.setColour(juce::Colours::white.withAlpha(0.78f));
    graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    graphics.drawFittedText(
        root.isDirectory()
            ? "ROOT  " + root.getFileName()
            : "SET IR ROOT DIRECTORY",
        rootBounds(area).reduced(6.0f, 1.0f).toNearestInt(),
        juce::Justification::centredLeft, 1, 0.75f);

    const auto activeBank =
        processor.activeConvolutionImpulseResponseBank(slot);
    static constexpr std::array<const char*, 3> bankLabels {
        "A", "B", "C"
    };
    for (int bank = 0; bank < 3; ++bank)
    {
        const auto bounds = bankBounds(area, bank);
        const auto selected = bank == activeBank;
        graphics.setColour(
            selected ? accent.withAlpha(0.76f)
                     : juce::Colours::white.withAlpha(0.075f));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        const auto bankName =
            processor.convolutionImpulseResponseBankName(slot, bank);
        graphics.setColour(juce::Colours::white.withAlpha(
            selected ? 0.98f : 0.65f));
        graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        graphics.drawFittedText(
            juce::String(bankLabels[static_cast<size_t>(bank)])
                + (bankName.isNotEmpty()
                       ? "  " + bankName
                       : "  EMPTY"),
            bounds.reduced(4.0f, 1.0f).toNearestInt(),
            juce::Justification::centred, 1, 0.65f);
    }

    const auto search = searchBounds(area);
    graphics.setColour(
        searchFocused ? accent.withAlpha(0.18f)
                      : juce::Colours::white.withAlpha(0.055f));
    graphics.fillRoundedRectangle(search, 4.0f);
    graphics.setColour(
        searchFocused ? accent
                      : juce::Colours::white.withAlpha(0.22f));
    graphics.drawRoundedRectangle(search, 4.0f, 1.0f);
    graphics.setColour(juce::Colours::white.withAlpha(
        searchText.isNotEmpty() ? 0.92f : 0.42f));
    graphics.setFont(juce::FontOptions(13.0f));
    graphics.drawFittedText(
        searchText.isNotEmpty()
            ? searchText + (searchFocused ? "|" : "")
            : "Search name or path...",
        search.reduced(7.0f, 2.0f).toNearestInt(),
        juce::Justification::centredLeft, 1, 0.75f);

    static constexpr std::array<const char*, 5> filterNames {
        "MONO", "ST", "<1s", "1-3s", ">3s"
    };
    for (int filter = 0; filter < 5; ++filter)
    {
        const auto bounds = filterBounds(area, filter);
        graphics.setColour(
            filters[static_cast<size_t>(filter)]
                ? accent.withAlpha(0.60f)
                : juce::Colours::white.withAlpha(0.06f));
        graphics.fillRoundedRectangle(bounds, 4.0f);
        graphics.setColour(juce::Colours::white.withAlpha(0.82f));
        graphics.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        graphics.drawText(
            filterNames[static_cast<size_t>(filter)],
            bounds.toNearestInt(), juce::Justification::centred);
    }

    const auto selectedBuffer =
        processor.convolutionWetBufferSamples(slot);
    auto bufferLabel = libraryBounds(area).reduced(10.0f);
    bufferLabel.removeFromTop(162.0f);
    bufferLabel = bufferLabel.removeFromTop(14.0f);
    graphics.setColour(juce::Colours::white.withAlpha(0.48f));
    graphics.setFont(juce::FontOptions(8.0f, juce::Font::bold));
    graphics.drawText(
        "WET BUFFER  /  LOWER = LESS PRE-DELAY  /  HIGHER = MORE SAFETY",
        bufferLabel.toNearestInt(),
        juce::Justification::centredLeft);
    for (int choice = 0;
         choice < static_cast<int>(
             ConvolutionReverbModule::
                 wetProcessingLatencyChoices.size());
         ++choice)
    {
        const auto samples =
            ConvolutionReverbModule::wetProcessingLatencyChoices[
                static_cast<size_t>(choice)];
        const auto bounds = bufferBounds(area, choice);
        graphics.setColour(
            samples == selectedBuffer
                ? accent.withAlpha(0.60f)
                : juce::Colours::white.withAlpha(0.06f));
        graphics.fillRoundedRectangle(bounds, 4.0f);
        graphics.setColour(juce::Colours::white.withAlpha(0.86f));
        graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        const auto milliseconds = processor.getSampleRate() > 0.0
            ? static_cast<double>(samples)
                  * 1000.0 / processor.getSampleRate()
            : 0.0;
        graphics.drawFittedText(
            (samples >= 1024
                 ? juce::String(samples / 1024) + "K"
                 : juce::String(samples))
                + "  " + juce::String(milliseconds, 0) + "ms",
            bounds.reduced(3.0f, 1.0f).toNearestInt(),
            juce::Justification::centred, 1, 0.72f);
    }

    for (int row = 0; row < browserRowsPerPage; ++row)
    {
        const auto bounds = resultBounds(area, row);
        const auto index = resultPage * browserRowsPerPage + row;
        if (!juce::isPositiveAndBelow(
                index, static_cast<int>(results.size())))
            continue;
        const auto& entry = results[static_cast<size_t>(index)];
        const auto isLoaded =
            processor.convolutionImpulseResponseBankPath(
                slot, activeBank)
            == entry.file.getFullPathName();
        graphics.setColour(
            isLoaded ? accent.withAlpha(0.16f)
                     : juce::Colours::white.withAlpha(
                           row % 2 == 0 ? 0.035f : 0.055f));
        graphics.fillRoundedRectangle(bounds, 3.0f);
        graphics.setColour(
            isLoaded ? accent : juce::Colours::white.withAlpha(0.84f));
        graphics.setFont(juce::FontOptions(11.5f, juce::Font::bold));
        auto text = bounds.reduced(5.0f, 2.0f);
        graphics.drawFittedText(
            entry.relativePath,
            text.removeFromTop(text.getHeight() * 0.55f)
                .toNearestInt(),
            juce::Justification::centredLeft, 1, 0.72f);
        graphics.setColour(juce::Colours::white.withAlpha(0.45f));
        graphics.setFont(juce::FontOptions(9.5f));
        graphics.drawText(
            (entry.channelCount == 1 ? "Mono" : "Stereo")
                + juce::String("  ")
                + juce::String(entry.sampleRate / 1000.0, 1)
                + " kHz  " + juce::String(entry.durationSeconds, 2)
                + " s",
            text.toNearestInt(), juce::Justification::centredLeft);
    }

    auto paging = library.reduced(10.0f).removeFromBottom(28.0f);
    const auto pageCount = juce::jmax(
        1, static_cast<int>(
               (results.size() + browserRowsPerPage - 1)
               / browserRowsPerPage));
    graphics.setColour(juce::Colours::white.withAlpha(0.58f));
    graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    graphics.drawText(
        "<", paging.removeFromLeft(24.0f).toNearestInt(),
        juce::Justification::centred);
    graphics.drawText(
        ">", paging.removeFromRight(24.0f).toNearestInt(),
        juce::Justification::centred);
    graphics.drawText(
        juce::String(results.size()) + " IRs  "
            + juce::String(resultPage + 1) + "/"
            + juce::String(pageCount),
        paging.toNearestInt(), juce::Justification::centred);
}

void ConvolutionReverbModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    if (searchDirty)
        refreshResults();
    paintLibrary(graphics, area);
    const auto name = processor.impulseResponseName(slot);
    const auto storedPath = processor.impulseResponsePath(slot);
    auto column = waveformColumn(area);
    const auto waveform = waveformBounds(area);
    const auto passband = passbandBounds(area);
    const auto controlsPanel = controlsBounds(area);
    const auto lowCut = exponential(20.0f, 1000.0f, value(0));
    const auto highCut = exponential(2000.0f, 20000.0f, value(1));
    const auto frequencyX = [](juce::Rectangle<float> bounds, float frequency)
    {
        return bounds.getX()
            + std::log(frequency / 20.0f) / std::log(1000.0f)
                  * bounds.getWidth();
    };
    graphics.setColour(juce::Colour(0xff151b24));
    graphics.fillRoundedRectangle(waveform, 7.0f);
    graphics.setColour(juce::Colour(0xffa8b3c0));
    graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    auto header = column.removeFromTop(20.0f);
    auto nameBounds = header;
    if (name.isNotEmpty())
        nameBounds.removeFromRight(150.0f);
    graphics.drawText(
        name.isNotEmpty() ? name : "IMPULSE RESPONSE",
        nameBounds.toNearestInt(),
        juce::Justification::centredLeft);
    if (name.isNotEmpty())
    {
        graphics.setColour(juce::Colours::white.withAlpha(0.52f));
        graphics.setFont(juce::FontOptions(8.5f, juce::Font::bold));
        graphics.drawText(
            "LIVE WET SPECTROGRAM",
            header.removeFromRight(146.0f).toNearestInt(),
            juce::Justification::centredRight);
    }

    if (name.isEmpty())
    {
        graphics.setColour(juce::Colour(0xffa8b3c0));
        graphics.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        graphics.drawText(
            storedPath.isNotEmpty()
                ? "IR FILE NOT FOUND\n"
                      + juce::File(storedPath).getFileName()
                      + "\nLOAD OR DROP A REPLACEMENT"
                : "LOAD IR... OR DROP WAV / AIFF / FLAC HERE",
            waveform.reduced(12.0f).toNearestInt(),
            juce::Justification::centred);
    }
    else
    {
        ContinuousTelemetrySnapshot telemetry;
        const auto hasTelemetry =
            readContinuousTelemetry(telemetry)
            && telemetry.sequence != 0
            && telemetry.valueCount
                   >= ConvolutionReverbModule::telemetryValueCount;
        if (viewMode == ViewMode::history)
        {
            const auto graphArea = waveform.reduced(11.0f, 18.0f);
            graphics.setColour(juce::Colours::white.withAlpha(0.05f));
            for (int division = 1; division < 6; ++division)
            {
                const auto y = graphArea.getY()
                    + graphArea.getHeight() * static_cast<float>(division)
                          / 6.0f;
                graphics.drawHorizontalLine(
                    juce::roundToInt(y),
                    graphArea.getX(), graphArea.getRight());
            }

            const auto cellWidth =
                graphArea.getWidth() / static_cast<float>(
                    spectrogramFrameCount);
            const auto cellHeight =
                graphArea.getHeight()
                / static_cast<float>(
                    ConvolutionReverbModule::spectrumBandCount);
            const auto oldest = spectrogramCount < spectrogramFrameCount
                ? 0 : spectrogramWritePosition;
            const auto startX = graphArea.getRight()
                - cellWidth * static_cast<float>(spectrogramCount);
            const auto dark = juce::Colour(0xff101721);
            const auto warm = juce::Colour(0xffd85b72);
            const auto bright = juce::Colour(0xff78ddff);
            for (int frame = 0; frame < spectrogramCount; ++frame)
            {
                const auto& spectrum = spectrogram[static_cast<size_t>(
                    (oldest + frame) % spectrogramFrameCount)];
                for (size_t band = 0; band < spectrum.size(); ++band)
                {
                    constexpr auto displayGainDb = 24.0f;
                    const auto db = juce::Decibels::gainToDecibels(
                        spectrum[band], -96.0f) + displayGainDb;
                    const auto normalized = juce::jlimit(
                        0.0f, 1.0f,
                        (db + 72.0f) / 72.0f);
                    const auto intensity = std::sqrt(normalized);
                    auto colour = dark.interpolatedWith(
                        warm, juce::jmin(1.0f, intensity * 1.7f));
                    if (intensity > 0.58f)
                        colour = warm.interpolatedWith(
                            bright, (intensity - 0.58f) / 0.42f);
                    const auto y = graphArea.getBottom()
                        - static_cast<float>(band + 1) * cellHeight;
                    graphics.setColour(colour.withAlpha(
                        0.12f + intensity * 0.88f));
                    graphics.fillRect(
                        startX + static_cast<float>(frame) * cellWidth,
                        y, cellWidth + 0.6f, cellHeight + 0.6f);
                }
            }
            graphics.setColour(juce::Colours::white.withAlpha(0.55f));
            graphics.setFont(juce::FontOptions(8.5f, juce::Font::bold));
            const auto labels = waveform.reduced(7.0f);
            graphics.drawText(
                "12 kHz", labels.toNearestInt(),
                juce::Justification::topLeft);
            graphics.drawText(
                "80 Hz", labels.toNearestInt(),
                juce::Justification::bottomLeft);
            const auto wetLevel = hasTelemetry
                ? juce::jmax(
                      telemetry.values[
                          ConvolutionReverbModule::leftWetLevel],
                      telemetry.values[
                          ConvolutionReverbModule::rightWetLevel])
                : 0.0f;
            const auto wetDb = juce::Decibels::gainToDecibels(
                wetLevel, -96.0f);
            graphics.drawText(
                spectrogramCount > 0
                    ? "4 SEC WET HISTORY  /  "
                          + juce::String(wetDb, 1)
                          + " dB  /  NEWEST"
                    : "PLAY AUDIO TO EXCITE THE IR",
                labels.toNearestInt(),
                juce::Justification::bottomRight);
        }
        else if (viewMode == ViewMode::analyzer)
        {
            const auto graphArea = waveform.reduced(30.0f, 24.0f);
            static constexpr std::array<float, 5> frequencies {
                80.0f, 250.0f, 1000.0f, 4000.0f, 12000.0f
            };
            static constexpr std::array<const char*, 5> frequencyLabels {
                "80", "250", "1k", "4k", "12k"
            };
            const auto frequencyPosition = [&graphArea](float frequency)
            {
                return graphArea.getX()
                    + std::log(frequency / 80.0f)
                          / std::log(12000.0f / 80.0f)
                          * graphArea.getWidth();
            };
            graphics.setColour(juce::Colours::white.withAlpha(0.055f));
            graphics.setFont(juce::FontOptions(8.0f));
            for (size_t index = 0; index < frequencies.size(); ++index)
            {
                const auto x = frequencyPosition(frequencies[index]);
                graphics.drawVerticalLine(
                    juce::roundToInt(x),
                    graphArea.getY(), graphArea.getBottom());
                graphics.drawText(
                    frequencyLabels[index],
                    juce::Rectangle<float>(
                        x - 18.0f, graphArea.getBottom() + 3.0f,
                        36.0f, 12.0f).toNearestInt(),
                    juce::Justification::centred);
            }
            for (int line = 1; line < 4; ++line)
            {
                const auto y = graphArea.getY()
                    + graphArea.getHeight() * static_cast<float>(line)
                          / 4.0f;
                graphics.drawHorizontalLine(
                    juce::roundToInt(y),
                    graphArea.getX(), graphArea.getRight());
            }

            std::array<
                juce::Point<float>,
                ConvolutionReverbModule::spectrumBandCount> points {};
            constexpr auto analyzerDisplayGainDb = 18.0f;
            const auto analyzerDisplayGain =
                juce::Decibels::decibelsToGain(analyzerDisplayGainDb);
            for (size_t band = 0; band < points.size(); ++band)
            {
                const auto magnitude = hasTelemetry
                    ? telemetry.values[
                          ConvolutionReverbModule::spectrumBandStart + band]
                          * analyzerDisplayGain
                    : 0.0f;
                const auto db = juce::Decibels::gainToDecibels(
                    magnitude, -72.0f);
                points[band] = {
                    graphArea.getX()
                        + (static_cast<float>(band) + 0.5f)
                              / static_cast<float>(points.size())
                              * graphArea.getWidth(),
                    graphArea.getBottom()
                        - (db + 72.0f) / 72.0f * graphArea.getHeight()
                };
            }

            juce::Path response;
            response.startNewSubPath(points.front());
            for (size_t band = 1; band + 1 < points.size(); ++band)
            {
                const auto midpoint =
                    (points[band] + points[band + 1]) * 0.5f;
                response.quadraticTo(points[band], midpoint);
            }
            response.lineTo(points.back());
            auto fill = response;
            fill.lineTo(points.back().x, graphArea.getBottom());
            fill.lineTo(points.front().x, graphArea.getBottom());
            fill.closeSubPath();

            const auto lowColour = juce::Colour(0xffff9b61);
            const auto midColour = juce::Colour(0xffd85bba);
            const auto highColour = juce::Colour(0xff78ddff);
            juce::ColourGradient fillGradient(
                lowColour.withAlpha(0.30f), graphArea.getX(), 0.0f,
                highColour.withAlpha(0.30f), graphArea.getRight(), 0.0f,
                false);
            fillGradient.addColour(0.52, midColour.withAlpha(0.34f));
            graphics.setGradientFill(fillGradient);
            graphics.fillPath(fill);

            juce::ColourGradient lineGradient(
                lowColour, graphArea.getX(), 0.0f,
                highColour, graphArea.getRight(), 0.0f, false);
            lineGradient.addColour(0.52, midColour);
            graphics.setGradientFill(lineGradient);
            graphics.strokePath(
                response, juce::PathStrokeType(
                              2.2f, juce::PathStrokeType::curved,
                              juce::PathStrokeType::rounded));
            for (size_t band = 0; band < points.size(); ++band)
            {
                const auto proportion = static_cast<float>(band)
                    / static_cast<float>(points.size() - 1);
                const auto colour = proportion < 0.52f
                    ? lowColour.interpolatedWith(
                          midColour, proportion / 0.52f)
                    : midColour.interpolatedWith(
                          highColour, (proportion - 0.52f) / 0.48f);
                graphics.setColour(colour);
                graphics.fillEllipse(
                    juce::Rectangle<float>(5.0f, 5.0f)
                        .withCentre(points[band]));
            }

            graphics.setColour(juce::Colours::white.withAlpha(0.55f));
            graphics.setFont(juce::FontOptions(8.5f, juce::Font::bold));
            graphics.drawText(
                "WET ONLY  /  +18 dB DISPLAY",
                waveform.reduced(7.0f).toNearestInt(),
                juce::Justification::topLeft);
            if (!hasTelemetry)
                graphics.drawText(
                    "PLAY AUDIO TO EXCITE THE IR",
                    waveform.reduced(7.0f).toNearestInt(),
                    juce::Justification::centredBottom);
        }
        else
        {
        const auto centre = waveform.getCentre();
        const auto chamber = waveform.reduced(18.0f, 24.0f);
        graphics.setColour(juce::Colours::white.withAlpha(0.045f));
        for (int ring = 1; ring <= 5; ++ring)
        {
            const auto scale = static_cast<float>(ring) / 5.0f;
            const auto guide = juce::Rectangle<float>(
                chamber.getWidth() * scale,
                chamber.getHeight() * scale).withCentre(centre);
            graphics.drawEllipse(guide, 1.0f);
        }
        graphics.setColour(juce::Colours::white.withAlpha(0.035f));
        graphics.drawHorizontalLine(
            juce::roundToInt(centre.y),
            chamber.getX(), chamber.getRight());
        graphics.drawVerticalLine(
            juce::roundToInt(centre.x),
            chamber.getY(), chamber.getBottom());

        const auto normalizedLevel = [](float rms)
        {
            return juce::jlimit(
                0.0f, 1.0f,
                (juce::Decibels::gainToDecibels(rms, -72.0f) + 72.0f)
                    / 72.0f);
        };
        const auto drawReflections =
            [&graphics, &waveform, &chamber, &telemetry, centre,
             &normalizedLevel](
                std::uint32_t levelLane, std::uint32_t brightnessLane,
                bool left, juce::Colour brightColour)
        {
            graphics.saveState();
            auto half = waveform;
            if (left)
                half.setRight(centre.x);
            else
                half.setLeft(centre.x);
            graphics.reduceClipRegion(half.toNearestInt());

            const auto warmColour = juce::Colour(0xffff9b61);
            for (int age = static_cast<int>(telemetry.historyCount) - 1;
                 age >= 0; --age)
            {
                const auto chronologicalIndex =
                    telemetry.historyCount - 1u
                    - static_cast<std::uint32_t>(age);
                const auto level = normalizedLevel(
                    continuousTelemetryHistoryValue(
                        telemetry, levelLane, chronologicalIndex));
                if (level <= 0.001f)
                    continue;
                const auto brightness = juce::jlimit(
                    0.0f, 1.0f,
                    continuousTelemetryHistoryValue(
                        telemetry, brightnessLane, chronologicalIndex));
                const auto radius =
                    (static_cast<float>(age) + 2.0f)
                    / static_cast<float>(
                        continuousTelemetryHistoryCapacity + 1);
                const auto bounds = juce::Rectangle<float>(
                    chamber.getWidth() * radius,
                    chamber.getHeight() * radius).withCentre(centre);
                const auto colour =
                    warmColour.interpolatedWith(brightColour, brightness);
                const auto ageFade = 1.0f - radius * 0.58f;
                graphics.setColour(colour.withAlpha(
                    juce::jlimit(0.0f, 0.72f, level * ageFade * 0.72f)));
                graphics.drawEllipse(bounds, 0.8f + level * 2.2f);
            }
            graphics.restoreState();
        };
        if (hasTelemetry)
        {
            drawReflections(
                ConvolutionReverbModule::leftWetHistory,
                ConvolutionReverbModule::leftBrightnessHistory,
                true, accent.brighter(0.25f));
            drawReflections(
                ConvolutionReverbModule::rightWetHistory,
                ConvolutionReverbModule::rightBrightnessHistory,
                false, outputColour.brighter(0.25f));
        }

        const auto leftSource = hasTelemetry
            ? normalizedLevel(telemetry.values[
                  ConvolutionReverbModule::leftWetLevel]) : 0.0f;
        const auto rightSource = hasTelemetry
            ? normalizedLevel(telemetry.values[
                  ConvolutionReverbModule::rightWetLevel]) : 0.0f;
        graphics.saveState();
        graphics.reduceClipRegion(
            waveform.withRight(centre.x).toNearestInt());
        graphics.setColour(accent.withAlpha(0.20f + leftSource * 0.65f));
        graphics.fillEllipse(
            juce::Rectangle<float>(
                10.0f + leftSource * 24.0f,
                10.0f + leftSource * 24.0f).withCentre(centre));
        graphics.restoreState();
        graphics.saveState();
        graphics.reduceClipRegion(
            waveform.withLeft(centre.x).toNearestInt());
        graphics.setColour(outputColour.withAlpha(0.20f + rightSource * 0.65f));
        graphics.fillEllipse(
            juce::Rectangle<float>(
                10.0f + rightSource * 24.0f,
                10.0f + rightSource * 24.0f).withCentre(centre));
        graphics.restoreState();
        graphics.setColour(juce::Colours::white.withAlpha(0.85f));
        graphics.fillEllipse(
            juce::Rectangle<float>(6.0f, 6.0f).withCentre(centre));

        graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        graphics.setColour(accent);
        graphics.drawText(
            "L", waveform.reduced(7.0f).toNearestInt(),
            juce::Justification::topLeft);
        graphics.setColour(outputColour);
        graphics.drawText(
            "R", waveform.reduced(7.0f).toNearestInt(),
            juce::Justification::topRight);
        graphics.setColour(juce::Colours::white.withAlpha(0.48f));
        graphics.drawText(
            hasTelemetry
                    && telemetry.values[
                           ConvolutionReverbModule::outputChannelCount] < 1.5f
                ? "LIVE MONO WET" : "LIVE STEREO WET",
            waveform.reduced(7.0f).toNearestInt(),
            juce::Justification::centredTop);
        graphics.drawText(
            hasTelemetry ? "2 SEC DECAY" : "PLAY AUDIO TO EXCITE THE IR",
            waveform.reduced(7.0f).toNearestInt(),
            juce::Justification::centredBottom);
        }
    }

    graphics.setColour(juce::Colour(0xff151b24));
    graphics.fillRoundedRectangle(passband, 5.0f);
    const auto passLowX = frequencyX(passband, lowCut);
    const auto passHighX = frequencyX(passband, highCut);
    const auto passbandY = passband.getBottom() - 8.0f;
    graphics.setColour(juce::Colours::white.withAlpha(0.28f));
    graphics.drawHorizontalLine(
        juce::roundToInt(passbandY),
        passband.getX() + 5.0f, passband.getRight() - 5.0f);
    graphics.setColour(accent.withAlpha(0.50f));
    graphics.drawLine(
        passLowX, passbandY, passHighX, passbandY, 3.0f);
    for (const auto x : { passLowX, passHighX })
    {
        graphics.setColour(juce::Colours::white);
        graphics.fillEllipse(
            juce::Rectangle<float>(10.0f, 10.0f)
                .withCentre({ x, passbandY }));
    }
    graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    graphics.setColour(juce::Colours::white);
    graphics.drawText(
        "LOW CUT  " + megadsp::formatControlValue(type, 0, value(0)),
        passband.reduced(7.0f, 2.0f).toNearestInt(),
        juce::Justification::topLeft);
    graphics.drawText(
        "HIGH CUT  " + megadsp::formatControlValue(type, 1, value(1)),
        passband.reduced(7.0f, 2.0f).toNearestInt(),
        juce::Justification::topRight);

    graphics.setColour(juce::Colour(0xd918202a));
    graphics.fillRoundedRectangle(controlsPanel, 7.0f);
    graphics.setColour(juce::Colour(0xffa8b3c0));
    graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    graphics.drawText(
        "LEVELS & DYNAMICS", controlsPanel.reduced(8.0f).toNearestInt(),
        juce::Justification::centredTop);
    static constexpr std::array<int, 4> controlIndices { 4, 2, 5, 3 };
    static constexpr std::array<const char*, 4> labels {
        "DRY", "WET", "DUCK", "OUTPUT"
    };
    const std::array<juce::Colour, 4> colours {
        inputColour, accent, juce::Colour(0xffffb15c), outputColour
    };
    for (int index = 0; index < 4; ++index)
    {
        const auto control = controlIndices[static_cast<size_t>(index)];
        auto rail = railBounds(area, index);
        const auto track = railTrackBounds(area, index);
        const auto handleY = track.getBottom() - value(control) * track.getHeight();
        graphics.setColour(juce::Colours::white.withAlpha(0.16f));
        graphics.fillRoundedRectangle(track, 3.0f);
        auto fill = track.withTop(handleY);
        if (control == 3)
        {
            const auto centre = track.getCentreY();
            fill = juce::Rectangle<float>(
                track.getX(), juce::jmin(centre, handleY),
                track.getWidth(), std::abs(handleY - centre));
        }
        graphics.setColour(colours[static_cast<size_t>(index)].withAlpha(0.72f));
        graphics.fillRoundedRectangle(fill, 3.0f);
        graphics.setColour(juce::Colours::white);
        graphics.fillEllipse(
            juce::Rectangle<float>(12.0f, 12.0f)
                .withCentre({ track.getCentreX(), handleY }));
        graphics.setFont(juce::FontOptions(8.5f, juce::Font::bold));
        graphics.drawText(
            labels[static_cast<size_t>(index)],
            rail.removeFromTop(18.0f).toNearestInt(),
            juce::Justification::centred);
        graphics.drawFittedText(
            megadsp::formatControlValue(type, control, value(control)),
            rail.removeFromBottom(18.0f).toNearestInt(),
            juce::Justification::centred, 1, 0.75f);
    }
}
} // namespace

std::unique_ptr<ModuleView> createConvolutionReverbView(EffectGraph& graph)
{
    return std::make_unique<ConvolutionReverbModuleView>(graph);
}
} // namespace megadsp::ui
