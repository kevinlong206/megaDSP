#include "../GraphStyle.h"
#include "../ModuleView.h"
#include "../../modules/FrequencyLab.h"

#include "ModuleViewCreators.h"

#include <array>
#include <cmath>

namespace megadsp::ui
{
namespace
{
class FrequencyLabModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    bool usesFullPanel() const override { return true; }
    void configureAccessibility(juce::Component& component) const override
    {
        component.setTitle("Frequency Lab translation display");
        component.setHelpText(
            "Drag the spectrum horizontally to set Shift. Fine, feedback, "
            "modulation, stereo offset, wet filtering, mix, and output use the "
            "lower tracks. Spectra are measured histories; the result marker "
            "shows the current DSP-smoothed translation and LFO position.");
    }
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void timerCallback() override;

private:
    static juce::Rectangle<float> spectrumBounds(juce::Rectangle<float> area)
    {
        area.removeFromBottom(area.getHeight() * 0.42f);
        return area.reduced(5.0f);
    }
    static juce::Rectangle<float> trackBounds(
        juce::Rectangle<float> area, int control)
    {
        auto tracks = area.removeFromBottom(area.getHeight() * 0.42f);
        static constexpr std::array<int, 9> order {
            1, 2, 3, 4, 5, 6, 7, 8, 9
        };
        auto position = 0;
        while (position < static_cast<int>(order.size())
               && order[static_cast<size_t>(position)] != control)
            ++position;
        if (position == static_cast<int>(order.size()))
            return {};
        const auto width = tracks.getWidth() / 3.0f;
        const auto height = tracks.getHeight() / 3.0f;
        return juce::Rectangle<float>(
            tracks.getX() + width * static_cast<float>(position % 3),
            tracks.getY() + height * static_cast<float>(position / 3),
            width, height).reduced(6.0f, 4.0f);
    }
    static constexpr int historyFrames = 24;
    static constexpr int historyBands = 48;
    std::array<std::array<float, historyBands>, historyFrames> inputHistory {};
    std::array<std::array<float, historyBands>, historyFrames> outputHistory {};
    int historyPosition = 0;
};

void FrequencyLabModuleView::timerCallback()
{
    auto& inputFrame = inputHistory[static_cast<size_t>(historyPosition)];
    auto& outputFrame = outputHistory[static_cast<size_t>(historyPosition)];
    for (int band = 0; band < historyBands; ++band)
    {
        const auto normalized = static_cast<float>(band)
                                / static_cast<float>(historyBands - 1);
        const auto bin = juce::jlimit(
            0, fftSize / 2 - 1,
            juce::roundToInt(normalized * normalized
                             * static_cast<float>(fftSize / 2 - 1)));
        inputFrame[static_cast<size_t>(band)] = juce::jlimit(
            0.0f, 1.0f,
            (inputSpectrum[static_cast<size_t>(bin)] + 90.0f) / 90.0f);
        outputFrame[static_cast<size_t>(band)] = juce::jlimit(
            0.0f, 1.0f,
            (outputSpectrum[static_cast<size_t>(bin)] + 90.0f) / 90.0f);
    }
    historyPosition = (historyPosition + 1) % historyFrames;
}

void FrequencyLabModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (spectrumBounds(area).contains(event.position))
        dragPrimary = 0;
    for (const auto control : { 1, 2, 3, 4, 5, 6, 7, 8, 9 })
        if (dragPrimary < 0 && trackBounds(area, control).contains(event.position))
            dragPrimary = control;
    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void FrequencyLabModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto bounds = dragPrimary == 0
        ? spectrumBounds(area) : trackBounds(area, dragPrimary);
    setValue(dragPrimary, juce::jlimit(
        0.0f, 1.0f,
        (event.position.x - bounds.getX()) / bounds.getWidth()));
    if (dragPrimary == 0)
        dragReadout = "RESULT  "
            + juce::String((value(0) * 2.0f - 1.0f) * 5000.0f
                               + (value(1) * 2.0f - 1.0f) * 50.0f,
                           1)
            + " Hz";
    else
        updateDefaultDragReadout();
}

void FrequencyLabModuleView::mouseDoubleClick(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (spectrumBounds(area).contains(event.position))
    {
        resetToDefault(0);
        return;
    }
    for (const auto control : { 1, 2, 3, 4, 5, 6, 7, 8, 9 })
        if (trackBounds(area, control).contains(event.position))
        {
            resetToDefault(control);
            return;
        }
}

void FrequencyLabModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto field = spectrumBounds(area);
    graphics.setColour(juce::Colour(0xe6121821));
    graphics.fillRoundedRectangle(field, 8.0f);
    const auto halfHeight = field.getHeight() * 0.5f;
    const auto cellWidth = field.getWidth() / static_cast<float>(historyBands);
    const auto cellHeight = halfHeight / static_cast<float>(historyFrames);
    for (int age = 0; age < historyFrames; ++age)
    {
        const auto frame = (historyPosition + age) % historyFrames;
        for (int band = 0; band < historyBands; ++band)
        {
            const auto x = field.getX() + static_cast<float>(band) * cellWidth;
            const auto y = field.getY() + static_cast<float>(age) * cellHeight;
            const auto inputLevel = inputHistory[static_cast<size_t>(frame)]
                                                [static_cast<size_t>(band)];
            const auto outputLevel = outputHistory[static_cast<size_t>(frame)]
                                                  [static_cast<size_t>(band)];
            graphics.setColour(inputColour.withAlpha(inputLevel * 0.34f));
            graphics.fillRect(x, y, cellWidth + 0.5f, cellHeight + 0.5f);
            graphics.setColour(outputColour.withAlpha(outputLevel * 0.38f));
            graphics.fillRect(x, y + halfHeight,
                              cellWidth + 0.5f, cellHeight + 0.5f);
        }
    }
    drawSpectrum(graphics, field.reduced(8.0f), inputSpectrum,
                 inputColour.withAlpha(0.72f));
    drawSpectrum(graphics, field.reduced(8.0f), outputSpectrum,
                 outputColour.withAlpha(0.84f));

    ContinuousTelemetrySnapshot telemetry;
    const auto hasTelemetry =
        readContinuousTelemetry(telemetry)
        && telemetry.sequence != 0
        && telemetry.valueCount
               >= FrequencyLabModule::telemetryValueCount;
    const auto shiftHz = hasTelemetry
        ? telemetry.values[FrequencyLabModule::commonShiftHz]
        : (value(0) * 2.0f - 1.0f) * 5000.0f
              + (value(1) * 2.0f - 1.0f) * 50.0f;
    const auto leftShiftHz = hasTelemetry
        ? telemetry.values[FrequencyLabModule::leftShiftHz] : shiftHz;
    const auto rightShiftHz = hasTelemetry
        ? telemetry.values[FrequencyLabModule::rightShiftHz] : shiftHz;
    const auto lfoPosition = hasTelemetry
        ? telemetry.values[FrequencyLabModule::lfoPosition] : 0.0f;
    const auto sourceX = field.getCentreX();
    const auto destinationX = juce::jlimit(
        field.getX(), field.getRight(),
        sourceX + shiftHz / 10000.0f * field.getWidth());
    const auto modulationDepthHz = hasTelemetry
        ? telemetry.values[FrequencyLabModule::modulationDepthHz]
        : value(4) * 2000.0f;
    const auto lfoWidth =
        modulationDepthHz / 10000.0f * field.getWidth();
    const auto leftX = juce::jlimit(
        field.getX(), field.getRight(),
        sourceX + leftShiftHz / 10000.0f * field.getWidth());
    const auto rightX = juce::jlimit(
        field.getX(), field.getRight(),
        sourceX + rightShiftHz / 10000.0f * field.getWidth());
    graphics.setColour(accent.withAlpha(0.16f));
    graphics.fillRect(juce::Rectangle<float>(
        destinationX - lfoWidth, field.getY() + 5.0f,
        lfoWidth * 2.0f, field.getHeight() - 10.0f));
    graphics.setColour(outputColour.withAlpha(0.35f));
    graphics.drawVerticalLine(juce::roundToInt(leftX),
                              field.getY(), field.getBottom());
    graphics.drawVerticalLine(juce::roundToInt(rightX),
                              field.getY(), field.getBottom());
    graphics.setColour(accent);
    graphics.drawArrow({ sourceX, field.getBottom() - 18.0f,
                         destinationX, field.getBottom() - 18.0f },
                       2.0f, 8.0f, 7.0f);
    graphics.setColour(juce::Colours::white);
    graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    graphics.drawText(
        (hasTelemetry ? "DSP RESULT  " : "CONTROL TARGET  ")
            + juce::String(shiftHz, 1) + " Hz"
            + (hasTelemetry
                   ? "  ·  LFO " + juce::String(lfoPosition, 2)
                   : "  ·  DSP TELEMETRY WAITING"),
        field.withHeight(24.0f).reduced(8.0f, 0.0f).toNearestInt(),
        juce::Justification::centredLeft);

    for (const auto control : { 1, 2, 3, 4, 5, 6, 7, 8, 9 })
    {
        const auto bounds = trackBounds(area, control);
        graphics.setColour(juce::Colour(0xff151b24));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour((control == 8 || control == 9 ? outputColour : accent)
                               .withAlpha(0.58f));
        graphics.fillRoundedRectangle(
            bounds.withWidth(bounds.getWidth() * value(control)), 5.0f);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(9.5f));
        graphics.drawFittedText(
            juce::String(controlMetadata(type, control).label) + "  "
                + formatControlValue(type, control, value(control)),
            bounds.reduced(6.0f, 0.0f).toNearestInt(),
            juce::Justification::centred, 1);
    }
}
} // namespace

std::unique_ptr<ModuleView> createFrequencyLabView(EffectGraph& graph)
{
    return std::make_unique<FrequencyLabModuleView>(graph);
}
} // namespace megadsp::ui
