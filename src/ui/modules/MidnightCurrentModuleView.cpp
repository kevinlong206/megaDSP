#include "../GraphStyle.h"
#include "../ModuleView.h"
#include "../../modules/MidnightCurrent.h"
#include "../../modules/DspHelpers.h"

#include "ModuleViewCreators.h"

#include <cmath>

namespace megadsp::ui
{
namespace
{
class MidnightCurrentModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    bool usesFullPanel() const override { return true; }

    void configureAccessibility(juce::Component& component) const override
    {
        component.setTitle("Midnight Current submerged phase editor");
        component.setHelpText(
            "Drag the main field horizontally to set Filter Center and "
            "vertically to set Filter Sweep. The moving response follows the "
            "actual analog-style filter and phase modulation. Lower tracks edit "
            "motion, phase depth, regeneration, resonance, warmth, softness, "
            "drift, stereo spread, mix, and output.");
    }

    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    static juce::Rectangle<float> fieldBounds(juce::Rectangle<float> area)
    {
        area.removeFromBottom(area.getHeight() * 0.48f);
        return area.reduced(5.0f, 3.0f);
    }

    static juce::Rectangle<float> trackBounds(
        juce::Rectangle<float> area, int track)
    {
        auto tracks = area.removeFromBottom(area.getHeight() * 0.48f);
        const auto column = track % 5;
        const auto row = track / 5;
        const auto width = tracks.getWidth() / 5.0f;
        const auto height = tracks.getHeight() / 2.0f;
        return juce::Rectangle<float>(
            tracks.getX() + width * static_cast<float>(column),
            tracks.getY() + height * static_cast<float>(row),
            width, height).reduced(5.0f, 4.0f);
    }

    static int controlForTrack(int track)
    {
        return track + MidnightCurrentModule::motionRateControl;
    }
};

void MidnightCurrentModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (fieldBounds(area).contains(event.position))
    {
        dragPrimary = MidnightCurrentModule::filterCenterControl;
        dragSecondary = MidnightCurrentModule::filterSweepControl;
    }
    else
    {
        for (int track = 0; track < 10; ++track)
            if (trackBounds(area, track).contains(event.position))
            {
                dragPrimary = controlForTrack(track);
                break;
            }
    }
    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void MidnightCurrentModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (dragPrimary == MidnightCurrentModule::filterCenterControl)
    {
        const auto field = fieldBounds(area);
        setValue(
            dragPrimary,
            juce::jlimit(
                0.0f, 1.0f,
                (event.position.x - field.getX()) / field.getWidth()));
        setValue(
            dragSecondary,
            juce::jlimit(
                0.0f, 1.0f,
                (field.getBottom() - event.position.y) / field.getHeight()));
        dragReadout =
            "CENTER  " + formatControlValue(type, dragPrimary, value(dragPrimary))
            + "  |  SWEEP  "
            + formatControlValue(type, dragSecondary, value(dragSecondary));
        repaint();
        return;
    }

    const auto track =
        juce::jlimit(0, 9, dragPrimary - MidnightCurrentModule::motionRateControl);
    const auto bounds = trackBounds(area, track);
    setValue(
        dragPrimary,
        juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - bounds.getX()) / bounds.getWidth()));
    updateDefaultDragReadout();
}

void MidnightCurrentModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (fieldBounds(area).contains(event.position))
    {
        resetToDefault(MidnightCurrentModule::filterCenterControl);
        resetToDefault(MidnightCurrentModule::filterSweepControl);
        return;
    }
    for (int track = 0; track < 10; ++track)
        if (trackBounds(area, track).contains(event.position))
        {
            resetToDefault(controlForTrack(track));
            return;
        }
}

void MidnightCurrentModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    area = area.reduced(12.0f);
    auto field = fieldBounds(area);
    graphics.setColour(juce::Colour(0xff08131a));
    graphics.fillRoundedRectangle(field, 9.0f);

    ContinuousTelemetrySnapshot telemetry;
    const auto hasTelemetry =
        readContinuousTelemetry(telemetry)
        && telemetry.sequence != 0
        && telemetry.valueCount >= MidnightCurrentModule::telemetryValueCount;
    const auto phase = hasTelemetry
        ? telemetry.values[MidnightCurrentModule::leftPhase] : 0.0f;
    const auto cutoff = hasTelemetry
        ? telemetry.values[MidnightCurrentModule::actualCutoffHz]
        : detail::exponential(
              120.0f, 3000.0f,
              value(MidnightCurrentModule::filterCenterControl));
    const auto cutoffPosition = juce::jlimit(
        0.0f, 1.0f,
        std::log(cutoff / 120.0f) / std::log(3000.0f / 120.0f));
    const auto intensity = hasTelemetry
        ? telemetry.values[MidnightCurrentModule::phaseIntensity]
        : value(MidnightCurrentModule::phaseDepthControl);
    const auto energy = hasTelemetry
        ? juce::jlimit(
              0.0f, 1.0f,
              telemetry.values[MidnightCurrentModule::wetEnergy] * 5.0f)
        : 0.0f;

    auto header = field.removeFromTop(25.0f).reduced(8.0f, 0.0f);
    graphics.setColour(juce::Colours::white.withAlpha(0.88f));
    graphics.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    graphics.drawText(
        "SUBMERGED ANALOG CURRENT", header.toNearestInt(),
        juce::Justification::centredLeft);
    graphics.setColour(accent.withAlpha(0.78f));
    graphics.drawText(
        formatControlValue(
            type, MidnightCurrentModule::filterCenterControl,
            value(MidnightCurrentModule::filterCenterControl))
            + "  /  "
            + formatControlValue(
                type, MidnightCurrentModule::motionRateControl,
                value(MidnightCurrentModule::motionRateControl)),
        header.toNearestInt(), juce::Justification::centredRight);

    const auto response = field.reduced(8.0f, 6.0f);
    for (int line = 1; line < 5; ++line)
    {
        graphics.setColour(juce::Colours::white.withAlpha(0.045f));
        const auto x = response.getX()
            + response.getWidth() * static_cast<float>(line) / 5.0f;
        graphics.drawVerticalLine(
            juce::roundToInt(x), response.getY(), response.getBottom());
    }

    juce::Path responsePath;
    for (int pixel = 0; pixel <= juce::roundToInt(response.getWidth()); ++pixel)
    {
        const auto normalized =
            static_cast<float>(pixel) / response.getWidth();
        const auto lowpass = 1.0f / std::sqrt(
            1.0f + std::pow(
                std::exp2((normalized - cutoffPosition) * 7.0f), 4.0f));
        auto notchAmount = 0.0f;
        for (int notch = 0; notch < 4; ++notch)
        {
            const auto notchCentre = cutoffPosition
                + (static_cast<float>(notch) - 1.5f) * 0.105f
                + std::sin(
                      juce::MathConstants<float>::twoPi * phase
                      + static_cast<float>(notch) * 0.37f)
                    * intensity * 0.10f;
            const auto distance = (normalized - notchCentre) / 0.025f;
            notchAmount += intensity * std::exp(-distance * distance);
        }
        const auto magnitude =
            juce::jlimit(0.0f, 1.0f, lowpass * (1.0f - 0.62f * notchAmount));
        const auto x = response.getX() + static_cast<float>(pixel);
        const auto y = response.getBottom() - magnitude * response.getHeight();
        if (pixel == 0)
            responsePath.startNewSubPath(x, y);
        else
            responsePath.lineTo(x, y);
    }
    graphics.setColour(accent.withAlpha(0.35f + 0.55f * energy));
    graphics.strokePath(
        responsePath, juce::PathStrokeType(1.7f + energy * 1.5f));

    const auto cursorX =
        response.getX() + cutoffPosition * response.getWidth();
    const auto pulse = 4.0f + energy * 8.0f;
    graphics.setColour(outputColour.withAlpha(0.32f + energy * 0.55f));
    graphics.fillEllipse(
        cursorX - pulse, response.getCentreY() - pulse,
        pulse * 2.0f, pulse * 2.0f);

    for (int track = 0; track < 10; ++track)
    {
        const auto control = controlForTrack(track);
        const auto bounds = trackBounds(area, track);
        graphics.setColour(juce::Colour(0xff151d24));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour(
            (control >= MidnightCurrentModule::mixControl
                 ? outputColour : accent).withAlpha(0.64f));
        graphics.fillRoundedRectangle(
            bounds.withWidth(bounds.getWidth() * value(control)), 5.0f);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(8.7f));
        graphics.drawFittedText(
            juce::String(controlMetadata(type, control).label) + "  "
                + formatControlValue(type, control, value(control)),
            bounds.reduced(5.0f, 0.0f).toNearestInt(),
            juce::Justification::centred, 1);
    }
}
} // namespace

std::unique_ptr<ModuleView> createMidnightCurrentView(EffectGraph& graph)
{
    return std::make_unique<MidnightCurrentModuleView>(graph);
}
} // namespace megadsp::ui
