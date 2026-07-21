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
class CompressorModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    void configureAccessibility(juce::Component&) const override;
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
};

void CompressorModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Compressor threshold history");
    component.setHelpText(
        "Drag the Threshold marker over the ten-second input and output "
        "history. Auto Makeup is the primary recovery control; Manual Trim "
        "adds gain without replacing it. Double-click Threshold to reset.");
}

void CompressorModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto plot = getLocalBounds().toFloat().reduced(12.0f);
    const auto thresholdY = juce::jmap(
        lerp(-60.0f, 0.0f, value(0)), -60.0f, 0.0f,
        plot.getBottom(), plot.getY());
    const auto handle = juce::Rectangle<float>(
        plot.getRight() - 109.0f, thresholdY - 14.0f, 109.0f, 28.0f);
    if (!handle.contains(event.position))
        return;
    dragPrimary = 0;
    beginGestures();
    mouseDrag(event);
}

void CompressorModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto plot = getLocalBounds().toFloat().reduced(12.0f);
    const auto plotY = juce::jlimit(
        0.0f, 1.0f,
        (plot.getBottom() - event.position.y) / plot.getHeight());
    setValue(0, plotY);
    updateDefaultDragReadout();
}

void CompressorModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto plot = getLocalBounds().toFloat().reduced(12.0f);
    const auto thresholdY = juce::jmap(
        lerp(-60.0f, 0.0f, value(0)), -60.0f, 0.0f,
        plot.getBottom(), plot.getY());
    if (juce::Rectangle<float>(
            plot.getRight() - 109.0f, thresholdY - 14.0f,
            109.0f, 28.0f).contains(event.position))
    {
        setValue(0, moduleDefaults(type)[0]);
        repaint();
    }
}

void CompressorModuleView::paint(juce::Graphics& graphics, juce::Rectangle<float> area)
{
    for (int db = -60; db <= 0; db += 12)
    {
        const auto y = juce::jmap(static_cast<float>(db),
                                 -60.0f, 0.0f,
                                 area.getBottom(), area.getY());
        graphics.setColour(juce::Colour(0xff303a46));
        graphics.drawHorizontalLine(juce::roundToInt(y),
                                    area.getX(), area.getRight());
        graphics.setColour(juce::Colour(0xff8995a4));
        graphics.drawText(juce::String(db) + " dB",
                          juce::Rectangle<float>(area.getX() + 4.0f,
                                                 y - 13.0f, 52.0f, 12.0f),
                          juce::Justification::centredLeft);
    }

    drawLevelHistory(graphics, area, inputLevels, inputColour);
    drawLevelHistory(graphics, area, outputLevels, outputColour);

    const auto thresholdDb = lerp(-60.0f, 0.0f, value(0));
    const auto thresholdY = juce::jmap(thresholdDb, -60.0f, 0.0f,
                                      area.getBottom(), area.getY());
    graphics.setColour(accent);
    graphics.drawHorizontalLine(juce::roundToInt(thresholdY),
                                area.getX(), area.getRight());
    graphics.fillRoundedRectangle(area.getRight() - 105.0f,
                                  thresholdY - 10.0f, 101.0f, 20.0f, 4.0f);
    graphics.setColour(juce::Colour(0xff10141a));
    graphics.drawText("Threshold " + juce::String(thresholdDb, 1) + " dB",
                      juce::Rectangle<float>(area.getRight() - 103.0f,
                                             thresholdY - 10.0f,
                                             97.0f, 20.0f),
                      juce::Justification::centred);

    graphics.setColour(inputColour);
    graphics.drawText("INPUT", area.toNearestInt(),
                      juce::Justification::topLeft);
    graphics.setColour(outputColour);
    graphics.drawText("OUTPUT", area.toNearestInt(),
                      juce::Justification::topRight);
    const auto manualTrim = value(5);
    const auto recovery = value(8) >= 0.5f
        ? "AUTO MAKEUP"
        : "MANUAL RECOVERY";
    graphics.setColour(value(8) >= 0.5f ? outputColour : reductionColour);
    graphics.drawText(
        recovery
            + (manualTrim > 0.0f
                   ? "  +  TRIM "
                         + megadsp::formatControlValue(type, 5, manualTrim)
                   : ""),
        area.toNearestInt(), juce::Justification::centredTop);
    graphics.setColour(juce::Colour(0xff8995a4));
    graphics.drawText("10 seconds", area.toNearestInt(),
                      juce::Justification::centredBottom);
    drawGainReductionOverlay(graphics, area);
}
} // namespace

std::unique_ptr<ModuleView> createCompressorView(EffectGraph& graph)
{
    return std::make_unique<CompressorModuleView>(graph);
}
} // namespace megadsp::ui
