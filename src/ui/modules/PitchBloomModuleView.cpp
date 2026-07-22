#include "../GraphStyle.h"
#include "../ModuleView.h"
#include "../../modules/PitchBloom.h"

#include "ModuleViewCreators.h"

#include <array>
#include <cmath>

namespace megadsp::ui
{
namespace
{
class PitchBloomModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    bool usesFullPanel() const override { return true; }
    void configureAccessibility(juce::Component& component) const override
    {
        component.setTitle("Pitch Bloom interval field");
        component.setHelpText(
            "Choose an interval pill, then drag the bloom field horizontally "
            "for Delay and vertically for Bloom density. Live arcs are actual "
            "pitch-shifted repeats from the selected slot; their travel, "
            "height, brightness, and width report DSP interval, progress, "
            "energy, and stereo spread. Lower tracks edit fine tuning, "
            "feedback, stereo spread, passband, ducking, mix, and output. "
            "Double-click a region to restore its default.");
    }
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    static juce::Rectangle<float> intervalBounds(
        juce::Rectangle<float> area, int index)
    {
        auto row = area.removeFromTop(31.0f);
        const auto width = row.getWidth() / 5.0f;
        return juce::Rectangle<float>(
            row.getX() + width * static_cast<float>(index),
            row.getY(), width, row.getHeight()).reduced(3.0f, 2.0f);
    }
    static juce::Rectangle<float> bloomBounds(juce::Rectangle<float> area)
    {
        area.removeFromTop(33.0f);
        area.removeFromBottom(area.getHeight() * 0.44f);
        return area.reduced(5.0f);
    }
    static juce::Rectangle<float> fieldFeedbackBounds(
        juce::Rectangle<float> area)
    {
        return bloomBounds(area).removeFromBottom(28.0f).reduced(8.0f, 2.0f);
    }
    static juce::Rectangle<float> fieldSpreadBounds(
        juce::Rectangle<float> area)
    {
        auto bounds = bloomBounds(area);
        bounds.removeFromBottom(30.0f);
        return bounds.removeFromRight(28.0f).reduced(2.0f, 7.0f);
    }
    static juce::Rectangle<float> trackBounds(
        juce::Rectangle<float> area, int control)
    {
        area.removeFromTop(33.0f);
        auto tracks = area.removeFromBottom(area.getHeight() * 0.44f);
        static constexpr std::array<int, 8> order {
            1, 3, 5, 8, 6, 7, 9, 10
        };
        auto position = 0;
        while (position < static_cast<int>(order.size())
               && order[static_cast<size_t>(position)] != control)
            ++position;
        if (position == static_cast<int>(order.size()))
            return {};
        const auto width = tracks.getWidth() * 0.25f;
        const auto height = tracks.getHeight() * 0.5f;
        return juce::Rectangle<float>(
            tracks.getX() + width * static_cast<float>(position % 4),
            tracks.getY() + height * static_cast<float>(position / 4),
            width, height).reduced(6.0f, 4.0f);
    }
    void chooseInterval(int index);
};

void PitchBloomModuleView::chooseInterval(int index)
{
    if (auto* target = parameter(0))
    {
        graph.focusKeyboardControl(0);
        target->beginChangeGesture();
        target->setValueNotifyingHost(discreteValue(index, 5));
        target->endChangeGesture();
    }
    repaint();
}

void PitchBloomModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int index = 0; index < 5; ++index)
        if (intervalBounds(area, index).contains(event.position))
        {
            chooseInterval(index);
            return;
        }
    if (fieldFeedbackBounds(area).contains(event.position))
        dragPrimary = 3;
    else if (fieldSpreadBounds(area).contains(event.position))
        dragPrimary = 5;
    else if (bloomBounds(area).contains(event.position))
    {
        dragPrimary = 2;
        dragSecondary = 4;
    }
    for (const auto control : { 1, 3, 5, 8, 6, 7, 9, 10 })
        if (dragPrimary < 0
            && trackBounds(area, control).contains(event.position))
            dragPrimary = control;
    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void PitchBloomModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (dragSecondary == 4)
    {
        const auto bounds = bloomBounds(area);
        setValue(2, juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - bounds.getX()) / bounds.getWidth()));
        setValue(4, juce::jlimit(
            0.0f, 1.0f,
            (bounds.getBottom() - event.position.y) / bounds.getHeight()));
        dragReadout = "DELAY  " + formatControlValue(type, 2, value(2))
            + "    BLOOM  " + formatControlValue(type, 4, value(4));
        repaint();
        return;
    }
    const auto bounds = dragPrimary == 3
            ? fieldFeedbackBounds(area)
        : dragPrimary == 5
            ? fieldSpreadBounds(area)
            : trackBounds(area, dragPrimary);
    const auto normalized = dragPrimary == 5
        ? (bounds.getBottom() - event.position.y) / bounds.getHeight()
        : (event.position.x - bounds.getX()) / bounds.getWidth();
    setValue(dragPrimary, juce::jlimit(
        0.0f, 1.0f, normalized));
    updateDefaultDragReadout();
}

void PitchBloomModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int index = 0; index < 5; ++index)
        if (intervalBounds(area, index).contains(event.position))
        {
            resetToDefault(0);
            return;
        }
    if (fieldFeedbackBounds(area).contains(event.position))
    {
        resetToDefault(3);
        return;
    }
    if (fieldSpreadBounds(area).contains(event.position))
    {
        resetToDefault(5);
        return;
    }
    if (bloomBounds(area).contains(event.position))
    {
        resetToDefault(2);
        resetToDefault(4);
        return;
    }
    for (const auto control : { 1, 3, 5, 8, 6, 7, 9, 10 })
        if (trackBounds(area, control).contains(event.position))
        {
            resetToDefault(control);
            return;
        }
}

void PitchBloomModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    static constexpr std::array<const char*, 5> intervals {
        "UNISON", "FIFTH", "OCTAVE", "OCT + FIFTH", "TWO OCT"
    };
    const auto selected = discreteIndex(value(0), 5);
    for (int index = 0; index < 5; ++index)
    {
        const auto bounds = intervalBounds(area, index);
        graphics.setColour(index == selected
                               ? accent : juce::Colour(0xff27313d));
        graphics.fillRoundedRectangle(bounds, 6.0f);
        graphics.setColour(index == selected
                               ? juce::Colour(0xff101820)
                               : juce::Colours::white);
        graphics.setFont(juce::FontOptions(8.8f, juce::Font::bold));
        graphics.drawText(intervals[static_cast<size_t>(index)],
                          bounds.toNearestInt(),
                          juce::Justification::centred);
    }

    const auto field = bloomBounds(area);
    graphics.setColour(juce::Colour(0xe6121821));
    graphics.fillRoundedRectangle(field, 8.0f);
    drawWaveforms(graphics, field.reduced(7.0f));
    const auto origin = juce::Point<float>(
        field.getX() + 12.0f, field.getBottom() - 15.0f);
    EventTelemetrySnapshot telemetry;
    const auto hasTelemetry =
        readEventTelemetry(telemetry) && telemetry.sequence != 0;
    if (hasTelemetry)
    {
        for (std::uint32_t index = 0; index < telemetry.eventCount; ++index)
        {
            const auto& event = telemetry.events[index];
            if (event.kind != static_cast<std::uint32_t>(
                    PitchBloomTelemetryEventKind::shiftedRepeat))
                continue;
            const auto progress = juce::jlimit(
                0.0f, 1.0f, event.progress);
            const auto semitones = event.values[static_cast<size_t>(
                PitchBloomTelemetryValue::intervalSemitones)];
            const auto energy = event.values[static_cast<size_t>(
                PitchBloomTelemetryValue::energy)];
            const auto spread = event.values[static_cast<size_t>(
                PitchBloomTelemetryValue::stereoSpread)];
            const auto pitchHeight = juce::jmap(
                juce::jlimit(-1.0f, 24.5f, semitones),
                -1.0f, 24.5f, field.getHeight() * 0.04f,
                field.getHeight() * 0.62f);
            const auto point = juce::Point<float>(
                juce::jmap(progress, origin.x, field.getRight() - 12.0f),
                origin.y - pitchHeight * progress
                    + event.position[0] * spread * 10.0f);
            const auto level = juce::jlimit(
                0.0f, 1.0f,
                std::sqrt(juce::jmax(0.0f, energy)) * 2.4f);
            graphics.setColour(
                (event.position[0] < 0.0f ? accent : outputColour)
                    .withAlpha(0.18f + level * 0.72f));
            juce::Path path;
            path.startNewSubPath(origin);
            path.quadraticTo(
                (origin.x + point.x) * 0.5f,
                point.y + field.getHeight() * 0.18f,
                point.x, point.y);
            graphics.strokePath(
                path, juce::PathStrokeType(1.0f + level * 1.7f));
            graphics.drawLine(
                point.x - spread * 10.0f, point.y,
                point.x + spread * 10.0f, point.y,
                1.0f + level);
            graphics.fillEllipse(
                juce::Rectangle<float>(4.0f + level * 7.0f,
                                       4.0f + level * 7.0f)
                    .withCentre(point));
        }
    }
    else
    {
        graphics.setColour(juce::Colours::white.withAlpha(0.55f));
        graphics.drawText(
            "DSP EVENT TELEMETRY WAITING", field.reduced(8.0f).toNearestInt(),
            juce::Justification::centredBottom);
    }
    const auto node = juce::Point<float>(
        field.getX() + value(2) * field.getWidth(),
        field.getBottom() - value(4) * field.getHeight());
    graphics.setColour(juce::Colours::white);
    graphics.fillEllipse(
        juce::Rectangle<float>(14.0f, 14.0f).withCentre(node));
    graphics.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    graphics.drawText(
        formatControlValue(type, 2, value(2)) + "  /  "
            + formatControlValue(type, 4, value(4)),
        juce::Rectangle<float>(170.0f, 18.0f)
            .withCentre(node.translated(0.0f, -15.0f))
            .constrainedWithin(field).toNearestInt(),
        juce::Justification::centred);

    const auto feedbackRail = fieldFeedbackBounds(area);
    graphics.setColour(juce::Colour(0xff2d3745));
    graphics.fillRoundedRectangle(feedbackRail, 4.0f);
    graphics.setColour(accent.withAlpha(0.65f));
    graphics.fillRoundedRectangle(
        feedbackRail.withWidth(feedbackRail.getWidth() * value(3)), 4.0f);
    graphics.setColour(juce::Colours::white);
    graphics.setFont(juce::FontOptions(8.5f, juce::Font::bold));
    graphics.drawText(
        "FEEDBACK  " + formatControlValue(type, 3, value(3)),
        feedbackRail.toNearestInt(), juce::Justification::centred);

    const auto spreadRail = fieldSpreadBounds(area);
    graphics.setColour(juce::Colour(0xff2d3745));
    graphics.fillRoundedRectangle(spreadRail, 4.0f);
    graphics.setColour(outputColour.withAlpha(0.68f));
    const auto spreadFill = spreadRail.withTop(
        spreadRail.getBottom() - spreadRail.getHeight() * value(5));
    graphics.fillRoundedRectangle(spreadFill, 4.0f);

    for (const auto control : { 1, 3, 5, 8, 6, 7, 9, 10 })
    {
        const auto bounds = trackBounds(area, control);
        graphics.setColour(juce::Colour(0xff151b24));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour((control == 9 || control == 10
                                ? outputColour : accent).withAlpha(0.58f));
        graphics.fillRoundedRectangle(
            bounds.withWidth(bounds.getWidth() * value(control)), 5.0f);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(9.2f));
        graphics.drawFittedText(
            juce::String(controlMetadata(type, control).label) + "  "
                + formatControlValue(type, control, value(control)),
            bounds.reduced(6.0f, 0.0f).toNearestInt(),
            juce::Justification::centred, 1);
    }
}
} // namespace

std::unique_ptr<ModuleView> createPitchBloomView(EffectGraph& graph)
{
    return std::make_unique<PitchBloomModuleView>(graph);
}
} // namespace megadsp::ui
