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
class RotarySpeakerModuleView final : public ModuleView
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
    static juce::Rectangle<float> controlsBounds(juce::Rectangle<float>);
    static juce::Rectangle<float> animationBounds(juce::Rectangle<float>);
    static juce::Rectangle<float> speedPillBounds(
        juce::Rectangle<float>, int);
    static juce::Rectangle<float> controlBounds(
        juce::Rectangle<float>, int);
    static juce::Rectangle<float> groupBounds(
        juce::Rectangle<float>, int);
    int hitControl(juce::Rectangle<float>, juce::Point<float>) const;
    juce::Point<float> microphonePoint(
        juce::Rectangle<float>, int channel) const;
};

void RotarySpeakerModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Rotary Speaker full panel editor");
    component.setHelpText(
        "The left panel directly edits all twelve Rotary Speaker controls. "
        "Choose Brake, Chorale, or Tremolo with the Speed selector, then drag "
        "the grouped Rotor, Microphones, Character, and Output tracks. The "
        "unobstructed right panel visualizes the cabinet, rotors, and "
        "microphones. Double-click any control to restore its default.");
}

juce::Rectangle<float> RotarySpeakerModuleView::controlsBounds(
    juce::Rectangle<float> area)
{
    return area.removeFromLeft(area.getWidth() * 0.59f).reduced(0.0f, 1.0f);
}

juce::Rectangle<float> RotarySpeakerModuleView::animationBounds(
    juce::Rectangle<float> area)
{
    area.removeFromLeft(area.getWidth() * 0.59f);
    return area.reduced(10.0f, 3.0f);
}

juce::Rectangle<float> RotarySpeakerModuleView::speedPillBounds(
    juce::Rectangle<float> area, int index)
{
    auto row = controlsBounds(area).removeFromTop(35.0f);
    row.removeFromLeft(48.0f);
    const auto width = row.getWidth() / 3.0f;
    return juce::Rectangle<float>(
               row.getX() + width * static_cast<float>(index),
               row.getY(), width, row.getHeight())
        .reduced(3.0f, 4.0f);
}

juce::Rectangle<float> RotarySpeakerModuleView::groupBounds(
    juce::Rectangle<float> area, int group)
{
    auto controls = controlsBounds(area);
    controls.removeFromTop(38.0f);
    const auto remaining = controls.getHeight();
    const std::array<float, 4> heights {
        remaining * 0.34f, remaining * 0.22f,
        remaining * 0.22f, remaining * 0.22f
    };
    for (int index = 0; index < group; ++index)
        controls.removeFromTop(heights[static_cast<size_t>(index)]);
    return controls.removeFromTop(heights[static_cast<size_t>(group)]);
}

juce::Rectangle<float> RotarySpeakerModuleView::controlBounds(
    juce::Rectangle<float> area, int control)
{
    int group = 0;
    int item = 0;
    int columns = 2;
    if (control == 2 || control == 3 || control == 4 || control == 7)
    {
        constexpr std::array<int, 4> rotorControls { 2, 3, 4, 7 };
        item = static_cast<int>(std::find(
            rotorControls.begin(), rotorControls.end(), control)
                                - rotorControls.begin());
    }
    else if (control == 5 || control == 6)
    {
        group = 1;
        item = control - 5;
    }
    else if (control == 1 || control == 8 || control == 9)
    {
        group = 2;
        columns = 3;
        item = control == 1 ? 0 : control - 7;
    }
    else
    {
        group = 3;
        item = control - 10;
    }

    auto bounds = groupBounds(area, group);
    bounds.removeFromTop(15.0f);
    const auto rows = group == 0 ? 2 : 1;
    const auto width = bounds.getWidth() / static_cast<float>(columns);
    const auto height = bounds.getHeight() / static_cast<float>(rows);
    return juce::Rectangle<float>(
               bounds.getX() + width * static_cast<float>(item % columns),
               bounds.getY() + height * static_cast<float>(item / columns),
               width, height)
        .reduced(4.0f, 3.0f);
}

int RotarySpeakerModuleView::hitControl(
    juce::Rectangle<float> area, juce::Point<float> point) const
{
    for (int control = 1; control < 12; ++control)
        if (controlBounds(area, control).contains(point))
            return control;
    return -1;
}

juce::Point<float> RotarySpeakerModuleView::microphonePoint(
    juce::Rectangle<float> area, int channel) const
{
    const auto animation = animationBounds(area);
    const auto centre = animation.getCentre();
    const auto radius =
        juce::jmin(animation.getWidth(), animation.getHeight()) * 0.31f;
    const auto distance = radius * lerp(1.08f, 1.55f, value(5));
    const auto spread = juce::degreesToRadians(value(6) * 180.0f);
    const auto angle = -juce::MathConstants<float>::halfPi
        + (channel == 0 ? -0.5f : 0.5f) * spread;
    return centre
           + juce::Point<float>(std::cos(angle), std::sin(angle)) * distance;
}

void RotarySpeakerModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int speed = 0; speed < 3; ++speed)
        if (speedPillBounds(area, speed).contains(event.position))
        {
            graph.focusKeyboardControl(0);
            if (auto* target = parameter(0))
            {
                target->beginChangeGesture();
                target->setValueNotifyingHost(discreteValue(speed, 3));
                target->endChangeGesture();
            }
            repaint();
            return;
        }

    dragPrimary = hitControl(area, event.position);
    if (dragPrimary < 0)
        return;
    graph.focusKeyboardControl(dragPrimary);
    beginGestures();
    mouseDrag(event);
}

void RotarySpeakerModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary <= 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto bounds = controlBounds(area, dragPrimary);
    setValue(dragPrimary, juce::jlimit(
        0.0f, 1.0f,
        (event.position.x - bounds.getX()) / bounds.getWidth()));
    updateDefaultDragReadout();
}

void RotarySpeakerModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int speed = 0; speed < 3; ++speed)
        if (speedPillBounds(area, speed).contains(event.position))
        {
            graph.focusKeyboardControl(0);
            resetToDefault(0);
            return;
        }
    const auto control = hitControl(area, event.position);
    if (control >= 0)
    {
        graph.focusKeyboardControl(control);
        resetToDefault(control);
    }
}

void RotarySpeakerModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    constexpr std::array<const char*, 3> speedNames {
        "BRAKE", "CHORALE", "TREMOLO"
    };
    constexpr std::array<const char*, 4> groupNames {
        "ROTOR", "MICROPHONES", "CHARACTER", "OUTPUT"
    };
    constexpr std::array<const char*, 12> controlNames {
        "SPEED", "DRIVE", "BALANCE", "CROSSOVER", "MOTION", "DISTANCE",
        "SPREAD", "SPIN-UP", "CABINET COLOR", "AMBIENCE", "MIX", "OUTPUT"
    };

    auto controls = controlsBounds(area);
    graphics.setColour(juce::Colour(0x9a111821));
    graphics.fillRoundedRectangle(controls, 9.0f);
    graphics.setColour(juce::Colour(0xff9ba8b8));
    graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    graphics.drawText(
        "SPEED", controls.removeFromTop(35.0f).removeFromLeft(48.0f)
                     .toNearestInt(),
        juce::Justification::centredLeft);

    const auto speed = megadsp::discreteIndex(value(0), 3);
    for (int pill = 0; pill < 3; ++pill)
    {
        const auto bounds = speedPillBounds(area, pill);
        graphics.setColour(
            pill == speed ? accent : juce::Colour(0xff27313d));
        graphics.fillRoundedRectangle(bounds, 6.0f);
        graphics.setColour(
            pill == speed ? juce::Colour(0xff111820)
                          : juce::Colour(0xffabb6c4));
        graphics.drawText(
            speedNames[static_cast<size_t>(pill)],
            bounds.toNearestInt(), juce::Justification::centred);
    }

    for (int group = 0; group < 4; ++group)
    {
        auto bounds = groupBounds(area, group);
        graphics.setColour(juce::Colour(0xff718095));
        graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        graphics.drawText(
            groupNames[static_cast<size_t>(group)],
            bounds.removeFromTop(14.0f).toNearestInt(),
            juce::Justification::centredLeft);
    }

    for (int control = 1; control < 12; ++control)
    {
        auto bounds = controlBounds(area, control);
        const auto amount = value(control);
        auto track = bounds.removeFromBottom(8.0f);
        graphics.setColour(juce::Colour(0xff18212c));
        graphics.fillRoundedRectangle(track, 4.0f);
        graphics.setColour(accent.withAlpha(0.62f));
        graphics.fillRoundedRectangle(
            track.withWidth(track.getWidth() * amount), 4.0f);
        const auto x = track.getX() + track.getWidth() * amount;
        graphics.setColour(juce::Colours::white);
        graphics.fillEllipse(
            x - 3.5f, track.getCentreY() - 3.5f, 7.0f, 7.0f);

        graphics.setFont(juce::FontOptions(8.5f, juce::Font::bold));
        graphics.setColour(juce::Colour(0xffc5ced9));
        graphics.drawFittedText(
            controlNames[static_cast<size_t>(control)],
            bounds.removeFromLeft(bounds.getWidth() * 0.55f).toNearestInt(),
            juce::Justification::centredLeft, 1);
        graphics.setColour(juce::Colours::white);
        graphics.drawFittedText(
            megadsp::formatControlValue(type, control, amount),
            bounds.toNearestInt(), juce::Justification::centredRight, 1);
    }

    const auto animation = animationBounds(area);
    const auto centre = animation.getCentre();
    const auto cabinetRadius =
        juce::jmin(animation.getWidth(), animation.getHeight()) * 0.31f;
    const auto time = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const auto hornHz = speed == 0 ? 0.0f : speed == 1 ? 0.8f : 6.67f;
    const auto drumHz = speed == 0 ? 0.0f : speed == 1 ? 0.67f : 5.67f;
    const auto rotorAngle = [time](double frequency, double offset = 0.0)
    {
        return static_cast<float>(std::remainder(
            time * frequency * juce::MathConstants<double>::twoPi + offset,
            juce::MathConstants<double>::twoPi));
    };
    const auto balance = value(2);
    const auto motion = value(4);
    const auto spinUp = value(7);

    graphics.setColour(juce::Colour(0x60151c25));
    graphics.fillRoundedRectangle(animation, 12.0f);
    graphics.setColour(
        juce::Colour(0xff2d211c).interpolatedWith(
            juce::Colour(0xff674832), value(8)));
    graphics.fillRoundedRectangle(
        juce::Rectangle<float>(cabinetRadius * 2.0f, cabinetRadius * 2.0f)
            .withCentre(centre),
        12.0f);
    graphics.setColour(
        juce::Colour(0xff74513b).withAlpha(0.55f + value(9) * 0.35f));
    graphics.drawRoundedRectangle(
        juce::Rectangle<float>(cabinetRadius * 2.0f, cabinetRadius * 2.0f)
            .withCentre(centre)
            .expanded(value(9) * 5.0f),
        12.0f, 2.0f);

    auto drawRotor = [&](float radius, float angle, juce::Colour colour,
                         bool horn)
    {
        const auto emphasis =
            horn ? std::sqrt(balance) : std::sqrt(1.0f - balance);
        graphics.setColour(colour.withAlpha(0.10f + 0.24f * emphasis));
        graphics.fillEllipse(
            juce::Rectangle<float>(radius * 2.0f, radius * 2.0f)
                .withCentre(centre));
        juce::Path trail;
        trail.addCentredArc(
            centre.x, centre.y, radius, radius, 0.0f,
            angle - lerp(0.12f, 1.2f, spinUp), angle, true);
        graphics.setColour(colour.withAlpha(0.16f + emphasis * 0.30f));
        graphics.strokePath(
            trail, juce::PathStrokeType(horn ? 3.0f : 5.0f));
        const auto direction =
            juce::Point<float>(std::cos(angle), std::sin(angle));
        const auto movingRadius = radius * lerp(0.74f, 1.0f, motion);
        graphics.setColour(colour.withAlpha(0.45f + emphasis * 0.55f));
        graphics.drawLine(
            centre.x, centre.y,
            centre.x + direction.x * movingRadius,
            centre.y + direction.y * movingRadius,
            horn ? 5.0f : 8.0f);
        graphics.fillEllipse(
            centre.x + direction.x * movingRadius - 5.0f,
            centre.y + direction.y * movingRadius - 5.0f, 10.0f, 10.0f);
    };
    drawRotor(
        cabinetRadius * 0.42f, rotorAngle(hornHz), accent, true);
    drawRotor(
        cabinetRadius * 0.72f,
        rotorAngle(drumHz, juce::MathConstants<double>::pi),
        juce::Colour(0xff6f8cff), false);

    for (int channel = 0; channel < 2; ++channel)
    {
        const auto microphone = microphonePoint(area, channel);
        graphics.setColour(channel == 0 ? accent : outputColour);
        graphics.fillEllipse(
            juce::Rectangle<float>(13.0f, 13.0f).withCentre(microphone));
        graphics.drawLine(juce::Line<float>(microphone, centre), 1.0f);
    }
}
} // namespace

std::unique_ptr<ModuleView> createRotarySpeakerView(EffectGraph& graph)
{
    return std::make_unique<RotarySpeakerModuleView>(graph);
}
} // namespace megadsp::ui
