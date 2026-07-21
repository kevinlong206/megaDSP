#include "ModuleView.h"

#include "GraphStyle.h"
#include "GuiLayout.h"

#include <algorithm>

namespace megadsp::ui
{
ModuleView::ModuleView(EffectGraph& graphToUse)
    : graph(graphToUse), processor(graph.moduleProcessor()),
      slot(graph.moduleSlot()), type(graph.moduleType()),
      inputSamples(graph.inputSampleData()),
      outputSamples(graph.outputSampleData()),
      stereoLeftSamples(graph.stereoLeftSampleData()),
      stereoRightSamples(graph.stereoRightSampleData()),
      gainHistory(graph.gainHistoryData()),
      inputLevels(graph.inputLevelData()),
      outputLevels(graph.outputLevelData()),
      gainReductionLevels(graph.gainReductionLevelData()),
      inputSpectrum(graph.inputSpectrumData()),
      outputSpectrum(graph.outputSpectrumData()),
      dragPrimary(graph.primaryDragControl()),
      dragSecondary(graph.secondaryDragControl()),
      dragReadout(graph.dragValueReadout())
{
}

float ModuleView::value(int control) const
{
    return graph.value(control);
}

juce::RangedAudioParameter* ModuleView::parameter(int control) const
{
    return graph.parameter(control);
}

void ModuleView::setValue(int control, float normalized)
{
    graph.setValue(control, normalized);
}

void ModuleView::beginGestures()
{
    graph.beginGestures();
}

void ModuleView::repaint()
{
    graph.repaint();
}

juce::Rectangle<int> ModuleView::getLocalBounds() const
{
    return graph.getLocalBounds();
}

std::vector<int> ModuleView::keyboardControls() const
{
    std::array<float, controlsPerSlot> values {};
    for (int control = 0; control < controlsPerSlot; ++control)
        values[static_cast<size_t>(control)] = value(control);
    return keyboardControlOrder(
        type, values, processor.hasStereoOutput(),
        processor.hasExternalSidechain());
}

ControlKind ModuleView::keyboardKind(int control) const
{
    return keyboardControlKind(type, control);
}

int ModuleView::keyboardOptionCount(int control) const
{
    return keyboardControlOptionCount(type, control);
}

juce::String ModuleView::keyboardLabel(int control) const
{
    return keyboardControlLabel(type, control);
}

juce::String ModuleView::keyboardValueText(int control) const
{
    return keyboardControlValueText(type, control, value(control));
}

bool ModuleView::isKeyboardControlAvailable(int control) const
{
    const auto controls = keyboardControls();
    return std::find(controls.begin(), controls.end(), control)
           != controls.end();
}

bool ModuleView::setKeyboardNormalizedValue(
    int control, float normalized)
{
    if (!isKeyboardControlAvailable(control))
        return false;
    auto* target = parameter(control);
    if (target == nullptr)
        return false;
    target->beginChangeGesture();
    target->setValueNotifyingHost(
        juce::jlimit(0.0f, 1.0f, normalized));
    target->endChangeGesture();
    repaint();
    return true;
}

bool ModuleView::adjustKeyboardControl(
    int control, int direction, bool fine)
{
    if (direction == 0 || !isKeyboardControlAvailable(control))
        return false;
    const auto kind = keyboardKind(control);
    if (kind == ControlKind::choice)
    {
        const auto count = keyboardOptionCount(control);
        if (count <= 0)
            return false;
        const auto current = discreteIndex(value(control), count);
        const auto next =
            (current + (direction > 0 ? 1 : count - 1)) % count;
        return setKeyboardNormalizedValue(
            control, discreteValue(next, count));
    }
    if (kind == ControlKind::toggle)
        return setKeyboardNormalizedValue(
            control, direction > 0 ? 1.0f : 0.0f);
    const auto step = fine ? 0.002f : 0.01f;
    return setKeyboardNormalizedValue(
        control, value(control)
                     + static_cast<float>(direction) * step);
}

bool ModuleView::pressKeyboardControl(int control)
{
    if (!isKeyboardControlAvailable(control))
        return false;
    const auto kind = keyboardKind(control);
    if (kind == ControlKind::toggle)
        return setKeyboardNormalizedValue(
            control, value(control) >= 0.5f ? 0.0f : 1.0f);
    if (kind == ControlKind::choice)
        return adjustKeyboardControl(control, 1, false);
    return false;
}

bool ModuleView::resetKeyboardControl(int control)
{
    if (!isKeyboardControlAvailable(control))
        return false;
    return setKeyboardNormalizedValue(
        control, moduleDefaults(type)[static_cast<size_t>(control)]);
}

double ModuleView::keyboardAccessibilityValue(int control) const
{
    const auto kind = keyboardKind(control);
    if (kind == ControlKind::choice)
        return static_cast<double>(
            discreteIndex(value(control), keyboardOptionCount(control)));
    if (kind == ControlKind::toggle)
        return value(control) >= 0.5f ? 1.0 : 0.0;
    return static_cast<double>(value(control));
}

double ModuleView::keyboardAccessibilityMaximum(int control) const
{
    if (keyboardKind(control) == ControlKind::choice)
        return static_cast<double>(
            juce::jmax(1, keyboardOptionCount(control) - 1));
    return 1.0;
}

double ModuleView::keyboardAccessibilityInterval(int control) const
{
    return keyboardKind(control) == ControlKind::choice
               || keyboardKind(control) == ControlKind::toggle
        ? 1.0 : 0.01;
}

bool ModuleView::setKeyboardAccessibilityValue(
    int control, double newValue)
{
    if (!isKeyboardControlAvailable(control))
        return false;
    const auto kind = keyboardKind(control);
    if (kind == ControlKind::choice)
    {
        const auto count = keyboardOptionCount(control);
        return count > 0 && setKeyboardNormalizedValue(
            control, discreteValue(
                         juce::roundToInt(newValue), count));
    }
    if (kind == ControlKind::toggle)
        return setKeyboardNormalizedValue(
            control, newValue >= 0.5 ? 1.0f : 0.0f);
    return setKeyboardNormalizedValue(
        control, static_cast<float>(newValue));
}

bool ModuleView::setKeyboardAccessibilityValueAsString(
    int control, const juce::String& text)
{
    if (!isKeyboardControlAvailable(control))
        return false;
    const auto kind = keyboardKind(control);
    if (kind == ControlKind::toggle)
    {
        const auto query = text.trim();
        for (const auto state : { false, true })
        {
            const auto presentation =
                togglePresentation(type, control, state);
            if (query.equalsIgnoreCase(presentation.stateText)
                || query.equalsIgnoreCase(presentation.buttonText)
                || query.equalsIgnoreCase(state ? "On" : "Off"))
                return setKeyboardNormalizedValue(
                    control, state ? 1.0f : 0.0f);
        }
        return false;
    }
    if (kind == ControlKind::choice)
    {
        const auto options = controlOptions(type, control);
        for (int index = 0; index < options.size(); ++index)
            if (text.trim().equalsIgnoreCase(options[index]))
                return setKeyboardNormalizedValue(
                    control, discreteValue(index, options.size()));
        return false;
    }
    if (const auto parsed = parseControlValue(type, control, text))
        return setKeyboardNormalizedValue(control, *parsed);
    return false;
}

void ModuleView::toggleParameter(int control)
{
    if (auto* target = parameter(control))
    {
        graph.focusKeyboardControl(control);
        target->beginChangeGesture();
        target->setValueNotifyingHost(
            target->getValue() > 0.5f ? 0.0f : 1.0f);
        target->endChangeGesture();
        repaint();
    }
}

void ModuleView::cycleChoice(int control, int optionCount)
{
    if (auto* target = parameter(control))
    {
        graph.focusKeyboardControl(control);
        const auto next =
            (discreteIndex(target->getValue(), optionCount) + 1)
            % optionCount;
        target->beginChangeGesture();
        target->setValueNotifyingHost(discreteValue(next, optionCount));
        target->endChangeGesture();
        repaint();
    }
}

void ModuleView::updateDefaultDragReadout()
{
    dragReadout = juce::String(controlMetadata(type, dragPrimary).label)
        + "  " + formatControlValue(type, dragPrimary, value(dragPrimary));
    repaint();
}

float ModuleView::dbToY(float db, juce::Rectangle<float> area)
{
    return area.getBottom()
           - juce::jlimit(0.0f, 1.0f, (db + 24.0f) / 24.0f)
                 * area.getHeight();
}

void ModuleView::drawSpectrum(
    juce::Graphics& graphics, juce::Rectangle<float> area,
    const std::array<float, graphFftSize / 2>& spectrum,
    juce::Colour colour)
{
    juce::Path path;
    const auto rate = juce::jmax(8000.0, processor.getSampleRate());
    bool started = false;
    for (int pixel = 0; pixel < static_cast<int>(area.getWidth()); pixel += 2)
    {
        const auto proportion = static_cast<float>(pixel) / area.getWidth();
        const auto frequency = 20.0f * std::pow(1000.0f, proportion);
        const auto bin = juce::jlimit(
            0, graphFftSize / 2 - 1,
            juce::roundToInt(
                frequency * graphFftSize / static_cast<float>(rate)));
        const auto db = spectrum[static_cast<size_t>(bin)];
        const auto y = juce::jmap(
            db, -80.0f, 6.0f, area.getBottom(), area.getY());
        if (!started)
        {
            path.startNewSubPath(
                area.getX() + static_cast<float>(pixel), y);
            started = true;
        }
        else
            path.lineTo(area.getX() + static_cast<float>(pixel), y);
    }
    graphics.setColour(colour.withAlpha(0.65f));
    graphics.strokePath(path, juce::PathStrokeType(1.5f));
}

void ModuleView::drawWaveforms(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    auto draw = [&](const auto& samples, juce::Colour colour)
    {
        juce::Path path;
        for (int pixel = 0; pixel < static_cast<int>(area.getWidth()); ++pixel)
        {
            const auto index = static_cast<size_t>(
                static_cast<float>(pixel)
                * static_cast<float>(samples.size())
                / juce::jmax(1.0f, area.getWidth()));
            const auto y = area.getCentreY()
                - samples[juce::jmin(index, samples.size() - 1)]
                    * area.getHeight() * 0.45f;
            if (pixel == 0)
                path.startNewSubPath(area.getX(), y);
            else
                path.lineTo(
                    area.getX() + static_cast<float>(pixel), y);
        }
        graphics.setColour(colour.withAlpha(0.75f));
        graphics.strokePath(path, juce::PathStrokeType(1.3f));
    };
    draw(inputSamples, inputColour);
    draw(outputSamples, outputColour);
}

void ModuleView::drawLevelHistory(
    juce::Graphics& graphics, juce::Rectangle<float> area,
    const std::array<float, graphLevelHistorySize>& levels,
    juce::Colour colour)
{
    juce::Path path;
    for (size_t index = 0; index < levels.size(); ++index)
    {
        const auto x = area.getX()
            + static_cast<float>(index)
                / static_cast<float>(levels.size() - 1)
                * area.getWidth();
        const auto y = juce::jmap(
            juce::jlimit(-60.0f, 0.0f, levels[index]),
            -60.0f, 0.0f, area.getBottom(), area.getY());
        if (index == 0)
            path.startNewSubPath(x, y);
        else
            path.lineTo(x, y);
    }
    graphics.setColour(colour);
    graphics.strokePath(path, juce::PathStrokeType(2.0f));
}

void ModuleView::drawGainReductionOverlay(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto current = juce::jlimit(
        0.0f, 24.0f, processor.getRack().slotMeter(slot));
    const auto maximum = juce::jlimit(
        0.0f, 24.0f,
        *std::max_element(
            gainReductionLevels.begin(), gainReductionLevels.end()));
    const auto maximumY =
        area.getY() + maximum / 24.0f * area.getHeight();
    const float dashLengths[] { 5.0f, 4.0f };
    graphics.setColour(reductionColour);
    graphics.drawDashedLine(
        juce::Line<float>(
            area.getX(), maximumY, area.getRight(), maximumY),
        dashLengths, 2, 1.2f);

    auto meter = area.removeFromRight(24.0f).reduced(3.0f, 8.0f);
    graphics.setColour(juce::Colour(0xdd11161d));
    graphics.fillRoundedRectangle(meter, 3.0f);
    auto fill = meter;
    fill.setHeight(meter.getHeight() * current / 24.0f);
    graphics.setColour(reductionColour.withAlpha(0.88f));
    graphics.fillRoundedRectangle(fill, 3.0f);
    graphics.setColour(juce::Colours::white);
    graphics.drawText(
        "GR " + juce::String(current, 1) + " dB",
        area.removeFromTop(18.0f).removeFromRight(92.0f).toNearestInt(),
        juce::Justification::centredRight);
    graphics.setColour(reductionColour);
    graphics.drawText(
        "10s MAX " + juce::String(maximum, 1) + " dB",
        juce::Rectangle<float>(
            area.getX() + 4.0f, maximumY - 18.0f, 130.0f, 18.0f)
            .toNearestInt(),
        juce::Justification::centredLeft);
}
} // namespace megadsp::ui
