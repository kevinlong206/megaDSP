#include "../GraphStyle.h"
#include "../ModuleView.h"
#include "../../modules/PhaseCoherence.h"

#include "ModuleViewCreators.h"

#include <array>
#include <cmath>

namespace megadsp::ui
{
namespace
{
class PhaseCoherenceModuleView final : public ModuleView
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
    static juce::Rectangle<float> graphArea(juce::Rectangle<float>);
    static juce::Rectangle<float> correlationArea(juce::Rectangle<float>);
    static juce::Rectangle<float> correctionArea(juce::Rectangle<float>);
    static juce::Rectangle<float> controlArea(
        juce::Rectangle<float>, int control);
    void drawControl(juce::Graphics&, juce::Rectangle<float>, int) const;
};

void PhaseCoherenceModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Phase Coherence repair history");
    component.setHelpText(
        "The large graph compares measured correlation before and after DSP "
        "repair and shows the actual applied delay and rotation below it. "
        "Drag the lower control tracks horizontally; click Range to choose "
        "the analysis band. Histories advance only when DSP telemetry does.");
}

juce::Rectangle<float> PhaseCoherenceModuleView::graphArea(
    juce::Rectangle<float> area)
{
    area.removeFromBottom(86.0f);
    return area.reduced(4.0f, 2.0f);
}

juce::Rectangle<float> PhaseCoherenceModuleView::correlationArea(
    juce::Rectangle<float> area)
{
    auto graph = graphArea(area);
    graph.removeFromTop(24.0f);
    graph.removeFromBottom(graph.getHeight() * 0.30f + 5.0f);
    return graph;
}

juce::Rectangle<float> PhaseCoherenceModuleView::correctionArea(
    juce::Rectangle<float> area)
{
    auto graph = graphArea(area);
    graph.removeFromTop(24.0f);
    return graph.removeFromBottom(graph.getHeight() * 0.30f);
}

juce::Rectangle<float> PhaseCoherenceModuleView::controlArea(
    juce::Rectangle<float> area, int control)
{
    auto controls = area.removeFromBottom(82.0f);
    const auto width = controls.getWidth() / 4.0f;
    const auto height = controls.getHeight() / 2.0f;
    return juce::Rectangle<float>(
               controls.getX() + width * static_cast<float>(control % 4),
               controls.getY() + height * static_cast<float>(control / 4),
               width, height)
        .reduced(4.0f, 4.0f);
}

void PhaseCoherenceModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int control = 0; control < 8; ++control)
    {
        if (!controlArea(area, control).contains(event.position))
            continue;
        if (control == PhaseCoherenceModule::rangeControl)
        {
            cycleChoice(control, 3);
            return;
        }
        dragPrimary = control;
        beginGestures();
        mouseDrag(event);
        return;
    }
}

void PhaseCoherenceModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto bounds = controlArea(
        getLocalBounds().toFloat().reduced(12.0f), dragPrimary);
    setValue(dragPrimary, juce::jlimit(
        0.0f, 1.0f,
        (event.position.x - bounds.getX()) / bounds.getWidth()));
    updateDefaultDragReadout();
}

void PhaseCoherenceModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int control = 0; control < 8; ++control)
        if (controlArea(area, control).contains(event.position))
        {
            resetToDefault(control);
            return;
        }
}

void PhaseCoherenceModuleView::drawControl(
    juce::Graphics& graphics, juce::Rectangle<float> bounds,
    int control) const
{
    graphics.setColour(juce::Colour(0xff151b24));
    graphics.fillRoundedRectangle(bounds, 5.0f);
    graphics.setColour((control == PhaseCoherenceModule::correctionControl
                            ? reductionColour : accent)
                           .withAlpha(0.32f));
    graphics.fillRoundedRectangle(
        bounds.withWidth(bounds.getWidth() * value(control)), 5.0f);
    graphics.setColour(juce::Colours::white);
    graphics.setFont(juce::FontOptions(9.0f));
    graphics.drawFittedText(
        juce::String(controlMetadata(type, control).label).toUpperCase()
            + "  " + formatControlValue(type, control, value(control)),
        bounds.reduced(5.0f, 0.0f).toNearestInt(),
        juce::Justification::centred, 1, 0.72f);
}

void PhaseCoherenceModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto graph = graphArea(area);
    const auto correlation = correlationArea(area);
    const auto correction = correctionArea(area);
    graphics.setColour(juce::Colour(0xe6121821));
    graphics.fillRoundedRectangle(graph, 8.0f);

    ContinuousTelemetrySnapshot telemetry;
    const auto hasTelemetry =
        readContinuousTelemetry(telemetry)
        && telemetry.sequence != 0
        && telemetry.valueCount >= PhaseCoherenceModule::telemetryValueCount;
    const auto hasHistory =
        hasTelemetry
        && telemetry.historyValueCount
               >= PhaseCoherenceModule::telemetryHistoryValueCount;

    graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    graphics.setColour(inputColour);
    graphics.drawText("BEFORE CORRELATION", graph.toNearestInt(),
                      juce::Justification::topLeft);
    graphics.setColour(outputColour);
    graphics.drawText("AFTER CORRELATION", graph.toNearestInt(),
                      juce::Justification::topRight);

    auto correlationY = [correlation](float valueToMap)
    {
        return correlation.getBottom()
               - (juce::jlimit(-1.0f, 1.0f, valueToMap) + 1.0f)
                     * 0.5f * correlation.getHeight();
    };
    for (const auto marker : { -1.0f, 0.0f, 1.0f })
    {
        const auto y = correlationY(marker);
        graphics.setColour(juce::Colours::white.withAlpha(
            marker == 0.0f ? 0.18f : 0.08f));
        graphics.drawHorizontalLine(
            juce::roundToInt(y), correlation.getX(), correlation.getRight());
    }

    juce::Path before;
    juce::Path after;
    juce::Path delay;
    juce::Path rotation;
    const auto historyCount = hasHistory ? telemetry.historyCount : 0u;
    for (std::uint32_t index = 0; index < historyCount; ++index)
    {
        const auto proportion = static_cast<float>(index)
            / static_cast<float>(juce::jmax(1u, historyCount - 1));
        const auto x = correlation.getX() + proportion * correlation.getWidth();
        const auto beforeY = correlationY(
            continuousTelemetryHistoryValue(
                telemetry, PhaseCoherenceModule::beforeCorrelationHistory,
                index));
        const auto afterY = correlationY(
            continuousTelemetryHistoryValue(
                telemetry, PhaseCoherenceModule::afterCorrelationHistory,
                index));
        const auto delayValue = juce::jlimit(
            -1.0f, 1.0f,
            continuousTelemetryHistoryValue(
                telemetry, PhaseCoherenceModule::delayHistory, index)
                / 2.0f);
        const auto rotationValue = juce::jlimit(
            -1.0f, 1.0f,
            continuousTelemetryHistoryValue(
                telemetry, PhaseCoherenceModule::rotationHistory, index)
                / 180.0f);
        const auto delayY =
            correction.getCentreY() - delayValue * correction.getHeight() * 0.45f;
        const auto rotationY = correction.getCentreY()
            - rotationValue * correction.getHeight() * 0.45f;
        if (index == 0)
        {
            before.startNewSubPath(x, beforeY);
            after.startNewSubPath(x, afterY);
            delay.startNewSubPath(x, delayY);
            rotation.startNewSubPath(x, rotationY);
        }
        else
        {
            before.lineTo(x, beforeY);
            after.lineTo(x, afterY);
            delay.lineTo(x, delayY);
            rotation.lineTo(x, rotationY);
        }
    }
    graphics.setColour(inputColour.withAlpha(0.85f));
    graphics.strokePath(before, juce::PathStrokeType(1.5f));
    graphics.setColour(outputColour);
    graphics.strokePath(after, juce::PathStrokeType(2.0f));

    graphics.setColour(juce::Colours::white.withAlpha(0.14f));
    graphics.drawHorizontalLine(
        juce::roundToInt(correction.getCentreY()),
        correction.getX(), correction.getRight());
    graphics.setColour(accent);
    graphics.strokePath(delay, juce::PathStrokeType(1.5f));
    graphics.setColour(reductionColour);
    graphics.strokePath(rotation, juce::PathStrokeType(1.5f));

    graphics.setFont(juce::FontOptions(9.2f));
    const auto correctionLeft =
        correction.withWidth(correction.getWidth() * 0.42f);
    const auto correctionRight =
        correction.withTrimmedLeft(correction.getWidth() * 0.42f);
    graphics.setColour(accent);
    graphics.drawText(
        hasTelemetry
            ? "ACTUAL DELAY  "
                  + juce::String(
                      telemetry.values[
                          PhaseCoherenceModule::appliedDelayMilliseconds],
                      3)
                  + " ms"
            : "DSP TELEMETRY WAITING",
        correctionLeft.toNearestInt(), juce::Justification::topLeft);
    graphics.setColour(reductionColour);
    graphics.drawText(
        hasTelemetry
            ? "ACTUAL ROTATION  "
                  + juce::String(
                      telemetry.values[
                          PhaseCoherenceModule::appliedRotationDegrees],
                      1)
                  + juce::String::charToString(0x00b0)
                  + "   CONFIDENCE "
                  + juce::String(
                      telemetry.values[
                          PhaseCoherenceModule::analysisConfidence] * 100.0f,
                      0)
                  + "%"
            : juce::String(),
        correctionRight.toNearestInt(), juce::Justification::topRight);

    for (int control = 0; control < 8; ++control)
        drawControl(graphics, controlArea(area, control), control);
}
} // namespace

std::unique_ptr<ModuleView> createPhaseCoherenceView(EffectGraph& graph)
{
    return std::make_unique<PhaseCoherenceModuleView>(graph);
}
} // namespace megadsp::ui
