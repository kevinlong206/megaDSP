#include "../GraphStyle.h"
#include "../ModuleView.h"

#include "ModuleViewCreators.h"

#include <array>
#include <cmath>

namespace megadsp::ui
{
namespace
{
class BitcrusherModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    bool usesFullPanel() const override { return true; }
    void configureAccessibility(juce::Component& component) const override
    {
        component.setTitle("Bitcrusher quantization display");
        component.setHelpText(
            "Choose a quantizer with the top pills. Drag the waveform field "
            "horizontally for Sample Rate and vertically for Bit Depth. Lower "
            "tracks edit dither, noise shaping, anti-aliasing, jitter, slew, "
            "grid bias, drive, mix, and output.");
    }
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    static juce::Rectangle<float> modePill(
        juce::Rectangle<float> area, int index)
    {
        auto row = area.removeFromTop(31.0f);
        const auto width = row.getWidth() * 0.25f;
        return juce::Rectangle<float>(
            row.getX() + width * static_cast<float>(index), row.getY(),
            width, row.getHeight()).reduced(4.0f, 2.0f);
    }
    static juce::Rectangle<float> fieldBounds(juce::Rectangle<float> area)
    {
        area.removeFromTop(34.0f);
        area.removeFromBottom(area.getHeight() * 0.47f);
        return area.reduced(5.0f);
    }
    static juce::Rectangle<float> trackBounds(
        juce::Rectangle<float> area, int control)
    {
        area.removeFromTop(34.0f);
        auto tracks = area.removeFromBottom(area.getHeight() * 0.47f);
        static constexpr std::array<int, 9> order {
            3, 4, 5, 6, 7, 8, 9, 10, 11
        };
        const auto iterator = std::find(
            order.begin(), order.end(), control);
        if (iterator == order.end())
            return {};
        const auto position =
            static_cast<int>(std::distance(order.begin(), iterator));
        const auto width = tracks.getWidth() / 3.0f;
        const auto height = tracks.getHeight() / 3.0f;
        return juce::Rectangle<float>(
            tracks.getX() + width * static_cast<float>(position % 3),
            tracks.getY() + height * static_cast<float>(position / 3),
            width, height).reduced(6.0f, 4.0f);
    }
    void chooseMode(int);
};

void BitcrusherModuleView::chooseMode(int index)
{
    if (auto* target = parameter(2))
    {
        graph.focusKeyboardControl(2);
        target->beginChangeGesture();
        target->setValueNotifyingHost(discreteValue(index, 4));
        target->endChangeGesture();
    }
    repaint();
}

void BitcrusherModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int index = 0; index < 4; ++index)
        if (modePill(area, index).contains(event.position))
        {
            chooseMode(index);
            return;
        }
    if (fieldBounds(area).contains(event.position))
    {
        dragPrimary = 1;
        dragSecondary = 0;
    }
    for (const auto control : { 3, 4, 5, 6, 7, 8, 9, 10, 11 })
        if (dragPrimary < 0
            && trackBounds(area, control).contains(event.position))
            dragPrimary = control;
    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void BitcrusherModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (dragPrimary == 1)
    {
        const auto bounds = fieldBounds(area);
        setValue(1, juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - bounds.getX()) / bounds.getWidth()));
        setValue(0, juce::jlimit(
            0.0f, 1.0f,
            (bounds.getBottom() - event.position.y) / bounds.getHeight()));
        dragReadout = "SAMPLE RATE  "
            + formatControlValue(type, 1, value(1))
            + "    BIT DEPTH  "
            + formatControlValue(type, 0, value(0));
        repaint();
        return;
    }
    const auto bounds = trackBounds(area, dragPrimary);
    setValue(dragPrimary, juce::jlimit(
        0.0f, 1.0f,
        (event.position.x - bounds.getX()) / bounds.getWidth()));
    updateDefaultDragReadout();
}

void BitcrusherModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int index = 0; index < 4; ++index)
        if (modePill(area, index).contains(event.position))
        {
            resetToDefault(2);
            return;
        }
    if (fieldBounds(area).contains(event.position))
    {
        resetToDefault(0);
        resetToDefault(1);
        return;
    }
    for (const auto control : { 3, 4, 5, 6, 7, 8, 9, 10, 11 })
        if (trackBounds(area, control).contains(event.position))
        {
            resetToDefault(control);
            return;
        }
}

void BitcrusherModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    static constexpr std::array<const char*, 4> modeNames {
        "BALANCED", "TRUNCATE", "FLOOR", "COMPAND"
    };
    const auto mode = discreteIndex(value(2), 4);
    for (int index = 0; index < 4; ++index)
    {
        const auto bounds = modePill(area, index);
        graphics.setColour(
            index == mode ? accent : juce::Colour(0xff27313d));
        graphics.fillRoundedRectangle(bounds, 7.0f);
        graphics.setColour(
            index == mode ? juce::Colour(0xff101820)
                          : juce::Colours::white);
        graphics.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        graphics.drawText(
            modeNames[static_cast<size_t>(index)],
            bounds.toNearestInt(), juce::Justification::centred);
    }

    const auto field = fieldBounds(area);
    graphics.setColour(juce::Colour(0xe6121821));
    graphics.fillRoundedRectangle(field, 8.0f);
    const auto bits = 2.0f + value(0) * 22.0f;
    const auto visibleSteps = juce::jlimit(
        2, 16, juce::roundToInt(std::exp2(bits - 1.0f)));
    graphics.setColour(juce::Colours::white.withAlpha(0.08f));
    for (int step = 1; step < visibleSteps; ++step)
    {
        const auto y = field.getY()
            + field.getHeight() * static_cast<float>(step)
                                  / static_cast<float>(visibleSteps);
        graphics.drawHorizontalLine(
            juce::roundToInt(y), field.getX(), field.getRight());
    }

    juce::Path inputPath;
    juce::Path outputPath;
    const auto pixels = juce::jmax(2, juce::roundToInt(field.getWidth()));
    for (int pixel = 0; pixel < pixels; ++pixel)
    {
        const auto proportion =
            static_cast<float>(pixel) / static_cast<float>(pixels - 1);
        const auto index = juce::jlimit(
            0, fftSize - 1,
            juce::roundToInt(proportion * static_cast<float>(fftSize - 1)));
        const auto x = field.getX() + proportion * field.getWidth();
        const auto inputY = field.getCentreY()
            - juce::jlimit(-1.0f, 1.0f,
                           inputSamples[static_cast<size_t>(index)])
                  * field.getHeight() * 0.40f;
        const auto outputY = field.getCentreY()
            - juce::jlimit(-1.0f, 1.0f,
                           outputSamples[static_cast<size_t>(index)])
                  * field.getHeight() * 0.40f;
        if (pixel == 0)
        {
            inputPath.startNewSubPath(x, inputY);
            outputPath.startNewSubPath(x, outputY);
        }
        else
        {
            inputPath.lineTo(x, inputY);
            outputPath.lineTo(x, outputY);
        }
    }
    graphics.setColour(inputColour.withAlpha(0.55f));
    graphics.strokePath(inputPath, juce::PathStrokeType(1.0f));
    graphics.setColour(outputColour.withAlpha(0.95f));
    graphics.strokePath(outputPath, juce::PathStrokeType(1.5f));

    graphics.setColour(juce::Colours::white);
    graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    graphics.drawText(
        formatControlValue(type, 0, value(0)) + "  |  "
            + formatControlValue(type, 1, value(1)),
        field.withHeight(22.0f).reduced(8.0f, 0.0f).toNearestInt(),
        juce::Justification::centredLeft);

    for (const auto control : { 3, 4, 5, 6, 7, 8, 9, 10, 11 })
    {
        const auto bounds = trackBounds(area, control);
        graphics.setColour(juce::Colour(0xff151b24));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        const auto bipolar = control == 8;
        const auto fill = value(control);
        graphics.setColour(
            (control >= 10 ? outputColour : accent).withAlpha(0.60f));
        if (bipolar)
        {
            const auto centre = bounds.getCentreX();
            const auto x = bounds.getX() + bounds.getWidth() * fill;
            graphics.fillRoundedRectangle(
                juce::Rectangle<float>(
                    juce::jmin(x, centre), bounds.getY(),
                    std::abs(x - centre), bounds.getHeight()),
                5.0f);
        }
        else
            graphics.fillRoundedRectangle(
                bounds.withWidth(bounds.getWidth() * fill), 5.0f);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(9.0f));
        graphics.drawFittedText(
            juce::String(controlMetadata(type, control).label) + "  "
                + formatControlValue(type, control, value(control)),
            bounds.reduced(5.0f, 0.0f).toNearestInt(),
            juce::Justification::centred, 1);
    }
}
} // namespace

std::unique_ptr<ModuleView> createBitcrusherView(EffectGraph& graph)
{
    return std::make_unique<BitcrusherModuleView>(graph);
}
} // namespace megadsp::ui
