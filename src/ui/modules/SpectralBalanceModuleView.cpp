#include "../GraphStyle.h"
#include "../ModuleView.h"
#include "../../modules/SpectralBalance.h"

#include "ModuleViewCreators.h"

#include <array>
#include <cmath>

namespace megadsp::ui
{
std::unique_ptr<ModuleView> createSpectralBalanceView(EffectGraph&);

namespace
{
class SpectralBalanceModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;

    bool usesFullPanel() const override { return true; }
    void configureAccessibility(juce::Component& component) const override
    {
        component.setTitle("Spectral Balance target and correction display");
        component.setHelpText(
            "Captured DSP measured, broad measured, target, and applied "
            "correction curves are shown across frequency. Drag the Low, "
            "Presence, or Air target nodes vertically, or use any lower "
            "control. Click Contour or Detail to choose a mode. Double-click "
            "a control to restore its default.");
    }

    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    static constexpr std::array<int, 9> controls {
        0, 1, 2, 3, 4, 5, 6, 7, 8
    };
    static constexpr std::array<int, 3> targetControls { 2, 3, 4 };
    static constexpr std::array<float, 3> targetFrequencies {
        120.0f, 3000.0f, 12000.0f
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

    static float levelY(float db, juce::Rectangle<float> area)
    {
        return juce::jmap(
            juce::jlimit(-100.0f, 12.0f, db), -100.0f, 12.0f,
            area.getBottom(), area.getY());
    }

    static float correctionY(float db, juce::Rectangle<float> area)
    {
        return juce::jmap(
            juce::jlimit(-9.0f, 9.0f, db), -9.0f, 9.0f,
            area.getBottom(), area.getY());
    }

    static juce::Path telemetryPath(
        const ContinuousTelemetrySnapshot& telemetry, std::uint32_t lane,
        juce::Rectangle<float> area, bool correction)
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
            const auto y = correction ? correctionY(sample, area)
                                      : levelY(sample, area);
            if (point == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
        return path;
    }

    static float targetCurveDb(
        const ContinuousTelemetrySnapshot& telemetry, float frequency)
    {
        const auto normalized =
            std::log(juce::jlimit(20.0f, 20000.0f, frequency) / 20.0f)
            / std::log(1000.0f);
        const auto point = static_cast<std::uint32_t>(juce::roundToInt(
            normalized
            * static_cast<float>(juce::jmax(1u, telemetry.historyCount - 1u))));
        return continuousTelemetryHistoryValue(telemetry, 1, point);
    }

    juce::Point<float> targetNode(
        int control, juce::Rectangle<float> spectrum,
        const ContinuousTelemetrySnapshot* telemetry) const
    {
        const auto index = control - 2;
        const auto frequency =
            targetFrequencies[static_cast<size_t>(index)];
        const auto db = telemetry != nullptr
            ? targetCurveDb(*telemetry, frequency)
            : value(control) * 12.0f - 6.0f;
        return { frequencyX(frequency, spectrum), levelY(db, spectrum) };
    }

    juce::String controlText(int control) const
    {
        switch (control)
        {
            case 0:
            case 6: return formatControlValue(type, control, value(control));
            case 1:
            case 7: return juce::String(value(control) * 100.0f, 0) + "%";
            case 2:
            case 3:
            case 4:
            {
                const auto db = value(control) * 12.0f - 6.0f;
                return (db > 0.05f ? "+" : "")
                    + juce::String(db, 1) + " dB";
            }
            case 5:
                return juce::String(
                           0.5f * std::pow(60.0f, value(control)), 1)
                    + " s";
            case 8:
                return juce::String(value(control) * 30.0f - 18.0f, 1)
                    + " dB";
            default: return {};
        }
    }

    float dragStartY = 0.0f;
    float dragStartValue = 0.0f;
    bool draggingTargetNode = false;
};

void SpectralBalanceModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    draggingTargetNode = false;
    for (const auto control : controls)
        if (controlBounds(area, control).contains(event.position))
        {
            if (control == 0)
            {
                cycleChoice(control, 5);
                return;
            }
            if (control == 6)
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
    auto graphArea = plot.reduced(8.0f);
    graphArea.removeFromTop(26.0f);
    graphArea.removeFromBottom(16.0f);
    graphArea.removeFromBottom(
        juce::jmax(48.0f, graphArea.getHeight() * 0.28f) + 6.0f);
    ContinuousTelemetrySnapshot telemetry;
    const auto hasTelemetry =
        readContinuousTelemetry(telemetry) && telemetry.sequence != 0
        && telemetry.historyValueCount >= 4 && telemetry.historyCount >= 2;
    auto bestDistance = 19.0f;
    for (const auto control : targetControls)
    {
        const auto distance = event.position.getDistanceFrom(
            targetNode(control, graphArea, hasTelemetry ? &telemetry : nullptr));
        if (distance < bestDistance)
        {
            bestDistance = distance;
            dragPrimary = control;
        }
    }
    if (dragPrimary >= 0)
    {
        draggingTargetNode = true;
        dragStartY = event.position.y;
        dragStartValue = value(dragPrimary);
        beginGestures();
    }
}

void SpectralBalanceModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (draggingTargetNode)
    {
        const auto height = juce::jmax(1.0f, plotBounds(area).getHeight());
        setValue(dragPrimary, juce::jlimit(
            0.0f, 1.0f,
            dragStartValue + (dragStartY - event.position.y) / height));
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

void SpectralBalanceModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (const auto control : controls)
        if (controlBounds(area, control).contains(event.position))
        {
            resetToDefault(control);
            return;
        }

    const auto plot = plotBounds(area);
    auto graphArea = plot.reduced(8.0f);
    graphArea.removeFromTop(26.0f);
    graphArea.removeFromBottom(16.0f);
    graphArea.removeFromBottom(
        juce::jmax(48.0f, graphArea.getHeight() * 0.28f) + 6.0f);
    ContinuousTelemetrySnapshot telemetry;
    const auto hasTelemetry =
        readContinuousTelemetry(telemetry) && telemetry.sequence != 0
        && telemetry.historyValueCount >= 4 && telemetry.historyCount >= 2;
    for (const auto control : targetControls)
        if (event.position.getDistanceFrom(
                targetNode(
                    control, graphArea, hasTelemetry ? &telemetry : nullptr))
            <= 19.0f)
        {
            resetToDefault(control);
            return;
        }
}

void SpectralBalanceModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto fullArea = area;
    auto header = area.removeFromTop(28.0f);
    graphics.setColour(accent);
    graphics.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    graphics.drawText(
        "ADAPTIVE BALANCE", header.removeFromLeft(header.getWidth() * 0.34f)
                                .reduced(6.0f, 0.0f),
        juce::Justification::centredLeft);
    graphics.setColour(juce::Colours::white.withAlpha(0.72f));
    graphics.setFont(juce::FontOptions(10.0f));
    graphics.drawFittedText(
        "MEASURED   ·   LONG-TERM   ·   TARGET   ·   CORRECTION",
        header.reduced(5.0f, 0.0f).toNearestInt(),
        juce::Justification::centredRight, 1, 0.65f);

    const auto plot = plotBounds(fullArea);
    graphics.setColour(juce::Colour(0xe6121821));
    graphics.fillRoundedRectangle(plot, 8.0f);
    auto graphArea = plot.reduced(8.0f);
    auto metrics = graphArea.removeFromTop(24.0f);
    graphArea.removeFromTop(2.0f);
    auto frequencyAxis = graphArea.removeFromBottom(16.0f);
    auto correctionArea = graphArea.removeFromBottom(
        juce::jmax(48.0f, graphArea.getHeight() * 0.28f));
    graphArea.removeFromBottom(6.0f);
    const auto spectrumArea = graphArea;

    ContinuousTelemetrySnapshot telemetry;
    const auto hasTelemetry =
        readContinuousTelemetry(telemetry) && telemetry.sequence != 0
        && telemetry.valueCount >= SpectralBalanceModule::telemetryValueCount
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
    for (const auto db : { -9.0f, 0.0f, 9.0f })
    {
        const auto y = correctionY(db, correctionArea);
        graphics.setColour(
            db == 0.0f ? juce::Colour(0xff536172)
                       : juce::Colour(0xff303a46));
        graphics.drawHorizontalLine(
            juce::roundToInt(y), correctionArea.getX(),
            correctionArea.getRight());
        graphics.setColour(juce::Colour(0xff8995a4));
        graphics.drawText(
            (db > 0.0f ? "+" : "") + juce::String(db, 0) + " dB",
            juce::Rectangle<float>(
                correctionArea.getX() + 3.0f, y - 12.0f, 45.0f, 12.0f),
            juce::Justification::centredLeft);
    }

    if (hasTelemetry)
    {
        auto measured = telemetryPath(telemetry, 0, spectrumArea, false);
        auto target = telemetryPath(telemetry, 1, spectrumArea, false);
        auto correction = telemetryPath(telemetry, 2, correctionArea, true);
        auto broad = telemetryPath(telemetry, 3, spectrumArea, false);
        graphics.setColour(inputColour.withAlpha(0.42f));
        graphics.strokePath(measured, juce::PathStrokeType(1.3f));
        graphics.setColour(inputColour.withAlpha(0.86f));
        graphics.strokePath(broad, juce::PathStrokeType(2.1f));
        graphics.setColour(outputColour);
        graphics.strokePath(target, juce::PathStrokeType(2.5f));
        graphics.setColour(reductionColour);
        graphics.strokePath(correction, juce::PathStrokeType(2.6f));

        constexpr std::array<const char*, 3> nodeLabels {
            "LOW", "PRESENCE", "AIR"
        };
        constexpr std::array<SpectralBalanceModule::TelemetryValue, 3>
            valueIndices {
                SpectralBalanceModule::lowTargetDb,
                SpectralBalanceModule::presenceTargetDb,
                SpectralBalanceModule::airTargetDb
            };
        for (int index = 0; index < 3; ++index)
        {
            const auto control =
                targetControls[static_cast<size_t>(index)];
            const auto node = targetNode(control, spectrumArea, &telemetry);
            graphics.setColour(outputColour);
            graphics.fillEllipse(
                juce::Rectangle<float>(14.0f, 14.0f).withCentre(node));
            graphics.setColour(juce::Colour(0xff101820));
            graphics.drawEllipse(
                juce::Rectangle<float>(7.0f, 7.0f).withCentre(node), 1.5f);
            const auto targetDb =
                telemetry.values[valueIndices[static_cast<size_t>(index)]];
            auto label = juce::Rectangle<float>(104.0f, 17.0f)
                .withCentre(node.translated(
                    0.0f, index == 1 ? 15.0f : -15.0f))
                .constrainedWithin(spectrumArea);
            graphics.setColour(juce::Colours::white);
            graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
            graphics.drawFittedText(
                juce::String(nodeLabels[static_cast<size_t>(index)]) + "  "
                    + (targetDb > 0.05f ? "+" : "")
                    + juce::String(targetDb, 1) + " dB",
                label.toNearestInt(), juce::Justification::centred, 1, 0.72f);
        }

        const std::array<juce::String, 3> metricText {
            "MEASURED  "
                + juce::String(
                    telemetry.values[SpectralBalanceModule::measuredLevelDb],
                    1)
                + " dB",
            "CORRECTION  "
                + juce::String(
                    telemetry.values[
                        SpectralBalanceModule::maximumCorrectionDb],
                    1)
                + " dB",
            "ADAPTATION  "
                + juce::String(
                    telemetry.values[SpectralBalanceModule::adaptationSeconds],
                    1)
                + " s"
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
            "WAITING FOR CAPTURED DSP BALANCE",
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
        const auto choice = control == 0 || control == 6;
        graphics.setColour(juce::Colour(0xff202936));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        if (!choice)
        {
            graphics.setColour(
                (control >= 2 && control <= 4 ? outputColour : accent)
                    .withAlpha(0.34f));
            graphics.fillRoundedRectangle(
                bounds.withWidth(bounds.getWidth() * value(control)), 5.0f);
            graphics.setColour(
                control >= 2 && control <= 4 ? outputColour : accent);
            const auto x = bounds.getX() + value(control) * bounds.getWidth();
            graphics.fillEllipse(
                x - 3.5f, bounds.getCentreY() - 3.5f, 7.0f, 7.0f);
        }
        else
        {
            graphics.setColour(outputColour.withAlpha(0.15f));
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

std::unique_ptr<ModuleView> createSpectralBalanceView(EffectGraph& graph)
{
    return std::make_unique<SpectralBalanceModuleView>(graph);
}
} // namespace megadsp::ui
