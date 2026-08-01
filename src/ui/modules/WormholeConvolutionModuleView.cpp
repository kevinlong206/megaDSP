#include "../GraphStyle.h"
#include "../ModuleView.h"

#include "ModuleViewCreators.h"

#include <array>
#include <cmath>

namespace megadsp::ui
{
namespace
{
class WormholeConvolutionModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    bool usesFullPanel() const override { return true; }
    void configureAccessibility(juce::Component&) const override;
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    static juce::Rectangle<float> laneBounds(
        juce::Rectangle<float>, int);
    static juce::Rectangle<float> waveformBounds(
        juce::Rectangle<float>, int);
    static juce::Rectangle<float> reverseBounds(
        juce::Rectangle<float>, int);
    static juce::Rectangle<float> levelBounds(
        juce::Rectangle<float>, int);
    static juce::Rectangle<float> bottomTrack(
        juce::Rectangle<float>, int);
};

void WormholeConvolutionModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Wormhole Convolution dual impulse editor");
    component.setHelpText(
        "Load impulse A and B from the module header. Drag each waveform's "
        "left and right crop handles independently, toggle Reverse for either "
        "lane, and set their parallel levels. Low Cut and High Cut affect only "
        "the combined wet field; Dry remains immediate.");
}

juce::Rectangle<float> WormholeConvolutionModuleView::laneBounds(
    juce::Rectangle<float> area, int lane)
{
    area.removeFromBottom(72.0f);
    const auto height = area.getHeight() * 0.5f;
    return juce::Rectangle<float>(
        area.getX(), area.getY() + height * static_cast<float>(lane),
        area.getWidth(), height).reduced(3.0f);
}

juce::Rectangle<float> WormholeConvolutionModuleView::waveformBounds(
    juce::Rectangle<float> area, int lane)
{
    auto row = laneBounds(area, lane);
    row.removeFromTop(21.0f);
    row.removeFromRight(118.0f);
    return row.reduced(3.0f);
}

juce::Rectangle<float> WormholeConvolutionModuleView::reverseBounds(
    juce::Rectangle<float> area, int lane)
{
    auto row = laneBounds(area, lane);
    return row.removeFromRight(112.0f).removeFromTop(28.0f).reduced(4.0f);
}

juce::Rectangle<float> WormholeConvolutionModuleView::levelBounds(
    juce::Rectangle<float> area, int lane)
{
    auto row = laneBounds(area, lane);
    row.removeFromTop(32.0f);
    return row.removeFromRight(112.0f).reduced(4.0f);
}

juce::Rectangle<float> WormholeConvolutionModuleView::bottomTrack(
    juce::Rectangle<float> area, int control)
{
    auto bottom = area.removeFromBottom(68.0f);
    const auto index = control - 8;
    const auto width = bottom.getWidth() * 0.25f;
    return juce::Rectangle<float>(
        bottom.getX() + width * static_cast<float>(index),
        bottom.getY(), width, bottom.getHeight()).reduced(5.0f);
}

void WormholeConvolutionModuleView::mouseDown(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int lane = 0; lane < 2; ++lane)
    {
        const auto offset = lane == 0 ? 0 : 4;
        if (reverseBounds(area, lane).contains(event.position))
        {
            toggleParameter(offset + 2);
            repaint();
            return;
        }
        if (levelBounds(area, lane).contains(event.position))
        {
            dragPrimary = offset + 3;
            beginGestures();
            mouseDrag(event);
            return;
        }
        const auto waveform = waveformBounds(area, lane);
        if (waveform.expanded(5.0f).contains(event.position))
        {
            const auto startX =
                waveform.getX() + waveform.getWidth() * value(offset);
            const auto endX =
                waveform.getX() + waveform.getWidth() * value(offset + 1);
            dragPrimary = std::abs(event.position.x - startX)
                    <= std::abs(event.position.x - endX)
                ? offset : offset + 1;
            beginGestures();
            mouseDrag(event);
            return;
        }
    }
    for (int control = 8; control < 12; ++control)
        if (bottomTrack(area, control).contains(event.position))
        {
            dragPrimary = control;
            beginGestures();
            mouseDrag(event);
            return;
        }
}

void WormholeConvolutionModuleView::mouseDrag(
    const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (dragPrimary <= 6 && dragPrimary != 3)
    {
        const auto lane = dragPrimary >= 4 ? 1 : 0;
        const auto waveform = waveformBounds(area, lane);
        setValue(dragPrimary, juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - waveform.getX()) / waveform.getWidth()));
    }
    else
    {
        juce::Rectangle<float> track;
        if (dragPrimary == 3 || dragPrimary == 7)
            track = levelBounds(area, dragPrimary == 3 ? 0 : 1);
        else
            track = bottomTrack(area, dragPrimary);
        setValue(dragPrimary, juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - track.getX()) / track.getWidth()));
    }
    updateDefaultDragReadout();
    repaint();
}

void WormholeConvolutionModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int lane = 0; lane < 2; ++lane)
    {
        const auto offset = lane == 0 ? 0 : 4;
        if (waveformBounds(area, lane).contains(event.position))
        {
            resetToDefault(offset);
            resetToDefault(offset + 1);
            repaint();
            return;
        }
        if (levelBounds(area, lane).contains(event.position))
        {
            resetToDefault(offset + 3);
            repaint();
            return;
        }
    }
    for (int control = 8; control < 12; ++control)
        if (bottomTrack(area, control).contains(event.position))
        {
            resetToDefault(control);
            repaint();
            return;
        }
}

void WormholeConvolutionModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    for (int lane = 0; lane < 2; ++lane)
    {
        const auto offset = lane == 0 ? 0 : 4;
        auto row = laneBounds(area, lane);
        const auto waveform = waveformBounds(area, lane);
        const auto preview = processor.impulseResponsePreview(slot, lane);
        auto name = processor.impulseResponseName(slot, lane);
        if (name.isEmpty())
            name = "LOAD IR " + juce::String(lane == 0 ? "A" : "B")
                + " FROM HEADER";
        graphics.setColour(juce::Colour(0xff151c25));
        graphics.fillRoundedRectangle(row, 8.0f);
        graphics.setColour(juce::Colours::white.withAlpha(0.82f));
        graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        graphics.drawText(
            juce::String(lane == 0 ? "A  " : "B  ") + name,
            row.removeFromTop(20.0f).toNearestInt(),
            juce::Justification::centredLeft);
        graphics.setColour(juce::Colour(0xff0d131a));
        graphics.fillRoundedRectangle(waveform, 5.0f);

        juce::Path response;
        const auto centre = waveform.getCentreY();
        for (size_t point = 0; point < preview.size(); ++point)
        {
            const auto x = waveform.getX()
                + waveform.getWidth() * static_cast<float>(point)
                    / static_cast<float>(preview.size() - 1);
            const auto y = centre - preview[point]
                * waveform.getHeight() * 0.42f;
            if (point == 0)
                response.startNewSubPath(x, centre);
            response.lineTo(x, y);
        }
        graphics.setColour(accent.withAlpha(0.78f));
        graphics.strokePath(response, juce::PathStrokeType(1.4f));
        const auto startX =
            waveform.getX() + waveform.getWidth() * value(offset);
        const auto endX =
            waveform.getX() + waveform.getWidth() * value(offset + 1);
        graphics.setColour(accent.withAlpha(0.16f));
        graphics.fillRect(juce::Rectangle<float>(
            juce::jmin(startX, endX), waveform.getY(),
            std::abs(endX - startX), waveform.getHeight()));
        graphics.setColour(accent);
        graphics.drawVerticalLine(
            juce::roundToInt(startX), waveform.getY(), waveform.getBottom());
        graphics.drawVerticalLine(
            juce::roundToInt(endX), waveform.getY(), waveform.getBottom());

        const auto reverse = reverseBounds(area, lane);
        graphics.setColour(
            value(offset + 2) >= 0.5f ? accent
                                      : juce::Colour(0xff293440));
        graphics.fillRoundedRectangle(reverse, 5.0f);
        graphics.setColour(juce::Colours::white);
        graphics.drawText(
            "REVERSE", reverse.toNearestInt(),
            juce::Justification::centred);

        auto level = levelBounds(area, lane);
        graphics.setColour(juce::Colour(0xff202a35));
        graphics.fillRoundedRectangle(level, 5.0f);
        graphics.setColour(accent.withAlpha(0.72f));
        graphics.fillRoundedRectangle(
            level.withWidth(level.getWidth() * value(offset + 3)), 5.0f);
        graphics.setColour(juce::Colours::white);
        graphics.drawText(
            "LEVEL " + formatControlValue(type, offset + 3, value(offset + 3)),
            level.toNearestInt(), juce::Justification::centred);
    }

    for (int control = 8; control < 12; ++control)
    {
        auto track = bottomTrack(area, control);
        graphics.setColour(juce::Colour(0xff202a35));
        graphics.fillRoundedRectangle(track, 5.0f);
        graphics.setColour(accent.withAlpha(0.72f));
        graphics.fillRoundedRectangle(
            track.withWidth(track.getWidth() * value(control)), 5.0f);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        graphics.drawText(
            juce::String(moduleDefinition(type)
                .controls[static_cast<size_t>(control)].name).toUpperCase(),
            track.removeFromTop(track.getHeight() * 0.48f).toNearestInt(),
            juce::Justification::centred);
        graphics.setFont(juce::FontOptions(10.0f));
        graphics.drawText(
            formatControlValue(type, control, value(control)),
            track.toNearestInt(), juce::Justification::centred);
    }
}
} // namespace

std::unique_ptr<ModuleView> createWormholeConvolutionView(EffectGraph& graph)
{
    return std::make_unique<WormholeConvolutionModuleView>(graph);
}
} // namespace megadsp::ui
