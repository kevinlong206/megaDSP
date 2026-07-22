#include "../GraphStyle.h"
#include "../ModuleView.h"
#include "../../modules/StudioFlanger.h"

#include "ModuleViewCreators.h"

#include <cmath>

namespace megadsp::ui
{
namespace
{
class StudioFlangerModuleView final : public ModuleView
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
    static juce::Rectangle<float> combBounds(
        juce::Rectangle<float>);
    static juce::Rectangle<float> railBounds(
        juce::Rectangle<float>, int);
    int hitRail(juce::Rectangle<float>, juce::Point<float>) const;
    int controlForRail(int) const;
};

void StudioFlangerModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Studio Flanger comb editor");
    component.setHelpText(
        "Choose Tape, Through-Zero, Jet, or BBD with the top model pills. "
        "Drag the comb field horizontally for Manual Delay and vertically "
        "for Depth. Comb teeth follow the DSP LFO and model crossfade. "
        "Lower rails edit synchronized motion, feedback, stereo phase, tone, "
        "mix, and output. Double-click any gesture to restore its default.");
}

juce::Rectangle<float> StudioFlangerModuleView::pillBounds(
    juce::Rectangle<float> area, int index)
{
    auto row = area.removeFromTop(31.0f);
    const auto width = row.getWidth() * 0.25f;
    return juce::Rectangle<float>(
        row.getX() + width * static_cast<float>(index), row.getY(),
        width, row.getHeight()).reduced(4.0f, 2.0f);
}

juce::Rectangle<float> StudioFlangerModuleView::combBounds(
    juce::Rectangle<float> area)
{
    area.removeFromTop(36.0f);
    area.removeFromBottom(area.getHeight() * 0.36f);
    return area.reduced(7.0f, 3.0f);
}

juce::Rectangle<float> StudioFlangerModuleView::railBounds(
    juce::Rectangle<float> area, int index)
{
    area.removeFromTop(36.0f);
    auto rails = area.removeFromBottom(area.getHeight() * 0.36f);
    const auto columns = index < 4 ? 4 : 3;
    const auto row = index < 4 ? 0 : 1;
    const auto column = index < 4 ? index : index - 4;
    const auto width = rails.getWidth() / static_cast<float>(columns);
    const auto height = rails.getHeight() * 0.5f;
    return juce::Rectangle<float>(
        rails.getX() + width * static_cast<float>(column),
        rails.getY() + height * static_cast<float>(row),
        width, height).reduced(6.0f, 5.0f);
}

int StudioFlangerModuleView::hitRail(
    juce::Rectangle<float> area, juce::Point<float> point) const
{
    for (int index = 0; index < 7; ++index)
        if (railBounds(area, index).contains(point))
            return index;
    return -1;
}

int StudioFlangerModuleView::controlForRail(int rail) const
{
    static constexpr std::array<int, 6> trailing { 2, 6, 7, 8, 9, 10 };
    return rail == 0
        ? (value(2) >= 0.5f ? 3 : 1)
        : trailing[static_cast<size_t>(juce::jlimit(0, 5, rail - 1))];
}

void StudioFlangerModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int model = 0; model < 4; ++model)
        if (pillBounds(area, model).contains(event.position))
        {
            graph.focusKeyboardControl(0);
            if (auto* target = parameter(0))
            {
                target->beginChangeGesture();
                target->setValueNotifyingHost(discreteValue(model, 4));
                target->endChangeGesture();
            }
            repaint();
            return;
        }
    if (combBounds(area).contains(event.position))
    {
        dragPrimary = 5;
        dragSecondary = 4;
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

void StudioFlangerModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (dragPrimary == 5)
    {
        const auto comb = combBounds(area);
        setValue(5, juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - comb.getX()) / comb.getWidth()));
        setValue(4, juce::jlimit(
            0.0f, 1.0f,
            (comb.getBottom() - event.position.y) / comb.getHeight()));
        dragReadout =
            "MANUAL DELAY  "
            + megadsp::formatControlValue(type, 5, value(5))
            + "  ·  DEPTH  "
            + megadsp::formatControlValue(type, 4, value(4));
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

void StudioFlangerModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int model = 0; model < 4; ++model)
        if (pillBounds(area, model).contains(event.position))
        {
            resetToDefault(0);
            return;
        }
    if (combBounds(area).contains(event.position))
    {
        resetToDefault(5);
        resetToDefault(4);
        return;
    }
    const auto rail = hitRail(area, event.position);
    if (rail >= 0)
    {
        const auto control = controlForRail(rail);
        resetToDefault(control);
    }
}

void StudioFlangerModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    constexpr std::array<const char*, 4> names {
        "TAPE", "THROUGH-ZERO", "JET", "BBD"
    };
    constexpr std::array<const char*, 4> captions {
        "moving reels", "crosses the dry plane",
        "short inverted comb", "dark bucket-brigade"
    };
    const auto selected = megadsp::discreteIndex(value(0), 4);
    ContinuousTelemetrySnapshot telemetry;
    const auto hasTelemetry =
        readContinuousTelemetry(telemetry)
        && telemetry.sequence != 0
        && telemetry.valueCount
               >= StudioFlangerModule::telemetryValueCount;
    auto renderedModel = selected;
    auto transitionAmount = 1.0f;
    if (hasTelemetry)
    {
        renderedModel = juce::jlimit(
            0, StudioFlangerModule::modelCount - 1,
            juce::roundToInt(
                telemetry.values[StudioFlangerModule::targetModel]));
        transitionAmount = telemetry.values[static_cast<size_t>(
            StudioFlangerModule::modelMix0 + renderedModel)];
    }
    for (int index = 0; index < 4; ++index)
    {
        const auto bounds = pillBounds(area, index);
        graphics.setColour(
            index == selected ? accent : juce::Colour(0xff27313d));
        graphics.fillRoundedRectangle(bounds, 7.0f);
        graphics.setColour(
            index == selected ? juce::Colour(0xff111820)
                              : juce::Colour(0xffabb6c4));
        graphics.setFont(juce::FontOptions(
            10.5f, index == selected ? juce::Font::bold
                                     : juce::Font::plain));
        graphics.drawText(
            names[static_cast<size_t>(index)], bounds.toNearestInt(),
            juce::Justification::centred);
    }

    const auto comb = combBounds(area);
    graphics.setColour(juce::Colour(0xff121923).withAlpha(0.88f));
    graphics.fillRoundedRectangle(comb, 8.0f);
    const auto delayMs = hasTelemetry
        ? telemetry.values[StudioFlangerModule::selectedDelayMs]
        : exponential(0.1f, 15.0f, value(5));
    const auto spacing = lerp(
        16.0f, 72.0f, juce::jlimit(0.0f, 1.0f, delayMs / 25.0f));
    juce::Path response;
    for (int pixel = 0; pixel <= static_cast<int>(comb.getWidth()); ++pixel)
    {
        const auto x = static_cast<float>(pixel);
        const auto magnitude = 0.5f + 0.5f * std::cos(
            juce::MathConstants<float>::twoPi * x / spacing);
        const auto shaped = renderedModel == 2
            ? std::pow(magnitude, 0.55f) : magnitude;
        const auto y = comb.getBottom()
            - (0.12f + shaped * 0.73f) * comb.getHeight();
        if (pixel == 0)
            response.startNewSubPath(comb.getX(), y);
        else
            response.lineTo(comb.getX() + x, y);
    }
    graphics.setColour(
        renderedModel == 3 ? juce::Colour(0xffffb45b) : accent);
    graphics.strokePath(response, juce::PathStrokeType(2.2f));
    const auto handle = juce::Point<float>(
        comb.getX() + value(5) * comb.getWidth(),
        comb.getBottom() - value(4) * comb.getHeight());
    graphics.setColour(juce::Colours::white);
    graphics.fillEllipse(
        juce::Rectangle<float>(10.0f, 10.0f).withCentre(handle));
    graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    graphics.drawText(
        juce::String(captions[static_cast<size_t>(renderedModel)]).toUpperCase()
            + "   " + megadsp::formatControlValue(type, 5, value(5))
            + (hasTelemetry
                   ? "  ·  DSP " + juce::String(delayMs, 2) + " ms"
                   : "  ·  CONTROL "
                       + juce::String(delayMs, 2)
                       + " ms  ·  DSP TELEMETRY WAITING")
            + (hasTelemetry && transitionAmount < 0.999f
                   ? "  ·  XFADE "
                       + juce::String(transitionAmount * 100.0f, 0) + "%"
                   : ""),
        comb.reduced(8.0f).toNearestInt(), juce::Justification::topLeft);

    static constexpr std::array<const char*, 6> trailingLabels {
        "SYNC", "FEEDBACK", "STEREO PHASE", "TONE", "MIX", "OUTPUT"
    };
    for (int rail = 0; rail < 7; ++rail)
    {
        const auto bounds = railBounds(area, rail);
        const auto control = controlForRail(rail);
        const auto label = rail == 0
            ? (control == 3 ? "DIVISION" : "RATE")
            : trailingLabels[static_cast<size_t>(rail - 1)];
        const auto valueText = control == 2
            ? juce::String(value(2) >= 0.5f ? "TEMPO" : "FREE")
            : megadsp::formatControlValue(type, control, value(control));
        graphics.setColour(juce::Colour(0xff151b24));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour(accent.withAlpha(
            control == 2 && value(control) < 0.5f ? 0.16f : 0.52f));
        graphics.fillRoundedRectangle(
            bounds.withWidth(bounds.getWidth() * value(control)), 5.0f);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        graphics.drawFittedText(
            juce::String(label) + "  " + valueText,
            bounds.reduced(4.0f, 0.0f).toNearestInt(),
            juce::Justification::centred, 1, 0.7f);
    }
}
} // namespace

std::unique_ptr<ModuleView> createStudioFlangerView(EffectGraph& graph)
{
    return std::make_unique<StudioFlangerModuleView>(graph);
}
} // namespace megadsp::ui
