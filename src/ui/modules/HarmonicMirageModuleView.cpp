#include "../GraphStyle.h"
#include "../ModuleView.h"
#include "../../modules/HarmonicMirage.h"

#include "ModuleViewCreators.h"

#include <array>
#include <cmath>

namespace megadsp::ui
{
namespace
{
class HarmonicMirageModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    bool usesFullPanel() const override { return true; }
    void configureAccessibility(juce::Component& component) const override
    {
        component.setTitle("Harmonic Mirage tracking graph");
        component.setHelpText(
            "Choose Harmonic, Subharmonic, Hollow, or Metallic at the top. "
            "Captured DSP histories show tracked and generated pitch, confidence, "
            "and generated level. Drag the graph horizontally for Even / Odd "
            "balance and vertically for Partials. Lower tracks edit Tracking, "
            "Inharmonicity, Fine Drift, Response, Transient Preserve, Stereo "
            "Spread, Mix, and Output. Double-click to restore defaults.");
    }
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    static constexpr std::array<int, 8> lowerControls {
        HarmonicMirageModule::trackingControl,
        HarmonicMirageModule::inharmonicityControl,
        HarmonicMirageModule::fineDriftControl,
        HarmonicMirageModule::responseControl,
        HarmonicMirageModule::transientPreserveControl,
        HarmonicMirageModule::stereoSpreadControl,
        HarmonicMirageModule::mixControl,
        HarmonicMirageModule::outputControl
    };
    static juce::Rectangle<float> modeBounds(
        juce::Rectangle<float> area, int index)
    {
        auto row = area.removeFromTop(31.0f);
        const auto width = row.getWidth() / 4.0f;
        return juce::Rectangle<float>(
            row.getX() + width * static_cast<float>(index),
            row.getY(), width, row.getHeight())
            .reduced(3.0f, 2.0f);
    }
    static juce::Rectangle<float> graphBounds(juce::Rectangle<float> area)
    {
        area.removeFromTop(33.0f);
        area.removeFromBottom(area.getHeight() * 0.41f);
        return area.reduced(5.0f);
    }
    static juce::Rectangle<float> plotBounds(juce::Rectangle<float> area)
    {
        auto plot = graphBounds(area).reduced(8.0f);
        plot.removeFromTop(27.0f);
        plot.removeFromBottom(17.0f);
        return plot;
    }
    static juce::Rectangle<float> controlBounds(
        juce::Rectangle<float> area, int control)
    {
        area.removeFromTop(33.0f);
        auto controls = area.removeFromBottom(area.getHeight() * 0.41f);
        auto position = 0;
        while (position < static_cast<int>(lowerControls.size())
               && lowerControls[static_cast<size_t>(position)] != control)
            ++position;
        if (position == static_cast<int>(lowerControls.size()))
            return {};
        const auto width = controls.getWidth() * 0.25f;
        const auto height = controls.getHeight() * 0.5f;
        return juce::Rectangle<float>(
            controls.getX() + width * static_cast<float>(position % 4),
            controls.getY() + height * static_cast<float>(position / 4),
            width, height)
            .reduced(5.0f, 4.0f);
    }
    static float frequencyY(float frequency, juce::Rectangle<float> plot)
    {
        const auto normalized = std::log(
            juce::jlimit(40.0f, 8000.0f, frequency) / 40.0f)
            / std::log(200.0f);
        return plot.getBottom() - normalized * plot.getHeight();
    }
    static juce::String pitchText(float frequency)
    {
        if (frequency <= 0.0f)
            return "—";
        static constexpr std::array<const char*, 12> notes {
            "C", "C#", "D", "D#", "E", "F",
            "F#", "G", "G#", "A", "A#", "B"
        };
        const auto midi = juce::roundToInt(
            69.0 + 12.0 * std::log2(static_cast<double>(frequency) / 440.0));
        const auto note = (midi % 12 + 12) % 12;
        const auto octave = midi / 12 - 1;
        return juce::String(notes[static_cast<size_t>(note)])
            + juce::String(octave) + "  " + juce::String(frequency, 1) + " Hz";
    }
    juce::String controlText(int control) const
    {
        const auto normalized = value(control);
        if (control == HarmonicMirageModule::partialsControl)
            return juce::String(juce::jlimit(
                HarmonicMirageModule::minimumPartials,
                HarmonicMirageModule::maximumPartials,
                juce::roundToInt(
                    HarmonicMirageModule::minimumPartials
                    + (HarmonicMirageModule::maximumPartials
                       - HarmonicMirageModule::minimumPartials) * normalized)));
        if (control == HarmonicMirageModule::evenOddControl)
            return juce::String(normalized * 100.0f, 0) + "% even";
        if (control == HarmonicMirageModule::fineDriftControl)
            return juce::String(normalized * 30.0f, 1) + " cents";
        if (control == HarmonicMirageModule::responseControl)
        {
            const auto seconds = exponential(0.020f, 2.0f, normalized);
            return seconds < 1.0f
                ? juce::String(seconds * 1000.0f, 0) + " ms"
                : juce::String(seconds, 2) + " s";
        }
        return formatControlValue(type, control, normalized);
    }
    void chooseMode(int);
};

void HarmonicMirageModuleView::chooseMode(int index)
{
    if (auto* target = parameter(HarmonicMirageModule::modeControl))
    {
        graph.focusKeyboardControl(HarmonicMirageModule::modeControl);
        target->beginChangeGesture();
        target->setValueNotifyingHost(
            discreteValue(index, HarmonicMirageModule::modeCount));
        target->endChangeGesture();
    }
    repaint();
}

void HarmonicMirageModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int index = 0; index < HarmonicMirageModule::modeCount; ++index)
        if (modeBounds(area, index).contains(event.position))
        {
            chooseMode(index);
            return;
        }
    if (plotBounds(area).contains(event.position))
    {
        dragPrimary = HarmonicMirageModule::evenOddControl;
        dragSecondary = HarmonicMirageModule::partialsControl;
    }
    for (const auto control : lowerControls)
        if (dragPrimary < 0
            && controlBounds(area, control).contains(event.position))
            dragPrimary = control;
    if (dragPrimary >= 0)
    {
        beginGestures();
        mouseDrag(event);
    }
}

void HarmonicMirageModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (dragSecondary == HarmonicMirageModule::partialsControl)
    {
        const auto bounds = plotBounds(area);
        setValue(HarmonicMirageModule::evenOddControl, juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - bounds.getX()) / bounds.getWidth()));
        setValue(HarmonicMirageModule::partialsControl, juce::jlimit(
            0.0f, 1.0f,
            (bounds.getBottom() - event.position.y) / bounds.getHeight()));
        dragReadout =
            "EVEN / ODD  "
            + controlText(HarmonicMirageModule::evenOddControl)
            + "    PARTIALS  "
            + controlText(HarmonicMirageModule::partialsControl);
        repaint();
        return;
    }
    const auto bounds = controlBounds(area, dragPrimary);
    setValue(dragPrimary, juce::jlimit(
        0.0f, 1.0f,
        (event.position.x - bounds.getX()) / bounds.getWidth()));
    dragReadout =
        juce::String(controlMetadata(type, dragPrimary).label).toUpperCase()
        + "  " + controlText(dragPrimary);
    repaint();
}

void HarmonicMirageModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int index = 0; index < HarmonicMirageModule::modeCount; ++index)
        if (modeBounds(area, index).contains(event.position))
        {
            resetToDefault(HarmonicMirageModule::modeControl);
            return;
        }
    if (plotBounds(area).contains(event.position))
    {
        resetToDefault(HarmonicMirageModule::evenOddControl);
        resetToDefault(HarmonicMirageModule::partialsControl);
        return;
    }
    for (const auto control : lowerControls)
        if (controlBounds(area, control).contains(event.position))
        {
            resetToDefault(control);
            return;
        }
}

void HarmonicMirageModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    static constexpr std::array<const char*, 4> modes {
        "HARMONIC", "SUBHARMONIC", "HOLLOW", "METALLIC"
    };
    const auto selected = discreteIndex(
        value(HarmonicMirageModule::modeControl),
        HarmonicMirageModule::modeCount);
    for (int index = 0; index < HarmonicMirageModule::modeCount; ++index)
    {
        const auto bounds = modeBounds(area, index);
        graphics.setColour(index == selected ? accent
                                             : juce::Colour(0xff27313d));
        graphics.fillRoundedRectangle(bounds, 6.0f);
        graphics.setColour(index == selected ? juce::Colour(0xff101820)
                                             : juce::Colours::white);
        graphics.setFont(juce::FontOptions(8.8f, juce::Font::bold));
        graphics.drawFittedText(
            modes[static_cast<size_t>(index)], bounds.toNearestInt(),
            juce::Justification::centred, 1);
    }

    const auto graphArea = graphBounds(area);
    const auto plot = plotBounds(area);
    graphics.setColour(juce::Colour(0xe6121821));
    graphics.fillRoundedRectangle(graphArea, 8.0f);
    ContinuousTelemetrySnapshot telemetry;
    const auto hasTelemetry =
        readContinuousTelemetry(telemetry) && telemetry.sequence != 0
        && telemetry.valueCount >= HarmonicMirageModule::telemetryValueCount;
    const auto hasHistory = hasTelemetry
        && telemetry.historyValueCount
            >= HarmonicMirageModule::telemetryHistoryValueCount
        && telemetry.historyCount > 1;
    const auto tracked = hasTelemetry
        ? telemetry.values[HarmonicMirageModule::trackedFrequencyHz] : 0.0f;
    const auto generated = hasTelemetry
        ? telemetry.values[HarmonicMirageModule::generatedFrequencyHz] : 0.0f;
    graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    graphics.setColour(inputColour);
    graphics.drawFittedText(
        "TRACKED  " + pitchText(tracked),
        graphArea.reduced(8.0f).withWidth(graphArea.getWidth() * 0.48f)
            .toNearestInt(),
        juce::Justification::topLeft, 1);
    graphics.setColour(outputColour);
    graphics.drawFittedText(
        "GENERATED  " + pitchText(generated),
        graphArea.reduced(8.0f).withTrimmedLeft(graphArea.getWidth() * 0.48f)
            .toNearestInt(),
        juce::Justification::topRight, 1);
    for (const auto frequency : { 50.0f, 100.0f, 440.0f, 1000.0f, 4000.0f })
    {
        const auto y = frequencyY(frequency, plot);
        graphics.setColour(juce::Colours::white.withAlpha(0.10f));
        graphics.drawHorizontalLine(
            juce::roundToInt(y), plot.getX(), plot.getRight());
        graphics.setColour(juce::Colours::white.withAlpha(0.4f));
        graphics.drawText(
            frequency >= 1000.0f
                ? juce::String(frequency / 1000.0f, 0) + " kHz"
                : juce::String(frequency, 0) + " Hz",
            juce::Rectangle<float>(plot.getX() + 2.0f, y - 10.0f, 42.0f, 10.0f),
            juce::Justification::centredLeft);
    }
    if (hasHistory)
    {
        juce::Path trackedPath, generatedPath, confidencePath, levelPath;
        for (std::uint32_t point = 0; point < telemetry.historyCount; ++point)
        {
            const auto x = plot.getX()
                + static_cast<float>(point)
                    / static_cast<float>(juce::jmax(1u, telemetry.historyCount - 1))
                    * plot.getWidth();
            const auto trackedY = frequencyY(continuousTelemetryHistoryValue(
                telemetry, HarmonicMirageModule::trackedFrequencyHistory, point),
                plot);
            const auto generatedY = frequencyY(continuousTelemetryHistoryValue(
                telemetry, HarmonicMirageModule::generatedFrequencyHistory, point),
                plot);
            const auto confidence = juce::jlimit(
                0.0f, 1.0f, continuousTelemetryHistoryValue(
                    telemetry, HarmonicMirageModule::confidenceHistory, point));
            const auto level = juce::jlimit(
                0.0f, 1.0f, std::sqrt(juce::jmax(
                    0.0f, continuousTelemetryHistoryValue(
                        telemetry, HarmonicMirageModule::generatedLevelHistory,
                        point))) * 2.5f);
            const auto confidenceY =
                plot.getBottom() - confidence * plot.getHeight();
            const auto levelY = plot.getBottom() - level * plot.getHeight();
            if (point == 0)
            {
                trackedPath.startNewSubPath(x, trackedY);
                generatedPath.startNewSubPath(x, generatedY);
                confidencePath.startNewSubPath(x, confidenceY);
                levelPath.startNewSubPath(x, levelY);
            }
            else
            {
                trackedPath.lineTo(x, trackedY);
                generatedPath.lineTo(x, generatedY);
                confidencePath.lineTo(x, confidenceY);
                levelPath.lineTo(x, levelY);
            }
        }
        graphics.setColour(inputColour.withAlpha(0.85f));
        graphics.strokePath(trackedPath, juce::PathStrokeType(1.5f));
        graphics.setColour(outputColour);
        graphics.strokePath(generatedPath, juce::PathStrokeType(2.1f));
        graphics.setColour(accent.withAlpha(0.34f));
        graphics.strokePath(confidencePath, juce::PathStrokeType(1.0f));
        graphics.setColour(reductionColour.withAlpha(0.30f));
        graphics.strokePath(levelPath, juce::PathStrokeType(1.0f));
    }
    else
    {
        graphics.setColour(juce::Colours::white.withAlpha(0.5f));
        graphics.drawText("DSP TRACKING TELEMETRY WAITING", plot.toNearestInt(),
                          juce::Justification::centred);
    }
    const auto target = juce::Point<float>(
        plot.getX() + value(HarmonicMirageModule::evenOddControl) * plot.getWidth(),
        plot.getBottom()
            - value(HarmonicMirageModule::partialsControl) * plot.getHeight());
    graphics.setColour(juce::Colours::white);
    graphics.drawEllipse(
        juce::Rectangle<float>(15.0f, 15.0f).withCentre(target), 1.5f);
    graphics.setFont(juce::FontOptions(8.5f, juce::Font::bold));
    graphics.drawFittedText(
        "PARTIALS  "
            + controlText(HarmonicMirageModule::partialsControl)
            + "  ·  EVEN / ODD  "
            + controlText(HarmonicMirageModule::evenOddControl),
        juce::Rectangle<float>(260.0f, 15.0f)
            .withCentre(target.translated(0.0f, -14.0f))
            .constrainedWithin(plot).toNearestInt(),
        juce::Justification::centred, 1);
    if (hasTelemetry)
    {
        graphics.setColour(juce::Colours::white.withAlpha(0.75f));
        graphics.drawFittedText(
            "CONFIDENCE  "
                + juce::String(
                    telemetry.values[HarmonicMirageModule::trackingConfidence]
                        * 100.0f, 0)
                + "%  ·  ACTIVE  "
                + juce::String(juce::roundToInt(
                    telemetry.values[HarmonicMirageModule::activePartialCount]))
                + " PARTIALS",
            graphArea.reduced(8.0f).toNearestInt(),
            juce::Justification::bottomRight, 1);
    }

    for (const auto control : lowerControls)
    {
        const auto bounds = controlBounds(area, control);
        const auto choice = control == HarmonicMirageModule::trackingControl;
        graphics.setColour(juce::Colour(0xff151b24));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour((control >= HarmonicMirageModule::mixControl
                                ? outputColour : accent).withAlpha(0.5f));
        if (!choice)
            graphics.fillRoundedRectangle(
                bounds.withWidth(bounds.getWidth() * value(control)), 5.0f);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(8.4f));
        graphics.drawFittedText(
            juce::String(controlMetadata(type, control).label).toUpperCase()
                + "  " + controlText(control),
            bounds.reduced(4.0f, 0.0f).toNearestInt(),
            juce::Justification::centred, 1, 0.62f);
    }
}
} // namespace

std::unique_ptr<ModuleView> createHarmonicMirageView(EffectGraph& graph)
{
    return std::make_unique<HarmonicMirageModuleView>(graph);
}
} // namespace megadsp::ui
