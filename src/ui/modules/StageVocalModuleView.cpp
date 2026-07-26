#include "../GraphStyle.h"
#include "../ModuleView.h"
#include "../../modules/StageVocal.h"

#include "ModuleViewCreators.h"

namespace megadsp::ui
{
namespace
{
class StageVocalModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    void configureAccessibility(juce::Component&) const override;
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
};

void StageVocalModuleView::configureAccessibility(
    juce::Component& component) const
{
    component.setTitle("Stage Vocal processing display");
    component.setHelpText(
        "Shows live input and output level with separate Expander, De-Ess, "
        "and Compressor gain reduction.");
}

void StageVocalModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    area = area.reduced(12.0f);
    auto header = area.removeFromTop(24.0f);
    graphics.setColour(juce::Colours::white.withAlpha(0.92f));
    graphics.setFont(13.0f);
    graphics.drawText(
        "LIVE VOCAL SIGNAL PATH", header,
        juce::Justification::centredLeft);

    ContinuousTelemetrySnapshot telemetry;
    const auto hasTelemetry = readContinuousTelemetry(telemetry)
        && telemetry.valueCount >= StageVocalModule::telemetryValueCount;
    const auto inputDb = hasTelemetry
        ? telemetry.values[StageVocalModule::inputLevelDb] : -100.0f;
    const auto outputDb = hasTelemetry
        ? telemetry.values[StageVocalModule::outputLevelDb] : -100.0f;
    const std::array<float, 3> reductions {
        hasTelemetry
            ? telemetry.values[StageVocalModule::expanderReductionDb] : 0.0f,
        hasTelemetry
            ? telemetry.values[StageVocalModule::deEssReductionDb] : 0.0f,
        hasTelemetry
            ? telemetry.values[StageVocalModule::compressorReductionDb] : 0.0f
    };
    constexpr std::array<const char*, 3> names {
        "EXPANDER", "DE-ESS", "COMPRESSOR"
    };

    auto levelArea = area.removeFromLeft(72.0f).reduced(8.0f, 4.0f);
    auto outputArea = area.removeFromRight(72.0f).reduced(8.0f, 4.0f);
    const auto drawLevel = [&graphics](
                               juce::Rectangle<float> bounds,
                               float db, const char* label)
    {
        graphics.setColour(juce::Colours::black.withAlpha(0.28f));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        auto fill = bounds.reduced(7.0f, 20.0f);
        const auto amount = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
        auto active = fill;
        active.removeFromTop(fill.getHeight() * (1.0f - amount));
        graphics.setColour(accent);
        graphics.fillRoundedRectangle(active, 3.0f);
        graphics.setColour(juce::Colours::white.withAlpha(0.62f));
        graphics.setFont(10.0f);
        graphics.drawText(
            label, bounds.removeFromBottom(18.0f),
            juce::Justification::centred);
    };
    drawLevel(levelArea, inputDb, "INPUT");
    drawLevel(outputArea, outputDb, "OUTPUT");

    const auto sectionWidth = area.getWidth() / 3.0f;
    for (int index = 0; index < 3; ++index)
    {
        auto section = juce::Rectangle<float>(
            area.getX() + sectionWidth * static_cast<float>(index),
            area.getY(), sectionWidth, area.getHeight()).reduced(8.0f);
        graphics.setColour(juce::Colours::black.withAlpha(0.24f));
        graphics.fillRoundedRectangle(section, 7.0f);
        graphics.setColour(juce::Colours::white.withAlpha(0.62f));
        graphics.setFont(10.0f);
        graphics.drawText(
            names[static_cast<size_t>(index)],
            section.removeFromTop(20.0f),
            juce::Justification::centred);
        const auto reduction = juce::jlimit(
            0.0f, 18.0f, reductions[static_cast<size_t>(index)]);
        auto meter = section.reduced(14.0f, 12.0f);
        const auto height = meter.getHeight() * reduction / 18.0f;
        auto active = meter.removeFromTop(height);
        graphics.setColour(
            index == 1 ? reductionColour.withAlpha(0.9f)
                       : accent.withAlpha(0.9f));
        graphics.fillRoundedRectangle(active, 4.0f);
        graphics.setColour(juce::Colours::white.withAlpha(0.92f));
        graphics.setFont(12.0f);
        graphics.drawText(
            juce::String(reduction, 1) + " dB",
            section.removeFromBottom(22.0f),
            juce::Justification::centred);
    }
}
} // namespace

std::unique_ptr<ModuleView> createStageVocalView(EffectGraph& graph)
{
    return std::make_unique<StageVocalModuleView>(graph);
}
} // namespace megadsp::ui
