#include "../GraphStyle.h"
#include "../ModuleView.h"
#include "../../modules/SignalDecay.h"

#include "ModuleViewCreators.h"

#include <array>
#include <cmath>

namespace megadsp::ui
{
namespace
{
class SignalDecayModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    bool usesFullPanel() const override { return true; }
    void configureAccessibility(juce::Component& component) const override
    {
        component.setTitle("Signal Decay wear display");
        component.setHelpText(
            "Drag the waveform field horizontally for Sample Rate and "
            "vertically for Resolution. Measured original, degraded, and error "
            "waveforms remain measured histories. Clock markers, dropout shade, "
            "and wear read the current DSP state. Other controls use lower tracks.");
    }
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    static juce::Rectangle<float> waveformBounds(juce::Rectangle<float> area)
    {
        area.removeFromBottom(area.getHeight() * 0.43f);
        return area.reduced(5.0f);
    }
    static juce::Rectangle<float> trackBounds(
        juce::Rectangle<float> area, int control)
    {
        auto tracks = area.removeFromBottom(area.getHeight() * 0.43f);
        static constexpr std::array<int, 9> order {
            2, 3, 4, 5, 6, 7, 8, 9, 10
        };
        auto position = 0;
        while (position < static_cast<int>(order.size())
               && order[static_cast<size_t>(position)] != control)
            ++position;
        if (position == static_cast<int>(order.size()))
            return {};
        const auto width = tracks.getWidth() / 3.0f;
        const auto height = tracks.getHeight() / 3.0f;
        return juce::Rectangle<float>(
            tracks.getX() + width * static_cast<float>(position % 3),
            tracks.getY() + height * static_cast<float>(position / 3),
            width, height).reduced(6.0f, 4.0f);
    }
};

void SignalDecayModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (waveformBounds(area).contains(event.position))
    {
        dragPrimary = 1;
        dragSecondary = 0;
    }
    for (const auto control : { 2, 3, 4, 5, 6, 7, 8, 9, 10 })
        if (dragPrimary < 0 && trackBounds(area, control).contains(event.position))
            dragPrimary = control;
    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void SignalDecayModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (dragPrimary == 1)
    {
        const auto bounds = waveformBounds(area);
        setValue(1, juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - bounds.getX()) / bounds.getWidth()));
        setValue(0, juce::jlimit(
            0.0f, 1.0f,
            (bounds.getBottom() - event.position.y) / bounds.getHeight()));
        dragReadout = "SAMPLE RATE  " + formatControlValue(type, 1, value(1))
            + "    RESOLUTION  " + formatControlValue(type, 0, value(0));
        repaint();
        return;
    }
    const auto bounds = trackBounds(area, dragPrimary);
    setValue(dragPrimary, juce::jlimit(
        0.0f, 1.0f,
        (event.position.x - bounds.getX()) / bounds.getWidth()));
    updateDefaultDragReadout();
}

void SignalDecayModuleView::mouseDoubleClick(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (waveformBounds(area).contains(event.position))
    {
        resetToDefault(0);
        resetToDefault(1);
        return;
    }
    for (const auto control : { 2, 3, 4, 5, 6, 7, 8, 9, 10 })
        if (trackBounds(area, control).contains(event.position))
        {
            resetToDefault(control);
            return;
        }
}

void SignalDecayModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto field = waveformBounds(area);
    graphics.setColour(juce::Colour(0xe6121821));
    graphics.fillRoundedRectangle(field, 8.0f);
    auto upper = field.reduced(8.0f);
    auto errorLane = upper.removeFromBottom(upper.getHeight() * 0.28f);
    const auto centreY = upper.getCentreY();
    juce::Path inputPath;
    juce::Path outputPath;
    juce::Path errorPath;
    for (int pixel = 0; pixel < juce::jmax(2, juce::roundToInt(upper.getWidth()));
         ++pixel)
    {
        const auto proportion = static_cast<float>(pixel)
                                / juce::jmax(1.0f, upper.getWidth() - 1.0f);
        const auto index = juce::jlimit(
            0, fftSize - 1, juce::roundToInt(proportion * (fftSize - 1)));
        const auto x = upper.getX() + proportion * upper.getWidth();
        const auto inputY = centreY
            - juce::jlimit(-1.0f, 1.0f, inputSamples[static_cast<size_t>(index)])
                  * upper.getHeight() * 0.42f;
        const auto outputY = centreY
            - juce::jlimit(-1.0f, 1.0f, outputSamples[static_cast<size_t>(index)])
                  * upper.getHeight() * 0.42f;
        const auto error = juce::jlimit(
            -1.0f, 1.0f,
            outputSamples[static_cast<size_t>(index)]
                - inputSamples[static_cast<size_t>(index)]);
        const auto errorY = errorLane.getCentreY()
                            - error * errorLane.getHeight() * 0.44f;
        if (pixel == 0)
        {
            inputPath.startNewSubPath(x, inputY);
            outputPath.startNewSubPath(x, outputY);
            errorPath.startNewSubPath(x, errorY);
        }
        else
        {
            inputPath.lineTo(x, inputY);
            outputPath.lineTo(x, outputY);
            errorPath.lineTo(x, errorY);
        }
    }
    graphics.setColour(inputColour.withAlpha(0.72f));
    graphics.strokePath(inputPath, juce::PathStrokeType(1.0f));
    graphics.setColour(outputColour.withAlpha(0.9f));
    graphics.strokePath(outputPath, juce::PathStrokeType(1.25f));
    graphics.setColour(reductionColour.withAlpha(0.72f));
    graphics.strokePath(errorPath, juce::PathStrokeType(1.0f));
    ContinuousTelemetrySnapshot telemetry;
    EventTelemetrySnapshot events;
    const auto hasTelemetry =
        readContinuousTelemetry(telemetry)
        && telemetry.sequence != 0
        && telemetry.valueCount
               >= SignalDecayModule::telemetryValueCount;
    const auto hasEvents =
        readEventTelemetry(events) && events.sequence != 0;
    const auto currentRate = hasTelemetry
        ? telemetry.values[SignalDecayModule::currentSampleRate]
        : exponential(1000.0f, 48000.0f, value(1));
    graphics.setColour(juce::Colours::white.withAlpha(0.16f));
    const auto columns = juce::jlimit(
        1, 48, juce::roundToInt(currentRate / 1000.0f));
    for (int column = 1; column < columns; ++column)
    {
        const auto x = upper.getX()
            + upper.getWidth() * static_cast<float>(column)
                                  / static_cast<float>(columns);
        graphics.drawVerticalLine(juce::roundToInt(x),
                                  upper.getY(), upper.getBottom());
    }
    if (hasTelemetry)
    {
        const auto leftGain =
            telemetry.values[SignalDecayModule::leftDropoutGain];
        const auto rightGain =
            telemetry.values[SignalDecayModule::rightDropoutGain];
        const auto dropout = 1.0f - juce::jlimit(
            0.0f, 1.0f, 0.5f * (leftGain + rightGain));
        graphics.setColour(reductionColour.withAlpha(
            juce::jlimit(0.0f, 0.55f, dropout * 0.65f)));
        graphics.fillRect(upper);
        graphics.setColour(accent.withAlpha(0.9f));
        for (const auto phase : {
                 telemetry.values[SignalDecayModule::leftClockPhase],
                 telemetry.values[SignalDecayModule::rightClockPhase] })
        {
            const auto x = upper.getX()
                + juce::jlimit(0.0f, 1.0f, phase) * upper.getWidth();
            graphics.drawVerticalLine(
                juce::roundToInt(x), upper.getY(), upper.getBottom());
        }
    }
    if (hasEvents && events.eventCount > 0)
    {
        graphics.setColour(reductionColour.withAlpha(0.9f));
        graphics.fillRoundedRectangle(
            field.withLeft(field.getRight() - 8.0f).reduced(1.0f, 5.0f),
            3.0f);
    }
    graphics.setColour(juce::Colours::white);
    graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    graphics.drawText(
        formatControlValue(type, 0, value(0))
            + (hasTelemetry ? "  ·  DSP " : "  ·  CONTROL ")
            + juce::String(currentRate, 0) + " Hz"
            + (hasTelemetry
                   ? "  ·  WEAR "
                       + juce::String(
                           telemetry.values[SignalDecayModule::stereoWearAmount]
                               * 100.0f,
                           0)
                       + "%"
                   : "  ·  DSP TELEMETRY WAITING"),
        field.withHeight(22.0f).reduced(8.0f, 0.0f).toNearestInt(),
        juce::Justification::centredLeft);
    graphics.setColour(reductionColour.withAlpha(0.85f));
    graphics.drawText("MEASURED ERROR", errorLane.toNearestInt(),
                      juce::Justification::topRight);

    for (const auto control : { 2, 3, 4, 5, 6, 7, 8, 9, 10 })
    {
        const auto bounds = trackBounds(area, control);
        graphics.setColour(juce::Colour(0xff151b24));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour((control >= 9 ? outputColour : accent)
                               .withAlpha(0.58f));
        graphics.fillRoundedRectangle(
            bounds.withWidth(bounds.getWidth() * value(control)), 5.0f);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(9.2f));
        graphics.drawFittedText(
            juce::String(controlMetadata(type, control).label) + "  "
                + formatControlValue(type, control, value(control)),
            bounds.reduced(5.0f, 0.0f).toNearestInt(),
            juce::Justification::centred, 1);
    }
}
} // namespace

std::unique_ptr<ModuleView> createSignalDecayView(EffectGraph& graph)
{
    return std::make_unique<SignalDecayModuleView>(graph);
}
} // namespace megadsp::ui
