#include "../GraphStyle.h"
#include "../ModuleView.h"
#include "../../modules/DiffusionDelay.h"

#include "ModuleViewCreators.h"

#include <array>
#include <cmath>

namespace megadsp::ui
{
namespace
{
class DiffusionDelayModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    bool usesFullPanel() const override { return true; }
    void configureAccessibility(juce::Component& component) const override
    {
        component.setTitle("Diffusion Delay echo cloud");
        component.setHelpText(
            "Drag the cloud field horizontally for Time or synced Division "
            "and vertically for Diffusion. Sync and note values are explicit "
            "pills. Live repeat and cloud marks come from the selected slot's "
            "processed audio; their travel, level, and vertical placement show "
            "actual DSP progress, energy, and stereo position. Lower tracks "
            "edit feedback, movement, passband, width, ducking, mix, and "
            "output. Double-click a region to reset it.");
    }
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    static juce::Rectangle<float> syncBounds(juce::Rectangle<float> area)
    {
        return area.removeFromTop(28.0f).removeFromRight(92.0f)
            .reduced(4.0f, 2.0f);
    }
    static juce::Rectangle<float> divisionBounds(
        juce::Rectangle<float> area, int index)
    {
        area.removeFromTop(30.0f);
        auto row = area.removeFromTop(28.0f);
        const auto width = row.getWidth() / 8.0f;
        return juce::Rectangle<float>(
            row.getX() + width * static_cast<float>(index),
            row.getY(), width, row.getHeight()).reduced(2.0f, 1.0f);
    }
    static juce::Rectangle<float> cloudBounds(juce::Rectangle<float> area)
    {
        area.removeFromTop(57.0f);
        area.removeFromBottom(area.getHeight() * 0.43f);
        return area.reduced(5.0f);
    }
    static juce::Rectangle<float> trackBounds(
        juce::Rectangle<float> area, int control)
    {
        area.removeFromTop(57.0f);
        auto tracks = area.removeFromBottom(area.getHeight() * 0.43f);
        static constexpr std::array<int, 8> order {
            3, 5, 6, 7, 8, 9, 10, 11
        };
        auto position = 0;
        while (position < static_cast<int>(order.size())
               && order[static_cast<size_t>(position)] != control)
            ++position;
        if (position == static_cast<int>(order.size()))
            return {};
        const auto width = tracks.getWidth() * 0.25f;
        const auto height = tracks.getHeight() * 0.5f;
        return juce::Rectangle<float>(
            tracks.getX() + width * static_cast<float>(position % 4),
            tracks.getY() + height * static_cast<float>(position / 4),
            width, height).reduced(6.0f, 4.0f);
    }
    void chooseDivision(int index);
};

void DiffusionDelayModuleView::chooseDivision(int index)
{
    if (auto* target = parameter(2))
    {
        graph.focusKeyboardControl(2);
        target->beginChangeGesture();
        target->setValueNotifyingHost(discreteValue(index, 8));
        target->endChangeGesture();
    }
    repaint();
}

void DiffusionDelayModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (syncBounds(area).contains(event.position))
    {
        toggleParameter(1);
        return;
    }
    if (value(1) >= 0.5f)
        for (int index = 0; index < 8; ++index)
            if (divisionBounds(area, index).contains(event.position))
            {
                chooseDivision(index);
                return;
            }
    if (cloudBounds(area).contains(event.position))
    {
        dragPrimary = value(1) >= 0.5f ? 2 : 0;
        dragSecondary = 4;
    }
    for (const auto control : { 3, 5, 6, 7, 8, 9, 10, 11 })
        if (dragPrimary < 0
            && trackBounds(area, control).contains(event.position))
            dragPrimary = control;
    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void DiffusionDelayModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (dragSecondary == 4)
    {
        const auto bounds = cloudBounds(area);
        setValue(dragPrimary, juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - bounds.getX()) / bounds.getWidth()));
        setValue(4, juce::jlimit(
            0.0f, 1.0f,
            (bounds.getBottom() - event.position.y) / bounds.getHeight()));
        dragReadout = juce::String(value(1) >= 0.5f ? "DIVISION  " : "TIME  ")
            + formatControlValue(type, dragPrimary, value(dragPrimary))
            + "    DIFFUSION  " + formatControlValue(type, 4, value(4));
        repaint();
        return;
    }
    const auto bounds = trackBounds(area, dragPrimary);
    setValue(dragPrimary, juce::jlimit(
        0.0f, 1.0f,
        (event.position.x - bounds.getX()) / bounds.getWidth()));
    updateDefaultDragReadout();
}

void DiffusionDelayModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (syncBounds(area).contains(event.position))
    {
        resetToDefault(1);
        return;
    }
    if (cloudBounds(area).contains(event.position))
    {
        resetToDefault(value(1) >= 0.5f ? 2 : 0);
        resetToDefault(4);
        return;
    }
    for (const auto control : { 3, 5, 6, 7, 8, 9, 10, 11 })
        if (trackBounds(area, control).contains(event.position))
        {
            resetToDefault(control);
            return;
        }
}

void DiffusionDelayModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto fullArea = area;
    auto title = area.removeFromTop(28.0f);
    graphics.setColour(accent);
    graphics.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    graphics.drawText("PRIMARY ECHO → DIFFUSION CLOUD",
                      title.reduced(7.0f, 0.0f).toNearestInt(),
                      juce::Justification::centredLeft);
    const auto sync = syncBounds(fullArea);
    graphics.setColour(value(1) >= 0.5f
                           ? accent : juce::Colour(0xff27313d));
    graphics.fillRoundedRectangle(sync, 6.0f);
    graphics.setColour(value(1) >= 0.5f
                           ? juce::Colour(0xff101820)
                           : juce::Colours::white);
    graphics.drawText(value(1) >= 0.5f ? "SYNCED" : "FREE",
                      sync.toNearestInt(), juce::Justification::centred);

    static constexpr std::array<const char*, 8> divisions {
        "1/32", "1/16", "1/16.", "1/8", "1/8.", "1/4", "1/4.", "1/2"
    };
    const auto selectedDivision = discreteIndex(value(2), 8);
    for (int index = 0; index < 8; ++index)
    {
        const auto bounds = divisionBounds(fullArea, index);
        const auto selected = value(1) >= 0.5f && index == selectedDivision;
        graphics.setColour(selected ? outputColour
                                    : juce::Colour(0xff222c38));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour(selected ? juce::Colour(0xff101820)
                                    : juce::Colours::white.withAlpha(
                                          value(1) >= 0.5f ? 0.82f : 0.30f));
        graphics.setFont(juce::FontOptions(8.5f, juce::Font::bold));
        graphics.drawText(divisions[static_cast<size_t>(index)],
                          bounds.toNearestInt(),
                          juce::Justification::centred);
    }

    const auto cloud = cloudBounds(fullArea);
    graphics.setColour(juce::Colour(0xe6121821));
    graphics.fillRoundedRectangle(cloud, 8.0f);
    const auto timing = value(1) >= 0.5f ? value(2) : value(0);
    const auto primaryX = cloud.getX()
        + juce::jmap(timing, 0.0f, 1.0f, 0.12f, 0.42f) * cloud.getWidth();
    const auto centreY = cloud.getCentreY();
    graphics.setColour(juce::Colours::white.withAlpha(0.15f));
    graphics.drawHorizontalLine(juce::roundToInt(centreY),
                                cloud.getX() + 8.0f,
                                cloud.getRight() - 8.0f);
    graphics.setColour(juce::Colours::white.withAlpha(0.75f));
    graphics.drawVerticalLine(juce::roundToInt(cloud.getX() + 12.0f),
                              centreY - 18.0f, centreY + 18.0f);
    EventTelemetrySnapshot telemetry;
    const auto hasTelemetry =
        readEventTelemetry(telemetry) && telemetry.sequence != 0;
    if (hasTelemetry)
    {
        for (std::uint32_t index = 0; index < telemetry.eventCount; ++index)
        {
            const auto& event = telemetry.events[index];
            const auto progress = juce::jlimit(0.0f, 1.0f, event.progress);
            const auto x = juce::jmap(
                progress, cloud.getX() + 12.0f, cloud.getRight() - 12.0f);
            const auto y = centreY
                + event.position[0] * cloud.getHeight() * 0.30f;
            const auto energy = event.values[static_cast<size_t>(
                DiffusionDelayTelemetryValue::energy)];
            const auto level = juce::jlimit(
                0.0f, 1.0f, std::sqrt(juce::jmax(0.0f, energy)) * 2.2f);
            if (event.kind == static_cast<std::uint32_t>(
                    DiffusionDelayTelemetryEventKind::primaryRepeat))
            {
                graphics.setColour(accent.withAlpha(0.20f + level * 0.70f));
                graphics.drawVerticalLine(
                    juce::roundToInt(x), y - 10.0f - level * 12.0f,
                    y + 10.0f + level * 12.0f);
                graphics.fillEllipse(
                    juce::Rectangle<float>(4.0f + level * 5.0f,
                                           4.0f + level * 5.0f)
                        .withCentre({ x, y }));
                continue;
            }
            if (event.kind != static_cast<std::uint32_t>(
                    DiffusionDelayTelemetryEventKind::diffusionCloud))
                continue;

            const auto diffusion = event.values[static_cast<size_t>(
                DiffusionDelayTelemetryValue::diffusion)];
            const auto spread = event.values[static_cast<size_t>(
                DiffusionDelayTelemetryValue::stereoSpread)];
            const auto radius = 4.0f + diffusion * 16.0f
                + spread * 8.0f;
            for (int particle = 0; particle < 7; ++particle)
            {
                const auto phase = static_cast<float>(
                    event.sequence % 97u + static_cast<std::uint64_t>(
                        particle * 17));
                const auto point = juce::Point<float>(
                    x + std::sin(phase * 0.73f) * radius,
                    y + std::cos(phase * 0.51f) * radius * 0.65f);
                graphics.setColour(outputColour.withAlpha(
                    0.08f + level
                        * (0.20f + 0.06f * static_cast<float>(particle))));
                graphics.fillEllipse(
                    juce::Rectangle<float>(3.0f + level * 4.0f,
                                           3.0f + level * 4.0f)
                        .withCentre(point));
            }
        }
    }
    else
    {
        graphics.setColour(juce::Colours::white.withAlpha(0.55f));
        graphics.drawText(
            "DSP EVENT TELEMETRY WAITING", cloud.reduced(8.0f).toNearestInt(),
            juce::Justification::centredBottom);
    }
    const auto node = juce::Point<float>(
        primaryX, cloud.getBottom() - value(4) * cloud.getHeight());
    graphics.setColour(juce::Colours::white);
    graphics.fillEllipse(
        juce::Rectangle<float>(13.0f, 13.0f).withCentre(node));
    graphics.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    graphics.drawText(
        formatControlValue(type, value(1) >= 0.5f ? 2 : 0, timing)
            + "  /  " + formatControlValue(type, 4, value(4)),
        juce::Rectangle<float>(180.0f, 18.0f)
            .withCentre(node.translated(0.0f, -15.0f))
            .constrainedWithin(cloud).toNearestInt(),
        juce::Justification::centred);

    for (const auto control : { 3, 5, 6, 7, 8, 9, 10, 11 })
    {
        const auto bounds = trackBounds(fullArea, control);
        graphics.setColour(juce::Colour(0xff151b24));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour((control == 10 || control == 11
                                ? outputColour : accent).withAlpha(0.58f));
        graphics.fillRoundedRectangle(
            bounds.withWidth(bounds.getWidth() * value(control)), 5.0f);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(9.2f));
        graphics.drawFittedText(
            juce::String(controlMetadata(type, control).label) + "  "
                + formatControlValue(type, control, value(control)),
            bounds.reduced(6.0f, 0.0f).toNearestInt(),
            juce::Justification::centred, 1);
    }
}
} // namespace

std::unique_ptr<ModuleView> createDiffusionDelayView(EffectGraph& graph)
{
    return std::make_unique<DiffusionDelayModuleView>(graph);
}
} // namespace megadsp::ui
