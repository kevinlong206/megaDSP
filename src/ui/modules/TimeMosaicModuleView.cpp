#include "../GraphStyle.h"
#include "../ModuleView.h"
#include "../../modules/TimeMosaic.h"

#include "ModuleViewCreators.h"

#include <array>
#include <cmath>

namespace megadsp::ui
{
namespace
{
class TimeMosaicModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    bool usesFullPanel() const override { return true; }
    void configureAccessibility(juce::Component& component) const override
    {
        component.setTitle("Time Mosaic spectral history map");
        component.setHelpText(
            "Captured DSP tile assignments appear by frequency and history age; "
            "brightness follows measured energy and horizontal marks show pitch "
            "drift. Drag the map horizontally for Age and vertically for "
            "Coherence. Click Freeze at the top. Lower tracks edit History, Tile "
            "Width, Tile Time, Motion, Pitch Drift, Stereo Spread, Mix, and "
            "Output. Double-click any editable region to restore its default.");
    }
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    static constexpr std::array<int, 8> lowerControls {
        TimeMosaicModule::historyControl,
        TimeMosaicModule::tileWidthControl,
        TimeMosaicModule::tileTimeControl,
        TimeMosaicModule::motionControl,
        TimeMosaicModule::pitchDriftControl,
        TimeMosaicModule::stereoSpreadControl,
        TimeMosaicModule::mixControl,
        TimeMosaicModule::outputControl
    };
    static juce::Rectangle<float> statusBounds(juce::Rectangle<float> area)
    {
        return area.removeFromTop(31.0f);
    }
    static juce::Rectangle<float> freezeBounds(juce::Rectangle<float> area)
    {
        return statusBounds(area).removeFromRight(100.0f).reduced(3.0f, 2.0f);
    }
    static juce::Rectangle<float> mapBounds(juce::Rectangle<float> area)
    {
        area.removeFromTop(33.0f);
        area.removeFromBottom(area.getHeight() * 0.41f);
        return area.reduced(5.0f);
    }
    static juce::Rectangle<float> plotBounds(juce::Rectangle<float> area)
    {
        auto plot = mapBounds(area).reduced(8.0f);
        plot.removeFromTop(25.0f);
        plot.removeFromBottom(17.0f);
        return plot;
    }
    static juce::Rectangle<float> controlBounds(
        juce::Rectangle<float> area, int control)
    {
        area.removeFromTop(33.0f);
        auto controls = area.removeFromBottom(area.getHeight() * 0.41f);
        auto position = 0;
        while (position < static_cast<int>(lowerControls.size())
               && lowerControls[static_cast<size_t>(position)] != control)
            ++position;
        if (position == static_cast<int>(lowerControls.size()))
            return {};
        const auto width = controls.getWidth() * 0.25f;
        const auto height = controls.getHeight() * 0.5f;
        return juce::Rectangle<float>(
            controls.getX() + width * static_cast<float>(position % 4),
            controls.getY() + height * static_cast<float>(position / 4),
            width, height)
            .reduced(5.0f, 4.0f);
    }
    static juce::String secondsText(float seconds)
    {
        return seconds < 1.0f ? juce::String(seconds * 1000.0f, 0) + " ms"
                              : juce::String(seconds, 2) + " s";
    }
    juce::String controlText(int control) const
    {
        const auto normalized = value(control);
        if (control == TimeMosaicModule::tileWidthControl)
            return juce::String(
                exponential(1.0f / 24.0f, 3.0f, normalized), 2)
                + " oct";
        if (control == TimeMosaicModule::tileTimeControl)
            return secondsText(exponential(0.010f, 0.5f, normalized));
        if (control == TimeMosaicModule::pitchDriftControl)
            return juce::String(normalized * 0.5f, 2) + " st";
        return formatControlValue(type, control, normalized);
    }
};

void TimeMosaicModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (freezeBounds(area).contains(event.position))
    {
        toggleParameter(TimeMosaicModule::freezeControl);
        return;
    }
    if (plotBounds(area).contains(event.position))
    {
        dragPrimary = TimeMosaicModule::ageControl;
        dragSecondary = TimeMosaicModule::coherenceControl;
    }
    for (const auto control : lowerControls)
        if (dragPrimary < 0
            && controlBounds(area, control).contains(event.position))
            dragPrimary = control;
    if (dragPrimary >= 0)
    {
        beginGestures();
        mouseDrag(event);
    }
}

void TimeMosaicModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (dragSecondary == TimeMosaicModule::coherenceControl)
    {
        const auto bounds = plotBounds(area);
        setValue(TimeMosaicModule::ageControl, juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - bounds.getX()) / bounds.getWidth()));
        setValue(TimeMosaicModule::coherenceControl, juce::jlimit(
            0.0f, 1.0f,
            (bounds.getBottom() - event.position.y) / bounds.getHeight()));
        dragReadout =
            "AGE  "
            + controlText(TimeMosaicModule::ageControl)
            + "    COHERENCE  "
            + controlText(TimeMosaicModule::coherenceControl);
        repaint();
        return;
    }
    const auto bounds = controlBounds(area, dragPrimary);
    setValue(dragPrimary, juce::jlimit(
        0.0f, 1.0f,
        (event.position.x - bounds.getX()) / bounds.getWidth()));
    dragReadout =
        juce::String(controlMetadata(type, dragPrimary).label).toUpperCase()
        + "  " + controlText(dragPrimary);
    repaint();
}

void TimeMosaicModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (freezeBounds(area).contains(event.position))
    {
        resetToDefault(TimeMosaicModule::freezeControl);
        return;
    }
    if (plotBounds(area).contains(event.position))
    {
        resetToDefault(TimeMosaicModule::ageControl);
        resetToDefault(TimeMosaicModule::coherenceControl);
        return;
    }
    for (const auto control : lowerControls)
        if (controlBounds(area, control).contains(event.position))
        {
            resetToDefault(control);
            return;
        }
}

void TimeMosaicModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    ContinuousTelemetrySnapshot telemetry;
    EventTelemetrySnapshot events;
    const auto hasTelemetry =
        readContinuousTelemetry(telemetry) && telemetry.sequence != 0
        && telemetry.valueCount >= TimeMosaicModule::telemetryValueCount;
    const auto hasHistory = hasTelemetry
        && telemetry.historyValueCount
            >= TimeMosaicModule::telemetryHistoryValueCount
        && telemetry.historyCount > 1;
    const auto hasEvents =
        readEventTelemetry(events) && events.sequence != 0;

    const auto status = statusBounds(area);
    const auto freeze = freezeBounds(area);
    graphics.setColour(
        value(TimeMosaicModule::freezeControl) >= 0.5f
            ? outputColour : juce::Colour(0xff27313d));
    graphics.fillRoundedRectangle(freeze, 6.0f);
    graphics.setColour(
        value(TimeMosaicModule::freezeControl) >= 0.5f
            ? juce::Colour(0xff101820) : juce::Colours::white);
    graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    graphics.drawText(
        value(TimeMosaicModule::freezeControl) >= 0.5f
            ? "FROZEN" : "CAPTURING",
        freeze.toNearestInt(), juce::Justification::centred);
    auto metrics = status;
    metrics.removeFromRight(104.0f);
    graphics.setColour(juce::Colours::white.withAlpha(0.8f));
    graphics.drawFittedText(
        hasTelemetry
            ? "HISTORY  "
                + secondsText(
                    telemetry.values[TimeMosaicModule::activeHistorySeconds])
                + "  ·  MEAN AGE  "
                + secondsText(
                    telemetry.values[TimeMosaicModule::actualMeanAgeSeconds])
                + "  ·  PITCH  "
                + juce::String(
                    telemetry.values[TimeMosaicModule::actualPitchCents], 1)
                + " cents"
            : "DSP MOSAIC TELEMETRY WAITING",
        metrics.reduced(5.0f, 0.0f).toNearestInt(),
        juce::Justification::centredLeft, 1);

    const auto map = mapBounds(area);
    const auto plot = plotBounds(area);
    graphics.setColour(juce::Colour(0xe6121821));
    graphics.fillRoundedRectangle(map, 8.0f);
    graphics.setColour(juce::Colours::white.withAlpha(0.7f));
    graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    graphics.drawText(
        "FREQUENCY  LOW → HIGH", map.reduced(8.0f).toNearestInt(),
        juce::Justification::topLeft);
    graphics.drawText(
        "HISTORY AGE  NOW → OLDER", map.reduced(8.0f).toNearestInt(),
        juce::Justification::topRight);
    for (int division = 0; division <= 4; ++division)
    {
        const auto proportion = static_cast<float>(division) / 4.0f;
        const auto x = plot.getX() + plot.getWidth() * proportion;
        const auto y = plot.getY() + plot.getHeight() * proportion;
        graphics.setColour(juce::Colours::white.withAlpha(0.08f));
        graphics.drawVerticalLine(
            juce::roundToInt(x), plot.getY(), plot.getBottom());
        graphics.drawHorizontalLine(
            juce::roundToInt(y), plot.getX(), plot.getRight());
    }

    if (hasHistory)
    {
        const std::array<juce::Colour, 3> colours {
            inputColour, accent, outputColour
        };
        for (int band = 0; band < 3; ++band)
        {
            juce::Path path;
            for (std::uint32_t point = 0; point < telemetry.historyCount; ++point)
            {
                const auto x = plot.getX()
                    + static_cast<float>(point)
                        / static_cast<float>(juce::jmax(1u, telemetry.historyCount - 1))
                        * plot.getWidth();
                const auto age = continuousTelemetryHistoryValue(
                    telemetry, static_cast<std::uint32_t>(band), point);
                const auto historySeconds = juce::jmax(
                    0.001f, telemetry.values[
                                TimeMosaicModule::activeHistorySeconds]);
                const auto y = plot.getY()
                    + juce::jlimit(0.0f, 1.0f, age / historySeconds)
                        * plot.getHeight();
                if (point == 0) path.startNewSubPath(x, y);
                else path.lineTo(x, y);
            }
            graphics.setColour(colours[static_cast<size_t>(band)].withAlpha(0.55f));
            graphics.strokePath(path, juce::PathStrokeType(1.25f));
        }
    }
    if (hasEvents)
    {
        for (std::uint32_t index = 0; index < events.eventCount; ++index)
        {
            const auto& event = events.events[index];
            if (event.kind != TimeMosaicModule::tileReassigned)
                continue;
            const auto frequency = juce::jlimit(0.0f, 1.0f, event.position[0]);
            const auto age = juce::jlimit(0.0f, 1.0f, event.position[1]);
            const auto pitch = juce::jlimit(-1.0f, 1.0f, event.position[2]);
            const auto energy = juce::jlimit(
                0.0f, 1.0f,
                std::sqrt(juce::jmax(0.0f, event.values[0])) * 2.5f);
            const auto tileWidth = juce::jlimit(
                4.0f, plot.getWidth() * 0.20f,
                4.0f + event.values[1] * plot.getWidth() / 512.0f);
            const auto point = juce::Point<float>(
                plot.getX() + frequency * plot.getWidth(),
                plot.getY() + age * plot.getHeight());
            auto tile = juce::Rectangle<float>(
                tileWidth, 5.0f + event.values[2] * 22.0f).withCentre(point)
                .constrainedWithin(plot);
            graphics.setColour(
                accent.interpolatedWith(outputColour, (pitch + 1.0f) * 0.5f)
                    .withAlpha(0.18f + energy * 0.72f));
            graphics.fillRoundedRectangle(tile, 2.5f);
            graphics.drawLine(
                point.x, point.y, point.x + pitch * 12.0f, point.y,
                1.0f + energy);
        }
    }
    else
    {
        graphics.setColour(juce::Colours::white.withAlpha(0.5f));
        graphics.drawText("DSP TILE EVENTS WAITING", plot.toNearestInt(),
                          juce::Justification::centred);
    }

    const auto target = juce::Point<float>(
        plot.getX() + value(TimeMosaicModule::ageControl) * plot.getWidth(),
        plot.getBottom()
            - value(TimeMosaicModule::coherenceControl) * plot.getHeight());
    graphics.setColour(juce::Colours::white);
    graphics.drawEllipse(
        juce::Rectangle<float>(15.0f, 15.0f).withCentre(target), 1.5f);
    graphics.setFont(juce::FontOptions(8.4f, juce::Font::bold));
    graphics.drawFittedText(
        "AGE  "
            + controlText(TimeMosaicModule::ageControl)
            + "  ·  COHERENCE  "
            + controlText(TimeMosaicModule::coherenceControl),
        juce::Rectangle<float>(230.0f, 15.0f)
            .withCentre(target.translated(0.0f, -14.0f))
            .constrainedWithin(plot).toNearestInt(),
        juce::Justification::centred, 1);

    for (const auto control : lowerControls)
    {
        const auto bounds = controlBounds(area, control);
        graphics.setColour(juce::Colour(0xff151b24));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour((control >= TimeMosaicModule::mixControl
                                ? outputColour : accent).withAlpha(0.5f));
        graphics.fillRoundedRectangle(
            bounds.withWidth(bounds.getWidth() * value(control)), 5.0f);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(8.4f));
        graphics.drawFittedText(
            juce::String(controlMetadata(type, control).label).toUpperCase()
                + "  " + controlText(control),
            bounds.reduced(4.0f, 0.0f).toNearestInt(),
            juce::Justification::centred, 1, 0.62f);
    }
}
} // namespace

std::unique_ptr<ModuleView> createTimeMosaicView(EffectGraph& graph)
{
    return std::make_unique<TimeMosaicModuleView>(graph);
}
} // namespace megadsp::ui
