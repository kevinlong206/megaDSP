#include "../GraphStyle.h"
#include "../ModuleView.h"
#include "../../modules/FetLimiter.h"

#include "ModuleViewCreators.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace megadsp::ui
{
namespace
{
class FetLimiterModuleView final : public ModuleView
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
    static constexpr std::array<int, 8> trackControls {
        FetLimiterModule::inputControl,
        FetLimiterModule::outputControl,
        FetLimiterModule::attackControl,
        FetLimiterModule::releaseControl,
        FetLimiterModule::detectorLowCutControl,
        FetLimiterModule::stereoLinkControl,
        FetLimiterModule::mixControl,
        FetLimiterModule::outputTrimControl
    };
    static constexpr std::array<const char*, 8> trackNames {
        "INPUT", "OUTPUT", "ATTACK", "RELEASE",
        "DETECTOR LOW CUT", "STEREO LINK", "MIX", "OUTPUT TRIM"
    };
    static constexpr std::array<const char*, FetLimiterModule::ratioCount>
        ratioNames { "4:1", "8:1", "12:1", "20:1", "ALL" };

    static juce::Rectangle<float> ratioPillArea(
        juce::Rectangle<float> area, int index);
    static juce::Rectangle<float> plotArea(juce::Rectangle<float> area);
    static juce::Rectangle<float> curveArea(juce::Rectangle<float> plot);
    static juce::Rectangle<float> historyArea(juce::Rectangle<float> plot);
    static juce::Rectangle<float> trackBounds(
        juce::Rectangle<float> area, int track);

    void chooseRatio(int index);
};

juce::Rectangle<float> FetLimiterModuleView::ratioPillArea(
    juce::Rectangle<float> area, int index)
{
    auto row = area.removeFromTop(28.0f);
    const auto width = row.getWidth()
        / static_cast<float>(FetLimiterModule::ratioCount);
    return juce::Rectangle<float>(
        row.getX() + width * static_cast<float>(index), row.getY(),
        width, row.getHeight()).reduced(4.0f, 2.0f);
}

juce::Rectangle<float> FetLimiterModuleView::plotArea(
    juce::Rectangle<float> area)
{
    area.removeFromTop(32.0f);
    area.removeFromBottom(area.getHeight() * 0.4f);
    return area;
}

juce::Rectangle<float> FetLimiterModuleView::curveArea(
    juce::Rectangle<float> plot)
{
    return plot.removeFromLeft(plot.getWidth() * 0.42f).reduced(4.0f, 0.0f);
}

juce::Rectangle<float> FetLimiterModuleView::historyArea(
    juce::Rectangle<float> plot)
{
    plot.removeFromLeft(plot.getWidth() * 0.42f);
    return plot.reduced(4.0f, 0.0f);
}

juce::Rectangle<float> FetLimiterModuleView::trackBounds(
    juce::Rectangle<float> area, int track)
{
    area.removeFromTop(32.0f);
    auto tracks = area.removeFromBottom(area.getHeight() * 0.4f);
    const auto width = tracks.getWidth() * 0.25f;
    const auto height = tracks.getHeight() * 0.5f;
    return juce::Rectangle<float>(
        tracks.getX() + width * static_cast<float>(track % 4),
        tracks.getY() + height * static_cast<float>(track / 4),
        width, height).reduced(5.0f, 4.0f);
}

void FetLimiterModuleView::chooseRatio(int index)
{
    if (auto* target = parameter(FetLimiterModule::ratioControl))
    {
        graph.focusKeyboardControl(FetLimiterModule::ratioControl);
        target->beginChangeGesture();
        target->setValueNotifyingHost(
            discreteValue(index, FetLimiterModule::ratioCount));
        target->endChangeGesture();
    }
    repaint();
}

void FetLimiterModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("FET Limiter");
    component.setHelpText(
        "Choose the 4:1, 8:1, 12:1, 20:1, or aggressive ALL ratio. The "
        "left graph shows the live feedback gain-reduction curve and the "
        "right graph shows measured detector, gain reduction, input, and "
        "output history. Lower tracks edit Input, Output, Attack, Release, "
        "detector Low Cut, Stereo Link, Mix, and Output Trim.");
}

void FetLimiterModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int index = 0; index < FetLimiterModule::ratioCount; ++index)
        if (ratioPillArea(area, index).contains(event.position))
        {
            chooseRatio(index);
            return;
        }
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

void FetLimiterModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    auto track = 0;
    for (; track < static_cast<int>(trackControls.size()); ++track)
        if (trackControls[static_cast<size_t>(track)] == dragPrimary)
            break;
    const auto bounds = trackBounds(area, juce::jlimit(0, 7, track));
    setValue(
        dragPrimary,
        juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - bounds.getX()) / bounds.getWidth()));
    updateDefaultDragReadout();
}

void FetLimiterModuleView::mouseDoubleClick(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int index = 0; index < FetLimiterModule::ratioCount; ++index)
        if (ratioPillArea(area, index).contains(event.position))
        {
            resetToDefault(FetLimiterModule::ratioControl);
            return;
        }
    for (int track = 0; track < static_cast<int>(trackControls.size());
         ++track)
        if (trackBounds(area, track).contains(event.position))
        {
            resetToDefault(trackControls[static_cast<size_t>(track)]);
            return;
        }
}

void FetLimiterModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto selectedRatio = discreteIndex(
        value(FetLimiterModule::ratioControl), FetLimiterModule::ratioCount);
    for (int index = 0; index < FetLimiterModule::ratioCount; ++index)
    {
        const auto bounds = ratioPillArea(area, index);
        const auto selected = index == selectedRatio;
        graphics.setColour(
            selected ? accent : juce::Colour(0xff242c33));
        graphics.fillRoundedRectangle(bounds, 6.0f);
        graphics.setColour(
            selected ? juce::Colour(0xff11161b) : juce::Colours::white);
        graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        graphics.drawText(
            ratioNames[static_cast<size_t>(index)],
            bounds.toNearestInt(), juce::Justification::centred);
    }

    const auto plot = plotArea(area);
    const auto curve = curveArea(plot);
    const auto history = historyArea(plot);

    // Live transfer curve: input level (dB) against the resulting gain
    // reduction, computed with the exact function the DSP uses.
    const auto ratio = FetLimiterModule::ratioTable[
        static_cast<size_t>(selectedRatio)];
    const auto knee = selectedRatio == FetLimiterModule::allButtonsInIndex
        ? 0.0f : FetLimiterModule::softKneeDb;
    constexpr float curveLowDb = -40.0f;
    constexpr float curveHighDb = 10.0f;
    constexpr float reductionRangeDb = 40.0f;
    graphics.setColour(juce::Colour(0xff242c33));
    graphics.fillRoundedRectangle(curve, 6.0f);
    juce::Path transfer;
    for (int step = 0; step <= 64; ++step)
    {
        const auto levelDb = curveLowDb
            + (curveHighDb - curveLowDb) * static_cast<float>(step) / 64.0f;
        const auto over = levelDb - FetLimiterModule::internalThresholdDb;
        const auto reductionDb = FetLimiterModule::kneeReductionDb(
            over, ratio, knee);
        const auto x = curve.getX()
            + (levelDb - curveLowDb) / (curveHighDb - curveLowDb)
                  * curve.getWidth();
        const auto y = curve.getBottom()
            - juce::jlimit(0.0f, 1.0f, -reductionDb / reductionRangeDb)
                  * curve.getHeight();
        if (step == 0)
            transfer.startNewSubPath(x, y);
        else
            transfer.lineTo(x, y);
    }
    graphics.setColour(accent);
    graphics.strokePath(transfer, juce::PathStrokeType(2.0f));

    const auto liveDetectorDb = juce::jlimit(
        curveLowDb, curveHighDb,
        processor.getRack().slotDetectorMeter(slot));
    const auto liveReductionDb = juce::jlimit(
        0.0f, reductionRangeDb, processor.getRack().slotMeter(slot));
    const auto liveX = curve.getX()
        + (liveDetectorDb - curveLowDb) / (curveHighDb - curveLowDb)
              * curve.getWidth();
    const auto liveY = curve.getBottom()
        - juce::jlimit(0.0f, 1.0f, liveReductionDb / reductionRangeDb)
              * curve.getHeight();
    graphics.setColour(reductionColour);
    graphics.fillEllipse(liveX - 4.0f, liveY - 4.0f, 8.0f, 8.0f);
    graphics.setColour(juce::Colours::white);
    graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    graphics.drawText(
        "TRANSFER", curve.withHeight(14.0f).toNearestInt(),
        juce::Justification::centred);

    ContinuousTelemetrySnapshot telemetrySnapshot;
    const auto hasTelemetry =
        readContinuousTelemetry(telemetrySnapshot)
        && telemetrySnapshot.sequence != 0
        && telemetrySnapshot.valueCount
               >= FetLimiterModule::telemetryValueCount;
    graphics.setColour(juce::Colour(0xff242c33));
    graphics.fillRoundedRectangle(history, 6.0f);
    auto drawHistoryLane = [&](std::uint32_t lane, juce::Colour colour, float range)
    {
        juce::Path path;
        for (std::uint32_t index = 0;
             hasTelemetry && index < telemetrySnapshot.historyCount; ++index)
        {
            const auto x = history.getX()
                + static_cast<float>(index)
                      / static_cast<float>(juce::jmax(
                            1u, telemetrySnapshot.historyCount - 1))
                      * history.getWidth();
            const auto measured = continuousTelemetryHistoryValue(
                telemetrySnapshot, lane, index);
            const auto y = history.getBottom()
                - juce::jlimit(0.0f, 1.0f, measured / range)
                      * history.getHeight();
            if (index == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
        graphics.setColour(colour);
        graphics.strokePath(path, juce::PathStrokeType(1.6f));
    };
    drawHistoryLane(
        FetLimiterModule::gainReductionHistory, reductionColour,
        reductionRangeDb);
    drawHistoryLane(
        FetLimiterModule::detectorHistory, accent.withAlpha(0.85f), 40.0f);
    graphics.setColour(juce::Colours::white);
    graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    graphics.drawText(
        "HISTORY", history.withHeight(14.0f).toNearestInt(),
        juce::Justification::centredTop);

    graphics.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    graphics.setColour(juce::Colours::white);
    graphics.drawText(
        "GR " + juce::String(liveReductionDb, 1) + " dB",
        plot.toNearestInt(), juce::Justification::centredBottom);

    for (int track = 0; track < static_cast<int>(trackControls.size());
         ++track)
    {
        const auto bounds = trackBounds(area, track);
        const auto control = trackControls[static_cast<size_t>(track)];
        graphics.setColour(juce::Colour(0xff27303b));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        const auto fill =
            bounds.withWidth(bounds.getWidth() * value(control));
        graphics.setColour(accent.withAlpha(0.35f));
        graphics.fillRoundedRectangle(fill, 5.0f);
        const auto x = bounds.getX() + value(control) * bounds.getWidth();
        graphics.setColour(accent);
        graphics.fillEllipse(x - 5.0f, bounds.getCentreY() - 5.0f,
                            10.0f, 10.0f);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(10.0f));
        graphics.drawText(
            juce::String(trackNames[static_cast<size_t>(track)])
                + "  " + formatControlValue(type, control, value(control)),
            bounds.toNearestInt(), juce::Justification::centred);
    }
}
} // namespace

std::unique_ptr<ModuleView> createFetLimiterView(EffectGraph& graph)
{
    return std::make_unique<FetLimiterModuleView>(graph);
}
} // namespace megadsp::ui
