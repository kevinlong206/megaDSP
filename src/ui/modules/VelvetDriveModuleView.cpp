#include "../GraphStyle.h"
#include "../ModuleView.h"

#include "ModuleViewCreators.h"
#include "../../modules/VelvetDrive.h"

#include <array>
#include <cmath>

namespace megadsp::ui
{
namespace
{
constexpr std::array<int, 9> continuousControls {
    2, 3, 4, 5, 6, 7, 8, 9, 11
};

class VelvetDriveModuleView final : public ModuleView
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
    static juce::Rectangle<float> choiceBounds(
        juce::Rectangle<float>, int, int, int);
    static juce::Rectangle<float> controlBounds(
        juce::Rectangle<float>, int);
    void choose(int, int, int);
};

void VelvetDriveModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Velvet Drive modeled guitar amplifier");
    component.setHelpText(
        "Choose the input, amplifier voice, and analytic speaker load with "
        "the labeled pills. Drag the gain, tone, presence, feel, and master "
        "tracks horizontally. Double-click any control to restore its default.");
}

juce::Rectangle<float> VelvetDriveModuleView::choiceBounds(
    juce::Rectangle<float> area, int control, int option, int count)
{
    const auto rowHeight = juce::jlimit(24.0f, 32.0f, area.getHeight() * 0.10f);
    const auto row = control == 10 ? 2 : control;
    auto bounds = juce::Rectangle<float>(
        area.getX(), area.getY() + rowHeight * static_cast<float>(row),
        area.getWidth(), rowHeight).reduced(3.0f, 3.0f);
    const auto labelWidth = juce::jmin(74.0f, bounds.getWidth() * 0.18f);
    bounds.removeFromLeft(labelWidth);
    const auto width = bounds.getWidth() / static_cast<float>(count);
    return juce::Rectangle<float>(
        bounds.getX() + width * static_cast<float>(option),
        bounds.getY(), width, bounds.getHeight()).reduced(2.0f, 0.0f);
}

juce::Rectangle<float> VelvetDriveModuleView::controlBounds(
    juce::Rectangle<float> area, int control)
{
    const auto top = juce::jlimit(76.0f, 98.0f, area.getHeight() * 0.30f);
    area.removeFromTop(top);
    auto index = 0;
    for (; index < static_cast<int>(continuousControls.size()); ++index)
        if (continuousControls[static_cast<size_t>(index)] == control)
            break;
    if (index >= static_cast<int>(continuousControls.size()))
        return {};
    const auto row = index / 3;
    const auto column = index % 3;
    const auto width = area.getWidth() / 3.0f;
    const auto height = area.getHeight() / 3.0f;
    return juce::Rectangle<float>(
        area.getX() + width * static_cast<float>(column),
        area.getY() + height * static_cast<float>(row),
        width, height).reduced(7.0f, 6.0f);
}

void VelvetDriveModuleView::choose(int control, int option, int count)
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

void VelvetDriveModuleView::mouseDown(const juce::MouseEvent& event)
{
    constexpr std::array<int, 3> controls { 0, 1, 10 };
    constexpr std::array<int, 3> counts { 2, 3, 4 };
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int group = 0; group < 3; ++group)
        for (int option = 0; option < counts[static_cast<size_t>(group)];
             ++option)
            if (choiceBounds(
                    area, controls[static_cast<size_t>(group)], option,
                    counts[static_cast<size_t>(group)])
                    .contains(event.position))
            {
                choose(
                    controls[static_cast<size_t>(group)], option,
                    counts[static_cast<size_t>(group)]);
                return;
            }

    for (const auto control : continuousControls)
        if (controlBounds(area, control).contains(event.position))
        {
            dragPrimary = control;
            beginGestures();
            mouseDrag(event);
            return;
        }
}

void VelvetDriveModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto bounds = controlBounds(
        getLocalBounds().toFloat().reduced(12.0f), dragPrimary);
    setValue(
        dragPrimary,
        juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - bounds.getX()) / bounds.getWidth()));
    updateDefaultDragReadout();
    repaint();
}

void VelvetDriveModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    constexpr std::array<int, 3> choiceControls { 0, 1, 10 };
    constexpr std::array<int, 3> choiceCounts { 2, 3, 4 };
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int group = 0; group < 3; ++group)
        for (int option = 0;
             option < choiceCounts[static_cast<size_t>(group)]; ++option)
            if (choiceBounds(
                    area, choiceControls[static_cast<size_t>(group)], option,
                    choiceCounts[static_cast<size_t>(group)])
                    .contains(event.position))
            {
                resetToDefault(choiceControls[static_cast<size_t>(group)]);
                return;
            }
    for (const auto control : continuousControls)
        if (controlBounds(area, control).contains(event.position))
        {
            resetToDefault(control);
            return;
        }
}

void VelvetDriveModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    graphics.setColour(juce::Colour(0xff17120f));
    graphics.fillRoundedRectangle(area, 10.0f);

    constexpr std::array<int, 3> choiceControls { 0, 1, 10 };
    constexpr std::array<int, 3> choiceCounts { 2, 3, 4 };
    constexpr std::array<const char*, 3> choiceLabels {
        "INPUT", "VOICE", "SPEAKER"
    };
    for (int group = 0; group < 3; ++group)
    {
        const auto control = choiceControls[static_cast<size_t>(group)];
        for (int option = 0;
             option < choiceCounts[static_cast<size_t>(group)]; ++option)
        {
            const auto selected = discreteIndex(
                value(control), choiceCounts[static_cast<size_t>(group)])
                == option;
            const auto pill = choiceBounds(
                area, control, option,
                choiceCounts[static_cast<size_t>(group)]);
            graphics.setColour(
                selected ? juce::Colour(0xffd8a15d)
                         : juce::Colour(0xff392b21));
            graphics.fillRoundedRectangle(pill, 5.0f);
            graphics.setColour(
                selected ? juce::Colour(0xff21160e)
                         : juce::Colours::white.withAlpha(0.82f));
            graphics.setFont(juce::FontOptions(
                9.5f, selected ? juce::Font::bold : juce::Font::plain));
            graphics.drawText(
                formatControlValue(
                    type, control,
                    discreteValue(
                        option, choiceCounts[static_cast<size_t>(group)])),
                pill.toNearestInt(), juce::Justification::centred);
        }
        const auto first = choiceBounds(
            area, control, 0, choiceCounts[static_cast<size_t>(group)]);
        graphics.setColour(juce::Colours::white.withAlpha(0.68f));
        graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        graphics.drawText(
            choiceLabels[static_cast<size_t>(group)],
            juce::Rectangle<float>(
                area.getX() + 14.0f, first.getY(), 68.0f, first.getHeight())
                .toNearestInt(),
            juce::Justification::centredLeft);
    }

    ContinuousTelemetrySnapshot telemetry;
    const auto hasTelemetry = readContinuousTelemetry(telemetry)
        && telemetry.valueCount >= VelvetDriveModule::telemetryValueCount;
    const auto driveActivity = hasTelemetry
        ? juce::jlimit(
              0.0f, 1.0f,
              telemetry.values[VelvetDriveModule::preampActivity])
        : 0.0f;
    const auto sag = hasTelemetry
        ? juce::jlimit(
              0.0f, 1.0f,
              telemetry.values[VelvetDriveModule::sagReduction])
        : 0.0f;

    for (const auto control : continuousControls)
    {
        auto bounds = controlBounds(area, control);
        graphics.setColour(juce::Colour(0xff30251e));
        graphics.fillRoundedRectangle(bounds, 6.0f);
        auto amount = value(control);
        auto colour = control == 11
            ? outputColour
            : juce::Colour(0xffd8a15d);
        if (control == 2 || control == 3)
            colour = colour.interpolatedWith(
                juce::Colour(0xffff6d45), driveActivity * 0.55f);
        if (control == 9)
            amount *= 1.0f - 0.28f * sag;
        graphics.setColour(colour.withAlpha(0.82f));
        graphics.fillRoundedRectangle(
            bounds.withWidth(bounds.getWidth() * amount), 6.0f);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        graphics.drawText(
            juce::String(controlMetadata(type, control).label).toUpperCase(),
            bounds.removeFromTop(bounds.getHeight() * 0.50f).toNearestInt(),
            juce::Justification::centred);
        graphics.setFont(juce::FontOptions(10.0f));
        graphics.drawText(
            formatControlValue(type, control, value(control)),
            bounds.toNearestInt(), juce::Justification::centred);
    }
}
} // namespace

std::unique_ptr<ModuleView> createVelvetDriveView(EffectGraph& graph)
{
    return std::make_unique<VelvetDriveModuleView>(graph);
}
} // namespace megadsp::ui
