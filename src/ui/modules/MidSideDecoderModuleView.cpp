#include "../GraphStyle.h"
#include "../GuiLayout.h"
#include "../ModuleView.h"

#include "ModuleViewCreators.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace megadsp::ui
{
namespace
{
class MidSideDecoderModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    void configureAccessibility(juce::Component& component) const override
    {
        component.setTitle("M/S Decoder output balance");
        component.setHelpText(
            "Shows measured decoded left and right output levels, peak "
            "markers, and the current energy balance. Width, channel swap, "
            "and side mute remain in the controls below.");
    }
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void timerCallback() override;

private:
    static float levelX(float db, juce::Rectangle<float> area)
    {
        return juce::jmap(
            juce::jlimit(-60.0f, 6.0f, db), -60.0f, 6.0f,
            area.getX(), area.getRight());
    }

    float displayedLeftDb = -60.0f;
    float displayedRightDb = -60.0f;
    float displayedBalance = 0.0f;
    float heldLeftPeakDb = -60.0f;
    float heldRightPeakDb = -60.0f;
    int leftPeakHold = 0;
    int rightPeakHold = 0;
    bool hasMeasurement = false;
};

void MidSideDecoderModuleView::timerCallback()
{
    constexpr auto sampleCount = fftSize / 2;
    const auto measurement = measureStereoBalance(
        stereoLeftSamples.data() + fftSize - sampleCount,
        stereoRightSamples.data() + fftSize - sampleCount,
        sampleCount);
    const auto smooth = [](float current, float target)
    {
        const auto coefficient = target > current ? 0.48f : 0.14f;
        return current + (target - current) * coefficient;
    };
    displayedLeftDb = smooth(displayedLeftDb, measurement.leftRmsDb);
    displayedRightDb = smooth(displayedRightDb, measurement.rightRmsDb);
    displayedBalance +=
        (measurement.balance - displayedBalance) * 0.28f;

    const auto updatePeak = [](float measured, float& held, int& hold)
    {
        if (measured >= held)
        {
            held = measured;
            hold = 24;
        }
        else if (hold > 0)
            --hold;
        else
            held = juce::jmax(-60.0f, held - 0.8f);
    };
    updatePeak(measurement.leftPeakDb, heldLeftPeakDb, leftPeakHold);
    updatePeak(measurement.rightPeakDb, heldRightPeakDb, rightPeakHold);
    hasMeasurement = measurement.leftPeakDb > -59.9f
        || measurement.rightPeakDb > -59.9f;
}

void MidSideDecoderModuleView::paint(juce::Graphics& graphics,
                        juce::Rectangle<float> area)
{
    auto header = area.removeFromTop(16.0f);
    auto footer = area.removeFromBottom(16.0f);

    graphics.setColour(juce::Colour(0xffa8b3c0));
    graphics.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    graphics.drawText("MID / SIDE IN",
                      header.toNearestInt(),
                      juce::Justification::centredLeft);
    graphics.drawText(
        "L / R OUTPUT BALANCE", header.toNearestInt(),
        juce::Justification::centredRight);

    auto balanceArea = area.removeFromBottom(25.0f).reduced(4.0f, 2.0f);
    auto meters = area.reduced(4.0f, 1.0f);
    const auto rowHeight = meters.getHeight() * 0.5f;
    const std::array<float, 2> levels {
        displayedLeftDb, displayedRightDb
    };
    const std::array<float, 2> peaks {
        heldLeftPeakDb, heldRightPeakDb
    };
    const std::array<const char*, 2> labels { "L", "R" };
    for (int channel = 0; channel < 2; ++channel)
    {
        auto row = juce::Rectangle<float>(
            meters.getX(),
            meters.getY() + rowHeight * static_cast<float>(channel),
            meters.getWidth(), rowHeight).reduced(0.0f, 2.0f);
        auto label = row.removeFromLeft(22.0f);
        auto readout = row.removeFromRight(58.0f);
        auto meter = row.reduced(3.0f, 1.0f);
        graphics.setColour(juce::Colour(0xff111820));
        graphics.fillRoundedRectangle(meter, 4.0f);
        for (const auto marker : { -48.0f, -24.0f, -12.0f, 0.0f })
        {
            graphics.setColour(juce::Colours::white.withAlpha(0.10f));
            graphics.drawVerticalLine(
                juce::roundToInt(levelX(marker, meter)),
                meter.getY(), meter.getBottom());
        }
        const auto levelRight = levelX(levels[static_cast<size_t>(channel)],
                                       meter);
        graphics.setColour(
            (channel == 0 ? accent : outputColour).withAlpha(0.72f));
        graphics.fillRoundedRectangle(
            meter.withRight(juce::jmax(meter.getX(), levelRight)), 4.0f);
        const auto peakX =
            levelX(peaks[static_cast<size_t>(channel)], meter);
        graphics.setColour(juce::Colours::white.withAlpha(0.88f));
        graphics.drawVerticalLine(
            juce::roundToInt(peakX), meter.getY(), meter.getBottom());
        graphics.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        graphics.drawText(
            labels[static_cast<size_t>(channel)], label.toNearestInt(),
            juce::Justification::centred);
        graphics.setFont(juce::FontOptions(9.0f));
        graphics.drawText(
            hasMeasurement
                ? juce::String(levels[static_cast<size_t>(channel)], 1)
                      + " dB"
                : juce::String("-inf"),
            readout.toNearestInt(), juce::Justification::centredRight);
    }

    const auto centreX = balanceArea.getCentreX();
    const auto railY = balanceArea.getCentreY() + 2.0f;
    const auto deadband = 0.03f;
    const auto visibleBalance =
        std::abs(displayedBalance) < deadband ? 0.0f : displayedBalance;
    const auto indicatorX = balanceArea.getX()
        + (visibleBalance + 1.0f) * 0.5f * balanceArea.getWidth();
    graphics.setColour(juce::Colour(0xff303a46));
    graphics.drawLine(
        balanceArea.getX(), railY, balanceArea.getRight(), railY, 3.0f);
    graphics.setColour(juce::Colours::white.withAlpha(0.35f));
    graphics.drawVerticalLine(
        juce::roundToInt(centreX), railY - 6.0f, railY + 6.0f);
    graphics.setColour(
        visibleBalance < 0.0f ? accent
                              : visibleBalance > 0.0f ? outputColour
                                                      : juce::Colours::white);
    graphics.fillEllipse(indicatorX - 5.0f, railY - 5.0f, 10.0f, 10.0f);
    const auto amount =
        juce::roundToInt(std::abs(visibleBalance) * 100.0f);
    const auto balanceText = amount == 0
        ? juce::String("CENTER")
        : juce::String(amount) + "% "
              + (visibleBalance < 0.0f ? "L" : "R");
    graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    graphics.drawText(
        balanceText, balanceArea.toNearestInt(),
        juce::Justification::centredTop);
    graphics.setColour(juce::Colours::white.withAlpha(0.62f));
    graphics.drawText(
        "L", balanceArea.toNearestInt(),
        juce::Justification::centredLeft);
    graphics.drawText(
        "R", balanceArea.toNearestInt(),
        juce::Justification::centredRight);

    const auto widthText = "WIDTH "
        + megadsp::formatControlValue(
            megadsp::ModuleType::midSideDecoder, 0, value(0));
    graphics.setColour(accent);
    graphics.drawText(widthText, footer.toNearestInt(),
                      juce::Justification::centredRight);
    juce::String routing;
    if (value(2) >= 0.5f) routing << "SIDES MUTED  ";
    if (value(1) >= 0.5f) routing << "CHANNELS SWAPPED";
    if (routing.isNotEmpty())
    {
        graphics.setColour(reductionColour);
        graphics.drawText(routing.trimEnd(), footer.toNearestInt(),
                          juce::Justification::centredLeft);
    }
}
} // namespace

std::unique_ptr<ModuleView> createMidSideDecoderView(EffectGraph& graph)
{
    return std::make_unique<MidSideDecoderModuleView>(graph);
}
} // namespace megadsp::ui
