#include "../GraphStyle.h"
#include "../ModuleView.h"

#include "ModuleViewCreators.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace megadsp::ui
{
namespace
{
class ConvolutionReverbModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    void configureAccessibility(juce::Component&) const override;
    bool usesFullPanel() const override { return true; }
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void timerCallback() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    enum class ViewMode
    {
        room,
        analyzer,
        history
    };

    static juce::Rectangle<float> waveformColumn(
        juce::Rectangle<float>);
    static juce::Rectangle<float> waveformBounds(
        juce::Rectangle<float>);
    static juce::Rectangle<float> passbandBounds(
        juce::Rectangle<float>);
    static juce::Rectangle<float> controlsBounds(
        juce::Rectangle<float>);
    static juce::Rectangle<float> viewSelectorBounds(
        juce::Rectangle<float>);
    static juce::Rectangle<float> railBounds(
        juce::Rectangle<float>, int);
    static juce::Rectangle<float> railTrackBounds(
        juce::Rectangle<float>, int);

    static constexpr int spectrogramFrameCount = 72;
    using SpectrumFrame =
        std::array<float, ConvolutionReverbModule::spectrumBandCount>;
    std::array<SpectrumFrame, spectrogramFrameCount> spectrogram {};
    std::uint64_t lastTelemetrySequence = 0;
    int spectrogramWritePosition = 0;
    int spectrogramCount = 0;
    ViewMode viewMode = ViewMode::room;
};

void ConvolutionReverbModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Convolution reverb impulse response");
    component.setHelpText(
        "Drop a WAV, AIFF, or FLAC impulse response file here. "
        "Use the previous and next header buttons to browse the persisted IR "
        "directory. "
        "The live stereo chamber shows measured wet output as expanding "
        "left and right reflection rings. Ring colour follows the measured "
        "brightness of the reverberation. Switch between Room, an EQ-style "
        "wet Analyzer, and a rolling wet-only History spectrograph. "
        "It has a separate Low Cut and High Cut passband strip. "
        "The adjacent Dry, Wet, Ducking, and Output Trim rails are directly "
        "editable. Double-click any rail or passband handle to restore its "
        "default.");
}

juce::Rectangle<float> ConvolutionReverbModuleView::controlsBounds(
    juce::Rectangle<float> area)
{
    const auto width = juce::jlimit(
        220.0f, 300.0f, area.getWidth() * 0.29f);
    return area.removeFromRight(width);
}

juce::Rectangle<float> ConvolutionReverbModuleView::waveformColumn(
    juce::Rectangle<float> area)
{
    const auto controls = controlsBounds(area);
    area.removeFromRight(controls.getWidth() + 10.0f);
    return area;
}

juce::Rectangle<float> ConvolutionReverbModuleView::waveformBounds(
    juce::Rectangle<float> area)
{
    auto column = waveformColumn(area);
    column.removeFromTop(23.0f);
    column.removeFromBottom(42.0f);
    return column;
}

juce::Rectangle<float> ConvolutionReverbModuleView::viewSelectorBounds(
    juce::Rectangle<float> area)
{
    auto header = waveformColumn(area).removeFromTop(20.0f);
    return header.removeFromRight(216.0f).reduced(1.0f);
}

juce::Rectangle<float> ConvolutionReverbModuleView::passbandBounds(
    juce::Rectangle<float> area)
{
    auto column = waveformColumn(area);
    return column.removeFromBottom(36.0f);
}

juce::Rectangle<float> ConvolutionReverbModuleView::railBounds(
    juce::Rectangle<float> area, int index)
{
    auto rails = controlsBounds(area).reduced(7.0f);
    rails.removeFromTop(22.0f);
    const auto width = rails.getWidth() * 0.25f;
    return juce::Rectangle<float>(
        rails.getX() + width * static_cast<float>(index),
        rails.getY(), width, rails.getHeight()).reduced(4.0f, 0.0f);
}

juce::Rectangle<float> ConvolutionReverbModuleView::railTrackBounds(
    juce::Rectangle<float> area, int index)
{
    auto rail = railBounds(area, index);
    rail.removeFromTop(22.0f);
    rail.removeFromBottom(21.0f);
    return rail.reduced(rail.getWidth() * 0.34f, 2.0f);
}

void ConvolutionReverbModuleView::mouseDown(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto selector = viewSelectorBounds(area);
    if (selector.contains(event.position))
    {
        const auto index = juce::jlimit(
            0, 2,
            static_cast<int>(
                (event.position.x - selector.getX())
                / (selector.getWidth() / 3.0f)));
        viewMode = static_cast<ViewMode>(index);
        repaint();
        return;
    }
    const auto passband = passbandBounds(area);
    const auto frequencyX = [&passband](float frequency)
    {
        return passband.getX()
            + std::log(frequency / 20.0f) / std::log(1000.0f)
                  * passband.getWidth();
    };
    const auto lowX = frequencyX(
        exponential(20.0f, 1000.0f, value(0)));
    const auto highX = frequencyX(
        exponential(2000.0f, 20000.0f, value(1)));
    const auto lowDistance = std::abs(event.position.x - lowX);
    const auto highDistance = std::abs(event.position.x - highX);
    if (passband.expanded(0.0f, 5.0f).contains(event.position)
        && juce::jmin(lowDistance, highDistance) <= 17.0f)
        dragPrimary = lowDistance <= highDistance ? 0 : 1;
    else
    {
        static constexpr std::array<int, 4> controls { 4, 2, 5, 3 };
        for (int index = 0; index < 4; ++index)
            if (railBounds(area, index).contains(event.position))
                dragPrimary = controls[static_cast<size_t>(index)];
    }

    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void ConvolutionReverbModuleView::timerCallback()
{
    ContinuousTelemetrySnapshot telemetry;
    if (!readContinuousTelemetry(telemetry)
        || telemetry.sequence == 0
        || telemetry.sequence == lastTelemetrySequence
        || telemetry.valueCount
               < ConvolutionReverbModule::telemetryValueCount)
        return;

    lastTelemetrySequence = telemetry.sequence;
    auto& frame = spectrogram[
        static_cast<size_t>(spectrogramWritePosition)];
    for (size_t band = 0; band < frame.size(); ++band)
        frame[band] = telemetry.values[
            ConvolutionReverbModule::spectrumBandStart + band];
    spectrogramWritePosition =
        (spectrogramWritePosition + 1) % spectrogramFrameCount;
    spectrogramCount = juce::jmin(
        spectrogramCount + 1, spectrogramFrameCount);
}

void ConvolutionReverbModuleView::mouseDrag(
    const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (dragPrimary == 0 || dragPrimary == 1)
    {
        const auto passband = passbandBounds(area);
        const auto proportion = juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - passband.getX()) / passband.getWidth());
        const auto frequency = 20.0f * std::pow(1000.0f, proportion);
        if (dragPrimary == 0)
            setValue(0, juce::jlimit(
                0.0f, 1.0f,
                std::log(
                    juce::jlimit(20.0f, 1000.0f, frequency) / 20.0f)
                    / std::log(50.0f)));
        else
            setValue(1, juce::jlimit(
                0.0f, 1.0f,
                std::log(
                    juce::jlimit(2000.0f, 20000.0f, frequency) / 2000.0f)
                    / std::log(10.0f)));
    }
    else
    {
        static constexpr std::array<int, 4> controls { 4, 2, 5, 3 };
        const auto position = std::find(
            controls.begin(), controls.end(), dragPrimary);
        if (position == controls.end())
            return;
        const auto track = railTrackBounds(
            area, static_cast<int>(std::distance(controls.begin(), position)));
        setValue(dragPrimary, juce::jlimit(
            0.0f, 1.0f,
            (track.getBottom() - event.position.y) / track.getHeight()));
    }
    updateDefaultDragReadout();
}

void ConvolutionReverbModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto defaults = moduleDefaults(type);
    static constexpr std::array<int, 4> controls { 4, 2, 5, 3 };
    for (int index = 0; index < 4; ++index)
        if (railBounds(area, index).contains(event.position))
        {
            setValue(
                controls[static_cast<size_t>(index)],
                defaults[static_cast<size_t>(
                    controls[static_cast<size_t>(index)])]);
            repaint();
            return;
        }

    const auto passband = passbandBounds(area);
    const auto frequencyX = [&passband](float frequency)
    {
        return passband.getX()
            + std::log(frequency / 20.0f) / std::log(1000.0f)
                  * passband.getWidth();
    };
    const auto lowX = frequencyX(
        exponential(20.0f, 1000.0f, value(0)));
    const auto highX = frequencyX(
        exponential(2000.0f, 20000.0f, value(1)));
    const auto lowDistance = std::abs(event.position.x - lowX);
    const auto highDistance = std::abs(event.position.x - highX);
    if (!passband.expanded(0.0f, 5.0f).contains(event.position)
        || juce::jmin(lowDistance, highDistance) > 17.0f)
        return;
    const auto control = lowDistance <= highDistance ? 0 : 1;
    setValue(control, defaults[static_cast<size_t>(control)]);
    repaint();
}

void ConvolutionReverbModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto name = processor.impulseResponseName(slot);
    const auto storedPath = processor.impulseResponsePath(slot);
    auto column = waveformColumn(area);
    const auto waveform = waveformBounds(area);
    const auto passband = passbandBounds(area);
    const auto controlsPanel = controlsBounds(area);
    const auto lowCut = exponential(20.0f, 1000.0f, value(0));
    const auto highCut = exponential(2000.0f, 20000.0f, value(1));
    const auto frequencyX = [](juce::Rectangle<float> bounds, float frequency)
    {
        return bounds.getX()
            + std::log(frequency / 20.0f) / std::log(1000.0f)
                  * bounds.getWidth();
    };
    graphics.setColour(juce::Colour(0xff151b24));
    graphics.fillRoundedRectangle(waveform, 7.0f);
    graphics.setColour(juce::Colour(0xffa8b3c0));
    graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    auto header = column.removeFromTop(20.0f);
    auto nameBounds = header;
    if (name.isNotEmpty())
        nameBounds.removeFromRight(222.0f);
    graphics.drawText(
        name.isNotEmpty() ? name : "IMPULSE RESPONSE",
        nameBounds.toNearestInt(),
        juce::Justification::centredLeft);
    if (name.isNotEmpty())
    {
        const auto selector = viewSelectorBounds(area);
        const auto pillWidth = selector.getWidth() / 3.0f;
        graphics.setColour(juce::Colours::white.withAlpha(0.08f));
        graphics.fillRoundedRectangle(selector, 5.0f);
        graphics.setColour(accent.withAlpha(0.72f));
        graphics.fillRoundedRectangle(
            juce::Rectangle<float>(
                selector.getX()
                    + static_cast<float>(static_cast<int>(viewMode))
                          * pillWidth,
                selector.getY(), pillWidth, selector.getHeight()),
            5.0f);
        graphics.setColour(juce::Colours::white.withAlpha(0.92f));
        graphics.setFont(juce::FontOptions(7.5f, juce::Font::bold));
        static constexpr std::array<const char*, 3> viewNames {
            "ROOM", "ANALYZER", "HISTORY"
        };
        for (int index = 0; index < 3; ++index)
            graphics.drawText(
                viewNames[static_cast<size_t>(index)],
                juce::Rectangle<float>(
                    selector.getX() + static_cast<float>(index) * pillWidth,
                    selector.getY(), pillWidth,
                    selector.getHeight()).toNearestInt(),
                juce::Justification::centred);
    }

    if (name.isEmpty())
    {
        graphics.setColour(juce::Colour(0xffa8b3c0));
        graphics.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        graphics.drawText(
            storedPath.isNotEmpty()
                ? "IR FILE NOT FOUND\n"
                      + juce::File(storedPath).getFileName()
                      + "\nLOAD OR DROP A REPLACEMENT"
                : "LOAD IR... OR DROP WAV / AIFF / FLAC HERE",
            waveform.reduced(12.0f).toNearestInt(),
            juce::Justification::centred);
    }
    else
    {
        ContinuousTelemetrySnapshot telemetry;
        const auto hasTelemetry =
            readContinuousTelemetry(telemetry)
            && telemetry.sequence != 0
            && telemetry.valueCount
                   >= ConvolutionReverbModule::telemetryValueCount
            && telemetry.historyValueCount
                   >= ConvolutionReverbModule::telemetryHistoryCount
            && telemetry.historyCount > 0;
        if (viewMode == ViewMode::history)
        {
            const auto graphArea = waveform.reduced(11.0f, 18.0f);
            graphics.setColour(juce::Colours::white.withAlpha(0.045f));
            for (int division = 1; division < 4; ++division)
            {
                const auto y = graphArea.getY()
                    + graphArea.getHeight() * static_cast<float>(division)
                          / 4.0f;
                graphics.drawHorizontalLine(
                    juce::roundToInt(y),
                    graphArea.getX(), graphArea.getRight());
            }

            const auto cellWidth =
                graphArea.getWidth() / static_cast<float>(
                    spectrogramFrameCount);
            const auto cellHeight =
                graphArea.getHeight()
                / static_cast<float>(
                    ConvolutionReverbModule::spectrumBandCount);
            const auto oldest = spectrogramCount < spectrogramFrameCount
                ? 0 : spectrogramWritePosition;
            const auto startX = graphArea.getRight()
                - cellWidth * static_cast<float>(spectrogramCount);
            const auto dark = juce::Colour(0xff111827);
            const auto warm = juce::Colour(0xffd85b72);
            const auto bright = juce::Colour(0xff78ddff);
            for (int frame = 0; frame < spectrogramCount; ++frame)
            {
                const auto& spectrum = spectrogram[static_cast<size_t>(
                    (oldest + frame) % spectrogramFrameCount)];
                for (size_t band = 0; band < spectrum.size(); ++band)
                {
                    const auto intensity = juce::jlimit(
                        0.0f, 1.0f,
                        (juce::Decibels::gainToDecibels(
                             spectrum[band], -84.0f)
                         + 84.0f)
                            / 72.0f);
                    auto colour = dark.interpolatedWith(
                        warm, juce::jmin(1.0f, intensity * 1.7f));
                    if (intensity > 0.58f)
                        colour = warm.interpolatedWith(
                            bright, (intensity - 0.58f) / 0.42f);
                    const auto y = graphArea.getBottom()
                        - static_cast<float>(band + 1) * cellHeight;
                    graphics.setColour(colour.withAlpha(
                        0.18f + intensity * 0.82f));
                    graphics.fillRect(
                        startX + static_cast<float>(frame) * cellWidth,
                        y, cellWidth + 0.6f, cellHeight + 0.6f);
                }
            }
            graphics.setColour(juce::Colours::white.withAlpha(0.55f));
            graphics.setFont(juce::FontOptions(8.5f, juce::Font::bold));
            graphics.drawText(
                "AIR", waveform.reduced(7.0f).toNearestInt(),
                juce::Justification::topLeft);
            graphics.drawText(
                "LOW", waveform.reduced(7.0f).toNearestInt(),
                juce::Justification::bottomLeft);
            graphics.drawText(
                spectrogramCount > 0
                    ? "WET SPECTROGRAPH  /  NEWEST"
                    : "PLAY AUDIO TO EXCITE THE IR",
                waveform.reduced(7.0f).toNearestInt(),
                juce::Justification::bottomRight);
        }
        else if (viewMode == ViewMode::analyzer)
        {
            const auto graphArea = waveform.reduced(30.0f, 24.0f);
            static constexpr std::array<float, 5> frequencies {
                80.0f, 250.0f, 1000.0f, 4000.0f, 12000.0f
            };
            static constexpr std::array<const char*, 5> frequencyLabels {
                "80", "250", "1k", "4k", "12k"
            };
            const auto frequencyPosition = [&graphArea](float frequency)
            {
                return graphArea.getX()
                    + std::log(frequency / 80.0f)
                          / std::log(12000.0f / 80.0f)
                          * graphArea.getWidth();
            };
            graphics.setColour(juce::Colours::white.withAlpha(0.055f));
            graphics.setFont(juce::FontOptions(8.0f));
            for (size_t index = 0; index < frequencies.size(); ++index)
            {
                const auto x = frequencyPosition(frequencies[index]);
                graphics.drawVerticalLine(
                    juce::roundToInt(x),
                    graphArea.getY(), graphArea.getBottom());
                graphics.drawText(
                    frequencyLabels[index],
                    juce::Rectangle<float>(
                        x - 18.0f, graphArea.getBottom() + 3.0f,
                        36.0f, 12.0f).toNearestInt(),
                    juce::Justification::centred);
            }
            for (int line = 1; line < 4; ++line)
            {
                const auto y = graphArea.getY()
                    + graphArea.getHeight() * static_cast<float>(line)
                          / 4.0f;
                graphics.drawHorizontalLine(
                    juce::roundToInt(y),
                    graphArea.getX(), graphArea.getRight());
            }

            std::array<
                juce::Point<float>,
                ConvolutionReverbModule::spectrumBandCount> points {};
            constexpr auto analyzerDisplayGainDb = 18.0f;
            const auto analyzerDisplayGain =
                juce::Decibels::decibelsToGain(analyzerDisplayGainDb);
            for (size_t band = 0; band < points.size(); ++band)
            {
                const auto magnitude = hasTelemetry
                    ? telemetry.values[
                          ConvolutionReverbModule::spectrumBandStart + band]
                          * analyzerDisplayGain
                    : 0.0f;
                const auto db = juce::Decibels::gainToDecibels(
                    magnitude, -72.0f);
                points[band] = {
                    graphArea.getX()
                        + (static_cast<float>(band) + 0.5f)
                              / static_cast<float>(points.size())
                              * graphArea.getWidth(),
                    graphArea.getBottom()
                        - (db + 72.0f) / 72.0f * graphArea.getHeight()
                };
            }

            juce::Path response;
            response.startNewSubPath(points.front());
            for (size_t band = 1; band + 1 < points.size(); ++band)
            {
                const auto midpoint =
                    (points[band] + points[band + 1]) * 0.5f;
                response.quadraticTo(points[band], midpoint);
            }
            response.lineTo(points.back());
            auto fill = response;
            fill.lineTo(points.back().x, graphArea.getBottom());
            fill.lineTo(points.front().x, graphArea.getBottom());
            fill.closeSubPath();

            const auto lowColour = juce::Colour(0xffff9b61);
            const auto midColour = juce::Colour(0xffd85bba);
            const auto highColour = juce::Colour(0xff78ddff);
            juce::ColourGradient fillGradient(
                lowColour.withAlpha(0.30f), graphArea.getX(), 0.0f,
                highColour.withAlpha(0.30f), graphArea.getRight(), 0.0f,
                false);
            fillGradient.addColour(0.52, midColour.withAlpha(0.34f));
            graphics.setGradientFill(fillGradient);
            graphics.fillPath(fill);

            juce::ColourGradient lineGradient(
                lowColour, graphArea.getX(), 0.0f,
                highColour, graphArea.getRight(), 0.0f, false);
            lineGradient.addColour(0.52, midColour);
            graphics.setGradientFill(lineGradient);
            graphics.strokePath(
                response, juce::PathStrokeType(
                              2.2f, juce::PathStrokeType::curved,
                              juce::PathStrokeType::rounded));
            for (size_t band = 0; band < points.size(); ++band)
            {
                const auto proportion = static_cast<float>(band)
                    / static_cast<float>(points.size() - 1);
                const auto colour = proportion < 0.52f
                    ? lowColour.interpolatedWith(
                          midColour, proportion / 0.52f)
                    : midColour.interpolatedWith(
                          highColour, (proportion - 0.52f) / 0.48f);
                graphics.setColour(colour);
                graphics.fillEllipse(
                    juce::Rectangle<float>(5.0f, 5.0f)
                        .withCentre(points[band]));
            }

            graphics.setColour(juce::Colours::white.withAlpha(0.55f));
            graphics.setFont(juce::FontOptions(8.5f, juce::Font::bold));
            graphics.drawText(
                "WET ONLY  /  +18 dB DISPLAY",
                waveform.reduced(7.0f).toNearestInt(),
                juce::Justification::topLeft);
            if (!hasTelemetry)
                graphics.drawText(
                    "PLAY AUDIO TO EXCITE THE IR",
                    waveform.reduced(7.0f).toNearestInt(),
                    juce::Justification::centredBottom);
        }
        else
        {
        const auto centre = waveform.getCentre();
        const auto chamber = waveform.reduced(18.0f, 24.0f);
        graphics.setColour(juce::Colours::white.withAlpha(0.045f));
        for (int ring = 1; ring <= 5; ++ring)
        {
            const auto scale = static_cast<float>(ring) / 5.0f;
            const auto guide = juce::Rectangle<float>(
                chamber.getWidth() * scale,
                chamber.getHeight() * scale).withCentre(centre);
            graphics.drawEllipse(guide, 1.0f);
        }
        graphics.setColour(juce::Colours::white.withAlpha(0.035f));
        graphics.drawHorizontalLine(
            juce::roundToInt(centre.y),
            chamber.getX(), chamber.getRight());
        graphics.drawVerticalLine(
            juce::roundToInt(centre.x),
            chamber.getY(), chamber.getBottom());

        const auto normalizedLevel = [](float rms)
        {
            return juce::jlimit(
                0.0f, 1.0f,
                (juce::Decibels::gainToDecibels(rms, -72.0f) + 72.0f)
                    / 72.0f);
        };
        const auto drawReflections =
            [&graphics, &waveform, &chamber, &telemetry, centre,
             &normalizedLevel](
                std::uint32_t levelLane, std::uint32_t brightnessLane,
                bool left, juce::Colour brightColour)
        {
            graphics.saveState();
            auto half = waveform;
            if (left)
                half.setRight(centre.x);
            else
                half.setLeft(centre.x);
            graphics.reduceClipRegion(half.toNearestInt());

            const auto warmColour = juce::Colour(0xffff9b61);
            for (int age = static_cast<int>(telemetry.historyCount) - 1;
                 age >= 0; --age)
            {
                const auto chronologicalIndex =
                    telemetry.historyCount - 1u
                    - static_cast<std::uint32_t>(age);
                const auto level = normalizedLevel(
                    continuousTelemetryHistoryValue(
                        telemetry, levelLane, chronologicalIndex));
                if (level <= 0.001f)
                    continue;
                const auto brightness = juce::jlimit(
                    0.0f, 1.0f,
                    continuousTelemetryHistoryValue(
                        telemetry, brightnessLane, chronologicalIndex));
                const auto radius =
                    (static_cast<float>(age) + 2.0f)
                    / static_cast<float>(
                        continuousTelemetryHistoryCapacity + 1);
                const auto bounds = juce::Rectangle<float>(
                    chamber.getWidth() * radius,
                    chamber.getHeight() * radius).withCentre(centre);
                const auto colour =
                    warmColour.interpolatedWith(brightColour, brightness);
                const auto ageFade = 1.0f - radius * 0.58f;
                graphics.setColour(colour.withAlpha(
                    juce::jlimit(0.0f, 0.72f, level * ageFade * 0.72f)));
                graphics.drawEllipse(bounds, 0.8f + level * 2.2f);
            }
            graphics.restoreState();
        };
        if (hasTelemetry)
        {
            drawReflections(
                ConvolutionReverbModule::leftWetHistory,
                ConvolutionReverbModule::leftBrightnessHistory,
                true, accent.brighter(0.25f));
            drawReflections(
                ConvolutionReverbModule::rightWetHistory,
                ConvolutionReverbModule::rightBrightnessHistory,
                false, outputColour.brighter(0.25f));
        }

        const auto leftSource = hasTelemetry
            ? normalizedLevel(telemetry.values[
                  ConvolutionReverbModule::leftWetLevel]) : 0.0f;
        const auto rightSource = hasTelemetry
            ? normalizedLevel(telemetry.values[
                  ConvolutionReverbModule::rightWetLevel]) : 0.0f;
        graphics.saveState();
        graphics.reduceClipRegion(
            waveform.withRight(centre.x).toNearestInt());
        graphics.setColour(accent.withAlpha(0.20f + leftSource * 0.65f));
        graphics.fillEllipse(
            juce::Rectangle<float>(
                10.0f + leftSource * 24.0f,
                10.0f + leftSource * 24.0f).withCentre(centre));
        graphics.restoreState();
        graphics.saveState();
        graphics.reduceClipRegion(
            waveform.withLeft(centre.x).toNearestInt());
        graphics.setColour(outputColour.withAlpha(0.20f + rightSource * 0.65f));
        graphics.fillEllipse(
            juce::Rectangle<float>(
                10.0f + rightSource * 24.0f,
                10.0f + rightSource * 24.0f).withCentre(centre));
        graphics.restoreState();
        graphics.setColour(juce::Colours::white.withAlpha(0.85f));
        graphics.fillEllipse(
            juce::Rectangle<float>(6.0f, 6.0f).withCentre(centre));

        graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        graphics.setColour(accent);
        graphics.drawText(
            "L", waveform.reduced(7.0f).toNearestInt(),
            juce::Justification::topLeft);
        graphics.setColour(outputColour);
        graphics.drawText(
            "R", waveform.reduced(7.0f).toNearestInt(),
            juce::Justification::topRight);
        graphics.setColour(juce::Colours::white.withAlpha(0.48f));
        graphics.drawText(
            hasTelemetry
                    && telemetry.values[
                           ConvolutionReverbModule::outputChannelCount] < 1.5f
                ? "LIVE MONO WET" : "LIVE STEREO WET",
            waveform.reduced(7.0f).toNearestInt(),
            juce::Justification::centredTop);
        graphics.drawText(
            hasTelemetry ? "2 SEC DECAY" : "PLAY AUDIO TO EXCITE THE IR",
            waveform.reduced(7.0f).toNearestInt(),
            juce::Justification::centredBottom);
        }
    }

    graphics.setColour(juce::Colour(0xff151b24));
    graphics.fillRoundedRectangle(passband, 5.0f);
    const auto passLowX = frequencyX(passband, lowCut);
    const auto passHighX = frequencyX(passband, highCut);
    const auto passbandY = passband.getBottom() - 8.0f;
    graphics.setColour(juce::Colours::white.withAlpha(0.28f));
    graphics.drawHorizontalLine(
        juce::roundToInt(passbandY),
        passband.getX() + 5.0f, passband.getRight() - 5.0f);
    graphics.setColour(accent.withAlpha(0.50f));
    graphics.drawLine(
        passLowX, passbandY, passHighX, passbandY, 3.0f);
    for (const auto x : { passLowX, passHighX })
    {
        graphics.setColour(juce::Colours::white);
        graphics.fillEllipse(
            juce::Rectangle<float>(10.0f, 10.0f)
                .withCentre({ x, passbandY }));
    }
    graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    graphics.setColour(juce::Colours::white);
    graphics.drawText(
        "LOW CUT  " + megadsp::formatControlValue(type, 0, value(0)),
        passband.reduced(7.0f, 2.0f).toNearestInt(),
        juce::Justification::topLeft);
    graphics.drawText(
        "HIGH CUT  " + megadsp::formatControlValue(type, 1, value(1)),
        passband.reduced(7.0f, 2.0f).toNearestInt(),
        juce::Justification::topRight);

    graphics.setColour(juce::Colour(0xd918202a));
    graphics.fillRoundedRectangle(controlsPanel, 7.0f);
    graphics.setColour(juce::Colour(0xffa8b3c0));
    graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    graphics.drawText(
        "LEVELS & DYNAMICS", controlsPanel.reduced(8.0f).toNearestInt(),
        juce::Justification::centredTop);
    static constexpr std::array<int, 4> controlIndices { 4, 2, 5, 3 };
    static constexpr std::array<const char*, 4> labels {
        "DRY", "WET", "DUCK", "OUTPUT"
    };
    const std::array<juce::Colour, 4> colours {
        inputColour, accent, juce::Colour(0xffffb15c), outputColour
    };
    for (int index = 0; index < 4; ++index)
    {
        const auto control = controlIndices[static_cast<size_t>(index)];
        auto rail = railBounds(area, index);
        const auto track = railTrackBounds(area, index);
        const auto handleY = track.getBottom() - value(control) * track.getHeight();
        graphics.setColour(juce::Colours::white.withAlpha(0.16f));
        graphics.fillRoundedRectangle(track, 3.0f);
        auto fill = track.withTop(handleY);
        if (control == 3)
        {
            const auto centre = track.getCentreY();
            fill = juce::Rectangle<float>(
                track.getX(), juce::jmin(centre, handleY),
                track.getWidth(), std::abs(handleY - centre));
        }
        graphics.setColour(colours[static_cast<size_t>(index)].withAlpha(0.72f));
        graphics.fillRoundedRectangle(fill, 3.0f);
        graphics.setColour(juce::Colours::white);
        graphics.fillEllipse(
            juce::Rectangle<float>(12.0f, 12.0f)
                .withCentre({ track.getCentreX(), handleY }));
        graphics.setFont(juce::FontOptions(8.5f, juce::Font::bold));
        graphics.drawText(
            labels[static_cast<size_t>(index)],
            rail.removeFromTop(18.0f).toNearestInt(),
            juce::Justification::centred);
        graphics.drawFittedText(
            megadsp::formatControlValue(type, control, value(control)),
            rail.removeFromBottom(18.0f).toNearestInt(),
            juce::Justification::centred, 1, 0.75f);
    }
}
} // namespace

std::unique_ptr<ModuleView> createConvolutionReverbView(EffectGraph& graph)
{
    return std::make_unique<ConvolutionReverbModuleView>(graph);
}
} // namespace megadsp::ui
