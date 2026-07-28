#include "../GraphStyle.h"
#include "../GuiLayout.h"
#include "../ModuleView.h"

#include "ModuleViewCreators.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace megadsp::ui
{
namespace
{
constexpr std::array<float, 4> lowFrequencyValues { 20.0f, 30.0f, 60.0f, 100.0f };
constexpr std::array<float, 7> highBoostFrequencyValues {
    3000.0f, 4000.0f, 5000.0f, 8000.0f, 10000.0f, 12000.0f, 16000.0f
};
constexpr std::array<float, 3> highAttenuationFrequencyValues {
    5000.0f, 10000.0f, 20000.0f
};

// Mirrors the passive-network shaping in ProgramEqualizer.cpp so the graph
// shows the same response the DSP produces.
constexpr float lowShelfQ = 0.85f;
constexpr float lowAttenuationQ = 1.35f;
constexpr float highAttenuationShelfQ = 0.7f;
constexpr float minimumHighBoostQ = 0.5f;
constexpr float maximumHighBoostQ = 4.0f;
constexpr float proportionalQAmount = 0.65f;

struct Coefficients
{
    float b0, b1, b2, a1, a2;
};

Coefficients lowShelfCoefficients(
    float rate, float frequency, float q, float gainDb)
{
    const auto a = std::pow(10.0f, gainDb / 40.0f);
    const auto omega = juce::MathConstants<float>::twoPi * frequency / rate;
    const auto cosine = std::cos(omega);
    const auto alpha = std::sin(omega) / (2.0f * q);
    const auto rootA = std::sqrt(a);
    const auto twoRootAAlpha = 2.0f * rootA * alpha;
    const auto a0 = (a + 1.0f) + (a - 1.0f) * cosine + twoRootAAlpha;
    return {
        a * ((a + 1.0f) - (a - 1.0f) * cosine + twoRootAAlpha) / a0,
        2.0f * a * ((a - 1.0f) - (a + 1.0f) * cosine) / a0,
        a * ((a + 1.0f) - (a - 1.0f) * cosine - twoRootAAlpha) / a0,
        -2.0f * ((a - 1.0f) + (a + 1.0f) * cosine) / a0,
        ((a + 1.0f) + (a - 1.0f) * cosine - twoRootAAlpha) / a0
    };
}

Coefficients peakCoefficients(
    float rate, float frequency, float q, float gainDb)
{
    const auto a = std::pow(10.0f, gainDb / 40.0f);
    const auto omega = juce::MathConstants<float>::twoPi * frequency / rate;
    const auto alpha = std::sin(omega) / (2.0f * q);
    const auto cosOmega = std::cos(omega);
    const auto a0 = 1.0f + alpha / a;
    return {
        (1.0f + alpha * a) / a0, (-2.0f * cosOmega) / a0,
        (1.0f - alpha * a) / a0, (-2.0f * cosOmega) / a0,
        (1.0f - alpha / a) / a0
    };
}

Coefficients highShelfCoefficients(
    float rate, float frequency, float q, float gainDb)
{
    const auto a = std::pow(10.0f, gainDb / 40.0f);
    const auto omega = juce::MathConstants<float>::twoPi * frequency / rate;
    const auto cosine = std::cos(omega);
    const auto alpha = std::sin(omega) / (2.0f * q);
    const auto rootA = std::sqrt(a);
    const auto twoRootAAlpha = 2.0f * rootA * alpha;
    const auto a0 = (a + 1.0f) - (a - 1.0f) * cosine + twoRootAAlpha;
    return {
        a * ((a + 1.0f) + (a - 1.0f) * cosine + twoRootAAlpha) / a0,
        -2.0f * a * ((a - 1.0f) + (a + 1.0f) * cosine) / a0,
        a * ((a + 1.0f) + (a - 1.0f) * cosine - twoRootAAlpha) / a0,
        2.0f * ((a - 1.0f) - (a + 1.0f) * cosine) / a0,
        ((a + 1.0f) - (a - 1.0f) * cosine - twoRootAAlpha) / a0
    };
}

float magnitudeAt(const Coefficients& coefficients, float frequency, float rate)
{
    const auto omega = juce::MathConstants<float>::twoPi
        * juce::jmin(frequency, rate * 0.475f) / rate;
    const auto cosine = std::cos(omega);
    const auto sine = std::sin(omega);
    const auto cosine2 = std::cos(2.0f * omega);
    const auto sine2 = std::sin(2.0f * omega);
    const auto numeratorReal = coefficients.b0
        + coefficients.b1 * cosine + coefficients.b2 * cosine2;
    const auto numeratorImag =
        -coefficients.b1 * sine - coefficients.b2 * sine2;
    const auto denominatorReal =
        1.0f + coefficients.a1 * cosine + coefficients.a2 * cosine2;
    const auto denominatorImag =
        -coefficients.a1 * sine - coefficients.a2 * sine2;
    return std::sqrt(
        (numeratorReal * numeratorReal + numeratorImag * numeratorImag)
        / juce::jmax(1.0e-12f,
              denominatorReal * denominatorReal
                  + denominatorImag * denominatorImag));
}

class ProgramEqualizerModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    void configureAccessibility(juce::Component&) const override;
    bool usesFullPanel() const override { return true; }
    bool usesSpectrum() const override { return true; }
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&,
                        const juce::MouseWheelDetails&) override;

private:
    static juce::Rectangle<float> plotArea(juce::Rectangle<float>);
    static juce::Rectangle<float> pillBounds(juce::Rectangle<float>, int index);
    static juce::Rectangle<float> trackBounds(juce::Rectangle<float>, int index);
    static float frequencyToX(float frequency, juce::Rectangle<float> plot);
    static float dbToPlotY(float db, juce::Rectangle<float> plot);
    float highBoostQ() const;
    juce::Point<float> lowBoostPoint(juce::Rectangle<float> plot) const;
    juce::Point<float> lowAttenuationPoint(juce::Rectangle<float> plot) const;
    juce::Point<float> highBoostPoint(juce::Rectangle<float> plot) const;
    juce::Point<float> highAttenuationPoint(juce::Rectangle<float> plot) const;
};

void ProgramEqualizerModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Program Equalizer");
    component.setHelpText(
        "Click the top pills to step through the Low Frequency, High Boost "
        "Frequency, and High Attenuation Frequency. Drag the boost and "
        "attenuation handles on the graph for gain; boosting and cutting "
        "the same Low Frequency together creates a resonant interaction "
        "instead of cancelling. The lower tracks edit Bandwidth, Drive, and "
        "Output.");
}

juce::Rectangle<float> ProgramEqualizerModuleView::plotArea(
    juce::Rectangle<float> area)
{
    area.removeFromTop(34.0f);
    area.removeFromBottom(40.0f);
    return area;
}

juce::Rectangle<float> ProgramEqualizerModuleView::pillBounds(
    juce::Rectangle<float> area, int index)
{
    auto pills = area.removeFromTop(30.0f);
    const auto width = pills.getWidth() / 3.0f;
    return juce::Rectangle<float>(
        pills.getX() + width * static_cast<float>(index),
        pills.getY(), width, pills.getHeight()).reduced(4.0f, 3.0f);
}

juce::Rectangle<float> ProgramEqualizerModuleView::trackBounds(
    juce::Rectangle<float> area, int index)
{
    auto controls = area.removeFromBottom(34.0f);
    const auto columnWidth = controls.getWidth() / 3.0f;
    return juce::Rectangle<float>(
        controls.getX() + columnWidth * static_cast<float>(index),
        controls.getY(), columnWidth, controls.getHeight())
        .reduced(4.0f, 4.0f);
}

float ProgramEqualizerModuleView::frequencyToX(
    float frequency, juce::Rectangle<float> plot)
{
    const auto x = std::log(juce::jmax(20.0f, frequency) / 20.0f)
        / std::log(1000.0f);
    return plot.getX() + juce::jlimit(0.0f, 1.0f, x) * plot.getWidth();
}

float ProgramEqualizerModuleView::dbToPlotY(
    float db, juce::Rectangle<float> plot)
{
    return plot.getBottom()
        - juce::jlimit(0.0f, 1.0f, (db + 20.0f) / 40.0f) * plot.getHeight();
}

float ProgramEqualizerModuleView::highBoostQ() const
{
    const auto baseQ = exponential(
        minimumHighBoostQ, maximumHighBoostQ, value(5));
    const auto boostDb = lerp(0.0f, 18.0f, value(4));
    return baseQ * (1.0f + proportionalQAmount * (boostDb / 18.0f));
}

juce::Point<float> ProgramEqualizerModuleView::lowBoostPoint(
    juce::Rectangle<float> plot) const
{
    const auto frequency = lowFrequencyValues[static_cast<size_t>(
        megadsp::discreteIndex(value(0), 4))];
    return { frequencyToX(frequency, plot),
             dbToPlotY(lerp(0.0f, 14.0f, value(1)), plot) };
}

juce::Point<float> ProgramEqualizerModuleView::lowAttenuationPoint(
    juce::Rectangle<float> plot) const
{
    const auto frequency = lowFrequencyValues[static_cast<size_t>(
        megadsp::discreteIndex(value(0), 4))];
    return { frequencyToX(frequency, plot),
             dbToPlotY(-lerp(0.0f, 18.0f, value(2)), plot) };
}

juce::Point<float> ProgramEqualizerModuleView::highBoostPoint(
    juce::Rectangle<float> plot) const
{
    const auto frequency = highBoostFrequencyValues[static_cast<size_t>(
        megadsp::discreteIndex(value(3), 7))];
    return { frequencyToX(frequency, plot),
             dbToPlotY(lerp(0.0f, 18.0f, value(4)), plot) };
}

juce::Point<float> ProgramEqualizerModuleView::highAttenuationPoint(
    juce::Rectangle<float> plot) const
{
    const auto frequency = highAttenuationFrequencyValues[static_cast<size_t>(
        megadsp::discreteIndex(value(6), 3))];
    return { frequencyToX(frequency, plot),
             dbToPlotY(-lerp(0.0f, 18.0f, value(7)), plot) };
}

void ProgramEqualizerModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    static constexpr std::array<int, 3> pillOptionCounts { 4, 7, 3 };
    static constexpr std::array<int, 3> pillControls { 0, 3, 6 };
    for (int pill = 0; pill < 3; ++pill)
        if (pillBounds(area, pill).contains(event.position))
        {
            cycleChoice(pillControls[static_cast<size_t>(pill)],
                       pillOptionCounts[static_cast<size_t>(pill)]);
            return;
        }
    static constexpr std::array<int, 3> trackControls { 5, 8, 9 };
    for (int track = 0; track < 3; ++track)
        if (trackBounds(area, track).contains(event.position))
        {
            dragPrimary = trackControls[static_cast<size_t>(track)];
            beginGestures();
            mouseDrag(event);
            return;
        }
    const auto plot = plotArea(area);
    struct Handle { juce::Point<float> point; int control; };
    const std::array<Handle, 4> handles {{
        { lowBoostPoint(plot), 1 },
        { lowAttenuationPoint(plot), 2 },
        { highBoostPoint(plot), 4 },
        { highAttenuationPoint(plot), 7 }
    }};
    auto closestDistance = 20.0f;
    auto closestControl = -1;
    for (const auto& handle : handles)
    {
        const auto distance = event.position.getDistanceFrom(handle.point);
        if (distance < closestDistance)
        {
            closestDistance = distance;
            closestControl = handle.control;
        }
    }
    if (closestControl < 0)
        return;
    dragPrimary = closestControl;
    beginGestures();
    mouseDrag(event);
}

void ProgramEqualizerModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (dragPrimary == 5 || dragPrimary == 8 || dragPrimary == 9)
    {
        const auto trackIndex =
            dragPrimary == 5 ? 0 : dragPrimary == 8 ? 1 : 2;
        const auto track = trackBounds(area, trackIndex);
        setValue(dragPrimary, juce::jlimit(
            0.0f, 1.0f, (event.position.x - track.getX()) / track.getWidth()));
        updateDefaultDragReadout();
        return;
    }
    const auto plot = plotArea(area);
    const auto normalizedGain =
        (plot.getBottom() - event.position.y) / plot.getHeight();
    if (dragPrimary == 1 || dragPrimary == 4)
        setValue(dragPrimary, juce::jlimit(0.0f, 1.0f, normalizedGain * 2.0f));
    else if (dragPrimary == 2 || dragPrimary == 7)
        setValue(dragPrimary,
                 juce::jlimit(0.0f, 1.0f, (1.0f - normalizedGain) * 2.0f));
    updateDefaultDragReadout();
}

void ProgramEqualizerModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    static constexpr std::array<int, 3> trackControls { 5, 8, 9 };
    for (int track = 0; track < 3; ++track)
        if (trackBounds(area, track).contains(event.position))
        {
            resetToDefault(trackControls[static_cast<size_t>(track)]);
            return;
        }
    const auto plot = plotArea(area);
    struct Handle { juce::Point<float> point; int control; };
    const std::array<Handle, 4> handles {{
        { lowBoostPoint(plot), 1 },
        { lowAttenuationPoint(plot), 2 },
        { highBoostPoint(plot), 4 },
        { highAttenuationPoint(plot), 7 }
    }};
    for (const auto& handle : handles)
        if (event.position.getDistanceFrom(handle.point) <= 20.0f)
        {
            resetToDefault(handle.control);
            return;
        }
}

void ProgramEqualizerModuleView::mouseWheelMove(
    const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto plot = plotArea(getLocalBounds().toFloat().reduced(12.0f));
    if (event.position.getDistanceFrom(highBoostPoint(plot)) > 40.0f)
        return;
    if (auto* target = parameter(5))
    {
        graph.focusKeyboardControl(5);
        const auto step = event.mods.isShiftDown() ? 0.02f : 0.06f;
        const auto delta = normalizedWheelDelta(
            wheel.deltaX, wheel.deltaY, wheel.isReversed);
        target->beginChangeGesture();
        target->setValueNotifyingHost(
            juce::jlimit(0.0f, 1.0f, target->getValue() + delta * step));
        target->endChangeGesture();
        repaint();
    }
}

void ProgramEqualizerModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto plot = plotArea(area);
    const auto rate = static_cast<float>(
        processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0);

    drawSpectrum(graphics, plot, inputSpectrum, inputColour);
    drawSpectrum(graphics, plot, outputSpectrum, outputColour);

    for (const auto db : { -18.0f, -12.0f, -6.0f, 0.0f, 6.0f, 12.0f, 18.0f })
    {
        const auto y = dbToPlotY(db, plot);
        graphics.setColour(
            db == 0.0f ? juce::Colour(0xff536172) : juce::Colour(0xff2b3542));
        graphics.drawHorizontalLine(
            juce::roundToInt(y), plot.getX(), plot.getRight());
        graphics.setColour(juce::Colour(0xff8995a4));
        graphics.drawText(
            juce::String(db, 0) + " dB",
            juce::Rectangle<float>(plot.getX() + 4.0f, y - 13.0f, 48.0f, 12.0f),
            juce::Justification::centredLeft);
    }
    constexpr std::array<float, 5> axisFrequencies {
        20.0f, 100.0f, 1000.0f, 10000.0f, 20000.0f
    };
    constexpr std::array<const char*, 5> axisLabels {
        "20", "100", "1k", "10k", "20k"
    };
    for (size_t marker = 0; marker < axisFrequencies.size(); ++marker)
    {
        const auto x = frequencyToX(axisFrequencies[marker], plot);
        graphics.setColour(juce::Colour(0xff8995a4));
        graphics.drawText(
            axisLabels[marker],
            juce::Rectangle<float>(x - 22.0f, plot.getY(), 44.0f, 16.0f),
            juce::Justification::centred);
    }

    const auto lowFrequency = lowFrequencyValues[static_cast<size_t>(
        megadsp::discreteIndex(value(0), 4))];
    const auto highBoostFrequency = highBoostFrequencyValues[static_cast<size_t>(
        megadsp::discreteIndex(value(3), 7))];
    const auto highAttenuationFrequency =
        highAttenuationFrequencyValues[static_cast<size_t>(
            megadsp::discreteIndex(value(6), 3))];
    const auto lowBoostDb = lerp(0.0f, 14.0f, value(1));
    const auto lowAttenuationDb = lerp(0.0f, 18.0f, value(2));
    const auto highBoostDb = lerp(0.0f, 18.0f, value(4));
    const auto highAttenuationDb = lerp(0.0f, 18.0f, value(7));
    const auto boostQ = highBoostQ();

    juce::Path response;
    for (int pixel = 0; pixel <= static_cast<int>(plot.getWidth()); ++pixel)
    {
        const auto fraction =
            static_cast<float>(pixel) / plot.getWidth();
        const auto frequency = 20.0f * std::pow(1000.0f, fraction);
        const auto magnitude =
            magnitudeAt(lowShelfCoefficients(
                            rate, lowFrequency, lowShelfQ, lowBoostDb),
                       frequency, rate)
            * magnitudeAt(peakCoefficients(
                              rate, lowFrequency, lowAttenuationQ,
                              -lowAttenuationDb),
                          frequency, rate)
            * magnitudeAt(peakCoefficients(
                              rate, highBoostFrequency, boostQ, highBoostDb),
                          frequency, rate)
            * magnitudeAt(highShelfCoefficients(
                              rate, highAttenuationFrequency,
                              highAttenuationShelfQ, -highAttenuationDb),
                          frequency, rate);
        const auto db = juce::Decibels::gainToDecibels(magnitude, -40.0f);
        const auto y = dbToPlotY(db, plot);
        if (pixel == 0)
            response.startNewSubPath(plot.getX(), y);
        else
            response.lineTo(plot.getX() + static_cast<float>(pixel), y);
    }
    graphics.setColour(accent);
    graphics.strokePath(response, juce::PathStrokeType(2.6f));

    const auto drawHandle = [&](juce::Point<float> point, juce::Colour colour,
                                const juce::String& label)
    {
        graphics.setColour(colour.withAlpha(0.35f));
        graphics.drawVerticalLine(
            juce::roundToInt(point.x), dbToPlotY(0.0f, plot), point.y);
        graphics.setColour(colour);
        graphics.fillEllipse(
            juce::Rectangle<float>(16.0f, 16.0f).withCentre(point));
        graphics.setColour(juce::Colour(0xff10141a));
        graphics.drawEllipse(
            juce::Rectangle<float>(9.0f, 9.0f).withCentre(point), 2.0f);
        graphics.setColour(juce::Colours::white);
        auto labelBounds = juce::Rectangle<float>(150.0f, 18.0f)
            .withCentre(point.translated(0.0f, point.y > dbToPlotY(0.0f, plot)
                ? 16.0f : -16.0f));
        labelBounds = labelBounds.constrainedWithin(plot);
        graphics.drawText(label, labelBounds.toNearestInt(),
                          juce::Justification::centred);
    };
    drawHandle(lowBoostPoint(plot), accent,
              "LOW BOOST  " + megadsp::formatControlValue(type, 1, value(1)));
    drawHandle(lowAttenuationPoint(plot), reductionColour,
              "LOW ATTEN  "
                  + megadsp::formatControlValue(type, 2, value(2)));
    drawHandle(highBoostPoint(plot), accent,
              "HIGH BOOST  "
                  + megadsp::formatControlValue(type, 4, value(4)));
    drawHandle(highAttenuationPoint(plot), reductionColour,
              "HIGH ATTEN  "
                  + megadsp::formatControlValue(type, 7, value(7)));

    static constexpr std::array<int, 3> pillControls { 0, 3, 6 };
    static constexpr std::array<const char*, 3> pillNames {
        "LOW FREQ", "HIGH BOOST FREQ", "HIGH ATTEN FREQ"
    };
    for (int pill = 0; pill < 3; ++pill)
    {
        const auto control = pillControls[static_cast<size_t>(pill)];
        const auto bounds = pillBounds(area, pill);
        graphics.setColour(juce::Colour(0xff27303b));
        graphics.fillRoundedRectangle(bounds, 6.0f);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(11.5f, juce::Font::bold));
        graphics.drawText(
            juce::String(pillNames[static_cast<size_t>(pill)]) + "  "
                + megadsp::formatControlValue(type, control, value(control)),
            bounds.toNearestInt(), juce::Justification::centred);
    }

    ContinuousTelemetrySnapshot telemetry;
    const auto hasTelemetry = readContinuousTelemetry(telemetry)
        && telemetry.valueCount
               >= ProgramEqualizerModule::telemetryValueCount;
    const auto driveGlow = hasTelemetry
        ? juce::jlimit(0.0f, 1.0f,
              telemetry.values[ProgramEqualizerModule::driveActivity] * 6.0f)
        : 0.0f;

    static constexpr std::array<int, 3> trackControls { 5, 8, 9 };
    static constexpr std::array<const char*, 3> trackNames {
        "BANDWIDTH", "DRIVE", "OUTPUT"
    };
    for (int track = 0; track < 3; ++track)
    {
        const auto control = trackControls[static_cast<size_t>(track)];
        const auto bounds = trackBounds(area, track);
        graphics.setColour(juce::Colour(0xff27303b));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        const auto glow = control == 8 ? driveGlow : 0.0f;
        graphics.setColour(accent.withAlpha(0.25f + glow * 0.35f));
        graphics.fillRoundedRectangle(
            bounds.withWidth(bounds.getWidth() * value(control)), 5.0f);
        const auto x = bounds.getX() + value(control) * bounds.getWidth();
        graphics.setColour(accent);
        graphics.fillEllipse(x - 4.0f, bounds.getCentreY() - 4.0f, 8.0f, 8.0f);
        graphics.setColour(juce::Colours::white);
        graphics.drawText(
            juce::String(trackNames[static_cast<size_t>(track)]) + "  "
                + megadsp::formatControlValue(type, control, value(control)),
            bounds.toNearestInt(), juce::Justification::centred);
    }
}
} // namespace

std::unique_ptr<ModuleView> createProgramEqualizerView(EffectGraph& graph)
{
    return std::make_unique<ProgramEqualizerModuleView>(graph);
}
} // namespace megadsp::ui
