#include "../GraphStyle.h"
#include "../ModuleView.h"
#include "../../modules/LoudnessRider.h"

#include "ModuleViewCreators.h"

#include <array>

namespace megadsp::ui
{
namespace
{
class LoudnessRiderModuleView final : public ModuleView
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
    static constexpr std::array<int, 7> trackControls {
        LoudnessRiderModule::rangeControl,
        LoudnessRiderModule::windowControl,
        LoudnessRiderModule::reactionControl,
        LoudnessRiderModule::lookaheadControl,
        LoudnessRiderModule::transientHoldControl,
        LoudnessRiderModule::crestPreserveControl,
        LoudnessRiderModule::outputControl
    };
    static juce::Rectangle<float> graphArea(juce::Rectangle<float>);
    static juce::Rectangle<float> loudnessArea(juce::Rectangle<float>);
    static juce::Rectangle<float> rideArea(juce::Rectangle<float>);
    static juce::Rectangle<float> trackArea(
        juce::Rectangle<float>, int position);
    static float loudnessY(juce::Rectangle<float>, float);
    int trackAt(juce::Point<float>, juce::Rectangle<float>) const;
    void drawTrack(juce::Graphics&, juce::Rectangle<float>, int) const;
};

void LoudnessRiderModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Loudness Rider history");
    component.setHelpText(
        "The graph shows measured momentary loudness, target, gate, and actual "
        "ride gain histories. Drag the Target or Gate line vertically. Drag "
        "lower tracks horizontally and click Window to choose its duration. "
        "Histories advance only when DSP telemetry does.");
}

juce::Rectangle<float> LoudnessRiderModuleView::graphArea(
    juce::Rectangle<float> area)
{
    area.removeFromBottom(82.0f);
    return area.reduced(4.0f, 2.0f);
}

juce::Rectangle<float> LoudnessRiderModuleView::loudnessArea(
    juce::Rectangle<float> area)
{
    auto graph = graphArea(area);
    graph.removeFromTop(24.0f);
    graph.removeFromBottom(graph.getHeight() * 0.28f + 5.0f);
    return graph;
}

juce::Rectangle<float> LoudnessRiderModuleView::rideArea(
    juce::Rectangle<float> area)
{
    auto graph = graphArea(area);
    graph.removeFromTop(24.0f);
    return graph.removeFromBottom(graph.getHeight() * 0.28f);
}

juce::Rectangle<float> LoudnessRiderModuleView::trackArea(
    juce::Rectangle<float> area, int position)
{
    auto controls = area.removeFromBottom(78.0f);
    const auto width = controls.getWidth() / 4.0f;
    const auto height = controls.getHeight() / 2.0f;
    return juce::Rectangle<float>(
               controls.getX() + width * static_cast<float>(position % 4),
               controls.getY() + height * static_cast<float>(position / 4),
               width, height)
        .reduced(4.0f, 3.0f);
}

float LoudnessRiderModuleView::loudnessY(
    juce::Rectangle<float> bounds, float lufs)
{
    return bounds.getBottom()
           - juce::jlimit(0.0f, 1.0f, (lufs + 70.0f) / 62.0f)
                 * bounds.getHeight();
}

int LoudnessRiderModuleView::trackAt(
    juce::Point<float> point, juce::Rectangle<float> area) const
{
    for (int position = 0;
         position < static_cast<int>(trackControls.size()); ++position)
        if (trackArea(area, position).contains(point))
            return position;
    return -1;
}

void LoudnessRiderModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto position = trackAt(event.position, area);
    if (position >= 0)
    {
        const auto control = trackControls[static_cast<size_t>(position)];
        if (control == LoudnessRiderModule::windowControl)
        {
            cycleChoice(control, 3);
            return;
        }
        dragPrimary = control;
    }
    else if (loudnessArea(area).contains(event.position))
    {
        const auto plot = loudnessArea(area);
        const auto target = loudnessY(
            plot, lerp(-36.0f, -8.0f,
                       value(LoudnessRiderModule::targetControl)));
        const auto gate = loudnessY(
            plot, lerp(-70.0f, -30.0f,
                       value(LoudnessRiderModule::gateControl)));
        dragPrimary = std::abs(event.position.y - target)
                              <= std::abs(event.position.y - gate)
            ? LoudnessRiderModule::targetControl
            : LoudnessRiderModule::gateControl;
    }
    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void LoudnessRiderModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (dragPrimary == LoudnessRiderModule::targetControl
        || dragPrimary == LoudnessRiderModule::gateControl)
    {
        const auto plot = loudnessArea(area);
        const auto graphPosition = juce::jlimit(
            0.0f, 1.0f,
            (plot.getBottom() - event.position.y) / plot.getHeight());
        const auto lufs = -70.0f + graphPosition * 62.0f;
        setValue(dragPrimary,
                 dragPrimary == LoudnessRiderModule::targetControl
                     ? juce::jlimit(0.0f, 1.0f, (lufs + 36.0f) / 28.0f)
                     : juce::jlimit(0.0f, 1.0f, (lufs + 70.0f) / 40.0f));
    }
    else
    {
        int position = 0;
        while (position < static_cast<int>(trackControls.size())
               && trackControls[static_cast<size_t>(position)] != dragPrimary)
            ++position;
        if (position >= static_cast<int>(trackControls.size()))
            return;
        const auto bounds = trackArea(area, position);
        setValue(dragPrimary, juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - bounds.getX()) / bounds.getWidth()));
    }
    updateDefaultDragReadout();
}

void LoudnessRiderModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto position = trackAt(event.position, area);
    if (position >= 0)
    {
        resetToDefault(trackControls[static_cast<size_t>(position)]);
        return;
    }
    if (!loudnessArea(area).contains(event.position))
        return;
    const auto plot = loudnessArea(area);
    const auto target = loudnessY(
        plot, lerp(-36.0f, -8.0f,
                   value(LoudnessRiderModule::targetControl)));
    const auto gate = loudnessY(
        plot, lerp(-70.0f, -30.0f,
                   value(LoudnessRiderModule::gateControl)));
    resetToDefault(std::abs(event.position.y - target)
                           <= std::abs(event.position.y - gate)
        ? LoudnessRiderModule::targetControl
        : LoudnessRiderModule::gateControl);
}

void LoudnessRiderModuleView::drawTrack(
    juce::Graphics& graphics, juce::Rectangle<float> bounds,
    int control) const
{
    graphics.setColour(juce::Colour(0xff151b24));
    graphics.fillRoundedRectangle(bounds, 5.0f);
    graphics.setColour(accent.withAlpha(0.32f));
    graphics.fillRoundedRectangle(
        bounds.withWidth(bounds.getWidth() * value(control)), 5.0f);
    graphics.setColour(juce::Colours::white);
    graphics.setFont(juce::FontOptions(8.8f));
    graphics.drawFittedText(
        juce::String(controlMetadata(type, control).label).toUpperCase()
            + "  " + formatControlValue(type, control, value(control)),
        bounds.reduced(5.0f, 0.0f).toNearestInt(),
        juce::Justification::centred, 1, 0.70f);
}

void LoudnessRiderModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto graph = graphArea(area);
    const auto loudness = loudnessArea(area);
    const auto ride = rideArea(area);
    graphics.setColour(juce::Colour(0xe6121821));
    graphics.fillRoundedRectangle(graph, 8.0f);

    ContinuousTelemetrySnapshot telemetry;
    const auto hasTelemetry =
        readContinuousTelemetry(telemetry)
        && telemetry.sequence != 0
        && telemetry.valueCount >= LoudnessRiderModule::telemetryValueCount;
    const auto hasHistory =
        hasTelemetry
        && telemetry.historyValueCount
               >= LoudnessRiderModule::telemetryHistoryValueCount;

    graphics.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    graphics.setColour(inputColour);
    graphics.drawText("MOMENTARY LOUDNESS", graph.toNearestInt(),
                      juce::Justification::topLeft);
    graphics.setColour(hasTelemetry
                           && telemetry.values[LoudnessRiderModule::gatedState]
                                  >= 0.5f
                       ? reductionColour : outputColour);
    graphics.drawText(
        hasTelemetry
            ? "ACTUAL RIDE  "
                  + juce::String(
                      telemetry.values[LoudnessRiderModule::rideGainDecibels],
                      1)
                  + " dB"
                  + (telemetry.values[LoudnessRiderModule::gatedState] >= 0.5f
                         ? "  ·  GATED" : "")
            : "DSP TELEMETRY WAITING",
        graph.toNearestInt(), juce::Justification::topRight);

    for (const auto lufs : { -60.0f, -40.0f, -20.0f })
    {
        const auto y = loudnessY(loudness, lufs);
        graphics.setColour(juce::Colours::white.withAlpha(0.10f));
        graphics.drawHorizontalLine(
            juce::roundToInt(y), loudness.getX(), loudness.getRight());
        graphics.setColour(juce::Colours::white.withAlpha(0.45f));
        graphics.drawText(
            juce::String(juce::roundToInt(lufs)),
            juce::Rectangle<float>(
                loudness.getX(), y - 13.0f, 28.0f, 12.0f),
            juce::Justification::centredLeft);
    }

    auto rideY = [ride](float gain)
    {
        return ride.getCentreY()
               - juce::jlimit(-18.0f, 18.0f, gain)
                     / 18.0f * ride.getHeight() * 0.45f;
    };
    graphics.setColour(juce::Colours::white.withAlpha(0.14f));
    graphics.drawHorizontalLine(
        juce::roundToInt(ride.getCentreY()), ride.getX(), ride.getRight());

    juce::Path measured;
    juce::Path target;
    juce::Path gate;
    juce::Path actualRide;
    const auto historyCount = hasHistory ? telemetry.historyCount : 0u;
    for (std::uint32_t index = 0; index < historyCount; ++index)
    {
        const auto proportion = static_cast<float>(index)
            / static_cast<float>(juce::jmax(1u, historyCount - 1));
        const auto x = loudness.getX() + proportion * loudness.getWidth();
        const auto measuredY = loudnessY(
            loudness, continuousTelemetryHistoryValue(
                telemetry, LoudnessRiderModule::loudnessHistory, index));
        const auto targetY = loudnessY(
            loudness, continuousTelemetryHistoryValue(
                telemetry, LoudnessRiderModule::targetHistory, index));
        const auto gateY = loudnessY(
            loudness, continuousTelemetryHistoryValue(
                telemetry, LoudnessRiderModule::gateHistory, index));
        const auto actualY = rideY(continuousTelemetryHistoryValue(
            telemetry, LoudnessRiderModule::rideHistory, index));
        if (index == 0)
        {
            measured.startNewSubPath(x, measuredY);
            target.startNewSubPath(x, targetY);
            gate.startNewSubPath(x, gateY);
            actualRide.startNewSubPath(x, actualY);
        }
        else
        {
            measured.lineTo(x, measuredY);
            target.lineTo(x, targetY);
            gate.lineTo(x, gateY);
            actualRide.lineTo(x, actualY);
        }
    }
    graphics.setColour(inputColour);
    graphics.strokePath(measured, juce::PathStrokeType(2.0f));
    graphics.setColour(outputColour.withAlpha(0.85f));
    graphics.strokePath(target, juce::PathStrokeType(1.5f));
    graphics.setColour(reductionColour.withAlpha(0.8f));
    graphics.strokePath(gate, juce::PathStrokeType(1.25f));
    graphics.setColour(accent);
    graphics.strokePath(actualRide, juce::PathStrokeType(2.0f));

    const auto targetControlY = loudnessY(
        loudness, lerp(-36.0f, -8.0f,
                       value(LoudnessRiderModule::targetControl)));
    const auto gateControlY = loudnessY(
        loudness, lerp(-70.0f, -30.0f,
                       value(LoudnessRiderModule::gateControl)));
    const auto targetLabelY = juce::jlimit(
        loudness.getY() + 10.0f, loudness.getBottom() - 10.0f,
        targetControlY);
    const auto gateLabelY = juce::jlimit(
        loudness.getY() + 10.0f, loudness.getBottom() - 10.0f,
        gateControlY);
    graphics.setColour(outputColour);
    graphics.drawHorizontalLine(
        juce::roundToInt(targetControlY),
        loudness.getX(), loudness.getRight());
    graphics.fillRoundedRectangle(
        juce::Rectangle<float>(
            loudness.getX(), targetLabelY - 10.0f, 116.0f, 20.0f),
        4.0f);
    graphics.setColour(juce::Colour(0xff10141a));
    graphics.drawText(
        "TARGET  " + juce::String(
            lerp(-36.0f, -8.0f,
                 value(LoudnessRiderModule::targetControl)), 1),
        juce::Rectangle<float>(
            loudness.getX(), targetLabelY - 10.0f, 116.0f, 20.0f)
            .toNearestInt(),
        juce::Justification::centred);
    graphics.setColour(reductionColour);
    graphics.drawHorizontalLine(
        juce::roundToInt(gateControlY),
        loudness.getX(), loudness.getRight());
    graphics.fillRoundedRectangle(
        juce::Rectangle<float>(
            loudness.getRight() - 106.0f, gateLabelY - 10.0f,
            106.0f, 20.0f),
        4.0f);
    graphics.setColour(juce::Colour(0xff10141a));
    graphics.drawText(
        "GATE  " + juce::String(
            lerp(-70.0f, -30.0f,
                 value(LoudnessRiderModule::gateControl)), 1),
        juce::Rectangle<float>(
            loudness.getRight() - 106.0f, gateLabelY - 10.0f,
            106.0f, 20.0f).toNearestInt(),
        juce::Justification::centred);

    graphics.setColour(accent);
    graphics.drawText("ACTUAL RIDE HISTORY", ride.toNearestInt(),
                      juce::Justification::topLeft);
    for (int position = 0;
         position < static_cast<int>(trackControls.size()); ++position)
        drawTrack(
            graphics, trackArea(area, position),
            trackControls[static_cast<size_t>(position)]);
}
} // namespace

std::unique_ptr<ModuleView> createLoudnessRiderView(EffectGraph& graph)
{
    return std::make_unique<LoudnessRiderModuleView>(graph);
}
} // namespace megadsp::ui
