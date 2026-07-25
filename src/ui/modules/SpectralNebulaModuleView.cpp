#include "../GraphStyle.h"
#include "../ModuleView.h"
#include "../../modules/SpectralNebula.h"

#include "ModuleViewCreators.h"

#include <cmath>

namespace megadsp::ui
{
namespace
{
class SpectralNebulaModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    bool usesFullPanel() const override { return true; }

    void configureAccessibility(juce::Component& component) const override
    {
        component.setTitle("Spectral Nebula cloud");
        component.setHelpText(
            "Continuously absorbs guitar or vocal spectra into an evolving "
            "cloud. The upper field shows measured low, mid, and high cloud "
            "energy, drift, focus, and freeze state.");
    }

    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    static juce::Rectangle<float> fieldBounds(juce::Rectangle<float> area)
    {
        area.removeFromBottom(area.getHeight() * 0.58f);
        return area.reduced(5.0f);
    }

    static juce::Rectangle<float> trackBounds(
        juce::Rectangle<float> area, int control)
    {
        auto tracks = area.removeFromBottom(area.getHeight() * 0.58f);
        const auto width = tracks.getWidth() / 4.0f;
        const auto height = tracks.getHeight() / 3.0f;
        return juce::Rectangle<float>(
            tracks.getX() + width * static_cast<float>(control % 4),
            tracks.getY() + height * static_cast<float>(control / 4),
            width, height).reduced(6.0f, 4.0f);
    }
};

void SpectralNebulaModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int control = 0; control < megadsp::controlsPerSlot; ++control)
        if (trackBounds(area, control).contains(event.position))
        {
            dragPrimary = control;
            break;
        }
    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void SpectralNebulaModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto track = trackBounds(
        getLocalBounds().toFloat().reduced(12.0f), dragPrimary);
    if (dragPrimary == SpectralNebulaModule::freezeControl)
        setValue(dragPrimary, event.position.x >= track.getCentreX() ? 1.0f : 0.0f);
    else
        setValue(dragPrimary, juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - track.getX()) / track.getWidth()));
    updateDefaultDragReadout();
}

void SpectralNebulaModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int control = 0; control < megadsp::controlsPerSlot; ++control)
        if (trackBounds(area, control).contains(event.position))
        {
            resetToDefault(control);
            return;
        }
}

void SpectralNebulaModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    area = area.reduced(12.0f);
    auto field = fieldBounds(area);
    graphics.setColour(juce::Colour(0xe6121821));
    graphics.fillRoundedRectangle(field, 8.0f);

    ContinuousTelemetrySnapshot telemetry;
    const auto hasTelemetry =
        readContinuousTelemetry(telemetry)
        && telemetry.sequence != 0
        && telemetry.valueCount >= SpectralNebulaModule::telemetryValueCount;
    const auto frozen = hasTelemetry
        && telemetry.values[SpectralNebulaModule::frozen] > 0.5f;
    const auto cloud = hasTelemetry
        ? juce::jlimit(
              0.0f, 1.0f,
              telemetry.values[SpectralNebulaModule::cloudEnergy] * 5.0f)
        : 0.0f;
    const auto drift = hasTelemetry
        ? telemetry.values[SpectralNebulaModule::actualDriftCents] / 40.0f
        : 0.0f;

    auto header = field.removeFromTop(24.0f).reduced(8.0f, 0.0f);
    graphics.setColour(juce::Colours::white.withAlpha(0.90f));
    graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    graphics.drawText(
        frozen ? "SPECTRAL CLOUD FROZEN"
               : "ABSORBING LIVE SPECTRUM INTO THE CLOUD",
        header.toNearestInt(), juce::Justification::centredLeft);

    const std::array<const char*, 3> labels { "LOW", "MID", "HIGH" };
    const std::array<juce::Colour, 3> colours {
        inputColour, accent, outputColour
    };
    const auto centreY = field.getCentreY();
    for (int band = 0; band < 3; ++band)
    {
        const auto energy = hasTelemetry
            ? juce::jlimit(
                  0.0f, 1.0f,
                  telemetry.values[static_cast<std::size_t>(
                      SpectralNebulaModule::lowEnergy + band)] * 7.0f)
            : 0.0f;
        const auto x = field.getX()
            + field.getWidth() * static_cast<float>(band + 1) * 0.25f
            + drift * 10.0f * static_cast<float>(band - 1);
        const auto radius = 18.0f + 28.0f * std::sqrt(energy);
        graphics.setColour(
            colours[static_cast<std::size_t>(band)]
                .withAlpha(0.10f + 0.40f * energy));
        graphics.fillEllipse(
            x - radius, centreY - radius, radius * 2.0f, radius * 2.0f);
        graphics.setColour(
            colours[static_cast<std::size_t>(band)]
                .withAlpha(0.50f + 0.45f * energy));
        for (int ring = 0; ring < 3; ++ring)
        {
            const auto ringRadius =
                radius * (0.48f + 0.24f * static_cast<float>(ring));
            graphics.drawEllipse(
                x - ringRadius, centreY - ringRadius,
                ringRadius * 2.0f, ringRadius * 2.0f,
                1.0f + cloud * 1.4f);
        }
        graphics.setColour(juce::Colours::white.withAlpha(0.78f));
        graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        graphics.drawText(
            labels[static_cast<std::size_t>(band)],
            juce::Rectangle<float>(
                x - 30.0f, centreY + radius + 2.0f, 60.0f, 14.0f)
                .toNearestInt(),
            juce::Justification::centred);
    }

    for (int control = 0; control < megadsp::controlsPerSlot; ++control)
    {
        const auto bounds = trackBounds(area, control);
        graphics.setColour(juce::Colour(0xff151b24));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        const auto activeFreeze =
            control == SpectralNebulaModule::freezeControl
            && value(control) >= 0.5f;
        graphics.setColour(
            (control >= SpectralNebulaModule::dryControl
                 ? outputColour : accent)
                .withAlpha(activeFreeze ? 0.92f : 0.58f));
        graphics.fillRoundedRectangle(
            bounds.withWidth(bounds.getWidth() * value(control)), 5.0f);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(9.3f));
        graphics.drawFittedText(
            juce::String(controlMetadata(type, control).label) + "  "
                + formatControlValue(type, control, value(control)),
            bounds.reduced(6.0f, 0.0f).toNearestInt(),
            juce::Justification::centred, 1);
    }
}
} // namespace

std::unique_ptr<ModuleView> createSpectralNebulaView(EffectGraph& graph)
{
    return std::make_unique<SpectralNebulaModuleView>(graph);
}
} // namespace megadsp::ui
