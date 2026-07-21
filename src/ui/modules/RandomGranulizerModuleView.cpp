#include "../GraphStyle.h"
#include "../ModuleView.h"

#include "ModuleViewCreators.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace megadsp::ui
{
namespace
{
class RandomGranulizerModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    void configureAccessibility(juce::Component&) const override;
    bool usesFullPanel() const override { return true; }
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void timerCallback() override;

private:
    static juce::Rectangle<float> grainTimelineBounds(
        juce::Rectangle<float>);
    static juce::Rectangle<float> grainControlBounds(
        juce::Rectangle<float>, int control);
    static juce::Rectangle<float> grainSizeBounds(
        juce::Rectangle<float>);
    static juce::Rectangle<float> captureRangeBounds(
        juce::Rectangle<float>);
    static juce::Rectangle<float> stereoSpreadBounds(
        juce::Rectangle<float>);
    static juce::Rectangle<float> rhythmicDelayBounds(
        juce::Rectangle<float>);

    GrainVisualEvents grainEvents {};
};

void RandomGranulizerModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Random Granulizer grain stream");
    component.setHelpText(
        "The upper timeline shows recent grains: horizontal position "
        "is Capture Range, vertical position is Stereo Spread, width is size, "
        "arrow direction is playback direction, hue is filtering, and "
        "the label is duration. Drag Capture Range, Stereo Spread, and "
        "Rhythmic Delay Chance directly on the timeline. Drag either "
        "MIN/MAX size-window handle through the other, or a lower track; "
        "double-click it to restore its default.");
}

juce::Rectangle<float> RandomGranulizerModuleView::grainTimelineBounds(
    juce::Rectangle<float> area)
{
    area.removeFromBottom(area.getHeight() * 0.48f);
    return area.reduced(4.0f, 4.0f);
}

juce::Rectangle<float> RandomGranulizerModuleView::grainControlBounds(
    juce::Rectangle<float> area, int control)
{
    auto controls = area.removeFromBottom(area.getHeight() * 0.48f);
    const auto sizeHeight = controls.getHeight() / 3.0f;
    if (control == 1 || control == 4)
        return controls.removeFromTop(sizeHeight).reduced(7.0f, 6.0f);
    static constexpr std::array<int, 7> controlOrder {
        0, 2, 5, 8, 9, 10, 11
    };
    const auto position = static_cast<int>(std::distance(
        controlOrder.begin(),
        std::find(controlOrder.begin(), controlOrder.end(), control)));
    controls.removeFromTop(sizeHeight);
    const auto row = position / 4;
    const auto column = position % 4;
    const auto width = controls.getWidth() * 0.25f;
    const auto height = controls.getHeight() * 0.5f;
    return juce::Rectangle<float>(
        controls.getX() + width * static_cast<float>(column),
        controls.getY() + height * static_cast<float>(row),
        width, height).reduced(7.0f, 6.0f);
}

juce::Rectangle<float> RandomGranulizerModuleView::grainSizeBounds(
    juce::Rectangle<float> area)
{
    return grainControlBounds(area, 1);
}

juce::Rectangle<float> RandomGranulizerModuleView::captureRangeBounds(
    juce::Rectangle<float> area)
{
    return grainTimelineBounds(area)
        .removeFromTop(23.0f).reduced(5.0f, 2.0f);
}

juce::Rectangle<float> RandomGranulizerModuleView::stereoSpreadBounds(
    juce::Rectangle<float> area)
{
    auto timeline = grainTimelineBounds(area);
    timeline.removeFromTop(25.0f);
    timeline.removeFromBottom(23.0f);
    return timeline.removeFromRight(22.0f).reduced(2.0f);
}

juce::Rectangle<float> RandomGranulizerModuleView::rhythmicDelayBounds(
    juce::Rectangle<float> area)
{
    return grainTimelineBounds(area)
        .removeFromBottom(21.0f).reduced(5.0f, 2.0f);
}

void RandomGranulizerModuleView::mouseDown(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto sizeTrack = grainSizeBounds(area);
    if (sizeTrack.contains(event.position))
    {
        const auto firstX =
            sizeTrack.getX() + value(1) * sizeTrack.getWidth();
        const auto secondX =
            sizeTrack.getX() + value(4) * sizeTrack.getWidth();
        dragPrimary = std::abs(event.position.x - firstX)
                          <= std::abs(event.position.x - secondX)
            ? 1 : 4;
    }
    if (dragPrimary < 0
        && captureRangeBounds(area).contains(event.position))
        dragPrimary = 3;
    if (dragPrimary < 0
        && stereoSpreadBounds(area).contains(event.position))
        dragPrimary = 6;
    if (dragPrimary < 0
        && rhythmicDelayBounds(area).contains(event.position))
        dragPrimary = 7;
    for (int control = 0; control < controlsPerSlot; ++control)
    {
        if (dragPrimary >= 0 || control == 1 || control == 3
            || control == 4 || control == 6 || control == 7)
            continue;
        if (grainControlBounds(area, control).contains(event.position))
        {
            dragPrimary = control;
            break;
        }
    }
    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void RandomGranulizerModuleView::mouseDrag(
    const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (dragPrimary == 3)
    {
        const auto track = captureRangeBounds(area);
        setValue(3, juce::jlimit(
            0.0f, 1.0f,
            (track.getRight() - event.position.x) / track.getWidth()));
        updateDefaultDragReadout();
        return;
    }
    if (dragPrimary == 6)
    {
        const auto track = stereoSpreadBounds(area);
        setValue(6, juce::jlimit(
            0.0f, 1.0f,
            std::abs(event.position.y - track.getCentreY())
                / (track.getHeight() * 0.5f)));
        updateDefaultDragReadout();
        return;
    }
    if (dragPrimary == 7)
    {
        const auto track = rhythmicDelayBounds(area);
        setValue(7, juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - track.getX()) / track.getWidth()));
        updateDefaultDragReadout();
        return;
    }
    const auto track = grainControlBounds(area, dragPrimary);
    setValue(dragPrimary, juce::jlimit(
        0.0f, 1.0f,
        (event.position.x - track.getX()) / track.getWidth()));
    if (dragPrimary == 1 || dragPrimary == 4)
    {
        const auto low = juce::jmin(value(1), value(4));
        const auto high = juce::jmax(value(1), value(4));
        dragReadout = "MIN  " + formatControlValue(type, 1, low)
            + "    MAX  " + formatControlValue(type, 4, high);
        repaint();
    }
    else
        updateDefaultDragReadout();
}

void RandomGranulizerModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto defaults = moduleDefaults(type);
    if (grainSizeBounds(area).contains(event.position))
    {
        setValue(1, defaults[1]);
        setValue(4, defaults[4]);
        repaint();
        return;
    }
    for (const auto control : { 3, 6, 7 })
    {
        const auto bounds = control == 3 ? captureRangeBounds(area)
            : control == 6 ? stereoSpreadBounds(area)
                           : rhythmicDelayBounds(area);
        if (bounds.contains(event.position))
        {
            setValue(control, defaults[static_cast<size_t>(control)]);
            repaint();
            return;
        }
    }
    for (int control = 0; control < controlsPerSlot; ++control)
    {
        if (control == 1 || control == 3 || control == 4
            || control == 6 || control == 7)
            continue;
        if (grainControlBounds(area, control).contains(event.position))
        {
            setValue(control, defaults[static_cast<size_t>(control)]);
            repaint();
            return;
        }
    }
}

void RandomGranulizerModuleView::timerCallback()
{
    grainEvents = processor.getRack().grainVisualEvents(slot);
}

void RandomGranulizerModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto timelineFrame = grainTimelineBounds(area);
    auto timeline = timelineFrame;
    timeline.removeFromTop(25.0f);
    timeline.removeFromBottom(23.0f);
    timeline.removeFromRight(24.0f);
    timeline.reduce(3.0f, 2.0f);
    graphics.setColour(juce::Colour(0xff151b24).withAlpha(0.92f));
    graphics.fillRoundedRectangle(timelineFrame, 7.0f);

    const auto captureRail = captureRangeBounds(area);
    const auto captureX =
        captureRail.getRight() - value(3) * captureRail.getWidth();
    graphics.setColour(juce::Colour(0xff2d3745));
    graphics.fillRoundedRectangle(captureRail, 4.0f);
    graphics.setColour(accent.withAlpha(0.28f));
    graphics.fillRoundedRectangle(
        captureRail.withLeft(captureX), 4.0f);
    graphics.setColour(juce::Colours::white);
    graphics.fillEllipse(
        captureX - 4.0f, captureRail.getCentreY() - 4.0f,
        8.0f, 8.0f);
    graphics.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    graphics.drawText(
        "CAPTURE RANGE  "
            + megadsp::formatControlValue(type, 3, value(3))
            + "    ← RECENT INPUT · NOW →",
        captureRail.reduced(7.0f, 0.0f).toNearestInt(),
        juce::Justification::centred);

    const auto spreadRail = stereoSpreadBounds(area);
    graphics.setColour(juce::Colour(0xff2d3745));
    graphics.fillRoundedRectangle(spreadRail, 4.0f);
    const auto spreadDistance =
        value(6) * spreadRail.getHeight() * 0.5f;
    graphics.setColour(outputColour.withAlpha(0.48f));
    graphics.drawLine(
        spreadRail.getCentreX(),
        spreadRail.getCentreY() - spreadDistance,
        spreadRail.getCentreX(),
        spreadRail.getCentreY() + spreadDistance, 4.0f);
    graphics.setColour(juce::Colours::white);
    for (const auto direction : { -1.0f, 1.0f })
        graphics.fillEllipse(
            spreadRail.getCentreX() - 4.0f,
            spreadRail.getCentreY() + direction * spreadDistance - 4.0f,
            8.0f, 8.0f);

    const auto delayRail = rhythmicDelayBounds(area);
    graphics.setColour(juce::Colour(0xff2d3745));
    graphics.fillRoundedRectangle(delayRail, 4.0f);
    graphics.setColour(accent.withAlpha(0.32f));
    graphics.fillRoundedRectangle(
        delayRail.withWidth(delayRail.getWidth() * value(7)), 4.0f);
    const auto delayX =
        delayRail.getX() + value(7) * delayRail.getWidth();
    graphics.setColour(juce::Colours::white);
    graphics.fillEllipse(
        delayX - 4.0f, delayRail.getCentreY() - 4.0f,
        8.0f, 8.0f);
    graphics.drawText(
        "RHYTHMIC DELAY CHANCE  "
            + megadsp::formatControlValue(type, 7, value(7)),
        delayRail.reduced(7.0f, 0.0f).toNearestInt(),
        juce::Justification::centred);

    const auto centreY = timeline.getCentreY();
    graphics.setColour(juce::Colours::white.withAlpha(0.12f));
    graphics.drawHorizontalLine(
        juce::roundToInt(centreY), timeline.getX(), timeline.getRight());
    graphics.setFont(juce::FontOptions(10.0f));
    graphics.setColour(juce::Colour(0xff7d8998));
    graphics.drawText("L", timeline.withWidth(18.0f).toNearestInt(),
                      juce::Justification::centredTop);
    graphics.drawText("R", timeline.withWidth(18.0f).toNearestInt(),
                      juce::Justification::centredBottom);
    graphics.drawFittedText(
        "SPREAD\n"
            + megadsp::formatControlValue(type, 6, value(6)),
        spreadRail.toNearestInt().expanded(2, 0),
        juce::Justification::centred, 2);

    auto newestSequence = std::uint32_t {};
    for (const auto& event : grainEvents)
        newestSequence = juce::jmax(newestSequence, event.sequence);
    for (const auto& event : grainEvents)
    {
        if (event.sequence == 0)
            continue;
        const auto recency = juce::jlimit(
            0.0f, 1.0f,
            1.0f - static_cast<float>(newestSequence - event.sequence)
                       / static_cast<float>(
                          megadsp::grainVisualEventCount));
        const auto x = timeline.getRight()
                       - event.historyPosition * timeline.getWidth() * 0.88f
                       - 10.0f;
        const auto y = centreY
                       + event.pan * timeline.getHeight() * 0.36f;
        const auto size = juce::jlimit(
            0.0f, 1.0f,
            std::log(juce::jlimit(
                50.0f, 2000.0f, event.durationSeconds * 1000.0f) / 50.0f)
                / std::log(40.0f));
        const auto width = juce::jmap(
            size, 0.0f, 1.0f, 18.0f,
            juce::jmax(28.0f, timeline.getWidth() * 0.22f));
        const auto height = 8.0f + 5.0f * size;
        auto grain = juce::Rectangle<float>(
            x - width * (event.reverse ? 0.0f : 1.0f),
            y - height * 0.5f, width, height)
                         .constrainedWithin(timeline.reduced(3.0f));
        const auto hue = juce::jmap(event.filter, 0.0f, 1.0f,
                                   0.56f, 0.38f);
        const auto colour = juce::Colour::fromHSV(
            hue, 0.68f, 0.95f,
            juce::jlimit(0.18f, 0.92f,
                         recency * (event.progress < 1.0f ? 1.0f : 0.62f)));
        graphics.setColour(colour.withAlpha(0.22f));
        graphics.fillRoundedRectangle(grain.expanded(2.0f), 4.0f);
        graphics.setColour(colour);
        graphics.fillRoundedRectangle(grain, 3.0f);
        const auto arrowX = event.reverse ? grain.getX() : grain.getRight();
        juce::Path arrow;
        arrow.startNewSubPath(
            arrowX, grain.getCentreY());
        arrow.lineTo(
            arrowX + (event.reverse ? 6.0f : -6.0f),
            grain.getCentreY() - 4.0f);
        arrow.lineTo(
            arrowX + (event.reverse ? 6.0f : -6.0f),
            grain.getCentreY() + 4.0f);
        arrow.closeSubPath();
        graphics.fillPath(arrow);
        graphics.setColour(juce::Colours::white.withAlpha(0.9f));
        graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        graphics.drawText(
            event.durationSeconds < 1.0f
                ? juce::String(event.durationSeconds * 1000.0f, 0) + " ms"
                : juce::String(event.durationSeconds, 2) + " s",
            grain.toNearestInt(), juce::Justification::centred);
        if (event.progress < 1.0f)
        {
            graphics.setColour(juce::Colours::white.withAlpha(0.78f));
            graphics.drawVerticalLine(
                juce::roundToInt(
                    grain.getX() + event.progress * grain.getWidth()),
                grain.getY() - 2.0f, grain.getBottom() + 2.0f);
        }
    }

    auto sizeTrack = grainSizeBounds(area);
    auto sizeHeader = sizeTrack.removeFromTop(22.0f);
    const auto lowSize = juce::jmin(value(1), value(4));
    const auto highSize = juce::jmax(value(1), value(4));
    graphics.setColour(juce::Colour(0xffa8b3c0));
    graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    graphics.drawText("SIZE WINDOW", sizeHeader.toNearestInt(),
                      juce::Justification::centredLeft);
    graphics.setColour(juce::Colours::white.withAlpha(0.9f));
    graphics.setFont(juce::FontOptions(10.0f));
    graphics.drawText(
        "MIN  " + megadsp::formatControlValue(type, 1, lowSize)
            + "     MAX  "
            + megadsp::formatControlValue(type, 4, highSize),
        sizeHeader.toNearestInt(), juce::Justification::centredRight);
    auto sizeRail = sizeTrack.withHeight(10.0f).withCentre(
        sizeTrack.getCentre());
    graphics.setColour(juce::Colour(0xff2d3745));
    graphics.fillRoundedRectangle(sizeRail, 5.0f);
    const auto lowX = sizeRail.getX() + lowSize * sizeRail.getWidth();
    const auto highX = sizeRail.getX() + highSize * sizeRail.getWidth();
    graphics.setColour(accent.withAlpha(0.72f));
    graphics.fillRoundedRectangle(
        juce::Rectangle<float>(
            lowX, sizeRail.getY(), juce::jmax(3.0f, highX - lowX),
            sizeRail.getHeight()),
        5.0f);
    graphics.setColour(juce::Colours::white);
    graphics.fillEllipse(lowX - 6.0f, sizeRail.getCentreY() - 6.0f,
                         12.0f, 12.0f);
    graphics.setColour(outputColour);
    graphics.fillEllipse(highX - 6.0f, sizeRail.getCentreY() - 6.0f,
                         12.0f, 12.0f);

    for (int control = 0; control < megadsp::controlsPerSlot; ++control)
    {
        if (control == 1 || control == 3 || control == 4
            || control == 6 || control == 7)
            continue;
        auto track = grainControlBounds(area, control);
        const auto label = juce::String(
            megadsp::controlMetadata(type, control).label);
        graphics.setColour(juce::Colour(0xffa8b3c0));
        graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        graphics.drawText(
            label, track.removeFromTop(17.0f).toNearestInt(),
            juce::Justification::centredLeft);
        graphics.setColour(juce::Colours::white.withAlpha(0.88f));
        graphics.setFont(juce::FontOptions(10.0f));
        graphics.drawText(
            megadsp::formatControlValue(type, control, value(control)),
            track.removeFromTop(17.0f).toNearestInt(),
            juce::Justification::centredRight);
        auto rail = track.withHeight(8.0f).withCentre(track.getCentre());
        graphics.setColour(juce::Colour(0xff2d3745));
        graphics.fillRoundedRectangle(rail, 4.0f);
        graphics.setColour(
            (control == 5 || control == 8)
                ? outputColour : accent);
        graphics.fillRoundedRectangle(
            rail.withWidth(
                juce::jmax(4.0f, rail.getWidth() * value(control))),
            4.0f);
        const auto handleX = rail.getX() + value(control) * rail.getWidth();
        graphics.setColour(juce::Colours::white);
        graphics.fillEllipse(handleX - 5.0f, rail.getCentreY() - 5.0f,
                            10.0f, 10.0f);
    }
}
} // namespace

std::unique_ptr<ModuleView> createRandomGranulizerView(EffectGraph& graph)
{
    return std::make_unique<RandomGranulizerModuleView>(graph);
}
} // namespace megadsp::ui
