#include "../GraphStyle.h"
#include "../GuiLayout.h"
#include "../ModuleView.h"

#include "ModuleViewCreators.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace megadsp::ui
{
namespace
{
class DynamicEqualizerModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    void configureAccessibility(juce::Component&) const override;
    bool usesFullPanel() const override { return true; }
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&,
                        const juce::MouseWheelDetails&) override;

private:
    static juce::Rectangle<float> dynamicPlotArea(
        juce::Rectangle<float>);
    static juce::Rectangle<float> dynamicPillBounds(
        juce::Rectangle<float>, int index);
    static juce::Rectangle<float> dynamicTrackBounds(
        juce::Rectangle<float>, int index);
    juce::Point<float> dynamicEqPoint(
        juce::Rectangle<float>) const;
};

void DynamicEqualizerModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Visual Dynamic EQ and De-Esser");
    component.setHelpText(
        "Drag the band node for Frequency and Range, use the mouse "
        "wheel near it for Q, and drag the Threshold line. The lower tracks "
        "edit Ratio, Attack, Release, and Stereo Link. Click the "
        "mode pills for Shape, Detector, Sidechain, and Listen.");
}

juce::Rectangle<float> DynamicEqualizerModuleView::dynamicPlotArea(
    juce::Rectangle<float> area)
{
    area.removeFromTop(34.0f);
    area.removeFromBottom(40.0f);
    return area;
}

juce::Rectangle<float> DynamicEqualizerModuleView::dynamicPillBounds(
    juce::Rectangle<float> area, int index)
{
    auto pills = area.removeFromTop(28.0f);
    const auto width = pills.getWidth() / 4.0f;
    return juce::Rectangle<float>(
        pills.getX() + width * static_cast<float>(index),
        pills.getY(), width, pills.getHeight()).reduced(3.0f, 2.0f);
}

juce::Rectangle<float> DynamicEqualizerModuleView::dynamicTrackBounds(
    juce::Rectangle<float> area, int index)
{
    auto controls = area.removeFromBottom(34.0f);
    const auto columnWidth = controls.getWidth() * 0.25f;
    return juce::Rectangle<float>(
        controls.getX() + columnWidth * static_cast<float>(index),
        controls.getY(), columnWidth, controls.getHeight())
        .reduced(4.0f, 4.0f);
}

juce::Point<float> DynamicEqualizerModuleView::dynamicEqPoint(
    juce::Rectangle<float> plot) const
{
    return {
        plot.getX() + value(0) * plot.getWidth(),
        plot.getBottom() - value(2) * plot.getHeight()
    };
}

void DynamicEqualizerModuleView::mouseDown(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int pill = 0; pill < 4; ++pill)
        if (dynamicPillBounds(area, pill).contains(event.position))
        {
            if (pill == 0)
                cycleChoice(7, 3);
            else if (pill == 1)
                cycleChoice(8, 2);
            else
            {
                if (pill == 2 && !processor.hasExternalSidechain()
                    && value(9) < 0.5f)
                    return;
                toggleParameter(pill == 2 ? 9 : 10);
            }
            return;
        }
    for (int track = 0; track < 4; ++track)
        if (dynamicTrackBounds(area, track).contains(event.position))
        {
            static constexpr std::array<int, 4> controls {
                4, 5, 6, 11
            };
            dragPrimary = controls[static_cast<size_t>(track)];
            break;
        }
    if (dragPrimary < 0)
    {
        const auto plot = dynamicPlotArea(area);
        const auto thresholdY =
            plot.getBottom() - value(3) * plot.getHeight();
        if (juce::Rectangle<float>(
                plot.getRight() - 142.0f, thresholdY - 14.0f,
                142.0f, 28.0f).contains(event.position))
            dragPrimary = 3;
        else if (event.position.getDistanceFrom(
                     dynamicEqPoint(plot)) <= 20.0f)
        {
            dragPrimary = 0;
            dragSecondary = 2;
        }
    }
    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void DynamicEqualizerModuleView::mouseDrag(
    const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto plot = dynamicPlotArea(area);
    if (dragPrimary == 0)
    {
        setValue(0, juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - plot.getX()) / plot.getWidth()));
        setValue(2, juce::jlimit(
            0.0f, 1.0f,
            (plot.getBottom() - event.position.y) / plot.getHeight()));
    }
    else if (dragPrimary == 3)
        setValue(3, juce::jlimit(
            0.0f, 1.0f,
            (plot.getBottom() - event.position.y) / plot.getHeight()));
    else
    {
        const auto trackIndex =
            dragPrimary == 4 ? 0 : dragPrimary == 5 ? 1
            : dragPrimary == 6 ? 2 : 3;
        const auto track = dynamicTrackBounds(area, trackIndex);
        setValue(dragPrimary, juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - track.getX()) / track.getWidth()));
    }
    updateDefaultDragReadout();
}

void DynamicEqualizerModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto defaults = moduleDefaults(type);
    const auto plot = dynamicPlotArea(area);
    if (event.position.getDistanceFrom(dynamicEqPoint(plot)) <= 20.0f)
    {
        setValue(0, defaults[0]);
        setValue(2, defaults[2]);
    }
    else
    {
        const auto thresholdY =
            plot.getBottom() - value(3) * plot.getHeight();
        if (juce::Rectangle<float>(
                plot.getRight() - 142.0f, thresholdY - 14.0f,
                142.0f, 28.0f).contains(event.position))
            setValue(3, defaults[3]);
        for (int track = 0; track < 4; ++track)
            if (dynamicTrackBounds(area, track).contains(event.position))
            {
                static constexpr std::array<int, 4> controls {
                    4, 5, 6, 11
                };
                const auto control =
                    controls[static_cast<size_t>(track)];
                setValue(
                    control, defaults[static_cast<size_t>(control)]);
            }
    }
    repaint();
}

void DynamicEqualizerModuleView::mouseWheelMove(
    const juce::MouseEvent& event,
    const juce::MouseWheelDetails& wheel)
{
    const auto plot = dynamicPlotArea(
        getLocalBounds().toFloat().reduced(12.0f));
    if (!plot.contains(event.position)
        || event.position.getDistanceFrom(dynamicEqPoint(plot)) > 40.0f)
        return;
    if (auto* target = parameter(1))
    {
        graph.focusKeyboardControl(1);
        const auto step = event.mods.isShiftDown() ? 0.035f : 0.12f;
        const auto delta = normalizedWheelDelta(
            wheel.deltaX, wheel.deltaY, wheel.isReversed);
        target->beginChangeGesture();
        target->setValueNotifyingHost(juce::jlimit(
            0.0f, 1.0f, target->getValue() + delta * step));
        target->endChangeGesture();
        repaint();
    }
}

void DynamicEqualizerModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto plot = dynamicPlotArea(area);
    const auto shape = megadsp::discreteIndex(value(7), 3);
    const auto frequency = exponential(20.0f, 20000.0f, value(0));
    const auto q = exponential(0.2f, 12.0f, value(1));
    const auto rangeDb = lerp(-18.0f, 12.0f, value(2));
    const auto thresholdDb = lerp(-60.0f, 0.0f, value(3));
    const auto detectorDb = juce::jlimit(
        -60.0f, 0.0f, processor.getRack().slotDetectorMeter(slot));
    const auto currentAmount = juce::jlimit(
        0.0f, std::abs(rangeDb), processor.getRack().slotMeter(slot));
    const auto currentGainDb = std::copysign(currentAmount, rangeDb);
    const auto rangeToY = [&plot](float db)
    {
        return plot.getBottom()
               - juce::jlimit(0.0f, 1.0f, (db + 18.0f) / 30.0f)
                     * plot.getHeight();
    };
    const auto detectorToY = [&plot](float db)
    {
        return plot.getBottom()
               - juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f)
                     * plot.getHeight();
    };

    drawSpectrum(graphics, plot, inputSpectrum, inputColour);
    drawSpectrum(graphics, plot, outputSpectrum, outputColour);
    for (const auto db : { -18.0f, -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
    {
        const auto y = rangeToY(db);
        graphics.setColour(
            db == 0.0f ? juce::Colour(0xff536172)
                       : juce::Colour(0xff303a46));
        graphics.drawHorizontalLine(
            juce::roundToInt(y), plot.getX(), plot.getRight());
        graphics.setColour(juce::Colour(0xff8995a4));
        graphics.drawText(
            juce::String(db, 0) + " dB",
            juce::Rectangle<float>(
                plot.getX() + 4.0f, y - 13.0f, 48.0f, 12.0f),
            juce::Justification::centredLeft);
    }

    juce::Path history;
    history.startNewSubPath(plot.getX(), plot.getBottom());
    for (size_t index = 0; index < gainReductionLevels.size(); ++index)
    {
        const auto x = plot.getX()
            + static_cast<float>(index)
                  / static_cast<float>(gainReductionLevels.size() - 1)
                  * plot.getWidth();
        const auto y = plot.getBottom()
            - juce::jlimit(0.0f, 1.0f,
                          gainReductionLevels[index] / 18.0f)
                  * plot.getHeight() * 0.26f;
        history.lineTo(x, y);
    }
    history.lineTo(plot.getRight(), plot.getBottom());
    history.closeSubPath();
    graphics.setColour(reductionColour.withAlpha(0.12f));
    graphics.fillPath(history);
    graphics.setColour(reductionColour.withAlpha(0.65f));
    graphics.strokePath(history, juce::PathStrokeType(1.2f));

    struct Coefficients
    {
        float b0, b1, b2, a1, a2;
    };
    const auto rate = static_cast<float>(
        processor.getSampleRate() > 0.0 ? processor.getSampleRate()
                                        : 48000.0);
    const auto coefficientsFor = [=](float gainDb)
    {
        const auto centre = juce::jmin(frequency, rate * 0.475f);
        const auto omega =
            juce::MathConstants<float>::twoPi * centre / rate;
        const auto cosine = std::cos(omega);
        const auto alpha = std::sin(omega) / (2.0f * q);
        const auto a = std::pow(10.0f, gainDb / 40.0f);
        if (shape == 0)
        {
            const auto a0 = 1.0f + alpha / a;
            return Coefficients {
                (1.0f + alpha * a) / a0, -2.0f * cosine / a0,
                (1.0f - alpha * a) / a0, -2.0f * cosine / a0,
                (1.0f - alpha / a) / a0
            };
        }
        const auto term = 2.0f * std::sqrt(a) * alpha;
        if (shape == 1)
        {
            const auto a0 =
                (a + 1.0f) + (a - 1.0f) * cosine + term;
            return Coefficients {
                a * ((a + 1.0f) - (a - 1.0f) * cosine + term) / a0,
                2.0f * a * ((a - 1.0f) - (a + 1.0f) * cosine) / a0,
                a * ((a + 1.0f) - (a - 1.0f) * cosine - term) / a0,
                -2.0f * ((a - 1.0f) + (a + 1.0f) * cosine) / a0,
                ((a + 1.0f) + (a - 1.0f) * cosine - term) / a0
            };
        }
        const auto a0 = (a + 1.0f) - (a - 1.0f) * cosine + term;
        return Coefficients {
            a * ((a + 1.0f) + (a - 1.0f) * cosine + term) / a0,
            -2.0f * a * ((a - 1.0f) + (a + 1.0f) * cosine) / a0,
            a * ((a + 1.0f) + (a - 1.0f) * cosine - term) / a0,
            2.0f * ((a - 1.0f) - (a + 1.0f) * cosine) / a0,
            ((a + 1.0f) - (a - 1.0f) * cosine - term) / a0
        };
    };
    const auto magnitude = [](const Coefficients& coefficients,
                              float responseFrequency, float sampleRate)
    {
        const auto omega = juce::MathConstants<float>::twoPi
                           * juce::jmin(
                               responseFrequency, sampleRate * 0.475f)
                           / sampleRate;
        const auto cosine = std::cos(omega);
        const auto sine = std::sin(omega);
        const auto cosine2 = std::cos(2.0f * omega);
        const auto sine2 = std::sin(2.0f * omega);
        const auto numeratorReal = coefficients.b0
            + coefficients.b1 * cosine + coefficients.b2 * cosine2;
        const auto numeratorImag =
            -coefficients.b1 * sine - coefficients.b2 * sine2;
        const auto denominatorReal = 1.0f
            + coefficients.a1 * cosine + coefficients.a2 * cosine2;
        const auto denominatorImag =
            -coefficients.a1 * sine - coefficients.a2 * sine2;
        return std::sqrt(
            (numeratorReal * numeratorReal
             + numeratorImag * numeratorImag)
            / juce::jmax(
                1.0e-12f,
                denominatorReal * denominatorReal
                    + denominatorImag * denominatorImag));
    };
    auto drawResponse = [&](float gainDb, juce::Colour colour,
                            float thickness)
    {
        const auto coefficients = coefficientsFor(gainDb);
        juce::Path response;
        for (int pixel = 0; pixel < static_cast<int>(plot.getWidth()); ++pixel)
        {
            const auto x = static_cast<float>(pixel) / plot.getWidth();
            const auto responseFrequency =
                20.0f * std::pow(1000.0f, x);
            const auto db = juce::Decibels::gainToDecibels(
                magnitude(coefficients, responseFrequency, rate), -72.0f);
            const auto y = rangeToY(db);
            if (pixel == 0)
                response.startNewSubPath(plot.getX(), y);
            else
                response.lineTo(
                    plot.getX() + static_cast<float>(pixel), y);
        }
        graphics.setColour(colour);
        graphics.strokePath(response, juce::PathStrokeType(thickness));
    };
    drawResponse(rangeDb, accent.withAlpha(0.28f), 1.5f);
    drawResponse(currentGainDb, accent, 3.0f);

    const auto thresholdY = detectorToY(thresholdDb);
    const auto detectorY = detectorToY(detectorDb);
    graphics.setColour(reductionColour);
    graphics.drawHorizontalLine(
        juce::roundToInt(thresholdY), plot.getX(), plot.getRight());
    auto thresholdHandle = juce::Rectangle<float>(
        plot.getRight() - 138.0f, thresholdY - 12.0f, 138.0f, 24.0f);
    graphics.fillRoundedRectangle(thresholdHandle, 5.0f);
    graphics.setColour(juce::Colour(0xff10141a));
    graphics.drawText(
        "THRESHOLD  " + juce::String(thresholdDb, 1) + " dB",
        thresholdHandle.toNearestInt(), juce::Justification::centred);
    graphics.setColour(outputColour);
    graphics.drawHorizontalLine(
        juce::roundToInt(detectorY), plot.getX(), plot.getRight());
    graphics.fillEllipse(
        plot.getX() + 4.0f, detectorY - 5.0f, 10.0f, 10.0f);
    graphics.drawText(
        "DETECTOR " + juce::String(detectorDb, 1) + " dB",
        juce::Rectangle<float>(
            plot.getX() + 18.0f, detectorY - 10.0f, 126.0f, 20.0f),
        juce::Justification::centredLeft);

    const auto node = dynamicEqPoint(plot);
    graphics.setColour(accent);
    graphics.fillEllipse(
        juce::Rectangle<float>(18.0f, 18.0f).withCentre(node));
    graphics.setColour(juce::Colour(0xff10141a));
    graphics.drawEllipse(
        juce::Rectangle<float>(10.0f, 10.0f).withCentre(node), 2.0f);
    auto nodeLabel = juce::Rectangle<float>(
        190.0f, 36.0f).withCentre(node.translated(0.0f, -26.0f));
    nodeLabel = nodeLabel.constrainedWithin(plot);
    graphics.setColour(juce::Colours::white);
    graphics.drawText(
        megadsp::formatControlValue(type, 0, value(0)) + "   "
            + megadsp::formatControlValue(type, 1, value(1)) + "   "
            + megadsp::formatControlValue(type, 2, value(2)),
        nodeLabel.toNearestInt(), juce::Justification::centred);
    graphics.setColour(reductionColour);
    graphics.drawText(
        (rangeDb >= 0.0f ? "GAIN  +" : "REDUCTION  ")
            + juce::String(currentAmount, 1) + " dB   |   10s HISTORY",
        plot.toNearestInt(), juce::Justification::centredBottom);

    constexpr std::array<float, 5> axisFrequencies {
        20.0f, 100.0f, 1000.0f, 10000.0f, 20000.0f
    };
    constexpr std::array<const char*, 5> axisLabels {
        "20", "100", "1k", "10k", "20k"
    };
    for (int marker = 0; marker < 5; ++marker)
    {
        const auto x = plot.getX()
            + std::log(axisFrequencies[static_cast<size_t>(marker)] / 20.0f)
                  / std::log(1000.0f) * plot.getWidth();
        graphics.setColour(juce::Colour(0xff8995a4));
        graphics.drawText(
            axisLabels[static_cast<size_t>(marker)],
            juce::Rectangle<float>(
                x - 22.0f, plot.getY(), 44.0f, 16.0f),
            juce::Justification::centred);
    }

    const auto shapeName = megadsp::formatControlValue(
        type, 7, value(7));
    const auto detectorName = megadsp::formatControlValue(
        type, 8, value(8));
    const auto external = value(9) >= 0.5f;
    const auto listen = value(10) >= 0.5f;
    const auto externalAvailable = processor.hasExternalSidechain();
    const std::array<juce::String, 4> pillLabels {
        "SHAPE  " + shapeName,
        "DETECTOR  " + detectorName,
        externalAvailable
            ? external ? "SIDECHAIN  EXTERNAL" : "SIDECHAIN  INTERNAL"
            : external ? "EXTERNAL UNAVAILABLE  ·  INPUT"
                       : "SIDECHAIN  UNAVAILABLE",
        listen ? "LISTEN  ON" : "LISTEN  OFF"
    };
    for (int pill = 0; pill < 4; ++pill)
    {
        const auto active = pill == 2 ? external : pill == 3 && listen;
        const auto bounds = dynamicPillBounds(area, pill);
        graphics.setColour(active ? accent.withAlpha(0.38f)
                                  : juce::Colour(0xff27303b));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour(
            pill == 2 && !externalAvailable
                ? reductionColour
                : active ? juce::Colours::white
                         : juce::Colour(0xffa8b3c0));
        graphics.drawText(
            pillLabels[static_cast<size_t>(pill)],
            bounds.toNearestInt(), juce::Justification::centred);
    }

    constexpr std::array<int, 4> trackControls { 4, 5, 6, 11 };
    constexpr std::array<const char*, 4> trackNames {
        "RATIO", "ATTACK", "RELEASE", "STEREO LINK"
    };
    for (int track = 0; track < 4; ++track)
    {
        const auto control =
            trackControls[static_cast<size_t>(track)];
        const auto bounds = dynamicTrackBounds(area, track);
        graphics.setColour(juce::Colour(0xff27303b));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour(accent.withAlpha(0.32f));
        if (track > 0)
            graphics.setColour(accent.withAlpha(0.18f));
        graphics.fillRoundedRectangle(
            bounds.withWidth(bounds.getWidth() * value(control)), 5.0f);
        const auto x = bounds.getX() + value(control) * bounds.getWidth();
        graphics.setColour(accent);
        graphics.fillEllipse(
            x - 4.0f, bounds.getCentreY() - 4.0f, 8.0f, 8.0f);
        graphics.setColour(juce::Colours::white);
        graphics.drawText(
            juce::String(trackNames[static_cast<size_t>(track)]) + "  "
                + megadsp::formatControlValue(
                    type, control, value(control)),
            bounds.toNearestInt(), juce::Justification::centred);
    }
}
} // namespace

std::unique_ptr<ModuleView> createDynamicEqualizerView(EffectGraph& graph)
{
    return std::make_unique<DynamicEqualizerModuleView>(graph);
}
} // namespace megadsp::ui
