#include "../GraphStyle.h"
#include "../ModuleView.h"
#include "../../modules/ChordLoom.h"

#include "ModuleViewCreators.h"

#include <cmath>

namespace megadsp::ui
{
namespace
{
class ChordLoomModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    bool usesFullPanel() const override { return true; }
    void configureAccessibility(juce::Component& component) const override
    {
        component.setTitle("Chord Loom sustain field");
        component.setHelpText(
            "Strum a note or chord to capture it automatically. The three hold "
            "voices are internal memories, not controls: each new strum uses "
            "the next available voice so chord changes can crossfade smoothly. "
            "The Level and Attack meters compare the incoming signal with the "
            "two detection markers controlled by Sensitivity. Capture, "
            "Transition, Texture, Motion, Octave Bloom, Stereo Spread, "
            "filtering, Dry, Sustain, and Output are edited below.");
    }
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    static juce::Rectangle<float> fieldBounds(juce::Rectangle<float> area)
    {
        area.removeFromBottom(area.getHeight() * 0.58f);
        return area.reduced(5.0f);
    }
    static juce::Rectangle<float> trackBounds(
        juce::Rectangle<float> area, int control)
    {
        auto tracks = area.removeFromBottom(area.getHeight() * 0.58f);
        const auto width = tracks.getWidth() / 4.0f;
        const auto height = tracks.getHeight() / 3.0f;
        return juce::Rectangle<float>(
            tracks.getX() + width * static_cast<float>(control % 4),
            tracks.getY() + height * static_cast<float>(control / 4),
            width, height).reduced(6.0f, 4.0f);
    }
};

void ChordLoomModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int control = 0; control < megadsp::controlsPerSlot; ++control)
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

void ChordLoomModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto track = trackBounds(
        getLocalBounds().toFloat().reduced(12.0f), dragPrimary);
    setValue(dragPrimary, juce::jlimit(
        0.0f, 1.0f,
        (event.position.x - track.getX()) / track.getWidth()));
    updateDefaultDragReadout();
}

void ChordLoomModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int control = 0; control < megadsp::controlsPerSlot; ++control)
        if (trackBounds(area, control).contains(event.position))
        {
            resetToDefault(control);
            return;
        }
}

void ChordLoomModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    area = area.reduced(12.0f);
    auto field = fieldBounds(area);
    graphics.setColour(juce::Colour(0xe6121821));
    graphics.fillRoundedRectangle(field, 8.0f);

    ContinuousTelemetrySnapshot telemetry;
    const auto hasTelemetry =
        readContinuousTelemetry(telemetry)
        && telemetry.sequence != 0
        && telemetry.valueCount >= ChordLoomModule::telemetryValueCount;
    const auto meterPositionForGain = [](float gain)
    {
        const auto decibels =
            juce::Decibels::gainToDecibels(gain, -60.0f);
        return juce::jlimit(0.0f, 1.0f, (decibels + 60.0f) / 60.0f);
    };
    const auto input = hasTelemetry
        ? meterPositionForGain(
              telemetry.values[ChordLoomModule::inputEnvelope])
        : 0.0f;
    const auto inputThreshold = hasTelemetry
        ? meterPositionForGain(
              telemetry.values[ChordLoomModule::inputThreshold])
        : 0.0f;
    const auto onset = hasTelemetry
        ? juce::jlimit(
              0.0f, 1.0f,
              telemetry.values[ChordLoomModule::onsetStrength] / 7.0f)
        : 0.0f;
    const auto onsetThreshold = hasTelemetry
        ? juce::jlimit(
              0.0f, 1.0f,
              telemetry.values[ChordLoomModule::onsetThreshold] / 7.0f)
        : 0.0f;
    const auto triggered = hasTelemetry
        && telemetry.values[ChordLoomModule::captureTriggered] > 0.5f;
    const auto capture = hasTelemetry
        ? telemetry.values[ChordLoomModule::captureProgress] : 0.0f;
    const auto wet = hasTelemetry
        ? juce::jlimit(
              0.0f, 1.0f,
              telemetry.values[ChordLoomModule::wetEnvelope] * 3.0f)
        : 0.0f;

    auto header = field.removeFromTop(24.0f).reduced(8.0f, 0.0f);
    graphics.setColour(juce::Colours::white.withAlpha(0.90f));
    graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    graphics.drawText(
        "STRUM TO HOLD  |  3 AUTOMATIC VOICES SMOOTH CHORD CHANGES",
        header.toNearestInt(), juce::Justification::centredLeft);

    const auto centreY = field.getCentreY();
    for (int layer = 0; layer < ChordLoomModule::layerCount; ++layer)
    {
        const auto gain = hasTelemetry
            ? telemetry.values[static_cast<size_t>(
                  ChordLoomModule::layerGain0 + layer)]
            : 0.0f;
        const auto x = field.getX()
            + field.getWidth() * static_cast<float>(layer + 1)
                / static_cast<float>(ChordLoomModule::layerCount + 1);
        const auto radius = 18.0f + gain * 18.0f;
        graphics.setColour(accent.withAlpha(0.12f + gain * 0.38f));
        graphics.fillEllipse(
            x - radius, centreY - radius, radius * 2.0f, radius * 2.0f);
        graphics.setColour(outputColour.withAlpha(0.45f + gain * 0.50f));
        graphics.drawEllipse(
            x - radius, centreY - radius, radius * 2.0f, radius * 2.0f,
            1.5f + gain * 2.0f);
        for (int strand = 0; strand < 4; ++strand)
        {
            const auto phase = static_cast<float>(strand) * 0.25f
                               + wet * 0.08f;
            const auto angle =
                phase * juce::MathConstants<float>::twoPi;
            graphics.drawLine(
                x, centreY,
                x + std::cos(angle) * radius * 0.82f,
                centreY + std::sin(angle) * radius * 0.82f, 1.2f);
        }
        graphics.setColour(juce::Colours::white.withAlpha(0.72f));
        graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        graphics.drawText(
            "HOLD " + juce::String(layer + 1),
            juce::Rectangle<float>(
                x - 32.0f, centreY + radius + 3.0f, 64.0f, 14.0f)
                .toNearestInt(),
            juce::Justification::centred);
    }

    auto meters = field.removeFromBottom(18.0f).reduced(8.0f, 2.0f);
    const auto meterWidth = meters.getWidth() / 3.0f;
    const std::array<float, 3> meterValues { input, onset, capture };
    const std::array<float, 3> meterThresholds {
        inputThreshold, onsetThreshold, -1.0f
    };
    const std::array<const char*, 3> meterLabels {
        "LEVEL", triggered ? "TRIGGERED" : "ATTACK", "CAPTURE"
    };
    for (int meter = 0; meter < 3; ++meter)
    {
        auto bounds = juce::Rectangle<float>(
            meters.getX() + meterWidth * static_cast<float>(meter),
            meters.getY(), meterWidth, meters.getHeight()).reduced(4.0f, 0.0f);
        graphics.setColour(juce::Colour(0xff2d3745));
        graphics.fillRoundedRectangle(bounds, 3.0f);
        graphics.setColour(
            (meter == 1 ? accent : outputColour)
                .withAlpha(triggered && meter < 2 ? 1.0f : 0.72f));
        graphics.fillRoundedRectangle(
            bounds.withWidth(
                bounds.getWidth()
                * juce::jlimit(
                    0.0f, 1.0f,
                    meterValues[static_cast<size_t>(meter)])),
            3.0f);
        const auto threshold =
            meterThresholds[static_cast<size_t>(meter)];
        if (threshold >= 0.0f)
        {
            const auto thresholdX =
                bounds.getX() + bounds.getWidth() * threshold;
            graphics.setColour(juce::Colours::white.withAlpha(0.90f));
            graphics.drawVerticalLine(
                juce::roundToInt(thresholdX),
                bounds.getY() + 1.0f, bounds.getBottom() - 1.0f);
        }
        graphics.setColour(juce::Colours::white.withAlpha(0.82f));
        graphics.setFont(juce::FontOptions(8.5f, juce::Font::bold));
        graphics.drawText(
            meterLabels[static_cast<size_t>(meter)],
            bounds.toNearestInt(), juce::Justification::centred);
    }

    for (int control = 0; control < megadsp::controlsPerSlot; ++control)
    {
        const auto bounds = trackBounds(area, control);
        graphics.setColour(juce::Colour(0xff151b24));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour((control >= 9 ? outputColour : accent)
                               .withAlpha(0.58f));
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

std::unique_ptr<ModuleView> createChordLoomView(EffectGraph& graph)
{
    return std::make_unique<ChordLoomModuleView>(graph);
}
} // namespace megadsp::ui
