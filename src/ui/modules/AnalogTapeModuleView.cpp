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
constexpr std::array<int, 10> tapeTrackOrder {
    1, 2, 3, 5, 6, 7, 8, 9, 10, 11
};
constexpr std::array<const char*, 4> tapeSpeedLabels {
    "3.75", "7.5", "15", "30"
};

class AnalogTapeModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    void configureAccessibility(juce::Component&) const override;
    bool usesFullPanel() const override { return true; }
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void timerCallback() override;

private:
    static juce::Rectangle<float> machinePillBounds(
        juce::Rectangle<float>, int machine);
    static juce::Rectangle<float> displayBounds(juce::Rectangle<float>);
    static juce::Rectangle<float> speedSegmentBounds(
        juce::Rectangle<float>, int index);
    static juce::Rectangle<float> reelBounds(juce::Rectangle<float>);
    static juce::Rectangle<float> trackBounds(
        juce::Rectangle<float>, int control);

    float reelAngle = 0.0f;
};

void AnalogTapeModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Analog Tape transport");
    component.setHelpText(
        "Choose a machine with the top pills. The reel graphic shows "
        "transport motion at the selected Tape Speed; drag the speed "
        "strip beneath it to change speed. Input, Drive, Bias, Head "
        "Bump, Wow, Flutter, Wear, Noise, Mix, and Output use the "
        "tracks below; drag any track and double-click any control "
        "to restore its default.");
}

juce::Rectangle<float> AnalogTapeModuleView::machinePillBounds(
    juce::Rectangle<float> area, int machine)
{
    auto pills = area.removeFromTop(32.0f);
    const auto width = pills.getWidth() * 0.25f;
    return juce::Rectangle<float>(
        pills.getX() + width * static_cast<float>(machine),
        pills.getY(), width, pills.getHeight()).reduced(4.0f, 2.0f);
}

juce::Rectangle<float> AnalogTapeModuleView::displayBounds(
    juce::Rectangle<float> area)
{
    area.removeFromTop(38.0f);
    area.removeFromBottom(area.getHeight() * 0.5f);
    return area.reduced(5.0f, 2.0f);
}

juce::Rectangle<float> AnalogTapeModuleView::speedSegmentBounds(
    juce::Rectangle<float> area, int index)
{
    auto display = displayBounds(area);
    auto strip = display.removeFromBottom(22.0f);
    const auto width = strip.getWidth() / 4.0f;
    return juce::Rectangle<float>(
        strip.getX() + width * static_cast<float>(index),
        strip.getY(), width, strip.getHeight()).reduced(2.0f, 1.0f);
}

juce::Rectangle<float> AnalogTapeModuleView::reelBounds(
    juce::Rectangle<float> area)
{
    auto display = displayBounds(area);
    display.removeFromBottom(24.0f);
    return display.reduced(4.0f);
}

juce::Rectangle<float> AnalogTapeModuleView::trackBounds(
    juce::Rectangle<float> area, int control)
{
    area.removeFromTop(38.0f);
    auto grid = area.removeFromBottom(area.getHeight() * 0.5f);
    const auto iterator = std::find(
        tapeTrackOrder.begin(), tapeTrackOrder.end(), control);
    if (iterator == tapeTrackOrder.end())
        return {};
    const auto position = static_cast<int>(
        std::distance(tapeTrackOrder.begin(), iterator));
    const auto row = position < 5 ? 0 : 1;
    const auto column = row == 0 ? position : position - 5;
    const auto width = grid.getWidth() / 5.0f;
    const auto height = grid.getHeight() * 0.5f;
    return juce::Rectangle<float>(
        grid.getX() + width * static_cast<float>(column),
        grid.getY() + height * static_cast<float>(row),
        width, height).reduced(7.0f, 5.0f);
}

void AnalogTapeModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int machine = 0; machine < 4; ++machine)
        if (machinePillBounds(area, machine).contains(event.position))
        {
            if (auto* target = parameter(0))
            {
                graph.focusKeyboardControl(0);
                target->beginChangeGesture();
                target->setValueNotifyingHost(discreteValue(machine, 4));
                target->endChangeGesture();
            }
            repaint();
            return;
        }
    for (int index = 0; index < 4; ++index)
        if (speedSegmentBounds(area, index).contains(event.position))
        {
            if (auto* target = parameter(4))
            {
                graph.focusKeyboardControl(4);
                target->beginChangeGesture();
                target->setValueNotifyingHost(discreteValue(index, 4));
                target->endChangeGesture();
            }
            repaint();
            return;
        }
    for (const auto control : tapeTrackOrder)
    {
        if (trackBounds(area, control).contains(event.position))
        {
            dragPrimary = control;
            break;
        }
    }
    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void AnalogTapeModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto track = trackBounds(area, dragPrimary);
    setValue(dragPrimary, juce::jlimit(
        0.0f, 1.0f,
        (event.position.x - track.getX()) / track.getWidth()));
    updateDefaultDragReadout();
}

void AnalogTapeModuleView::mouseDoubleClick(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto defaults = moduleDefaults(type);
    for (int machine = 0; machine < 4; ++machine)
        if (machinePillBounds(area, machine).contains(event.position))
        {
            setValue(0, defaults[0]);
            repaint();
            return;
        }
    for (int index = 0; index < 4; ++index)
        if (speedSegmentBounds(area, index).contains(event.position))
        {
            setValue(4, defaults[4]);
            repaint();
            return;
        }
    for (const auto control : tapeTrackOrder)
    {
        if (trackBounds(area, control).contains(event.position))
        {
            setValue(control, defaults[static_cast<size_t>(control)]);
            repaint();
            return;
        }
    }
}

void AnalogTapeModuleView::timerCallback()
{
    static constexpr std::array<float, 4> speedRate {
        0.35f, 0.65f, 1.0f, 1.7f
    };
    const auto speedIndex = discreteIndex(value(4), 4);
    reelAngle += 0.05f * speedRate[static_cast<size_t>(speedIndex)];
    if (reelAngle > juce::MathConstants<float>::twoPi)
        reelAngle -= juce::MathConstants<float>::twoPi;
}

void AnalogTapeModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto machine = megadsp::discreteIndex(value(0), 4);
    for (int index = 0; index < 4; ++index)
    {
        auto pill = machinePillBounds(area, index);
        graphics.setColour(index == machine
            ? accent.withAlpha(0.85f) : juce::Colour(0xff2d3745));
        graphics.fillRoundedRectangle(pill, 6.0f);
        graphics.setColour(index == machine
            ? juce::Colours::black : juce::Colours::white.withAlpha(0.85f));
        graphics.setFont(juce::FontOptions(11.5f, juce::Font::bold));
        graphics.drawText(
            megadsp::formatControlValue(type, 0, discreteValue(index, 4)),
            pill.toNearestInt(), juce::Justification::centred);
    }

    const auto display = displayBounds(area);
    graphics.setColour(juce::Colour(0xff151b24).withAlpha(0.92f));
    graphics.fillRoundedRectangle(display, 7.0f);

    auto reels = reelBounds(area);
    const auto radius = juce::jmin(reels.getWidth() * 0.22f,
                                   reels.getHeight() * 0.42f);
    const auto leftCentre = juce::Point<float>(
        reels.getX() + radius + 10.0f, reels.getCentreY());
    const auto rightCentre = juce::Point<float>(
        reels.getRight() - radius - 10.0f, reels.getCentreY());
    graphics.setColour(juce::Colour(0xff384456));
    graphics.drawLine(leftCentre.x, leftCentre.y - radius * 0.55f,
                      rightCentre.x, rightCentre.y - radius * 0.55f, 2.0f);
    graphics.drawLine(leftCentre.x, leftCentre.y + radius * 0.9f,
                      rightCentre.x, rightCentre.y + radius * 0.9f, 2.0f);
    for (const auto& centre : { leftCentre, rightCentre })
    {
        graphics.setColour(juce::Colour(0xff232c39));
        graphics.fillEllipse(centre.x - radius, centre.y - radius,
                             radius * 2.0f, radius * 2.0f);
        graphics.setColour(accent.withAlpha(0.65f));
        graphics.drawEllipse(centre.x - radius, centre.y - radius,
                             radius * 2.0f, radius * 2.0f, 1.5f);
        for (int spoke = 0; spoke < 3; ++spoke)
        {
            const auto angle = reelAngle
                + static_cast<float>(spoke) * juce::MathConstants<float>::twoPi / 3.0f;
            graphics.drawLine(
                centre.x, centre.y,
                centre.x + radius * 0.82f * std::cos(angle),
                centre.y + radius * 0.82f * std::sin(angle), 2.0f);
        }
        graphics.setColour(juce::Colour(0xff101821));
        graphics.fillEllipse(centre.x - radius * 0.22f, centre.y - radius * 0.22f,
                             radius * 0.44f, radius * 0.44f);
    }
    graphics.setColour(juce::Colours::white.withAlpha(0.85f));
    graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    graphics.drawText(
        megadsp::formatControlValue(type, 0, value(0)),
        reels.removeFromTop(18.0f).toNearestInt(), juce::Justification::centred);

    for (int index = 0; index < 4; ++index)
    {
        auto segment = speedSegmentBounds(area, index);
        const auto selected = index == megadsp::discreteIndex(value(4), 4);
        graphics.setColour(selected
            ? outputColour.withAlpha(0.55f) : juce::Colour(0xff2d3745));
        graphics.fillRoundedRectangle(segment, 4.0f);
        graphics.setColour(juce::Colours::white.withAlpha(selected ? 0.95f : 0.6f));
        graphics.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        graphics.drawText(tapeSpeedLabels[static_cast<size_t>(index)],
                          segment.toNearestInt(), juce::Justification::centred);
    }

    for (const auto control : tapeTrackOrder)
    {
        auto track = trackBounds(area, control);
        const auto label = juce::String(
            megadsp::controlMetadata(type, control).label);
        graphics.setColour(juce::Colour(0xffa8b3c0));
        graphics.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        graphics.drawText(
            label, track.removeFromTop(16.0f).toNearestInt(),
            juce::Justification::centredLeft);
        graphics.setColour(juce::Colours::white.withAlpha(0.88f));
        graphics.setFont(juce::FontOptions(9.5f));
        graphics.drawText(
            megadsp::formatControlValue(type, control, value(control)),
            track.removeFromTop(16.0f).toNearestInt(),
            juce::Justification::centredRight);
        auto rail = track.withHeight(7.0f).withCentre(track.getCentre());
        graphics.setColour(juce::Colour(0xff2d3745));
        graphics.fillRoundedRectangle(rail, 3.5f);
        graphics.setColour((control == 10 || control == 11)
            ? outputColour : accent);
        graphics.fillRoundedRectangle(
            rail.withWidth(juce::jmax(4.0f, rail.getWidth() * value(control))),
            3.5f);
        const auto handleX = rail.getX() + value(control) * rail.getWidth();
        graphics.setColour(juce::Colours::white);
        graphics.fillEllipse(handleX - 4.5f, rail.getCentreY() - 4.5f, 9.0f, 9.0f);
    }
}
} // namespace

std::unique_ptr<ModuleView> createAnalogTapeView(EffectGraph& graph)
{
    return std::make_unique<AnalogTapeModuleView>(graph);
}
} // namespace megadsp::ui
