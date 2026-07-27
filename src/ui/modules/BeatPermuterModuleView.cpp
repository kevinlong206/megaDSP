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
            "Choose Slice and Pattern, then edit the Sequence, Trigger, "
            "Transform, and Output bands. The display shows the actual source "
            "slice and reverse state for each output step. Source 1 is newest. "
            "Double-click any control to restore its default.");
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
    static juce::Rectangle<float> commandBounds(
        juce::Rectangle<float> area)
    {
        return area.removeFromTop(32.0f);
    }
    static juce::Rectangle<float> choiceGroupBounds(
        juce::Rectangle<float> area, int control)
    {
        auto row = commandBounds(area);
        const auto sliceWidth = row.getWidth() * 0.52f;
        return (control == 0 ? row.removeFromLeft(sliceWidth) : row)
            .reduced(2.0f, 1.0f);
    }
    static juce::Rectangle<float> choicePill(
        juce::Rectangle<float> area, int control, int index)
    {
        auto row = choiceGroupBounds(area, control);
        row.removeFromLeft(control == 0 ? 34.0f : 48.0f);
        const auto count = control == 0 ? 6 : 4;
        const auto width = row.getWidth() / static_cast<float>(count);
        return juce::Rectangle<float>(
            row.getX() + width * static_cast<float>(index), row.getY(),
            width, row.getHeight()).reduced(1.5f, 2.0f);
    }
    static juce::Rectangle<float> sequenceBounds(juce::Rectangle<float> area)
    {
        area.removeFromTop(34.0f);
        area.removeFromBottom(82.0f);
        return area.reduced(2.0f, 3.0f);
    }
    static int bandIndex(int control)
    {
        if (control == 3 || control == 4)
            return 0;
        if (control == 1 || control == 5)
            return 1;
        if (control >= 6 && control <= 8)
            return 2;
        if (control >= 9 && control <= 11)
            return 3;
        return -1;
    }
    static juce::Rectangle<float> bandBounds(
        juce::Rectangle<float> area, int band)
    {
        area.removeFromTop(34.0f);
        auto row = area.removeFromBottom(82.0f);
        const auto width = row.getWidth() * 0.25f;
        return juce::Rectangle<float>(
            row.getX() + width * static_cast<float>(band), row.getY(),
            width, row.getHeight()).reduced(3.0f, 0.0f);
    }
    static juce::Rectangle<float> trackBounds(
        juce::Rectangle<float> area, int control)
    {
        static constexpr std::array<std::array<int, 3>, 4> controls {{
            {{ 3, 4, -1 }}, {{ 1, 5, -1 }},
            {{ 6, 7, 8 }}, {{ 9, 10, 11 }}
        }};
        const auto band = bandIndex(control);
        if (band < 0)
            return {};
        auto tracks = bandBounds(area, band);
        tracks.removeFromTop(15.0f);
        const auto count = band < 2 ? 2 : 3;
        const auto height = tracks.getHeight() / static_cast<float>(count);
        for (int row = 0; row < count; ++row)
            if (controls[static_cast<size_t>(band)][static_cast<size_t>(row)]
                == control)
                return juce::Rectangle<float>(
                    tracks.getX(),
                    tracks.getY() + height * static_cast<float>(row),
                    tracks.getWidth(), height).reduced(3.0f, 2.0f);
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
        if (choicePill(area, 0, index).contains(event.position))
        {
            choose(0, index, 6);
            return;
        }
    for (int index = 0; index < 4; ++index)
        if (choicePill(area, 2, index).contains(event.position))
        {
            choose(2, index, 4);
            return;
        }

    for (const auto control : { 1, 3, 4, 5, 6, 7, 8, 9, 10, 11 })
        if (trackBounds(area, control).contains(event.position))
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
    const auto bounds = trackBounds(area, dragPrimary);
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
        if (choicePill(area, 0, index).contains(event.position))
        {
            setValue(0, defaults[0]);
            return;
        }
    for (int index = 0; index < 4; ++index)
        if (choicePill(area, 2, index).contains(event.position))
        {
            setValue(2, defaults[2]);
            return;
        }
    for (const auto control : { 1, 3, 4, 5, 6, 7, 8, 9, 10, 11 })
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
    const auto selectedGrid = discreteIndex(value(0), 6);
    const auto selectedPattern = discreteIndex(value(2), 4);
    for (const auto control : { 0, 2 })
    {
        const auto bounds = choiceGroupBounds(area, control);
        graphics.setColour(juce::Colour(0xcc151c25));
        graphics.fillRoundedRectangle(bounds, 6.0f);
        graphics.setColour(juce::Colours::white.withAlpha(0.62f));
        graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        graphics.drawText(control == 0 ? "SLICE" : "PATTERN",
                          bounds.toNearestInt(),
                          juce::Justification::centredLeft);
    }
    for (int index = 0; index < 6; ++index)
    {
        const auto bounds = choicePill(area, 0, index);
        graphics.setColour(index == selectedGrid
                               ? accent : juce::Colour(0xff27313d));
        graphics.fillRoundedRectangle(bounds, 4.0f);
        graphics.setColour(index == selectedGrid
                               ? juce::Colour(0xff101820)
                               : juce::Colour(0xffb5c0cd));
        graphics.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        graphics.drawText(grids[static_cast<size_t>(index)],
                          bounds.toNearestInt(),
                          juce::Justification::centred);
    }
    for (int index = 0; index < 4; ++index)
    {
        const auto bounds = choicePill(area, 2, index);
        graphics.setColour(index == selectedPattern
                               ? outputColour : juce::Colour(0xff202a36));
        graphics.fillRoundedRectangle(bounds, 4.0f);
        graphics.setColour(index == selectedPattern
                               ? juce::Colour(0xff101820)
                               : juce::Colour(0xffb5c0cd));
        graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        graphics.drawText(patterns[static_cast<size_t>(index)],
                          bounds.toNearestInt(),
                          juce::Justification::centred);
    }

    const auto sequence = sequenceBounds(area);
    graphics.setColour(juce::Colour(0xe6121821));
    graphics.fillRoundedRectangle(sequence, 8.0f);
    auto sequenceContent = sequence.reduced(8.0f, 4.0f);
    auto sequenceHeader = sequenceContent.removeFromTop(17.0f);
    graphics.setColour(juce::Colours::white.withAlpha(0.58f));
    graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    graphics.drawText("OUTPUT STEP", sequenceHeader.toNearestInt(),
                      juce::Justification::centredLeft);
    graphics.drawText("SOURCE  (1 NEWEST)   REV = REVERSE",
                      sequenceHeader.toNearestInt(),
                      juce::Justification::centredRight);

    const auto eventSteps = juce::jlimit(
        1, 8, juce::roundToInt(1.0f + value(4) * 7.0f));
    const BeatPermutationVisualEvent* currentEvent = nullptr;
    for (const auto& event : visualEvents)
        if (event.sequence != 0 && event.progress < 1.0f
            && (currentEvent == nullptr
                || event.sequence > currentEvent->sequence))
            currentEvent = &event;

    auto cellsArea = sequenceContent.reduced(0.0f, 3.0f);
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
            cell == playhead ? accent.withAlpha(0.22f)
                             : active ? outputColour.withAlpha(0.13f)
                                      : juce::Colours::white.withAlpha(0.05f));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        if (cell == playhead)
        {
            graphics.setColour(accent.withAlpha(0.9f));
            graphics.drawRoundedRectangle(bounds, 5.0f, 1.5f);
        }
        graphics.setColour(juce::Colours::white.withAlpha(
            active ? 0.85f : 0.28f));
        graphics.setFont(juce::FontOptions(15.0f, juce::Font::bold));
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
            active ? juce::String(cell + 1) : juce::String(),
            bounds.removeFromTop(15.0f).toNearestInt(),
            juce::Justification::centred);
        if (reversed)
        {
            graphics.setColour(accent.withAlpha(0.9f));
            graphics.drawText(
                "REV", bounds.removeFromBottom(14.0f).toNearestInt(),
                juce::Justification::centred);
        }
    }

    static constexpr std::array<const char*, 4> bandNames {
        "SEQUENCE", "TRIGGER", "TRANSFORM", "OUTPUT"
    };
    const std::array<juce::Colour, 4> bandColours {
        outputColour, accent, juce::Colour(0xffb693f6),
        juce::Colour(0xff75d6b1)
    };
    for (int band = 0; band < 4; ++band)
    {
        auto bounds = bandBounds(area, band);
        graphics.setColour(juce::Colour(0xb8121821));
        graphics.fillRoundedRectangle(bounds, 6.0f);
        graphics.setColour(
            bandColours[static_cast<size_t>(band)].withAlpha(0.82f));
        graphics.setFont(juce::FontOptions(8.5f, juce::Font::bold));
        graphics.drawText(
            bandNames[static_cast<size_t>(band)],
            bounds.removeFromTop(15.0f).reduced(4.0f, 0.0f).toNearestInt(),
            juce::Justification::centredLeft);
    }
    for (const auto control : { 1, 3, 4, 5, 6, 7, 8, 9, 10, 11 })
    {
        auto bounds = trackBounds(area, control);
        graphics.setColour(juce::Colour(0xff151b24));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour(
            bandColours[static_cast<size_t>(bandIndex(control))]
                .withAlpha(0.52f));
        graphics.fillRoundedRectangle(
            bounds.withWidth(bounds.getWidth() * value(control)), 5.0f);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(9.0f));
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
