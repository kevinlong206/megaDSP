#include "../GraphStyle.h"
#include "../ModuleView.h"
#include "../../modules/VinylSimulator.h"

#include "ModuleViewCreators.h"

#include <cmath>

namespace megadsp::ui
{
namespace
{
class VinylSimulatorModuleView final : public ModuleView
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
    static juce::Rectangle<float> recordBounds(
        juce::Rectangle<float>);
    static juce::Rectangle<float> controlBounds(
        juce::Rectangle<float>, int);
};

void VinylSimulatorModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Vinyl simulator turntable");
    component.setHelpText(
        "The record shows measured platter motion, crackle, scratches, noise, "
        "and rumble. Drag any lower track horizontally to edit its labeled "
        "control. Double-click a track to restore its default.");
}

juce::Rectangle<float> VinylSimulatorModuleView::recordBounds(
    juce::Rectangle<float> area)
{
    area.removeFromBottom(area.getHeight() * 0.42f);
    return area.reduced(5.0f);
}

juce::Rectangle<float> VinylSimulatorModuleView::controlBounds(
    juce::Rectangle<float> area, int control)
{
    auto controls = area.removeFromBottom(area.getHeight() * 0.42f);
    const auto column = control % 4;
    const auto row = control / 4;
    const auto width = controls.getWidth() * 0.25f;
    const auto height = controls.getHeight() / 3.0f;
    return juce::Rectangle<float>(
        controls.getX() + width * static_cast<float>(column),
        controls.getY() + height * static_cast<float>(row),
        width, height).reduced(6.0f, 5.0f);
}

void VinylSimulatorModuleView::mouseDown(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int control = 0; control < controlsPerSlot; ++control)
        if (controlBounds(area, control).contains(event.position))
        {
            dragPrimary = control;
            break;
        }
    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void VinylSimulatorModuleView::mouseDrag(
    const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto bounds = controlBounds(
        getLocalBounds().toFloat().reduced(12.0f), dragPrimary);
    setValue(
        dragPrimary,
        juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - bounds.getX()) / bounds.getWidth()));
    updateDefaultDragReadout();
}

void VinylSimulatorModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int control = 0; control < controlsPerSlot; ++control)
        if (controlBounds(area, control).contains(event.position))
        {
            resetToDefault(control);
            return;
        }
}

void VinylSimulatorModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto field = recordBounds(area);
    graphics.setColour(juce::Colour(0xff11151b));
    graphics.fillRoundedRectangle(field, 9.0f);

    ContinuousTelemetrySnapshot telemetry;
    const auto hasTelemetry =
        readContinuousTelemetry(telemetry)
        && telemetry.sequence != 0
        && telemetry.valueCount >= VinylSimulatorModule::telemetryValueCount;
    const auto phase = hasTelemetry
        ? telemetry.values[VinylSimulatorModule::transportPhase] : 0.0f;
    const auto condition = value(VinylSimulatorModule::conditionControl);
    const auto recordDiameter = juce::jmin(
        field.getHeight() - 18.0f,
        field.getWidth() * 0.54f);
    const auto record = juce::Rectangle<float>(
        recordDiameter, recordDiameter).withCentre(field.getCentre());

    graphics.setColour(
        juce::Colour(0xff24252a).interpolatedWith(
            juce::Colour(0xff171719), condition));
    graphics.fillEllipse(record);
    for (int groove = 1; groove <= 18; ++groove)
    {
        const auto scale =
            0.24f + static_cast<float>(groove) / 18.0f * 0.72f;
        graphics.setColour(juce::Colours::white.withAlpha(
            groove % 3 == 0 ? 0.11f : 0.045f));
        graphics.drawEllipse(
            record.withSizeKeepingCentre(
                record.getWidth() * scale,
                record.getHeight() * scale),
            groove % 3 == 0 ? 1.0f : 0.65f);
    }

    const auto label = record.withSizeKeepingCentre(
        record.getWidth() * 0.31f, record.getHeight() * 0.31f);
    graphics.setColour(accent.darker(0.28f));
    graphics.fillEllipse(label);
    const auto phaseAngle =
        static_cast<float>(phase * juce::MathConstants<double>::twoPi);
    const auto highlightStart = record.getCentre()
        + juce::Point<float>(
              std::cos(phaseAngle), std::sin(phaseAngle))
              * record.getWidth() * 0.14f;
    const auto highlightEnd = record.getCentre()
        + juce::Point<float>(
              std::cos(phaseAngle), std::sin(phaseAngle))
              * record.getWidth() * 0.46f;
    graphics.setColour(accent.withAlpha(0.68f));
    graphics.drawLine(
        highlightStart.x, highlightStart.y,
        highlightEnd.x, highlightEnd.y, 2.0f);
    graphics.setColour(juce::Colour(0xffe5d8b5));
    graphics.fillEllipse(
        juce::Rectangle<float>(8.0f, 8.0f)
            .withCentre(record.getCentre()));

    if (hasTelemetry)
    {
        const auto historyCount = telemetry.historyCount;
        for (std::uint32_t index = 0; index < historyCount; ++index)
        {
            const auto left = continuousTelemetryHistoryValue(
                telemetry, VinylSimulatorModule::leftCrackleHistory, index);
            const auto right = continuousTelemetryHistoryValue(
                telemetry, VinylSimulatorModule::rightCrackleHistory, index);
            const auto scratch = continuousTelemetryHistoryValue(
                telemetry, VinylSimulatorModule::scratchHistory, index);
            const auto activity = juce::jmax(left, right);
            if (activity <= 0.0001f && scratch <= 0.0001f)
                continue;
            const auto age = static_cast<float>(historyCount - 1u - index)
                / static_cast<float>(
                    continuousTelemetryHistoryCapacity);
            const auto angle = phaseAngle
                - age * juce::MathConstants<float>::twoPi * 1.8f;
            const auto radius = record.getWidth()
                * (0.25f + 0.20f * (1.0f - age));
            const auto start = record.getCentre()
                + juce::Point<float>(std::cos(angle), std::sin(angle))
                      * radius;
            const auto length = juce::jlimit(
                2.0f, 20.0f, (activity * 55.0f + scratch * 40.0f));
            const auto end = record.getCentre()
                + juce::Point<float>(std::cos(angle), std::sin(angle))
                      * (radius + length);
            graphics.setColour(
                (scratch > activity
                     ? juce::Colour(0xffffb15c) : juce::Colours::white)
                    .withAlpha(0.18f + (1.0f - age) * 0.72f));
            graphics.drawLine(start.x, start.y, end.x, end.y, 1.2f);
        }
    }

    graphics.setColour(juce::Colours::white.withAlpha(0.82f));
    graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    graphics.drawText(
        "GROOVE ACTIVITY", field.reduced(9.0f).toNearestInt(),
        juce::Justification::topLeft);
    graphics.setColour(juce::Colours::white.withAlpha(0.50f));
    graphics.drawText(
        hasTelemetry
            ? "WOW "
                  + juce::String(
                      telemetry.values[
                          VinylSimulatorModule::modulationMilliseconds],
                      2)
                  + " ms  /  SURFACE "
                  + juce::String(
                      telemetry.values[
                          VinylSimulatorModule::surfaceNoiseActivity]
                          * 100.0f,
                      0)
                  + "%"
            : "PLAY AUDIO FOR MEASURED ACTIVITY",
        field.reduced(9.0f).toNearestInt(),
        juce::Justification::topRight);

    for (int control = 0; control < controlsPerSlot; ++control)
    {
        const auto bounds = controlBounds(area, control);
        graphics.setColour(juce::Colour(0xff151b24));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        const auto colour =
            control >= VinylSimulatorModule::mixControl
                ? outputColour
                : control >= VinylSimulatorModule::wowControl
                    ? accent : juce::Colour(0xffffb15c);
        graphics.setColour(colour.withAlpha(0.56f));
        graphics.fillRoundedRectangle(
            bounds.withWidth(bounds.getWidth() * value(control)), 5.0f);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(8.8f, juce::Font::bold));
        graphics.drawFittedText(
            juce::String(controlMetadata(type, control).label)
                + "  " + formatControlValue(type, control, value(control)),
            bounds.reduced(5.0f, 0.0f).toNearestInt(),
            juce::Justification::centred, 1, 0.72f);
    }
}
} // namespace

std::unique_ptr<ModuleView> createVinylSimulatorView(EffectGraph& graph)
{
    return std::make_unique<VinylSimulatorModuleView>(graph);
}
} // namespace megadsp::ui
