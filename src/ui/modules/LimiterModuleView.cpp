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
class LimiterModuleView final : public ModuleView
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
    static juce::Rectangle<float> limiterPlotArea(
        juce::Rectangle<float>);
    static juce::Rectangle<float> limiterReleaseBounds(
        juce::Rectangle<float>);
    static juce::Rectangle<float> limiterLookaheadBounds(
        juce::Rectangle<float>);
    static juce::Rectangle<float> limiterAutoGainBounds(
        juce::Rectangle<float>);
};

void LimiterModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Visual limiter");
    component.setHelpText(
        "Drag the left Threshold or right Ceiling marker vertically. "
        "Drag the Release and Lookahead tracks horizontally. "
        "Click Auto Gain to match the undriven level.");
}

juce::Rectangle<float> LimiterModuleView::limiterPlotArea(
    juce::Rectangle<float> area)
{
    area.removeFromBottom(66.0f);
    return area;
}

juce::Rectangle<float> LimiterModuleView::limiterReleaseBounds(
    juce::Rectangle<float> area)
{
    auto controls = area.removeFromBottom(54.0f);
    controls.removeFromRight(122.0f);
    return controls.removeFromTop(24.0f).reduced(4.0f, 2.0f);
}

juce::Rectangle<float> LimiterModuleView::limiterLookaheadBounds(
    juce::Rectangle<float> area)
{
    auto controls = area.removeFromBottom(54.0f);
    controls.removeFromRight(122.0f);
    controls.removeFromTop(24.0f);
    return controls.removeFromTop(24.0f).reduced(4.0f, 2.0f);
}

juce::Rectangle<float> LimiterModuleView::limiterAutoGainBounds(
    juce::Rectangle<float> area)
{
    auto controls = area.removeFromBottom(54.0f);
    return controls.removeFromRight(114.0f).reduced(4.0f, 5.0f);
}

void LimiterModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (limiterAutoGainBounds(area).contains(event.position))
    {
        toggleParameter(4);
        return;
    }
    if (limiterReleaseBounds(area).contains(event.position))
        dragPrimary = 2;
    else if (limiterLookaheadBounds(area).contains(event.position))
        dragPrimary = 3;
    else
    {
        const auto plot = limiterPlotArea(area);
        const auto thresholdY =
            dbToY(lerp(-24.0f, 0.0f, value(0)), plot);
        const auto ceilingY =
            dbToY(lerp(-12.0f, 0.0f, value(1)), plot);
        const auto thresholdHandle = juce::Rectangle<float>(
            plot.getX(), thresholdY - 14.0f, 142.0f, 28.0f);
        const auto ceilingHandle = juce::Rectangle<float>(
            plot.getRight() - 142.0f, ceilingY - 14.0f,
            142.0f, 28.0f);
        if (thresholdHandle.contains(event.position))
            dragPrimary = 0;
        else if (ceilingHandle.contains(event.position))
            dragPrimary = 1;
    }
    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void LimiterModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (dragPrimary == 0 || dragPrimary == 1)
    {
        const auto plot = limiterPlotArea(area);
        const auto level = juce::jlimit(
            0.0f, 1.0f,
            (plot.getBottom() - event.position.y) / plot.getHeight());
        const auto db = lerp(-24.0f, 0.0f, level);
        setValue(dragPrimary, dragPrimary == 0 ? level
            : juce::jlimit(0.0f, 1.0f, (db + 12.0f) / 12.0f));
    }
    else
    {
        const auto track = dragPrimary == 2
            ? limiterReleaseBounds(area) : limiterLookaheadBounds(area);
        setValue(dragPrimary, juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - track.getX()) / track.getWidth()));
    }
    updateDefaultDragReadout();
}

void LimiterModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto defaults = moduleDefaults(type);
    if (limiterReleaseBounds(area).contains(event.position))
        setValue(2, defaults[2]);
    else if (limiterLookaheadBounds(area).contains(event.position))
        setValue(3, defaults[3]);
    else
    {
        const auto plot = limiterPlotArea(area);
        const auto thresholdY =
            dbToY(lerp(-24.0f, 0.0f, value(0)), plot);
        const auto ceilingY =
            dbToY(lerp(-12.0f, 0.0f, value(1)), plot);
        if (juce::Rectangle<float>(
                plot.getX(), thresholdY - 14.0f,
                142.0f, 28.0f).contains(event.position))
            setValue(0, defaults[0]);
        else if (juce::Rectangle<float>(
                     plot.getRight() - 142.0f, ceilingY - 14.0f,
                     142.0f, 28.0f).contains(event.position))
            setValue(1, defaults[1]);
    }
    repaint();
}

void LimiterModuleView::paint(juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto plot = limiterPlotArea(area);
    for (int db = -24; db <= 0; db += 6)
    {
        const auto y = dbToY(static_cast<float>(db), plot);
        graphics.setColour(juce::Colour(0xff303a46));
        graphics.drawHorizontalLine(
            juce::roundToInt(y), plot.getX(), plot.getRight());
        graphics.setColour(juce::Colour(0xff8995a4));
        graphics.drawText(
            juce::String(db) + " dB",
            juce::Rectangle<float>(
                plot.getCentreX() - 25.0f, y - 13.0f, 50.0f, 12.0f),
            juce::Justification::centred);
    }

    auto drawLimiterLevel = [&](const auto& levels, juce::Colour colour)
    {
        juce::Path path;
        for (size_t index = 0; index < levels.size(); ++index)
        {
            const auto x = plot.getX()
                + static_cast<float>(index)
                      / static_cast<float>(levels.size() - 1)
                      * plot.getWidth();
            const auto y = dbToY(
                juce::jlimit(-24.0f, 0.0f, levels[index]), plot);
            if (index == 0) path.startNewSubPath(x, y);
            else path.lineTo(x, y);
        }
        graphics.setColour(colour);
        graphics.strokePath(path, juce::PathStrokeType(2.0f));
    };
    drawLimiterLevel(inputLevels, inputColour);
    drawLimiterLevel(outputLevels, outputColour);

    juce::Path reduction;
    reduction.startNewSubPath(plot.getX(), plot.getY());
    for (size_t index = 0; index < gainReductionLevels.size(); ++index)
    {
        const auto x = plot.getX()
            + static_cast<float>(index)
                  / static_cast<float>(gainReductionLevels.size() - 1)
                  * plot.getWidth();
        const auto y = plot.getY()
            + juce::jlimit(0.0f, 1.0f,
                          gainReductionLevels[index] / 24.0f)
                  * plot.getHeight();
        reduction.lineTo(x, y);
    }
    reduction.lineTo(plot.getRight(), plot.getY());
    reduction.closeSubPath();
    graphics.setColour(reductionColour.withAlpha(0.14f));
    graphics.fillPath(reduction);
    graphics.setColour(reductionColour.withAlpha(0.82f));
    graphics.strokePath(reduction, juce::PathStrokeType(1.5f));

    const auto thresholdDb = lerp(-24.0f, 0.0f, value(0));
    const auto ceilingDb = lerp(-12.0f, 0.0f, value(1));
    const auto thresholdY = dbToY(thresholdDb, plot);
    const auto ceilingY = dbToY(ceilingDb, plot);
    graphics.setColour(reductionColour);
    graphics.drawHorizontalLine(
        juce::roundToInt(thresholdY), plot.getX(), plot.getRight());
    auto thresholdHandle = juce::Rectangle<float>(
        plot.getX(), thresholdY - 12.0f, 138.0f, 24.0f);
    graphics.fillRoundedRectangle(thresholdHandle, 5.0f);
    graphics.setColour(juce::Colour(0xff10141a));
    graphics.drawText(
        "THRESHOLD  " + juce::String(thresholdDb, 1) + " dB",
        thresholdHandle.toNearestInt(), juce::Justification::centred);

    graphics.setColour(accent);
    graphics.drawHorizontalLine(
        juce::roundToInt(ceilingY), plot.getX(), plot.getRight());
    auto ceilingHandle = juce::Rectangle<float>(
        plot.getRight() - 138.0f, ceilingY - 12.0f, 138.0f, 24.0f);
    graphics.fillRoundedRectangle(ceilingHandle, 5.0f);
    graphics.setColour(juce::Colour(0xff10141a));
    graphics.drawText(
        "CEILING  " + juce::String(ceilingDb, 1) + " dB",
        ceilingHandle.toNearestInt(), juce::Justification::centred);

    const auto currentReduction = juce::jlimit(
        0.0f, 24.0f, processor.getRack().slotMeter(slot));
    const auto maximumReduction = juce::jlimit(
        0.0f, 24.0f,
        *std::max_element(gainReductionLevels.begin(),
                          gainReductionLevels.end()));
    graphics.setColour(inputColour);
    graphics.drawText("INPUT", plot.toNearestInt(),
                      juce::Justification::topLeft);
    graphics.setColour(outputColour);
    graphics.drawText("OUTPUT", plot.toNearestInt(),
                      juce::Justification::topRight);
    graphics.setColour(juce::Colours::white);
    graphics.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    graphics.drawText(
        "GR " + juce::String(currentReduction, 1) + " dB",
        plot.toNearestInt(), juce::Justification::centredTop);
    graphics.setFont(juce::FontOptions(12.0f));
    graphics.setColour(reductionColour);
    graphics.drawText(
        "10s MAX " + juce::String(maximumReduction, 1) + " dB",
        plot.toNearestInt(), juce::Justification::centredBottom);

    auto drawTrack = [&](juce::Rectangle<float> bounds, int control,
                         const juce::String& name)
    {
        graphics.setColour(juce::Colour(0xff27303b));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        const auto fill = bounds.withWidth(bounds.getWidth() * value(control));
        graphics.setColour(accent.withAlpha(0.35f));
        graphics.fillRoundedRectangle(fill, 5.0f);
        const auto x = bounds.getX() + value(control) * bounds.getWidth();
        graphics.setColour(accent);
        graphics.fillEllipse(x - 5.0f, bounds.getCentreY() - 5.0f,
                             10.0f, 10.0f);
        graphics.setColour(juce::Colours::white);
        graphics.drawText(
            name + "  "
                + megadsp::formatControlValue(type, control, value(control)),
            bounds.toNearestInt(), juce::Justification::centred);
    };
    drawTrack(limiterReleaseBounds(area), 2, "RELEASE");
    drawTrack(limiterLookaheadBounds(area), 3, "LOOKAHEAD");

    const auto autoGainBounds = limiterAutoGainBounds(area);
    const auto autoGain = value(4) >= 0.5f;
    graphics.setColour(autoGain ? outputColour.withAlpha(0.38f)
                                : juce::Colour(0xff27303b));
    graphics.fillRoundedRectangle(autoGainBounds, 6.0f);
    graphics.setColour(autoGain ? juce::Colours::white
                                : juce::Colour(0xffa8b3c0));
    graphics.drawText(
        autoGain ? "AUTO GAIN\nMATCHED" : "AUTO GAIN\nOFF",
        autoGainBounds.toNearestInt(), juce::Justification::centred);
}
} // namespace

std::unique_ptr<ModuleView> createLimiterView(EffectGraph& graph)
{
    return std::make_unique<LimiterModuleView>(graph);
}
} // namespace megadsp::ui
