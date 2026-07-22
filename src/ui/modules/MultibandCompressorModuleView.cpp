#include "../GraphStyle.h"
#include "../ModuleView.h"
#include "../../modules/MultibandCompressor.h"

#include "ModuleViewCreators.h"

#include <array>
#include <cmath>

namespace megadsp::ui
{
namespace
{
class MultibandCompressorModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    void configureAccessibility(juce::Component&) const override;
    bool usesFullPanel() const override { return true; }
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    static juce::Rectangle<float> plotArea(juce::Rectangle<float>);
    static juce::Rectangle<float> autoArea(juce::Rectangle<float>);
    static juce::Rectangle<float> trackArea(juce::Rectangle<float>, int);
    float crossoverX(juce::Rectangle<float>, int) const;
    float thresholdY(juce::Rectangle<float>, int) const;
};

void MultibandCompressorModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Visual Multiband Compressor");
    component.setHelpText(
        "Drag the two crossover dividers horizontally and each band's "
        "Threshold handle vertically. Lower tracks edit Ratio, Attack, "
        "Release, Stereo Link, Mix, and Output. Auto Makeup is an explicit "
        "button. Per-band gain reduction and active state are read directly "
        "from the selected DSP slot.");
}

juce::Rectangle<float> MultibandCompressorModuleView::plotArea(
    juce::Rectangle<float> area)
{
    area.removeFromTop(32.0f);
    area.removeFromBottom(40.0f);
    return area;
}

juce::Rectangle<float> MultibandCompressorModuleView::autoArea(
    juce::Rectangle<float> area)
{
    return area.removeFromTop(28.0f).removeFromRight(170.0f)
        .reduced(4.0f, 2.0f);
}

juce::Rectangle<float> MultibandCompressorModuleView::trackArea(
    juce::Rectangle<float> area, int index)
{
    auto row = area.removeFromBottom(34.0f);
    const auto width = row.getWidth() / 6.0f;
    return juce::Rectangle<float>(
               row.getX() + width * static_cast<float>(index),
               row.getY(), width, row.getHeight())
        .reduced(3.0f, 4.0f);
}

float MultibandCompressorModuleView::crossoverX(
    juce::Rectangle<float> plot, int control) const
{
    const auto frequency = control == 0
        ? exponential(40.0f, 800.0f, value(0))
        : exponential(1000.0f, 12000.0f, value(1));
    return plot.getX()
           + std::log(frequency / 20.0f) / std::log(1000.0f)
                 * plot.getWidth();
}

float MultibandCompressorModuleView::thresholdY(
    juce::Rectangle<float> plot, int control) const
{
    return plot.getBottom() - value(control) * plot.getHeight();
}

void MultibandCompressorModuleView::mouseDown(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (autoArea(area).contains(event.position))
    {
        toggleParameter(8);
        return;
    }
    constexpr std::array<int, 6> controls { 5, 6, 7, 9, 10, 11 };
    for (int track = 0; track < 6; ++track)
        if (trackArea(area, track).contains(event.position))
            dragPrimary = controls[static_cast<size_t>(track)];
    if (dragPrimary < 0 && plotArea(area).contains(event.position))
    {
        const auto plot = plotArea(area);
        const auto lowX = crossoverX(plot, 0);
        const auto highX = crossoverX(plot, 1);
        if (std::abs(event.position.x - lowX) <= 14.0f)
            dragPrimary = 0;
        else if (std::abs(event.position.x - highX) <= 14.0f)
            dragPrimary = 1;
        else
        {
            const auto band =
                event.position.x < lowX ? 0
                : event.position.x < highX ? 1 : 2;
            const auto control = band + 2;
            if (std::abs(
                    event.position.y - thresholdY(plot, control))
                <= 20.0f)
                dragPrimary = control;
        }
    }
    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void MultibandCompressorModuleView::mouseDrag(
    const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (dragPrimary == 0 || dragPrimary == 1)
    {
        const auto plot = plotArea(area);
        const auto graphPosition = juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - plot.getX()) / plot.getWidth());
        const auto frequency = 20.0f * std::pow(1000.0f, graphPosition);
        if (dragPrimary == 0)
            setValue(0, juce::jlimit(
                0.0f, 1.0f,
                std::log(frequency / 40.0f) / std::log(20.0f)));
        else
            setValue(1, juce::jlimit(
                0.0f, 1.0f,
                std::log(frequency / 1000.0f) / std::log(12.0f)));
    }
    else if (dragPrimary >= 2 && dragPrimary <= 4)
    {
        const auto plot = plotArea(area);
        setValue(dragPrimary, juce::jlimit(
            0.0f, 1.0f,
            (plot.getBottom() - event.position.y) / plot.getHeight()));
    }
    else
    {
        constexpr std::array<int, 6> controls { 5, 6, 7, 9, 10, 11 };
        int track = 0;
        while (track < 6
               && controls[static_cast<size_t>(track)] != dragPrimary)
            ++track;
        if (track >= 6)
            return;
        const auto bounds = trackArea(area, track);
        setValue(dragPrimary, juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - bounds.getX()) / bounds.getWidth()));
    }
    updateDefaultDragReadout();
}

void MultibandCompressorModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (autoArea(area).contains(event.position))
    {
        resetToDefault(8);
        return;
    }
    const auto plot = plotArea(area);
    if (plot.contains(event.position))
        for (int crossover = 0; crossover < 2; ++crossover)
            if (std::abs(
                    event.position.x - crossoverX(plot, crossover))
                <= 14.0f)
            {
                resetToDefault(crossover);
                return;
            }
    const auto lowX = crossoverX(plot, 0);
    const auto highX = crossoverX(plot, 1);
    const auto band =
        event.position.x < lowX ? 0
        : event.position.x < highX ? 1 : 2;
    const auto thresholdControl = band + 2;
    if (plot.contains(event.position)
        && std::abs(
               event.position.y
               - thresholdY(plot, thresholdControl)) <= 20.0f)
        resetToDefault(thresholdControl);
    constexpr std::array<int, 6> controls { 5, 6, 7, 9, 10, 11 };
    for (int track = 0; track < 6; ++track)
        if (trackArea(area, track).contains(event.position))
        {
            const auto control = controls[static_cast<size_t>(track)];
            resetToDefault(control);
        }
}

void MultibandCompressorModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto plot = plotArea(area);
    ContinuousTelemetrySnapshot telemetrySnapshot;
    const auto hasTelemetry =
        readContinuousTelemetry(telemetrySnapshot)
        && telemetrySnapshot.sequence != 0
        && telemetrySnapshot.valueCount
               >= MultibandCompressorModule::telemetryValueCount;
    drawSpectrum(graphics, plot, inputSpectrum, inputColour);
    drawSpectrum(graphics, plot, outputSpectrum, outputColour);
    const auto lowX = crossoverX(plot, 0);
    const auto highX = crossoverX(plot, 1);
    const std::array<juce::Rectangle<float>, 3> bands {
        juce::Rectangle<float>(
            plot.getX(), plot.getY(), lowX - plot.getX(), plot.getHeight()),
        juce::Rectangle<float>(
            lowX, plot.getY(), highX - lowX, plot.getHeight()),
        juce::Rectangle<float>(
            highX, plot.getY(), plot.getRight() - highX, plot.getHeight())
    };
    const std::array<juce::Colour, 3> bandColours {
        juce::Colour(0xff5596d8), accent, juce::Colour(0xffb879e8)
    };
    const std::array<const char*, 3> bandNames { "LOW", "MID", "HIGH" };
    for (int band = 0; band < 3; ++band)
    {
        graphics.setColour(
            bandColours[static_cast<size_t>(band)].withAlpha(0.055f));
        graphics.fillRect(bands[static_cast<size_t>(band)]);
    }

    for (int db = -60; db <= 0; db += 12)
    {
        const auto y = juce::jmap(
            static_cast<float>(db), -60.0f, 0.0f,
            plot.getBottom(), plot.getY());
        graphics.setColour(juce::Colour(0xff303a46));
        graphics.drawHorizontalLine(
            juce::roundToInt(y), plot.getX(), plot.getRight());
        graphics.setColour(juce::Colour(0xff8995a4));
        graphics.drawText(
            juce::String(db) + " dB",
            juce::Rectangle<float>(
                plot.getX() + 3.0f, y - 12.0f, 45.0f, 12.0f),
            juce::Justification::centredLeft);
    }

    std::array<juce::Path, 3> reductionPaths;
    for (std::uint32_t index = 0;
         hasTelemetry && index < telemetrySnapshot.historyCount; ++index)
    {
        const auto x = plot.getX()
            + static_cast<float>(index)
                  / static_cast<float>(
                      juce::jmax(1u, telemetrySnapshot.historyCount - 1))
                  * plot.getWidth();
        for (size_t band = 0; band < reductionPaths.size(); ++band)
        {
            const auto y = plot.getBottom()
                - juce::jlimit(
                      0.0f, 1.0f,
                      continuousTelemetryHistoryValue(
                          telemetrySnapshot,
                          static_cast<std::uint32_t>(band), index)
                          / 24.0f)
                      * plot.getHeight() * 0.45f;
            if (index == 0)
                reductionPaths[band].startNewSubPath(x, y);
            else
                reductionPaths[band].lineTo(x, y);
        }
    }
    for (size_t band = 0; band < reductionPaths.size(); ++band)
    {
        graphics.setColour(
            bandColours[band].withAlpha(0.86f));
        graphics.strokePath(
            reductionPaths[band], juce::PathStrokeType(1.8f));
    }

    for (int crossover = 0; crossover < 2; ++crossover)
    {
        const auto x = crossover == 0 ? lowX : highX;
        graphics.setColour(accent);
        graphics.drawVerticalLine(
            juce::roundToInt(x), plot.getY(), plot.getBottom());
        graphics.fillRoundedRectangle(
            x - 62.0f, plot.getY() + 3.0f, 124.0f, 22.0f, 4.0f);
        graphics.setColour(juce::Colour(0xff10141a));
        graphics.drawText(
            formatControlValue(type, crossover, value(crossover)),
            juce::Rectangle<float>(
                x - 60.0f, plot.getY() + 3.0f, 120.0f, 22.0f),
            juce::Justification::centred);
    }

    for (int band = 0; band < 3; ++band)
    {
        const auto control = band + 2;
        const auto bandArea = bands[static_cast<size_t>(band)];
        const auto y = thresholdY(plot, control);
        graphics.setColour(bandColours[static_cast<size_t>(band)]);
        graphics.drawHorizontalLine(
            juce::roundToInt(y), bandArea.getX(), bandArea.getRight());
        auto handle = juce::Rectangle<float>(
            juce::jmax(bandArea.getX() + 3.0f,
                       bandArea.getCentreX() - 68.0f),
            y - 11.0f, juce::jmin(136.0f, bandArea.getWidth() - 6.0f),
            22.0f);
        graphics.fillRoundedRectangle(handle, 4.0f);
        graphics.setColour(juce::Colour(0xff10141a));
        graphics.drawText(
            juce::String(bandNames[static_cast<size_t>(band)]) + "  "
                + formatControlValue(type, control, value(control)),
            handle.toNearestInt(), juce::Justification::centred);
        if (hasTelemetry)
        {
            const auto index = static_cast<size_t>(band);
            graphics.setColour(bandColours[index]);
            graphics.drawText(
                "GR "
                    + juce::String(
                        telemetrySnapshot.values[
                            static_cast<size_t>(
                                MultibandCompressorModule::lowReductionDb)
                            + index],
                        1)
                    + " dB · "
                    + (telemetrySnapshot.values[
                           static_cast<size_t>(
                               MultibandCompressorModule::lowActive)
                           + index] > 0.0f
                           ? "ACTIVE" : "IDLE"),
                bandArea.reduced(4.0f).toNearestInt(),
                juce::Justification::centredBottom);
        }
    }
    graphics.setColour(reductionColour);
    graphics.drawText(
        hasTelemetry ? "DSP PER-BAND GAIN REDUCTION · CAPTURED BLOCKS"
                     : "DSP TELEMETRY WAITING",
        plot.toNearestInt(), juce::Justification::centredBottom);

    const auto automatic = autoArea(area);
    const auto autoOn = value(8) >= 0.5f;
    graphics.setColour(autoOn ? accent.withAlpha(0.36f)
                              : juce::Colour(0xff27303b));
    graphics.fillRoundedRectangle(automatic, 5.0f);
    graphics.setColour(juce::Colours::white);
    graphics.drawText(
        autoOn ? "AUTO MAKEUP  ON" : "AUTO MAKEUP  OFF",
        automatic.toNearestInt(), juce::Justification::centred);

    constexpr std::array<int, 6> controls { 5, 6, 7, 9, 10, 11 };
    constexpr std::array<const char*, 6> names {
        "RATIO", "ATTACK", "RELEASE", "LINK", "MIX", "OUTPUT"
    };
    for (int track = 0; track < 6; ++track)
    {
        const auto control = controls[static_cast<size_t>(track)];
        const auto bounds = trackArea(area, track);
        graphics.setColour(juce::Colour(0xff27303b));
        graphics.fillRoundedRectangle(bounds, 4.0f);
        graphics.setColour(accent.withAlpha(0.25f));
        graphics.fillRoundedRectangle(
            bounds.withWidth(bounds.getWidth() * value(control)), 4.0f);
        graphics.setColour(juce::Colours::white);
        graphics.drawText(
            juce::String(names[static_cast<size_t>(track)]) + "  "
                + formatControlValue(type, control, value(control)),
            bounds.toNearestInt(), juce::Justification::centred);
    }
}
} // namespace

std::unique_ptr<ModuleView> createMultibandCompressorView(
    EffectGraph& graph)
{
    return std::make_unique<MultibandCompressorModuleView>(graph);
}
} // namespace megadsp::ui
