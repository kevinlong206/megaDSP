#include "../GraphStyle.h"
#include "../ModuleView.h"

#include "ModuleViewCreators.h"
#include "../../modules/NeuralAmpModeler.h"

#include <cmath>

namespace megadsp::ui
{
namespace
{
class NeuralAmpModelerModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    bool usesFullPanel() const override { return true; }

    void configureAccessibility(juce::Component& component) const override
    {
        component.setTitle("Neural Amp Modeler");
        component.setHelpText(
            "Drop a .nam model into the panel. Pre Gain drives the model, "
            "Bass, Mid, and Treble form a guitar tone stack, and Post Gain "
            "sets the final modeled level.");
    }

    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void timerCallback() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) const override
    {
        return files.size() == 1 && files[0].endsWithIgnoreCase(".nam");
    }

    void filesDropped(const juce::StringArray& files, int, int) override
    {
        if (!isInterestedInFileDrag(files))
            return;
        const auto result = processor.loadNamModel(slot, juce::File(files[0]));
        if (result.failed())
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Could not load NAM model", result.getErrorMessage());
    }

    std::vector<int> keyboardControls() const override
    {
        return { 0, 1, 2, 3, 4 };
    }

private:
    NamModelInfo cachedInfo;
    float dragStartValue = 0.0f;

    static juce::Rectangle<float> knobBounds(
        juce::Rectangle<float> area, int control)
    {
        area.removeFromTop(78.0f);
        const auto width = area.getWidth() / 5.0f;
        return { area.getX() + width * static_cast<float>(control),
                 area.getY(), width, area.getHeight() };
    }

    static juce::String kindName(NamModelKind kind)
    {
        switch (kind)
        {
            case NamModelKind::waveNetA2Standard: return "A2 Standard";
            case NamModelKind::waveNetA2Nano: return "A2 Nano";
            case NamModelKind::slimmableWaveNet: return "Slimmable WaveNet";
            case NamModelKind::waveNet: return "WaveNet";
            case NamModelKind::lstm: return "LSTM";
            case NamModelKind::linear: return "Linear";
            case NamModelKind::convNet: return "ConvNet";
            case NamModelKind::container: return "Slimmable Container";
            case NamModelKind::unknown: return "Unknown";
        }
        return "Unknown";
    }
};

void NeuralAmpModelerModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int control = 0; control < 5; ++control)
        if (knobBounds(area, control).contains(event.position))
        {
            dragPrimary = control;
            dragStartValue = value(control);
            beginGestures();
            return;
        }
}

void NeuralAmpModelerModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    setValue(
        dragPrimary,
        juce::jlimit(
            0.0f, 1.0f,
            dragStartValue - static_cast<float>(event.getDistanceFromDragStartY())
                / 180.0f));
    updateDefaultDragReadout();
}

void NeuralAmpModelerModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int control = 0; control < 5; ++control)
        if (knobBounds(area, control).contains(event.position))
        {
            resetToDefault(control);
            return;
        }
}

void NeuralAmpModelerModuleView::timerCallback()
{
    const auto* module = processor.getRack().activeModuleInstance(slot);
    const auto* capability =
        module != nullptr ? module->namModelCapability() : nullptr;
    if (capability == nullptr)
        return;
    const auto info = capability->namModelInfo();
    if (info.name != cachedInfo.name || info.valid != cachedInfo.valid
        || info.kind != cachedInfo.kind)
    {
        cachedInfo = info;
        repaint();
    }
}

void NeuralAmpModelerModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto fullArea = area;
    auto header = area.removeFromTop(68.0f);
    graphics.setColour(juce::Colour(0xff10141b));
    graphics.fillRoundedRectangle(header, 8.0f);
    graphics.setColour(cachedInfo.valid
        ? juce::Colours::white : juce::Colour(0xffa8b3c0));
    graphics.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    graphics.drawText(
        cachedInfo.valid ? cachedInfo.name : "No model loaded",
        header.removeFromTop(28.0f).reduced(10.0f, 0.0f).toNearestInt(),
        juce::Justification::centredLeft);
    graphics.setFont(juce::FontOptions(10.0f));
    graphics.setColour(juce::Colour(0xffa8b3c0));
    graphics.drawText(
        cachedInfo.valid
            ? kindName(cachedInfo.kind)
                + (cachedInfo.expectedSampleRate > 0.0
                       ? "  |  "
                           + juce::String(
                               cachedInfo.expectedSampleRate / 1000.0, 1)
                           + " kHz"
                       : juce::String())
            : "Drop a .nam file or use Load NAM...",
        header.reduced(10.0f, 0.0f).toNearestInt(),
        juce::Justification::centredLeft);

    static constexpr std::array<const char*, 5> labels {
        "PRE GAIN", "BASS", "MID", "TREBLE", "POST GAIN"
    };
    for (int control = 0; control < 5; ++control)
    {
        auto bounds = knobBounds(fullArea, control);
        const auto diameter = juce::jmin(
            74.0f, juce::jmin(bounds.getWidth() - 16.0f,
                              bounds.getHeight() - 42.0f));
        const auto knob = juce::Rectangle<float>(diameter, diameter)
            .withCentre({ bounds.getCentreX(), bounds.getCentreY() - 4.0f });
        graphics.setColour(juce::Colour(0xff242c33));
        graphics.fillEllipse(knob);
        const auto start = juce::MathConstants<float>::pi * 0.75f;
        const auto end = juce::MathConstants<float>::pi * 2.25f;
        juce::Path arc;
        arc.addCentredArc(
            knob.getCentreX(), knob.getCentreY(),
            diameter * 0.42f, diameter * 0.42f, 0.0f,
            start, start + (end - start) * value(control), true);
        graphics.setColour(
            control == 0 || control == 4 ? outputColour : accent);
        graphics.strokePath(arc, juce::PathStrokeType(4.0f));
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        graphics.drawText(
            labels[static_cast<size_t>(control)],
            bounds.removeFromBottom(22.0f).toNearestInt(),
            juce::Justification::centred);
        graphics.setFont(juce::FontOptions(9.0f));
        graphics.drawText(
            formatControlValue(type, control, value(control)),
            knob.toNearestInt(), juce::Justification::centred);
    }
}
} // namespace

std::unique_ptr<ModuleView> createNeuralAmpModelerView(EffectGraph& graph)
{
    return std::make_unique<NeuralAmpModelerModuleView>(graph);
}
} // namespace megadsp::ui
