#include "../GraphStyle.h"
#include "../ModuleView.h"
#include "../../modules/ResonanceTamer.h"

#include "ModuleViewCreators.h"

#include <array>
#include <cmath>
#include <tuple>

namespace megadsp::ui
{
std::unique_ptr<ModuleView> createResonanceTamerView(EffectGraph&);

namespace
{
class ResonanceTamerModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;

    bool usesFullPanel() const override { return true; }
    void configureAccessibility(juce::Component& component) const override
    {
        component.setTitle("Resonance Tamer spectrum and controls");
        component.setHelpText(
            "Captured DSP input, learned baseline, detected excess, and actual "
            "reduction are shown across frequency. Drag the Low and High Limit "
            "handles or any lower control. Click Selectivity or Reaction to "
            "choose a mode. Double-click a control to restore its default.");
    }

    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    static constexpr std::array<int, 9> controls {
        0, 1, 2, 3, 4, 5, 6, 7, 8
    };

    static float controlHeight(juce::Rectangle<float> area)
    {
        return juce::jlimit(96.0f, 126.0f, area.getHeight() * 0.34f);
    }

    static juce::Rectangle<float> plotBounds(juce::Rectangle<float> area)
    {
        const auto controlsHeight = controlHeight(area);
        area.removeFromTop(30.0f);
        area.removeFromBottom(controlsHeight + 8.0f);
        return area.reduced(4.0f, 2.0f);
    }

    static juce::Rectangle<float> spectrumBounds(
        juce::Rectangle<float> plot)
    {
        auto graphArea = plot.reduced(8.0f);
        graphArea.removeFromTop(26.0f);
        graphArea.removeFromBottom(16.0f);
        graphArea.removeFromBottom(
            juce::jmax(42.0f, graphArea.getHeight() * 0.27f) + 6.0f);
        return graphArea;
    }

    static juce::Rectangle<float> controlBounds(
        juce::Rectangle<float> area, int control)
    {
        auto controlsArea = area.removeFromBottom(controlHeight(area));
        const auto column = control % 3;
        const auto row = control / 3;
        const auto width = controlsArea.getWidth() / 3.0f;
        const auto height = controlsArea.getHeight() / 3.0f;
        return juce::Rectangle<float>(
                   controlsArea.getX() + width * static_cast<float>(column),
                   controlsArea.getY() + height * static_cast<float>(row),
                   width, height)
            .reduced(4.0f, 3.0f);
    }

    static float frequencyX(float frequency, juce::Rectangle<float> area)
    {
        const auto normalized =
            std::log(juce::jlimit(20.0f, 20000.0f, frequency) / 20.0f)
            / std::log(1000.0f);
        return area.getX() + normalized * area.getWidth();
    }

    static float frequencyAtX(float x, juce::Rectangle<float> area)
    {
        const auto normalized = juce::jlimit(
            0.0f, 1.0f, (x - area.getX()) / area.getWidth());
        return 20.0f * std::pow(1000.0f, normalized);
    }

    static float lowFrequency(float normalized)
    {
        return 20.0f * std::pow(100.0f, normalized);
    }

    static float highFrequency(float normalized)
    {
        return 1000.0f * std::pow(20.0f, normalized);
    }

    static juce::String frequencyText(float frequency)
    {
        return frequency >= 1000.0f
            ? juce::String(frequency * 0.001f,
                           frequency >= 10000.0f ? 1 : 2)
                  + " kHz"
            : juce::String(frequency, 0) + " Hz";
    }

    juce::String controlText(int control) const
    {
        switch (control)
        {
            case 0: return juce::String(value(control) * 18.0f, 1) + " dB max";
            case 1:
            case 2: return formatControlValue(type, control, value(control));
            case 3:
            {
                const auto bias = value(control) * 6.0f - 3.0f;
                return (bias > 0.05f ? "+" : "")
                    + juce::String(bias, 1) + " dB/oct";
            }
            case 4: return frequencyText(lowFrequency(value(control)));
            case 5: return frequencyText(highFrequency(value(control)));
            case 6:
            case 7: return juce::String(value(control) * 100.0f, 0) + "%";
            case 8:
                return juce::String(value(control) * 30.0f - 18.0f, 1)
                    + " dB";
            default: return {};
        }
    }

    static float levelY(float db, juce::Rectangle<float> area)
    {
        return juce::jmap(
            juce::jlimit(-100.0f, 12.0f, db), -100.0f, 12.0f,
            area.getBottom(), area.getY());
    }

    static float reductionY(float db, juce::Rectangle<float> area)
    {
        return juce::jmap(
            juce::jlimit(0.0f, 18.0f, db), 0.0f, 18.0f,
            area.getBottom(), area.getY());
    }

    static juce::Path telemetryPath(
        const ContinuousTelemetrySnapshot& telemetry, std::uint32_t lane,
        juce::Rectangle<float> area, bool reduction)
    {
        juce::Path path;
        const auto count = telemetry.historyCount;
        for (std::uint32_t point = 0; point < count; ++point)
        {
            const auto x = area.getX()
                + static_cast<float>(point)
                      / static_cast<float>(juce::jmax(1u, count - 1u))
                      * area.getWidth();
            const auto sample =
                continuousTelemetryHistoryValue(telemetry, lane, point);
            const auto y = reduction ? reductionY(sample, area)
                                     : levelY(sample, area);
            if (point == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
        return path;
    }

    bool draggingRangeHandle = false;
};

void ResonanceTamerModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    draggingRangeHandle = false;
    for (const auto control : controls)
        if (controlBounds(area, control).contains(event.position))
        {
            if (control == 1 || control == 2)
            {
                cycleChoice(control, 3);
                return;
            }
            dragPrimary = control;
            beginGestures();
            mouseDrag(event);
            return;
        }

    const auto plot = plotBounds(area);
    const auto spectrum = spectrumBounds(plot);
    ContinuousTelemetrySnapshot telemetry;
    const auto hasTelemetry =
        readContinuousTelemetry(telemetry) && telemetry.sequence != 0
        && telemetry.valueCount >= ResonanceTamerModule::telemetryValueCount;
    const auto lowHz = hasTelemetry
        ? telemetry.values[ResonanceTamerModule::lowLimitHz]
        : lowFrequency(value(4));
    const auto highHz = hasTelemetry
        ? telemetry.values[ResonanceTamerModule::highLimitHz]
        : highFrequency(value(5));
    const auto lowDistance =
        std::abs(event.position.x - frequencyX(lowHz, spectrum));
    const auto highDistance =
        std::abs(event.position.x - frequencyX(highHz, spectrum));
    if (plot.contains(event.position)
        && juce::jmin(lowDistance, highDistance) <= 20.0f)
    {
        dragPrimary = lowDistance <= highDistance ? 4 : 5;
        draggingRangeHandle = true;
        beginGestures();
        mouseDrag(event);
    }
}

void ResonanceTamerModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (draggingRangeHandle)
    {
        const auto plot = spectrumBounds(plotBounds(area));
        const auto frequency = frequencyAtX(event.position.x, plot);
        if (dragPrimary == 4)
        {
            const auto limited = juce::jmin(
                frequency, highFrequency(value(5)));
            setValue(4, juce::jlimit(
                0.0f, 1.0f, std::log(limited / 20.0f) / std::log(100.0f)));
        }
        else
        {
            const auto limited = juce::jmax(
                frequency, lowFrequency(value(4)));
            setValue(5, juce::jlimit(
                0.0f, 1.0f,
                std::log(limited / 1000.0f) / std::log(20.0f)));
        }
    }
    else
    {
        const auto bounds = controlBounds(area, dragPrimary);
        setValue(dragPrimary, juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - bounds.getX()) / bounds.getWidth()));
    }
    dragReadout =
        juce::String(controlMetadata(type, dragPrimary).label) + "  "
        + controlText(dragPrimary);
    repaint();
}

void ResonanceTamerModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (const auto control : controls)
        if (controlBounds(area, control).contains(event.position))
        {
            resetToDefault(control);
            return;
        }

    const auto plot = spectrumBounds(plotBounds(area));
    if (!plotBounds(area).contains(event.position))
        return;
    const auto lowDistance = std::abs(
        event.position.x - frequencyX(lowFrequency(value(4)), plot));
    const auto highDistance = std::abs(
        event.position.x - frequencyX(highFrequency(value(5)), plot));
    if (juce::jmin(lowDistance, highDistance) <= 20.0f)
        resetToDefault(lowDistance <= highDistance ? 4 : 5);
}

void ResonanceTamerModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto fullArea = area;
    auto header = area.removeFromTop(28.0f);
    graphics.setColour(accent);
    graphics.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    graphics.drawText(
        "RESONANCE MAP", header.removeFromLeft(header.getWidth() * 0.34f)
                             .reduced(6.0f, 0.0f),
        juce::Justification::centredLeft);
    graphics.setColour(juce::Colours::white.withAlpha(0.72f));
    graphics.setFont(juce::FontOptions(10.0f));
    graphics.drawFittedText(
        "INPUT   ·   LEARNED BASELINE   ·   EXCESS   ·   REDUCTION",
        header.reduced(5.0f, 0.0f).toNearestInt(),
        juce::Justification::centredRight, 1, 0.65f);

    const auto plot = plotBounds(fullArea);
    graphics.setColour(juce::Colour(0xe6121821));
    graphics.fillRoundedRectangle(plot, 8.0f);
    auto graphArea = plot.reduced(8.0f);
    auto metrics = graphArea.removeFromTop(24.0f);
    graphArea.removeFromTop(2.0f);
    auto frequencyAxis = graphArea.removeFromBottom(16.0f);
    auto reductionArea = graphArea.removeFromBottom(
        juce::jmax(42.0f, graphArea.getHeight() * 0.27f));
    graphArea.removeFromBottom(6.0f);
    const auto spectrumArea = graphArea;

    ContinuousTelemetrySnapshot telemetry;
    const auto hasTelemetry =
        readContinuousTelemetry(telemetry) && telemetry.sequence != 0
        && telemetry.valueCount >= ResonanceTamerModule::telemetryValueCount
        && telemetry.historyValueCount >= 4
        && telemetry.historyCount >= 2;

    for (const auto db : { -90.0f, -60.0f, -30.0f, 0.0f })
    {
        const auto y = levelY(db, spectrumArea);
        graphics.setColour(juce::Colour(0xff303a46));
        graphics.drawHorizontalLine(
            juce::roundToInt(y), spectrumArea.getX(), spectrumArea.getRight());
        graphics.setColour(juce::Colour(0xff8995a4));
        graphics.drawText(
            juce::String(db, 0) + " dB",
            juce::Rectangle<float>(
                spectrumArea.getX() + 3.0f, y - 12.0f, 45.0f, 12.0f),
            juce::Justification::centredLeft);
    }
    graphics.setColour(juce::Colour(0xff303a46));
    graphics.drawHorizontalLine(
        juce::roundToInt(reductionY(0.0f, reductionArea)),
        reductionArea.getX(), reductionArea.getRight());
    graphics.drawHorizontalLine(
        juce::roundToInt(reductionY(9.0f, reductionArea)),
        reductionArea.getX(), reductionArea.getRight());
    graphics.setColour(juce::Colour(0xff8995a4));
    graphics.drawText(
        "0", reductionArea.withTrimmedTop(
                 juce::jmax(0.0f, reductionArea.getHeight() - 14.0f))
                 .withWidth(25.0f),
        juce::Justification::centredLeft);
    graphics.drawText(
        "18 dB", juce::Rectangle<float>(
                     reductionArea.getX(), reductionArea.getY(), 42.0f, 14.0f),
        juce::Justification::centredLeft);

    if (hasTelemetry)
    {
        auto input = telemetryPath(telemetry, 0, spectrumArea, false);
        auto baseline = telemetryPath(telemetry, 1, spectrumArea, false);
        auto excess = telemetryPath(telemetry, 2, reductionArea, true);
        auto reduction = telemetryPath(telemetry, 3, reductionArea, true);
        graphics.setColour(inputColour.withAlpha(0.82f));
        graphics.strokePath(input, juce::PathStrokeType(1.8f));
        graphics.setColour(accent.withAlpha(0.86f));
        graphics.strokePath(baseline, juce::PathStrokeType(2.0f));
        graphics.setColour(reductionColour.withAlpha(0.42f));
        graphics.strokePath(excess, juce::PathStrokeType(1.5f));
        graphics.setColour(reductionColour);
        graphics.strokePath(reduction, juce::PathStrokeType(2.6f));

        const auto lowHz = telemetry.values[ResonanceTamerModule::lowLimitHz];
        const auto highHz = telemetry.values[ResonanceTamerModule::highLimitHz];
        const auto lowX = frequencyX(lowHz, spectrumArea);
        const auto highX = frequencyX(highHz, spectrumArea);
        graphics.setColour(accent.withAlpha(0.10f));
        graphics.fillRect(juce::Rectangle<float>(
            lowX, spectrumArea.getY(), juce::jmax(0.0f, highX - lowX),
            reductionArea.getBottom() - spectrumArea.getY()));
        for (const auto& [x, label, top] : {
                 std::tuple { lowX, "LOW  " + frequencyText(lowHz), true },
                 std::tuple { highX, "HIGH  " + frequencyText(highHz), false } })
        {
            graphics.setColour(accent);
            graphics.drawVerticalLine(
                juce::roundToInt(x), spectrumArea.getY(),
                reductionArea.getBottom());
            graphics.fillEllipse(x - 5.0f, reductionArea.getBottom() - 5.0f,
                                 10.0f, 10.0f);
            auto labelBounds = juce::Rectangle<float>(
                x - 55.0f,
                spectrumArea.getY() + (top ? 2.0f : 18.0f),
                110.0f, 16.0f).constrainedWithin(spectrumArea);
            graphics.drawFittedText(
                label, labelBounds.toNearestInt(),
                juce::Justification::centred, 1, 0.72f);
        }

        const auto strongest =
            telemetry.values[ResonanceTamerModule::strongestFrequencyHz];
        if (strongest >= 20.0f)
        {
            graphics.setColour(reductionColour.withAlpha(0.72f));
            graphics.drawVerticalLine(
                juce::roundToInt(frequencyX(strongest, spectrumArea)),
                spectrumArea.getY(), spectrumArea.getBottom());
        }

        const std::array<juce::String, 3> metricText {
            "INPUT  "
                + juce::String(
                    telemetry.values[ResonanceTamerModule::inputLevelDb], 1)
                + " dB",
            "EXCESS  "
                + juce::String(
                    telemetry.values[ResonanceTamerModule::detectedExcessDb],
                    1)
                + " dB",
            "REDUCTION  "
                + juce::String(
                    telemetry.values[ResonanceTamerModule::actualReductionDb],
                    1)
                + " dB"
        };
        const auto metricWidth = metrics.getWidth() / 3.0f;
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        for (int index = 0; index < 3; ++index)
            graphics.drawFittedText(
                metricText[static_cast<size_t>(index)],
                juce::Rectangle<float>(
                    metrics.getX() + metricWidth * static_cast<float>(index),
                    metrics.getY(), metricWidth, metrics.getHeight())
                    .toNearestInt(),
                juce::Justification::centred, 1, 0.7f);
    }
    else
    {
        graphics.setColour(juce::Colours::white.withAlpha(0.58f));
        graphics.drawText(
            "WAITING FOR CAPTURED DSP SPECTRUM",
            plot.toNearestInt(), juce::Justification::centred);
    }

    constexpr std::array<float, 5> frequencies {
        20.0f, 100.0f, 1000.0f, 10000.0f, 20000.0f
    };
    constexpr std::array<const char*, 5> labels {
        "20 Hz", "100 Hz", "1 kHz", "10 kHz", "20 kHz"
    };
    graphics.setColour(juce::Colour(0xff8995a4));
    graphics.setFont(juce::FontOptions(9.0f));
    for (int index = 0; index < 5; ++index)
        graphics.drawText(
            labels[static_cast<size_t>(index)],
            juce::Rectangle<float>(
                frequencyX(frequencies[static_cast<size_t>(index)],
                           frequencyAxis)
                    - 28.0f,
                frequencyAxis.getY(), 56.0f, frequencyAxis.getHeight()),
            juce::Justification::centred);

    for (const auto control : controls)
    {
        const auto bounds = controlBounds(fullArea, control);
        const auto choice = control == 1 || control == 2;
        graphics.setColour(juce::Colour(0xff202936));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        if (!choice)
        {
            graphics.setColour(
                (control == 0 ? reductionColour : accent).withAlpha(0.36f));
            graphics.fillRoundedRectangle(
                bounds.withWidth(bounds.getWidth() * value(control)), 5.0f);
            graphics.setColour(control == 0 ? reductionColour : accent);
            const auto x = bounds.getX() + value(control) * bounds.getWidth();
            graphics.fillEllipse(
                x - 3.5f, bounds.getCentreY() - 3.5f, 7.0f, 7.0f);
        }
        else
        {
            graphics.setColour(accent.withAlpha(0.16f));
            graphics.fillRoundedRectangle(bounds.reduced(2.0f), 4.0f);
        }
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(9.5f));
        graphics.drawFittedText(
            juce::String(controlMetadata(type, control).label).toUpperCase()
                + "  " + controlText(control),
            bounds.reduced(6.0f, 0.0f).toNearestInt(),
            juce::Justification::centred, 1, 0.68f);
    }
}
} // namespace

std::unique_ptr<ModuleView> createResonanceTamerView(EffectGraph& graph)
{
    return std::make_unique<ResonanceTamerModuleView>(graph);
}
} // namespace megadsp::ui
