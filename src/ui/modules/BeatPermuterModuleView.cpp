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
            "Choose Grid and Pattern with named pills. The eight-cell display "
            "shows the capture window and emitted order. Drag Window or Repeats "
            "in the display, use the lower tracks for remaining controls, and "
            "double-click any control to restore its default.");
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

    if (sequenceBounds(area).contains(event.position))
        dragPrimary = event.position.y < sequenceBounds(area).getCentreY()
                          ? 3 : 4;
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
                            ? sequenceBounds(area)
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
    if (sequenceBounds(area).contains(event.position))
    {
        const auto control =
            event.position.y < sequenceBounds(area).getCentreY() ? 3 : 4;
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
        "REPEAT", "REVERSE", "ROTATE", "SCATTER"
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
    const auto cells = juce::jlimit(
        1, 8, juce::roundToInt(1.0f + value(3) * 7.0f));
    const auto repeats = juce::jlimit(
        1, 8, juce::roundToInt(1.0f + value(4) * 7.0f));
    const auto cellWidth = (sequence.getWidth() - 16.0f) / 8.0f;
    const BeatPermutationVisualEvent* currentEvent = nullptr;
    for (const auto& event : visualEvents)
        if (event.sequence != 0 && event.progress < 1.0f
            && (currentEvent == nullptr
                || event.sequence > currentEvent->sequence))
            currentEvent = &event;
    const auto playhead = currentEvent != nullptr
        ? juce::jlimit(0, 7, static_cast<int>(
              currentEvent->progress * static_cast<float>(repeats)))
        : -1;
    for (int cell = 0; cell < 8; ++cell)
    {
        const auto x = sequence.getX() + 8.0f
                       + cellWidth * static_cast<float>(cell);
        auto bounds = juce::Rectangle<float>(
            x + 2.0f, sequence.getY() + 28.0f, cellWidth - 4.0f,
            sequence.getHeight() - 49.0f);
        const auto active = cell < cells;
        graphics.setColour(
            cell == playhead ? accent.withAlpha(0.75f)
                             : active ? outputColour.withAlpha(0.24f)
                                      : juce::Colours::white.withAlpha(0.05f));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour(juce::Colours::white.withAlpha(
            active ? 0.85f : 0.28f));
        auto source = selectedPattern == 1 ? cells - 1 - cell % cells
            : selectedPattern == 2 ? (cell + repeats) % cells
            : selectedPattern == 3 ? (cell * 5 + repeats) % cells
                                   : cell % cells;
        if (currentEvent != nullptr && cell == playhead)
            source = juce::jlimit(
                0, cells - 1,
                juce::roundToInt(
                    currentEvent->sourcePosition
                    * static_cast<float>(juce::jmax(0, cells - 1))));
        graphics.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        graphics.drawText(juce::String(source + 1), bounds.toNearestInt(),
                          juce::Justification::centred);
        const auto gateHeight = bounds.getHeight() * (0.2f + value(5) * 0.8f);
        graphics.setColour(accent.withAlpha(0.55f));
        graphics.fillRect(bounds.withY(bounds.getBottom() - gateHeight)
                                  .withHeight(2.0f));
    }
    graphics.setColour(juce::Colour(0xffa8b3c0));
    graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    auto sequenceHeader = sequence;
    graphics.drawText(
        "CAPTURE WINDOW  " + juce::String(cells)
            + "     EVENT REPEATS  " + juce::String(repeats),
        sequenceHeader.removeFromTop(24.0f)
            .reduced(8.0f, 0.0f).toNearestInt(),
        juce::Justification::centredLeft);

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
