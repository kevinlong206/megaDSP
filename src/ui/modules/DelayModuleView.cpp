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
class DelayModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    void configureAccessibility(juce::Component&) const override;
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    static juce::Rectangle<float> timelineBounds(juce::Rectangle<float>);
    static juce::Rectangle<float> movementBounds(juce::Rectangle<float>);
};

void DelayModuleView::configureAccessibility(juce::Component& component) const
{
    component.setTitle("Delay tap and movement editor");
    component.setHelpText(
        "Free mode exposes Time and Tempo mode exposes Division; the inactive "
        "value is retained. Drag the delay handle in Free mode. Drag the "
        "Movement field horizontally for Rate and vertically for Depth. "
        "Double-click a handle to reset it.");
}

juce::Rectangle<float> DelayModuleView::timelineBounds(
    juce::Rectangle<float> area)
{
    area.removeFromBottom(54.0f);
    return area;
}

juce::Rectangle<float> DelayModuleView::movementBounds(
    juce::Rectangle<float> area)
{
    return area.removeFromBottom(46.0f).reduced(5.0f, 2.0f);
}

void DelayModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto movement = movementBounds(area);
    const juce::Point<float> movementHandle {
        movement.getX() + value(7) * movement.getWidth(),
        movement.getBottom() - value(8) * movement.getHeight()
    };
    if (event.position.getDistanceFrom(movementHandle) <= 15.0f
        || movement.contains(event.position))
    {
        dragPrimary = 7;
        dragSecondary = 8;
        beginGestures();
        mouseDrag(event);
        return;
    }
    if (value(5) >= 0.5f)
        return;
    const auto timeline = timelineBounds(area);
    const auto primaryX = timeline.getX()
        + exponential(1.0f, 2000.0f, value(0)) / 2000.0f
            * timeline.getWidth();
    if (event.position.getDistanceFrom(
            { primaryX, timeline.getCentreY() }) > 14.0f)
        return;
    dragPrimary = 0;
    beginGestures();
    mouseDrag(event);
}

void DelayModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (dragPrimary == 7)
    {
        const auto movement = movementBounds(area);
        setValue(7, juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - movement.getX()) / movement.getWidth()));
        setValue(8, juce::jlimit(
            0.0f, 1.0f,
            (movement.getBottom() - event.position.y)
                / movement.getHeight()));
        dragReadout =
            "MOVEMENT  "
            + megadsp::formatControlValue(type, 7, value(7))
            + "  ·  "
            + megadsp::formatControlValue(type, 8, value(8));
        repaint();
        return;
    }
    const auto timeline = timelineBounds(area);
    const auto x = juce::jlimit(
        0.0f, 1.0f,
        (event.position.x - timeline.getX()) / timeline.getWidth());
    const auto milliseconds = juce::jmax(1.0f, x * 2000.0f);
    setValue(0, std::log(milliseconds) / std::log(2000.0f));
    updateDefaultDragReadout();
}

void DelayModuleView::mouseDoubleClick(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto defaults = moduleDefaults(type);
    if (movementBounds(area).contains(event.position))
    {
        setValue(7, defaults[7]);
        setValue(8, defaults[8]);
        repaint();
        return;
    }
    if (value(5) < 0.5f)
    {
        const auto timeline = timelineBounds(area);
        const auto handleX = timeline.getX()
            + exponential(1.0f, 2000.0f, value(0)) / 2000.0f
                  * timeline.getWidth();
        if (event.position.getDistanceFrom(
                { handleX, timeline.getCentreY() }) <= 16.0f)
        {
            setValue(0, defaults[0]);
            repaint();
        }
    }
}

void DelayModuleView::paint(juce::Graphics& graphics, juce::Rectangle<float> area)
{
    constexpr std::array<float, 8> divisions {
        0.125f, 0.25f, 0.375f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f
    };
    const auto divisionIndex = megadsp::discreteIndex(
        value(6), static_cast<int>(divisions.size()));
    const auto syncedMilliseconds =
        60000.0f / static_cast<float>(
            juce::jmax(20.0, processor.getCurrentBpmForUI()))
        * divisions[static_cast<size_t>(divisionIndex)];
    const auto time = value(5) >= 0.5f
                          ? juce::jlimit(0.0f, 1.0f,
                              syncedMilliseconds / 6000.0f)
                          : exponential(1.0f, 2000.0f, value(0)) / 2000.0f;
    const auto feedback = value(1) * 0.95f;
    const bool pingPong = value(4) >= 0.5f;
    const auto movement = movementBounds(area);
    auto timeline = timelineBounds(area);
    graphics.setColour(juce::Colours::white);
    graphics.fillRect(
        timeline.getX(), timeline.getY(), 3.0f, timeline.getHeight());
    float level = 1.0f;
    for (int tap = 1; tap < 12 && level > 0.03f; ++tap)
    {
        const auto x = timeline.getX()
                       + std::fmod(time * static_cast<float>(tap), 1.0f)
                             * timeline.getWidth();
        const auto height = timeline.getHeight() * 0.75f * level;
        graphics.setColour(pingPong && tap % 2 == 0 ? outputColour : accent);
        graphics.fillRoundedRectangle(
            x - 2.0f, timeline.getCentreY() - height * 0.5f,
                                      4.0f, height, 2.0f);
        level *= feedback;
    }
    graphics.setColour(juce::Colours::white);
    graphics.drawText(
        value(5) >= 0.5f
            ? "DIVISION  "
                  + megadsp::formatControlValue(type, 6, value(6))
            : "TIME  " + megadsp::formatControlValue(type, 0, value(0)),
        timeline.toNearestInt(), juce::Justification::topRight);
    if (value(5) < 0.5f)
    {
        const auto primaryX = timeline.getX()
            + exponential(1.0f, 2000.0f, value(0)) / 2000.0f
                  * timeline.getWidth();
        graphics.setColour(juce::Colours::white);
        graphics.fillEllipse(
            primaryX - 5.0f, timeline.getCentreY() - 5.0f,
                            10.0f, 10.0f);
    }

    graphics.setColour(juce::Colour(0xff151b24));
    graphics.fillRoundedRectangle(movement, 5.0f);
    graphics.setColour(accent.withAlpha(0.22f));
    graphics.fillRoundedRectangle(
        movement.withWidth(movement.getWidth() * value(7)), 5.0f);
    const juce::Point<float> movementHandle {
        movement.getX() + value(7) * movement.getWidth(),
        movement.getBottom() - value(8) * movement.getHeight()
    };
    graphics.setColour(outputColour.withAlpha(0.45f));
    graphics.drawLine(
        movement.getX(), movementHandle.y,
        movement.getRight(), movementHandle.y, 1.0f);
    graphics.setColour(juce::Colours::white);
    graphics.fillEllipse(
        juce::Rectangle<float>(10.0f, 10.0f).withCentre(movementHandle));
    graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    graphics.drawText(
        "MOVEMENT  RATE "
            + megadsp::formatControlValue(type, 7, value(7))
            + "  ·  DEPTH "
            + megadsp::formatControlValue(type, 8, value(8)),
        movement.reduced(7.0f, 0.0f).toNearestInt(),
        juce::Justification::centredLeft);
}
} // namespace

std::unique_ptr<ModuleView> createDelayView(EffectGraph& graph)
{
    return std::make_unique<DelayModuleView>(graph);
}
} // namespace megadsp::ui
