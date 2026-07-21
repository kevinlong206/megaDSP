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
class TremoloModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    void configureAccessibility(juce::Component&) const override;
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    static juce::Rectangle<float> modePillBounds(
        juce::Rectangle<float>, int);
    static juce::Rectangle<float> waveBounds(juce::Rectangle<float>);
    static juce::Rectangle<float> depthBounds(juce::Rectangle<float>);
    static juce::Rectangle<float> crossoverBounds(juce::Rectangle<float>);
};

void TremoloModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Mode-aware Tremolo modulation editor");
    component.setHelpText(
        "Choose Amplitude, Harmonic, or Vibrato with the mode pills. "
        "Amplitude and Harmonic expose Tremolo Depth; Vibrato exposes Pitch "
        "Depth in cents. Crossover appears only for Harmonic. Free Rate and "
        "synced Division retain their independent values.");
}

juce::Rectangle<float> TremoloModuleView::modePillBounds(
    juce::Rectangle<float> area, int index)
{
    auto pills = area.removeFromTop(28.0f);
    const auto width = pills.getWidth() / 3.0f;
    return juce::Rectangle<float>(
        pills.getX() + static_cast<float>(index) * width,
        pills.getY(), width, pills.getHeight()).reduced(3.0f, 2.0f);
}

juce::Rectangle<float> TremoloModuleView::waveBounds(
    juce::Rectangle<float> area)
{
    area.removeFromTop(32.0f);
    area.removeFromBottom(28.0f);
    area.removeFromRight(30.0f);
    return area.reduced(5.0f, 2.0f);
}

juce::Rectangle<float> TremoloModuleView::depthBounds(
    juce::Rectangle<float> area)
{
    area.removeFromTop(34.0f);
    area.removeFromBottom(30.0f);
    return area.removeFromRight(24.0f).reduced(2.0f);
}

juce::Rectangle<float> TremoloModuleView::crossoverBounds(
    juce::Rectangle<float> area)
{
    return area.removeFromBottom(24.0f).reduced(5.0f, 3.0f);
}

void TremoloModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int mode = 0; mode < 3; ++mode)
        if (modePillBounds(area, mode).contains(event.position))
        {
            if (auto* target = parameter(0))
            {
                target->beginChangeGesture();
                target->setValueNotifyingHost(discreteValue(mode, 3));
                target->endChangeGesture();
            }
            repaint();
            return;
        }
    const auto mode = discreteIndex(value(0), 3);
    if (depthBounds(area).contains(event.position))
        dragPrimary = mode == 2 ? 5 : 4;
    else if (mode == 1
             && crossoverBounds(area).contains(event.position))
        dragPrimary = 8;
    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void TremoloModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (dragPrimary == 8)
    {
        const auto crossover = crossoverBounds(area);
        setValue(8, juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - crossover.getX())
                / crossover.getWidth()));
    }
    else
    {
        const auto depth = depthBounds(area);
        setValue(dragPrimary, juce::jlimit(
            0.0f, 1.0f,
            (depth.getBottom() - event.position.y) / depth.getHeight()));
    }
    updateDefaultDragReadout();
}

void TremoloModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto defaults = moduleDefaults(type);
    const auto mode = discreteIndex(value(0), 3);
    if (depthBounds(area).contains(event.position))
        setValue(mode == 2 ? 5 : 4, defaults[mode == 2 ? 5 : 4]);
    else if (mode == 1
             && crossoverBounds(area).contains(event.position))
        setValue(8, defaults[8]);
    else
        return;
    repaint();
}

void TremoloModuleView::paint(juce::Graphics& graphics, juce::Rectangle<float> area)
{
    constexpr std::array<const char*, 3> names {
        "AMPLITUDE", "HARMONIC", "VIBRATO"
    };
    const auto mode = megadsp::discreteIndex(value(0), 3);
    for (int pill = 0; pill < 3; ++pill)
    {
        const auto bounds = modePillBounds(area, pill);
        graphics.setColour(
            pill == mode ? accent : juce::Colour(0xff27313d));
        graphics.fillRoundedRectangle(bounds, 6.0f);
        graphics.setColour(
            pill == mode ? juce::Colour(0xff111820)
                         : juce::Colour(0xffabb6c4));
        graphics.setFont(juce::FontOptions(
            10.5f, pill == mode ? juce::Font::bold : juce::Font::plain));
        graphics.drawText(
            names[static_cast<size_t>(pill)], bounds.toNearestInt(),
            juce::Justification::centred);
    }

    const auto shape = value(6);
    const auto rightOffset = value(7) * 0.5f;
    const auto tremoloDepth = value(4);
    const auto pitchDepth = value(5) * 100.0f;
    auto waveArea = waveBounds(area);
    const auto centreY = waveArea.getCentreY();
    auto waveform = [shape](float phase)
    {
        phase -= std::floor(phase);
        const auto sine = std::sin(
            juce::MathConstants<float>::twoPi * phase);
        const auto triangle = 1.0f - 4.0f * std::abs(phase - 0.5f);
        if (shape <= 0.5f)
            return sine + (triangle - sine) * shape * 2.0f;
        const auto drive = lerp(1.0f, 6.0f, (shape - 0.5f) * 2.0f);
        const auto pulse = std::tanh(triangle * drive) / std::tanh(drive);
        return triangle + (pulse - triangle) * (shape - 0.5f) * 2.0f;
    };
    auto drawLfo = [&](float offset, juce::Colour colour)
    {
        juce::Path path;
        for (int pixel = 0; pixel < static_cast<int>(waveArea.getWidth());
             ++pixel)
        {
            const auto phase = static_cast<float>(pixel)
                               / waveArea.getWidth() + offset;
            const auto activeDepth = mode == 2 ? value(5) : value(4);
            const auto y = centreY - waveform(phase)
                                      * waveArea.getHeight()
                                      * (0.08f + activeDepth * 0.34f);
            if (pixel == 0) path.startNewSubPath(waveArea.getX(), y);
            else path.lineTo(waveArea.getX() + static_cast<float>(pixel), y);
        }
        graphics.setColour(colour);
        graphics.strokePath(path, juce::PathStrokeType(2.2f));
    };
    drawLfo(0.0f, accent);
    if (rightOffset > 0.002f)
        drawLfo(rightOffset, outputColour.withAlpha(0.82f));

    graphics.setColour(juce::Colour(0xffa8b3c0));
    graphics.drawText(
        value(2) >= 0.5f
            ? "DIVISION  "
                  + megadsp::formatControlValue(type, 3, value(3))
            : "RATE  " + megadsp::formatControlValue(type, 1, value(1)),
        waveArea.toNearestInt(), juce::Justification::topLeft);
    graphics.drawText(
        mode == 2
            ? "PITCH DEPTH  " + juce::String(pitchDepth, 1) + " cents"
            : "TREMOLO DEPTH  "
                  + juce::String(tremoloDepth * 100.0f, 0) + "%",
        waveArea.toNearestInt(), juce::Justification::topRight);

    const auto depth = depthBounds(area);
    graphics.setColour(juce::Colour(0xff151b24));
    graphics.fillRoundedRectangle(depth, 4.0f);
    const auto depthValue = mode == 2 ? value(5) : value(4);
    auto depthFill = depth;
    depthFill.setY(depth.getBottom() - depth.getHeight() * depthValue);
    graphics.setColour(mode == 2 ? outputColour : accent);
    graphics.fillRoundedRectangle(depthFill, 4.0f);
    const auto depthY = depth.getBottom() - depth.getHeight() * depthValue;
    graphics.setColour(juce::Colours::white);
    graphics.fillEllipse(
        depth.getCentreX() - 5.0f, depthY - 5.0f, 10.0f, 10.0f);

    if (mode == 1)
    {
        auto bandBar = crossoverBounds(area);
        const auto splitX = bandBar.getX() + value(8) * bandBar.getWidth();
        auto lowBar = bandBar.withRight(splitX);
        auto highBar = bandBar.withLeft(splitX);
        graphics.setColour(juce::Colour(0xff6f8cff).withAlpha(0.42f));
        graphics.fillRoundedRectangle(lowBar, 4.0f);
        graphics.setColour(juce::Colour(0xffffb15c).withAlpha(0.42f));
        graphics.fillRoundedRectangle(highBar, 4.0f);
        graphics.setColour(juce::Colours::white);
        graphics.fillEllipse(
            splitX - 5.0f, bandBar.getCentreY() - 5.0f, 10.0f, 10.0f);
        graphics.setColour(juce::Colours::white);
        graphics.drawText(
            "HARMONIC CROSSOVER  "
                + megadsp::formatControlValue(type, 8, value(8)),
            bandBar.toNearestInt(), juce::Justification::centred);
    }
    else
    {
        graphics.setColour(juce::Colour(0xff8995a4));
        graphics.drawText(
            rightOffset > 0.002f ? "LEFT / RIGHT PHASE" : "MONO-COHERENT LFO",
            crossoverBounds(area).toNearestInt(),
            juce::Justification::centred);
    }
}
} // namespace

std::unique_ptr<ModuleView> createTremoloView(EffectGraph& graph)
{
    return std::make_unique<TremoloModuleView>(graph);
}
} // namespace megadsp::ui
