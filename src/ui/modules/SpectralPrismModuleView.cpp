#include "../GraphStyle.h"
#include "../ModuleView.h"

#include "ModuleViewCreators.h"

#include <array>
#include <cmath>

namespace megadsp::ui
{
namespace
{
class SpectralPrismModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    bool usesFullPanel() const override { return true; }
    void configureAccessibility(juce::Component& component) const override
    {
        component.setTitle("Spectral Prism mapping");
        component.setHelpText(
            "Drag the prism node horizontally for Pivot and vertically for "
            "Warp. The live spectra and mapping rays show the transformation. "
            "Freeze uses an explicit pill; remaining spectral controls use "
            "lower tracks. Double-click any region to restore its default.");
    }
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    static juce::Rectangle<float> freezeBounds(juce::Rectangle<float> area)
    {
        return area.removeFromTop(30.0f).removeFromRight(116.0f)
            .reduced(5.0f, 2.0f);
    }
    static juce::Rectangle<float> prismBounds(juce::Rectangle<float> area)
    {
        area.removeFromTop(33.0f);
        area.removeFromBottom(area.getHeight() * 0.43f);
        return area.reduced(5.0f);
    }
    static juce::Rectangle<float> trackBounds(
        juce::Rectangle<float> area, int control)
    {
        area.removeFromTop(33.0f);
        auto tracks = area.removeFromBottom(area.getHeight() * 0.43f);
        static constexpr std::array<int, 9> order {
            2, 3, 9, 5, 6, 7, 8, 10, 11
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
};

void SpectralPrismModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (freezeBounds(area).contains(event.position))
    {
        toggleParameter(4);
        return;
    }
    if (prismBounds(area).contains(event.position))
    {
        dragPrimary = 1;
        dragSecondary = 0;
    }
    for (const auto control : { 2, 3, 9, 5, 6, 7, 8, 10, 11 })
        if (dragPrimary < 0
            && trackBounds(area, control).contains(event.position))
            dragPrimary = control;
    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void SpectralPrismModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (dragPrimary == 1)
    {
        const auto bounds = prismBounds(area);
        setValue(1, juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - bounds.getX()) / bounds.getWidth()));
        setValue(0, juce::jlimit(
            0.0f, 1.0f,
            (bounds.getBottom() - event.position.y) / bounds.getHeight()));
        dragReadout = "PIVOT  " + formatControlValue(type, 1, value(1))
            + "    WARP  " + formatControlValue(type, 0, value(0));
        repaint();
        return;
    }
    const auto bounds = trackBounds(area, dragPrimary);
    setValue(dragPrimary, juce::jlimit(
        0.0f, 1.0f, (event.position.x - bounds.getX()) / bounds.getWidth()));
    updateDefaultDragReadout();
}

void SpectralPrismModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    const auto defaults = moduleDefaults(type);
    if (freezeBounds(area).contains(event.position))
    {
        setValue(4, defaults[4]);
        return;
    }
    if (prismBounds(area).contains(event.position))
    {
        setValue(0, defaults[0]);
        setValue(1, defaults[1]);
        return;
    }
    for (const auto control : { 2, 3, 9, 5, 6, 7, 8, 10, 11 })
        if (trackBounds(area, control).contains(event.position))
        {
            setValue(control, defaults[static_cast<size_t>(control)]);
            return;
        }
}

void SpectralPrismModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto fullArea = area;
    auto title = area.removeFromTop(30.0f);
    graphics.setColour(accent);
    graphics.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    graphics.drawText("SPECTRAL MAPPING", title.reduced(7.0f, 0.0f)
                                         .toNearestInt(),
                      juce::Justification::centredLeft);
    const auto freeze = freezeBounds(fullArea);
    graphics.setColour(value(4) >= 0.5f
                           ? outputColour : juce::Colour(0xff27313d));
    graphics.fillRoundedRectangle(freeze, 7.0f);
    graphics.setColour(value(4) >= 0.5f
                           ? juce::Colour(0xff101820)
                           : juce::Colours::white);
    graphics.drawText(value(4) >= 0.5f ? "FREEZE: HELD" : "FREEZE: LIVE",
                      freeze.toNearestInt(), juce::Justification::centred);

    const auto prism = prismBounds(fullArea);
    graphics.setColour(juce::Colour(0xe6121821));
    graphics.fillRoundedRectangle(prism, 8.0f);
    drawSpectrum(graphics, prism.reduced(8.0f), inputSpectrum,
                 inputColour.withAlpha(0.55f));
    drawSpectrum(graphics, prism.reduced(8.0f), outputSpectrum,
                 outputColour.withAlpha(0.75f));

    const auto pivotX = prism.getX() + value(1) * prism.getWidth();
    const auto nodeY = prism.getBottom() - value(0) * prism.getHeight();
    const auto shift = value(2) * 2.0f - 1.0f;
    graphics.setColour(accent.withAlpha(0.30f));
    for (int ray = 0; ray < 9; ++ray)
    {
        const auto source = prism.getX()
            + prism.getWidth() * static_cast<float>(ray) / 8.0f;
        const auto relative = (source - pivotX) / prism.getWidth();
        const auto warped = std::copysign(
            std::pow(std::abs(relative),
                     juce::jmap(value(0), 0.0f, 1.0f, 1.8f, 0.55f)),
            relative);
        const auto destination = juce::jlimit(
            prism.getX(), prism.getRight(),
            pivotX + warped * prism.getWidth()
                + shift * prism.getWidth() * 0.18f);
        graphics.drawLine(source, prism.getBottom() - 6.0f,
                          destination, prism.getY() + 7.0f, 1.0f);
    }
    graphics.setColour(juce::Colours::white.withAlpha(0.55f));
    graphics.drawVerticalLine(juce::roundToInt(pivotX),
                              prism.getY(), prism.getBottom());
    graphics.setColour(accent);
    graphics.fillEllipse(
        juce::Rectangle<float>(14.0f, 14.0f)
            .withCentre({ pivotX, nodeY }));
    graphics.setColour(juce::Colours::white);
    graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    graphics.drawText(
        formatControlValue(type, 1, value(1)) + "  /  "
            + formatControlValue(type, 0, value(0)),
        juce::Rectangle<float>(210.0f, 18.0f)
            .withCentre({ pivotX, nodeY - 15.0f })
            .constrainedWithin(prism)
            .toNearestInt(),
        juce::Justification::centred);

    for (const auto control : { 2, 3, 9, 5, 6, 7, 8, 10, 11 })
    {
        const auto bounds = trackBounds(fullArea, control);
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

std::unique_ptr<ModuleView> createSpectralPrismView(EffectGraph& graph)
{
    return std::make_unique<SpectralPrismModuleView>(graph);
}
} // namespace megadsp::ui
