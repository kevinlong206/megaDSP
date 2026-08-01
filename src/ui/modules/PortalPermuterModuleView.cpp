#include "../GraphStyle.h"
#include "../ModuleView.h"

#include "ModuleViewCreators.h"

#include <algorithm>
#include <array>

namespace megadsp::ui
{
namespace
{
constexpr std::array<const char*, 4> patternNames {
    "REPEAT", "REVERSE", "ROTATE", "SHUFFLE"
};
constexpr std::array<int, 10> trackControls {
    0, 1, 3, 4, 5, 6, 7, 9, 10, 11
};

class PortalPermuterModuleView final : public ModuleView
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
    static juce::Rectangle<float> patternBounds(
        juce::Rectangle<float>, int);
    static juce::Rectangle<float> waveformBounds(juce::Rectangle<float>);
    static juce::Rectangle<float> reverseBounds(juce::Rectangle<float>);
    static juce::Rectangle<float> trackBounds(
        juce::Rectangle<float>, int);
};

void PortalPermuterModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Portal Permuter convolution slicer");
    component.setHelpText(
        "Load one impulse response from the module header. Portal Permuter "
        "captures recent tempo-locked input slices, rearranges them with the "
        "chosen pattern, and sends those slices into the impulse response.");
}

juce::Rectangle<float> PortalPermuterModuleView::patternBounds(
    juce::Rectangle<float> area, int pattern)
{
    auto row = area.removeFromTop(34.0f);
    const auto width = row.getWidth() * 0.25f;
    return juce::Rectangle<float>(
        row.getX() + width * static_cast<float>(pattern),
        row.getY(), width, row.getHeight()).reduced(4.0f);
}

juce::Rectangle<float> PortalPermuterModuleView::waveformBounds(
    juce::Rectangle<float> area)
{
    area.removeFromTop(38.0f);
    area.removeFromBottom(104.0f);
    return area.reduced(5.0f, 4.0f);
}

juce::Rectangle<float> PortalPermuterModuleView::reverseBounds(
    juce::Rectangle<float> area)
{
    auto waveform = waveformBounds(area);
    return waveform.removeFromTop(24.0f).removeFromRight(92.0f).reduced(3.0f);
}

juce::Rectangle<float> PortalPermuterModuleView::trackBounds(
    juce::Rectangle<float> area, int control)
{
    auto tracks = area.removeFromBottom(100.0f);
    const auto iterator = std::find(
        trackControls.begin(), trackControls.end(), control);
    if (iterator == trackControls.end())
        return {};
    const auto index =
        static_cast<int>(std::distance(trackControls.begin(), iterator));
    const auto row = index / 5;
    const auto column = index % 5;
    constexpr auto columns = 5;
    const auto width = tracks.getWidth() / static_cast<float>(columns);
    return juce::Rectangle<float>(
        tracks.getX() + width * static_cast<float>(column),
        tracks.getY() + 50.0f * static_cast<float>(row),
        width, 50.0f).reduced(5.0f, 4.0f);
}

void PortalPermuterModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int pattern = 0; pattern < 4; ++pattern)
        if (patternBounds(area, pattern).contains(event.position))
        {
            setValue(2, discreteValue(pattern, 4));
            repaint();
            return;
        }
    if (reverseBounds(area).contains(event.position))
    {
        toggleParameter(8);
        repaint();
        return;
    }
    for (const auto control : trackControls)
        if (trackBounds(area, control).contains(event.position))
            dragPrimary = control;
    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void PortalPermuterModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto track = trackBounds(area, dragPrimary);
    setValue(dragPrimary, juce::jlimit(
        0.0f, 1.0f,
        (event.position.x - track.getX()) / track.getWidth()));
    updateDefaultDragReadout();
    repaint();
}

void PortalPermuterModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (const auto control : trackControls)
        if (trackBounds(area, control).contains(event.position))
        {
            resetToDefault(control);
            repaint();
            return;
        }
}

void PortalPermuterModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto activePattern = discreteIndex(value(2), 4);
    for (int pattern = 0; pattern < 4; ++pattern)
    {
        const auto bounds = patternBounds(area, pattern);
        graphics.setColour(
            pattern == activePattern ? accent.withAlpha(0.30f)
                                     : juce::Colour(0xff151c25));
        graphics.fillRoundedRectangle(bounds, 6.0f);
        graphics.setColour(
            pattern == activePattern ? accent
                                     : juce::Colours::white.withAlpha(0.58f));
        graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        graphics.drawText(
            patternNames[static_cast<size_t>(pattern)],
            bounds.toNearestInt(), juce::Justification::centred);
    }

    const auto waveform = waveformBounds(area);
    graphics.setColour(juce::Colour(0xff0d131a));
    graphics.fillRoundedRectangle(waveform, 8.0f);
    const auto preview = processor.impulseResponsePreview(slot, 0);
    const auto name = processor.impulseResponseName(slot, 0);
    juce::Path response;
    for (size_t point = 0; point < preview.size(); ++point)
    {
        const auto x = waveform.getX()
            + waveform.getWidth() * static_cast<float>(point)
                / static_cast<float>(preview.size() - 1);
        const auto y = waveform.getCentreY()
            - preview[point] * waveform.getHeight() * 0.37f;
        if (point == 0)
            response.startNewSubPath(x, waveform.getCentreY());
        response.lineTo(x, y);
    }
    graphics.setColour(accent.withAlpha(0.74f));
    graphics.strokePath(response, juce::PathStrokeType(1.4f));
    const auto regionWidth = waveform.getWidth() * 0.35f;
    const auto regionX = waveform.getX()
        + (waveform.getWidth() - regionWidth) * value(7);
    graphics.setColour(accent.withAlpha(0.12f));
    graphics.fillRect(
        juce::Rectangle<float>(
            regionX, waveform.getY(), regionWidth, waveform.getHeight()));
    graphics.setColour(juce::Colours::white.withAlpha(0.64f));
    graphics.setFont(juce::FontOptions(10.0f));
    graphics.drawText(
        name.isEmpty() ? "LOAD AN IR FROM THE MODULE HEADER"
                       : name.toUpperCase(),
        waveform.reduced(8.0f).removeFromTop(18.0f).toNearestInt(),
        juce::Justification::centredLeft);

    const auto reverse = reverseBounds(area);
    graphics.setColour(value(8) >= 0.5f
                           ? accent.withAlpha(0.30f)
                           : juce::Colour(0xff151c25));
    graphics.fillRoundedRectangle(reverse, 5.0f);
    graphics.setColour(juce::Colours::white.withAlpha(0.78f));
    graphics.drawText(
        value(8) >= 0.5f ? "IR REVERSED" : "IR FORWARD",
        reverse.toNearestInt(), juce::Justification::centred);

    ContinuousTelemetrySnapshot telemetry {};
    if (processor.readContinuousTelemetry(slot, telemetry)
        && telemetry.valueCount >= 5 && telemetry.values[0] > 0.5f)
    {
        const auto slice = juce::jlimit(0.0f, 1.0f, telemetry.values[1]);
        const auto progress = juce::jlimit(0.0f, 1.0f, telemetry.values[3]);
        const auto markerX = waveform.getX()
            + slice * (waveform.getWidth() - 10.0f);
        graphics.setColour(accent.withAlpha(0.82f));
        graphics.fillRoundedRectangle(
            markerX, waveform.getBottom() - 18.0f,
            10.0f, 5.0f + 10.0f * progress, 2.5f);
    }

    const auto drawTrack = [&](int control, juce::Rectangle<float> track)
    {
        auto rail = track.reduced(2.0f);
        const auto label = rail.removeFromTop(16.0f);
        const auto valueArea = rail.removeFromBottom(15.0f);
        const auto line = rail.withHeight(4.0f).withCentre(rail.getCentre());
        graphics.setColour(juce::Colours::white.withAlpha(0.16f));
        graphics.fillRoundedRectangle(line, 2.0f);
        graphics.setColour(accent.withAlpha(0.72f));
        graphics.fillRoundedRectangle(
            line.withWidth(line.getWidth() * value(control)), 2.0f);
        graphics.setColour(accent);
        graphics.fillEllipse(
            line.getX() + line.getWidth() * value(control) - 4.0f,
            line.getCentreY() - 4.0f, 8.0f, 8.0f);
        graphics.setColour(juce::Colours::white.withAlpha(0.68f));
        graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        graphics.drawText(
            controlMetadata(type, control).label, label.toNearestInt(),
            juce::Justification::centred);
        graphics.setColour(juce::Colours::white.withAlpha(0.48f));
        graphics.setFont(juce::FontOptions(9.0f));
        graphics.drawText(
            formatControlValue(type, control, value(control)),
            valueArea.toNearestInt(), juce::Justification::centred);
    };
    for (const auto control : trackControls)
        drawTrack(control, trackBounds(area, control));
}
} // namespace

std::unique_ptr<ModuleView> createPortalPermuterView(EffectGraph& graph)
{
    return std::make_unique<PortalPermuterModuleView>(graph);
}
} // namespace megadsp::ui
