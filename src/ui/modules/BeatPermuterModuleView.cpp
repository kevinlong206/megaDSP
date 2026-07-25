#include "../GraphStyle.h"
#include "../ModuleView.h"

#include "ModuleViewCreators.h"

#include <array>
#include <cmath>

namespace megadsp::ui
{
namespace
{
class BeatPermuterModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    bool usesFullPanel() const override { return true; }
    void configureAccessibility(juce::Component& component) const override
    {
        component.setTitle("Beat Permuter sequence");
        component.setHelpText(
            "Choose a Slice value and Pattern, then set Chance, History, and "
            "Length. The display shows the actual source slice chosen for each "
            "output step. Source 1 is the newest captured slice. Double-click "
            "any control to restore its default.");
    }
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void timerCallback() override
    {
        visualEvents = processor.getRack().beatPermutationVisualEvents(slot);
    }

private:
    static juce::Rectangle<float> gridPill(
        juce::Rectangle<float> area, int index)
    {
        auto row = area.removeFromTop(29.0f);
        const auto width = row.getWidth() / 6.0f;
        return juce::Rectangle<float>(
            row.getX() + width * static_cast<float>(index), row.getY(),
            width, row.getHeight()).reduced(3.0f, 2.0f);
    }
    static juce::Rectangle<float> patternPill(
        juce::Rectangle<float> area, int index)
    {
        area.removeFromTop(30.0f);
        auto row = area.removeFromTop(29.0f);
        const auto width = row.getWidth() / 4.0f;
        return juce::Rectangle<float>(
            row.getX() + width * static_cast<float>(index), row.getY(),
            width, row.getHeight()).reduced(3.0f, 2.0f);
    }
    static juce::Rectangle<float> sequenceBounds(juce::Rectangle<float> area)
    {
        area.removeFromTop(62.0f);
        area.removeFromBottom(area.getHeight() * 0.42f);
        return area.reduced(5.0f, 5.0f);
    }
    static juce::Rectangle<float> trackBounds(
        juce::Rectangle<float> area, int control)
    {
        area.removeFromTop(62.0f);
        auto tracks = area.removeFromBottom(area.getHeight() * 0.42f);
        static constexpr std::array<int, 8> order {
            1, 5, 6, 7, 8, 9, 10, 11
        };
        auto position = 0;
        while (position < static_cast<int>(order.size())
               && order[static_cast<size_t>(position)] != control)
            ++position;
        if (position >= static_cast<int>(order.size()))
            return {};
        const auto width = tracks.getWidth() * 0.25f;
        const auto height = tracks.getHeight() * 0.5f;
        return juce::Rectangle<float>(
            tracks.getX() + width * static_cast<float>(position % 4),
            tracks.getY() + height * static_cast<float>(position / 4),
            width, height).reduced(6.0f, 5.0f);
    }
    static juce::Rectangle<float> sequenceControlBounds(
        juce::Rectangle<float> area, int control)
    {
        auto row = sequenceBounds(area).removeFromTop(30.0f).reduced(7.0f, 3.0f);
        const auto width = row.getWidth() * 0.5f;
        if (control == 3)
            return row.removeFromLeft(width).reduced(3.0f, 0.0f);
        if (control == 4)
            return row.removeFromRight(width).reduced(3.0f, 0.0f);
        return {};
    }
    void choose(int control, int index, int count);
    BeatPermutationVisualEvents visualEvents {};
};

void BeatPermuterModuleView::choose(int control, int index, int count)
{
    if (auto* target = parameter(control))
    {
        graph.focusKeyboardControl(control);
        target->beginChangeGesture();
        target->setValueNotifyingHost(discreteValue(index, count));
        target->endChangeGesture();
    }
    repaint();
}

void BeatPermuterModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int index = 0; index < 6; ++index)
        if (gridPill(area, index).contains(event.position))
        {
            choose(0, index, 6);
            return;
        }
    for (int index = 0; index < 4; ++index)
        if (patternPill(area, index).contains(event.position))
        {
            choose(2, index, 4);
            return;
        }

    for (const auto control : { 3, 4 })
        if (sequenceControlBounds(area, control).contains(event.position))
            dragPrimary = control;
    for (const auto control : { 1, 5, 6, 7, 8, 9, 10, 11 })
        if (dragPrimary < 0
            && trackBounds(area, control).contains(event.position))
            dragPrimary = control;
    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void BeatPermuterModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto bounds = dragPrimary == 3 || dragPrimary == 4
                            ? sequenceControlBounds(area, dragPrimary)
                            : trackBounds(area, dragPrimary);
    setValue(dragPrimary, juce::jlimit(
        0.0f, 1.0f, (event.position.x - bounds.getX()) / bounds.getWidth()));
    updateDefaultDragReadout();
}

void BeatPermuterModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto defaults = moduleDefaults(type);
    for (int index = 0; index < 6; ++index)
        if (gridPill(area, index).contains(event.position))
        {
            setValue(0, defaults[0]);
            return;
        }
    for (int index = 0; index < 4; ++index)
        if (patternPill(area, index).contains(event.position))
        {
            setValue(2, defaults[2]);
            return;
        }
    for (const auto control : { 3, 4 })
        if (sequenceControlBounds(area, control).contains(event.position))
        {
            setValue(control, defaults[static_cast<size_t>(control)]);
            return;
        }
    for (const auto control : { 1, 5, 6, 7, 8, 9, 10, 11 })
        if (trackBounds(area, control).contains(event.position))
        {
            setValue(control, defaults[static_cast<size_t>(control)]);
            return;
        }
}

void BeatPermuterModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    static constexpr std::array<const char*, 6> grids {
        "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"
    };
    static constexpr std::array<const char*, 4> patterns {
        "REPEAT", "REVERSE", "ROTATE", "SHUFFLE"
    };
    static constexpr std::array<const char*, 4> patternHelp {
        "Loop one captured slice",
        "Walk backward and play in reverse",
        "Cycle through captured slices",
        "Choose a source for every step"
    };
    const auto selectedGrid = discreteIndex(value(0), 6);
    const auto selectedPattern = discreteIndex(value(2), 4);
    for (int index = 0; index < 6; ++index)
    {
        const auto bounds = gridPill(area, index);
        graphics.setColour(index == selectedGrid
                               ? accent : juce::Colour(0xff27313d));
        graphics.fillRoundedRectangle(bounds, 6.0f);
        graphics.setColour(index == selectedGrid
                               ? juce::Colour(0xff101820)
                               : juce::Colour(0xffb5c0cd));
        graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        graphics.drawText(grids[static_cast<size_t>(index)],
                          bounds.toNearestInt(),
                          juce::Justification::centred);
    }
    for (int index = 0; index < 4; ++index)
    {
        const auto bounds = patternPill(area, index);
        graphics.setColour(index == selectedPattern
                               ? outputColour : juce::Colour(0xff202a36));
        graphics.fillRoundedRectangle(bounds, 6.0f);
        graphics.setColour(index == selectedPattern
                               ? juce::Colour(0xff101820)
                               : juce::Colour(0xffb5c0cd));
        graphics.drawText(patterns[static_cast<size_t>(index)],
                          bounds.toNearestInt(),
                          juce::Justification::centred);
    }

    const auto sequence = sequenceBounds(area);
    graphics.setColour(juce::Colour(0xe6121821));
    graphics.fillRoundedRectangle(sequence, 8.0f);
    const auto eventSteps = juce::jlimit(
        1, 8, juce::roundToInt(1.0f + value(4) * 7.0f));
    const BeatPermutationVisualEvent* currentEvent = nullptr;
    for (const auto& event : visualEvents)
        if (event.sequence != 0 && event.progress < 1.0f
            && (currentEvent == nullptr
                || event.sequence > currentEvent->sequence))
            currentEvent = &event;

    for (const auto control : { 3, 4 })
    {
        const auto bounds = sequenceControlBounds(area, control);
        graphics.setColour(juce::Colour(0xff222c38));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour(
            (control == 3 ? outputColour : accent).withAlpha(0.62f));
        graphics.fillRoundedRectangle(
            bounds.withWidth(bounds.getWidth() * value(control)), 5.0f);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        graphics.drawText(
            juce::String(controlMetadata(type, control).label) + "  "
                + formatControlValue(type, control, value(control)),
            bounds.reduced(6.0f, 0.0f).toNearestInt(),
            juce::Justification::centred);
    }

    auto explanation = sequence.reduced(10.0f, 0.0f);
    explanation.removeFromTop(34.0f);
    auto explanationRow = explanation.removeFromTop(20.0f);
    graphics.setColour(juce::Colour(0xffa8b3c0));
    graphics.setFont(juce::FontOptions(10.0f));
    graphics.drawText(
        patternHelp[static_cast<size_t>(selectedPattern)],
        explanationRow.toNearestInt(), juce::Justification::centredLeft);
    graphics.drawText(
        "SOURCE 1 = NEWEST",
        explanationRow.toNearestInt(), juce::Justification::centredRight);

    auto cellsArea = explanation.reduced(0.0f, 5.0f);
    const auto cellWidth = cellsArea.getWidth() / 8.0f;
    const auto displayedSteps = currentEvent != nullptr
        ? juce::jlimit(1, 8, currentEvent->stepCount)
        : eventSteps;
    const auto playhead = currentEvent != nullptr
        ? juce::jlimit(0, displayedSteps - 1, currentEvent->currentStep)
        : -1;
    for (int cell = 0; cell < 8; ++cell)
    {
        const auto x = cellsArea.getX()
                       + cellWidth * static_cast<float>(cell);
        auto bounds = juce::Rectangle<float>(
            x + 2.0f, cellsArea.getY(), cellWidth - 4.0f,
            cellsArea.getHeight());
        const auto active = cell < displayedSteps;
        graphics.setColour(
            cell == playhead ? accent.withAlpha(0.75f)
                             : active ? outputColour.withAlpha(0.24f)
                                      : juce::Colours::white.withAlpha(0.05f));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour(juce::Colours::white.withAlpha(
            active ? 0.85f : 0.28f));
        graphics.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        auto sourceText = juce::String("?");
        auto reversed = false;
        if (currentEvent != nullptr && active)
        {
            const auto left =
                currentEvent->leftSourceSlices[static_cast<size_t>(cell)];
            const auto right =
                currentEvent->rightSourceSlices[static_cast<size_t>(cell)];
            sourceText = left == right
                ? juce::String(left)
                : juce::String(left) + "|" + juce::String(right);
            reversed =
                currentEvent->leftReverse[static_cast<size_t>(cell)]
                || currentEvent->rightReverse[static_cast<size_t>(cell)];
        }
        graphics.drawText(sourceText, bounds.toNearestInt(),
                          juce::Justification::centred);
        graphics.setFont(juce::FontOptions(8.0f, juce::Font::bold));
        graphics.drawText(
            active ? "STEP " + juce::String(cell + 1) : juce::String(),
            bounds.removeFromTop(15.0f).toNearestInt(),
            juce::Justification::centred);
        if (reversed)
            graphics.drawText("REV", bounds.removeFromBottom(14.0f).toNearestInt(),
                             juce::Justification::centred);
        const auto gateHeight = bounds.getHeight() * (0.2f + value(5) * 0.8f);
        graphics.setColour(accent.withAlpha(0.55f));
        graphics.fillRect(bounds.withY(bounds.getBottom() - gateHeight)
                                 .withHeight(2.0f));
    }

    for (const auto control : { 1, 5, 6, 7, 8, 9, 10, 11 })
    {
        auto bounds = trackBounds(area, control);
        graphics.setColour(juce::Colour(0xff151b24));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour(accent.withAlpha(0.56f));
        graphics.fillRoundedRectangle(
            bounds.withWidth(bounds.getWidth() * value(control)), 5.0f);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(9.5f));
        graphics.drawFittedText(
            juce::String(controlMetadata(type, control).label) + "  "
                + formatControlValue(type, control, value(control)),
            bounds.reduced(6.0f, 0.0f).toNearestInt(),
            juce::Justification::centred, 1);
    }
}
} // namespace

std::unique_ptr<ModuleView> createBeatPermuterView(EffectGraph& graph)
{
    return std::make_unique<BeatPermuterModuleView>(graph);
}
} // namespace megadsp::ui
