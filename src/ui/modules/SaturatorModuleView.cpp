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
class SaturatorModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
};

void SaturatorModuleView::paint(juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto drive = juce::Decibels::decibelsToGain(lerp(0.0f, 36.0f, value(0)));
    const auto bias = lerp(-0.35f, 0.35f, value(2));
    const auto mode = megadsp::discreteIndex(value(5), 3);
    juce::Path curve;
    for (int pixel = 0; pixel < static_cast<int>(area.getWidth()); ++pixel)
    {
        const auto input = static_cast<float>(pixel) / area.getWidth()
                           * 2.0f - 1.0f;
        const auto driven = input * drive + bias;
        const auto output = mode == 1
                                ? (2.0f / juce::MathConstants<float>::pi)
                                      * std::atan(driven)
                                : mode == 2 ? juce::jlimit(-1.0f, 1.0f, driven)
                                            : std::tanh(driven);
        const auto y = area.getCentreY() - output * area.getHeight() * 0.45f;
        if (pixel == 0) curve.startNewSubPath(area.getX(), y);
        else curve.lineTo(area.getX() + static_cast<float>(pixel), y);
    }
    graphics.setColour(accent);
    graphics.strokePath(curve, juce::PathStrokeType(2.5f));
    drawWaveforms(graphics, area.removeFromBottom(area.getHeight() * 0.28f));
}
} // namespace

std::unique_ptr<ModuleView> createSaturatorView(EffectGraph& graph)
{
    return std::make_unique<SaturatorModuleView>(graph);
}
} // namespace megadsp::ui
