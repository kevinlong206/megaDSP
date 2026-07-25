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
    void configureAccessibility(juce::Component& c) const override
    {
        c.setTitle("Neural Amp Modeler");
        const auto storedPath = processor.namModelPath(slot);
        juce::String help =
            "Drop a single .nam neural amp model file here. ";
        if (storedPath.isNotEmpty())
            help += "Stored model: " + storedPath + ". ";
        help += "Controls: Input Trim, Gate, Low/High Cut, CPU Mode, "
                "Dry/Wet, Output. Double-click a track to reset.";
        c.setHelpText(help);
    }

    void paint(juce::Graphics& g, juce::Rectangle<float> bounds) override
    {
        auto area = bounds.reduced(12.0f);
        paintStatus(g, area);
        area.removeFromTop(8.0f);
        paintGatePill(g, area);
        paintCpuPill(g, area);
        area.removeFromTop(4.0f);
        paintTracks(g, area);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto area = getLocalBounds().toFloat().reduced(12.0f);
        // Gate pill hit test
        if (gatePillBounds.contains(e.position))
        {
            toggleParameter(1);
            repaint();
            return;
        }
        // CPU pill hit test (only if slimmable)
        if (cpuPillBounds.contains(e.position) && cachedInfo.valid
            && cachedInfo.cpuCapability == NamCpuCapability::slimmable)
        {
            cycleChoice(6, 2);
            repaint();
            return;
        }
        // Track drag
        const float trackStart = area.getY() + trackYOffset;
        if (e.position.y < trackStart) return;
        const int idx = static_cast<int>(
            (e.position.y - trackStart) / trackSpacing);
        const int param = trackParam(idx);
        if (param >= 0)
        {
            dragPrimary = param;
            beginGestures();
            mouseDrag(e);
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragPrimary < 0) return;
        const auto area = getLocalBounds().toFloat().reduced(12.0f);
        const float trackLeft = 100.0f;
        const float trackWidth = area.getWidth() - trackLeft;
        const float p = juce::jlimit(
            0.0f, 1.0f,
            (e.position.x - area.getX() - trackLeft) / trackWidth);
        setValue(dragPrimary, p);
        updateDefaultDragReadout();
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (gatePillBounds.contains(e.position)) return;
        if (cpuPillBounds.contains(e.position)) return;
        const auto area = getLocalBounds().toFloat().reduced(12.0f);
        const float trackStart = area.getY() + trackYOffset;
        if (e.position.y < trackStart) return;
        const int idx = static_cast<int>(
            (e.position.y - trackStart) / trackSpacing);
        const int param = trackParam(idx);
        if (param >= 0) resetToDefault(param);
    }

    void timerCallback() override
    {
        const auto* mod = processor.getRack().activeModuleInstance(slot);
        if (mod == nullptr) return;
        const auto* namCap = mod->namModelCapability();
        if (namCap == nullptr) return;
        const auto info = namCap->namModelInfo();
        if (info.name != cachedInfo.name || info.valid != cachedInfo.valid
            || info.kind != cachedInfo.kind)
        {
            cachedInfo = info;
            repaint();
        }
    }

    bool isInterestedInFileDrag(const juce::StringArray& files) const override
    {
        if (files.size() != 1) return false;
        return files[0].endsWithIgnoreCase(".nam");
    }

    void filesDropped(const juce::StringArray& files, int, int) override
    {
        if (files.size() != 1) return;
        const juce::File f(files[0]);
        if (!f.existsAsFile()
            || f.getFileExtension().toLowerCase() != ".nam")
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Invalid file", "Please drop a single .nam file.");
            return;
        }
        const auto result = processor.loadNamModel(slot, f);
        if (result.failed())
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Could not load NAM model",
                result.getErrorMessage());
    }

    std::vector<int> keyboardControls() const override
    {
        std::vector<int> ctrls { 0, 1, 4, 5, 7, 8, 9 };
        if (value(1) >= 0.5f)
        {
            ctrls.insert(ctrls.begin() + 2, 2);
            ctrls.insert(ctrls.begin() + 3, 3);
        }
        if (cachedInfo.valid
            && cachedInfo.cpuCapability == NamCpuCapability::slimmable)
            ctrls.push_back(6);
        return ctrls;
    }

    juce::String keyboardLabel(int control) const override
    {
        switch (control)
        {
            case 0: return "Input Trim";
            case 1: return "Gate";
            case 2: return "Gate Threshold";
            case 3: return "Gate Release";
            case 4: return "Low Cut";
            case 5: return "High Cut";
            case 6: return "CPU Mode";
            case 7: return "Dry";
            case 8: return "Wet";
            case 9: return "Output";
            default: return {};
        }
    }

private:
    NamModelInfo cachedInfo;
    juce::Rectangle<float> gatePillBounds;
    juce::Rectangle<float> cpuPillBounds;
    static constexpr float trackSpacing = 20.0f;
    float trackYOffset = 118.0f;

    int trackParam(int idx) const
    {
        const bool gateOn = value(1) >= 0.5f;
        const int map[] = { 0, 4, 5, 7, 8, 9 };
        const int mapGate[] = { 0, 2, 3, 4, 5, 7, 8, 9 };
        const auto& m = gateOn ? mapGate : map;
        const int n = gateOn ? 8 : 6;
        return idx >= 0 && idx < n ? m[idx] : -1;
    }

    void paintStatus(juce::Graphics& g, juce::Rectangle<float>& area) const
    {
        auto statusArea = area.removeFromTop(60.0f);
        if (!cachedInfo.valid)
        {
            g.setColour(juce::Colour(0xffa8b3c0));
            g.setFont(12.0f);
            g.drawText("No model loaded", statusArea,
                       juce::Justification::centred);
            const auto storedPath = processor.namModelPath(slot);
            if (storedPath.isNotEmpty())
            {
                g.setFont(9.0f);
                g.setColour(juce::Colour(0xffff8866));
                g.drawText("File not found: "
                    + juce::File(storedPath).getFileName(),
                    statusArea.translated(0.0f, 18.0f),
                    juce::Justification::centred);
            }
            else
            {
                g.setFont(10.0f);
                g.drawText("Drop a .nam file or use Load NAM...",
                           statusArea.translated(0.0f, 18.0f),
                           juce::Justification::centred);
            }
            return;
        }
        g.setColour(juce::Colours::white);
        g.setFont(14.0f);
        auto top = statusArea;
        g.drawText(cachedInfo.name, top.removeFromTop(20.0f),
                   juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xffa8b3c0));
        g.setFont(10.0f);
        juce::String kindStr;
        switch (cachedInfo.kind)
        {
            case NamModelKind::unknown: kindStr = "Unknown"; break;
            case NamModelKind::waveNetA2Standard: kindStr = "A2 Standard"; break;
            case NamModelKind::waveNetA2Nano: kindStr = "A2 Nano"; break;
            case NamModelKind::slimmableWaveNet: kindStr = "WaveNet (Slimmable)"; break;
            case NamModelKind::waveNet: kindStr = "WaveNet"; break;
            case NamModelKind::lstm: kindStr = "LSTM"; break;
            case NamModelKind::linear: kindStr = "Linear"; break;
            case NamModelKind::convNet: kindStr = "ConvNet"; break;
            case NamModelKind::container: kindStr = "Container"; break;
        }
        g.drawText("Architecture: " + kindStr, top.removeFromTop(14.0f),
                   juce::Justification::centredLeft);
        if (cachedInfo.expectedSampleRate > 0)
            g.drawText("Expected: "
                + juce::String(cachedInfo.expectedSampleRate / 1000.0, 1)
                + " kHz", top.removeFromTop(14.0f),
                juce::Justification::centredLeft);
    }

    void paintGatePill(juce::Graphics& g, juce::Rectangle<float>& area)
    {
        auto row = area.removeFromTop(20.0f);
        const bool on = value(1) >= 0.5f;
        gatePillBounds = row.removeFromLeft(80.0f);
        g.setColour(on ? accent : juce::Colour(0xff333b44));
        g.fillRoundedRectangle(gatePillBounds, 4.0f);
        g.setColour(juce::Colours::white);
        g.setFont(10.0f);
        g.drawText(on ? "Gate On" : "Gate Off", gatePillBounds,
                   juce::Justification::centred);
    }

    void paintCpuPill(juce::Graphics& g, juce::Rectangle<float>& area)
    {
        auto row = area.removeFromTop(20.0f);
        cpuPillBounds = row.removeFromRight(140.0f);
        juce::String cpuStr;
        bool editable = false;
        if (cachedInfo.valid)
        {
            switch (cachedInfo.cpuCapability)
            {
                case NamCpuCapability::a2Fast:
                    cpuStr = "A2 Fast (fixed)"; break;
                case NamCpuCapability::fullOnly:
                    cpuStr = "Full CPU (fixed)"; break;
                case NamCpuCapability::slimmable:
                    cpuStr = value(6) < 0.5f ? "Slimmed" : "Full CPU";
                    editable = true;
                    break;
            }
        }
        else cpuStr = "—";
        g.setColour(editable ? accent.withAlpha(0.5f) : juce::Colour(0xff333b44));
        g.fillRoundedRectangle(cpuPillBounds, 4.0f);
        g.setColour(juce::Colours::white);
        g.setFont(10.0f);
        g.drawText("CPU: " + cpuStr, cpuPillBounds,
                   juce::Justification::centred);
    }

    void paintTracks(juce::Graphics& g, juce::Rectangle<float>& area) const
    {
        auto drawTrack = [&](const juce::String& label, float norm,
                             juce::Colour colour)
        {
            auto row = area.removeFromTop(16.0f);
            area.removeFromTop(4.0f);
            g.setColour(juce::Colour(0xffa8b3c0));
            g.setFont(10.0f);
            g.drawText(label, row.removeFromLeft(80.0f),
                       juce::Justification::centredRight);
            row.removeFromLeft(8.0f);
            const auto bar = row.reduced(0.0f, 4.0f);
            g.setColour(colour.withAlpha(0.15f));
            g.fillRoundedRectangle(bar, 2.0f);
            g.setColour(colour);
            auto filled = bar;
            filled.setWidth(bar.getWidth() * norm);
            g.fillRoundedRectangle(filled, 2.0f);
        };
        drawTrack("Input", value(0), accent);
        if (value(1) >= 0.5f)
        {
            drawTrack("Gate Thresh", value(2), reductionColour);
            drawTrack("Gate Release", value(3), inputColour);
        }
        drawTrack("Low Cut", value(4), accent.withRotatedHue(0.15f));
        drawTrack("High Cut", value(5), accent.withRotatedHue(0.15f));
        drawTrack("Dry", value(7), inputColour);
        drawTrack("Wet", value(8), outputColour);
        drawTrack("Output", value(9), accent);
    }
};
} // namespace

std::unique_ptr<ModuleView> createNeuralAmpModelerView(EffectGraph& graph)
{
    return std::make_unique<NeuralAmpModelerModuleView>(graph);
}
} // namespace megadsp::ui
