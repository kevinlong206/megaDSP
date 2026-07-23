#include "../GraphStyle.h"
#include "../ModuleView.h"
#include "../../modules/SpectralDelayCanvas.h"

#include "ModuleViewCreators.h"

#include <array>
#include <cmath>

namespace megadsp::ui
{
namespace
{
class SpectralDelayCanvasModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    bool usesFullPanel() const override { return true; }
    void configureAccessibility(juce::Component& component) const override
    {
        component.setTitle("Spectral Delay Canvas history graph");
        component.setHelpText(
            "The graph shows captured low, mid, and high DSP energy and actual "
            "delay times. Drag the three large graph nodes vertically to set the "
            "Low, Mid, and High Delay scales. Click Sync or Freeze, and click "
            "Division to advance musical timing. Lower tracks edit the remaining "
            "controls. Double-click any editable region to restore its default.");
    }
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    static constexpr std::array<int, 3> delayControls {
        SpectralDelayCanvasModule::lowDelayControl,
        SpectralDelayCanvasModule::midDelayControl,
        SpectralDelayCanvasModule::highDelayControl
    };
    static constexpr std::array<int, 6> lowerControls {
        SpectralDelayCanvasModule::baseTimeControl,
        SpectralDelayCanvasModule::feedbackControl,
        SpectralDelayCanvasModule::diffusionControl,
        SpectralDelayCanvasModule::stereoSpreadControl,
        SpectralDelayCanvasModule::mixControl,
        SpectralDelayCanvasModule::outputControl
    };

    static juce::Rectangle<float> headerBounds(juce::Rectangle<float> area)
    {
        return area.removeFromTop(31.0f);
    }
    static juce::Rectangle<float> graphBounds(juce::Rectangle<float> area)
    {
        area.removeFromTop(33.0f);
        area.removeFromBottom(area.getHeight() * 0.37f);
        return area.reduced(5.0f);
    }
    static juce::Rectangle<float> plotBounds(juce::Rectangle<float> area)
    {
        auto graph = graphBounds(area).reduced(8.0f);
        graph.removeFromTop(25.0f);
        graph.removeFromBottom(18.0f);
        return graph;
    }
    static juce::Rectangle<float> headerCell(
        juce::Rectangle<float> area, int index)
    {
        auto row = headerBounds(area);
        const auto width = row.getWidth() / 3.0f;
        return juce::Rectangle<float>(
            row.getX() + width * static_cast<float>(index), row.getY(),
            width, row.getHeight()).reduced(3.0f, 2.0f);
    }
    static juce::Rectangle<float> controlBounds(
        juce::Rectangle<float> area, int control)
    {
        area.removeFromTop(33.0f);
        auto controls = area.removeFromBottom(area.getHeight() * 0.37f);
        auto position = 0;
        while (position < static_cast<int>(lowerControls.size())
               && lowerControls[static_cast<size_t>(position)] != control)
            ++position;
        if (position == static_cast<int>(lowerControls.size()))
            return {};
        const auto width = controls.getWidth() / 3.0f;
        const auto height = controls.getHeight() * 0.5f;
        return juce::Rectangle<float>(
            controls.getX() + width * static_cast<float>(position % 3),
            controls.getY() + height * static_cast<float>(position / 3),
            width, height).reduced(5.0f, 4.0f);
    }
    static float laneX(int lane, juce::Rectangle<float> plot)
    {
        return plot.getX() + (static_cast<float>(lane) + 0.5f)
            * plot.getWidth() / 3.0f;
    }
    static float delayY(float seconds, juce::Rectangle<float> plot)
    {
        const auto normalized = std::log(
            juce::jlimit(0.01f, 8.0f, seconds) / 0.01f) / std::log(800.0f);
        return plot.getBottom() - normalized * plot.getHeight();
    }
    static juce::String delayText(float seconds)
    {
        return seconds < 1.0f ? juce::String(seconds * 1000.0f, 0) + " ms"
                              : juce::String(seconds, 2) + " s";
    }
    static juce::String controlText(
        ModuleType moduleType, int control, float normalized)
    {
        if (control == SpectralDelayCanvasModule::baseTimeControl)
            return delayText(exponential(0.010f, 4.0f, normalized));
        if (control == SpectralDelayCanvasModule::feedbackControl)
            return juce::String(normalized * 90.0f, 0) + "%";
        if (control == SpectralDelayCanvasModule::diffusionControl)
            return juce::String(normalized * 70.0f, 0) + "%";
        return formatControlValue(moduleType, control, normalized);
    }
};

void SpectralDelayCanvasModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (headerCell(area, 0).contains(event.position))
    {
        toggleParameter(SpectralDelayCanvasModule::syncControl);
        return;
    }
    if (headerCell(area, 1).contains(event.position))
    {
        cycleChoice(SpectralDelayCanvasModule::divisionControl, 7);
        return;
    }
    if (headerCell(area, 2).contains(event.position))
    {
        toggleParameter(SpectralDelayCanvasModule::freezeControl);
        return;
    }
    const auto plot = plotBounds(area);
    if (plot.contains(event.position))
    {
        const auto lane = juce::jlimit(
            0, 2, static_cast<int>(
                (event.position.x - plot.getX()) * 3.0f / plot.getWidth()));
        dragPrimary = delayControls[static_cast<size_t>(lane)];
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

void SpectralDelayCanvasModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto isDelay = dragPrimary >= SpectralDelayCanvasModule::lowDelayControl
        && dragPrimary <= SpectralDelayCanvasModule::highDelayControl;
    const auto bounds = isDelay ? plotBounds(area)
                                : controlBounds(area, dragPrimary);
    const auto normalized = isDelay
        ? (bounds.getBottom() - event.position.y) / bounds.getHeight()
        : (event.position.x - bounds.getX()) / bounds.getWidth();
    setValue(dragPrimary, juce::jlimit(0.0f, 1.0f, normalized));
    dragReadout =
        juce::String(controlMetadata(type, dragPrimary).label).toUpperCase()
        + "  " + controlText(type, dragPrimary, value(dragPrimary));
    repaint();
}

void SpectralDelayCanvasModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int index = 0; index < 3; ++index)
        if (headerCell(area, index).contains(event.position))
        {
            resetToDefault(index == 0 ? SpectralDelayCanvasModule::syncControl
                                      : index == 1
                                          ? SpectralDelayCanvasModule::divisionControl
                                          : SpectralDelayCanvasModule::freezeControl);
            return;
        }
    const auto plot = plotBounds(area);
    if (plot.contains(event.position))
    {
        const auto lane = juce::jlimit(
            0, 2, static_cast<int>(
                (event.position.x - plot.getX()) * 3.0f / plot.getWidth()));
        resetToDefault(delayControls[static_cast<size_t>(lane)]);
        return;
    }
    for (const auto control : lowerControls)
        if (controlBounds(area, control).contains(event.position))
        {
            resetToDefault(control);
            return;
        }
}

void SpectralDelayCanvasModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto sync = value(SpectralDelayCanvasModule::syncControl) >= 0.5f;
    const std::array<juce::String, 3> headerText {
        sync ? "SYNC  MUSICAL" : "SYNC  FREE",
        "DIVISION  " + controlText(
            type, SpectralDelayCanvasModule::divisionControl,
            value(SpectralDelayCanvasModule::divisionControl)),
        value(SpectralDelayCanvasModule::freezeControl) >= 0.5f
            ? "FREEZE  ON" : "FREEZE  OFF"
    };
    for (int index = 0; index < 3; ++index)
    {
        const auto bounds = headerCell(area, index);
        const auto selected = index == 0 ? sync
            : index == 2
                ? value(SpectralDelayCanvasModule::freezeControl) >= 0.5f
                : false;
        graphics.setColour(selected ? outputColour : juce::Colour(0xff27313d));
        graphics.fillRoundedRectangle(bounds, 6.0f);
        graphics.setColour(selected ? juce::Colour(0xff101820)
                                    : juce::Colours::white);
        graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        graphics.drawFittedText(
            headerText[static_cast<size_t>(index)], bounds.toNearestInt(),
            juce::Justification::centred, 1);
    }

    const auto graph = graphBounds(area);
    const auto plot = plotBounds(area);
    graphics.setColour(juce::Colour(0xe6121821));
    graphics.fillRoundedRectangle(graph, 8.0f);
    graphics.setColour(juce::Colours::white.withAlpha(0.75f));
    graphics.setFont(juce::FontOptions(9.2f, juce::Font::bold));
    graphics.drawText("CAPTURED BAND ENERGY / ACTUAL DELAY",
                      graph.reduced(8.0f).toNearestInt(),
                      juce::Justification::topLeft);
    for (const auto seconds : { 0.01f, 0.1f, 1.0f, 8.0f })
    {
        const auto y = delayY(seconds, plot);
        graphics.setColour(juce::Colours::white.withAlpha(0.10f));
        graphics.drawHorizontalLine(
            juce::roundToInt(y), plot.getX(), plot.getRight());
        graphics.setColour(juce::Colours::white.withAlpha(0.45f));
        graphics.drawText(
            delayText(seconds),
            juce::Rectangle<float>(plot.getX() + 3.0f, y - 11.0f, 48.0f, 11.0f),
            juce::Justification::centredLeft);
    }

    ContinuousTelemetrySnapshot telemetry;
    const auto hasTelemetry =
        readContinuousTelemetry(telemetry) && telemetry.sequence != 0
        && telemetry.valueCount >= SpectralDelayCanvasModule::telemetryValueCount;
    const auto hasHistory = hasTelemetry
        && telemetry.historyValueCount
            >= SpectralDelayCanvasModule::telemetryHistoryValueCount
        && telemetry.historyCount > 1;
    static constexpr std::array<const char*, 3> names { "LOW", "MID", "HIGH" };
    const std::array<juce::Colour, 3> colours {
        inputColour, accent, outputColour
    };
    for (int lane = 0; lane < 3; ++lane)
    {
        const auto laneWidth = plot.getWidth() / 3.0f;
        const auto laneArea = juce::Rectangle<float>(
            plot.getX() + laneWidth * static_cast<float>(lane),
            plot.getY(), laneWidth,
            plot.getHeight()).reduced(5.0f, 0.0f);
        if (hasHistory)
        {
            juce::Path energy;
            for (std::uint32_t point = 0; point < telemetry.historyCount; ++point)
            {
                const auto x = laneArea.getX()
                    + static_cast<float>(point)
                        / static_cast<float>(juce::jmax(1u, telemetry.historyCount - 1))
                        * laneArea.getWidth();
                const auto level = juce::jlimit(
                    0.0f, 1.0f,
                    std::sqrt(juce::jmax(
                        0.0f, continuousTelemetryHistoryValue(
                                  telemetry, static_cast<std::uint32_t>(lane),
                                  point))) * 2.0f);
                const auto y = laneArea.getBottom() - level * laneArea.getHeight();
                if (point == 0) energy.startNewSubPath(x, y);
                else energy.lineTo(x, y);
            }
            graphics.setColour(colours[static_cast<size_t>(lane)].withAlpha(0.72f));
            graphics.strokePath(energy, juce::PathStrokeType(1.5f));
        }
        const auto x = laneX(lane, plot);
        const auto targetY = plot.getBottom()
            - value(delayControls[static_cast<size_t>(lane)]) * plot.getHeight();
        graphics.setColour(juce::Colours::white.withAlpha(0.45f));
        graphics.drawEllipse(
            juce::Rectangle<float>(16.0f, 16.0f).withCentre({ x, targetY }), 1.2f);
        if (hasTelemetry)
        {
            const auto valueIndex = static_cast<size_t>(
                SpectralDelayCanvasModule::lowDelaySeconds + lane);
            const auto seconds = telemetry.values[valueIndex];
            const auto point = juce::Point<float>(x, delayY(seconds, plot));
            graphics.setColour(colours[static_cast<size_t>(lane)]);
            graphics.fillEllipse(
                juce::Rectangle<float>(11.0f, 11.0f).withCentre(point));
            graphics.setColour(juce::Colours::white);
            graphics.setFont(juce::FontOptions(8.6f, juce::Font::bold));
            graphics.drawFittedText(
                juce::String(names[static_cast<size_t>(lane)]) + "  "
                    + delayText(seconds),
                juce::Rectangle<float>(laneArea.getX(), plot.getBottom() + 2.0f,
                                       laneArea.getWidth(), 15.0f).toNearestInt(),
                juce::Justification::centred, 1);
        }
    }
    if (!hasTelemetry)
    {
        graphics.setColour(juce::Colours::white.withAlpha(0.5f));
        graphics.drawText("DSP TELEMETRY WAITING", plot.toNearestInt(),
                          juce::Justification::centred);
    }

    for (const auto control : lowerControls)
    {
        const auto bounds = controlBounds(area, control);
        graphics.setColour(juce::Colour(0xff151b24));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour((control >= SpectralDelayCanvasModule::mixControl
                                ? outputColour : accent).withAlpha(0.5f));
        graphics.fillRoundedRectangle(
            bounds.withWidth(bounds.getWidth() * value(control)), 5.0f);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(8.8f));
        graphics.drawFittedText(
            juce::String(controlMetadata(type, control).label).toUpperCase()
                + "  " + controlText(type, control, value(control)),
            bounds.reduced(5.0f, 0.0f).toNearestInt(),
            juce::Justification::centred, 1, 0.68f);
    }
}
} // namespace

std::unique_ptr<ModuleView> createSpectralDelayCanvasView(EffectGraph& graph)
{
    return std::make_unique<SpectralDelayCanvasModuleView>(graph);
}
} // namespace megadsp::ui
