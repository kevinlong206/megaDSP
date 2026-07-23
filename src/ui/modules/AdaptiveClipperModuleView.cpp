#include "../GraphStyle.h"
#include "../ModuleView.h"
#include "../../modules/AdaptiveClipper.h"

#include "ModuleViewCreators.h"

#include <array>
#include <cmath>

namespace megadsp::ui
{
namespace
{
class AdaptiveClipperModuleView final : public ModuleView
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
    static juce::Rectangle<float> waveformArea(juce::Rectangle<float>);
    static juce::Rectangle<float> classificationArea(
        juce::Rectangle<float>);
    static juce::Rectangle<float> controlArea(
        juce::Rectangle<float>, int control);
    void drawControl(juce::Graphics&, juce::Rectangle<float>, int) const;
};

void AdaptiveClipperModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Adaptive Clipper waveform and state");
    component.setHelpText(
        "The graph shows measured input and output peak waveforms, clipping "
        "activity, transient-versus-body classification, and true-peak state. "
        "Drag lower control tracks; click Style, Oversampling, or Auto Trim. "
        "The display advances only when DSP telemetry does.");
}

juce::Rectangle<float> AdaptiveClipperModuleView::graphArea(
    juce::Rectangle<float> area)
{
    area.removeFromBottom(86.0f);
    return area.reduced(4.0f, 2.0f);
}

juce::Rectangle<float> AdaptiveClipperModuleView::waveformArea(
    juce::Rectangle<float> area)
{
    auto graph = graphArea(area);
    graph.removeFromTop(25.0f);
    graph.removeFromBottom(graph.getHeight() * 0.25f + 5.0f);
    return graph;
}

juce::Rectangle<float> AdaptiveClipperModuleView::classificationArea(
    juce::Rectangle<float> area)
{
    auto graph = graphArea(area);
    graph.removeFromTop(25.0f);
    return graph.removeFromBottom(graph.getHeight() * 0.25f);
}

juce::Rectangle<float> AdaptiveClipperModuleView::controlArea(
    juce::Rectangle<float> area, int control)
{
    auto controls = area.removeFromBottom(82.0f);
    constexpr int columns = 6;
    const auto width = controls.getWidth() / static_cast<float>(columns);
    const auto height = controls.getHeight() * 0.5f;
    return juce::Rectangle<float>(
               controls.getX() + width * static_cast<float>(control % columns),
               controls.getY() + height * static_cast<float>(control / columns),
               width, height)
        .reduced(3.0f, 4.0f);
}

void AdaptiveClipperModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int control = 0; control < 11; ++control)
    {
        if (!controlArea(area, control).contains(event.position))
            continue;
        if (control == AdaptiveClipperModule::styleControl
            || control == AdaptiveClipperModule::oversamplingControl)
        {
            cycleChoice(control, 3);
            return;
        }
        if (control == AdaptiveClipperModule::autoTrimControl)
        {
            toggleParameter(control);
            return;
        }
        dragPrimary = control;
        beginGestures();
        mouseDrag(event);
        return;
    }
}

void AdaptiveClipperModuleView::mouseDrag(const juce::MouseEvent& event)
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

void AdaptiveClipperModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int control = 0; control < 11; ++control)
        if (controlArea(area, control).contains(event.position))
        {
            resetToDefault(control);
            return;
        }
}

void AdaptiveClipperModuleView::drawControl(
    juce::Graphics& graphics, juce::Rectangle<float> bounds,
    int control) const
{
    const auto selected =
        control == AdaptiveClipperModule::autoTrimControl
            && value(control) >= 0.5f;
    graphics.setColour(selected ? outputColour.withAlpha(0.32f)
                                : juce::Colour(0xff151b24));
    graphics.fillRoundedRectangle(bounds, 5.0f);
    if (control != AdaptiveClipperModule::autoTrimControl)
    {
        graphics.setColour(
            (control <= AdaptiveClipperModule::ceilingControl
                 ? reductionColour : accent).withAlpha(0.30f));
        graphics.fillRoundedRectangle(
            bounds.withWidth(bounds.getWidth() * value(control)), 5.0f);
    }
    graphics.setColour(juce::Colours::white);
    graphics.setFont(juce::FontOptions(8.4f));
    graphics.drawFittedText(
        juce::String(controlMetadata(type, control).label).toUpperCase()
            + "  " + formatControlValue(type, control, value(control)),
        bounds.reduced(4.0f, 0.0f).toNearestInt(),
        juce::Justification::centred, 1, 0.64f);
}

void AdaptiveClipperModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto graph = graphArea(area);
    const auto waveform = waveformArea(area);
    const auto classification = classificationArea(area);
    graphics.setColour(juce::Colour(0xe6121821));
    graphics.fillRoundedRectangle(graph, 8.0f);

    ContinuousTelemetrySnapshot telemetry;
    const auto hasTelemetry =
        readContinuousTelemetry(telemetry)
        && telemetry.sequence != 0
        && telemetry.valueCount >= AdaptiveClipperModule::telemetryValueCount;
    const auto hasHistory =
        hasTelemetry
        && telemetry.historyValueCount
               >= AdaptiveClipperModule::telemetryHistoryValueCount;

    const auto truePeakDb = hasTelemetry
        ? juce::Decibels::gainToDecibels(
              telemetry.values[AdaptiveClipperModule::measuredTruePeak],
              -100.0f)
        : -100.0f;
    graphics.setFont(juce::FontOptions(9.4f, juce::Font::bold));
    graphics.setColour(inputColour);
    graphics.drawText("INPUT PEAK", graph.toNearestInt(),
                      juce::Justification::topLeft);
    graphics.setColour(outputColour);
    graphics.drawText(
        hasTelemetry
            ? "TRUE PEAK  " + juce::String(truePeakDb, 2)
                  + " dBTP  ·  MARGIN "
                  + juce::String(
                      telemetry.values[
                          AdaptiveClipperModule::ceilingMarginDecibels],
                      2)
                  + " dB"
            : "DSP TELEMETRY WAITING",
        graph.toNearestInt(), juce::Justification::topRight);

    graphics.setColour(juce::Colours::white.withAlpha(0.14f));
    graphics.drawHorizontalLine(
        juce::roundToInt(waveform.getCentreY()),
        waveform.getX(), waveform.getRight());
    const auto ceilingGain = juce::Decibels::decibelsToGain(
        lerp(-12.0f, 0.0f,
             value(AdaptiveClipperModule::ceilingControl)));
    const auto ceilingOffset =
        juce::jlimit(0.0f, 1.0f, ceilingGain) * waveform.getHeight() * 0.46f;
    graphics.setColour(reductionColour.withAlpha(0.7f));
    graphics.drawHorizontalLine(
        juce::roundToInt(waveform.getCentreY() - ceilingOffset),
        waveform.getX(), waveform.getRight());
    graphics.drawHorizontalLine(
        juce::roundToInt(waveform.getCentreY() + ceilingOffset),
        waveform.getX(), waveform.getRight());

    juce::Path inputUpper;
    juce::Path inputLower;
    juce::Path outputUpper;
    juce::Path outputLower;
    juce::Path classificationPath;
    const auto historyCount = hasHistory ? telemetry.historyCount : 0u;
    for (std::uint32_t index = 0; index < historyCount; ++index)
    {
        const auto proportion = static_cast<float>(index)
            / static_cast<float>(juce::jmax(1u, historyCount - 1));
        const auto x = waveform.getX() + proportion * waveform.getWidth();
        const auto input = juce::jlimit(
            0.0f, 1.5f,
            continuousTelemetryHistoryValue(
                telemetry, AdaptiveClipperModule::inputWaveformHistory,
                index));
        const auto output = juce::jlimit(
            0.0f, 1.5f,
            continuousTelemetryHistoryValue(
                telemetry, AdaptiveClipperModule::outputWaveformHistory,
                index));
        const auto inputOffset =
            input / 1.5f * waveform.getHeight() * 0.46f;
        const auto outputOffset =
            output / 1.5f * waveform.getHeight() * 0.46f;
        const auto classificationValue = juce::jlimit(
            0.0f, 1.0f,
            continuousTelemetryHistoryValue(
                telemetry, AdaptiveClipperModule::classificationHistory,
                index));
        const auto classificationY = classification.getBottom()
            - classificationValue * classification.getHeight();
        if (index == 0)
        {
            inputUpper.startNewSubPath(x, waveform.getCentreY() - inputOffset);
            inputLower.startNewSubPath(x, waveform.getCentreY() + inputOffset);
            outputUpper.startNewSubPath(x, waveform.getCentreY() - outputOffset);
            outputLower.startNewSubPath(x, waveform.getCentreY() + outputOffset);
            classificationPath.startNewSubPath(x, classificationY);
        }
        else
        {
            inputUpper.lineTo(x, waveform.getCentreY() - inputOffset);
            inputLower.lineTo(x, waveform.getCentreY() + inputOffset);
            outputUpper.lineTo(x, waveform.getCentreY() - outputOffset);
            outputLower.lineTo(x, waveform.getCentreY() + outputOffset);
            classificationPath.lineTo(x, classificationY);
        }

        const auto clipping = juce::jlimit(
            0.0f, 24.0f,
            continuousTelemetryHistoryValue(
                telemetry, AdaptiveClipperModule::clippingHistory, index));
        if (clipping > 0.05f)
        {
            const auto alpha = juce::jlimit(0.08f, 0.52f, clipping / 18.0f);
            const auto barWidth = waveform.getWidth()
                / static_cast<float>(juce::jmax(1u, historyCount));
            const auto barX = waveform.getX()
                + static_cast<float>(index) * barWidth;
            graphics.setColour(reductionColour.withAlpha(alpha));
            graphics.fillRect(
                barX, waveform.getY(), juce::jmax(1.0f, barWidth),
                waveform.getHeight());
        }
    }
    graphics.setColour(inputColour.withAlpha(0.72f));
    graphics.strokePath(inputUpper, juce::PathStrokeType(1.25f));
    graphics.strokePath(inputLower, juce::PathStrokeType(1.25f));
    graphics.setColour(outputColour);
    graphics.strokePath(outputUpper, juce::PathStrokeType(1.8f));
    graphics.strokePath(outputLower, juce::PathStrokeType(1.8f));

    graphics.setColour(outputColour.withAlpha(0.10f));
    graphics.fillRect(classification);
    graphics.setColour(reductionColour.withAlpha(0.10f));
    graphics.fillRect(classification.withTop(classification.getCentreY()));
    graphics.setColour(accent);
    graphics.strokePath(classificationPath, juce::PathStrokeType(2.0f));
    graphics.setFont(juce::FontOptions(8.8f, juce::Font::bold));
    graphics.setColour(outputColour);
    graphics.drawText("TRANSIENT", classification.toNearestInt(),
                      juce::Justification::topLeft);
    graphics.setColour(reductionColour);
    graphics.drawText("BODY", classification.toNearestInt(),
                      juce::Justification::bottomLeft);
    graphics.setColour(juce::Colours::white);
    graphics.drawText(
        hasTelemetry
            ? "CLIPPING  "
                  + juce::String(
                      telemetry.values[
                          AdaptiveClipperModule::clippingDecibels],
                      1)
                  + " dB  ·  "
                  + juce::String(
                      telemetry.values[
                          AdaptiveClipperModule::activeOversampling],
                      0)
                  + "x DSP"
            : juce::String(),
        classification.toNearestInt(), juce::Justification::centredRight);

    for (int control = 0; control < 11; ++control)
        drawControl(graphics, controlArea(area, control), control);
}
} // namespace

std::unique_ptr<ModuleView> createAdaptiveClipperView(EffectGraph& graph)
{
    return std::make_unique<AdaptiveClipperModuleView>(graph);
}
} // namespace megadsp::ui
