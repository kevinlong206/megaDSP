#include "../GraphStyle.h"
#include "../ModuleView.h"

#include "ModuleViewCreators.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace megadsp::ui
{
namespace
{
class RotarySpeakerModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    void configureAccessibility(juce::Component&) const override;
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    static juce::Rectangle<float> speedPillBounds(
        juce::Rectangle<float>, int);
    static juce::Rectangle<float> rotorBounds(juce::Rectangle<float>);
    static juce::Rectangle<float> balanceBounds(juce::Rectangle<float>);
    static juce::Rectangle<float> motionBounds(juce::Rectangle<float>);
    static juce::Rectangle<float> spinUpBounds(juce::Rectangle<float>);
    juce::Point<float> microphonePoint(
        juce::Rectangle<float>, int channel) const;
};

void RotarySpeakerModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Rotary Speaker rotor and microphone editor");
    component.setHelpText(
        "Choose Brake, Chorale, or Tremolo with the dominant Speed pills. "
        "Drag Rotor Balance horizontally, Motion and Spin-up vertically, "
        "or drag either microphone in two dimensions for exact distance "
        "and spread. Double-click a gesture to restore its default.");
}

juce::Rectangle<float> RotarySpeakerModuleView::speedPillBounds(
    juce::Rectangle<float> area, int index)
{
    auto pills = area.removeFromTop(31.0f);
    const auto width = pills.getWidth() / 3.0f;
    return juce::Rectangle<float>(
        pills.getX() + static_cast<float>(index) * width,
        pills.getY(), width, pills.getHeight()).reduced(3.0f, 2.0f);
}

juce::Rectangle<float> RotarySpeakerModuleView::rotorBounds(
    juce::Rectangle<float> area)
{
    area.removeFromTop(35.0f);
    area.removeFromBottom(34.0f);
    area.removeFromLeft(28.0f);
    area.removeFromRight(28.0f);
    return area;
}

juce::Rectangle<float> RotarySpeakerModuleView::balanceBounds(
    juce::Rectangle<float> area)
{
    return area.removeFromBottom(27.0f).reduced(5.0f, 2.0f);
}

juce::Rectangle<float> RotarySpeakerModuleView::motionBounds(
    juce::Rectangle<float> area)
{
    area.removeFromTop(39.0f);
    area.removeFromBottom(38.0f);
    return area.removeFromLeft(22.0f).reduced(2.0f);
}

juce::Rectangle<float> RotarySpeakerModuleView::spinUpBounds(
    juce::Rectangle<float> area)
{
    area.removeFromTop(39.0f);
    area.removeFromBottom(38.0f);
    return area.removeFromRight(22.0f).reduced(2.0f);
}

juce::Point<float> RotarySpeakerModuleView::microphonePoint(
    juce::Rectangle<float> area, int channel) const
{
    const auto rotor = rotorBounds(area);
    const auto centre = rotor.getCentre();
    const auto radius =
        juce::jmin(rotor.getWidth(), rotor.getHeight()) * 0.35f;
    const auto distance = radius * lerp(1.08f, 1.55f, value(5));
    const auto spread = juce::degreesToRadians(value(6) * 180.0f);
    const auto angle = -juce::MathConstants<float>::halfPi
        + (channel == 0 ? -0.5f : 0.5f) * spread;
    return centre
           + juce::Point<float>(std::cos(angle), std::sin(angle))
                 * distance;
}

void RotarySpeakerModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int speed = 0; speed < 3; ++speed)
        if (speedPillBounds(area, speed).contains(event.position))
        {
            if (auto* target = parameter(0))
            {
                target->beginChangeGesture();
                target->setValueNotifyingHost(discreteValue(speed, 3));
                target->endChangeGesture();
            }
            repaint();
            return;
        }

    if (balanceBounds(area).contains(event.position))
        dragPrimary = 2;
    else if (motionBounds(area).contains(event.position))
        dragPrimary = 4;
    else if (spinUpBounds(area).contains(event.position))
        dragPrimary = 7;
    else if (event.position.getDistanceFrom(microphonePoint(area, 0))
                 <= 20.0f
             || event.position.getDistanceFrom(microphonePoint(area, 1))
                    <= 20.0f)
    {
        dragPrimary = 5;
        dragSecondary = 6;
    }
    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void RotarySpeakerModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (dragPrimary == 2)
    {
        const auto balance = balanceBounds(area);
        setValue(2, juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - balance.getX()) / balance.getWidth()));
    }
    else if (dragPrimary == 4 || dragPrimary == 7)
    {
        const auto rail =
            dragPrimary == 4 ? motionBounds(area) : spinUpBounds(area);
        setValue(dragPrimary, juce::jlimit(
            0.0f, 1.0f,
            (rail.getBottom() - event.position.y) / rail.getHeight()));
    }
    else
    {
        const auto rotor = rotorBounds(area);
        const auto centre = rotor.getCentre();
        const auto radius =
            juce::jmin(rotor.getWidth(), rotor.getHeight()) * 0.35f;
        const auto vector = event.position - centre;
        const auto distance = vector.getDistanceFromOrigin()
                              / juce::jmax(1.0f, radius);
        setValue(5, juce::jlimit(
            0.0f, 1.0f, (distance - 1.08f) / (1.55f - 1.08f)));
        const auto angle = std::atan2(vector.y, vector.x);
        const auto spread = 2.0f
            * std::abs(angle + juce::MathConstants<float>::halfPi);
        setValue(6, juce::jlimit(
            0.0f, 1.0f, spread / juce::MathConstants<float>::pi));
    }

    if (dragPrimary == 5)
        dragReadout =
            "MICROPHONES  "
            + megadsp::formatControlValue(type, 5, value(5))
            + "  ·  "
            + megadsp::formatControlValue(type, 6, value(6));
    else
        updateDefaultDragReadout();
    repaint();
}

void RotarySpeakerModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto defaults = moduleDefaults(type);
    for (int speed = 0; speed < 3; ++speed)
        if (speedPillBounds(area, speed).contains(event.position))
        {
            setValue(0, defaults[0]);
            repaint();
            return;
        }
    if (balanceBounds(area).contains(event.position))
        setValue(2, defaults[2]);
    else if (motionBounds(area).contains(event.position))
        setValue(4, defaults[4]);
    else if (spinUpBounds(area).contains(event.position))
        setValue(7, defaults[7]);
    else if (event.position.getDistanceFrom(microphonePoint(area, 0))
                 <= 22.0f
             || event.position.getDistanceFrom(microphonePoint(area, 1))
                    <= 22.0f)
    {
        setValue(5, defaults[5]);
        setValue(6, defaults[6]);
    }
    else
        return;
    repaint();
}

void RotarySpeakerModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    constexpr std::array<const char*, 3> speedNames {
        "BRAKE", "CHORALE", "TREMOLO"
    };
    const auto speed = megadsp::discreteIndex(value(0), 3);
    for (int pill = 0; pill < 3; ++pill)
    {
        const auto bounds = speedPillBounds(area, pill);
        graphics.setColour(
            pill == speed ? accent : juce::Colour(0xff27313d));
        graphics.fillRoundedRectangle(bounds, 7.0f);
        graphics.setColour(
            pill == speed ? juce::Colour(0xff111820)
                          : juce::Colour(0xffabb6c4));
        graphics.setFont(juce::FontOptions(
            11.0f, pill == speed ? juce::Font::bold : juce::Font::plain));
        graphics.drawText(
            speedNames[static_cast<size_t>(pill)],
            bounds.toNearestInt(), juce::Justification::centred);
    }

    const auto rotor = rotorBounds(area);
    const auto centre = rotor.getCentre();
    const auto cabinetRadius =
        juce::jmin(rotor.getWidth(), rotor.getHeight()) * 0.35f;
    const auto time = static_cast<float>(
        juce::Time::getMillisecondCounterHiRes() * 0.001);
    const auto hornHz = speed == 0 ? 0.0f : speed == 1 ? 0.8f : 6.67f;
    const auto drumHz = speed == 0 ? 0.0f : speed == 1 ? 0.67f : 5.67f;
    const auto balance = value(2);
    const auto motion = value(4);
    const auto spinUp = value(7);

    graphics.setColour(
        juce::Colour(0xff2d211c).interpolatedWith(
            juce::Colour(0xff674832), value(8)));
    graphics.fillRoundedRectangle(
        juce::Rectangle<float>(
            cabinetRadius * 2.0f, cabinetRadius * 2.0f)
            .withCentre(centre),
        12.0f);
    graphics.setColour(
        juce::Colour(0xff74513b).withAlpha(0.55f + value(9) * 0.35f));
    graphics.drawRoundedRectangle(
        juce::Rectangle<float>(
            cabinetRadius * 2.0f, cabinetRadius * 2.0f)
            .withCentre(centre)
            .expanded(value(9) * 5.0f),
        12.0f, 2.0f);

    auto drawRotor = [&](float radius, float angle, juce::Colour colour,
                         bool horn)
    {
        const auto emphasis =
            horn ? std::sqrt(balance) : std::sqrt(1.0f - balance);
        graphics.setColour(
            colour.withAlpha(0.10f + 0.24f * emphasis));
        graphics.fillEllipse(
            juce::Rectangle<float>(radius * 2.0f, radius * 2.0f)
                .withCentre(centre));

        const auto trailLength = lerp(0.12f, 1.2f, spinUp);
        juce::Path trail;
        trail.addCentredArc(
            centre.x, centre.y, radius, radius, 0.0f,
            angle - trailLength, angle, true);
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
            centre.y + direction.y * movingRadius - 5.0f,
            10.0f, 10.0f);
    };
    drawRotor(
        cabinetRadius * 0.42f,
        time * hornHz * juce::MathConstants<float>::twoPi,
        accent, true);
    drawRotor(
        cabinetRadius * 0.72f,
        time * drumHz * juce::MathConstants<float>::twoPi
            + juce::MathConstants<float>::pi,
        juce::Colour(0xff6f8cff), false);

    for (int channel = 0; channel < 2; ++channel)
    {
        const auto microphone = microphonePoint(area, channel);
        graphics.setColour(channel == 0 ? accent : outputColour);
        graphics.fillEllipse(
            juce::Rectangle<float>(13.0f, 13.0f)
                .withCentre(microphone));
        graphics.drawLine(
            juce::Line<float>(microphone, centre), 1.0f);
    }
    graphics.setColour(juce::Colours::white);
    graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    graphics.drawText(
        "MICS  " + megadsp::formatControlValue(type, 5, value(5))
            + "  ·  "
            + megadsp::formatControlValue(type, 6, value(6)),
        rotor.toNearestInt(), juce::Justification::bottomRight);

    const auto motionRail = motionBounds(area);
    const auto spinRail = spinUpBounds(area);
    auto drawVerticalRail = [&](juce::Rectangle<float> rail, float amount,
                                const juce::String& title,
                                const juce::String& readout)
    {
        graphics.setColour(juce::Colour(0xff151b24));
        graphics.fillRoundedRectangle(rail, 4.0f);
        auto fill = rail;
        fill.setY(rail.getBottom() - rail.getHeight() * amount);
        graphics.setColour(accent.withAlpha(0.68f));
        graphics.fillRoundedRectangle(fill, 4.0f);
        const auto y = rail.getBottom() - rail.getHeight() * amount;
        graphics.setColour(juce::Colours::white);
        graphics.fillEllipse(
            rail.getCentreX() - 4.0f, y - 4.0f, 8.0f, 8.0f);
        graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        graphics.drawFittedText(
            title + "\n" + readout, rail.toNearestInt().expanded(3, 0),
            juce::Justification::centred, 2);
    };
    drawVerticalRail(
        motionRail, motion, "MOTION",
        megadsp::formatControlValue(type, 4, motion));
    drawVerticalRail(
        spinRail, spinUp, "SPIN-UP",
        megadsp::formatControlValue(type, 7, spinUp));

    const auto balanceRail = balanceBounds(area);
    graphics.setColour(juce::Colour(0xff151b24));
    graphics.fillRoundedRectangle(balanceRail, 5.0f);
    const auto balanceX =
        balanceRail.getX() + balance * balanceRail.getWidth();
    graphics.setColour(juce::Colour(0xff6f8cff).withAlpha(0.42f));
    graphics.fillRoundedRectangle(balanceRail.withRight(balanceX), 5.0f);
    graphics.setColour(accent.withAlpha(0.42f));
    graphics.fillRoundedRectangle(balanceRail.withLeft(balanceX), 5.0f);
    graphics.setColour(juce::Colours::white);
    graphics.fillEllipse(
        balanceX - 5.0f, balanceRail.getCentreY() - 5.0f,
        10.0f, 10.0f);
    graphics.drawText(
        "DRUM  ←  ROTOR BALANCE  →  HORN     "
            + megadsp::formatControlValue(type, 2, balance),
        balanceRail.toNearestInt(), juce::Justification::centred);
}
} // namespace

std::unique_ptr<ModuleView> createRotarySpeakerView(EffectGraph& graph)
{
    return std::make_unique<RotarySpeakerModuleView>(graph);
}
} // namespace megadsp::ui
