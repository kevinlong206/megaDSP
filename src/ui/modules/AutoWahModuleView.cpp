#include "../GraphStyle.h"
#include "../ModuleView.h"
#include "../../modules/AutoWah.h"
#include "../../modules/DspHelpers.h"

#include "ModuleViewCreators.h"

#include <cmath>

namespace megadsp::ui
{
namespace
{
class AutoWahModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    bool usesFullPanel() const override { return true; }

    void configureAccessibility(juce::Component& component) const override
    {
        component.setTitle("Auto Wah pick-responsive filter editor");
        component.setHelpText(
            "Center Frequency and Sweep Range use separate horizontal rails. "
            "The response field is read-only and follows the actual filter. "
            "The detector lane shows input level against Sensitivity.");
    }

    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    static constexpr std::array<int, 12> trackControls {
        AutoWahModule::centerFrequencyControl,
        AutoWahModule::sweepRangeControl,
        AutoWahModule::filterTypeControl,
        AutoWahModule::resonanceControl,
        AutoWahModule::sensitivityControl,
        AutoWahModule::pickFocusControl,
        AutoWahModule::attackControl,
        AutoWahModule::releaseControl,
        AutoWahModule::directionControl,
        AutoWahModule::driveControl,
        AutoWahModule::mixControl,
        AutoWahModule::outputControl
    };

    static juce::Rectangle<float> fieldBounds(juce::Rectangle<float> area)
    {
        area.removeFromBottom(area.getHeight() * 0.62f);
        return area.reduced(5.0f, 3.0f);
    }

    static juce::Rectangle<float> trackBounds(
        juce::Rectangle<float> area, int track)
    {
        auto tracks = area.removeFromBottom(area.getHeight() * 0.62f);
        const auto width = tracks.getWidth() / 4.0f;
        const auto height = tracks.getHeight() / 3.0f;
        return juce::Rectangle<float>(
            tracks.getX() + width * static_cast<float>(track % 4),
            tracks.getY() + height * static_cast<float>(track / 4),
            width, height).reduced(5.0f, 4.0f);
    }
};

void AutoWahModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int track = 0; track < static_cast<int>(trackControls.size());
         ++track)
        if (trackBounds(area, track).contains(event.position))
        {
            dragPrimary = trackControls[static_cast<size_t>(track)];
            break;
        }
    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void AutoWahModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    auto track = 0;
    for (; track < static_cast<int>(trackControls.size()); ++track)
        if (trackControls[static_cast<size_t>(track)] == dragPrimary)
            break;
    const auto bounds = trackBounds(area, juce::jlimit(0, 11, track));
    setValue(
        dragPrimary,
        juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - bounds.getX()) / bounds.getWidth()));
    updateDefaultDragReadout();
}

void AutoWahModuleView::mouseDoubleClick(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int track = 0; track < static_cast<int>(trackControls.size()); ++track)
        if (trackBounds(area, track).contains(event.position))
        {
            resetToDefault(trackControls[static_cast<size_t>(track)]);
            return;
        }
}

void AutoWahModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    auto field = fieldBounds(area);
    graphics.setColour(juce::Colour(0xff121409));
    graphics.fillRoundedRectangle(field, 9.0f);

    ContinuousTelemetrySnapshot telemetry;
    const auto hasTelemetry =
        readContinuousTelemetry(telemetry)
        && telemetry.sequence != 0
        && telemetry.valueCount >= AutoWahModule::telemetryValueCount;
    const auto envelope = hasTelemetry
        ? juce::jlimit(
              0.0f, 1.0f,
              telemetry.values[AutoWahModule::envelopeAmount])
        : 0.0f;
    const auto frequency = hasTelemetry
        ? telemetry.values[AutoWahModule::currentFrequencyHz]
        : detail::exponential(
              120.0f, 1600.0f,
              value(AutoWahModule::centerFrequencyControl));
    const auto frequencyPosition = juce::jlimit(
        0.0f, 1.0f,
        std::log(frequency / 80.0f) / std::log(12000.0f / 80.0f));
    const auto inputDb = hasTelemetry
        ? telemetry.values[AutoWahModule::inputLevelDb] : -100.0f;
    const auto threshold = hasTelemetry
        ? telemetry.values[AutoWahModule::thresholdDb]
        : detail::lerp(
              -18.0f, -66.0f,
              value(AutoWahModule::sensitivityControl));

    auto header = field.removeFromTop(25.0f).reduced(8.0f, 0.0f);
    graphics.setColour(juce::Colours::white.withAlpha(0.9f));
    graphics.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    graphics.drawText(
        "PICK-RESPONSIVE ENVELOPE FILTER", header.toNearestInt(),
        juce::Justification::centredLeft);
    graphics.setColour(accent);
    graphics.drawText(
        formatControlValue(
            type, AutoWahModule::centerFrequencyControl,
            value(AutoWahModule::centerFrequencyControl))
            + "  +  "
            + formatControlValue(
                type, AutoWahModule::sweepRangeControl,
                value(AutoWahModule::sweepRangeControl)),
        header.toNearestInt(), juce::Justification::centredRight);

    auto detector = field.removeFromTop(24.0f).reduced(8.0f, 3.0f);
    graphics.setColour(juce::Colours::black.withAlpha(0.35f));
    graphics.fillRoundedRectangle(detector, 4.0f);
    const auto levelPosition =
        juce::jlimit(0.0f, 1.0f, (inputDb + 72.0f) / 72.0f);
    const auto thresholdPosition =
        juce::jlimit(0.0f, 1.0f, (threshold + 72.0f) / 72.0f);
    graphics.setColour(
        (inputDb >= threshold ? outputColour : accent).withAlpha(0.75f));
    graphics.fillRoundedRectangle(
        detector.withWidth(detector.getWidth() * levelPosition), 4.0f);
    graphics.setColour(juce::Colours::white);
    const auto thresholdX =
        detector.getX() + detector.getWidth() * thresholdPosition;
    graphics.drawVerticalLine(
        juce::roundToInt(thresholdX), detector.getY(), detector.getBottom());
    graphics.setFont(juce::FontOptions(8.5f));
    graphics.drawText(
        "INPUT " + juce::String(inputDb, 1) + " dB   /   THRESHOLD "
            + juce::String(threshold, 1) + " dB",
        detector.toNearestInt(), juce::Justification::centred);

    const auto response = field.reduced(8.0f, 6.0f);
    juce::Path curve;
    const auto resonance = value(AutoWahModule::resonanceControl);
    const auto filterType = discreteIndex(
        value(AutoWahModule::filterTypeControl), 4);
    for (int pixel = 0; pixel <= juce::roundToInt(response.getWidth()); ++pixel)
    {
        const auto normalized =
            static_cast<float>(pixel) / response.getWidth();
        const auto distance =
            (normalized - frequencyPosition)
            / detail::lerp(0.11f, 0.025f, resonance);
        const auto bandMagnitude = std::exp(-0.5f * distance * distance);
        const auto lowPassMagnitude = 1.0f / std::sqrt(
            1.0f + std::exp2(
                (normalized - frequencyPosition)
                * detail::lerp(9.0f, 18.0f, resonance)));
        const auto magnitude =
            filterType == 0 ? lowPassMagnitude
            : filterType == 1 ? bandMagnitude
            : filterType == 2
                ? juce::jlimit(0.0f, 1.0f, 0.34f + bandMagnitude * 0.66f)
                : juce::jlimit(
                      0.0f, 1.0f,
                      bandMagnitude
                          + lowPassMagnitude * (1.0f - bandMagnitude) * 0.28f);
        const auto x = response.getX() + static_cast<float>(pixel);
        const auto y =
            response.getBottom() - magnitude * response.getHeight() * 0.85f;
        if (pixel == 0)
            curve.startNewSubPath(x, y);
        else
            curve.lineTo(x, y);
    }
    graphics.setColour(accent.withAlpha(0.45f + envelope * 0.5f));
    graphics.strokePath(
        curve, juce::PathStrokeType(1.8f + envelope * 1.4f));
    const auto cursorX =
        response.getX() + frequencyPosition * response.getWidth();
    graphics.setColour(outputColour.withAlpha(0.45f + envelope * 0.45f));
    graphics.fillEllipse(
        cursorX - 4.0f - envelope * 4.0f,
        response.getCentreY() - 4.0f - envelope * 4.0f,
        8.0f + envelope * 8.0f, 8.0f + envelope * 8.0f);

    for (int track = 0; track < static_cast<int>(trackControls.size()); ++track)
    {
        const auto control = trackControls[static_cast<size_t>(track)];
        const auto bounds = trackBounds(area, track);
        graphics.setColour(juce::Colour(0xff171b12));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour(
            (control >= AutoWahModule::mixControl ? outputColour : accent)
                .withAlpha(0.65f));
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

std::unique_ptr<ModuleView> createAutoWahView(EffectGraph& graph)
{
    return std::make_unique<AutoWahModuleView>(graph);
}
} // namespace megadsp::ui
