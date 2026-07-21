#include "../GraphStyle.h"
#include "../GuiLayout.h"
#include "../ModuleView.h"

#include "ModuleViewCreators.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace megadsp::ui
{
namespace
{
class VintageChorusModuleView final : public ModuleView
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
    static juce::Rectangle<float> chorusPillBounds(
        juce::Rectangle<float>, int model);
    static juce::Rectangle<float> chorusDisplayBounds(
        juce::Rectangle<float>);
    static juce::Rectangle<float> chorusControlBounds(
        juce::Rectangle<float>, int control);
    static juce::Rectangle<float> chorusRateBounds(
        juce::Rectangle<float>);
    static juce::Rectangle<float> chorusFieldBounds(
        juce::Rectangle<float>);
    ChorusHandleGeometry chorusHandles(
        juce::Rectangle<float>) const;
};

void VintageChorusModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Vintage Chorus modulation field");
    component.setHelpText(
        "Choose a topology with the model pills. The animated field "
        "shows each delay voice moving across delay time and stereo "
        "position. Drag Rate, Depth, Delay, and Width directly in that "
        "field; trails reveal phase relationships and the arrow shows "
        "Regeneration polarity. Density, Character, Mix, Phase, and Output "
        "use grouped lower rails. The nearest handle wins when hit zones "
        "overlap; hold Shift to prefer Depth. Drag any rail and "
        "double-click any control to restore its default.");
}

juce::Rectangle<float> VintageChorusModuleView::chorusPillBounds(
    juce::Rectangle<float> area, int model)
{
    auto pills = area.removeFromTop(32.0f);
    const auto width = pills.getWidth() * 0.25f;
    return juce::Rectangle<float>(
        pills.getX() + width * static_cast<float>(model),
        pills.getY(), width, pills.getHeight()).reduced(4.0f, 2.0f);
}

juce::Rectangle<float> VintageChorusModuleView::chorusDisplayBounds(
    juce::Rectangle<float> area)
{
    area.removeFromTop(38.0f);
    area.removeFromBottom(area.getHeight() * 0.38f);
    return area.reduced(5.0f, 2.0f);
}

juce::Rectangle<float> VintageChorusModuleView::chorusControlBounds(
    juce::Rectangle<float> area, int control)
{
    area.removeFromTop(38.0f);
    auto controls = area.removeFromBottom(area.getHeight() * 0.38f);
    auto compact = controls.removeFromBottom(
        juce::jmax(24.0f, controls.getHeight() * 0.25f));
    if (control == 9 || control == 11)
    {
        const auto column = control == 9 ? 0 : 1;
        const auto width = compact.getWidth() * 0.5f;
        return juce::Rectangle<float>(
            compact.getX() + width * static_cast<float>(column),
            compact.getY(), width, compact.getHeight())
            .reduced(7.0f, 3.0f);
    }
    static constexpr std::array<int, 5> order { 4, 6, 10, 7, 8 };
    const auto iterator = std::find(order.begin(), order.end(), control);
    if (iterator == order.end())
        return {};
    const auto position =
        static_cast<int>(std::distance(order.begin(), iterator));
    const auto row = position < 3 ? 0 : 1;
    const auto column = row == 0 ? position : position - 3;
    const auto columns = row == 0 ? 3 : 2;
    const auto height = controls.getHeight() * 0.5f;
    const auto width = controls.getWidth() / static_cast<float>(columns);
    return juce::Rectangle<float>(
        controls.getX() + width * static_cast<float>(column),
        controls.getY() + height * static_cast<float>(row),
        width, height).reduced(7.0f, 4.0f);
}

juce::Rectangle<float> VintageChorusModuleView::chorusRateBounds(
    juce::Rectangle<float> area)
{
    auto display = chorusDisplayBounds(area);
    return display.removeFromTop(24.0f)
        .removeFromRight(display.getWidth() * 0.40f)
        .reduced(5.0f, 4.0f);
}

juce::Rectangle<float> VintageChorusModuleView::chorusFieldBounds(
    juce::Rectangle<float> area)
{
    auto display = chorusDisplayBounds(area);
    display.removeFromTop(27.0f);
    display.removeFromBottom(17.0f);
    return display.reduced(10.0f, 3.0f);
}

ChorusHandleGeometry VintageChorusModuleView::chorusHandles(
    juce::Rectangle<float> area) const
{
    return calculateChorusHandleGeometry(
        chorusFieldBounds(area), value(3), value(5), value(2));
}

void VintageChorusModuleView::mouseDown(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int model = 0; model < 4; ++model)
        if (chorusPillBounds(area, model).contains(event.position))
        {
            if (auto* target = parameter(0))
            {
                graph.focusKeyboardControl(0);
                target->beginChangeGesture();
                target->setValueNotifyingHost(discreteValue(model, 4));
                target->endChangeGesture();
            }
            repaint();
            return;
        }
    if (chorusRateBounds(area).contains(event.position))
        dragPrimary = 1;
    else
    {
        const auto target = hitTestChorusHandles(
            chorusHandles(area), event.position,
            event.mods.isShiftDown());
        if (target == ChorusHandleTarget::depth)
            dragPrimary = 2;
        else if (target == ChorusHandleTarget::delayAndWidth)
        {
            dragPrimary = 3;
            dragSecondary = 5;
        }
    }
    static constexpr std::array<int, 7> railControls {
        4, 6, 7, 8, 9, 10, 11
    };
    for (const auto control : railControls)
    {
        if (dragPrimary >= 0)
            break;
        if (chorusControlBounds(area, control).contains(event.position))
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

void VintageChorusModuleView::mouseDrag(
    const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (dragPrimary == 1)
    {
        const auto rate = chorusRateBounds(area);
        setValue(1, juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - rate.getX()) / rate.getWidth()));
        updateDefaultDragReadout();
        return;
    }
    if (dragPrimary == 2)
    {
        const auto handles = chorusHandles(area);
        setValue(
            2, chorusDepthNormalizedAtX(handles, event.position.x));
        updateDefaultDragReadout();
        return;
    }
    if (dragPrimary == 3)
    {
        const auto field = chorusFieldBounds(area);
        setValue(
            3, chorusDelayNormalizedAtX(field, event.position.x));
        setValue(
            5, chorusWidthNormalizedAtY(field, event.position.y));
        dragReadout =
            "DELAY  "
            + megadsp::formatControlValue(type, 3, value(3))
            + "  ·  WIDTH "
            + megadsp::formatControlValue(type, 5, value(5));
        repaint();
        return;
    }
    const auto track = chorusControlBounds(area, dragPrimary);
    setValue(dragPrimary, juce::jlimit(
        0.0f, 1.0f,
        (event.position.x - track.getX()) / track.getWidth()));
    updateDefaultDragReadout();
}

void VintageChorusModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto defaults = moduleDefaults(type);
    for (int model = 0; model < 4; ++model)
        if (chorusPillBounds(area, model).contains(event.position))
        {
            setValue(0, defaults[0]);
            repaint();
            return;
        }
    if (chorusRateBounds(area).contains(event.position))
    {
        setValue(1, defaults[1]);
        repaint();
        return;
    }
    const auto target = hitTestChorusHandles(
        chorusHandles(area), event.position,
        event.mods.isShiftDown());
    if (target == ChorusHandleTarget::depth)
    {
        setValue(2, defaults[2]);
        repaint();
        return;
    }
    if (target == ChorusHandleTarget::delayAndWidth)
    {
        setValue(3, defaults[3]);
        setValue(5, defaults[5]);
        repaint();
        return;
    }
    static constexpr std::array<int, 7> railControls {
        4, 6, 7, 8, 9, 10, 11
    };
    for (const auto control : railControls)
        if (chorusControlBounds(area, control).contains(event.position))
        {
            setValue(control, defaults[static_cast<size_t>(control)]);
            repaint();
            return;
        }
}

void VintageChorusModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    static constexpr std::array<const char*, 4> modelNames {
        "VINTAGE BBD", "DIMENSION", "TRI-CHORUS", "STRING ENSEMBLE"
    };
    static constexpr std::array<const char*, 4> descriptions {
        "warm BBD  /  asymmetric drift + compander colour",
        "stable widening  /  complementary quadrature taps",
        "three-phase studio  /  smooth centre stability",
        "dense ensemble  /  dual-rate dark modulation groups"
    };
    static const std::array<juce::Colour, 6> voiceColours {
        juce::Colour(0xffffbd69), juce::Colour(0xff77d8ff),
        juce::Colour(0xffc898ff), juce::Colour(0xff70e4ad),
        juce::Colour(0xffff7f9f), juce::Colour(0xffd7e36e)
    };
    const auto model = megadsp::discreteIndex(value(0), 4);
    for (int pill = 0; pill < 4; ++pill)
    {
        const auto bounds = chorusPillBounds(area, pill);
        graphics.setColour(pill == model
            ? accent : juce::Colour(0xff27313d));
        graphics.fillRoundedRectangle(bounds, 7.0f);
        graphics.setColour(pill == model
            ? juce::Colour(0xff111820) : juce::Colour(0xffabb6c4));
        graphics.setFont(juce::FontOptions(
            10.5f, pill == model ? juce::Font::bold : juce::Font::plain));
        graphics.drawText(modelNames[static_cast<size_t>(pill)],
                          bounds.toNearestInt(),
                          juce::Justification::centred);
    }

    const auto display = chorusDisplayBounds(area);
    graphics.setColour(juce::Colour(0xe6121821));
    graphics.fillRoundedRectangle(display, 8.0f);
    graphics.setColour(accent.withAlpha(0.72f));
    graphics.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    auto descriptionBounds = display;
    descriptionBounds = descriptionBounds.removeFromTop(24.0f);
    descriptionBounds.removeFromRight(descriptionBounds.getWidth() * 0.41f);
    graphics.drawText(
        descriptions[static_cast<size_t>(model)],
        descriptionBounds.reduced(8.0f, 0.0f).toNearestInt(),
        juce::Justification::centredLeft);

    const auto rateRail = chorusRateBounds(area);
    graphics.setColour(juce::Colour(0xff2d3745));
    graphics.fillRoundedRectangle(rateRail, 4.0f);
    graphics.setColour(accent.withAlpha(0.45f));
    graphics.fillRoundedRectangle(
        rateRail.withWidth(rateRail.getWidth() * value(1)), 4.0f);
    const auto rateX =
        rateRail.getX() + value(1) * rateRail.getWidth();
    graphics.setColour(juce::Colours::white);
    graphics.fillEllipse(
        rateX - 4.0f, rateRail.getCentreY() - 4.0f, 8.0f, 8.0f);
    graphics.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    graphics.drawText(
        "RATE  " + megadsp::formatControlValue(type, 1, value(1)),
        rateRail.reduced(6.0f, 0.0f).toNearestInt(),
        juce::Justification::centred);

    const auto delayAxis = chorusFieldBounds(area);
    graphics.setColour(juce::Colours::white.withAlpha(0.10f));
    graphics.drawHorizontalLine(
        juce::roundToInt(delayAxis.getCentreY()),
        delayAxis.getX(), delayAxis.getRight());
    for (int marker = 0; marker < 4; ++marker)
    {
        const auto x = delayAxis.getX() + delayAxis.getWidth()
            * static_cast<float>(marker) / 3.0f;
        graphics.drawVerticalLine(juce::roundToInt(x),
                                 delayAxis.getY(), delayAxis.getBottom());
        graphics.setColour(juce::Colour(0xff8290a1));
        graphics.setFont(juce::FontOptions(9.0f));
        graphics.drawText(
            juce::String(2.0f + 28.0f * static_cast<float>(marker) / 3.0f,
                         0) + " ms",
            juce::Rectangle<float>(
                x - 24.0f, delayAxis.getBottom(), 48.0f, 15.0f)
                .toNearestInt(),
            juce::Justification::centred);
        graphics.setColour(juce::Colours::white.withAlpha(0.10f));
    }
    graphics.setColour(juce::Colour(0xff8290a1));
    graphics.drawText("L", delayAxis.withWidth(14.0f).toNearestInt(),
                      juce::Justification::centredTop);
    graphics.drawText("R", delayAxis.withWidth(14.0f).toNearestInt(),
                      juce::Justification::centredBottom);

    const auto rate = exponential(0.05f, 8.0f, value(1));
    const auto depth = value(2);
    const auto baseDelay = exponential(2.0f, 30.0f, value(3));
    const auto voiceCount = juce::jlimit(
        1, 6, juce::roundToInt(1.0f + value(4) * 5.0f));
    const auto stereoPhase = value(9) * 0.5f;
    const auto now = static_cast<float>(
        juce::Time::getMillisecondCounterHiRes() * 0.001);
    const auto modelRate = model == 0 ? 0.86f
        : model == 1 ? 0.63f : model == 3 ? 1.42f : 1.0f;
    const auto basePhase = now * rate * modelRate;
    auto modelLfo = [model](float phase)
    {
        const auto p = phase - std::floor(phase);
        const auto sine = std::sin(
            juce::MathConstants<float>::twoPi * p);
        if (model == 0)
            return 0.78f * sine + 0.22f * std::sin(
                juce::MathConstants<float>::twoPi * (2.0f * p + 0.12f));
        if (model == 1)
            return 0.72f * sine + 0.28f * std::sin(
                juce::MathConstants<float>::twoPi * (2.0f * p + 0.25f));
        if (model == 3)
            return 0.82f * sine + 0.18f * std::sin(
                juce::MathConstants<float>::twoPi * (3.0f * p + 0.17f));
        return sine;
    };
    auto voicePhase = [model](int voice)
    {
        if (model == 0)
            return (voice % 2 == 0 ? 0.0f : 0.47f)
                   + static_cast<float>(voice / 2) * 0.073f;
        if (model == 1)
            return static_cast<float>(voice % 4) / 4.0f
                   + static_cast<float>(voice / 4) * 0.055f;
        if (model == 2) return static_cast<float>(voice % 3) / 3.0f
                              + static_cast<float>(voice / 3) * 0.08f;
        return static_cast<float>(voice) / 6.0f;
    };
    const auto depthScale = std::array<float, 4> {
        0.82f, 0.30f, 0.68f, 1.0f
    }[static_cast<size_t>(model)];
    const auto swing = juce::jmin(8.0f, baseDelay * 0.72f)
                       * depth * depthScale;
    for (int voice = 0; voice < voiceCount; ++voice)
    {
        const auto colour = voiceColours[static_cast<size_t>(voice)];
        const auto pan = model == 0 ? (voice % 2 == 0 ? -0.34f : 0.34f)
            : model == 1 ? std::array<float, 4> { -0.85f, 0.72f, -0.42f, 0.91f }[
                  static_cast<size_t>(voice % 4)]
            : model == 2 ? std::array<float, 3> { -0.72f, 0.0f, 0.72f }[
                  static_cast<size_t>(voice % 3)]
            : std::array<float, 6> { -0.9f, 0.66f, -0.36f, 0.36f, -0.66f, 0.9f }[
                  static_cast<size_t>(voice)];
        const auto rateRatio =
            model == 3 ? (voice % 2 == 0 ? 0.72f : 1.37f) : 1.0f;
        juce::Path trail;
        for (int step = 0; step < 28; ++step)
        {
            const auto history = static_cast<float>(27 - step) / 27.0f;
            const auto phase = (basePhase - history * 0.42f * rate)
                              * rateRatio + voicePhase(voice);
            const auto lfo = modelLfo(phase + (pan > 0.0f
                                                  ? stereoPhase : 0.0f));
            const auto staticOffset = model == 1
                ? (static_cast<float>(voice % 4) - 1.5f) * 0.65f
                : model == 3 ? (voice % 2 == 0 ? -0.7f : 0.7f) : 0.0f;
            const auto delay = juce::jlimit(
                2.0f, 30.0f, baseDelay + staticOffset + swing * lfo);
            const auto point = juce::Point<float> {
                delayAxis.getX() + (delay - 2.0f) / 28.0f
                                      * delayAxis.getWidth(),
                delayAxis.getCentreY()
                    + pan * delayAxis.getHeight() * 0.38f
                    + lfo * delayAxis.getHeight() * 0.035f
            };
            if (step == 0) trail.startNewSubPath(point);
            else trail.lineTo(point);
        }
        graphics.setColour(colour.withAlpha(0.48f));
        graphics.strokePath(trail, juce::PathStrokeType(1.5f));
        const auto point = trail.getCurrentPosition();
        graphics.setColour(colour.withAlpha(0.20f));
        graphics.fillEllipse(
            juce::Rectangle<float>(14.0f, 14.0f).withCentre(point));
        graphics.setColour(colour);
        graphics.fillEllipse(
            juce::Rectangle<float>(7.0f, 7.0f).withCentre(point));
        graphics.setFont(juce::FontOptions(8.5f, juce::Font::bold));
        graphics.drawText(
            juce::String(voice + 1),
            juce::Rectangle<float>(14.0f, 14.0f).withCentre(point)
                .toNearestInt(),
            juce::Justification::centred);
    }

    const auto handles = chorusHandles(area);
    const auto primaryPoint = handles.delayAndWidth;
    const auto depthPoint = handles.depth;
    graphics.setColour(accent.withAlpha(0.62f));
    graphics.drawLine(
        primaryPoint.x, delayAxis.getBottom(),
        primaryPoint.x, primaryPoint.y, 1.5f);
    graphics.drawLine(
        juce::Line<float>(primaryPoint, handles.depthOrigin), 1.5f);
    graphics.drawLine(
        juce::Line<float>(handles.depthOrigin, depthPoint), 3.0f);
    graphics.setColour(juce::Colours::white);
    graphics.fillEllipse(
        juce::Rectangle<float>(13.0f, 13.0f)
            .withCentre(primaryPoint));
    graphics.setColour(outputColour);
    graphics.fillEllipse(
        juce::Rectangle<float>(11.0f, 11.0f)
            .withCentre(depthPoint));
    auto primaryLabel = juce::Rectangle<float>(
        190.0f, 18.0f)
                            .withCentre(primaryPoint.translated(0.0f, -14.0f))
                            .constrainedWithin(delayAxis);
    graphics.setColour(juce::Colours::white);
    graphics.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    graphics.drawText(
        "DELAY " + megadsp::formatControlValue(type, 3, value(3))
            + "  ·  WIDTH "
            + megadsp::formatControlValue(type, 5, value(5)),
        primaryLabel.toNearestInt(), juce::Justification::centred);
    graphics.drawText(
        "DEPTH " + megadsp::formatControlValue(type, 2, value(2)),
        juce::Rectangle<float>(
            depthPoint.x - 70.0f, depthPoint.y + 7.0f, 140.0f, 16.0f)
            .constrainedWithin(delayAxis)
            .toNearestInt(),
        juce::Justification::centred);

    const auto regeneration = value(6) * 150.0f - 75.0f;
    graphics.setColour(regeneration >= 0.0f
        ? juce::Colour(0xff70e4ad) : juce::Colour(0xffff7f9f));
    graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    auto regenerationBounds = display;
    regenerationBounds = regenerationBounds.removeFromBottom(16.0f);
    regenerationBounds = regenerationBounds.removeFromRight(180.0f);
    graphics.drawText(
        juce::String(regeneration >= 0.0f ? "↻  " : "↺  ")
            + megadsp::formatControlValue(type, 6, value(6))
            + " REGENERATION",
        regenerationBounds.toNearestInt(),
        juce::Justification::centredRight);

    static constexpr std::array<int, 7> railControls {
        4, 6, 7, 8, 9, 10, 11
    };
    for (const auto control : railControls)
    {
        const auto track = chorusControlBounds(area, control);
        graphics.setColour(juce::Colour(0xff151b24));
        graphics.fillRoundedRectangle(track, 5.0f);
        auto fill = track;
        fill.setWidth(track.getWidth() * value(control));
        graphics.setColour(
            control == 6 && value(control) < 0.5f
                ? juce::Colour(0xffff7f9f).withAlpha(0.70f)
                : accent.withAlpha(
                      control == 9 || control == 11 ? 0.40f : 0.65f));
        graphics.fillRoundedRectangle(fill, 5.0f);
        graphics.setColour(juce::Colours::white.withAlpha(0.92f));
        graphics.setFont(juce::FontOptions(10.0f));
        graphics.drawText(
            juce::String(megadsp::controlMetadata(type, control).label)
                + "   " + megadsp::formatControlValue(
                    type, control, value(control)),
            track.reduced(7.0f, 0.0f).toNearestInt(),
            juce::Justification::centredLeft);
        const auto handleX = track.getX()
            + track.getWidth() * value(control);
        graphics.setColour(juce::Colours::white);
        graphics.fillEllipse(handleX - 3.0f,
                            track.getCentreY() - 3.0f, 6.0f, 6.0f);
    }
}
} // namespace

std::unique_ptr<ModuleView> createVintageChorusView(EffectGraph& graph)
{
    return std::make_unique<VintageChorusModuleView>(graph);
}
} // namespace megadsp::ui
