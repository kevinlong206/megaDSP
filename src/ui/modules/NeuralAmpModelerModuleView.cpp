#include "../GraphStyle.h"
#include "../ModuleView.h"

#include "ModuleViewCreators.h"
#include "../../modules/NeuralAmpModeler.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace megadsp::ui
{
namespace
{
class NeuralAmpModelerModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    bool usesFullPanel() const override { return true; }
    int preferredCardHeight() const override
    {
        return moduleCardMaximumHeight;
    }

    void configureAccessibility(juce::Component& component) const override
    {
        component.setTitle("Neural Amp Modeler");
        component.setHelpText(
            "Set a persisted root directory, search and filter the recursive "
            "validated model library, or drop a .nam model into the panel. "
            "Pre Gain drives the model, "
            "Bass, Mid, and Treble form a guitar tone stack, and Post Gain "
            "sets the final modeled level.");
    }

    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void timerCallback() override;
    bool keyPressed(const juce::KeyPress&) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) const override
    {
        return files.size() == 1 && files[0].endsWithIgnoreCase(".nam");
    }

    void filesDropped(const juce::StringArray& files, int, int) override
    {
        if (!isInterestedInFileDrag(files))
            return;
        const auto result = processor.loadNamModel(slot, juce::File(files[0]));
        if (result.failed())
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Could not load NAM model", result.getErrorMessage());
    }

    std::vector<int> keyboardControls() const override
    {
        return { 0, 1, 2, 3, 4 };
    }

private:
    static constexpr int rowsPerPage = 8;
    NamModelInfo cachedInfo;
    float dragStartValue = 0.0f;
    juce::String searchText;
    std::array<bool, 4> filters {};
    std::vector<NamModelLibraryEntry> results;
    std::unique_ptr<juce::FileChooser> rootChooser;
    int resultPage = 0;
    bool searchFocused = false;
    bool searchDirty = true;

    static juce::Rectangle<float> browserBounds(
        juce::Rectangle<float> area)
    {
        return area.removeFromLeft(juce::jlimit(
            300.0f, 410.0f, area.getWidth() * 0.42f));
    }

    static juce::Rectangle<float> modelBounds(
        juce::Rectangle<float> area)
    {
        const auto browser = browserBounds(area);
        area.removeFromLeft(browser.getWidth() + 10.0f);
        return area;
    }

    static juce::Rectangle<float> modelHeaderBounds(
        juce::Rectangle<float> area)
    {
        return modelBounds(area).removeFromTop(150.0f);
    }

    static juce::Rectangle<float> knobBounds(
        juce::Rectangle<float> area, int control)
    {
        area = modelBounds(area);
        area.removeFromTop(166.0f);
        const auto width = area.getWidth() / 5.0f;
        return { area.getX() + width * static_cast<float>(control),
                 area.getY(), width, area.getHeight() };
    }

    static juce::Rectangle<float> rootBounds(
        juce::Rectangle<float> area)
    {
        return browserBounds(area).reduced(10.0f).removeFromTop(34.0f);
    }

    static juce::Rectangle<float> searchBounds(
        juce::Rectangle<float> area)
    {
        auto browser = browserBounds(area).reduced(10.0f);
        browser.removeFromTop(44.0f);
        return browser.removeFromTop(40.0f);
    }

    static juce::Rectangle<float> filterBounds(
        juce::Rectangle<float> area, int filter)
    {
        auto browser = browserBounds(area).reduced(10.0f);
        browser.removeFromTop(94.0f);
        auto row = browser.removeFromTop(30.0f);
        const auto width = row.getWidth() / 4.0f;
        return { row.getX() + width * static_cast<float>(filter),
                 row.getY(), width - 3.0f, row.getHeight() };
    }

    static juce::Rectangle<float> resultBounds(
        juce::Rectangle<float> area, int row)
    {
        auto browser = browserBounds(area).reduced(10.0f);
        browser.removeFromTop(134.0f);
        browser.removeFromBottom(28.0f);
        const auto height =
            browser.getHeight() / static_cast<float>(rowsPerPage);
        return { browser.getX(),
                 browser.getY() + height * static_cast<float>(row),
                 browser.getWidth(), juce::jmax(1.0f, height - 3.0f) };
    }

    juce::String searchQuery() const;
    void refreshResults();
    void chooseRootDirectory();
    void paintBrowser(juce::Graphics&, juce::Rectangle<float>);

    static juce::String kindName(NamModelKind kind)
    {
        switch (kind)
        {
            case NamModelKind::waveNetA2Standard: return "A2 Standard";
            case NamModelKind::waveNetA2Nano: return "A2 Nano";
            case NamModelKind::slimmableWaveNet: return "Slimmable WaveNet";
            case NamModelKind::waveNet: return "WaveNet";
            case NamModelKind::lstm: return "LSTM";
            case NamModelKind::linear: return "Linear";
            case NamModelKind::convNet: return "ConvNet";
            case NamModelKind::container: return "Slimmable Container";
            case NamModelKind::unknown: return "Unknown";
        }
        return "Unknown";
    }

    static juce::String generationName(NamArchitectureGeneration generation)
    {
        switch (generation)
        {
            case NamArchitectureGeneration::a1: return "A1";
            case NamArchitectureGeneration::a2: return "A2";
            case NamArchitectureGeneration::unknown: return "Unknown";
        }
        return "Unknown";
    }

    static juce::String technicalDescription(const NamModelInfo& info)
    {
        juce::StringArray parts;
        parts.add(generationName(info.generation));
        parts.add(kindName(info.kind));
        if (info.fullQuality
            && (info.generation == NamArchitectureGeneration::a2
                || info.cpuCapability == NamCpuCapability::slimmable
                || info.submodelCount > 1))
            parts.add("Full model");
        if (info.submodelCount > 1)
            parts.add(juce::String(info.submodelCount) + " sizes");
        if (info.expectedSampleRate > 0.0)
            parts.add(
                juce::String(info.expectedSampleRate / 1000.0, 1)
                + " kHz");
        if (info.formatVersion.isNotEmpty())
            parts.add("NAM " + info.formatVersion);
        return parts.joinIntoString(" | ");
    }

    static juce::String metadataDescription(const NamModelInfo& info)
    {
        juce::StringArray parts;
        if (info.captureDescription.isNotEmpty())
            parts.add(info.captureDescription);
        if (std::isfinite(info.inputLevelDbU))
            parts.add(
                "Input " + juce::String(info.inputLevelDbU, 1) + " dBu");
        if (std::isfinite(info.validationEsr))
            parts.add(
                "ESR " + juce::String(info.validationEsr, 4));
        return parts.joinIntoString(" | ");
    }
};

juce::String NeuralAmpModelerModuleView::searchQuery() const
{
    static constexpr std::array<const char*, 4> tokens {
        "a1", "a2", "cab", "nocab"
    };
    auto query = searchText.trim();
    for (size_t index = 0; index < filters.size(); ++index)
        if (filters[index])
            query += (query.isEmpty() ? "" : " ")
                + juce::String(tokens[index]);
    return query;
}

void NeuralAmpModelerModuleView::refreshResults()
{
    results = processor.searchNamModels(searchQuery(), 200);
    const auto pageCount = juce::jmax(
        1, static_cast<int>(
               (results.size() + rowsPerPage - 1) / rowsPerPage));
    resultPage = juce::jlimit(0, pageCount - 1, resultPage);
    searchDirty = false;
}

void NeuralAmpModelerModuleView::chooseRootDirectory()
{
    rootChooser = std::make_unique<juce::FileChooser>(
        "Choose NAM model root",
        processor.namModelRootDirectory());
    juce::Component::SafePointer<EffectGraph> safeGraph(&graph);
    rootChooser->launchAsync(
        juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectDirectories,
        [this, safeGraph](const juce::FileChooser& chooser)
        {
            if (safeGraph == nullptr)
                return;
            const auto root = chooser.getResult();
            if (!root.isDirectory())
                return;
            const auto result =
                processor.setNamModelRootDirectory(root);
            if (result.failed())
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Could not index NAM library",
                    result.getErrorMessage());
            else
            {
                resultPage = 0;
                searchDirty = true;
                repaint();
            }
        });
}

void NeuralAmpModelerModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto browser = browserBounds(area);
    if (browser.contains(event.position))
    {
        if (rootBounds(area).contains(event.position))
        {
            chooseRootDirectory();
            return;
        }
        if (searchBounds(area).contains(event.position))
        {
            searchFocused = true;
            repaint();
            return;
        }
        searchFocused = false;
        for (int filter = 0; filter < 4; ++filter)
            if (filterBounds(area, filter).contains(event.position))
            {
                filters[static_cast<size_t>(filter)] =
                    !filters[static_cast<size_t>(filter)];
                resultPage = 0;
                searchDirty = true;
                repaint();
                return;
            }
        for (int row = 0; row < rowsPerPage; ++row)
            if (resultBounds(area, row).contains(event.position))
            {
                const auto index = resultPage * rowsPerPage + row;
                if (juce::isPositiveAndBelow(
                        index, static_cast<int>(results.size())))
                {
                    const auto result = processor.loadNamModel(
                        slot, results[static_cast<size_t>(index)].file);
                    if (result.failed())
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::MessageBoxIconType::WarningIcon,
                            "Could not load NAM model",
                            result.getErrorMessage());
                }
                repaint();
                return;
            }
        auto paging = browser.reduced(10.0f).removeFromBottom(28.0f);
        if (paging.contains(event.position))
        {
            const auto direction =
                event.position.x < paging.getCentreX() ? -1 : 1;
            const auto pageCount = juce::jmax(
                1, static_cast<int>(
                       (results.size() + rowsPerPage - 1)
                       / rowsPerPage));
            resultPage = juce::jlimit(
                0, pageCount - 1, resultPage + direction);
            repaint();
        }
        return;
    }
    searchFocused = false;
    for (int control = 0; control < 5; ++control)
        if (knobBounds(area, control).contains(event.position))
        {
            dragPrimary = control;
            dragStartValue = value(control);
            beginGestures();
            return;
        }
}

bool NeuralAmpModelerModuleView::keyPressed(
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

void NeuralAmpModelerModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    setValue(
        dragPrimary,
        juce::jlimit(
            0.0f, 1.0f,
            dragStartValue - static_cast<float>(event.getDistanceFromDragStartY())
                / 180.0f));
    updateDefaultDragReadout();
}

void NeuralAmpModelerModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int control = 0; control < 5; ++control)
        if (knobBounds(area, control).contains(event.position))
        {
            resetToDefault(control);
            return;
        }
}

void NeuralAmpModelerModuleView::timerCallback()
{
    const auto* module = processor.getRack().activeModuleInstance(slot);
    const auto* capability =
        module != nullptr ? module->namModelCapability() : nullptr;
    if (capability == nullptr)
        return;
    const auto info = capability->namModelInfo();
    const auto changedNumber = [](double a, double b)
    {
        return std::isfinite(a) != std::isfinite(b)
            || (std::isfinite(a)
                && std::abs(a - b)
                       > std::numeric_limits<double>::epsilon());
    };
    if (info.name != cachedInfo.name || info.valid != cachedInfo.valid
        || info.kind != cachedInfo.kind
        || info.generation != cachedInfo.generation
        || info.cpuCapability != cachedInfo.cpuCapability
        || info.fullQuality != cachedInfo.fullQuality
        || info.formatVersion != cachedInfo.formatVersion
        || info.captureDescription != cachedInfo.captureDescription
        || changedNumber(info.inputLevelDbU, cachedInfo.inputLevelDbU)
        || changedNumber(info.validationEsr, cachedInfo.validationEsr)
        || info.submodelCount != cachedInfo.submodelCount)
    {
        cachedInfo = info;
        repaint();
    }
}

void NeuralAmpModelerModuleView::paintBrowser(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto browser = browserBounds(area);
    graphics.setColour(juce::Colour(0xd918202a));
    graphics.fillRoundedRectangle(browser, 7.0f);

    const auto root = processor.namModelRootDirectory();
    graphics.setColour(juce::Colours::white.withAlpha(0.08f));
    graphics.fillRoundedRectangle(rootBounds(area), 4.0f);
    graphics.setColour(juce::Colours::white.withAlpha(0.82f));
    graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    graphics.drawFittedText(
        root.isDirectory()
            ? "ROOT  " + root.getFileName()
            : "SET NAM ROOT DIRECTORY",
        rootBounds(area).reduced(7.0f, 1.0f).toNearestInt(),
        juce::Justification::centredLeft, 1, 0.72f);

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
            : "Search model, amp, tone, author...",
        search.reduced(7.0f, 2.0f).toNearestInt(),
        juce::Justification::centredLeft, 1, 0.75f);

    static constexpr std::array<const char*, 4> filterNames {
        "A1", "A2", "CAB", "AMP"
    };
    for (int filter = 0; filter < 4; ++filter)
    {
        const auto bounds = filterBounds(area, filter);
        graphics.setColour(
            filters[static_cast<size_t>(filter)]
                ? accent.withAlpha(0.62f)
                : juce::Colours::white.withAlpha(0.06f));
        graphics.fillRoundedRectangle(bounds, 4.0f);
        graphics.setColour(juce::Colours::white.withAlpha(0.84f));
        graphics.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        graphics.drawText(
            filterNames[static_cast<size_t>(filter)],
            bounds.toNearestInt(), juce::Justification::centred);
    }

    const auto currentPath = processor.namModelPath(slot);
    for (int row = 0; row < rowsPerPage; ++row)
    {
        const auto bounds = resultBounds(area, row);
        const auto index = resultPage * rowsPerPage + row;
        if (!juce::isPositiveAndBelow(
                index, static_cast<int>(results.size())))
            continue;
        const auto& entry = results[static_cast<size_t>(index)];
        const auto loaded =
            currentPath == entry.file.getFullPathName();
        graphics.setColour(
            loaded ? accent.withAlpha(0.16f)
                   : juce::Colours::white.withAlpha(
                         row % 2 == 0 ? 0.035f : 0.055f));
        graphics.fillRoundedRectangle(bounds, 3.0f);
        auto text = bounds.reduced(6.0f, 3.0f);
        graphics.setColour(
            loaded ? accent
                   : juce::Colours::white.withAlpha(0.88f));
        graphics.setFont(juce::FontOptions(11.5f, juce::Font::bold));
        graphics.drawFittedText(
            entry.displayName,
            text.removeFromTop(text.getHeight() * 0.38f)
                .toNearestInt(),
            juce::Justification::centredLeft, 1, 0.72f);
        graphics.setColour(juce::Colours::white.withAlpha(0.62f));
        graphics.setFont(juce::FontOptions(9.0f));
        graphics.drawFittedText(
            entry.captureDescription.isNotEmpty()
                ? entry.captureDescription
                : entry.relativePath,
            text.removeFromTop(text.getHeight() * 0.48f)
                .toNearestInt(),
            juce::Justification::centredLeft, 1, 0.72f);
        juce::StringArray details;
        details.add(entry.isA2 ? "A2" : "A1");
        details.add(entry.architecture);
        details.add(
            juce::String(entry.sampleRate / 1000.0, 1) + " kHz");
        if (entry.cabIncluded >= 0)
            details.add(entry.cabIncluded != 0 ? "Cab" : "Amp");
        graphics.setColour(juce::Colours::white.withAlpha(0.42f));
        graphics.setFont(juce::FontOptions(8.5f));
        graphics.drawFittedText(
            details.joinIntoString("  |  "),
            text.toNearestInt(),
            juce::Justification::centredLeft, 1, 0.72f);
    }

    auto paging = browser.reduced(10.0f).removeFromBottom(28.0f);
    const auto pageCount = juce::jmax(
        1, static_cast<int>(
               (results.size() + rowsPerPage - 1) / rowsPerPage));
    graphics.setColour(juce::Colours::white.withAlpha(0.58f));
    graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    graphics.drawText(
        "<", paging.removeFromLeft(24.0f).toNearestInt(),
        juce::Justification::centred);
    graphics.drawText(
        ">", paging.removeFromRight(24.0f).toNearestInt(),
        juce::Justification::centred);
    graphics.drawText(
        juce::String(results.size()) + " MODELS  "
            + juce::String(resultPage + 1) + "/"
            + juce::String(pageCount),
        paging.toNearestInt(), juce::Justification::centred);
}

void NeuralAmpModelerModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    if (searchDirty)
        refreshResults();
    paintBrowser(graphics, area);
    const auto fullArea = area;
    auto header = modelHeaderBounds(area);
    graphics.setColour(juce::Colour(0xff10141b));
    graphics.fillRoundedRectangle(header, 8.0f);
    graphics.setColour(cachedInfo.valid
        ? juce::Colours::white : juce::Colour(0xffa8b3c0));
    graphics.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    graphics.drawText(
        cachedInfo.valid ? cachedInfo.name : "No model loaded",
        header.removeFromTop(header.getHeight() * 0.38f)
            .reduced(10.0f, 0.0f).toNearestInt(),
        juce::Justification::centredLeft);
    graphics.setFont(juce::FontOptions(10.0f));
    graphics.setColour(juce::Colour(0xffa8b3c0));
    graphics.drawText(
        cachedInfo.valid
            ? technicalDescription(cachedInfo)
            : "Choose a library model or drop a .nam file",
        header.removeFromTop(header.getHeight() * 0.52f)
            .reduced(10.0f, 0.0f).toNearestInt(),
        juce::Justification::centredLeft);
    if (cachedInfo.valid)
    {
        graphics.setColour(juce::Colour(0xff7f93a8));
        graphics.setFont(juce::FontOptions(9.0f));
        graphics.drawText(
            metadataDescription(cachedInfo),
            header.reduced(10.0f, 0.0f).toNearestInt(),
            juce::Justification::centredLeft);
    }

    static constexpr std::array<const char*, 5> labels {
        "PRE GAIN", "BASS", "MID", "TREBLE", "POST GAIN"
    };
    for (int control = 0; control < 5; ++control)
    {
        auto bounds = knobBounds(fullArea, control);
        const auto diameter = juce::jmin(
            58.0f, juce::jmin(bounds.getWidth() - 16.0f,
                              bounds.getHeight() - 42.0f));
        const auto knob = juce::Rectangle<float>(diameter, diameter)
            .withCentre({ bounds.getCentreX(), bounds.getCentreY() - 4.0f });
        graphics.setColour(juce::Colour(0xff242c33));
        graphics.fillEllipse(knob);
        const auto start = juce::MathConstants<float>::pi * 0.75f;
        const auto end = juce::MathConstants<float>::pi * 2.25f;
        juce::Path arc;
        arc.addCentredArc(
            knob.getCentreX(), knob.getCentreY(),
            diameter * 0.42f, diameter * 0.42f, 0.0f,
            start, start + (end - start) * value(control), true);
        graphics.setColour(
            control == 0 || control == 4 ? outputColour : accent);
        graphics.strokePath(arc, juce::PathStrokeType(4.0f));
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        graphics.drawText(
            labels[static_cast<size_t>(control)],
            bounds.removeFromBottom(22.0f).toNearestInt(),
            juce::Justification::centred);
        graphics.setFont(juce::FontOptions(9.0f));
        graphics.drawText(
            formatControlValue(type, control, value(control)),
            knob.toNearestInt(), juce::Justification::centred);
    }
}
} // namespace

std::unique_ptr<ModuleView> createNeuralAmpModelerView(EffectGraph& graph)
{
    return std::make_unique<NeuralAmpModelerModuleView>(graph);
}
} // namespace megadsp::ui
