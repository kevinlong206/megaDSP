#include "../GraphStyle.h"
#include "../ModuleView.h"

#include "ModuleViewCreators.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace megadsp::ui
{
namespace
{
constexpr std::array<const char*, 4> patternNames {
    "STEP", "RANDOM", "PING PONG", "LEAP"
};
constexpr std::array<std::array<int, 8>, 4> tileOrders {{
    {{ 0, 3, 6, 1, 7, 4, 2, 5 }},
    {{ 5, 1, 7, 3, 0, 6, 4, 2 }},
    {{ 2, 7, 4, 0, 6, 1, 5, 3 }},
    {{ 6, 2, 0, 5, 3, 7, 1, 4 }}
}};
constexpr std::array<int, 9> trackControls {
    3, 4, 5, 6, 7, 8, 9, 10, 11
};

class IRMosaicModuleView final : public ModuleView
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
    static juce::Rectangle<float> syncBounds(juce::Rectangle<float>);
    static juce::Rectangle<float> rateDialBounds(juce::Rectangle<float>);
    static juce::Rectangle<float> waveformBounds(juce::Rectangle<float>);
    static juce::Rectangle<float> trackBounds(
        juce::Rectangle<float>, int);
};

void IRMosaicModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("IR Mosaic impulse rearranger");
    component.setHelpText(
        "Load one impulse response from the module header. IR Mosaic slices "
        "it into eight loudness-matched chunks and periodically selects a new "
        "shuffled order. Re-shuffle Speed or tempo Division sets how often "
        "this happens, and Crossfade keeps every transition click-free.");
}

juce::Rectangle<float> IRMosaicModuleView::patternBounds(
    juce::Rectangle<float> area, int pattern)
{
    auto row = area.removeFromTop(34.0f);
    const auto width = row.getWidth() * 0.25f;
    return juce::Rectangle<float>(
        row.getX() + width * static_cast<float>(pattern),
        row.getY(), width, row.getHeight()).reduced(4.0f);
}

juce::Rectangle<float> IRMosaicModuleView::syncBounds(
    juce::Rectangle<float> area)
{
    area.removeFromTop(38.0f);
    return area.removeFromTop(24.0f).removeFromRight(82.0f).reduced(3.0f);
}

juce::Rectangle<float> IRMosaicModuleView::rateDialBounds(
    juce::Rectangle<float> area)
{
    area.removeFromTop(40.0f);
    auto body = area;
    body.removeFromBottom(102.0f);
    return body.removeFromLeft(64.0f).withSizeKeepingCentre(54.0f, 54.0f);
}

juce::Rectangle<float> IRMosaicModuleView::waveformBounds(
    juce::Rectangle<float> area)
{
    area.removeFromTop(40.0f);
    area.removeFromBottom(102.0f);
    area.removeFromLeft(66.0f);
    return area.reduced(5.0f, 3.0f);
}

juce::Rectangle<float> IRMosaicModuleView::trackBounds(
    juce::Rectangle<float> area, int control)
{
    auto tracks = area.removeFromBottom(98.0f);
    auto iterator = std::find(
        trackControls.begin(), trackControls.end(), control);
    if (iterator == trackControls.end())
        return {};
    const auto index =
        static_cast<int>(std::distance(trackControls.begin(), iterator));
    const auto row = index / 5;
    const auto column = index % 5;
    const auto width = tracks.getWidth() / 5.0f;
    return juce::Rectangle<float>(
        tracks.getX() + width * static_cast<float>(column),
        tracks.getY() + 49.0f * static_cast<float>(row),
        width, 49.0f).reduced(5.0f, 4.0f);
}

void IRMosaicModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int pattern = 0; pattern < 4; ++pattern)
        if (patternBounds(area, pattern).contains(event.position))
        {
            setValue(0, discreteValue(pattern, 4));
            repaint();
            return;
        }
    if (syncBounds(area).contains(event.position))
    {
        toggleParameter(2);
        repaint();
        return;
    }
    if (value(2) < 0.5f && rateDialBounds(area).contains(event.position))
    {
        dragPrimary = 1;
        beginGestures();
        mouseDrag(event);
        return;
    }
    for (const auto control : trackControls)
    {
        if (control == 4 && value(2) < 0.5f)
            continue;
        if (trackBounds(area, control).contains(event.position))
        {
            dragPrimary = control;
            beginGestures();
            mouseDrag(event);
            return;
        }
    }
}

void IRMosaicModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    if (dragPrimary == 1)
    {
        const auto dial = rateDialBounds(
            getLocalBounds().toFloat().reduced(12.0f));
        setValue(1, juce::jlimit(
            0.0f, 1.0f,
            1.0f - (event.position.y - dial.getY()) / dial.getHeight()));
        updateDefaultDragReadout();
        repaint();
        return;
    }
    const auto track = trackBounds(
        getLocalBounds().toFloat().reduced(12.0f), dragPrimary);
    setValue(dragPrimary, juce::jlimit(
        0.0f, 1.0f,
        (event.position.x - track.getX()) / track.getWidth()));
    updateDefaultDragReadout();
    repaint();
}

void IRMosaicModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (rateDialBounds(area).contains(event.position))
    {
        resetToDefault(1);
        repaint();
        return;
    }
    for (const auto control : trackControls)
        if (trackBounds(area, control).contains(event.position))
        {
            resetToDefault(control);
            repaint();
            return;
        }
}

void IRMosaicModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto activePattern = discreteIndex(value(0), 4);
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

    auto sync = syncBounds(area);
    graphics.setColour(value(2) >= 0.5f
                           ? accent.withAlpha(0.30f)
                           : juce::Colour(0xff151c25));
    graphics.fillRoundedRectangle(sync, 5.0f);
    graphics.setColour(juce::Colours::white.withAlpha(0.78f));
    graphics.drawText(
        value(2) >= 0.5f ? "SYNC" : "FREE",
        sync.toNearestInt(), juce::Justification::centred);

    const auto dial = rateDialBounds(area);
    const auto dialValue = value(1);
    const auto startAngle = juce::MathConstants<float>::pi * 1.25f;
    const auto endAngle = juce::MathConstants<float>::pi * 2.75f;
    juce::Path dialTrack;
    dialTrack.addCentredArc(
        dial.getCentreX(), dial.getCentreY(),
        dial.getWidth() * 0.38f, dial.getHeight() * 0.38f,
        0.0f, startAngle, endAngle, true);
    graphics.setColour(juce::Colours::white.withAlpha(
        value(2) >= 0.5f ? 0.10f : 0.22f));
    graphics.strokePath(dialTrack, juce::PathStrokeType(4.0f));
    juce::Path dialValuePath;
    dialValuePath.addCentredArc(
        dial.getCentreX(), dial.getCentreY(),
        dial.getWidth() * 0.38f, dial.getHeight() * 0.38f,
        0.0f, startAngle,
        startAngle + (endAngle - startAngle) * dialValue, true);
    graphics.setColour(accent.withAlpha(
        value(2) >= 0.5f ? 0.25f : 0.90f));
    graphics.strokePath(dialValuePath, juce::PathStrokeType(4.0f));
    graphics.setColour(juce::Colours::white.withAlpha(0.72f));
    graphics.setFont(juce::FontOptions(8.5f, juce::Font::bold));
    graphics.drawText(
        "RE-SHUFFLE", dial.translated(0.0f, -7.0f).toNearestInt(),
        juce::Justification::centred);
    graphics.setFont(juce::FontOptions(9.0f));
    graphics.drawText(
        formatControlValue(type, 1, dialValue),
        dial.translated(0.0f, 8.0f).toNearestInt(),
        juce::Justification::centred);

    const auto waveform = waveformBounds(area);
    graphics.setColour(juce::Colour(0xff0d131a));
    graphics.fillRoundedRectangle(waveform, 8.0f);
    const auto preview = processor.impulseResponsePreview(slot, 0);
    const auto name = processor.impulseResponseName(slot, 0);
    const auto centre = waveform.getCentreY();
    const auto tileWidth = waveform.getWidth() / 8.0f;
    auto displayedPattern = activePattern;
    ContinuousTelemetrySnapshot telemetry {};
    if (processor.readContinuousTelemetry(slot, telemetry)
        && telemetry.valueCount >= 5)
        displayedPattern = juce::jlimit(
            0, 3, juce::roundToInt(telemetry.values[0]));
    for (int tile = 0; tile < 8; ++tile)
    {
        auto tileArea = juce::Rectangle<float>(
            waveform.getX() + tileWidth * static_cast<float>(tile),
            waveform.getY(), tileWidth, waveform.getHeight()).reduced(1.0f);
        graphics.setColour(
            (tile % 2 == 0 ? accent : accent.brighter(0.45f))
                .withAlpha(0.08f));
        graphics.fillRect(tileArea);
        graphics.setColour(juce::Colours::white.withAlpha(0.32f));
        graphics.setFont(juce::FontOptions(8.0f, juce::Font::bold));
        graphics.drawText(
            juce::String(
                tileOrders[static_cast<size_t>(displayedPattern)]
                          [static_cast<size_t>(tile)] + 1),
            tileArea.removeFromBottom(13.0f).toNearestInt(),
            juce::Justification::centred);
    }
    juce::Path response;
    for (size_t point = 0; point < preview.size(); ++point)
    {
        const auto x = waveform.getX()
            + waveform.getWidth() * static_cast<float>(point)
                / static_cast<float>(preview.size() - 1);
        const auto y = centre - preview[point]
            * waveform.getHeight() * 0.40f;
        if (point == 0)
            response.startNewSubPath(x, centre);
        response.lineTo(x, y);
    }
    graphics.setColour(accent.withAlpha(0.82f));
    graphics.strokePath(response, juce::PathStrokeType(1.5f));
    graphics.setColour(juce::Colours::white.withAlpha(0.64f));
    graphics.setFont(juce::FontOptions(10.0f));
    graphics.drawText(
        name.isEmpty() ? "LOAD AN IR FROM THE MODULE HEADER"
                       : name.toUpperCase(),
        waveform.reduced(8.0f).removeFromTop(18.0f).toNearestInt(),
        juce::Justification::centredLeft);

    if (telemetry.valueCount >= 5)
    {
        const auto blend = juce::jlimit(
            0.0f, 1.0f, telemetry.values[2]);
        graphics.setColour(accent);
        graphics.fillEllipse(
            waveform.getX() + blend * (waveform.getWidth() - 8.0f),
            waveform.getBottom() - 13.0f, 8.0f, 8.0f);
    }

    for (const auto control : trackControls)
    {
        if (control == 4 && value(2) < 0.5f)
            continue;
        const auto track = trackBounds(area, control);
        auto rail = track.reduced(2.0f);
        auto label = rail.removeFromTop(16.0f);
        auto valueArea = rail.removeFromBottom(15.0f);
        auto line = rail.withHeight(4.0f).withCentre(rail.getCentre());
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
    }
}
} // namespace

std::unique_ptr<ModuleView> createIRMosaicView(EffectGraph& graph)
{
    return std::make_unique<IRMosaicModuleView>(graph);
}
} // namespace megadsp::ui
