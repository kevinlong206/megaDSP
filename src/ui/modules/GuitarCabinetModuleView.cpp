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
constexpr std::array<int, 7> editableControls { 1, 2, 3, 4, 5, 6, 7 };

class GuitarCabinetModuleView final : public ModuleView
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
    static juce::Rectangle<float> selectorBounds(juce::Rectangle<float>);
    static juce::Rectangle<float> waveformBounds(juce::Rectangle<float>);
    static juce::Rectangle<float> trackBounds(
        juce::Rectangle<float>, int);
    void selectBuiltIn(int);
    void showBuiltInMenu();
};

void GuitarCabinetModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Guitar Cabinet impulse response editor");
    component.setHelpText(
        "Click the cabinet name to choose from all 23 bundled cabinet "
        "impulses, or use the previous and next arrows. You can also load "
        "and drop a WAV, AIFF, or FLAC cabinet IR. Drag the Low Cut, High "
        "Cut, Body, Presence, Drive, Mix, and Output tracks. Selecting a "
        "bundled cabinet replaces the current user IR.");
}

juce::Rectangle<float> GuitarCabinetModuleView::selectorBounds(
    juce::Rectangle<float> area)
{
    return area.removeFromTop(38.0f).reduced(4.0f, 2.0f);
}

juce::Rectangle<float> GuitarCabinetModuleView::waveformBounds(
    juce::Rectangle<float> area)
{
    area.removeFromTop(42.0f);
    area.removeFromBottom(104.0f);
    return area.reduced(4.0f, 3.0f);
}

juce::Rectangle<float> GuitarCabinetModuleView::trackBounds(
    juce::Rectangle<float> area, int control)
{
    auto tracks = area.removeFromBottom(100.0f);
    auto position = std::find(
        editableControls.begin(), editableControls.end(), control);
    if (position == editableControls.end())
        return {};
    const auto index =
        static_cast<int>(std::distance(editableControls.begin(), position));
    const auto row = index < 4 ? 0 : 1;
    const auto column = row == 0 ? index : index - 4;
    const auto columns = row == 0 ? 4 : 3;
    const auto width = tracks.getWidth() / static_cast<float>(columns);
    return juce::Rectangle<float>(
        tracks.getX() + width * static_cast<float>(column),
        tracks.getY() + 50.0f * static_cast<float>(row),
        width, 50.0f).reduced(5.0f, 4.0f);
}

void GuitarCabinetModuleView::selectBuiltIn(int index)
{
    index = (index + GuitarCabinetModule::builtInCount)
        % GuitarCabinetModule::builtInCount;
    if (auto* target = parameter(0))
    {
        graph.focusKeyboardControl(0);
        target->beginChangeGesture();
        target->setValueNotifyingHost(discreteValue(
            index, GuitarCabinetModule::builtInCount));
        target->endChangeGesture();
    }
    repaint();
}

void GuitarCabinetModuleView::showBuiltInMenu()
{
    auto* target = parameter(0);
    if (target == nullptr)
        return;

    const auto current = discreteIndex(
        value(0), GuitarCabinetModule::builtInCount);
    const auto options = controlOptions(type, 0);
    juce::PopupMenu menu;
    for (int index = 0; index < options.size(); ++index)
        menu.addItem(
            index + 1, options[index], true, index == current);

    graph.focusKeyboardControl(0);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&graph),
        [target](int result)
        {
            if (result <= 0)
                return;
            target->beginChangeGesture();
            target->setValueNotifyingHost(discreteValue(
                result - 1, GuitarCabinetModule::builtInCount));
            target->endChangeGesture();
        });
}

void GuitarCabinetModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    auto selector = selectorBounds(area);
    if (selector.contains(event.position))
    {
        const auto current = discreteIndex(
            value(0), GuitarCabinetModule::builtInCount);
        if (event.position.x < selector.getX() + 42.0f)
            selectBuiltIn(current - 1);
        else if (event.position.x > selector.getRight() - 42.0f)
            selectBuiltIn(current + 1);
        else
            showBuiltInMenu();
        return;
    }
    for (const auto control : editableControls)
        if (trackBounds(area, control).contains(event.position))
        {
            dragPrimary = control;
            break;
        }
    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void GuitarCabinetModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto track = trackBounds(area, dragPrimary);
    setValue(dragPrimary, juce::jlimit(
        0.0f, 1.0f,
        (event.position.x - track.getX()) / track.getWidth()));
    updateDefaultDragReadout();
    repaint();
}

void GuitarCabinetModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (const auto control : editableControls)
        if (trackBounds(area, control).contains(event.position))
        {
            resetToDefault(control);
            repaint();
            return;
        }
}

void GuitarCabinetModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    auto selector = selectorBounds(area);
    const auto waveform = waveformBounds(area);
    const auto external = processor.impulseResponsePath(slot).isNotEmpty();
    const auto selected = discreteIndex(
        value(0), GuitarCabinetModule::builtInCount);
    auto name = processor.impulseResponseName(slot);
    if (name.isEmpty())
        name = formatControlValue(type, 0, value(0));

    graphics.setColour(juce::Colour(0xff202a35));
    graphics.fillRoundedRectangle(selector, 7.0f);
    graphics.setColour(accent);
    graphics.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    graphics.drawText("<", selector.removeFromLeft(42.0f).toNearestInt(),
                      juce::Justification::centred);
    graphics.drawText(">", selector.removeFromRight(42.0f).toNearestInt(),
                      juce::Justification::centred);
    graphics.setColour(juce::Colours::white);
    graphics.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    graphics.drawText(
        (external ? "USER  " : "BUNDLED  ") + name,
        selector.toNearestInt(), juce::Justification::centred);
    graphics.setColour(juce::Colours::white.withAlpha(0.65f));
    graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    graphics.drawText(
        "v", selector.removeFromRight(18.0f).toNearestInt(),
        juce::Justification::centred);

    graphics.setColour(juce::Colour(0xff141b23));
    graphics.fillRoundedRectangle(waveform, 8.0f);
    const auto preview = processor.impulseResponsePreview(slot);
    juce::Path path;
    const auto centre = waveform.getCentreY();
    for (size_t point = 0; point < preview.size(); ++point)
    {
        const auto x = waveform.getX()
            + waveform.getWidth() * static_cast<float>(point)
                / static_cast<float>(preview.size() - 1);
        const auto magnitude = juce::jlimit(0.0f, 1.0f, preview[point]);
        const auto y = centre
            - magnitude * waveform.getHeight() * 0.42f;
        if (point == 0)
            path.startNewSubPath(x, centre);
        path.lineTo(x, y);
    }
    graphics.setColour(accent.withAlpha(0.85f));
    graphics.strokePath(path, juce::PathStrokeType(1.7f));
    graphics.setColour(juce::Colours::white.withAlpha(0.45f));
    graphics.setFont(juce::FontOptions(10.0f));
    graphics.drawText(
        external
            ? "Loaded user IR - choose a bundled cabinet above to return"
            : juce::String(selected + 1) + " / "
                + juce::String(GuitarCabinetModule::builtInCount)
                + " bundled cabinet impulses",
        waveform.reduced(10.0f).toNearestInt(),
        juce::Justification::centredBottom);

    ContinuousTelemetrySnapshot telemetry;
    if (readContinuousTelemetry(telemetry)
        && telemetry.valueCount >= GuitarCabinetModule::telemetryValueCount)
    {
        const auto activity = juce::jlimit(
            0.0f, 1.0f,
            telemetry.values[GuitarCabinetModule::wetLevel] * 3.0f);
        graphics.setColour(accent.withAlpha(0.08f + activity * 0.18f));
        graphics.fillRoundedRectangle(waveform.reduced(2.0f), 7.0f);
    }

    for (const auto control : editableControls)
    {
        auto track = trackBounds(area, control);
        const auto amount = value(control);
        graphics.setColour(juce::Colour(0xff202a35));
        graphics.fillRoundedRectangle(track, 5.0f);
        graphics.setColour(accent.withAlpha(0.72f));
        graphics.fillRoundedRectangle(
            track.withWidth(track.getWidth() * amount), 5.0f);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        graphics.drawText(
            juce::String(moduleDefinition(type)
                .controls[static_cast<size_t>(control)].name).toUpperCase(),
            track.removeFromTop(track.getHeight() * 0.48f).toNearestInt(),
            juce::Justification::centred);
        graphics.setFont(juce::FontOptions(10.0f));
        graphics.drawText(
            formatControlValue(type, control, amount),
            track.toNearestInt(), juce::Justification::centred);
    }
}
} // namespace

std::unique_ptr<ModuleView> createGuitarCabinetView(EffectGraph& graph)
{
    return std::make_unique<GuitarCabinetModuleView>(graph);
}
} // namespace megadsp::ui
