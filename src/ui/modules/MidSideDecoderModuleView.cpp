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
class MidSideDecoderModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
};

void MidSideDecoderModuleView::paint(juce::Graphics& graphics,
                        juce::Rectangle<float> area)
{
    auto scope = area.withSizeKeepingCentre(
        juce::jmin(area.getWidth(), area.getHeight()) * 0.88f,
        juce::jmin(area.getWidth(), area.getHeight()) * 0.88f);
    const auto centre = scope.getCentre();
    const auto radius = scope.getWidth() * 0.48f;

    graphics.setColour(juce::Colour(0xff303946));
    graphics.drawEllipse(scope, 1.0f);
    graphics.drawLine(centre.x - radius, centre.y,
                      centre.x + radius, centre.y, 1.0f);
    graphics.drawLine(centre.x, centre.y - radius,
                      centre.x, centre.y + radius, 1.0f);
    graphics.setColour(juce::Colour(0xff596675));
    graphics.drawLine(centre.x - radius * 0.71f,
                      centre.y - radius * 0.71f,
                      centre.x + radius * 0.71f,
                      centre.y + radius * 0.71f, 1.0f);
    graphics.drawLine(centre.x + radius * 0.71f,
                      centre.y - radius * 0.71f,
                      centre.x - radius * 0.71f,
                      centre.y + radius * 0.71f, 1.0f);

    graphics.setColour(juce::Colour(0xffa8b3c0));
    graphics.drawText("INPUT 1: MID  |  INPUT 2: SIDE",
                      area.toNearestInt(),
                      juce::Justification::topLeft);
    graphics.drawText("MID", juce::Rectangle<float>(
                          centre.x - 28.0f, scope.getY(),
                          56.0f, 18.0f).toNearestInt(),
                      juce::Justification::centred);
    graphics.drawText("SIDE", juce::Rectangle<float>(
                           scope.getRight() - 44.0f, centre.y - 9.0f,
                           44.0f, 18.0f).toNearestInt(),
                      juce::Justification::centredRight);
    graphics.drawText("R", juce::Rectangle<float>(
                        scope.getX(), scope.getY(), 22.0f, 18.0f).toNearestInt(),
                      juce::Justification::centredLeft);
    graphics.drawText("L", juce::Rectangle<float>(
                        scope.getRight() - 22.0f, scope.getY(),
                        22.0f, 18.0f).toNearestInt(),
                      juce::Justification::centredRight);

    float maximum = 0.0f;
    for (int sample = fftSize / 2; sample < fftSize; ++sample)
    {
        const auto left = stereoLeftSamples[static_cast<size_t>(sample)];
        const auto right = stereoRightSamples[static_cast<size_t>(sample)];
        maximum = juce::jmax(
            maximum,
            juce::jmax(std::abs((left + right) * 0.5f),
                       std::abs((left - right) * 0.5f)));
    }
    const auto scale = radius / juce::jmax(0.1f, maximum * 1.15f);
    juce::Path trace;
    bool started = false;
    for (int sample = fftSize / 2; sample < fftSize; sample += 2)
    {
        const auto left = stereoLeftSamples[static_cast<size_t>(sample)];
        const auto right = stereoRightSamples[static_cast<size_t>(sample)];
        const auto mid = (left + right) * 0.5f;
        const auto side = (left - right) * 0.5f;
        const auto point = juce::Point<float>(
            centre.x + side * scale,
            centre.y - mid * scale);
        if (!started)
        {
            trace.startNewSubPath(point);
            started = true;
        }
        else
            trace.lineTo(point);
    }
    graphics.setColour(outputColour.withAlpha(0.62f));
    graphics.strokePath(trace, juce::PathStrokeType(1.15f));

    const auto widthText = "WIDTH "
        + megadsp::formatControlValue(
            megadsp::ModuleType::midSideDecoder, 0, value(0));
    graphics.setColour(accent);
    graphics.drawText(widthText, area.toNearestInt(),
                      juce::Justification::bottomRight);
    juce::String routing;
    if (value(2) >= 0.5f) routing << "SIDES MUTED  ";
    if (value(1) >= 0.5f) routing << "CHANNELS SWAPPED";
    if (routing.isNotEmpty())
    {
        graphics.setColour(reductionColour);
        graphics.drawText(routing.trimEnd(), area.toNearestInt(),
                          juce::Justification::bottomLeft);
    }
}
} // namespace

std::unique_ptr<ModuleView> createMidSideDecoderView(EffectGraph& graph)
{
    return std::make_unique<MidSideDecoderModuleView>(graph);
}
} // namespace megadsp::ui
