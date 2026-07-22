#include "../GraphStyle.h"
#include "../ModuleView.h"

#include "ModuleViewCreators.h"

#include <array>
#include <cmath>

namespace megadsp::ui
{
namespace
{
class ResonantMatrixModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    bool usesFullPanel() const override { return true; }
    void configureAccessibility(juce::Component& component) const override
    {
        component.setTitle("Resonant Matrix node field");
        component.setHelpText(
            "Scale and Topology use named pills. Drag the root node "
            "horizontally for Tune and vertically for Span. Drag the tail field "
            "for Decay and Damping; lower tracks edit motion, width, mix, and "
            "output. Double-click a control to restore its default.");
    }
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    static juce::Rectangle<float> scalePill(
        juce::Rectangle<float> area, int index)
    {
        auto row = area.removeFromTop(28.0f);
        const auto width = row.getWidth() / 6.0f;
        return juce::Rectangle<float>(
            row.getX() + width * static_cast<float>(index), row.getY(),
            width, row.getHeight()).reduced(3.0f, 2.0f);
    }
    static juce::Rectangle<float> topologyPill(
        juce::Rectangle<float> area, int index)
    {
        area.removeFromTop(29.0f);
        auto row = area.removeFromTop(28.0f);
        const auto width = row.getWidth() / 4.0f;
        return juce::Rectangle<float>(
            row.getX() + width * static_cast<float>(index), row.getY(),
            width, row.getHeight()).reduced(3.0f, 2.0f);
    }
    static juce::Rectangle<float> nodeBounds(juce::Rectangle<float> area)
    {
        area.removeFromTop(60.0f);
        area.removeFromBottom(area.getHeight() * 0.40f);
        return area.reduced(5.0f);
    }
    static juce::Rectangle<float> trackBounds(
        juce::Rectangle<float> area, int control)
    {
        area.removeFromTop(60.0f);
        auto tracks = area.removeFromBottom(area.getHeight() * 0.40f);
        static constexpr std::array<int, 8> order {
            4, 5, 6, 7, 8, 9, 10, 11
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
    void choose(int control, int index, int count);
};

void ResonantMatrixModuleView::choose(int control, int index, int count)
{
    if (auto* target = parameter(control))
    {
        graph.focusKeyboardControl(control);
        target->beginChangeGesture();
        target->setValueNotifyingHost(discreteValue(index, count));
        target->endChangeGesture();
    }
    repaint();
}

void ResonantMatrixModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int index = 0; index < 6; ++index)
        if (scalePill(area, index).contains(event.position))
        {
            choose(1, index, 6);
            return;
        }
    for (int index = 0; index < 4; ++index)
        if (topologyPill(area, index).contains(event.position))
        {
            choose(3, index, 4);
            return;
        }
    if (nodeBounds(area).contains(event.position))
    {
        dragPrimary = 0;
        dragSecondary = 2;
    }
    for (const auto control : { 4, 5, 6, 7, 8, 9, 10, 11 })
        if (dragPrimary < 0
            && trackBounds(area, control).contains(event.position))
            dragPrimary = control;
    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void ResonantMatrixModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (dragPrimary == 0)
    {
        const auto bounds = nodeBounds(area);
        setValue(0, juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - bounds.getX()) / bounds.getWidth()));
        setValue(2, juce::jlimit(
            0.0f, 1.0f,
            (bounds.getBottom() - event.position.y) / bounds.getHeight()));
        dragReadout = "TUNE  " + formatControlValue(type, 0, value(0))
            + "    SPAN  " + formatControlValue(type, 2, value(2));
        repaint();
        return;
    }
    const auto bounds = trackBounds(area, dragPrimary);
    setValue(dragPrimary, juce::jlimit(
        0.0f, 1.0f, (event.position.x - bounds.getX()) / bounds.getWidth()));
    updateDefaultDragReadout();
}

void ResonantMatrixModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto defaults = moduleDefaults(type);
    for (int index = 0; index < 6; ++index)
        if (scalePill(area, index).contains(event.position))
        {
            setValue(1, defaults[1]);
            return;
        }
    for (int index = 0; index < 4; ++index)
        if (topologyPill(area, index).contains(event.position))
        {
            setValue(3, defaults[3]);
            return;
        }
    if (nodeBounds(area).contains(event.position))
    {
        setValue(0, defaults[0]);
        setValue(2, defaults[2]);
        return;
    }
    for (const auto control : { 4, 5, 6, 7, 8, 9, 10, 11 })
        if (trackBounds(area, control).contains(event.position))
        {
            setValue(control, defaults[static_cast<size_t>(control)]);
            return;
        }
}

void ResonantMatrixModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    static constexpr std::array<const char*, 6> scales {
        "MAJOR", "MINOR", "DORIAN", "PENTA", "WHOLE", "OCTAVES"
    };
    static constexpr std::array<const char*, 4> topologies {
        "ORBIT", "BUTTERFLY", "SPIRAL", "SCATTER"
    };
    static constexpr std::array<std::array<int, 8>, 6> intervals {{
        {{ 0, 2, 4, 5, 7, 9, 11, 12 }},
        {{ 0, 2, 3, 5, 7, 8, 10, 12 }},
        {{ 0, 2, 3, 5, 7, 9, 10, 12 }},
        {{ 0, 3, 5, 7, 10, 12, 15, 17 }},
        {{ 0, 2, 4, 6, 8, 10, 12, 14 }},
        {{ 0, 12, 19, 24, 31, 36, 43, 48 }}
    }};
    const auto scale = discreteIndex(value(1), 6);
    const auto topology = discreteIndex(value(3), 4);
    for (int index = 0; index < 6; ++index)
    {
        const auto bounds = scalePill(area, index);
        graphics.setColour(index == scale
                               ? accent : juce::Colour(0xff27313d));
        graphics.fillRoundedRectangle(bounds, 6.0f);
        graphics.setColour(index == scale
                               ? juce::Colour(0xff101820)
                               : juce::Colours::white);
        graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        graphics.drawText(scales[static_cast<size_t>(index)],
                          bounds.toNearestInt(),
                          juce::Justification::centred);
    }
    for (int index = 0; index < 4; ++index)
    {
        const auto bounds = topologyPill(area, index);
        graphics.setColour(index == topology
                               ? outputColour : juce::Colour(0xff202a36));
        graphics.fillRoundedRectangle(bounds, 6.0f);
        graphics.setColour(index == topology
                               ? juce::Colour(0xff101820)
                               : juce::Colours::white);
        graphics.drawText(topologies[static_cast<size_t>(index)],
                          bounds.toNearestInt(),
                          juce::Justification::centred);
    }

    const auto field = nodeBounds(area);
    graphics.setColour(juce::Colour(0xe6121821));
    graphics.fillRoundedRectangle(field, 8.0f);
    std::array<juce::Point<float>, 8> nodes {};
    const auto span = 1.0f + value(2) * 3.0f;
    for (int node = 0; node < 8; ++node)
    {
        const auto angle = juce::MathConstants<float>::twoPi
            * static_cast<float>(node) / 8.0f - 0.5f;
        const auto radius = juce::jmap(
            static_cast<float>(node), 0.0f, 7.0f,
            field.getHeight() * 0.18f, field.getHeight() * 0.41f);
        nodes[static_cast<size_t>(node)] = {
            field.getCentreX() + std::cos(angle * span * 0.35f) * radius,
            field.getCentreY() + std::sin(angle) * field.getHeight() * 0.36f
        };
    }
    for (int node = 0; node < 8; ++node)
    {
        const auto destination = topology == 0 ? (node + 1) % 8
            : topology == 1 ? node ^ 1
            : topology == 2 ? (node * 3 + 1) % 8
                            : (node * 5 + 3) % 8;
        graphics.setColour((node % 2 == 0 ? accent : outputColour)
                               .withAlpha(0.42f));
        graphics.drawArrow(
            juce::Line<float>(nodes[static_cast<size_t>(node)],
                              nodes[static_cast<size_t>(destination)]),
            1.2f, 6.0f, 5.0f);
    }
    const auto rootFrequency = exponential(27.5f, 440.0f, value(0));
    for (int node = 0; node < 8; ++node)
    {
        const auto point = nodes[static_cast<size_t>(node)];
        const auto semitones = static_cast<float>(
            intervals[static_cast<size_t>(scale)][static_cast<size_t>(node)])
            * span / juce::jmax(1.0f,
                static_cast<float>(
                    intervals[static_cast<size_t>(scale)][7]) / 12.0f);
        const auto frequency = rootFrequency
            * std::pow(2.0f, semitones / 12.0f);
        graphics.setColour(node == 0 ? juce::Colours::white : outputColour);
        graphics.fillEllipse(
            juce::Rectangle<float>(node == 0 ? 16.0f : 11.0f,
                                   node == 0 ? 16.0f : 11.0f)
                .withCentre(point));
        graphics.setColour(juce::Colours::white.withAlpha(0.88f));
        graphics.setFont(juce::FontOptions(8.5f, juce::Font::bold));
        graphics.drawText(
            frequency >= 1000.0f
                ? juce::String(frequency / 1000.0f, 1) + "k"
                : juce::String(frequency, 0),
            juce::Rectangle<float>(48.0f, 15.0f)
                .withCentre(point.translated(0.0f, 13.0f))
                .toNearestInt(),
            juce::Justification::centred);
    }
    graphics.setColour(juce::Colours::white);
    graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    auto fieldHeader = field;
    graphics.drawText(
        formatControlValue(type, 0, value(0)) + "  /  "
            + formatControlValue(type, 2, value(2)),
        fieldHeader.removeFromTop(22.0f)
            .reduced(8.0f, 0.0f).toNearestInt(),
        juce::Justification::centredLeft);

    for (const auto control : { 4, 5, 6, 7, 8, 9, 10, 11 })
    {
        const auto bounds = trackBounds(area, control);
        graphics.setColour(juce::Colour(0xff151b24));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour(
            control == 9 ? outputColour.withAlpha(0.58f)
                         : accent.withAlpha(0.56f));
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

std::unique_ptr<ModuleView> createResonantMatrixView(EffectGraph& graph)
{
    return std::make_unique<ResonantMatrixModuleView>(graph);
}
} // namespace megadsp::ui
