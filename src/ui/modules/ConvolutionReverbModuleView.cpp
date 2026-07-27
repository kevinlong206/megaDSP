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
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    static juce::Rectangle<float> waveformColumn(
        juce::Rectangle<float>);
    static juce::Rectangle<float> waveformBounds(
        juce::Rectangle<float>);
    static juce::Rectangle<float> passbandBounds(
        juce::Rectangle<float>);
    static juce::Rectangle<float> controlsBounds(
        juce::Rectangle<float>);
    static juce::Rectangle<float> railBounds(
        juce::Rectangle<float>, int);
    static juce::Rectangle<float> railTrackBounds(
        juce::Rectangle<float>, int);
};

void ConvolutionReverbModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Convolution reverb impulse response");
    component.setHelpText(
        "Drop a WAV, AIFF, or FLAC impulse response file here. "
        "Use the previous and next header buttons to browse the persisted IR "
        "directory. "
        "The waveform has a separate Low Cut and High Cut passband strip. "
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
    const auto preview = processor.impulseResponsePreview(slot);
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
    const auto lowX = frequencyX(waveform, lowCut);
    const auto highX = frequencyX(waveform, highCut);
    graphics.setColour(juce::Colour(0xff151b24));
    graphics.fillRoundedRectangle(waveform, 7.0f);
    graphics.setColour(juce::Colour(0x9911161d));
    graphics.fillRect(waveform.withRight(lowX));
    graphics.fillRect(waveform.withLeft(highX));
    graphics.setColour(juce::Colour(0xffa8b3c0));
    graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    graphics.drawText(
        name.isNotEmpty() ? name : "IMPULSE RESPONSE",
        column.removeFromTop(20.0f).toNearestInt(),
        juce::Justification::centredLeft);

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
    else if (preview.size() > 1)
    {
        juce::Path envelope;
        const auto centreY = waveform.getCentreY();
        for (size_t index = 0; index < preview.size(); ++index)
        {
            const auto x = waveform.getX()
                + static_cast<float>(index)
                      / static_cast<float>(preview.size() - 1)
                      * waveform.getWidth();
            const auto y = centreY
                - preview[index] * waveform.getHeight() * 0.42f;
            if (index == 0) envelope.startNewSubPath(x, y);
            else envelope.lineTo(x, y);
        }
        for (size_t index = preview.size(); index-- > 0;)
        {
            const auto x = waveform.getX()
                + static_cast<float>(index)
                      / static_cast<float>(preview.size() - 1)
                      * waveform.getWidth();
            envelope.lineTo(
                x, centreY + preview[index] * waveform.getHeight() * 0.42f);
        }
        envelope.closeSubPath();
        graphics.setColour(accent.withAlpha(0.24f));
        graphics.fillPath(envelope);
        graphics.setColour(accent);
        graphics.strokePath(envelope, juce::PathStrokeType(1.5f));
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
