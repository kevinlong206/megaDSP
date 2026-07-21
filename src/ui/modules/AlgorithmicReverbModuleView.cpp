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
class AlgorithmicReverbModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    void configureAccessibility(juce::Component&) const override;
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    static juce::Rectangle<float> tailBounds(juce::Rectangle<float>);
    static juce::Rectangle<float> filterBounds(juce::Rectangle<float>);
    juce::Point<float> spacePoint(juce::Rectangle<float>) const;
};

void AlgorithmicReverbModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Algorithmic reverb decay field");
    component.setHelpText(
        "Drag the two-dimensional Decay and Room Scale handle. Room Scale "
        "uses Compact, Natural, Large, "
        "and Vast names with the exact 25 to 200 percent scale. Dry and Wet "
        "are independent adjacent level rails. Drag the Low Cut and High Cut "
        "handles on the input passband; double-click a handle to reset.");
}

juce::Rectangle<float> AlgorithmicReverbModuleView::tailBounds(
    juce::Rectangle<float> area)
{
    area.removeFromBottom(30.0f);
    area.removeFromTop(area.getHeight() * 0.35f);
    area.removeFromTop(8.0f);
    return area;
}

juce::Rectangle<float> AlgorithmicReverbModuleView::filterBounds(
    juce::Rectangle<float> area)
{
    return area.removeFromBottom(24.0f).reduced(5.0f, 2.0f);
}

juce::Point<float> AlgorithmicReverbModuleView::spacePoint(
    juce::Rectangle<float> area) const
{
    const auto tail = tailBounds(area);
    const auto decay = exponential(0.2f, 12.0f, value(0));
    return {
        tail.getX() + decay / 12.0f * tail.getWidth(),
        tail.getBottom() - value(1) * tail.getHeight()
    };
}

void AlgorithmicReverbModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto filter = filterBounds(area);
    if (filter.contains(event.position))
    {
        const auto frequencyX = [&filter](float frequency)
        {
            return filter.getX()
                + std::log(frequency / 20.0f) / std::log(1000.0f)
                      * filter.getWidth();
        };
        const auto lowDistance = std::abs(
            event.position.x
            - frequencyX(exponential(20.0f, 1000.0f, value(10))));
        const auto highDistance = std::abs(
            event.position.x
            - frequencyX(exponential(2000.0f, 20000.0f, value(11))));
        if (juce::jmin(lowDistance, highDistance) <= 15.0f)
            dragPrimary = lowDistance <= highDistance ? 10 : 11;
    }
    else if (event.position.getDistanceFrom(spacePoint(area)) <= 17.0f)
    {
        dragPrimary = 0;
        dragSecondary = 1;
    }
    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void AlgorithmicReverbModuleView::mouseDrag(
    const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (dragPrimary == 10 || dragPrimary == 11)
    {
        const auto filter = filterBounds(area);
        const auto proportion = juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - filter.getX()) / filter.getWidth());
        const auto frequency = 20.0f * std::pow(1000.0f, proportion);
        if (dragPrimary == 10)
            setValue(10, std::log(
                juce::jlimit(20.0f, 1000.0f, frequency) / 20.0f)
                / std::log(50.0f));
        else
            setValue(11, std::log(
                juce::jlimit(2000.0f, 20000.0f, frequency) / 2000.0f)
                / std::log(10.0f));
        updateDefaultDragReadout();
        return;
    }
    const auto tail = tailBounds(area);
    const auto x = juce::jlimit(
        0.0f, 1.0f,
        (event.position.x - tail.getX()) / tail.getWidth());
    const auto seconds = juce::jlimit(0.2f, 12.0f, x * 12.0f);
    setValue(0, std::log(seconds / 0.2f) / std::log(12.0f / 0.2f));
    setValue(1, juce::jlimit(
        0.0f, 1.0f,
        (tail.getBottom() - event.position.y) / tail.getHeight()));
    dragReadout =
        "DECAY  " + megadsp::formatControlValue(type, 0, value(0))
        + "  ·  ROOM SCALE  "
        + megadsp::formatControlValue(type, 1, value(1));
    repaint();
}

void AlgorithmicReverbModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto defaults = moduleDefaults(type);
    if (event.position.getDistanceFrom(spacePoint(area)) <= 19.0f)
    {
        setValue(0, defaults[0]);
        setValue(1, defaults[1]);
        repaint();
        return;
    }
    const auto filter = filterBounds(area);
    if (!filter.contains(event.position))
        return;
    const auto frequencyX = [&filter](float frequency)
    {
        return filter.getX()
            + std::log(frequency / 20.0f) / std::log(1000.0f)
                  * filter.getWidth();
    };
    const auto lowDistance = std::abs(
        event.position.x
        - frequencyX(exponential(20.0f, 1000.0f, value(10))));
    const auto highDistance = std::abs(
        event.position.x
        - frequencyX(exponential(2000.0f, 20000.0f, value(11))));
    if (juce::jmin(lowDistance, highDistance) > 17.0f)
        return;
    const auto control = lowDistance <= highDistance ? 10 : 11;
    setValue(control, defaults[static_cast<size_t>(control)]);
    repaint();
}

void AlgorithmicReverbModuleView::paint(juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto fullArea = area;
    const auto filter = filterBounds(fullArea);
    constexpr std::array<const char*, 3> modeNames { "HALL", "CHAMBER", "PLATE" };
    const auto mode = megadsp::discreteIndex(value(4), 3);
    const auto size = lerp(0.25f, 2.0f, value(1));
    const auto decay = exponential(0.2f, 12.0f, value(0));
    const auto diffusion = value(6);
    const auto width = value(8) * 1.5f;
    const auto damping = value(9);
    const auto preDelay = value(5) * 250.0f;
    const auto decayRatios = megadsp::reverbDecayRatios(mode, damping);
    const std::array<float, 3> bandDecay {
        decay * decayRatios[0], decay, decay * decayRatios[1]
    };

    area.removeFromBottom(30.0f);
    auto reflectionArea = area.removeFromTop(area.getHeight() * 0.35f);
    area.removeFromTop(8.0f);
    graphics.setColour(juce::Colour(0xff8995a4));
    graphics.drawText(modeNames[static_cast<size_t>(mode)],
                      reflectionArea.toNearestInt(),
                      juce::Justification::topLeft);
    graphics.setColour(juce::Colours::white);
    graphics.drawText(
        "ROOM SCALE  "
            + megadsp::formatControlValue(type, 1, value(1)),
        reflectionArea.toNearestInt(),
        juce::Justification::centredTop);
    graphics.drawText("EARLY REFLECTIONS",
                      reflectionArea.toNearestInt(),
                      juce::Justification::topRight);
    for (int tap = 0; tap < 16; ++tap)
    {
        const auto time = preDelay
            + megadsp::reverbEarlyMilliseconds()
                  [static_cast<size_t>(mode)][static_cast<size_t>(tap)] * size;
        const auto x = reflectionArea.getX()
                       + juce::jlimit(0.0f, 1.0f, time / 400.0f)
                             * reflectionArea.getWidth();
        const auto pan = std::sin(
            static_cast<float>(tap * 5 + 2) * 1.71f);
        const auto spread = pan * width
                            * reflectionArea.getHeight() * 0.22f;
        const auto centre = reflectionArea.getCentreY();
        const auto scatteredAlpha =
            tap < 6 ? 0.95f : lerp(0.18f, 0.82f, diffusion);
        graphics.setColour(
            (tap % 2 == 0 ? accent : outputColour)
                .withAlpha(scatteredAlpha));
        graphics.drawLine(x, centre, x, centre + spread,
                          1.0f + diffusion * 1.5f);
        graphics.fillEllipse(x - 2.5f, centre + spread - 2.5f,
                             5.0f, 5.0f);
    }

    const std::array<juce::Colour, 3> bandColours {
        juce::Colour(0xff6f8cff), accent, juce::Colour(0xffffb15c)
    };
    for (int band = 0; band < 3; ++band)
    {
        juce::Path envelope;
        for (int pixel = 0; pixel < static_cast<int>(area.getWidth()); ++pixel)
        {
            const auto time = static_cast<float>(pixel)
                              / juce::jmax(1.0f, area.getWidth()) * 12.0f;
            const auto amplitude = std::pow(0.001f,
                time / juce::jmax(0.1f,
                    bandDecay[static_cast<size_t>(band)]));
            const auto baseline = area.getBottom()
                                  - static_cast<float>(band) * 6.0f;
            const auto y = baseline
                           - amplitude * area.getHeight()
                                 * (0.62f + diffusion * 0.25f);
            if (pixel == 0) envelope.startNewSubPath(area.getX(), y);
            else envelope.lineTo(area.getX() + static_cast<float>(pixel), y);
        }
        graphics.setColour(bandColours[static_cast<size_t>(band)].withAlpha(0.8f));
        graphics.strokePath(envelope, juce::PathStrokeType(2.0f));
    }
    graphics.setColour(juce::Colours::white);
    graphics.drawText(juce::String(decay, 2) + " s decay",
                      area.toNearestInt(), juce::Justification::topRight);
    graphics.drawText(
        "DRY " + megadsp::formatControlValue(type, 2, value(2))
            + "   WET "
            + megadsp::formatControlValue(type, 3, value(3)),
        area.toNearestInt(), juce::Justification::topLeft);
    const auto handle = spacePoint(fullArea);
    graphics.setColour(accent.withAlpha(0.8f));
    graphics.drawVerticalLine(juce::roundToInt(handle.x),
                            area.getY(), area.getBottom());
    graphics.drawHorizontalLine(
        juce::roundToInt(handle.y), area.getX(), area.getRight());
    graphics.setColour(juce::Colours::white);
    graphics.fillEllipse(
        juce::Rectangle<float>(12.0f, 12.0f).withCentre(handle));
    graphics.setColour(juce::Colour(0xff8995a4));
    graphics.drawText("Drag Decay / Room Scale",
                      area.toNearestInt(), juce::Justification::centredBottom);

    const auto frequencyX = [&filter](float frequency)
    {
        return filter.getX()
            + std::log(frequency / 20.0f) / std::log(1000.0f)
                  * filter.getWidth();
    };
    const auto lowX = frequencyX(
        exponential(20.0f, 1000.0f, value(10)));
    const auto highX = frequencyX(
        exponential(2000.0f, 20000.0f, value(11)));
    graphics.setColour(juce::Colour(0xff151b24));
    graphics.fillRoundedRectangle(filter, 5.0f);
    graphics.setColour(accent.withAlpha(0.30f));
    graphics.fillRoundedRectangle(
        juce::Rectangle<float>(
            lowX, filter.getY(), juce::jmax(3.0f, highX - lowX),
            filter.getHeight()),
        5.0f);
    graphics.setColour(juce::Colours::white);
    graphics.fillEllipse(
        lowX - 5.0f, filter.getCentreY() - 5.0f, 10.0f, 10.0f);
    graphics.fillEllipse(
        highX - 5.0f, filter.getCentreY() - 5.0f, 10.0f, 10.0f);
    graphics.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    graphics.drawText(
        "INPUT PASSBAND  "
            + megadsp::formatControlValue(type, 10, value(10))
            + " – "
            + megadsp::formatControlValue(type, 11, value(11)),
        filter.reduced(8.0f, 0.0f).toNearestInt(),
        juce::Justification::centred);
}
} // namespace

std::unique_ptr<ModuleView> createAlgorithmicReverbView(EffectGraph& graph)
{
    return std::make_unique<AlgorithmicReverbModuleView>(graph);
}
} // namespace megadsp::ui
