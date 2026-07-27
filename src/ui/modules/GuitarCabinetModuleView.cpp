#include "../GraphStyle.h"
#include "../ModuleView.h"

#include "ModuleViewCreators.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace megadsp::ui
{
namespace
{
constexpr std::array<int, 7> trackControls { 5, 6, 7, 8, 9, 10, 11 };

float modelRowsHeight(juce::Rectangle<float> area)
{
    return juce::jlimit(60.0f, 87.0f, area.getHeight() * 0.27f);
}

float controlRowsHeight(juce::Rectangle<float> area)
{
    return juce::jlimit(76.0f, 108.0f, area.getHeight() * 0.34f);
}

class GuitarCabinetModuleView final : public ModuleView
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
    static juce::Rectangle<float> pillBounds(
        juce::Rectangle<float>, int control, int option, int count);
    static juce::Rectangle<float> cabinetBounds(juce::Rectangle<float>);
    static juce::Rectangle<float> trackBounds(
        juce::Rectangle<float>, int control);
    juce::Point<float> microphonePoint(juce::Rectangle<float>) const;
    void choose(int control, int option, int count);
};

void GuitarCabinetModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Guitar Cabinet speaker and microphone editor");
    component.setHelpText(
        "Choose generic speaker, cabinet, and microphone models with the "
        "top pills. Drag the microphone across the speaker for position and "
        "toward or away from the cabinet for distance. The lower tracks edit "
        "Speaker Drive, Cone Breakup, Cabinet Resonance, Damping, Room, Room "
        "Width, and Output. Double-click any gesture to restore its default.");
}

juce::Rectangle<float> GuitarCabinetModuleView::pillBounds(
    juce::Rectangle<float> area, int control, int option, int count)
{
    auto rows = area.removeFromTop(modelRowsHeight(area));
    const auto rowHeight = rows.getHeight() / 3.0f;
    auto row = juce::Rectangle<float>(
        rows.getX(), rows.getY() + rowHeight * static_cast<float>(control),
        rows.getWidth(), rowHeight);
    const auto width = row.getWidth() / static_cast<float>(count);
    return juce::Rectangle<float>(
        row.getX() + width * static_cast<float>(option), row.getY(),
        width, row.getHeight()).reduced(3.0f, 2.0f);
}

juce::Rectangle<float> GuitarCabinetModuleView::cabinetBounds(
    juce::Rectangle<float> area)
{
    const auto modelHeight = modelRowsHeight(area);
    const auto controlsHeight = controlRowsHeight(area);
    area.removeFromTop(modelHeight + 4.0f);
    area.removeFromBottom(controlsHeight + 4.0f);
    return area.reduced(8.0f, 2.0f);
}

juce::Rectangle<float> GuitarCabinetModuleView::trackBounds(
    juce::Rectangle<float> area, int control)
{
    auto tracks = area.removeFromBottom(controlRowsHeight(area));
    auto iterator = std::find(
        trackControls.begin(), trackControls.end(), control);
    if (iterator == trackControls.end())
        return {};
    const auto index = static_cast<int>(
        std::distance(trackControls.begin(), iterator));
    const auto row = index < 4 ? 0 : 1;
    const auto column = row == 0 ? index : index - 4;
    const auto columns = row == 0 ? 4 : 3;
    const auto width = tracks.getWidth() / static_cast<float>(columns);
    const auto height = tracks.getHeight() * 0.5f;
    return juce::Rectangle<float>(
        tracks.getX() + width * static_cast<float>(column),
        tracks.getY() + height * static_cast<float>(row),
        width, height).reduced(6.0f, 3.0f);
}

juce::Point<float> GuitarCabinetModuleView::microphonePoint(
    juce::Rectangle<float> area) const
{
    const auto cabinet = cabinetBounds(area);
    const auto speakerRadius =
        juce::jmin(cabinet.getWidth() * 0.22f, cabinet.getHeight() * 0.34f);
    const auto speakerCentre = juce::Point<float>(
        cabinet.getCentreX(), cabinet.getY() + cabinet.getHeight() * 0.37f);
    const auto x = speakerCentre.x + value(3) * speakerRadius * 0.88f;
    const auto micGap = juce::jmin(13.0f, cabinet.getHeight() * 0.10f);
    const auto y = speakerCentre.y + speakerRadius + micGap
        + value(4) * juce::jmax(8.0f, cabinet.getHeight() * 0.18f);
    return { x, y };
}

void GuitarCabinetModuleView::choose(
    int control, int option, int count)
{
    if (auto* target = parameter(control))
    {
        graph.focusKeyboardControl(control);
        target->beginChangeGesture();
        target->setValueNotifyingHost(discreteValue(option, count));
        target->endChangeGesture();
    }
    repaint();
}

void GuitarCabinetModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    constexpr std::array<int, 3> optionCounts { 4, 4, 3 };
    for (int control = 0; control < 3; ++control)
        for (int option = 0; option < optionCounts[static_cast<size_t>(control)];
             ++option)
            if (pillBounds(
                    area, control, option,
                    optionCounts[static_cast<size_t>(control)])
                    .contains(event.position))
            {
                choose(
                    control, option,
                    optionCounts[static_cast<size_t>(control)]);
                return;
            }

    if (cabinetBounds(area).contains(event.position))
    {
        dragPrimary = 3;
        dragSecondary = 4;
    }
    else
        for (const auto control : trackControls)
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

void GuitarCabinetModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (dragPrimary == 3)
    {
        const auto cabinet = cabinetBounds(area);
        const auto speakerRadius =
            juce::jmin(cabinet.getWidth() * 0.22f, cabinet.getHeight() * 0.34f);
        const auto speakerY =
            cabinet.getY() + cabinet.getHeight() * 0.37f;
        setValue(3, juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - cabinet.getCentreX())
                / juce::jmax(1.0f, speakerRadius * 0.88f)));
        setValue(4, juce::jlimit(
            0.0f, 1.0f,
            (event.position.y - speakerY - speakerRadius
             - juce::jmin(13.0f, cabinet.getHeight() * 0.10f))
                / juce::jmax(8.0f, cabinet.getHeight() * 0.18f)));
        dragReadout =
            "MIC  " + formatControlValue(type, 3, value(3))
            + "  -  " + formatControlValue(type, 4, value(4));
    }
    else
    {
        const auto track = trackBounds(area, dragPrimary);
        setValue(dragPrimary, juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - track.getX()) / track.getWidth()));
        updateDefaultDragReadout();
    }
    repaint();
}

void GuitarCabinetModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    constexpr std::array<int, 3> optionCounts { 4, 4, 3 };
    const auto defaults = moduleDefaults(type);
    for (int control = 0; control < 3; ++control)
        for (int option = 0; option < optionCounts[static_cast<size_t>(control)];
             ++option)
            if (pillBounds(
                    area, control, option,
                    optionCounts[static_cast<size_t>(control)])
                    .contains(event.position))
            {
                setValue(control, defaults[static_cast<size_t>(control)]);
                repaint();
                return;
            }
    if (cabinetBounds(area).contains(event.position))
    {
        setValue(3, defaults[3]);
        setValue(4, defaults[4]);
        repaint();
        return;
    }
    for (const auto control : trackControls)
        if (trackBounds(area, control).contains(event.position))
        {
            setValue(control, defaults[static_cast<size_t>(control)]);
            repaint();
            return;
        }
}

void GuitarCabinetModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    constexpr std::array<int, 3> optionCounts { 4, 4, 3 };
    for (int control = 0; control < 3; ++control)
        for (int option = 0; option < optionCounts[static_cast<size_t>(control)];
             ++option)
        {
            const auto selected =
                discreteIndex(
                    value(control),
                    optionCounts[static_cast<size_t>(control)]) == option;
            const auto pill = pillBounds(
                area, control, option,
                optionCounts[static_cast<size_t>(control)]);
            graphics.setColour(
                selected ? accent : juce::Colour(0xff293440));
            graphics.fillRoundedRectangle(pill, 6.0f);
            graphics.setColour(
                selected ? juce::Colour(0xff101820)
                         : juce::Colours::white.withAlpha(0.82f));
            graphics.setFont(juce::FontOptions(
                control == 1 ? 9.5f : 10.0f,
                selected ? juce::Font::bold : juce::Font::plain));
            graphics.drawText(
                formatControlValue(
                    type, control,
                    discreteValue(
                        option,
                        optionCounts[static_cast<size_t>(control)])),
                pill.toNearestInt(), juce::Justification::centred);
        }

    ContinuousTelemetrySnapshot telemetry;
    const auto hasTelemetry = readContinuousTelemetry(telemetry)
        && telemetry.valueCount >= GuitarCabinetModule::telemetryValueCount;
    const auto excursion = hasTelemetry
        ? juce::jlimit(
              0.0f, 1.0f,
              telemetry.values[GuitarCabinetModule::coneExcursion])
        : 0.0f;
    const auto compression = hasTelemetry
        ? juce::jlimit(
              0.0f, 1.0f,
              telemetry.values[GuitarCabinetModule::powerCompression])
        : 0.0f;
    const auto breakup = hasTelemetry
        ? juce::jlimit(
              0.0f, 1.0f,
              telemetry.values[GuitarCabinetModule::breakupEnergy])
        : 0.0f;
    const auto roomEnergy = hasTelemetry
        ? juce::jlimit(
              0.0f, 1.0f,
              telemetry.values[GuitarCabinetModule::roomEnergy])
        : 0.0f;

    const auto cabinet = cabinetBounds(area);
    graphics.setColour(
        juce::Colour(0xff2d211b).interpolatedWith(
            juce::Colour(0xff684a34), value(7) * 0.55f));
    graphics.fillRoundedRectangle(cabinet, 12.0f);
    graphics.setColour(accent.withAlpha(0.08f + roomEnergy * 0.24f));
    graphics.drawRoundedRectangle(
        cabinet.expanded(3.0f + roomEnergy * 8.0f), 15.0f, 2.0f);

    const auto speakerRadius =
        juce::jmin(cabinet.getWidth() * 0.22f, cabinet.getHeight() * 0.34f);
    const auto speakerCentre = juce::Point<float>(
        cabinet.getCentreX(), cabinet.getY() + cabinet.getHeight() * 0.37f);
    const auto movingRadius = speakerRadius * (1.0f + excursion * 0.055f);
    graphics.setColour(juce::Colour(0xff10151a));
    graphics.fillEllipse(
        juce::Rectangle<float>(movingRadius * 2.0f, movingRadius * 2.0f)
            .withCentre(speakerCentre));
    graphics.setColour(accent.withAlpha(0.30f + breakup * 0.60f));
    graphics.drawEllipse(
        juce::Rectangle<float>(
            movingRadius * (1.25f + breakup * 0.10f),
            movingRadius * (1.25f + breakup * 0.10f))
            .withCentre(speakerCentre),
        2.0f + breakup * 2.0f);
    graphics.setColour(
        juce::Colour(0xffb7c2cc).interpolatedWith(
            juce::Colour(0xffff9d62), compression));
    graphics.fillEllipse(
        juce::Rectangle<float>(movingRadius * 0.48f, movingRadius * 0.48f)
            .withCentre(speakerCentre));

    const auto mic = microphonePoint(area);
    graphics.setColour(juce::Colours::white.withAlpha(0.45f));
    graphics.drawLine(speakerCentre.x, speakerCentre.y, mic.x, mic.y, 1.0f);
    graphics.setColour(accent);
    graphics.fillRoundedRectangle(
        juce::Rectangle<float>(25.0f, 10.0f).withCentre(mic), 3.0f);
    graphics.setColour(juce::Colours::white);
    graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    graphics.drawText(
        formatControlValue(type, 3, value(3)) + "  -  "
            + formatControlValue(type, 4, value(4)),
        cabinet.toNearestInt(), juce::Justification::centredBottom);

    for (const auto control : trackControls)
    {
        auto track = trackBounds(area, control);
        const auto amount = value(control);
        graphics.setColour(juce::Colour(0xff202a35));
        graphics.fillRoundedRectangle(track, 5.0f);
        graphics.setColour(accent.withAlpha(0.72f));
        graphics.fillRoundedRectangle(
            track.withWidth(track.getWidth() * amount), 5.0f);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        graphics.drawText(
            juce::String(
                moduleDefinition(type)
                    .controls[static_cast<size_t>(control)].name)
                .toUpperCase(),
            track.removeFromTop(track.getHeight() * 0.48f).toNearestInt(),
            juce::Justification::centred);
        graphics.setFont(juce::FontOptions(10.5f));
        graphics.drawText(
            formatControlValue(type, control, amount),
            track.toNearestInt(), juce::Justification::centred);
    }
}
} // namespace

std::unique_ptr<ModuleView> createGuitarCabinetView(EffectGraph& graph)
{
    return std::make_unique<GuitarCabinetModuleView>(graph);
}
} // namespace megadsp::ui
