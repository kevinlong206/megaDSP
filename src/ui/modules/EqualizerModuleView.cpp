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
    static juce::Rectangle<float> eqModeStripBounds(
        juce::Rectangle<float>);
    juce::Point<float> eqPoint(
        int band, juce::Rectangle<float>) const;
    bool bandIsActive(int band) const;
    int bandAt(juce::Point<float>, juce::Rectangle<float>) const;
    void removeBand(int band);
    void setBandMode(int band, EqualizerBandMode mode);
    void addBandAt(juce::Point<float>, juce::Rectangle<float>);

    int selectedBand = -1;
};

void EqualizerModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Visual parametric EQ");
    component.setHelpText(
        "Double-click the graph to add a band. Drag a node for frequency and "
        "gain, use the mouse wheel for Q, and use the selected node's shape "
        "strip for Bell, Shelf, or Cut. Double-click a node to remove it.");
}

juce::Rectangle<float> EqualizerModuleView::eqPlotArea(
    juce::Rectangle<float> area)
{
    area.removeFromTop(30.0f);
    area.removeFromBottom(58.0f);
    area.removeFromRight(52.0f);
    return area;
}

juce::Rectangle<float> EqualizerModuleView::eqOutputBounds(
    juce::Rectangle<float> area)
{
    area.removeFromTop(34.0f);
    area.removeFromBottom(58.0f);
    return area.removeFromRight(34.0f);
}

juce::Rectangle<float> EqualizerModuleView::eqModeStripBounds(
    juce::Rectangle<float> area)
{
    area.removeFromRight(52.0f);
    auto strip = area.removeFromBottom(36.0f);
    return strip.withSizeKeepingCentre(
        juce::jmin(400.0f, strip.getWidth()), 28.0f);
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
    const auto cut = band != 1
        && equalizerBandMode(value(band == 0 ? 10 : 11))
               == EqualizerBandMode::cut;
    return {
        area.getX() + x * area.getWidth(),
        cut
            ? area.getBottom()
                - value(band * 3 + 2) * area.getHeight()
            : area.getBottom()
                - value(band * 3 + 1) * area.getHeight()
    };
}

bool EqualizerModuleView::bandIsActive(int band) const
{
    if (band == selectedBand)
        return true;
    if (band != 1
        && equalizerBandMode(value(band == 0 ? 10 : 11))
               != EqualizerBandMode::bell)
        return true;
    return std::abs(value(band * 3 + 1) - 0.5f) > 0.0001f;
}

int EqualizerModuleView::bandAt(
    juce::Point<float> position, juce::Rectangle<float> plot) const
{
    auto closestDistance = 18.0f;
    auto closestBand = -1;
    for (int band = 0; band < 3; ++band)
    {
        if (!bandIsActive(band))
            continue;
        const auto distance = eqPoint(band, plot).getDistanceFrom(position);
        if (distance < closestDistance)
        {
            closestDistance = distance;
            closestBand = band;
        }
    }
    return closestBand;
}

void EqualizerModuleView::setBandMode(
    int band, EqualizerBandMode mode)
{
    if (band == 1)
        return;
    const auto control = band == 0 ? 10 : 11;
    if (auto* target = parameter(control))
    {
        target->beginChangeGesture();
        target->setValueNotifyingHost(discreteValue(
            static_cast<int>(mode), 3));
        target->endChangeGesture();
    }
    repaint();
}

void EqualizerModuleView::removeBand(int band)
{
    if (!juce::isPositiveAndBelow(band, 3))
        return;
    if (band != 1)
        setBandMode(band, EqualizerBandMode::bell);
    if (auto* gain = parameter(band * 3 + 1))
    {
        gain->beginChangeGesture();
        gain->setValueNotifyingHost(0.5f);
        gain->endChangeGesture();
    }
    selectedBand = -1;
    repaint();
}

void EqualizerModuleView::addBandAt(
    juce::Point<float> position, juce::Rectangle<float> plot)
{
    const auto x = juce::jlimit(
        0.0f, 1.0f, (position.x - plot.getX()) / plot.getWidth());
    const auto frequency = 20.0f * std::pow(1000.0f, x);
    auto band = frequency < 350.0f ? 0 : frequency > 4000.0f ? 2 : 1;
    if (bandIsActive(band))
        for (const auto candidate : { 1, 0, 2 })
            if (!bandIsActive(candidate))
            {
                band = candidate;
                break;
            }

    static constexpr std::array<float, 3> lows {
        30.0f, 150.0f, 1500.0f
    };
    static constexpr std::array<float, 3> highs {
        1200.0f, 7000.0f, 20000.0f
    };
    const auto frequencyValue = juce::jlimit(
        0.0f, 1.0f,
        std::log(frequency / lows[static_cast<size_t>(band)])
            / std::log(highs[static_cast<size_t>(band)]
                       / lows[static_cast<size_t>(band)]));
    auto gainValue = juce::jlimit(
        0.0f, 1.0f,
        (plot.getBottom() - position.y) / plot.getHeight());
    if (std::abs(gainValue - 0.5f) < 0.015f)
        gainValue = 0.515f;

    const auto defaults = moduleDefaults(type);
    for (const auto [control, newValue] : {
             std::pair { band * 3, frequencyValue },
             std::pair { band * 3 + 1, gainValue },
             std::pair { band * 3 + 2,
                         defaults[static_cast<size_t>(band * 3 + 2)] } })
        if (auto* target = parameter(control))
        {
            target->beginChangeGesture();
            target->setValueNotifyingHost(newValue);
            target->endChangeGesture();
        }
    if (band != 1)
        setBandMode(band, EqualizerBandMode::bell);
    selectedBand = band;
    repaint();
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
    const auto modeStrip = eqModeStripBounds(area);
    if (selectedBand >= 0 && modeStrip.contains(event.position))
    {
        const auto segmentCount = selectedBand == 1 ? 2 : 4;
        const auto segment = juce::jlimit(
            0, segmentCount - 1,
            static_cast<int>((event.position.x - modeStrip.getX())
                             / (modeStrip.getWidth()
                                / static_cast<float>(segmentCount))));
        if (selectedBand == 1)
        {
            if (segment == 1)
                removeBand(selectedBand);
        }
        else if (segment < 3)
            setBandMode(
                selectedBand, static_cast<EqualizerBandMode>(segment));
        else
            removeBand(selectedBand);
        return;
    }
    const auto plot = eqPlotArea(area);
    selectedBand = bandAt(event.position, plot);
    if (selectedBand < 0)
    {
        dragPrimary = dragSecondary = -1;
        repaint();
        return;
    }
    dragPrimary = selectedBand * 3;
    const auto cut = selectedBand != 1
        && equalizerBandMode(value(selectedBand == 0 ? 10 : 11))
               == EqualizerBandMode::cut;
    dragSecondary = selectedBand * 3 + (cut ? 2 : 1);
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
    const auto band = bandAt(event.position, plot);
    if (band >= 0)
        removeBand(band);
    else if (plot.contains(event.position))
        addBandAt(event.position, plot);
}

void EqualizerModuleView::mouseWheelMove(
    const juce::MouseEvent& event,
    const juce::MouseWheelDetails& wheel)
{
    const auto plot = eqPlotArea(
        getLocalBounds().toFloat().reduced(12.0f));
    if (!plot.contains(event.position))
        return;
    const auto closestBand = bandAt(event.position, plot);
    if (closestBand < 0)
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
    const auto plot = eqPlotArea(area);
    graphics.setColour(juce::Colour(0xffa8b3c0));
    graphics.drawText(
        "Double-click to add  |  Drag to shape  |  Wheel adjusts Q  |  Double-click node removes",
        area.withHeight(24.0f).toNearestInt(),
        juce::Justification::centredLeft);
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
            const auto mode = band == 1
                ? EqualizerBandMode::bell
                : equalizerBandMode(value(band == 0 ? 10 : 11));
            if (band == 0 && mode == EqualizerBandMode::cut)
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
            else if (band == 2 && mode == EqualizerBandMode::cut)
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
            else if (mode == EqualizerBandMode::shelf)
            {
                const auto gainDb =
                    lerp(-18.0f, 18.0f, value(band * 3 + 1));
                const auto a = std::pow(10.0f, gainDb / 40.0f);
                const auto rootA = std::sqrt(a);
                const auto twoRootAAlpha = 2.0f * rootA * alpha;
                float a0 = 1.0f;
                if (band == 0)
                {
                    a0 = (a + 1.0f) + (a - 1.0f) * cosine
                         + twoRootAAlpha;
                    coefficients = {
                        a * ((a + 1.0f) - (a - 1.0f) * cosine
                             + twoRootAAlpha) / a0,
                        2.0f * a * ((a - 1.0f) - (a + 1.0f) * cosine)
                            / a0,
                        a * ((a + 1.0f) - (a - 1.0f) * cosine
                             - twoRootAAlpha) / a0,
                        -2.0f * ((a - 1.0f) + (a + 1.0f) * cosine)
                            / a0,
                        ((a + 1.0f) + (a - 1.0f) * cosine
                         - twoRootAAlpha) / a0
                    };
                }
                else
                {
                    a0 = (a + 1.0f) - (a - 1.0f) * cosine
                         + twoRootAAlpha;
                    coefficients = {
                        a * ((a + 1.0f) + (a - 1.0f) * cosine
                             + twoRootAAlpha) / a0,
                        -2.0f * a * ((a - 1.0f) + (a + 1.0f) * cosine)
                            / a0,
                        a * ((a + 1.0f) + (a - 1.0f) * cosine
                             - twoRootAAlpha) / a0,
                        2.0f * ((a - 1.0f) - (a + 1.0f) * cosine)
                            / a0,
                        ((a + 1.0f) - (a - 1.0f) * cosine
                         - twoRootAAlpha) / a0
                    };
                }
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
        if (!bandIsActive(band))
            continue;
        const auto mode = band == 1
            ? EqualizerBandMode::bell
            : equalizerBandMode(value(band == 0 ? 10 : 11));
        const auto selected = band == selectedBand;
        const auto pointBounds = juce::Rectangle<float>(
            selected ? 22.0f : 18.0f, selected ? 22.0f : 18.0f)
                                     .withCentre(eqPoint(band, plot));
        graphics.setColour(accent.withAlpha(selected ? 1.0f : 0.82f));
        graphics.fillEllipse(pointBounds);
        if (selected)
        {
            graphics.setColour(juce::Colours::white.withAlpha(0.65f));
            graphics.drawEllipse(pointBounds.expanded(3.0f), 1.5f);
        }
        graphics.setColour(juce::Colours::black);
        const auto nodeText =
            mode == EqualizerBandMode::cut ? (band == 0 ? "HP" : "LP")
            : mode == EqualizerBandMode::shelf ? (band == 0 ? "LS" : "HS")
                                               : juce::String(band + 1);
        graphics.drawText(nodeText, pointBounds.toNearestInt(),
                          juce::Justification::centred);
        if (!selected)
            continue;
        const auto frequency = exponential(
            band == 0 ? 30.0f : band == 1 ? 150.0f : 1500.0f,
            band == 0 ? 1200.0f : band == 1 ? 7000.0f : 20000.0f,
            value(band * 3));
        const auto detail = mode == EqualizerBandMode::cut
            ? juce::String(frequency, 0) + " Hz  Q "
                  + juce::String(exponential(
                      0.2f, 10.0f, value(band * 3 + 2)), 2)
            : juce::String(frequency, 0) + " Hz  "
                  + juce::String(lerp(
                      -18.0f, 18.0f, value(band * 3 + 1)), 1) + " dB";
        auto readout = juce::Rectangle<float>(
            154.0f, 22.0f).withCentre(
                eqPoint(band, plot).translated(0.0f, -24.0f));
        readout = readout.constrainedWithin(plot);
        graphics.setColour(juce::Colour(0xee111820));
        graphics.fillRoundedRectangle(readout, 5.0f);
        graphics.setColour(juce::Colours::white.withAlpha(0.82f));
        graphics.drawText(detail, readout.toNearestInt(),
                          juce::Justification::centred);
    }

    if (selectedBand >= 0)
    {
        const auto strip = eqModeStripBounds(area);
        const auto middle = selectedBand == 1;
        const auto labels = middle
            ? juce::StringArray { "Bell", "Remove" }
            : selectedBand == 0
                ? juce::StringArray {
                      "Bell", "Low Shelf", "High Pass", "Remove" }
                : juce::StringArray {
                      "Bell", "High Shelf", "Low Pass", "Remove" };
        const auto mode = middle
            ? EqualizerBandMode::bell
            : equalizerBandMode(value(selectedBand == 0 ? 10 : 11));
        const auto width = strip.getWidth()
            / static_cast<float>(labels.size());
        for (int index = 0; index < labels.size(); ++index)
        {
            const auto bounds = juce::Rectangle<float>(
                strip.getX() + width * static_cast<float>(index), strip.getY(),
                width - 3.0f, strip.getHeight());
            const auto active = index < 3
                && index == static_cast<int>(mode);
            graphics.setColour(active ? accent.withAlpha(0.38f)
                                     : juce::Colour(0xff27303b));
            graphics.fillRoundedRectangle(bounds, 5.0f);
            graphics.setColour(
                active ? juce::Colours::white
                       : index == labels.size() - 1
                           ? juce::Colour(0xffff9b9b)
                           : juce::Colour(0xffa8b3c0));
            graphics.drawText(
                labels[index], bounds.toNearestInt(),
                juce::Justification::centred);
        }
    }

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
    if (!bandIsActive(0) && !bandIsActive(1) && !bandIsActive(2))
    {
        graphics.setColour(juce::Colours::white.withAlpha(0.45f));
        graphics.drawText(
            "Double-click anywhere to add your first EQ band",
            plot.toNearestInt(), juce::Justification::centred);
    }
}
} // namespace

std::unique_ptr<ModuleView> createEqualizerView(EffectGraph& graph)
{
    return std::make_unique<EqualizerModuleView>(graph);
}
} // namespace megadsp::ui
