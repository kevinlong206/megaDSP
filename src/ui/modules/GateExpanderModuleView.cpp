#include "../GraphStyle.h"
#include "../ModuleView.h"
#include "../../modules/GateExpander.h"

#include "ModuleViewCreators.h"

#include <array>
#include <cmath>

namespace megadsp::ui
{
namespace
{
class GateExpanderModuleView final : public ModuleView
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
    static juce::Rectangle<float> plotArea(juce::Rectangle<float>);
    static juce::Rectangle<float> pillArea(juce::Rectangle<float>, int);
    static juce::Rectangle<float> timingArea(juce::Rectangle<float>, int);
    static juce::Rectangle<float> detectorArea(juce::Rectangle<float>);
    static juce::Rectangle<float> linkArea(juce::Rectangle<float>);
    float detectorX(juce::Rectangle<float>, int) const;
    static float detectorValueFromX(
        juce::Rectangle<float>, int, float);
};

void GateExpanderModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Visual Gate and Expander");
    component.setHelpText(
        "Drag Threshold and Range in the history. The lower tracks edit "
        "Attack, Hold, Release, Hysteresis, detector Low Cut and High Cut, "
        "and Stereo Link. Live detector level, gain envelope, attenuation, "
        "and open state come directly from the selected DSP slot. Sidechain "
        "and detector Listen are explicit buttons.");
}

juce::Rectangle<float> GateExpanderModuleView::plotArea(
    juce::Rectangle<float> area)
{
    area.removeFromTop(32.0f);
    area.removeFromBottom(70.0f);
    return area;
}

juce::Rectangle<float> GateExpanderModuleView::pillArea(
    juce::Rectangle<float> area, int index)
{
    auto row = area.removeFromTop(28.0f);
    const auto width = row.getWidth() * 0.5f;
    return juce::Rectangle<float>(
               row.getX() + width * static_cast<float>(index),
               row.getY(), width, row.getHeight())
        .reduced(4.0f, 2.0f);
}

juce::Rectangle<float> GateExpanderModuleView::timingArea(
    juce::Rectangle<float> area, int index)
{
    auto controls = area.removeFromBottom(66.0f).removeFromTop(28.0f);
    const auto width = controls.getWidth() / 4.0f;
    return juce::Rectangle<float>(
               controls.getX() + width * static_cast<float>(index),
               controls.getY(), width, controls.getHeight())
    .reduced(4.0f, 2.0f);
}

juce::Rectangle<float> GateExpanderModuleView::detectorArea(
    juce::Rectangle<float> area)
{
    auto controls = area.removeFromBottom(66.0f);
    controls.removeFromTop(32.0f);
    controls.removeFromRight(controls.getWidth() * 0.34f);
    return controls.reduced(4.0f, 3.0f);
}

juce::Rectangle<float> GateExpanderModuleView::linkArea(
    juce::Rectangle<float> area)
{
    auto controls = area.removeFromBottom(66.0f);
    controls.removeFromTop(32.0f);
    return controls.removeFromRight(controls.getWidth() * 0.34f)
        .reduced(4.0f, 3.0f);
}

float GateExpanderModuleView::detectorX(
    juce::Rectangle<float> track, int control) const
{
    const auto frequency = control == 6
        ? exponential(20.0f, 2000.0f, value(6))
        : exponential(1000.0f, 20000.0f, value(7));
    return track.getX()
           + std::log(frequency / 20.0f) / std::log(1000.0f)
                 * track.getWidth();
}

float GateExpanderModuleView::detectorValueFromX(
    juce::Rectangle<float> track, int control, float x)
{
    const auto graphPosition = juce::jlimit(
        0.0f, 1.0f, (x - track.getX()) / track.getWidth());
    const auto frequency = 20.0f * std::pow(1000.0f, graphPosition);
    if (control == 6)
        return juce::jlimit(
            0.0f, 1.0f,
            std::log(frequency / 20.0f) / std::log(100.0f));
    return juce::jlimit(
        0.0f, 1.0f,
        std::log(frequency / 1000.0f) / std::log(20.0f));
}

void GateExpanderModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (pillArea(area, 0).contains(event.position))
    {
        if (processor.hasExternalSidechain() || value(8) >= 0.5f)
            toggleParameter(8);
        return;
    }
    if (pillArea(area, 1).contains(event.position))
    {
        toggleParameter(9);
        return;
    }
    for (int timing = 0; timing < 4; ++timing)
        if (timingArea(area, timing).contains(event.position))
            dragPrimary = timing + 2;
    if (detectorArea(area).contains(event.position))
    {
        const auto track = detectorArea(area);
        const auto lowX = detectorX(track, 6);
        const auto highX = detectorX(track, 7);
        dragPrimary = std::abs(event.position.x - lowX)
                              <= std::abs(event.position.x - highX)
                          ? 6 : 7;
    }
    else if (linkArea(area).contains(event.position))
        dragPrimary = 10;
    else if (plotArea(area).contains(event.position))
    {
        const auto plot = plotArea(area);
        const auto thresholdY = plot.getBottom() - value(0) * plot.getHeight();
        const auto rangeBottom = juce::jlimit(
            plot.getY(), plot.getBottom(),
            thresholdY + value(1) * plot.getHeight());
        dragPrimary = std::abs(event.position.y - thresholdY)
                              <= std::abs(event.position.y - rangeBottom)
                          ? 0 : 1;
    }
    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void GateExpanderModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (dragPrimary == 0)
    {
        const auto plot = plotArea(area);
        setValue(0, juce::jlimit(
            0.0f, 1.0f,
            (plot.getBottom() - event.position.y) / plot.getHeight()));
    }
    else if (dragPrimary == 1)
    {
        const auto plot = plotArea(area);
        const auto thresholdY = plot.getBottom() - value(0) * plot.getHeight();
        setValue(1, juce::jlimit(
            0.0f, 1.0f,
            (event.position.y - thresholdY) / plot.getHeight()));
    }
    else
    {
        juce::Rectangle<float> track;
        if (dragPrimary >= 2 && dragPrimary <= 5)
            track = timingArea(area, dragPrimary - 2);
        else if (dragPrimary == 6 || dragPrimary == 7)
        {
            track = detectorArea(area);
            setValue(
                dragPrimary,
                detectorValueFromX(track, dragPrimary, event.position.x));
            updateDefaultDragReadout();
            return;
        }
        else
            track = linkArea(area);
        setValue(dragPrimary, juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - track.getX()) / track.getWidth()));
    }
    updateDefaultDragReadout();
}

void GateExpanderModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (pillArea(area, 0).contains(event.position))
    {
        resetToDefault(8);
        return;
    }
    if (pillArea(area, 1).contains(event.position))
    {
        resetToDefault(9);
        return;
    }
    if (plotArea(area).contains(event.position))
    {
        const auto plot = plotArea(area);
        const auto thresholdY = plot.getBottom() - value(0) * plot.getHeight();
        const auto rangeBottom = juce::jlimit(
            plot.getY(), plot.getBottom(),
            thresholdY + value(1) * plot.getHeight());
        const auto control =
            std::abs(event.position.y - thresholdY)
                    <= std::abs(event.position.y - rangeBottom)
                ? 0 : 1;
        resetToDefault(control);
    }
    for (int timing = 0; timing < 4; ++timing)
        if (timingArea(area, timing).contains(event.position))
            resetToDefault(timing + 2);
    if (detectorArea(area).contains(event.position))
    {
        const auto track = detectorArea(area);
        const auto control =
            std::abs(event.position.x
                     - detectorX(track, 6))
                    <= std::abs(event.position.x
                                - detectorX(track, 7))
                ? 6 : 7;
        resetToDefault(control);
    }
    if (linkArea(area).contains(event.position))
        resetToDefault(10);
}

void GateExpanderModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto plot = plotArea(area);
    ContinuousTelemetrySnapshot telemetrySnapshot;
    const auto hasTelemetry =
        readContinuousTelemetry(telemetrySnapshot)
        && telemetrySnapshot.sequence != 0
        && telemetrySnapshot.valueCount
               >= GateExpanderModule::telemetryValueCount;
    const auto drawGateLevel = [&](const auto& levels, juce::Colour colour)
    {
        juce::Path path;
        for (size_t index = 0; index < levels.size(); ++index)
        {
            const auto x = plot.getX()
                + static_cast<float>(index)
                      / static_cast<float>(levels.size() - 1)
                      * plot.getWidth();
            const auto y = juce::jmap(
                juce::jlimit(-80.0f, 0.0f, levels[index]),
                -80.0f, 0.0f, plot.getBottom(), plot.getY());
            if (index == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
        graphics.setColour(colour);
        graphics.strokePath(path, juce::PathStrokeType(2.0f));
    };
    drawGateLevel(inputLevels, inputColour);
    drawGateLevel(outputLevels, outputColour);
    juce::Path detectorHistory;
    juce::Path reductionHistory;
    for (std::uint32_t index = 0;
         hasTelemetry && index < telemetrySnapshot.historyCount; ++index)
    {
        const auto x = plot.getX()
            + static_cast<float>(index)
                  / static_cast<float>(
                      juce::jmax(1u, telemetrySnapshot.historyCount - 1))
                  * plot.getWidth();
        const auto detector = continuousTelemetryHistoryValue(
            telemetrySnapshot, GateExpanderModule::detectorHistory, index);
        const auto detectorY = juce::jmap(
            juce::jlimit(-80.0f, 0.0f, detector),
            -80.0f, 0.0f, plot.getBottom(), plot.getY());
        const auto y = plot.getBottom()
            - juce::jlimit(
                  0.0f, 1.0f,
                  continuousTelemetryHistoryValue(
                      telemetrySnapshot,
                      GateExpanderModule::attenuationHistory, index)
                      / 80.0f)
                  * plot.getHeight();
        if (index == 0)
        {
            detectorHistory.startNewSubPath(x, detectorY);
            reductionHistory.startNewSubPath(x, y);
        }
        else
        {
            detectorHistory.lineTo(x, detectorY);
            reductionHistory.lineTo(x, y);
        }
    }
    graphics.setColour(accent.withAlpha(0.88f));
    graphics.strokePath(detectorHistory, juce::PathStrokeType(1.8f));
    graphics.setColour(reductionColour.withAlpha(0.72f));
    graphics.strokePath(reductionHistory, juce::PathStrokeType(1.4f));

    for (int db = -80; db <= 0; db += 20)
    {
        const auto y = juce::jmap(
            static_cast<float>(db), -80.0f, 0.0f,
            plot.getBottom(), plot.getY());
        graphics.setColour(juce::Colour(0xff303a46));
        graphics.drawHorizontalLine(
            juce::roundToInt(y), plot.getX(), plot.getRight());
        graphics.setColour(juce::Colour(0xff8995a4));
        graphics.drawText(
            juce::String(db) + " dB",
            juce::Rectangle<float>(plot.getX() + 4.0f, y - 12.0f,
                                   48.0f, 12.0f),
            juce::Justification::centredLeft);
    }

    const auto thresholdDb = lerp(-80.0f, 0.0f, value(0));
    const auto rangeDb = lerp(0.0f, 80.0f, value(1));
    const auto thresholdY = plot.getBottom() - value(0) * plot.getHeight();
    const auto rangeBottom = juce::jlimit(
        plot.getY(), plot.getBottom(),
        thresholdY + value(1) * plot.getHeight());
    graphics.setColour(accent.withAlpha(0.10f));
    graphics.fillRect(juce::Rectangle<float>(
        plot.getX(), thresholdY, plot.getWidth(),
        rangeBottom - thresholdY));
    graphics.setColour(accent);
    graphics.drawHorizontalLine(
        juce::roundToInt(thresholdY), plot.getX(), plot.getRight());
    graphics.fillRoundedRectangle(
        plot.getRight() - 144.0f, thresholdY - 11.0f,
        140.0f, 22.0f, 4.0f);
    graphics.setColour(juce::Colour(0xff10141a));
    graphics.drawText(
        "THRESHOLD  " + juce::String(thresholdDb, 1) + " dB",
        juce::Rectangle<float>(plot.getRight() - 142.0f,
                               thresholdY - 11.0f, 136.0f, 22.0f),
        juce::Justification::centred);
    graphics.setColour(reductionColour);
    graphics.drawHorizontalLine(
        juce::roundToInt(rangeBottom), plot.getX(), plot.getRight());
    graphics.drawText(
        "RANGE  " + juce::String(rangeDb, 1) + " dB",
        juce::Rectangle<float>(plot.getX() + 5.0f,
                               rangeBottom - 20.0f, 112.0f, 18.0f),
        juce::Justification::centredLeft);
    graphics.setColour(juce::Colour(0xffa8b3c0));
    graphics.drawText(
        hasTelemetry
            ? "DSP DETECTOR / ATTENUATION · "
                  + juce::String(
                      telemetrySnapshot.values[
                          GateExpanderModule::detectorDb],
                      1)
                  + " dB · "
                  + (telemetrySnapshot.values[
                         GateExpanderModule::openFraction] >= 0.999f
                         ? "OPEN"
                     : telemetrySnapshot.values[
                           GateExpanderModule::openFraction] <= 0.001f
                         ? "CLOSED"
                         : "SPLIT")
                  + " · ENV "
                  + juce::String(
                      telemetrySnapshot.values[
                          GateExpanderModule::gainEnvelopeDb],
                      1)
                  + " dB"
            : "DSP TELEMETRY WAITING",
        plot.toNearestInt(), juce::Justification::centredBottom);

    const auto external = value(8) >= 0.5f;
    const auto available = processor.hasExternalSidechain();
    const std::array<juce::String, 2> pills {
        available
            ? external ? "SIDECHAIN  EXTERNAL" : "SIDECHAIN  INTERNAL"
            : external ? "EXTERNAL UNAVAILABLE  ·  INPUT"
                       : "SIDECHAIN  UNAVAILABLE",
        value(9) >= 0.5f ? "DETECTOR LISTEN  ON"
                         : "DETECTOR LISTEN  OFF"
    };
    for (int pill = 0; pill < 2; ++pill)
    {
        const auto bounds = pillArea(area, pill);
        const auto active = pill == 0 ? external : value(9) >= 0.5f;
        graphics.setColour(active ? accent.withAlpha(0.36f)
                                  : juce::Colour(0xff27303b));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour(
            pill == 0 && !available ? reductionColour
                                    : juce::Colours::white);
        graphics.drawText(pills[static_cast<size_t>(pill)],
                          bounds.toNearestInt(),
                          juce::Justification::centred);
    }

    constexpr std::array<int, 4> timingControls { 2, 3, 4, 5 };
    constexpr std::array<const char*, 4> timingNames {
        "ATTACK", "HOLD", "RELEASE", "HYSTERESIS"
    };
    for (int timing = 0; timing < 4; ++timing)
    {
        const auto control =
            timingControls[static_cast<size_t>(timing)];
        const auto bounds = timingArea(area, timing);
        graphics.setColour(juce::Colour(0xff27303b));
        graphics.fillRoundedRectangle(bounds, 4.0f);
        graphics.setColour(accent.withAlpha(0.26f));
        graphics.fillRoundedRectangle(
            bounds.withWidth(bounds.getWidth() * value(control)), 4.0f);
        graphics.setColour(juce::Colours::white);
        graphics.drawText(
            juce::String(timingNames[static_cast<size_t>(timing)])
                + "  " + formatControlValue(type, control, value(control)),
            bounds.toNearestInt(), juce::Justification::centred);
    }

    const auto detector = detectorArea(area);
    graphics.setColour(juce::Colour(0xff27303b));
    graphics.fillRoundedRectangle(detector, 4.0f);
    const auto lowX = detectorX(detector, 6);
    const auto highX = detectorX(detector, 7);
    graphics.setColour(accent.withAlpha(0.30f));
    graphics.fillRect(juce::Rectangle<float>(
        juce::jmin(lowX, highX), detector.getY(),
        std::abs(highX - lowX), detector.getHeight()));
    graphics.setColour(juce::Colours::white);
    graphics.drawText(
        "DETECTOR  " + formatControlValue(type, 6, value(6))
            + " – " + formatControlValue(type, 7, value(7)),
        detector.toNearestInt(), juce::Justification::centred);

    const auto link = linkArea(area);
    graphics.setColour(juce::Colour(0xff27303b));
    graphics.fillRoundedRectangle(link, 4.0f);
    graphics.setColour(accent.withAlpha(0.26f));
    graphics.fillRoundedRectangle(
        link.withWidth(link.getWidth() * value(10)), 4.0f);
    graphics.setColour(juce::Colours::white);
    graphics.drawText(
        "LINK  " + formatControlValue(type, 10, value(10)),
        link.toNearestInt(), juce::Justification::centred);
}
} // namespace

std::unique_ptr<ModuleView> createGateExpanderView(EffectGraph& graph)
{
    return std::make_unique<GateExpanderModuleView>(graph);
}
} // namespace megadsp::ui
