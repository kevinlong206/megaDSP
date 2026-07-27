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
float selectorRowsHeight(juce::Rectangle<float> area)
{
    return juce::jlimit(66.0f, 83.0f, area.getHeight() * 0.33f);
}

class FrequencyLabModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    bool usesFullPanel() const override { return true; }
    void configureAccessibility(juce::Component& component) const override
    {
        component.setTitle("Harmonic Resonator chord field");
        component.setHelpText(
            "Choose a root note, harmony, and register with the top pills. "
            "Decay and Tone shape the chord resonance; Motion, Stereo Spread, "
            "Low Cut, High Cut, Mix, and Output use the lower tracks. "
            "Double-click any control to restore its musical default.");
    }
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    static juce::Rectangle<float> rootBounds(
        juce::Rectangle<float>, int index);
    static juce::Rectangle<float> harmonyBounds(
        juce::Rectangle<float>, int index);
    static juce::Rectangle<float> registerBounds(
        juce::Rectangle<float>, int index);
    static juce::Rectangle<float> fieldBounds(juce::Rectangle<float>);
    static juce::Rectangle<float> trackBounds(
        juce::Rectangle<float>, int control);
};

juce::Rectangle<float> FrequencyLabModuleView::rootBounds(
    juce::Rectangle<float> area, int index)
{
    auto row = area.removeFromTop(selectorRowsHeight(area) / 3.0f);
    const auto width = row.getWidth()
                       / static_cast<float>(FrequencyLabModule::rootCount);
    return juce::Rectangle<float>(
        row.getX() + width * static_cast<float>(index), row.getY(),
        width, row.getHeight()).reduced(1.5f, 2.0f);
}

juce::Rectangle<float> FrequencyLabModuleView::harmonyBounds(
    juce::Rectangle<float> area, int index)
{
    const auto rowHeight = selectorRowsHeight(area) / 3.0f;
    area.removeFromTop(rowHeight);
    auto row = area.removeFromTop(rowHeight);
    const auto width = row.getWidth()
                       / static_cast<float>(FrequencyLabModule::harmonyCount);
    return juce::Rectangle<float>(
        row.getX() + width * static_cast<float>(index), row.getY(),
        width, row.getHeight()).reduced(2.0f, 2.0f);
}

juce::Rectangle<float> FrequencyLabModuleView::registerBounds(
    juce::Rectangle<float> area, int index)
{
    const auto rowHeight = selectorRowsHeight(area) / 3.0f;
    area.removeFromTop(rowHeight * 2.0f);
    auto row = area.removeFromTop(rowHeight);
    const auto width = row.getWidth()
                       / static_cast<float>(FrequencyLabModule::registerCount);
    return juce::Rectangle<float>(
        row.getX() + width * static_cast<float>(index), row.getY(),
        width, row.getHeight()).reduced(3.0f, 2.0f);
}

juce::Rectangle<float> FrequencyLabModuleView::fieldBounds(
    juce::Rectangle<float> area)
{
    area.removeFromTop(selectorRowsHeight(area));
    area.removeFromBottom(area.getHeight() * 0.43f);
    return area.reduced(5.0f, 3.0f);
}

juce::Rectangle<float> FrequencyLabModuleView::trackBounds(
    juce::Rectangle<float> area, int control)
{
    static constexpr std::array<int, 9> order {
        3, 4, 5, 6, 7, 8, 9, 10, 11
    };
    auto position = 0;
    while (position < static_cast<int>(order.size())
           && order[static_cast<size_t>(position)] != control)
        ++position;
    if (position == static_cast<int>(order.size()))
        return {};
    auto tracks = area.removeFromBottom(area.getHeight() * 0.43f);
    const auto width = tracks.getWidth() / 3.0f;
    const auto height = tracks.getHeight() / 3.0f;
    return juce::Rectangle<float>(
        tracks.getX() + width * static_cast<float>(position % 3),
        tracks.getY() + height * static_cast<float>(position / 3),
        width, height).reduced(6.0f, 2.0f);
}

void FrequencyLabModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int index = 0; index < FrequencyLabModule::rootCount; ++index)
        if (rootBounds(area, index).contains(event.position))
        {
            setValue(0, discreteValue(index, FrequencyLabModule::rootCount));
            return;
        }
    for (int index = 0; index < FrequencyLabModule::harmonyCount; ++index)
        if (harmonyBounds(area, index).contains(event.position))
        {
            setValue(
                1, discreteValue(index, FrequencyLabModule::harmonyCount));
            return;
        }
    for (int index = 0; index < FrequencyLabModule::registerCount; ++index)
        if (registerBounds(area, index).contains(event.position))
        {
            setValue(
                2, discreteValue(index, FrequencyLabModule::registerCount));
            return;
        }
    for (const auto control : { 3, 4, 5, 6, 7, 8, 9, 10, 11 })
        if (trackBounds(area, control).contains(event.position))
        {
            dragPrimary = control;
            break;
        }
    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void FrequencyLabModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 3)
        return;
    const auto track = trackBounds(
        getLocalBounds().toFloat().reduced(12.0f), dragPrimary);
    setValue(dragPrimary, juce::jlimit(
        0.0f, 1.0f,
        (event.position.x - track.getX()) / track.getWidth()));
    updateDefaultDragReadout();
}

void FrequencyLabModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int index = 0; index < FrequencyLabModule::rootCount; ++index)
        if (rootBounds(area, index).contains(event.position))
        {
            resetToDefault(0);
            return;
        }
    for (int index = 0; index < FrequencyLabModule::harmonyCount; ++index)
        if (harmonyBounds(area, index).contains(event.position))
        {
            resetToDefault(1);
            return;
        }
    for (int index = 0; index < FrequencyLabModule::registerCount; ++index)
        if (registerBounds(area, index).contains(event.position))
        {
            resetToDefault(2);
            return;
        }
    for (const auto control : { 3, 4, 5, 6, 7, 8, 9, 10, 11 })
        if (trackBounds(area, control).contains(event.position))
        {
            resetToDefault(control);
            return;
        }
}

void FrequencyLabModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto root = discreteIndex(value(0), FrequencyLabModule::rootCount);
    const auto harmony =
        discreteIndex(value(1), FrequencyLabModule::harmonyCount);
    const auto registerIndex =
        discreteIndex(value(2), FrequencyLabModule::registerCount);
    for (int index = 0; index < FrequencyLabModule::rootCount; ++index)
    {
        const auto bounds = rootBounds(area, index);
        graphics.setColour(index == root
            ? accent.withAlpha(0.88f) : juce::Colour(0xff27313d));
        graphics.fillRoundedRectangle(bounds, 4.0f);
        graphics.setColour(index == root
            ? juce::Colours::black : juce::Colours::white.withAlpha(0.76f));
        graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        graphics.drawText(
            formatControlValue(
                type, 0, discreteValue(index, FrequencyLabModule::rootCount)),
            bounds.toNearestInt(), juce::Justification::centred);
    }
    for (int index = 0; index < FrequencyLabModule::harmonyCount; ++index)
    {
        const auto bounds = harmonyBounds(area, index);
        graphics.setColour(index == harmony
            ? outputColour.withAlpha(0.68f) : juce::Colour(0xff27313d));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour(juce::Colours::white.withAlpha(
            index == harmony ? 0.96f : 0.68f));
        graphics.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        graphics.drawFittedText(
            formatControlValue(
                type, 1,
                discreteValue(index, FrequencyLabModule::harmonyCount)),
            bounds.toNearestInt(), juce::Justification::centred, 1);
    }
    for (int index = 0; index < FrequencyLabModule::registerCount; ++index)
    {
        const auto bounds = registerBounds(area, index);
        graphics.setColour(index == registerIndex
            ? accent.withAlpha(0.48f) : juce::Colour(0xff202935));
        graphics.fillRoundedRectangle(bounds, 4.0f);
        graphics.setColour(juce::Colours::white.withAlpha(0.80f));
        graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        graphics.drawText(
            formatControlValue(
                type, 2,
                discreteValue(index, FrequencyLabModule::registerCount)),
            bounds.toNearestInt(), juce::Justification::centred);
    }

    const auto field = fieldBounds(area);
    graphics.setColour(juce::Colour(0xe6121821));
    graphics.fillRoundedRectangle(field, 8.0f);
    ContinuousTelemetrySnapshot telemetry;
    const auto hasTelemetry =
        readContinuousTelemetry(telemetry)
        && telemetry.sequence != 0
        && telemetry.valueCount >= FrequencyLabModule::telemetryValueCount;
    const auto motion = hasTelemetry
        ? telemetry.values[FrequencyLabModule::motionPosition] : 0.0f;
    const auto envelope = hasTelemetry
        ? juce::jlimit(
              0.0f, 1.0f,
              telemetry.values[FrequencyLabModule::wetEnvelope] * 2.0f)
        : 0.0f;
    for (int voice = 0; voice < FrequencyLabModule::resonatorCount; ++voice)
    {
        const auto position = static_cast<float>(voice + 1)
                              / static_cast<float>(
                                  FrequencyLabModule::resonatorCount + 1);
        const auto x = field.getX() + field.getWidth() * position;
        const auto pan = position * 2.0f - 1.0f;
        const auto y = field.getCentreY()
            + pan * field.getHeight() * 0.22f
            + motion * field.getHeight() * 0.04f;
        graphics.setColour(accent.withAlpha(0.22f + envelope * 0.38f));
        graphics.drawVerticalLine(
            juce::roundToInt(x), field.getY() + 25.0f,
            field.getBottom() - 18.0f);
        graphics.setColour(outputColour.withAlpha(0.72f + envelope * 0.25f));
        graphics.fillEllipse(x - 5.0f, y - 5.0f, 10.0f, 10.0f);
        if (hasTelemetry)
        {
            const auto frequency = telemetry.values[
                static_cast<size_t>(FrequencyLabModule::frequency0 + voice)];
            graphics.setColour(juce::Colours::white.withAlpha(0.68f));
            graphics.setFont(juce::FontOptions(8.5f));
            graphics.drawText(
                frequency >= 1000.0f
                    ? juce::String(frequency * 0.001f, 1) + "k"
                    : juce::String(frequency, 0),
                juce::Rectangle<float>(
                    x - 18.0f, field.getBottom() - 18.0f, 36.0f, 14.0f)
                    .toNearestInt(),
                juce::Justification::centred);
        }
    }
    graphics.setColour(juce::Colours::white.withAlpha(0.90f));
    graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    graphics.drawFittedText(
        "TUNED FIELD  |  " + formatControlValue(type, 0, value(0))
            + " " + formatControlValue(type, 1, value(1))
            + "  |  " + formatControlValue(type, 2, value(2))
            + (hasTelemetry
                   ? "  |  MOTION "
                         + juce::String(
                             telemetry.values[FrequencyLabModule::motionCents],
                             1)
                         + " cents"
                   : "  |  DSP TELEMETRY WAITING"),
        field.withHeight(24.0f).reduced(8.0f, 0.0f).toNearestInt(),
        juce::Justification::centredLeft, 1);

    for (const auto control : { 3, 4, 5, 6, 7, 8, 9, 10, 11 })
    {
        const auto bounds = trackBounds(area, control);
        graphics.setColour(juce::Colour(0xff151b24));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour((control >= 10 ? outputColour : accent)
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
