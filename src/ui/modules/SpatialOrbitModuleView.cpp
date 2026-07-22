#include "../GraphStyle.h"
#include "../ModuleView.h"
#include "../../modules/SpatialOrbit.h"

#include "ModuleViewCreators.h"

#include <array>
#include <cmath>

namespace megadsp::ui
{
namespace
{
class SpatialOrbitModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    bool usesFullPanel() const override { return true; }
    void configureAccessibility(juce::Component& component) const override
    {
        component.setTitle("Spatial Orbit source field");
        component.setHelpText(
            "Choose a path, then drag the source field horizontally for "
            "Azimuth Span and vertically for Distance. The field's right rail "
            "edits Width. The source and trail are DSP positions, including "
            "the resettable Wander path. Remaining controls use compact tracks.");
    }
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    static juce::Rectangle<float> pathPill(juce::Rectangle<float> area, int index)
    {
        auto row = area.removeFromTop(28.0f);
        const auto width = row.getWidth() * 0.25f;
        return juce::Rectangle<float>(
            row.getX() + width * static_cast<float>(index), row.getY(),
            width, row.getHeight()).reduced(3.0f, 2.0f);
    }
    static juce::Rectangle<float> fieldBounds(juce::Rectangle<float> area)
    {
        area.removeFromTop(31.0f);
        area.removeFromBottom(area.getHeight() * 0.38f);
        return area.reduced(5.0f);
    }
    static juce::Rectangle<float> fieldWidthBounds(
        juce::Rectangle<float> area)
    {
        auto field = fieldBounds(area);
        field.removeFromTop(26.0f);
        return field.removeFromRight(28.0f).reduced(2.0f, 7.0f);
    }
    int timingControl() const
    {
        return value(2) >= 0.5f ? 3 : 1;
    }
    juce::Rectangle<float> trackBounds(
        juce::Rectangle<float> area, int control) const
    {
        area.removeFromTop(31.0f);
        auto tracks = area.removeFromBottom(area.getHeight() * 0.38f);
        const std::array<int, 7> order {
            timingControl(), 2, 7, 8, 9, 10, 11
        };
        auto position = 0;
        while (position < static_cast<int>(order.size())
               && order[static_cast<size_t>(position)] != control)
            ++position;
        if (position == static_cast<int>(order.size()))
            return {};
        const auto columns = position < 4 ? 4 : 3;
        const auto row = position < 4 ? 0 : 1;
        const auto column = position < 4 ? position : position - 4;
        const auto width = tracks.getWidth() / static_cast<float>(columns);
        const auto height = tracks.getHeight() * 0.5f;
        return juce::Rectangle<float>(
            tracks.getX() + width * static_cast<float>(column),
            tracks.getY() + height * static_cast<float>(row),
            width, height).reduced(6.0f, 4.0f);
    }
};

void SpatialOrbitModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int index = 0; index < 4; ++index)
        if (pathPill(area, index).contains(event.position))
        {
            if (auto* target = parameter(0))
            {
                graph.focusKeyboardControl(0);
                target->beginChangeGesture();
                target->setValueNotifyingHost(discreteValue(index, 4));
                target->endChangeGesture();
            }
            repaint();
            return;
        }
    if (fieldWidthBounds(area).contains(event.position))
        dragPrimary = 5;
    else if (fieldBounds(area).contains(event.position))
    {
        dragPrimary = 4;
        dragSecondary = 6;
    }
    const std::array<int, 7> trackControls {
        timingControl(), 2, 7, 8, 9, 10, 11
    };
    for (const auto control : trackControls)
        if (dragPrimary < 0 && trackBounds(area, control).contains(event.position))
            dragPrimary = control;
    if (dragPrimary < 0)
        return;
    if (dragPrimary == 2)
    {
        toggleParameter(2);
        dragPrimary = -1;
        return;
    }
    if (dragPrimary == 3)
    {
        cycleChoice(3, 8);
        dragPrimary = -1;
        return;
    }
    beginGestures();
    mouseDrag(event);
}

void SpatialOrbitModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (dragPrimary == 4)
    {
        const auto bounds = fieldBounds(area);
        setValue(4, juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - bounds.getX()) / bounds.getWidth()));
        setValue(6, juce::jlimit(
            0.0f, 1.0f,
            (bounds.getBottom() - event.position.y) / bounds.getHeight()));
        dragReadout = "SPAN  " + formatControlValue(type, 4, value(4))
            + "    DISTANCE  " + formatControlValue(type, 6, value(6));
        repaint();
        return;
    }
    const auto bounds = dragPrimary == 5
        ? fieldWidthBounds(area) : trackBounds(area, dragPrimary);
    const auto normalized = dragPrimary == 5
        ? (bounds.getBottom() - event.position.y) / bounds.getHeight()
        : (event.position.x - bounds.getX()) / bounds.getWidth();
    setValue(dragPrimary, juce::jlimit(0.0f, 1.0f, normalized));
    updateDefaultDragReadout();
}

void SpatialOrbitModuleView::mouseDoubleClick(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int index = 0; index < 4; ++index)
        if (pathPill(area, index).contains(event.position))
        {
            resetToDefault(0);
            return;
        }
    if (fieldWidthBounds(area).contains(event.position))
    {
        resetToDefault(5);
        return;
    }
    if (fieldBounds(area).contains(event.position))
    {
        resetToDefault(4);
        resetToDefault(6);
        return;
    }
    const std::array<int, 7> trackControls {
        timingControl(), 2, 7, 8, 9, 10, 11
    };
    for (const auto control : trackControls)
        if (trackBounds(area, control).contains(event.position))
        {
            resetToDefault(control);
            return;
        }
}

void SpatialOrbitModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    static constexpr std::array<const char*, 4> paths {
        "CIRCLE", "FIGURE EIGHT", "PENDULUM", "WANDER"
    };
    const auto selectedPath = discreteIndex(value(0), 4);
    ContinuousTelemetrySnapshot telemetry;
    const auto hasTelemetry =
        readContinuousTelemetry(telemetry)
        && telemetry.sequence != 0
        && telemetry.valueCount
               >= SpatialOrbitModule::telemetryValueCount;
    const auto renderedPath = hasTelemetry
        ? juce::jlimit(
              0, 3, juce::roundToInt(
                        telemetry.values[SpatialOrbitModule::activePath]))
        : selectedPath;
    for (int index = 0; index < 4; ++index)
    {
        const auto bounds = pathPill(area, index);
        graphics.setColour(index == selectedPath
                               ? accent : juce::Colour(0xff27313d));
        graphics.fillRoundedRectangle(bounds, 6.0f);
        graphics.setColour(index == selectedPath
                               ? juce::Colour(0xff101820)
                               : juce::Colours::white);
        graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        graphics.drawText(paths[static_cast<size_t>(index)],
                          bounds.toNearestInt(), juce::Justification::centred);
    }

    const auto field = fieldBounds(area);
    graphics.setColour(juce::Colour(0xe6121821));
    graphics.fillRoundedRectangle(field, 8.0f);
    const auto centre = field.getCentre();
    const auto horizontalRadius = field.getWidth() * 0.42f;
    const auto verticalRadius = field.getHeight() * 0.34f;
    juce::Path path;
    if (hasTelemetry && telemetry.historyCount > 1)
    {
        const auto capacity = static_cast<std::uint32_t>(
            continuousTelemetryHistoryCapacity);
        const auto first = (telemetry.historyWritePosition + capacity
                            - telemetry.historyCount) % capacity;
        for (std::uint32_t point = 0; point < telemetry.historyCount; ++point)
        {
            const auto index = (first + point) % capacity;
            const juce::Point<float> position {
                centre.x
                    + telemetry.history[SpatialOrbitModule::xHistory][index]
                        * horizontalRadius,
                centre.y
                    + telemetry.history[SpatialOrbitModule::yHistory][index]
                        * verticalRadius
            };
            if (point == 0)
                path.startNewSubPath(position);
            else
                path.lineTo(position);
        }
    }
    graphics.setColour(accent.withAlpha(0.44f));
    graphics.strokePath(path, juce::PathStrokeType(1.4f));
    graphics.setColour(juce::Colours::white.withAlpha(0.7f));
    graphics.fillEllipse(juce::Rectangle<float>(18.0f, 18.0f).withCentre(centre));
    graphics.setColour(juce::Colour(0xff101820));
    graphics.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre(centre));

    const auto sourceX = hasTelemetry
        ? telemetry.values[SpatialOrbitModule::xPosition] : 0.0f;
    const auto sourceY = hasTelemetry
        ? telemetry.values[SpatialOrbitModule::yPosition] : 0.0f;
    const auto actualDistance = hasTelemetry
        ? telemetry.values[SpatialOrbitModule::distanceMetres]
        : exponential(0.5f, 10.0f, value(6));
    if (hasTelemetry)
    {
        const juce::Point<float> source {
            centre.x + sourceX * horizontalRadius,
            centre.y + sourceY * verticalRadius
        };
        const auto sourceSize = lerp(
            7.0f, 15.0f,
            1.0f - juce::jlimit(
                       0.0f, 1.0f, (actualDistance - 0.5f) / 9.5f));
        graphics.setColour(outputColour);
        graphics.fillEllipse(
            juce::Rectangle<float>(sourceSize, sourceSize).withCentre(source));
    }
    graphics.setColour(juce::Colours::white);
    graphics.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    graphics.drawText(
        paths[static_cast<size_t>(renderedPath)]
            + (hasTelemetry
                   ? juce::String("  ·  DSP ")
                       + juce::String(sourceX, 2) + ", "
                       + juce::String(sourceY, 2) + "  ·  "
                       + juce::String(actualDistance, 2) + " m"
                   : juce::String("  ·  DSP TELEMETRY WAITING")),
        field.withTrimmedTop(5.0f).removeFromTop(18.0f).toNearestInt(),
        juce::Justification::centred);

    const auto widthRail = fieldWidthBounds(area);
    graphics.setColour(juce::Colour(0xff2d3745));
    graphics.fillRoundedRectangle(widthRail, 4.0f);
    graphics.setColour(outputColour.withAlpha(0.68f));
    graphics.fillRoundedRectangle(
        widthRail.withTop(
            widthRail.getBottom() - widthRail.getHeight() * value(5)),
        4.0f);
    graphics.setColour(juce::Colours::white);
    graphics.setFont(juce::FontOptions(8.5f, juce::Font::bold));
    graphics.drawText(
        "WIDTH",
        widthRail.translated(-38.0f, 0.0f).withWidth(38.0f).toNearestInt(),
        juce::Justification::centredRight);

    const std::array<int, 7> trackControls {
        timingControl(), 2, 7, 8, 9, 10, 11
    };
    for (const auto control : trackControls)
    {
        const auto bounds = trackBounds(area, control);
        graphics.setColour(juce::Colour(0xff151b24));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour((control >= 10 ? outputColour : accent)
                               .withAlpha(0.58f));
        graphics.fillRoundedRectangle(
            bounds.withWidth(bounds.getWidth() * value(control)), 5.0f);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(9.2f));
        const auto label = control == timingControl()
            ? (control == 3 ? juce::String("Division")
                            : juce::String("Rate"))
            : juce::String(controlMetadata(type, control).label);
        const auto valueText = control == 2
            ? juce::String(value(2) >= 0.5f ? "Tempo" : "Free")
            : formatControlValue(type, control, value(control));
        graphics.drawFittedText(
            label + "  " + valueText,
            bounds.reduced(5.0f, 0.0f).toNearestInt(),
            juce::Justification::centred, 1);
    }
}
} // namespace

std::unique_ptr<ModuleView> createSpatialOrbitView(EffectGraph& graph)
{
    return std::make_unique<SpatialOrbitModuleView>(graph);
}
} // namespace megadsp::ui
