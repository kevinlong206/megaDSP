#include "../GraphStyle.h"
#include "../ModuleView.h"
#include "../../modules/StudioPlate.h"

#include "ModuleViewCreators.h"

#include <array>

namespace megadsp::ui
{
namespace
{
class StudioPlateModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    bool usesFullPanel() const override { return true; }

    void configureAccessibility(juce::Component& component) const override
    {
        component.setTitle("Studio Plate live surface");
        component.setHelpText(
            "Drag the plate surface horizontally to set Decay and "
            "vertically to set Damping. The surface shows the live "
            "excitation, modal ping, decay bands, and stereo pickups. "
            "Lower tracks edit Plate Size, Pre-Delay, Low Cut, High Cut, "
            "Pickup Spread, Width, Ducking, Dry, Wet, and Output.");
    }

    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    static constexpr std::array<int, 10> trackControls {
        StudioPlateModule::plateSizeControl,
        StudioPlateModule::preDelayControl,
        StudioPlateModule::lowCutControl,
        StudioPlateModule::highCutControl,
        StudioPlateModule::pickupSpreadControl,
        StudioPlateModule::widthControl,
        StudioPlateModule::duckingControl,
        StudioPlateModule::dryControl,
        StudioPlateModule::wetControl,
        StudioPlateModule::outputControl
    };

    static juce::Rectangle<float> surfaceBounds(juce::Rectangle<float> area)
    {
        area.removeFromBottom(area.getHeight() * 0.42f);
        return area.reduced(5.0f, 3.0f);
    }

    static juce::Rectangle<float> trackBounds(
        juce::Rectangle<float> area, int track)
    {
        auto tracks = area.removeFromBottom(area.getHeight() * 0.42f);
        const auto column = track % 5;
        const auto row = track / 5;
        const auto width = tracks.getWidth() / 5.0f;
        const auto height = tracks.getHeight() / 2.0f;
        return juce::Rectangle<float>(
            tracks.getX() + width * static_cast<float>(column),
            tracks.getY() + height * static_cast<float>(row),
            width, height).reduced(5.0f, 4.0f);
    }
};

void StudioPlateModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (surfaceBounds(area).contains(event.position))
    {
        dragPrimary = StudioPlateModule::decayControl;
        dragSecondary = StudioPlateModule::dampingControl;
    }
    else
    {
        for (int track = 0; track < static_cast<int>(trackControls.size());
             ++track)
            if (trackBounds(area, track).contains(event.position))
            {
                const auto control = trackControls[static_cast<size_t>(track)];
                if (control == StudioPlateModule::plateSizeControl)
                {
                    cycleChoice(control, StudioPlateModule::plateSizeCount);
                    dragPrimary = -1;
                    repaint();
                    return;
                }
                dragPrimary = control;
                break;
            }
    }
    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void StudioPlateModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (dragPrimary == StudioPlateModule::decayControl)
    {
        const auto field = surfaceBounds(area);
        setValue(
            dragPrimary,
            juce::jlimit(
                0.0f, 1.0f,
                (event.position.x - field.getX()) / field.getWidth()));
        setValue(
            dragSecondary,
            juce::jlimit(
                0.0f, 1.0f,
                (field.getBottom() - event.position.y) / field.getHeight()));
        dragReadout =
            "DECAY  " + formatControlValue(type, dragPrimary, value(dragPrimary))
            + "  |  DAMPING  "
            + formatControlValue(type, dragSecondary, value(dragSecondary));
        repaint();
        return;
    }

    for (int track = 0; track < static_cast<int>(trackControls.size()); ++track)
        if (trackControls[static_cast<size_t>(track)] == dragPrimary)
        {
            const auto bounds = trackBounds(area, track);
            setValue(
                dragPrimary,
                juce::jlimit(
                    0.0f, 1.0f,
                    (event.position.x - bounds.getX()) / bounds.getWidth()));
            updateDefaultDragReadout();
            return;
        }
}

void StudioPlateModuleView::mouseDoubleClick(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (surfaceBounds(area).contains(event.position))
    {
        resetToDefault(StudioPlateModule::decayControl);
        resetToDefault(StudioPlateModule::dampingControl);
        return;
    }
    for (int track = 0; track < static_cast<int>(trackControls.size()); ++track)
        if (trackBounds(area, track).contains(event.position))
        {
            resetToDefault(trackControls[static_cast<size_t>(track)]);
            return;
        }
}

void StudioPlateModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    area = area.reduced(12.0f);
    auto surface = surfaceBounds(area);
    graphics.setColour(juce::Colour(0xff10171d));
    graphics.fillRoundedRectangle(surface, 9.0f);

    ContinuousTelemetrySnapshot telemetry;
    const auto hasTelemetry =
        readContinuousTelemetry(telemetry)
        && telemetry.sequence != 0
        && telemetry.valueCount >= StudioPlateModule::telemetryValueCount;
    const auto excitation = hasTelemetry
        ? juce::jlimit(
              0.0f, 1.0f,
              telemetry.values[StudioPlateModule::excitationLevel] * 6.0f)
        : 0.0f;
    const auto modalActivity = hasTelemetry
        ? juce::jlimit(
              0.0f, 1.0f,
              telemetry.values[StudioPlateModule::modalActivityLevel] * 6.0f)
        : 0.0f;
    const auto leftPickup = hasTelemetry
        ? juce::jlimit(
              0.0f, 1.0f,
              telemetry.values[StudioPlateModule::leftPickupLevel] * 6.0f)
        : 0.0f;
    const auto rightPickup = hasTelemetry
        ? juce::jlimit(
              0.0f, 1.0f,
              telemetry.values[StudioPlateModule::rightPickupLevel] * 6.0f)
        : 0.0f;
    const auto lowBand = hasTelemetry
        ? juce::jlimit(
              0.0f, 1.0f,
              telemetry.values[StudioPlateModule::lowBandLevel] * 6.0f)
        : 0.0f;
    const auto midBand = hasTelemetry
        ? juce::jlimit(
              0.0f, 1.0f,
              telemetry.values[StudioPlateModule::midBandLevel] * 6.0f)
        : 0.0f;
    const auto highBand = hasTelemetry
        ? juce::jlimit(
              0.0f, 1.0f,
              telemetry.values[StudioPlateModule::highBandLevel] * 6.0f)
        : 0.0f;

    auto header = surface.removeFromTop(24.0f).reduced(8.0f, 0.0f);
    graphics.setColour(juce::Colours::white.withAlpha(0.88f));
    graphics.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    graphics.drawText(
        "STUDIO PLATE", header.toNearestInt(), juce::Justification::centredLeft);
    graphics.setColour(accent.withAlpha(0.78f));
    graphics.drawText(
        formatControlValue(
            type, StudioPlateModule::decayControl,
            value(StudioPlateModule::decayControl))
            + "  /  "
            + formatControlValue(
                type, StudioPlateModule::dampingControl,
                value(StudioPlateModule::dampingControl)),
        header.toNearestInt(), juce::Justification::centredRight);

    auto plateField = surface.reduced(10.0f, 6.0f);
    const auto bandRow = plateField.removeFromBottom(18.0f);
    graphics.setColour(juce::Colours::white.withAlpha(0.5f));
    graphics.setFont(juce::FontOptions(8.0f));
    graphics.drawText(
        "LOW/MID/HIGH DECAY", bandRow.toNearestInt().removeFromLeft(120),
        juce::Justification::centredLeft);
    auto bandMeters = bandRow.withTrimmedLeft(122.0f);
    const std::array<float, 3> bandLevels { lowBand, midBand, highBand };
    for (int band = 0; band < 3; ++band)
    {
        auto meter = bandMeters.removeFromLeft(bandMeters.getWidth() / 3.0f)
                         .reduced(3.0f, 2.0f);
        graphics.setColour(juce::Colour(0xff1c252c));
        graphics.fillRoundedRectangle(meter, 2.0f);
        graphics.setColour(accent.withAlpha(0.55f + 0.4f * bandLevels[
            static_cast<size_t>(band)]));
        graphics.fillRoundedRectangle(
            meter.withWidth(meter.getWidth()
                * bandLevels[static_cast<size_t>(band)]),
            2.0f);
    }

    // Modal ping: concentric rings expanding from the plate centre, driven
    // by the live modal-activity telemetry rather than editor-clock timing.
    const auto centre = plateField.getCentre();
    const auto maxRadius =
        juce::jmin(plateField.getWidth(), plateField.getHeight()) * 0.46f;
    for (int ring = 0; ring < 4; ++ring)
    {
        const auto proportion = static_cast<float>(ring + 1) / 4.0f;
        const auto radius = maxRadius * proportion;
        const auto alpha = (0.5f * excitation + 0.5f * modalActivity)
            * (1.0f - proportion) * 0.7f;
        graphics.setColour(outputColour.withAlpha(alpha));
        graphics.drawEllipse(
            centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f,
            1.4f);
    }
    graphics.setColour(accent.withAlpha(0.25f + 0.65f * excitation));
    const auto pingRadius = 4.0f + excitation * 10.0f;
    graphics.fillEllipse(
        centre.x - pingRadius, centre.y - pingRadius,
        pingRadius * 2.0f, pingRadius * 2.0f);

    // Stereo pickups: separation visualizes the Pickup Spread control,
    // glow visualizes the live left/right pickup telemetry.
    const auto spread = value(StudioPlateModule::pickupSpreadControl);
    const auto offset = spread * maxRadius * 0.85f;
    const std::array<juce::Point<float>, 2> pickupPositions {
        juce::Point<float>(centre.x - offset, centre.y + maxRadius * 0.55f),
        juce::Point<float>(centre.x + offset, centre.y + maxRadius * 0.55f)
    };
    const std::array<float, 2> pickupLevels { leftPickup, rightPickup };
    const char* pickupTags[2] { "L", "R" };
    for (int pickup = 0; pickup < 2; ++pickup)
    {
        const auto position = pickupPositions[static_cast<size_t>(pickup)];
        const auto glow = 3.0f + pickupLevels[static_cast<size_t>(pickup)] * 7.0f;
        graphics.setColour(outputColour.withAlpha(
            0.3f + 0.6f * pickupLevels[static_cast<size_t>(pickup)]));
        graphics.fillEllipse(
            position.x - glow, position.y - glow, glow * 2.0f, glow * 2.0f);
        graphics.setColour(juce::Colours::white.withAlpha(0.85f));
        graphics.drawText(
            pickupTags[pickup],
            juce::Rectangle<float>(position.x - 8.0f, position.y - 20.0f,
                                   16.0f, 14.0f)
                .toNearestInt(),
            juce::Justification::centred);
    }

    for (int track = 0; track < static_cast<int>(trackControls.size());
         ++track)
    {
        const auto control = trackControls[static_cast<size_t>(track)];
        const auto bounds = trackBounds(area, track);
        graphics.setColour(juce::Colour(0xff151d24));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour(
            (control == StudioPlateModule::dryControl
                 || control == StudioPlateModule::wetControl
                 || control == StudioPlateModule::outputControl
                 ? outputColour : accent)
                .withAlpha(0.64f));
        const auto fillProportion =
            control == StudioPlateModule::plateSizeControl
                ? (static_cast<float>(
                       discreteIndex(
                           value(control), StudioPlateModule::plateSizeCount))
                       + 1.0f)
                      / static_cast<float>(StudioPlateModule::plateSizeCount)
                : value(control);
        graphics.fillRoundedRectangle(
            bounds.withWidth(bounds.getWidth() * fillProportion), 5.0f);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(8.7f));
        graphics.drawFittedText(
            juce::String(controlMetadata(type, control).label) + "  "
                + formatControlValue(type, control, value(control)),
            bounds.reduced(5.0f, 0.0f).toNearestInt(),
            juce::Justification::centred, 1);
    }
}
} // namespace

std::unique_ptr<ModuleView> createStudioPlateView(EffectGraph& graph)
{
    return std::make_unique<StudioPlateModuleView>(graph);
}
} // namespace megadsp::ui
