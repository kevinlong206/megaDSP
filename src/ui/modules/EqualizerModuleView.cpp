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
class EqualizerModuleView final : public ModuleView
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
    static juce::Rectangle<float> eqPlotArea(juce::Rectangle<float>);
    static juce::Rectangle<float> eqOutputBounds(juce::Rectangle<float>);
    static juce::Rectangle<float> eqLowModeBounds(juce::Rectangle<float>);
    static juce::Rectangle<float> eqHighModeBounds(juce::Rectangle<float>);
    juce::Point<float> eqPoint(
        int band, juce::Rectangle<float>) const;
};

void EqualizerModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Visual parametric EQ");
    component.setHelpText(
        "Drag points for frequency and gain or resonance. "
        "Place the pointer near a point and use the mouse wheel for Q. "
        "Drag low or high points into "
        "the labeled edge lanes to engage rolloff filters.");
}

juce::Rectangle<float> EqualizerModuleView::eqPlotArea(
    juce::Rectangle<float> area)
{
    area.removeFromTop(22.0f);
    area.removeFromBottom(20.0f);
    area.removeFromRight(44.0f);
    return area;
}

juce::Rectangle<float> EqualizerModuleView::eqOutputBounds(
    juce::Rectangle<float> area)
{
    area.removeFromTop(28.0f);
    area.removeFromBottom(20.0f);
    return area.removeFromRight(30.0f);
}

juce::Rectangle<float> EqualizerModuleView::eqLowModeBounds(
    juce::Rectangle<float> area)
{
    return area.removeFromTop(20.0f).removeFromLeft(112.0f);
}

juce::Rectangle<float> EqualizerModuleView::eqHighModeBounds(
    juce::Rectangle<float> area)
{
    auto top = area.removeFromTop(20.0f);
    top.removeFromRight(44.0f);
    return top.removeFromRight(112.0f);
}

juce::Point<float> EqualizerModuleView::eqPoint(
    int band, juce::Rectangle<float> area) const
{
    static constexpr std::array<float, 3> lows {
        30.0f, 150.0f, 1500.0f
    };
    static constexpr std::array<float, 3> highs {
        1200.0f, 7000.0f, 20000.0f
    };
    const auto frequency = exponential(
        lows[static_cast<size_t>(band)],
        highs[static_cast<size_t>(band)], value(band * 3));
    const auto x = std::log(frequency / 20.0f) / std::log(1000.0f);
    const auto rolloff =
        (band == 0 && equalizerLowIsHighPass(value(10)))
        || (band == 2 && equalizerHighIsLowPass(value(11)));
    return {
        area.getX() + x * area.getWidth(),
        rolloff
            ? area.getBottom()
                - value(band * 3 + 2) * area.getHeight()
            : area.getBottom()
                - value(band * 3 + 1) * area.getHeight()
    };
}

void EqualizerModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (eqOutputBounds(area).contains(event.position))
    {
        dragPrimary = 9;
        dragSecondary = -1;
        beginGestures();
        mouseDrag(event);
        return;
    }
    if (eqLowModeBounds(area).contains(event.position))
    {
        toggleParameter(10);
        return;
    }
    if (eqHighModeBounds(area).contains(event.position))
    {
        toggleParameter(11);
        return;
    }
    const auto plot = eqPlotArea(area);
    auto closest = std::numeric_limits<float>::max();
    for (int band = 0; band < 3; ++band)
    {
        const auto point = eqPoint(band, plot);
        const auto distance = point.getDistanceFrom(event.position);
        if (distance < closest)
        {
            closest = distance;
            dragPrimary = band * 3;
            const auto rolloff =
                (band == 0 && equalizerLowIsHighPass(value(10)))
                || (band == 2 && equalizerHighIsLowPass(value(11)));
            dragSecondary = band * 3 + (rolloff ? 2 : 1);
        }
    }
    if (closest > 18.0f)
        dragPrimary = dragSecondary = -1;
    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void EqualizerModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (dragPrimary == 9)
    {
        const auto output = eqOutputBounds(area);
        setValue(9, juce::jlimit(
            0.0f, 1.0f,
            (output.getBottom() - event.position.y)
                / output.getHeight()));
        dragReadout =
            "Output  " + formatControlValue(type, 9, value(9));
        repaint();
        return;
    }
    const auto plot = eqPlotArea(area);
    const auto eqX = juce::jlimit(
        0.0f, 1.0f,
        (event.position.x - plot.getX()) / plot.getWidth());
    const auto eqY = juce::jlimit(
        0.0f, 1.0f,
        (plot.getBottom() - event.position.y) / plot.getHeight());
    static constexpr std::array<float, 3> lows {
        30.0f, 150.0f, 1500.0f
    };
    static constexpr std::array<float, 3> highs {
        1200.0f, 7000.0f, 20000.0f
    };
    const auto band = dragPrimary / 3;
    const auto frequency = 20.0f * std::pow(1000.0f, eqX);
    const auto normalized =
        std::log(frequency / lows[static_cast<size_t>(band)])
        / std::log(highs[static_cast<size_t>(band)]
                   / lows[static_cast<size_t>(band)]);
    setValue(dragPrimary, juce::jlimit(0.0f, 1.0f, normalized));
    const auto activationControl =
        band == 0 && eqX <= 0.08f ? 10
        : band == 2 && eqX >= 0.95f ? 11 : -1;
    if (activationControl >= 0 && value(activationControl) <= 0.5f)
    {
        auto* topology = parameter(activationControl);
        topology->beginChangeGesture();
        topology->setValueNotifyingHost(1.0f);
        topology->endChangeGesture();
        if (dragSecondary != band * 3 + 2)
        {
            if (auto* oldSecondary = parameter(dragSecondary))
                oldSecondary->endChangeGesture();
            dragSecondary = band * 3 + 2;
            if (auto* newSecondary = parameter(dragSecondary))
                newSecondary->beginChangeGesture();
        }
    }
    setValue(dragSecondary, eqY);
    updateDefaultDragReadout();
}

void EqualizerModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (eqOutputBounds(area).contains(event.position))
    {
        setValue(9, moduleDefaults(type)[9]);
        return;
    }
    const auto plot = eqPlotArea(area);
    for (int band = 0; band < 3; ++band)
        if (eqPoint(band, plot).getDistanceFrom(event.position) <= 18.0f)
        {
            const auto defaults = moduleDefaults(type);
            for (int offset = 0; offset < 3; ++offset)
                setValue(
                    band * 3 + offset,
                    defaults[static_cast<size_t>(band * 3 + offset)]);
            repaint();
            return;
        }
}

void EqualizerModuleView::mouseWheelMove(
    const juce::MouseEvent& event,
    const juce::MouseWheelDetails& wheel)
{
    const auto plot = eqPlotArea(
        getLocalBounds().toFloat().reduced(12.0f));
    if (!plot.contains(event.position))
        return;
    int closestBand = 0;
    auto closestDistance = std::numeric_limits<float>::max();
    for (int band = 0; band < 3; ++band)
    {
        const auto distance =
            eqPoint(band, plot).getDistanceFrom(event.position);
        if (distance < closestDistance)
        {
            closestDistance = distance;
            closestBand = band;
        }
    }
    if (closestDistance > 40.0f)
        return;
    const auto control = closestBand * 3 + 2;
    if (auto* target = parameter(control))
    {
        graph.focusKeyboardControl(control);
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

void EqualizerModuleView::paint(juce::Graphics& graphics, juce::Rectangle<float> area)
{
    auto fullArea = area;
    const auto plot = eqPlotArea(area);
    const auto lowLane = plot.withWidth(plot.getWidth() * 0.08f);
    const auto highLane = plot.withLeft(
        plot.getRight() - plot.getWidth() * 0.05f);
    graphics.setColour(accent.withAlpha(dragPrimary == 0 ? 0.18f : 0.06f));
    graphics.fillRect(lowLane);
    graphics.setColour(outputColour.withAlpha(
        dragPrimary == 6 ? 0.18f : 0.06f));
    graphics.fillRect(highLane);
    graphics.setColour(juce::Colour(0xff8995a4));
    graphics.drawText("DROP\nFOR HP", lowLane.toNearestInt(),
                      juce::Justification::centred);
    graphics.drawText("DROP\nFOR LP", highLane.toNearestInt(),
                      juce::Justification::centred);
    drawSpectrum(graphics, plot, inputSpectrum, inputColour);
    drawSpectrum(graphics, plot, outputSpectrum, outputColour);
    struct Coefficients
    {
        float b0, b1, b2, a1, a2;
    };
    const auto sampleRate = static_cast<float>(
        processor.getSampleRate() > 0.0 ? processor.getSampleRate()
                                        : 48000.0);
    const auto magnitude = [sampleRate](const Coefficients& coefficients,
                                        float frequency)
    {
        const auto omega = juce::MathConstants<float>::twoPi
                           * juce::jmin(frequency, sampleRate * 0.475f)
                           / sampleRate;
        const auto cos1 = std::cos(omega);
        const auto sin1 = std::sin(omega);
        const auto cos2 = std::cos(2.0f * omega);
        const auto sin2 = std::sin(2.0f * omega);
        const auto numeratorReal = coefficients.b0
            + coefficients.b1 * cos1 + coefficients.b2 * cos2;
        const auto numeratorImag =
            -coefficients.b1 * sin1 - coefficients.b2 * sin2;
        const auto denominatorReal = 1.0f
            + coefficients.a1 * cos1 + coefficients.a2 * cos2;
        const auto denominatorImag =
            -coefficients.a1 * sin1 - coefficients.a2 * sin2;
        return std::sqrt(
            (numeratorReal * numeratorReal
             + numeratorImag * numeratorImag)
            / juce::jmax(
                1.0e-12f,
                denominatorReal * denominatorReal
                    + denominatorImag * denominatorImag));
    };
    juce::Path response;
    for (int pixel = 0; pixel < static_cast<int>(plot.getWidth()); ++pixel)
    {
        const auto x = static_cast<float>(pixel) / plot.getWidth();
        const auto frequency = 20.0f * std::pow(1000.0f, x);
        float db = 0.0f;
        for (int band = 0; band < 3; ++band)
        {
            static constexpr std::array<float, 3> lows {
                30.0f, 150.0f, 1500.0f
            };
            static constexpr std::array<float, 3> highs {
                1200.0f, 7000.0f, 20000.0f
            };
            const auto centre = exponential(
                lows[static_cast<size_t>(band)],
                juce::jmin(highs[static_cast<size_t>(band)],
                           sampleRate * 0.475f),
                value(band * 3));
            const auto q = exponential(0.2f, 10.0f, value(band * 3 + 2));
            const auto omega = juce::MathConstants<float>::twoPi
                               * centre / sampleRate;
            const auto alpha = std::sin(omega) / (2.0f * q);
            const auto cosine = std::cos(omega);
            Coefficients coefficients {};
            if (band == 0
                && megadsp::equalizerLowIsHighPass(value(10)))
            {
                const auto a0 = 1.0f + alpha;
                coefficients = {
                    (1.0f + cosine) * 0.5f / a0,
                    -(1.0f + cosine) / a0,
                    (1.0f + cosine) * 0.5f / a0,
                    -2.0f * cosine / a0,
                    (1.0f - alpha) / a0
                };
            }
            else if (band == 2
                     && megadsp::equalizerHighIsLowPass(value(11)))
            {
                const auto a0 = 1.0f + alpha;
                coefficients = {
                    (1.0f - cosine) * 0.5f / a0,
                    (1.0f - cosine) / a0,
                    (1.0f - cosine) * 0.5f / a0,
                    -2.0f * cosine / a0,
                    (1.0f - alpha) / a0
                };
            }
            else
            {
                const auto gainDb =
                    lerp(-18.0f, 18.0f, value(band * 3 + 1));
                const auto a = std::pow(10.0f, gainDb / 40.0f);
                const auto a0 = 1.0f + alpha / a;
                coefficients = {
                    (1.0f + alpha * a) / a0,
                    -2.0f * cosine / a0,
                    (1.0f - alpha * a) / a0,
                    -2.0f * cosine / a0,
                    (1.0f - alpha / a) / a0
                };
            }
            db += juce::Decibels::gainToDecibels(
                magnitude(coefficients, frequency), -72.0f);
        }
        const auto y = juce::jlimit(
            plot.getY(), plot.getBottom(),
            plot.getCentreY() - db / 36.0f * plot.getHeight());
        if (pixel == 0) response.startNewSubPath(plot.getX(), y);
        else response.lineTo(plot.getX() + static_cast<float>(pixel), y);
    }
    graphics.setColour(accent);
    graphics.strokePath(response, juce::PathStrokeType(2.5f));
    for (int band = 0; band < 3; ++band)
    {
        const auto lowRolloff =
            band == 0 && megadsp::equalizerLowIsHighPass(value(10));
        const auto highRolloff =
            band == 2 && megadsp::equalizerHighIsLowPass(value(11));
        graphics.setColour(accent);
        graphics.fillEllipse(juce::Rectangle<float>(12.0f, 12.0f)
                                 .withCentre(eqPoint(band, plot)));
        graphics.setColour(juce::Colours::black);
        graphics.drawText(lowRolloff ? "HP" : highRolloff ? "LP"
                                                           : juce::String(band + 1),
                          juce::Rectangle<float>(12.0f, 12.0f)
                              .withCentre(eqPoint(band, plot)),
                          juce::Justification::centred);
        const auto frequency = exponential(
            band == 0 ? 30.0f : band == 1 ? 150.0f : 1500.0f,
            band == 0 ? 1200.0f : band == 1 ? 7000.0f : 20000.0f,
            value(band * 3));
        const auto detail = (lowRolloff || highRolloff)
            ? juce::String(frequency, 0) + " Hz  Q "
                  + juce::String(exponential(
                      0.2f, 10.0f, value(band * 3 + 2)), 2)
            : juce::String(frequency, 0) + " Hz  "
                  + juce::String(lerp(
                      -18.0f, 18.0f, value(band * 3 + 1)), 1) + " dB";
        auto readout = juce::Rectangle<float>(
            132.0f, 18.0f).withCentre(
                eqPoint(band, plot).translated(0.0f, -18.0f));
        readout = readout.constrainedWithin(plot);
        graphics.setColour(juce::Colours::white.withAlpha(0.82f));
        graphics.drawText(detail, readout.toNearestInt(),
                          juce::Justification::centred);
    }

    auto drawModePill = [&](juce::Rectangle<float> bounds,
                           const juce::String& text, bool active)
    {
        graphics.setColour(active ? accent.withAlpha(0.38f)
                                 : juce::Colour(0xff27303b));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour(active ? juce::Colours::white
                                 : juce::Colour(0xffa8b3c0));
        graphics.drawText(text, bounds.toNearestInt(),
                          juce::Justification::centred);
    };
    drawModePill(eqLowModeBounds(area),
                 megadsp::equalizerLowIsHighPass(value(10))
                     ? "LOW: HP" : "LOW: BELL",
                 megadsp::equalizerLowIsHighPass(value(10)));
    drawModePill(eqHighModeBounds(area),
                 megadsp::equalizerHighIsLowPass(value(11))
                     ? "HIGH: LP" : "HIGH: BELL",
                 megadsp::equalizerHighIsLowPass(value(11)));

    const auto outputBounds = eqOutputBounds(area);
    graphics.setColour(juce::Colour(0xff27303b));
    graphics.fillRoundedRectangle(outputBounds, 5.0f);
    const auto outputY = outputBounds.getBottom()
        - value(9) * outputBounds.getHeight();
    graphics.setColour(accent);
    graphics.drawHorizontalLine(
        juce::roundToInt(outputY), outputBounds.getX(),
        outputBounds.getRight());
    graphics.fillEllipse(outputBounds.getCentreX() - 5.0f,
                         outputY - 5.0f, 10.0f, 10.0f);
    graphics.setColour(juce::Colours::white);
    graphics.drawText("OUT\n"
                          + megadsp::formatControlValue(type, 9, value(9)),
                      outputBounds.toNearestInt(),
                      juce::Justification::centredBottom);

    constexpr std::array<const char*, 3> ranges {
        "LOW  30 Hz - 1.2 kHz",
        "MID  150 Hz - 7 kHz",
        "HIGH  1.5 kHz - 20 kHz"
    };
    for (int band = 0; band < 3; ++band)
    {
        auto labelArea = fullArea.removeFromLeft(
            fullArea.getWidth() / static_cast<float>(3 - band));
        graphics.setColour(juce::Colour(0xffa8b3c0));
        graphics.drawText(ranges[static_cast<size_t>(band)],
                          labelArea.withHeight(18.0f).toNearestInt(),
                          juce::Justification::centred);
    }
    constexpr std::array<float, 5> axisFrequencies {
        20.0f, 100.0f, 1000.0f, 10000.0f, 20000.0f
    };
    constexpr std::array<const char*, 5> axisLabels {
        "20 Hz", "100 Hz", "1 kHz", "10 kHz", "20 kHz"
    };
    for (int marker = 0; marker < 5; ++marker)
    {
        const auto markerX = plot.getX()
            + std::log(axisFrequencies[static_cast<size_t>(marker)] / 20.0f)
                  / std::log(1000.0f) * plot.getWidth();
        graphics.setColour(juce::Colour(0xff8995a4));
        graphics.drawText(
            axisLabels[static_cast<size_t>(marker)],
            juce::Rectangle<float>(markerX - 28.0f, plot.getBottom(),
                                   56.0f, 18.0f).toNearestInt(),
            marker == 0 ? juce::Justification::centredLeft
            : marker == 4 ? juce::Justification::centredRight
                          : juce::Justification::centred);
    }
    graphics.setColour(juce::Colour(0xff8995a4));
    graphics.drawText(
        "Wheel near a node adjusts Q",
        juce::Rectangle<float>(plot.getRight() - 190.0f, plot.getY(),
                               184.0f, 18.0f).toNearestInt(),
        juce::Justification::centredRight);
}
} // namespace

std::unique_ptr<ModuleView> createEqualizerView(EffectGraph& graph)
{
    return std::make_unique<EqualizerModuleView>(graph);
}
} // namespace megadsp::ui
