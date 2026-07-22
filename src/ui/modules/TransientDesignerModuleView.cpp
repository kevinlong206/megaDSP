#include "../GraphStyle.h"
#include "../ModuleView.h"
#include "../../modules/TransientDesigner.h"

#include "ModuleViewCreators.h"

#include <array>
#include <cmath>

namespace megadsp::ui
{
namespace
{
class TransientDesignerModuleView final : public ModuleView
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
    static juce::Rectangle<float> guardArea(juce::Rectangle<float>);
    static juce::Rectangle<float> trackArea(juce::Rectangle<float>, int);
    juce::Point<float> handlePoint(juce::Rectangle<float>, int) const;
};

void TransientDesignerModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Visual Transient Designer");
    component.setHelpText(
        "Drag the Attack and Sustain handles vertically. Lower tracks edit "
        "Sensitivity, Speed, Focus, Mix, and Output. Click Clip Guard to "
        "enable bounded peak protection. The graph uses the selected DSP "
        "slot's actual fast and slow envelopes and attack and sustain "
        "shaping components.");
}

juce::Rectangle<float> TransientDesignerModuleView::plotArea(
    juce::Rectangle<float> area)
{
    area.removeFromTop(32.0f);
    area.removeFromBottom(40.0f);
    return area;
}

juce::Rectangle<float> TransientDesignerModuleView::guardArea(
    juce::Rectangle<float> area)
{
    return area.removeFromTop(28.0f).removeFromRight(154.0f)
        .reduced(4.0f, 2.0f);
}

juce::Rectangle<float> TransientDesignerModuleView::trackArea(
    juce::Rectangle<float> area, int index)
{
    auto row = area.removeFromBottom(34.0f);
    const auto width = row.getWidth() / 5.0f;
    return juce::Rectangle<float>(
               row.getX() + width * static_cast<float>(index),
               row.getY(), width, row.getHeight())
        .reduced(4.0f, 4.0f);
}

juce::Point<float> TransientDesignerModuleView::handlePoint(
    juce::Rectangle<float> plot, int control) const
{
    return {
        control == 0 ? plot.getX() + plot.getWidth() * 0.32f
                     : plot.getX() + plot.getWidth() * 0.68f,
        plot.getBottom() - value(control) * plot.getHeight()
    };
}

void TransientDesignerModuleView::mouseDown(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (guardArea(area).contains(event.position))
    {
        toggleParameter(5);
        return;
    }
    constexpr std::array<int, 5> controls { 2, 3, 4, 6, 7 };
    for (int track = 0; track < 5; ++track)
        if (trackArea(area, track).contains(event.position))
            dragPrimary = controls[static_cast<size_t>(track)];
    if (dragPrimary < 0)
    {
        const auto plot = plotArea(area);
        const auto attackDistance =
            event.position.getDistanceFrom(handlePoint(plot, 0));
        const auto sustainDistance =
            event.position.getDistanceFrom(handlePoint(plot, 1));
        if (juce::jmin(attackDistance, sustainDistance) <= 30.0f)
            dragPrimary = attackDistance <= sustainDistance ? 0 : 1;
    }
    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void TransientDesignerModuleView::mouseDrag(
    const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (dragPrimary == 0 || dragPrimary == 1)
    {
        const auto plot = plotArea(area);
        setValue(dragPrimary, juce::jlimit(
            0.0f, 1.0f,
            (plot.getBottom() - event.position.y) / plot.getHeight()));
    }
    else
    {
        constexpr std::array<int, 5> controls { 2, 3, 4, 6, 7 };
        int track = 0;
        while (track < 5
               && controls[static_cast<size_t>(track)] != dragPrimary)
            ++track;
        if (track >= 5)
            return;
        const auto bounds = trackArea(area, track);
        setValue(dragPrimary, juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - bounds.getX()) / bounds.getWidth()));
    }
    updateDefaultDragReadout();
}

void TransientDesignerModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (guardArea(area).contains(event.position))
    {
        resetToDefault(5);
        return;
    }
    const auto plot = plotArea(area);
    for (int control = 0; control < 2; ++control)
        if (event.position.getDistanceFrom(
                handlePoint(plot, control)) <= 30.0f)
            resetToDefault(control);
    constexpr std::array<int, 5> controls { 2, 3, 4, 6, 7 };
    for (int track = 0; track < 5; ++track)
        if (trackArea(area, track).contains(event.position))
        {
            const auto control = controls[static_cast<size_t>(track)];
            resetToDefault(control);
        }
}

void TransientDesignerModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto plot = plotArea(area);
    const auto centreY = plot.getCentreY();
    ContinuousTelemetrySnapshot telemetrySnapshot;
    const auto hasTelemetry =
        readContinuousTelemetry(telemetrySnapshot)
        && telemetrySnapshot.sequence != 0
        && telemetrySnapshot.valueCount
               >= TransientDesignerModule::telemetryValueCount;
    graphics.setColour(juce::Colour(0xff303a46));
    graphics.drawHorizontalLine(
        juce::roundToInt(centreY), plot.getX(), plot.getRight());

    juce::Path waveform;
    juce::Path attacks;
    juce::Path sustain;
    for (size_t index = 0; index < inputSamples.size(); ++index)
    {
        const auto x = plot.getX()
            + static_cast<float>(index)
                  / static_cast<float>(inputSamples.size() - 1)
                  * plot.getWidth();
        const auto sample = juce::jlimit(
            -1.0f, 1.0f, inputSamples[index]);
        const auto y = centreY - sample * plot.getHeight() * 0.42f;
        if (index == 0)
            waveform.startNewSubPath(x, y);
        else
            waveform.lineTo(x, y);
    }
    for (std::uint32_t index = 0;
         hasTelemetry && index < telemetrySnapshot.historyCount; ++index)
    {
        const auto x = plot.getX()
            + static_cast<float>(index)
                  / static_cast<float>(
                      juce::jmax(1u, telemetrySnapshot.historyCount - 1))
                  * plot.getWidth();
        const auto attackY = centreY
            - juce::jlimit(
                  -1.0f, 1.0f,
                  continuousTelemetryHistoryValue(
                      telemetrySnapshot,
                      TransientDesignerModule::attackShapingHistory,
                      index)
                      / 24.0f)
                  * plot.getHeight() * 0.44f;
        const auto sustainY = centreY
            - juce::jlimit(
                  -1.0f, 1.0f,
                  continuousTelemetryHistoryValue(
                      telemetrySnapshot,
                      TransientDesignerModule::sustainShapingHistory,
                      index)
                      / 24.0f)
                  * plot.getHeight() * 0.44f;
        if (index == 0)
        {
            attacks.startNewSubPath(x, attackY);
            sustain.startNewSubPath(x, sustainY);
        }
        else
        {
            attacks.lineTo(x, attackY);
            sustain.lineTo(x, sustainY);
        }
    }
    graphics.setColour(inputColour.withAlpha(0.72f));
    graphics.strokePath(waveform, juce::PathStrokeType(1.4f));
    graphics.setColour(reductionColour.withAlpha(0.90f));
    graphics.strokePath(attacks, juce::PathStrokeType(2.0f));
    graphics.setColour(outputColour.withAlpha(0.82f));
    graphics.strokePath(sustain, juce::PathStrokeType(2.0f));

    const std::array<const char*, 2> names { "ATTACK", "SUSTAIN" };
    const std::array<juce::Colour, 2> colours {
        reductionColour, outputColour
    };
    for (int control = 0; control < 2; ++control)
    {
        const auto point = handlePoint(plot, control);
        graphics.setColour(colours[static_cast<size_t>(control)]);
        graphics.drawVerticalLine(
            juce::roundToInt(point.x), plot.getY(), plot.getBottom());
        graphics.fillEllipse(
            juce::Rectangle<float>(20.0f, 20.0f).withCentre(point));
        graphics.setColour(juce::Colour(0xff10141a));
        graphics.drawEllipse(
            juce::Rectangle<float>(10.0f, 10.0f).withCentre(point), 2.0f);
        auto label = juce::Rectangle<float>(150.0f, 24.0f)
                         .withCentre(point.translated(0.0f, -20.0f))
                         .constrainedWithin(plot);
        graphics.setColour(juce::Colours::white);
        graphics.drawText(
            juce::String(names[static_cast<size_t>(control)]) + "  "
                + formatControlValue(type, control, value(control)),
            label.toNearestInt(), juce::Justification::centred);
    }
    graphics.setColour(reductionColour);
    graphics.drawText(
        hasTelemetry
            ? "DSP ATTACK  "
                  + juce::String(
                      telemetrySnapshot.values[
                          TransientDesignerModule::attackShapingDb],
                      1)
                  + " dB"
            : "DSP TELEMETRY WAITING",
        plot.toNearestInt(),
        juce::Justification::topLeft);
    graphics.setColour(outputColour);
    graphics.drawText(
        hasTelemetry
            ? "DSP SUSTAIN  "
                  + juce::String(
                      telemetrySnapshot.values[
                          TransientDesignerModule::sustainShapingDb],
                      1)
                  + " dB · FAST "
                  + juce::String(
                      telemetrySnapshot.values[
                          TransientDesignerModule::fastEnvelopeDb],
                      1)
                  + " / SLOW "
                  + juce::String(
                      telemetrySnapshot.values[
                          TransientDesignerModule::slowEnvelopeDb],
                      1)
                  + " dB"
            : juce::String(),
        plot.toNearestInt(),
        juce::Justification::topRight);

    const auto guard = guardArea(area);
    const auto guardOn = value(5) >= 0.5f;
    graphics.setColour(guardOn ? accent.withAlpha(0.36f)
                               : juce::Colour(0xff27303b));
    graphics.fillRoundedRectangle(guard, 5.0f);
    graphics.setColour(juce::Colours::white);
    graphics.drawText(
        guardOn ? "CLIP GUARD  ON" : "CLIP GUARD  OFF",
        guard.toNearestInt(), juce::Justification::centred);

    constexpr std::array<int, 5> controls { 2, 3, 4, 6, 7 };
    constexpr std::array<const char*, 5> trackNames {
        "SENSITIVITY", "SPEED", "FOCUS", "MIX", "OUTPUT"
    };
    for (int track = 0; track < 5; ++track)
    {
        const auto control = controls[static_cast<size_t>(track)];
        const auto bounds = trackArea(area, track);
        graphics.setColour(juce::Colour(0xff27303b));
        graphics.fillRoundedRectangle(bounds, 4.0f);
        graphics.setColour(accent.withAlpha(0.25f));
        graphics.fillRoundedRectangle(
            bounds.withWidth(bounds.getWidth() * value(control)), 4.0f);
        graphics.setColour(juce::Colours::white);
        graphics.drawText(
            juce::String(trackNames[static_cast<size_t>(track)]) + "  "
                + formatControlValue(type, control, value(control)),
            bounds.toNearestInt(), juce::Justification::centred);
    }
}
} // namespace

std::unique_ptr<ModuleView> createTransientDesignerView(EffectGraph& graph)
{
    return std::make_unique<TransientDesignerModuleView>(graph);
}
} // namespace megadsp::ui
