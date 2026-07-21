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
class StereoWidthModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    void configureAccessibility(juce::Component&) const override;
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    static juce::Rectangle<float> fieldBounds(juce::Rectangle<float>);
    static juce::Rectangle<float> foundationBounds(juce::Rectangle<float>);
};

void StereoWidthModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Stereo Width image editor");
    component.setHelpText(
        "Drag the Width handle across the Dimension field. Drag the Mono "
        "Below handle along the low-frequency Foundation rail. Balance uses "
        "L, C, and R pan language; Mono Safe protects correlation.");
}

juce::Rectangle<float> StereoWidthModuleView::fieldBounds(
    juce::Rectangle<float> area)
{
    area.removeFromBottom(38.0f);
    return area;
}

juce::Rectangle<float> StereoWidthModuleView::foundationBounds(
    juce::Rectangle<float> area)
{
    return area.removeFromBottom(32.0f);
}

void StereoWidthModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto field = fieldBounds(area);
    const auto foundation = foundationBounds(area);
    const juce::Point<float> widthHandle {
        field.getX() + value(0) * field.getWidth(),
        field.getCentreY()
    };
    const juce::Point<float> cutoffHandle {
        foundation.getX() + value(2) * foundation.getWidth(),
        foundation.getCentreY()
    };
    if (event.position.getDistanceFrom(cutoffHandle) <= 16.0f)
        dragPrimary = 2;
    else if (event.position.getDistanceFrom(widthHandle) <= 16.0f)
        dragPrimary = 0;
    else
        return;
    beginGestures();
    mouseDrag(event);
}

void StereoWidthModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto target = dragPrimary == 2
        ? foundationBounds(area) : fieldBounds(area);
    setValue(dragPrimary, juce::jlimit(
        0.0f, 1.0f,
        (event.position.x - target.getX()) / target.getWidth()));
    updateDefaultDragReadout();
}

void StereoWidthModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto field = fieldBounds(area);
    const auto foundation = foundationBounds(area);
    const auto defaults = moduleDefaults(type);
    const juce::Point<float> widthHandle {
        field.getX() + value(0) * field.getWidth(), field.getCentreY()
    };
    const juce::Point<float> cutoffHandle {
        foundation.getX() + value(2) * foundation.getWidth(),
        foundation.getCentreY()
    };
    if (event.position.getDistanceFrom(widthHandle) <= 18.0f)
        setValue(0, defaults[0]);
    else if (event.position.getDistanceFrom(cutoffHandle) <= 18.0f)
        setValue(2, defaults[2]);
    else
        return;
    repaint();
}

void StereoWidthModuleView::paint(juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto field = fieldBounds(area);
    const auto foundation = foundationBounds(area);
    const auto width = value(0) * 2.0f;
    const auto dimension = value(1);
    const auto centre = field.getCentre();
    const auto radiusX =
        field.getWidth() * 0.42f * juce::jmin(1.0f, width);
    const auto fieldHeight =
        field.getHeight() * (0.18f + dimension * 0.27f);

    graphics.setColour(accent.withAlpha(0.045f + dimension * 0.07f));
    graphics.fillRoundedRectangle(field, 8.0f);

    for (int ring = 4; ring >= 1; --ring)
    {
        const auto scale = static_cast<float>(ring) / 4.0f;
        graphics.setColour(
            (ring % 2 == 0 ? accent : outputColour)
                .withAlpha(0.10f + dimension * 0.08f));
        graphics.drawEllipse(centre.x - radiusX * scale,
                             centre.y - fieldHeight * scale,
                             radiusX * 2.0f * scale,
                             fieldHeight * 2.0f * scale,
                             1.5f);
    }

    graphics.setColour(juce::Colour(0xff8995a4));
    graphics.drawVerticalLine(juce::roundToInt(centre.x),
                              field.getY(), field.getBottom());
    graphics.drawText("MONO", field.withWidth(field.getWidth() * 0.5f).toNearestInt(),
                      juce::Justification::centredTop);
    graphics.drawText("WIDE", field.withTrimmedLeft(field.getWidth() * 0.5f).toNearestInt(),
                      juce::Justification::centredTop);
    graphics.setColour(accent);
    graphics.drawText(
        "DIMENSION FIELD  "
            + megadsp::formatControlValue(type, 1, value(1)),
        field.toNearestInt(), juce::Justification::centredBottom);
    graphics.setColour(juce::Colours::white);
    graphics.drawText(
        "BALANCE  "
            + megadsp::formatControlValue(type, 4, value(4)),
        field.toNearestInt(), juce::Justification::topRight);

    const auto handleX = field.getX() + value(0) * field.getWidth();
    graphics.setColour(juce::Colours::white);
    graphics.fillEllipse(handleX - 6.0f, centre.y - 6.0f, 12.0f, 12.0f);
    graphics.setColour(accent);
    graphics.drawLine(centre.x, centre.y, handleX, centre.y, 2.5f);

    const auto cutoffX =
        foundation.getX() + value(2) * foundation.getWidth();
    graphics.setColour(juce::Colour(0xff111820));
    graphics.fillRoundedRectangle(foundation, 6.0f);
    graphics.setColour(juce::Colour(0xff6f8cff).withAlpha(0.35f));
    graphics.fillRoundedRectangle(
        foundation.withRight(cutoffX).reduced(0.0f, 8.0f), 4.0f);
    graphics.setColour(juce::Colours::white);
    graphics.fillEllipse(
        cutoffX - 6.0f, foundation.getCentreY() - 6.0f, 12.0f, 12.0f);
    graphics.setColour(juce::Colour(0xffa8b3c0));
    graphics.drawText(
        "FOUNDATION  MONO BELOW  "
            + megadsp::formatControlValue(type, 2, value(2)),
        foundation.reduced(7.0f, 0.0f).toNearestInt(),
        juce::Justification::centredLeft);
    graphics.setColour(
        value(7) >= 0.5f ? outputColour : juce::Colour(0xff758194));
    graphics.drawText(
        value(7) >= 0.5f ? "MONO SAFE  ON" : "MONO SAFE  OFF",
        foundation.reduced(7.0f, 0.0f).toNearestInt(),
        juce::Justification::centredRight);
}
} // namespace

std::unique_ptr<ModuleView> createStereoWidthView(EffectGraph& graph)
{
    return std::make_unique<StereoWidthModuleView>(graph);
}
} // namespace megadsp::ui
