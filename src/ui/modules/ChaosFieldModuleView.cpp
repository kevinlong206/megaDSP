#include "../GraphStyle.h"
#include "../ModuleView.h"
#include "../../modules/ChaosField.h"

#include "ModuleViewCreators.h"

#include <array>
#include <cmath>

namespace megadsp::ui
{
std::unique_ptr<ModuleView> createChaosFieldView(EffectGraph&);

namespace
{
class ChaosFieldModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    bool usesFullPanel() const override { return true; }
    void configureAccessibility(juce::Component& component) const override
    {
        component.setTitle("Chaos Field attractor scope");
        component.setHelpText(
            "Choose Lorenz, Rossler, or Double Pendulum with the top pills and "
            "toggle free or synced timing at the right. The scope trail is the "
            "actual captured DSP attractor; colour follows its third axis. Live "
            "readouts report the current filter, delay, and pan states. The "
            "scope's bottom Depth rail and right Pan Orbit rail are directly "
            "editable. Lower tracks edit timing, centers, feedback, stereo "
            "spread, mix, and output. Double-click any region to reset it.");
    }
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    static juce::Rectangle<float> attractorPill(
        juce::Rectangle<float> area, int index)
    {
        auto row = area.removeFromTop(31.0f);
        row.removeFromRight(108.0f);
        const auto width =
            row.getWidth() / static_cast<float>(ChaosFieldModule::attractorCount);
        return juce::Rectangle<float>(
            row.getX() + width * static_cast<float>(index), row.getY(),
            width, row.getHeight()).reduced(3.0f, 2.0f);
    }

    static juce::Rectangle<float> syncBounds(juce::Rectangle<float> area)
    {
        return area.removeFromTop(31.0f).removeFromRight(108.0f)
            .reduced(3.0f, 2.0f);
    }

    static juce::Rectangle<float> fieldBounds(juce::Rectangle<float> area)
    {
        area.removeFromTop(33.0f);
        area.removeFromBottom(area.getHeight() * 0.43f);
        return area.reduced(5.0f);
    }

    static juce::Rectangle<float> stateBounds(juce::Rectangle<float> area)
    {
        return fieldBounds(area).removeFromTop(25.0f);
    }

    static juce::Rectangle<float> depthRailBounds(
        juce::Rectangle<float> area)
    {
        auto body = fieldBounds(area);
        body.removeFromTop(27.0f);
        body.removeFromRight(54.0f);
        return body.removeFromBottom(25.0f).reduced(4.0f, 2.0f);
    }

    static juce::Rectangle<float> panRailBounds(
        juce::Rectangle<float> area)
    {
        auto body = fieldBounds(area);
        body.removeFromTop(27.0f);
        body.removeFromBottom(27.0f);
        return body.removeFromRight(52.0f).reduced(5.0f, 3.0f);
    }

    static juce::Rectangle<float> trailBounds(juce::Rectangle<float> area)
    {
        auto body = fieldBounds(area);
        body.removeFromTop(27.0f);
        body.removeFromBottom(27.0f);
        body.removeFromRight(54.0f);
        return body.reduced(7.0f, 4.0f);
    }

    static juce::Rectangle<float> trackBounds(
        juce::Rectangle<float> area, int control, bool synced)
    {
        area.removeFromTop(33.0f);
        auto tracks = area.removeFromBottom(area.getHeight() * 0.43f);
        const std::array<int, 7> order {
            synced ? ChaosFieldModule::divisionControl
                   : ChaosFieldModule::rateControl,
            ChaosFieldModule::filterCenterControl,
            ChaosFieldModule::delayCenterControl,
            ChaosFieldModule::feedbackControl,
            ChaosFieldModule::stereoSpreadControl,
            ChaosFieldModule::mixControl,
            ChaosFieldModule::outputControl
        };
        auto position = 0;
        while (position < static_cast<int>(order.size())
               && order[static_cast<size_t>(position)] != control)
            ++position;
        if (position == static_cast<int>(order.size()))
            return {};
        const auto columns = position < 4 ? 4 : 3;
        const auto row = position < 4 ? 0 : 1;
        const auto column = position < 4 ? position : position - 4;
        const auto width = tracks.getWidth() / static_cast<float>(columns);
        const auto height = tracks.getHeight() * 0.5f;
        return juce::Rectangle<float>(
            tracks.getX() + width * static_cast<float>(column),
            tracks.getY() + height * static_cast<float>(row),
            width, height).reduced(6.0f, 4.0f);
    }

    static juce::String compactFrequency(float frequency)
    {
        return frequency >= 1000.0f
            ? juce::String(frequency / 1000.0f, 2) + " kHz"
            : juce::String(frequency, 0) + " Hz";
    }

    static juce::String panText(float pan)
    {
        const auto amount = juce::roundToInt(std::abs(pan) * 100.0f);
        if (amount < 1)
            return "CENTER";
        return juce::String(amount) + "% " + (pan < 0.0f ? "L" : "R");
    }

    bool isSynced() const
    {
        return value(ChaosFieldModule::syncControl) >= 0.5f;
    }

    int timingControl() const
    {
        return isSynced() ? ChaosFieldModule::divisionControl
                          : ChaosFieldModule::rateControl;
    }

    void chooseAttractor(int index);
};

void ChaosFieldModuleView::chooseAttractor(int index)
{
    if (auto* target = parameter(ChaosFieldModule::attractorControl))
    {
        graph.focusKeyboardControl(ChaosFieldModule::attractorControl);
        target->beginChangeGesture();
        target->setValueNotifyingHost(
            discreteValue(index, ChaosFieldModule::attractorCount));
        target->endChangeGesture();
    }
    repaint();
}

void ChaosFieldModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int index = 0; index < ChaosFieldModule::attractorCount; ++index)
        if (attractorPill(area, index).contains(event.position))
        {
            chooseAttractor(index);
            return;
        }
    if (syncBounds(area).contains(event.position))
    {
        toggleParameter(ChaosFieldModule::syncControl);
        return;
    }

    if (depthRailBounds(area).contains(event.position))
        dragPrimary = ChaosFieldModule::depthControl;
    else if (panRailBounds(area).contains(event.position))
        dragPrimary = ChaosFieldModule::panOrbitControl;

    const auto synced = isSynced();
    const std::array<int, 7> controls {
        timingControl(),
        ChaosFieldModule::filterCenterControl,
        ChaosFieldModule::delayCenterControl,
        ChaosFieldModule::feedbackControl,
        ChaosFieldModule::stereoSpreadControl,
        ChaosFieldModule::mixControl,
        ChaosFieldModule::outputControl
    };
    for (const auto control : controls)
        if (dragPrimary < 0
            && trackBounds(area, control, synced).contains(event.position))
            dragPrimary = control;
    if (dragPrimary < 0)
        return;
    if (dragPrimary == ChaosFieldModule::divisionControl)
    {
        cycleChoice(ChaosFieldModule::divisionControl, 8);
        dragPrimary = -1;
        return;
    }
    beginGestures();
    mouseDrag(event);
}

void ChaosFieldModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    juce::Rectangle<float> bounds;
    float normalized = 0.0f;
    if (dragPrimary == ChaosFieldModule::depthControl)
    {
        bounds = depthRailBounds(area);
        normalized =
            (event.position.x - bounds.getX()) / bounds.getWidth();
    }
    else if (dragPrimary == ChaosFieldModule::panOrbitControl)
    {
        bounds = panRailBounds(area);
        normalized =
            (bounds.getBottom() - event.position.y) / bounds.getHeight();
    }
    else
    {
        bounds = trackBounds(area, dragPrimary, isSynced());
        normalized =
            (event.position.x - bounds.getX()) / bounds.getWidth();
    }
    setValue(dragPrimary, juce::jlimit(0.0f, 1.0f, normalized));
    updateDefaultDragReadout();
}

void ChaosFieldModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int index = 0; index < ChaosFieldModule::attractorCount; ++index)
        if (attractorPill(area, index).contains(event.position))
        {
            resetToDefault(ChaosFieldModule::attractorControl);
            return;
        }
    if (syncBounds(area).contains(event.position))
    {
        resetToDefault(ChaosFieldModule::syncControl);
        return;
    }
    if (depthRailBounds(area).contains(event.position))
    {
        resetToDefault(ChaosFieldModule::depthControl);
        return;
    }
    if (panRailBounds(area).contains(event.position))
    {
        resetToDefault(ChaosFieldModule::panOrbitControl);
        return;
    }

    const auto synced = isSynced();
    const std::array<int, 7> controls {
        timingControl(),
        ChaosFieldModule::filterCenterControl,
        ChaosFieldModule::delayCenterControl,
        ChaosFieldModule::feedbackControl,
        ChaosFieldModule::stereoSpreadControl,
        ChaosFieldModule::mixControl,
        ChaosFieldModule::outputControl
    };
    for (const auto control : controls)
        if (trackBounds(area, control, synced).contains(event.position))
        {
            resetToDefault(control);
            return;
        }
}

void ChaosFieldModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    static constexpr std::array<const char*, ChaosFieldModule::attractorCount>
        attractors { "LORENZ", "ROSSLER", "DOUBLE PENDULUM" };
    const auto selected = discreteIndex(
        value(ChaosFieldModule::attractorControl),
        ChaosFieldModule::attractorCount);
    for (int index = 0; index < ChaosFieldModule::attractorCount; ++index)
    {
        const auto bounds = attractorPill(area, index);
        graphics.setColour(
            index == selected ? accent : juce::Colour(0xff27313d));
        graphics.fillRoundedRectangle(bounds, 6.0f);
        graphics.setColour(
            index == selected
                ? juce::Colour(0xff101820) : juce::Colours::white);
        graphics.setFont(juce::FontOptions(8.8f, juce::Font::bold));
        graphics.drawFittedText(
            attractors[static_cast<size_t>(index)],
            bounds.toNearestInt(), juce::Justification::centred, 1);
    }

    const auto sync = syncBounds(area);
    graphics.setColour(
        isSynced() ? outputColour : juce::Colour(0xff27313d));
    graphics.fillRoundedRectangle(sync, 6.0f);
    graphics.setColour(
        isSynced() ? juce::Colour(0xff101820) : juce::Colours::white);
    graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    graphics.drawText(
        isSynced() ? "SYNCED" : "FREE", sync.toNearestInt(),
        juce::Justification::centred);

    ContinuousTelemetrySnapshot telemetry;
    const auto hasTelemetry =
        readContinuousTelemetry(telemetry)
        && telemetry.sequence != 0
        && telemetry.valueCount >= ChaosFieldModule::telemetryValueCount;
    const auto hasTrail =
        hasTelemetry
        && telemetry.historyValueCount
               >= ChaosFieldModule::telemetryHistoryValueCount
        && telemetry.historyCount > 1;

    const auto field = fieldBounds(area);
    graphics.setColour(juce::Colour(0xe6121821));
    graphics.fillRoundedRectangle(field, 8.0f);
    const auto states = stateBounds(area);
    const std::array<juce::String, 3> stateText {
        hasTelemetry
            ? "FILTER  "
                  + compactFrequency(
                      telemetry.values[ChaosFieldModule::actualFilterHz])
            : juce::String("FILTER  —"),
        hasTelemetry
            ? "DELAY  "
                  + juce::String(
                      telemetry.values[
                          ChaosFieldModule::actualDelayMilliseconds],
                      1)
                  + " ms"
            : juce::String("DELAY  —"),
        hasTelemetry
            ? "PAN  "
                  + panText(telemetry.values[ChaosFieldModule::actualPan])
            : juce::String("PAN  —")
    };
    for (int index = 0; index < 3; ++index)
    {
        const auto width = states.getWidth() / 3.0f;
        const auto cell = juce::Rectangle<float>(
            states.getX() + width * static_cast<float>(index),
            states.getY(), width, states.getHeight()).reduced(2.0f, 1.0f);
        graphics.setColour(juce::Colour(0xff202a36));
        graphics.fillRoundedRectangle(cell, 4.0f);
        graphics.setColour(
            hasTelemetry ? juce::Colours::white
                         : juce::Colours::white.withAlpha(0.42f));
        graphics.setFont(juce::FontOptions(8.8f, juce::Font::bold));
        graphics.drawFittedText(
            stateText[static_cast<size_t>(index)], cell.toNearestInt(),
            juce::Justification::centred, 1);
    }

    const auto trail = trailBounds(area);
    graphics.setColour(juce::Colours::white.withAlpha(0.10f));
    graphics.drawHorizontalLine(
        juce::roundToInt(trail.getCentreY()),
        trail.getX(), trail.getRight());
    graphics.drawVerticalLine(
        juce::roundToInt(trail.getCentreX()),
        trail.getY(), trail.getBottom());
    if (hasTrail)
    {
        const auto toPoint = [&telemetry, trail](std::uint32_t point)
        {
            const auto x = continuousTelemetryHistoryValue(
                telemetry, ChaosFieldModule::xHistory, point);
            const auto y = continuousTelemetryHistoryValue(
                telemetry, ChaosFieldModule::yHistory, point);
            return juce::Point<float> {
                trail.getCentreX()
                    + juce::jlimit(-1.0f, 1.0f, x)
                        * trail.getWidth() * 0.47f,
                trail.getCentreY()
                    - juce::jlimit(-1.0f, 1.0f, y)
                        * trail.getHeight() * 0.45f
            };
        };
        auto previous = toPoint(0);
        for (std::uint32_t point = 1; point < telemetry.historyCount; ++point)
        {
            const auto current = toPoint(point);
            const auto z = juce::jlimit(
                -1.0f, 1.0f,
                continuousTelemetryHistoryValue(
                    telemetry, ChaosFieldModule::zHistory, point));
            const auto wet = juce::jmax(
                0.0f,
                continuousTelemetryHistoryValue(
                    telemetry, ChaosFieldModule::wetHistory, point));
            const auto level = juce::jlimit(
                0.0f, 1.0f, std::sqrt(wet) * 2.4f);
            graphics.setColour(
                accent.interpolatedWith(outputColour, (z + 1.0f) * 0.5f)
                    .withAlpha(0.16f + level * 0.68f));
            graphics.drawLine(
                { previous, current }, 1.0f + level * 1.5f);
            previous = current;
        }
        const auto current = juce::Point<float> {
            trail.getCentreX()
                + juce::jlimit(
                      -1.0f, 1.0f,
                      telemetry.values[ChaosFieldModule::actualX])
                    * trail.getWidth() * 0.47f,
            trail.getCentreY()
                - juce::jlimit(
                      -1.0f, 1.0f,
                      telemetry.values[ChaosFieldModule::actualY])
                    * trail.getHeight() * 0.45f
        };
        graphics.setColour(outputColour);
        graphics.fillEllipse(
            juce::Rectangle<float>(9.0f, 9.0f).withCentre(current));
    }
    else
    {
        graphics.setColour(juce::Colours::white.withAlpha(0.50f));
        graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        graphics.drawText(
            "DSP ATTRACTOR TELEMETRY WAITING",
            trail.toNearestInt(), juce::Justification::centred);
    }

    const auto depthRail = depthRailBounds(area);
    graphics.setColour(juce::Colour(0xff2d3745));
    graphics.fillRoundedRectangle(depthRail, 4.0f);
    graphics.setColour(accent.withAlpha(0.65f));
    graphics.fillRoundedRectangle(
        depthRail.withWidth(
            depthRail.getWidth()
            * value(ChaosFieldModule::depthControl)),
        4.0f);
    graphics.setColour(juce::Colours::white);
    graphics.setFont(juce::FontOptions(8.5f, juce::Font::bold));
    graphics.drawFittedText(
        "DEPTH  "
            + formatControlValue(
                type, ChaosFieldModule::depthControl,
                value(ChaosFieldModule::depthControl)),
        depthRail.reduced(5.0f, 0.0f).toNearestInt(),
        juce::Justification::centred, 1);

    const auto panRail = panRailBounds(area);
    graphics.setColour(juce::Colour(0xff2d3745));
    graphics.fillRoundedRectangle(panRail, 4.0f);
    graphics.setColour(outputColour.withAlpha(0.62f));
    graphics.fillRoundedRectangle(
        panRail.withTop(
            panRail.getBottom()
            - panRail.getHeight()
                * value(ChaosFieldModule::panOrbitControl)),
        4.0f);
    graphics.setColour(juce::Colours::white);
    graphics.setFont(juce::FontOptions(8.0f, juce::Font::bold));
    graphics.drawFittedText(
        "PAN\nORBIT",
        panRail.reduced(2.0f).toNearestInt(),
        juce::Justification::centred, 2);

    const auto synced = isSynced();
    const std::array<int, 7> controls {
        timingControl(),
        ChaosFieldModule::filterCenterControl,
        ChaosFieldModule::delayCenterControl,
        ChaosFieldModule::feedbackControl,
        ChaosFieldModule::stereoSpreadControl,
        ChaosFieldModule::mixControl,
        ChaosFieldModule::outputControl
    };
    for (const auto control : controls)
    {
        const auto bounds = trackBounds(area, control, synced);
        graphics.setColour(juce::Colour(0xff151b24));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour(
            (control >= ChaosFieldModule::mixControl
                 ? outputColour : accent).withAlpha(0.58f));
        if (control == ChaosFieldModule::feedbackControl)
        {
            const auto x = bounds.getX() + bounds.getWidth() * value(control);
            const auto centre = bounds.getCentreX();
            graphics.fillRect(
                juce::Rectangle<float>(
                    juce::jmin(x, centre), bounds.getY(),
                    std::abs(x - centre), bounds.getHeight()));
        }
        else
            graphics.fillRoundedRectangle(
                bounds.withWidth(bounds.getWidth() * value(control)), 5.0f);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(9.2f));
        const auto label =
            control == ChaosFieldModule::divisionControl
                ? juce::String("Division")
                : control == ChaosFieldModule::rateControl
                    ? juce::String("Rate")
                    : juce::String(controlMetadata(type, control).label);
        graphics.drawFittedText(
            label + "  "
                + formatControlValue(type, control, value(control)),
            bounds.reduced(6.0f, 0.0f).toNearestInt(),
            juce::Justification::centred, 1);
    }
}
} // namespace

std::unique_ptr<ModuleView> createChaosFieldView(EffectGraph& graph)
{
    return std::make_unique<ChaosFieldModuleView>(graph);
}
} // namespace megadsp::ui
