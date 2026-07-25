#include "../GraphStyle.h"
#include "../ModuleView.h"
#include "../../modules/VolumeSwell.h"

#include "ModuleViewCreators.h"

#include <cmath>

namespace megadsp::ui
{
namespace
{
class VolumeSwellModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    bool usesFullPanel() const override { return true; }

    void configureAccessibility(juce::Component& component) const override
    {
        component.setTitle("Volume Swell envelope");
        component.setHelpText(
            "Each detected chord fades down before its attack reaches the "
            "output, then rises along the displayed swell curve. The Level and "
            "Attack meters show the live detector against both thresholds.");
    }

    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    static constexpr int editableControls = 6;

    static juce::Rectangle<float> fieldBounds(juce::Rectangle<float> area)
    {
        area.removeFromBottom(area.getHeight() * 0.42f);
        return area.reduced(5.0f);
    }

    static juce::Rectangle<float> trackBounds(
        juce::Rectangle<float> area, int control)
    {
        auto tracks = area.removeFromBottom(area.getHeight() * 0.42f);
        const auto width = tracks.getWidth() / 3.0f;
        const auto height = tracks.getHeight() / 2.0f;
        return juce::Rectangle<float>(
            tracks.getX() + width * static_cast<float>(control % 3),
            tracks.getY() + height * static_cast<float>(control / 3),
            width, height).reduced(6.0f, 5.0f);
    }

    static float curveExponent(float normalized)
    {
        return normalized < 0.5f
            ? 0.35f + normalized * 2.0f * 0.65f
            : 1.0f + (normalized - 0.5f) * 4.0f;
    }
};

void VolumeSwellModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int control = 0; control < editableControls; ++control)
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

void VolumeSwellModuleView::mouseDrag(const juce::MouseEvent& event)
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

void VolumeSwellModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int control = 0; control < editableControls; ++control)
        if (trackBounds(area, control).contains(event.position))
        {
            resetToDefault(control);
            return;
        }
}

void VolumeSwellModuleView::paint(
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
        && telemetry.valueCount >= VolumeSwellModule::telemetryValueCount;
    const auto progress = hasTelemetry
        ? juce::jlimit(
              0.0f, 1.0f,
              telemetry.values[VolumeSwellModule::swellProgress])
        : 1.0f;
    const auto envelopeGain = hasTelemetry
        ? juce::jlimit(
              0.0f, 1.0f,
              telemetry.values[VolumeSwellModule::swellGain])
        : 1.0f;
    const auto wasTriggered = hasTelemetry
        && telemetry.values[VolumeSwellModule::triggered] > 0.5f;

    auto header = field.removeFromTop(24.0f).reduced(8.0f, 0.0f);
    graphics.setColour(juce::Colours::white.withAlpha(0.90f));
    graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    graphics.drawText(
        wasTriggered ? "NEW CHORD  |  SWELL RESTARTED"
                     : "STRUM TO RETRIGGER THE VOLUME ENVELOPE",
        header.toNearestInt(), juce::Justification::centredLeft);

    auto meters = field.removeFromBottom(20.0f).reduced(8.0f, 2.0f);
    auto plot = field.reduced(14.0f, 8.0f);
    graphics.setColour(juce::Colours::white.withAlpha(0.08f));
    graphics.drawHorizontalLine(
        juce::roundToInt(plot.getBottom()), plot.getX(), plot.getRight());
    graphics.drawVerticalLine(
        juce::roundToInt(plot.getX()), plot.getY(), plot.getBottom());

    const auto floorGain = juce::Decibels::decibelsToGain(
        -60.0f + value(3) * 54.0f);
    const auto exponent = curveExponent(value(2));
    juce::Path curvePath;
    constexpr int curvePoints = 96;
    for (int point = 0; point <= curvePoints; ++point)
    {
        const auto x = static_cast<float>(point)
            / static_cast<float>(curvePoints);
        const auto gain =
            floorGain + (1.0f - floorGain) * std::pow(x, exponent);
        const auto position = juce::Point<float>(
            plot.getX() + x * plot.getWidth(),
            plot.getBottom() - gain * plot.getHeight());
        if (point == 0)
            curvePath.startNewSubPath(position);
        else
            curvePath.lineTo(position);
    }
    graphics.setColour(accent.withAlpha(0.88f));
    graphics.strokePath(
        curvePath, juce::PathStrokeType(
            2.5f, juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded));
    const auto head = juce::Point<float>(
        plot.getX() + progress * plot.getWidth(),
        plot.getBottom() - envelopeGain * plot.getHeight());
    graphics.setColour(outputColour);
    graphics.fillEllipse(
        juce::Rectangle<float>(12.0f, 12.0f).withCentre(head));

    const auto meterPositionForGain = [](float gain)
    {
        const auto decibels =
            juce::Decibels::gainToDecibels(gain, -60.0f);
        return juce::jlimit(0.0f, 1.0f, (decibels + 60.0f) / 60.0f);
    };
    const auto level = hasTelemetry
        ? meterPositionForGain(
              telemetry.values[VolumeSwellModule::inputEnvelope])
        : 0.0f;
    const auto levelThreshold = hasTelemetry
        ? meterPositionForGain(
              telemetry.values[VolumeSwellModule::inputThreshold])
        : 0.0f;
    const auto attack = hasTelemetry
        ? juce::jlimit(
              0.0f, 1.0f,
              telemetry.values[VolumeSwellModule::onsetStrength] / 2.0f)
        : 0.0f;
    const auto attackThreshold = hasTelemetry
        ? juce::jlimit(
              0.0f, 1.0f,
              telemetry.values[VolumeSwellModule::onsetThreshold] / 2.0f)
        : 0.0f;
    const std::array<float, 2> meterValues { level, attack };
    const std::array<float, 2> meterThresholds {
        levelThreshold, attackThreshold
    };
    const std::array<const char*, 2> meterLabels { "LEVEL", "ATTACK" };
    const auto meterWidth = meters.getWidth() * 0.5f;
    for (int meter = 0; meter < 2; ++meter)
    {
        const auto bounds = juce::Rectangle<float>(
            meters.getX() + meterWidth * static_cast<float>(meter),
            meters.getY(), meterWidth, meters.getHeight()).reduced(4.0f, 0.0f);
        graphics.setColour(juce::Colour(0xff2d3745));
        graphics.fillRoundedRectangle(bounds, 3.0f);
        graphics.setColour(
            (meter == 0 ? outputColour : accent).withAlpha(0.76f));
        graphics.fillRoundedRectangle(
            bounds.withWidth(
                bounds.getWidth()
                * meterValues[static_cast<size_t>(meter)]),
            3.0f);
        const auto thresholdX = bounds.getX() + bounds.getWidth()
            * meterThresholds[static_cast<size_t>(meter)];
        graphics.setColour(juce::Colours::white.withAlpha(0.90f));
        graphics.drawVerticalLine(
            juce::roundToInt(thresholdX),
            bounds.getY() + 1.0f, bounds.getBottom() - 1.0f);
        graphics.setFont(juce::FontOptions(8.5f, juce::Font::bold));
        graphics.drawText(
            meterLabels[static_cast<size_t>(meter)],
            bounds.toNearestInt(), juce::Justification::centred);
    }

    for (int control = 0; control < editableControls; ++control)
    {
        const auto bounds = trackBounds(area, control);
        graphics.setColour(juce::Colour(0xff151b24));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour((control >= 4 ? outputColour : accent)
                               .withAlpha(0.60f));
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

std::unique_ptr<ModuleView> createVolumeSwellView(EffectGraph& graph)
{
    return std::make_unique<VolumeSwellModuleView>(graph);
}
} // namespace megadsp::ui
