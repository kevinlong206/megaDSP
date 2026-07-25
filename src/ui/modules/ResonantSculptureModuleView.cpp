#include "../GraphStyle.h"
#include "../ModuleView.h"
#include "../../modules/ResonantSculpture.h"

#include "ModuleViewCreators.h"

#include <cmath>

namespace megadsp::ui
{
namespace
{
class ResonantSculptureModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    bool usesFullPanel() const override { return true; }

    void configureAccessibility(juce::Component& component) const override
    {
        component.setTitle("Resonant Sculpture physical model editor");
        component.setHelpText(
            "Choose String, Glass, Metal, or Membrane material. The animated "
            "structure follows measured modal energy. Independent rails edit "
            "shape, tracked-pitch offset, excitation, decay, mutation, bow, "
            "strike, stereo field, chaos, mix, and output.");
    }

    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    static constexpr std::array<int, 11> trackControls {
        ResonantSculptureModule::shapeControl,
        ResonantSculptureModule::tuningControl,
        ResonantSculptureModule::excitationControl,
        ResonantSculptureModule::decayControl,
        ResonantSculptureModule::mutationControl,
        ResonantSculptureModule::bowControl,
        ResonantSculptureModule::strikeControl,
        ResonantSculptureModule::stereoFieldControl,
        ResonantSculptureModule::chaosControl,
        ResonantSculptureModule::mixControl,
        ResonantSculptureModule::outputControl
    };

    static juce::Rectangle<float> materialPill(
        juce::Rectangle<float> area, int material)
    {
        auto row = area.removeFromTop(30.0f);
        const auto width = row.getWidth() * 0.25f;
        return juce::Rectangle<float>(
            row.getX() + width * static_cast<float>(material), row.getY(),
            width, row.getHeight()).reduced(4.0f, 2.0f);
    }

    static juce::Rectangle<float> structureBounds(juce::Rectangle<float> area)
    {
        area.removeFromTop(32.0f);
        area.removeFromBottom(area.getHeight() * 0.55f);
        return area.reduced(5.0f, 3.0f);
    }

    static juce::Rectangle<float> trackBounds(
        juce::Rectangle<float> area, int track)
    {
        area.removeFromTop(32.0f);
        auto tracks = area.removeFromBottom(area.getHeight() * 0.55f);
        const auto width = tracks.getWidth() * 0.25f;
        const auto height = tracks.getHeight() / 3.0f;
        return juce::Rectangle<float>(
            tracks.getX() + width * static_cast<float>(track % 4),
            tracks.getY() + height * static_cast<float>(track / 4),
            width, height).reduced(5.0f, 4.0f);
    }

    void chooseMaterial(int material);
};

void ResonantSculptureModuleView::chooseMaterial(int material)
{
    if (auto* target = parameter(ResonantSculptureModule::materialControl))
    {
        graph.focusKeyboardControl(ResonantSculptureModule::materialControl);
        target->beginChangeGesture();
        target->setValueNotifyingHost(discreteValue(material, 4));
        target->endChangeGesture();
    }
    repaint();
}

void ResonantSculptureModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int material = 0; material < 4; ++material)
        if (materialPill(area, material).contains(event.position))
        {
            chooseMaterial(material);
            return;
        }
    for (int track = 0; track < static_cast<int>(trackControls.size()); ++track)
        if (trackBounds(area, track).contains(event.position))
        {
            dragPrimary = trackControls[static_cast<size_t>(track)];
            break;
        }
    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void ResonantSculptureModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    auto track = 0;
    for (; track < static_cast<int>(trackControls.size()); ++track)
        if (trackControls[static_cast<size_t>(track)] == dragPrimary)
            break;
    const auto bounds = trackBounds(area, juce::jlimit(0, 10, track));
    setValue(
        dragPrimary,
        juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - bounds.getX()) / bounds.getWidth()));
    updateDefaultDragReadout();
}

void ResonantSculptureModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int material = 0; material < 4; ++material)
        if (materialPill(area, material).contains(event.position))
        {
            resetToDefault(ResonantSculptureModule::materialControl);
            return;
        }
    for (int track = 0; track < static_cast<int>(trackControls.size()); ++track)
        if (trackBounds(area, track).contains(event.position))
        {
            resetToDefault(trackControls[static_cast<size_t>(track)]);
            return;
        }
}

void ResonantSculptureModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    static constexpr std::array<const char*, 4> materialNames {
        "STRING", "GLASS", "METAL", "MEMBRANE"
    };
    const auto selectedMaterial = discreteIndex(
        value(ResonantSculptureModule::materialControl), 4);
    for (int material = 0; material < 4; ++material)
    {
        const auto bounds = materialPill(area, material);
        graphics.setColour(
            material == selectedMaterial
                ? accent : juce::Colour(0xff242c33));
        graphics.fillRoundedRectangle(bounds, 6.0f);
        graphics.setColour(
            material == selectedMaterial
                ? juce::Colour(0xff11161b) : juce::Colours::white);
        graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        graphics.drawText(
            materialNames[static_cast<size_t>(material)],
            bounds.toNearestInt(), juce::Justification::centred);
    }

    ContinuousTelemetrySnapshot telemetry;
    const auto hasTelemetry =
        readContinuousTelemetry(telemetry)
        && telemetry.sequence != 0
        && telemetry.valueCount
            >= ResonantSculptureModule::telemetryValueCount;
    const auto energy = hasTelemetry
        ? juce::jlimit(
              0.0f, 1.0f,
              telemetry.values[ResonantSculptureModule::overallEnergy] * 5.0f)
        : 0.0f;
    const auto transient = hasTelemetry
        ? telemetry.values[ResonantSculptureModule::transientAmount] : 0.0f;
    const auto chaos = hasTelemetry
        ? telemetry.values[ResonantSculptureModule::chaosPosition] : 0.0f;
    const auto trackedPitch = hasTelemetry
        ? telemetry.values[ResonantSculptureModule::trackedFrequencyHz] : 0.0f;
    const auto tracking = hasTelemetry
        ? telemetry.values[ResonantSculptureModule::trackingConfidence] : 0.0f;
    const auto shape = value(ResonantSculptureModule::shapeControl);

    const auto field = structureBounds(area);
    graphics.setColour(juce::Colour(0xff10141b));
    graphics.fillRoundedRectangle(field, 9.0f);
    std::array<juce::Point<float>, 8> nodes {};
    for (int node = 0; node < 8; ++node)
    {
        const auto angle =
            juce::MathConstants<float>::twoPi
                * static_cast<float>(node) / 8.0f
            + shape * 0.45f + chaos * 0.10f;
        const auto modeEnergy = hasTelemetry
            ? juce::jlimit(
                  0.0f, 1.0f,
                  telemetry.values[
                      static_cast<size_t>(
                          ResonantSculptureModule::firstModeEnergy + node)]
                      * 6.0f)
            : 0.0f;
        const auto radius = field.getHeight()
            * (0.18f + 0.20f * shape + modeEnergy * 0.07f);
        const auto horizontalWarp =
            0.72f + 0.28f * std::sin(angle * (1.0f + shape * 2.0f));
        nodes[static_cast<size_t>(node)] = {
            field.getCentreX()
                + std::cos(angle) * radius * horizontalWarp,
            field.getCentreY()
                + std::sin(angle) * radius
                    * (0.72f + shape * 0.28f)
        };
    }
    juce::Path body;
    body.startNewSubPath(nodes[0]);
    for (int node = 1; node < 8; ++node)
        body.lineTo(nodes[static_cast<size_t>(node)]);
    body.closeSubPath();
    graphics.setColour(accent.withAlpha(0.10f + energy * 0.22f));
    graphics.fillPath(body);
    graphics.setColour(accent.withAlpha(0.45f + energy * 0.45f));
    graphics.strokePath(
        body, juce::PathStrokeType(1.4f + energy * 2.0f));
    for (int node = 0; node < 8; ++node)
    {
        const auto modeEnergy = hasTelemetry
            ? juce::jlimit(
                  0.0f, 1.0f,
                  telemetry.values[
                      static_cast<size_t>(
                          ResonantSculptureModule::firstModeEnergy + node)]
                      * 6.0f)
            : 0.0f;
        const auto size = 6.0f + modeEnergy * 9.0f + transient * 3.0f;
        graphics.setColour(
            (node % 2 == 0 ? outputColour : accent)
                .withAlpha(0.55f + modeEnergy * 0.40f));
        graphics.fillEllipse(
            juce::Rectangle<float>(size, size)
                .withCentre(nodes[static_cast<size_t>(node)]));
    }
    graphics.setColour(juce::Colours::white.withAlpha(0.9f));
    graphics.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    auto fieldHeader = field;
    graphics.drawText(
        "MODAL ENERGY  " + juce::String(energy * 100.0f, 0) + "%"
            + "   |   TRACK  "
            + (tracking >= 0.2f
                   ? juce::String(trackedPitch, 1) + " Hz"
                   : juce::String("--")),
        fieldHeader.removeFromTop(23.0f).reduced(8.0f, 0.0f).toNearestInt(),
        juce::Justification::centredLeft);

    for (int track = 0; track < static_cast<int>(trackControls.size()); ++track)
    {
        const auto control = trackControls[static_cast<size_t>(track)];
        const auto bounds = trackBounds(area, track);
        graphics.setColour(juce::Colour(0xff171c24));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour(
            (control >= ResonantSculptureModule::mixControl
                 ? outputColour : accent).withAlpha(0.65f));
        graphics.fillRoundedRectangle(
            bounds.withWidth(bounds.getWidth() * value(control)), 5.0f);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(8.5f));
        graphics.drawFittedText(
            juce::String(controlMetadata(type, control).label) + "  "
                + formatControlValue(type, control, value(control)),
            bounds.reduced(5.0f, 0.0f).toNearestInt(),
            juce::Justification::centred, 1);
    }
}
} // namespace

std::unique_ptr<ModuleView> createResonantSculptureView(EffectGraph& graph)
{
    return std::make_unique<ResonantSculptureModuleView>(graph);
}
} // namespace megadsp::ui
