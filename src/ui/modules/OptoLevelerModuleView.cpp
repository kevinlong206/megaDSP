#include "../GraphStyle.h"
#include "../ModuleView.h"
#include "../../modules/OptoLeveler.h"

#include "ModuleViewCreators.h"

#include <array>
#include <cmath>

namespace megadsp::ui
{
namespace
{
constexpr std::array<int, 8> barControls {
    OptoLevelerModule::peakReductionControl,
    OptoLevelerModule::gainControl,
    OptoLevelerModule::hfEmphasisControl,
    OptoLevelerModule::sidechainLowCutControl,
    OptoLevelerModule::cellAgeControl,
    OptoLevelerModule::stereoLinkControl,
    OptoLevelerModule::mixControl,
    OptoLevelerModule::outputControl
};
constexpr std::array<const char*, 8> barNames {
    "PEAK REDUCTION", "GAIN", "HF EMPHASIS", "SC LOW CUT",
    "CELL AGE", "STEREO LINK", "MIX", "OUTPUT"
};

class OptoLevelerModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    void configureAccessibility(juce::Component&) const override;
    bool usesFullPanel() const override { return true; }
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    static juce::Rectangle<float> plotArea(juce::Rectangle<float>);
    static juce::Rectangle<float> pillArea(juce::Rectangle<float>);
    static juce::Rectangle<float> barArea(juce::Rectangle<float>, int index);
    int barAt(juce::Rectangle<float> area, juce::Point<float> position) const;
};

void OptoLevelerModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Opto Leveler feedback compressor display");
    component.setHelpText(
        "Shows measured input and output level with detector, gain "
        "reduction, and cell memory history from the feedback optical "
        "detector. Click Compress/Limit to switch curves, and drag the "
        "tracks below to edit Peak Reduction, Gain, HF Emphasis, "
        "Sidechain Low Cut, Cell Age, Stereo Link, Mix, and Output.");
}

juce::Rectangle<float> OptoLevelerModuleView::plotArea(
    juce::Rectangle<float> area)
{
    area.removeFromTop(26.0f);
    area.removeFromBottom(122.0f);
    return area;
}

juce::Rectangle<float> OptoLevelerModuleView::pillArea(
    juce::Rectangle<float> area)
{
    auto controls = area.removeFromBottom(122.0f);
    return controls.removeFromTop(28.0f).reduced(4.0f, 2.0f);
}

juce::Rectangle<float> OptoLevelerModuleView::barArea(
    juce::Rectangle<float> area, int index)
{
    auto controls = area.removeFromBottom(122.0f);
    controls.removeFromTop(28.0f);
    const auto row = index / 4;
    const auto column = index % 4;
    const auto rowArea = juce::Rectangle<float>(
        controls.getX(), controls.getY() + 47.0f * static_cast<float>(row),
        controls.getWidth(), 47.0f);
    const auto width = rowArea.getWidth() / 4.0f;
    return juce::Rectangle<float>(
               rowArea.getX() + width * static_cast<float>(column),
               rowArea.getY(), width, rowArea.getHeight())
        .reduced(4.0f, 3.0f);
}

int OptoLevelerModuleView::barAt(
    juce::Rectangle<float> area, juce::Point<float> position) const
{
    for (size_t index = 0; index < barControls.size(); ++index)
        if (barArea(area, static_cast<int>(index)).contains(position))
            return barControls[index];
    return -1;
}

void OptoLevelerModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (pillArea(area).contains(event.position))
    {
        cycleChoice(OptoLevelerModule::compressLimitControl, 2);
        return;
    }
    const auto control = barAt(area, event.position);
    if (control < 0)
        return;
    dragPrimary = control;
    beginGestures();
    mouseDrag(event);
}

void OptoLevelerModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (size_t index = 0; index < barControls.size(); ++index)
    {
        if (barControls[index] != dragPrimary)
            continue;
        const auto track = barArea(area, static_cast<int>(index));
        setValue(dragPrimary, juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - track.getX()) / track.getWidth()));
        break;
    }
    updateDefaultDragReadout();
}

void OptoLevelerModuleView::mouseDoubleClick(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (pillArea(area).contains(event.position))
    {
        resetToDefault(OptoLevelerModule::compressLimitControl);
        return;
    }
    const auto control = barAt(area, event.position);
    if (control >= 0)
        resetToDefault(control);
}

void OptoLevelerModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto plot = plotArea(area);

    ContinuousTelemetrySnapshot telemetrySnapshot;
    const auto hasTelemetry = readContinuousTelemetry(telemetrySnapshot)
        && telemetrySnapshot.sequence != 0
        && telemetrySnapshot.valueCount
               >= OptoLevelerModule::telemetryValueCount;

    graphics.setColour(juce::Colour(0xff141b23));
    graphics.fillRoundedRectangle(plot, 6.0f);

    const auto drawLevel = [&](const auto& levels, juce::Colour colour)
    {
        juce::Path path;
        for (size_t index = 0; index < levels.size(); ++index)
        {
            const auto x = plot.getX()
                + static_cast<float>(index)
                      / static_cast<float>(levels.size() - 1)
                      * plot.getWidth();
            const auto y = juce::jmap(
                juce::jlimit(-60.0f, 6.0f, levels[index]),
                -60.0f, 6.0f, plot.getBottom(), plot.getY());
            if (index == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
        graphics.setColour(colour);
        graphics.strokePath(path, juce::PathStrokeType(1.6f));
    };
    drawLevel(inputLevels, inputColour);
    drawLevel(outputLevels, outputColour);

    juce::Path reductionHistoryPath;
    juce::Path memoryHistoryPath;
    for (std::uint32_t index = 0;
         hasTelemetry && index < telemetrySnapshot.historyCount; ++index)
    {
        const auto x = plot.getX()
            + static_cast<float>(index)
                  / static_cast<float>(
                      juce::jmax(1u, telemetrySnapshot.historyCount - 1))
                  * plot.getWidth();
        const auto reduction = continuousTelemetryHistoryValue(
            telemetrySnapshot, OptoLevelerModule::reductionHistory, index);
        const auto reductionY = plot.getBottom()
            - juce::jlimit(0.0f, 1.0f, reduction / 24.0f) * plot.getHeight();
        const auto memory = continuousTelemetryHistoryValue(
            telemetrySnapshot, OptoLevelerModule::memoryHistory, index);
        const auto memoryY = plot.getBottom()
            - juce::jlimit(0.0f, 1.0f, memory) * plot.getHeight();
        if (index == 0)
        {
            reductionHistoryPath.startNewSubPath(x, reductionY);
            memoryHistoryPath.startNewSubPath(x, memoryY);
        }
        else
        {
            reductionHistoryPath.lineTo(x, reductionY);
            memoryHistoryPath.lineTo(x, memoryY);
        }
    }
    graphics.setColour(reductionColour.withAlpha(0.85f));
    graphics.strokePath(reductionHistoryPath, juce::PathStrokeType(1.8f));
    graphics.setColour(accent.withAlpha(0.55f));
    graphics.strokePath(memoryHistoryPath, juce::PathStrokeType(1.3f));

    graphics.setColour(juce::Colour(0xffa8b3c0));
    graphics.setFont(11.0f);
    graphics.drawText(
        hasTelemetry
            ? "DETECTOR " + juce::String(
                  telemetrySnapshot.values[OptoLevelerModule::detectorLevelDb],
                  1)
                  + " dB | GR "
                  + juce::String(
                      telemetrySnapshot.values[
                          OptoLevelerModule::gainReductionDb],
                      1)
                  + " dB | CELL MEMORY "
                  + juce::String(
                      100.0f * telemetrySnapshot.values[
                                   OptoLevelerModule::cellMemory],
                      0)
                  + "%"
            : "DSP TELEMETRY WAITING",
        plot.reduced(6.0f).toNearestInt(), juce::Justification::bottomLeft);

    const auto pill = pillArea(area);
    const auto isLimit = value(OptoLevelerModule::compressLimitControl) >= 0.5f;
    graphics.setColour(isLimit ? reductionColour.withAlpha(0.34f)
                               : accent.withAlpha(0.34f));
    graphics.fillRoundedRectangle(pill, 5.0f);
    graphics.setColour(juce::Colours::white);
    graphics.setFont(12.0f);
    graphics.drawText(
        isLimit ? "LIMIT" : "COMPRESS", pill.toNearestInt(),
        juce::Justification::centred);

    for (size_t index = 0; index < barControls.size(); ++index)
    {
        const auto control = barControls[index];
        const auto bounds = barArea(area, static_cast<int>(index));
        graphics.setColour(juce::Colour(0xff232c37));
        graphics.fillRoundedRectangle(bounds, 4.0f);
        graphics.setColour(accent.withAlpha(0.28f));
        graphics.fillRoundedRectangle(
            bounds.withWidth(bounds.getWidth() * value(control)), 4.0f);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(10.0f);
        auto textBounds = bounds;
        graphics.drawText(
            juce::String(barNames[index]),
            textBounds.removeFromTop(textBounds.getHeight() * 0.5f)
                .toNearestInt(),
            juce::Justification::centred);
        graphics.drawText(
            formatControlValue(type, control, value(control)),
            textBounds.toNearestInt(), juce::Justification::centred);
    }
}
} // namespace

std::unique_ptr<ModuleView> createOptoLevelerView(EffectGraph& graph)
{
    return std::make_unique<OptoLevelerModuleView>(graph);
}
} // namespace megadsp::ui
