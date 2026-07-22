#include "../GraphStyle.h"
#include "../ModuleView.h"

#include "ModuleViewCreators.h"

#include <array>
#include <cmath>

namespace megadsp::ui
{
namespace
{
class WavefoldGardenModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    bool usesFullPanel() const override { return true; }
    void configureAccessibility(juce::Component& component) const override
    {
        component.setTitle("Wavefold Garden transfer field");
        component.setHelpText(
            "Choose a nonlinear Character with the top pills. Drag the "
            "transfer field horizontally for Drive and vertically for "
            "Symmetry; hold Shift to edit Shape. The live envelope and fold "
            "thresholds animate the curve. Lower tracks edit stages, envelope, "
            "tone, stereo bloom, mix, and output.");
    }
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    static juce::Rectangle<float> characterPill(
        juce::Rectangle<float> area, int index)
    {
        auto row = area.removeFromTop(31.0f);
        const auto width = row.getWidth() * 0.25f;
        return juce::Rectangle<float>(
            row.getX() + width * static_cast<float>(index), row.getY(),
            width, row.getHeight()).reduced(4.0f, 2.0f);
    }
    static juce::Rectangle<float> transferBounds(juce::Rectangle<float> area)
    {
        area.removeFromTop(34.0f);
        area.removeFromBottom(area.getHeight() * 0.43f);
        return area.reduced(5.0f);
    }
    static juce::Rectangle<float> trackBounds(
        juce::Rectangle<float> area, int control)
    {
        area.removeFromTop(34.0f);
        auto tracks = area.removeFromBottom(area.getHeight() * 0.43f);
        static constexpr std::array<int, 8> order {
            2, 5, 6, 7, 8, 9, 10, 11
        };
        auto position = 0;
        while (position < static_cast<int>(order.size())
               && order[static_cast<size_t>(position)] != control)
            ++position;
        if (position == static_cast<int>(order.size()))
            return {};
        const auto width = tracks.getWidth() * 0.25f;
        const auto height = tracks.getHeight() * 0.5f;
        return juce::Rectangle<float>(
            tracks.getX() + width * static_cast<float>(position % 4),
            tracks.getY() + height * static_cast<float>(position / 4),
            width, height).reduced(6.0f, 4.0f);
    }
    static float transfer(float input, int character, int folds,
                          float symmetry, float shape);
    void chooseCharacter(int index);
};

float WavefoldGardenModuleView::transfer(
    float input, int character, int folds, float symmetry, float shape)
{
    auto value = input + symmetry * 0.65f;
    if (character == 2)
    {
        const auto order = juce::jlimit(1, 8, folds);
        value = std::cos(static_cast<float>(order)
                         * std::acos(juce::jlimit(-1.0f, 1.0f, value)));
    }
    else
    {
        for (int stage = 0; stage < folds; ++stage)
        {
            if (character == 0)
                value = std::sin(value * juce::MathConstants<float>::halfPi
                                 * (1.0f + shape));
            else if (character == 1)
                value = 2.0f * std::abs(
                    value * 0.5f - std::floor(value * 0.5f + 0.5f)) - 1.0f;
            else
                value = std::tanh(
                    (value + 0.3f * symmetry) * (1.2f + 2.8f * shape));
        }
    }
    return juce::jlimit(-1.25f, 1.25f, value);
}

void WavefoldGardenModuleView::chooseCharacter(int index)
{
    if (auto* target = parameter(0))
    {
        graph.focusKeyboardControl(0);
        target->beginChangeGesture();
        target->setValueNotifyingHost(discreteValue(index, 4));
        target->endChangeGesture();
    }
    repaint();
}

void WavefoldGardenModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int index = 0; index < 4; ++index)
        if (characterPill(area, index).contains(event.position))
        {
            chooseCharacter(index);
            return;
        }
    if (transferBounds(area).contains(event.position))
    {
        dragPrimary = event.mods.isShiftDown() ? 4 : 1;
        dragSecondary = event.mods.isShiftDown() ? -1 : 3;
    }
    for (const auto control : { 2, 5, 6, 7, 8, 9, 10, 11 })
        if (dragPrimary < 0
            && trackBounds(area, control).contains(event.position))
            dragPrimary = control;
    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void WavefoldGardenModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (dragPrimary == 1 || dragPrimary == 4)
    {
        const auto bounds = transferBounds(area);
        setValue(dragPrimary, juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - bounds.getX()) / bounds.getWidth()));
        if (dragSecondary == 3)
            setValue(3, juce::jlimit(
                0.0f, 1.0f,
                (bounds.getBottom() - event.position.y)
                    / bounds.getHeight()));
        dragReadout = juce::String(
            controlMetadata(type, dragPrimary).label) + "  "
            + formatControlValue(type, dragPrimary, value(dragPrimary));
        if (dragSecondary == 3)
            dragReadout += "    SYMMETRY  "
                + formatControlValue(type, 3, value(3));
        repaint();
        return;
    }
    const auto bounds = trackBounds(area, dragPrimary);
    setValue(dragPrimary, juce::jlimit(
        0.0f, 1.0f, (event.position.x - bounds.getX()) / bounds.getWidth()));
    updateDefaultDragReadout();
}

void WavefoldGardenModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto defaults = moduleDefaults(type);
    for (int index = 0; index < 4; ++index)
        if (characterPill(area, index).contains(event.position))
        {
            setValue(0, defaults[0]);
            return;
        }
    if (transferBounds(area).contains(event.position))
    {
        setValue(1, defaults[1]);
        setValue(3, defaults[3]);
        setValue(4, defaults[4]);
        return;
    }
    for (const auto control : { 2, 5, 6, 7, 8, 9, 10, 11 })
        if (trackBounds(area, control).contains(event.position))
        {
            setValue(control, defaults[static_cast<size_t>(control)]);
            return;
        }
}

void WavefoldGardenModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    static constexpr std::array<const char*, 4> names {
        "PETAL", "PRISM", "CHEBYSHEV", "BLOOM"
    };
    const auto character = discreteIndex(value(0), 4);
    for (int index = 0; index < 4; ++index)
    {
        const auto bounds = characterPill(area, index);
        graphics.setColour(index == character
                               ? accent : juce::Colour(0xff27313d));
        graphics.fillRoundedRectangle(bounds, 7.0f);
        graphics.setColour(index == character
                               ? juce::Colour(0xff101820)
                               : juce::Colours::white);
        graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        graphics.drawText(names[static_cast<size_t>(index)],
                          bounds.toNearestInt(),
                          juce::Justification::centred);
    }

    const auto field = transferBounds(area);
    graphics.setColour(juce::Colour(0xe6121821));
    graphics.fillRoundedRectangle(field, 8.0f);
    graphics.setColour(juce::Colours::white.withAlpha(0.10f));
    graphics.drawHorizontalLine(juce::roundToInt(field.getCentreY()),
                                field.getX(), field.getRight());
    graphics.drawVerticalLine(juce::roundToInt(field.getCentreX()),
                              field.getY(), field.getBottom());
    const auto drive = juce::Decibels::decibelsToGain(value(1) * 36.0f);
    const auto folds = juce::jlimit(
        1, 8, juce::roundToInt(1.0f + value(2) * 7.0f));
    const auto symmetry = value(3) * 2.0f - 1.0f;
    const auto envelope = juce::jlimit(
        0.0f, 1.0f,
        outputLevels.back() > -100.0f
            ? juce::jmap(outputLevels.back(), -48.0f, 0.0f, 0.0f, 1.0f)
            : 0.0f);
    const auto dynamics = value(5) * 2.0f - 1.0f;
    juce::Path curve;
    for (int point = 0; point <= 160; ++point)
    {
        const auto normalized = static_cast<float>(point) / 160.0f;
        const auto input = normalized * 2.0f - 1.0f;
        const auto dynamicDrive =
            drive * (1.0f + envelope * dynamics * 0.65f);
        const auto output = transfer(
            input * dynamicDrive, character, folds, symmetry, value(4));
        const auto graphPoint = juce::Point<float> {
            field.getX() + normalized * field.getWidth(),
            field.getCentreY()
                - output * field.getHeight() * 0.38f
        };
        if (point == 0) curve.startNewSubPath(graphPoint);
        else curve.lineTo(graphPoint);
    }
    graphics.setColour(outputColour.withAlpha(0.30f));
    graphics.strokePath(curve, juce::PathStrokeType(6.0f));
    graphics.setColour(accent);
    graphics.strokePath(curve, juce::PathStrokeType(2.0f));
    const auto handle = juce::Point<float> {
        field.getX() + value(1) * field.getWidth(),
        field.getBottom() - value(3) * field.getHeight()
    };
    graphics.setColour(juce::Colours::white);
    graphics.fillEllipse(
        juce::Rectangle<float>(13.0f, 13.0f).withCentre(handle));
    graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    auto fieldHeader = field;
    graphics.drawText(
        "DRIVE " + formatControlValue(type, 1, value(1))
            + "  /  SYMMETRY "
            + formatControlValue(type, 3, value(3)),
        fieldHeader.removeFromTop(22.0f)
            .reduced(8.0f, 0.0f).toNearestInt(),
        juce::Justification::centredLeft);

    for (const auto control : { 2, 5, 6, 7, 8, 9, 10, 11 })
    {
        const auto bounds = trackBounds(area, control);
        graphics.setColour(juce::Colour(0xff151b24));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour(
            control == 9 ? outputColour.withAlpha(0.58f)
                         : accent.withAlpha(0.56f));
        graphics.fillRoundedRectangle(
            bounds.withWidth(bounds.getWidth() * value(control)), 5.0f);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(9.5f));
        graphics.drawFittedText(
            juce::String(controlMetadata(type, control).label) + "  "
                + formatControlValue(type, control, value(control)),
            bounds.reduced(6.0f, 0.0f).toNearestInt(),
            juce::Justification::centred, 1);
    }
}
} // namespace

std::unique_ptr<ModuleView> createWavefoldGardenView(EffectGraph& graph)
{
    return std::make_unique<WavefoldGardenModuleView>(graph);
}
} // namespace megadsp::ui
