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
class ConvolutionReverbModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    void configureAccessibility(juce::Component&) const override;
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
};

void ConvolutionReverbModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Convolution reverb impulse response");
    component.setHelpText(
        "Drop a WAV, AIFF, or FLAC impulse response file here. "
        "Drag Low Cut and High Cut to define the wet passband. The adjacent "
        "Dry and Wet rails remain independent, with a separate Output Trim. "
        "Double-click either passband handle to restore its default.");
}

void ConvolutionReverbModuleView::mouseDown(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto frequencyX = [&area](float frequency)
    {
        return area.getX()
            + std::log(frequency / 20.0f) / std::log(1000.0f)
                  * area.getWidth();
    };
    const auto lowX = frequencyX(
        exponential(20.0f, 1000.0f, value(0)));
    const auto highX = frequencyX(
        exponential(2000.0f, 20000.0f, value(1)));
    const auto lowDistance = std::abs(event.position.x - lowX);
    const auto highDistance = std::abs(event.position.x - highX);
    if (juce::jmin(lowDistance, highDistance) > 15.0f)
        return;
    dragPrimary = lowDistance <= highDistance ? 0 : 1;
    beginGestures();
    mouseDrag(event);
}

void ConvolutionReverbModuleView::mouseDrag(
    const juce::MouseEvent& event)
{
    if (dragPrimary != 0 && dragPrimary != 1)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto proportion = juce::jlimit(
        0.0f, 1.0f,
        (event.position.x - area.getX()) / area.getWidth());
    const auto frequency = 20.0f * std::pow(1000.0f, proportion);
    if (dragPrimary == 0)
        setValue(0, juce::jlimit(
            0.0f, 1.0f,
            std::log(juce::jlimit(20.0f, 1000.0f, frequency) / 20.0f)
                / std::log(50.0f)));
    else
        setValue(1, juce::jlimit(
            0.0f, 1.0f,
            std::log(
                juce::jlimit(2000.0f, 20000.0f, frequency) / 2000.0f)
                / std::log(10.0f)));
    updateDefaultDragReadout();
}

void ConvolutionReverbModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto frequencyX = [&area](float frequency)
    {
        return area.getX()
            + std::log(frequency / 20.0f) / std::log(1000.0f)
                  * area.getWidth();
    };
    const auto lowX = frequencyX(
        exponential(20.0f, 1000.0f, value(0)));
    const auto highX = frequencyX(
        exponential(2000.0f, 20000.0f, value(1)));
    const auto lowDistance = std::abs(event.position.x - lowX);
    const auto highDistance = std::abs(event.position.x - highX);
    if (juce::jmin(lowDistance, highDistance) > 17.0f)
        return;
    const auto control = lowDistance <= highDistance ? 0 : 1;
    setValue(control, moduleDefaults(type)[static_cast<size_t>(control)]);
    repaint();
}

void ConvolutionReverbModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto preview = processor.impulseResponsePreview(slot);
    const auto name = processor.impulseResponseName(slot);
    const auto storedPath = processor.impulseResponsePath(slot);
    const auto lowCut = exponential(20.0f, 1000.0f, value(0));
    const auto highCut = exponential(2000.0f, 20000.0f, value(1));
    const auto frequencyX = [&](float frequency)
    {
        return area.getX()
            + std::log(frequency / 20.0f) / std::log(1000.0f)
                  * area.getWidth();
    };
    const auto lowX = frequencyX(lowCut);
    const auto highX = frequencyX(highCut);
    graphics.setColour(juce::Colour(0x9911161d));
    graphics.fillRect(area.withRight(lowX));
    graphics.fillRect(area.withLeft(highX));
    graphics.setColour(accent.withAlpha(0.65f));
    graphics.drawVerticalLine(
        juce::roundToInt(lowX), area.getY(), area.getBottom());
    graphics.drawVerticalLine(
        juce::roundToInt(highX), area.getY(), area.getBottom());
    auto drawHandle = [&](float x, const juce::String& label,
                          juce::Justification justification)
    {
        juce::Path handle;
        handle.addTriangle(
            x, area.getY() + 3.0f,
            x - 7.0f, area.getY() + 15.0f,
            x + 7.0f, area.getY() + 15.0f);
        graphics.setColour(juce::Colours::white);
        graphics.fillPath(handle);
        auto labelBounds = juce::Rectangle<float>(
            x - 70.0f, area.getY() + 15.0f, 140.0f, 18.0f)
                               .constrainedWithin(area);
        graphics.drawText(
            label, labelBounds.toNearestInt(), justification);
    };
    drawHandle(
        lowX, "LOW CUT  "
                  + megadsp::formatControlValue(type, 0, value(0)),
        juce::Justification::centredLeft);
    drawHandle(
        highX, "HIGH CUT  "
                   + megadsp::formatControlValue(type, 1, value(1)),
        juce::Justification::centredRight);
    graphics.setColour(juce::Colours::white);
    graphics.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    graphics.drawText(
        "DRY " + megadsp::formatControlValue(type, 4, value(4))
            + "   WET "
            + megadsp::formatControlValue(type, 2, value(2))
            + "   OUTPUT TRIM "
            + megadsp::formatControlValue(type, 3, value(3)),
        area.toNearestInt(), juce::Justification::topRight);

    if (name.isEmpty())
    {
        graphics.setColour(juce::Colour(0xffa8b3c0));
        graphics.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        graphics.drawText(
            storedPath.isNotEmpty()
                ? "IR FILE NOT FOUND\n"
                      + juce::File(storedPath).getFileName()
                      + "\nLOAD OR DROP A REPLACEMENT"
                : "LOAD IR... OR DROP WAV / AIFF / FLAC HERE",
            area.toNearestInt(), juce::Justification::centred);
        return;
    }

    juce::Path envelope;
    const auto centreY = area.getCentreY();
    for (size_t index = 0; index < preview.size(); ++index)
    {
        const auto x = area.getX()
            + static_cast<float>(index)
                  / static_cast<float>(preview.size() - 1)
                  * area.getWidth();
        const auto y = centreY
            - preview[index] * area.getHeight() * 0.42f;
        if (index == 0) envelope.startNewSubPath(x, y);
        else envelope.lineTo(x, y);
    }
    for (size_t index = preview.size(); index-- > 0;)
    {
        const auto x = area.getX()
            + static_cast<float>(index)
                  / static_cast<float>(preview.size() - 1)
                  * area.getWidth();
        envelope.lineTo(
            x, centreY + preview[index] * area.getHeight() * 0.42f);
    }
    envelope.closeSubPath();
    graphics.setColour(accent.withAlpha(0.24f));
    graphics.fillPath(envelope);
    graphics.setColour(accent);
    graphics.strokePath(envelope, juce::PathStrokeType(1.5f));
    graphics.setColour(juce::Colours::white);
    graphics.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    graphics.drawText(name, area.toNearestInt(),
                      juce::Justification::centredTop);
    graphics.setFont(juce::FontOptions(12.0f));
    graphics.setColour(juce::Colour(0xffa8b3c0));
    graphics.drawText(
        "WET PASSBAND  "
            + megadsp::formatControlValue(type, 0, value(0))
            + " - "
            + megadsp::formatControlValue(type, 1, value(1)),
        area.toNearestInt(), juce::Justification::centredBottom);
}
} // namespace

std::unique_ptr<ModuleView> createConvolutionReverbView(EffectGraph& graph)
{
    return std::make_unique<ConvolutionReverbModuleView>(graph);
}
} // namespace megadsp::ui
