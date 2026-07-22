#include "../GraphStyle.h"
#include "../ModuleView.h"
#include "../../modules/StudioPhaser.h"

#include "ModuleViewCreators.h"

#include <cmath>

namespace megadsp::ui
{
namespace
{
class StudioPhaserModuleView final : public ModuleView
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
    static juce::Rectangle<float> pillBounds(
        juce::Rectangle<float>, int);
    static juce::Rectangle<float> responseBounds(
        juce::Rectangle<float>);
    static juce::Rectangle<float> railBounds(
        juce::Rectangle<float>, int);
    int hitRail(juce::Rectangle<float>, juce::Point<float>) const;
    int controlForRail(int) const;
};

void StudioPhaserModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Studio Phaser response editor");
    component.setHelpText(
        "Choose two through twelve all-pass stages with the top pills. "
        "Drag the response field horizontally for Center and vertically for "
        "Sweep. The notches follow the DSP LFO and topology crossfade. Lower "
        "rails edit Rate or Division, Depth, regeneration, stereo, mix, and "
        "output. Double-click any gesture to restore its default.");
}

juce::Rectangle<float> StudioPhaserModuleView::pillBounds(
    juce::Rectangle<float> area, int index)
{
    auto row = area.removeFromTop(31.0f);
    const auto width = row.getWidth() / 5.0f;
    return juce::Rectangle<float>(
        row.getX() + width * static_cast<float>(index), row.getY(),
        width, row.getHeight()).reduced(3.0f, 2.0f);
}

juce::Rectangle<float> StudioPhaserModuleView::responseBounds(
    juce::Rectangle<float> area)
{
    area.removeFromTop(36.0f);
    area.removeFromBottom(area.getHeight() * 0.36f);
    return area.reduced(7.0f, 3.0f);
}

juce::Rectangle<float> StudioPhaserModuleView::railBounds(
    juce::Rectangle<float> area, int index)
{
    area.removeFromTop(36.0f);
    auto rails = area.removeFromBottom(area.getHeight() * 0.36f);
    const auto row = index / 4;
    const auto column = index % 4;
    const auto width = rails.getWidth() * 0.25f;
    const auto height = rails.getHeight() * 0.5f;
    return juce::Rectangle<float>(
        rails.getX() + width * static_cast<float>(column),
        rails.getY() + height * static_cast<float>(row),
        width, height).reduced(6.0f, 5.0f);
}

int StudioPhaserModuleView::hitRail(
    juce::Rectangle<float> area, juce::Point<float> point) const
{
    for (int index = 0; index < 7; ++index)
        if (railBounds(area, index).contains(point))
            return index;
    return -1;
}

int StudioPhaserModuleView::controlForRail(int rail) const
{
    static constexpr std::array<int, 6> trailing { 2, 4, 7, 8, 9, 10 };
    return rail == 0
        ? (value(2) >= 0.5f ? 3 : 1)
        : trailing[static_cast<size_t>(juce::jlimit(0, 5, rail - 1))];
}

void StudioPhaserModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int stage = 0; stage < 5; ++stage)
        if (pillBounds(area, stage).contains(event.position))
        {
            graph.focusKeyboardControl(0);
            if (auto* target = parameter(0))
            {
                target->beginChangeGesture();
                target->setValueNotifyingHost(discreteValue(stage, 5));
                target->endChangeGesture();
            }
            repaint();
            return;
        }

    if (responseBounds(area).contains(event.position))
    {
        dragPrimary = 5;
        dragSecondary = 6;
    }
    else
    {
        const auto rail = hitRail(area, event.position);
        if (rail >= 0)
        {
            const auto control = controlForRail(rail);
            if (control == 2)
            {
                graph.focusKeyboardControl(control);
                toggleParameter(control);
                repaint();
                return;
            }
            if (control == 3)
            {
                graph.focusKeyboardControl(control);
                cycleChoice(control, 8);
                repaint();
                return;
            }
            dragPrimary = control;
        }
    }
    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void StudioPhaserModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (dragPrimary == 5)
    {
        const auto response = responseBounds(area);
        setValue(5, juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - response.getX()) / response.getWidth()));
        setValue(6, juce::jlimit(
            0.0f, 1.0f,
            (response.getBottom() - event.position.y) / response.getHeight()));
        dragReadout =
            "CENTER  " + megadsp::formatControlValue(type, 5, value(5))
            + "  ·  SWEEP  "
            + megadsp::formatControlValue(type, 6, value(6));
        repaint();
        return;
    }
    int rail = 0;
    for (; rail < 7; ++rail)
        if (controlForRail(rail) == dragPrimary)
            break;
    const auto bounds = railBounds(area, juce::jlimit(0, 6, rail));
    setValue(dragPrimary, juce::jlimit(
        0.0f, 1.0f,
        (event.position.x - bounds.getX()) / bounds.getWidth()));
    updateDefaultDragReadout();
}

void StudioPhaserModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int stage = 0; stage < 5; ++stage)
        if (pillBounds(area, stage).contains(event.position))
        {
            resetToDefault(0);
            return;
        }
    if (responseBounds(area).contains(event.position))
    {
        resetToDefault(5);
        resetToDefault(6);
        return;
    }
    const auto rail = hitRail(area, event.position);
    if (rail >= 0)
    {
        const auto control = controlForRail(rail);
        resetToDefault(control);
    }
}

void StudioPhaserModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    constexpr std::array<const char*, 5> names {
        "2 STAGES", "4 STAGES", "6 STAGES", "8 STAGES", "12 STAGES"
    };
    const auto selected = megadsp::discreteIndex(value(0), 5);
    ContinuousTelemetrySnapshot telemetry;
    const auto hasTelemetry =
        readContinuousTelemetry(telemetry)
        && telemetry.sequence != 0
        && telemetry.valueCount
               >= StudioPhaserModule::telemetryValueCount;
    auto renderedTopology = selected;
    auto transitionAmount = 1.0f;
    if (hasTelemetry)
    {
        renderedTopology = juce::jlimit(
            0, StudioPhaserModule::topologyCount - 1,
            juce::roundToInt(
                telemetry.values[StudioPhaserModule::targetTopology]));
        transitionAmount = telemetry.values[static_cast<size_t>(
            StudioPhaserModule::topologyMix0 + renderedTopology)];
    }
    for (int index = 0; index < 5; ++index)
    {
        const auto bounds = pillBounds(area, index);
        graphics.setColour(
            index == selected ? accent : juce::Colour(0xff27313d));
        graphics.fillRoundedRectangle(bounds, 7.0f);
        graphics.setColour(
            index == selected ? juce::Colour(0xff111820)
                              : juce::Colour(0xffabb6c4));
        graphics.setFont(juce::FontOptions(
            10.0f, index == selected ? juce::Font::bold
                                     : juce::Font::plain));
        graphics.drawText(
            names[static_cast<size_t>(index)], bounds.toNearestInt(),
            juce::Justification::centred);
    }

    const auto response = responseBounds(area);
    graphics.setColour(juce::Colour(0xff121923).withAlpha(0.88f));
    graphics.fillRoundedRectangle(response, 8.0f);
    const auto stageCount =
        std::array<int, 5> { 2, 4, 6, 8, 12 }[
            static_cast<size_t>(renderedTopology)];
    const auto leftLfo = hasTelemetry
        ? std::sin(juce::MathConstants<float>::twoPi
                   * telemetry.values[StudioPhaserModule::leftPhase])
        : 0.0f;
    const auto rightLfo = hasTelemetry
        ? std::sin(juce::MathConstants<float>::twoPi
                   * telemetry.values[StudioPhaserModule::rightPhase])
        : leftLfo;
    const auto centreX = response.getX() + value(5) * response.getWidth();
    const auto depth = value(4);
    const auto sweep = value(6);
    auto drawResponse = [&](float modulation, juce::Colour colour)
    {
        juce::Path path;
        for (int pixel = 0; pixel <= static_cast<int>(response.getWidth());
             ++pixel)
        {
            const auto normalized =
                static_cast<float>(pixel) / response.getWidth();
            auto magnitude = 1.0f;
            for (int notch = 0; notch < stageCount / 2; ++notch)
            {
                const auto spread = stageCount > 2
                    ? (static_cast<float>(notch)
                           / static_cast<float>(stageCount / 2 - 1) - 0.5f)
                    : 0.0f;
                const auto moving = value(5)
                    + (0.04f + 0.22f * sweep) * spread
                    + (0.03f + 0.18f * sweep) * depth
                       * modulation;
                const auto distance = (normalized - moving)
                    / (0.018f + 0.018f / static_cast<float>(stageCount));
                magnitude *= 1.0f - 0.72f * std::exp(-distance * distance);
            }
            const auto y = response.getBottom()
                - (0.12f + 0.78f * magnitude) * response.getHeight();
            if (pixel == 0)
                path.startNewSubPath(response.getX(), y);
            else
                path.lineTo(response.getX() + static_cast<float>(pixel), y);
        }
        graphics.setColour(colour);
        graphics.strokePath(path, juce::PathStrokeType(2.2f));
    };
    drawResponse(leftLfo, accent);
    drawResponse(rightLfo, outputColour.withAlpha(0.72f));
    graphics.setColour(juce::Colours::white.withAlpha(0.9f));
    graphics.fillEllipse(
        centreX - 5.0f,
        response.getBottom() - sweep * response.getHeight() - 5.0f,
        10.0f, 10.0f);
    graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    graphics.drawText(
        "NOTCH FIELD   "
            + megadsp::formatControlValue(type, 5, value(5))
            + "  ·  " + megadsp::formatControlValue(type, 6, value(6))
            + (!hasTelemetry
                   ? "  ·  DSP TELEMETRY WAITING"
                   : transitionAmount < 0.999f
                       ? "  ·  DSP XFADE "
                           + juce::String(transitionAmount * 100.0f, 0) + "%"
                       : ""),
        response.reduced(8.0f).toNearestInt(),
        juce::Justification::topLeft);

    static constexpr std::array<const char*, 6> trailingLabels {
        "SYNC", "DEPTH", "FEEDBACK", "STEREO PHASE", "MIX", "OUTPUT"
    };
    for (int rail = 0; rail < 7; ++rail)
    {
        const auto bounds = railBounds(area, rail);
        const auto control = controlForRail(rail);
        const auto label = rail == 0
            ? (control == 3 ? "DIVISION" : "RATE")
            : trailingLabels[static_cast<size_t>(rail - 1)];
        const auto amount = value(control);
        const auto valueText = control == 2
            ? juce::String(value(2) >= 0.5f ? "TEMPO" : "FREE")
            : megadsp::formatControlValue(type, control, amount);
        graphics.setColour(juce::Colour(0xff151b24));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour(accent.withAlpha(
            control == 2 && amount < 0.5f ? 0.16f : 0.52f));
        graphics.fillRoundedRectangle(
            bounds.withWidth(bounds.getWidth() * amount), 5.0f);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        graphics.drawFittedText(
            juce::String(label) + "  " + valueText,
            bounds.reduced(4.0f, 0.0f).toNearestInt(),
            juce::Justification::centred, 1, 0.7f);
    }
}
} // namespace

std::unique_ptr<ModuleView> createStudioPhaserView(EffectGraph& graph)
{
    return std::make_unique<StudioPhaserModuleView>(graph);
}
} // namespace megadsp::ui
