#include "../GraphStyle.h"
#include "../ModuleView.h"
#include "../../modules/FormantForge.h"

#include "ModuleViewCreators.h"

#include <array>
#include <cmath>

namespace megadsp::ui
{
std::unique_ptr<ModuleView> createFormantForgeView(EffectGraph&);

namespace
{
class FormantForgeModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    bool usesFullPanel() const override { return true; }
    void configureAccessibility(juce::Component& component) const override
    {
        component.setTitle("Formant Forge vowel field");
        component.setHelpText(
            "Choose a vocal model with the top pills. Drag the vowel field "
            "directly: left and right set Vowel X, while up and down set Vowel "
            "Y. The outlined cursor is the target; the bright cursor and trail "
            "are captured DSP positions. The adjacent live spectrum marks the "
            "four measured formants. Lower tracks edit formant shift, resonance, "
            "breath, motion, stereo spread, mix, and output. Double-click any "
            "region to restore its default.");
    }
    void paint(juce::Graphics&, juce::Rectangle<float>) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    static juce::Rectangle<float> modelPill(
        juce::Rectangle<float> area, int index)
    {
        auto row = area.removeFromTop(31.0f);
        const auto width = row.getWidth() * 0.25f;
        return juce::Rectangle<float>(
            row.getX() + width * static_cast<float>(index), row.getY(),
            width, row.getHeight()).reduced(3.0f, 2.0f);
    }

    static juce::Rectangle<float> contentBounds(juce::Rectangle<float> area)
    {
        area.removeFromTop(33.0f);
        area.removeFromBottom(area.getHeight() * 0.43f);
        return area.reduced(5.0f);
    }

    static juce::Rectangle<float> vowelBounds(juce::Rectangle<float> area)
    {
        auto content = contentBounds(area);
        return content.removeFromLeft(content.getWidth() * 0.36f)
            .withTrimmedRight(3.0f);
    }

    static juce::Rectangle<float> spectrumBounds(juce::Rectangle<float> area)
    {
        auto content = contentBounds(area);
        content.removeFromLeft(content.getWidth() * 0.36f);
        return content.withTrimmedLeft(3.0f);
    }

    static juce::Rectangle<float> vowelPlotBounds(
        juce::Rectangle<float> area)
    {
        auto bounds = vowelBounds(area);
        bounds.removeFromTop(23.0f);
        return bounds.reduced(7.0f, 5.0f);
    }

    static juce::Rectangle<float> spectrumPlotBounds(
        juce::Rectangle<float> area)
    {
        auto bounds = spectrumBounds(area);
        bounds.removeFromTop(23.0f);
        return bounds.reduced(7.0f, 5.0f);
    }

    static juce::Rectangle<float> trackBounds(
        juce::Rectangle<float> area, int control)
    {
        area.removeFromTop(33.0f);
        auto tracks = area.removeFromBottom(area.getHeight() * 0.43f);
        static constexpr std::array<int, 8> order {
            3, 4, 5, 6, 7, 8, 9, 10
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

    static juce::String compactFrequency(float frequency)
    {
        return frequency >= 1000.0f
            ? juce::String(frequency / 1000.0f, 2) + " kHz"
            : juce::String(frequency, 0) + " Hz";
    }

    void chooseModel(int index);
};

void FormantForgeModuleView::chooseModel(int index)
{
    if (auto* target = parameter(FormantForgeModule::modelControl))
    {
        graph.focusKeyboardControl(FormantForgeModule::modelControl);
        target->beginChangeGesture();
        target->setValueNotifyingHost(
            discreteValue(index, FormantForgeModule::modelCount));
        target->endChangeGesture();
    }
    repaint();
}

void FormantForgeModuleView::mouseDown(const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int index = 0; index < FormantForgeModule::modelCount; ++index)
        if (modelPill(area, index).contains(event.position))
        {
            chooseModel(index);
            return;
        }

    if (vowelPlotBounds(area).contains(event.position))
    {
        dragPrimary = FormantForgeModule::vowelXControl;
        dragSecondary = FormantForgeModule::vowelYControl;
    }
    for (const auto control : { 3, 4, 5, 6, 7, 8, 9, 10 })
        if (dragPrimary < 0
            && trackBounds(area, control).contains(event.position))
            dragPrimary = control;
    if (dragPrimary < 0)
        return;
    beginGestures();
    mouseDrag(event);
}

void FormantForgeModuleView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragPrimary < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    if (dragPrimary == FormantForgeModule::vowelXControl)
    {
        const auto bounds = vowelPlotBounds(area);
        setValue(
            FormantForgeModule::vowelXControl,
            juce::jlimit(
                0.0f, 1.0f,
                (event.position.x - bounds.getX()) / bounds.getWidth()));
        setValue(
            FormantForgeModule::vowelYControl,
            juce::jlimit(
                0.0f, 1.0f,
                (bounds.getBottom() - event.position.y) / bounds.getHeight()));
        dragReadout =
            "VOWEL X  "
            + formatControlValue(
                type, FormantForgeModule::vowelXControl,
                value(FormantForgeModule::vowelXControl))
            + "    VOWEL Y  "
            + formatControlValue(
                type, FormantForgeModule::vowelYControl,
                value(FormantForgeModule::vowelYControl));
        repaint();
        return;
    }

    const auto bounds = trackBounds(area, dragPrimary);
    setValue(
        dragPrimary,
        juce::jlimit(
            0.0f, 1.0f,
            (event.position.x - bounds.getX()) / bounds.getWidth()));
    updateDefaultDragReadout();
}

void FormantForgeModuleView::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    const auto area = getLocalBounds().toFloat().reduced(12.0f);
    for (int index = 0; index < FormantForgeModule::modelCount; ++index)
        if (modelPill(area, index).contains(event.position))
        {
            resetToDefault(FormantForgeModule::modelControl);
            return;
        }
    if (vowelPlotBounds(area).contains(event.position))
    {
        resetToDefault(FormantForgeModule::vowelXControl);
        resetToDefault(FormantForgeModule::vowelYControl);
        return;
    }
    for (const auto control : { 3, 4, 5, 6, 7, 8, 9, 10 })
        if (trackBounds(area, control).contains(event.position))
        {
            resetToDefault(control);
            return;
        }
}

void FormantForgeModuleView::paint(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    static constexpr std::array<const char*, FormantForgeModule::modelCount>
        models { "HUMAN", "TUBE", "CREATURE", "METALLIC" };
    const auto selectedModel = discreteIndex(
        value(FormantForgeModule::modelControl),
        FormantForgeModule::modelCount);
    for (int index = 0; index < FormantForgeModule::modelCount; ++index)
    {
        const auto bounds = modelPill(area, index);
        graphics.setColour(
            index == selectedModel ? accent : juce::Colour(0xff27313d));
        graphics.fillRoundedRectangle(bounds, 6.0f);
        graphics.setColour(
            index == selectedModel
                ? juce::Colour(0xff101820) : juce::Colours::white);
        graphics.setFont(juce::FontOptions(9.2f, juce::Font::bold));
        graphics.drawText(
            models[static_cast<size_t>(index)], bounds.toNearestInt(),
            juce::Justification::centred);
    }

    ContinuousTelemetrySnapshot telemetry;
    const auto hasTelemetry =
        readContinuousTelemetry(telemetry)
        && telemetry.sequence != 0
        && telemetry.valueCount >= FormantForgeModule::telemetryValueCount;
    const auto hasVowelHistory =
        hasTelemetry
        && telemetry.historyValueCount
               >= FormantForgeModule::telemetryHistoryValueCount
        && telemetry.historyCount > 1;

    const auto vowel = vowelBounds(area);
    graphics.setColour(juce::Colour(0xe6121821));
    graphics.fillRoundedRectangle(vowel, 8.0f);
    graphics.setColour(juce::Colours::white.withAlpha(0.80f));
    graphics.setFont(juce::FontOptions(9.2f, juce::Font::bold));
    graphics.drawFittedText(
        hasTelemetry
            ? "VOWEL FIELD  ·  OUTLINE TARGET / BRIGHT DSP"
            : "VOWEL FIELD  ·  DSP TELEMETRY WAITING",
        vowel.withHeight(23.0f).reduced(7.0f, 0.0f).toNearestInt(),
        juce::Justification::centredLeft, 1);

    const auto vowelPlot = vowelPlotBounds(area);
    graphics.setColour(juce::Colours::white.withAlpha(0.10f));
    graphics.drawHorizontalLine(
        juce::roundToInt(vowelPlot.getCentreY()),
        vowelPlot.getX(), vowelPlot.getRight());
    graphics.drawVerticalLine(
        juce::roundToInt(vowelPlot.getCentreX()),
        vowelPlot.getY(), vowelPlot.getBottom());
    graphics.setFont(juce::FontOptions(8.0f, juce::Font::bold));
    graphics.setColour(juce::Colours::white.withAlpha(0.42f));
    graphics.drawText(
        "A", vowelPlot.withWidth(15.0f).toNearestInt(),
        juce::Justification::centredLeft);
    graphics.drawText(
        "E",
        vowelPlot.withX(vowelPlot.getRight() - 15.0f)
            .withWidth(15.0f).toNearestInt(),
        juce::Justification::centredRight);

    if (hasVowelHistory)
    {
        juce::Path path;
        for (std::uint32_t point = 0; point < telemetry.historyCount; ++point)
        {
            const auto x = continuousTelemetryHistoryValue(
                telemetry, FormantForgeModule::vowelXHistory, point);
            const auto y = continuousTelemetryHistoryValue(
                telemetry, FormantForgeModule::vowelYHistory, point);
            const juce::Point<float> position {
                vowelPlot.getX() + juce::jlimit(0.0f, 1.0f, x)
                    * vowelPlot.getWidth(),
                vowelPlot.getBottom() - juce::jlimit(0.0f, 1.0f, y)
                    * vowelPlot.getHeight()
            };
            if (point == 0)
                path.startNewSubPath(position);
            else
                path.lineTo(position);
        }
        graphics.setColour(outputColour.withAlpha(0.42f));
        graphics.strokePath(path, juce::PathStrokeType(1.4f));
    }

    const juce::Point<float> target {
        vowelPlot.getX() + value(FormantForgeModule::vowelXControl)
            * vowelPlot.getWidth(),
        vowelPlot.getBottom() - value(FormantForgeModule::vowelYControl)
            * vowelPlot.getHeight()
    };
    graphics.setColour(juce::Colours::white.withAlpha(0.82f));
    graphics.drawEllipse(
        juce::Rectangle<float>(15.0f, 15.0f).withCentre(target),
        1.5f);
    if (hasTelemetry)
    {
        const juce::Point<float> actual {
            vowelPlot.getX()
                + juce::jlimit(
                      0.0f, 1.0f,
                      telemetry.values[FormantForgeModule::actualVowelX])
                    * vowelPlot.getWidth(),
            vowelPlot.getBottom()
                - juce::jlimit(
                      0.0f, 1.0f,
                      telemetry.values[FormantForgeModule::actualVowelY])
                    * vowelPlot.getHeight()
        };
        graphics.setColour(outputColour);
        graphics.fillEllipse(
            juce::Rectangle<float>(9.0f, 9.0f).withCentre(actual));
    }

    const auto spectrum = spectrumBounds(area);
    graphics.setColour(juce::Colour(0xe6121821));
    graphics.fillRoundedRectangle(spectrum, 8.0f);
    auto formantHeader = spectrum;
    formantHeader = formantHeader.removeFromTop(23.0f);
    for (int formant = 0; formant < FormantForgeModule::formantCount; ++formant)
    {
        const auto width =
            formantHeader.getWidth()
            / static_cast<float>(FormantForgeModule::formantCount);
        const auto cell = juce::Rectangle<float>(
            formantHeader.getX() + width * static_cast<float>(formant),
            formantHeader.getY(), width, formantHeader.getHeight())
                              .reduced(2.0f, 1.0f);
        graphics.setColour(juce::Colour(0xff202a36));
        graphics.fillRoundedRectangle(cell, 4.0f);
        graphics.setColour(
            hasTelemetry ? juce::Colours::white
                         : juce::Colours::white.withAlpha(0.42f));
        graphics.setFont(juce::FontOptions(8.5f, juce::Font::bold));
        const auto frequency = hasTelemetry
            ? telemetry.values[static_cast<size_t>(
                  FormantForgeModule::actualFormant1Hz + formant)]
            : 0.0f;
        graphics.drawFittedText(
            "F" + juce::String(formant + 1) + "  "
                + (hasTelemetry ? compactFrequency(frequency)
                                : juce::String("—")),
            cell.toNearestInt(), juce::Justification::centred, 1);
    }

    const auto spectrumPlot = spectrumPlotBounds(area);
    drawSpectrum(
        graphics, spectrumPlot, inputSpectrum,
        inputColour.withAlpha(0.38f));
    drawSpectrum(
        graphics, spectrumPlot, outputSpectrum,
        outputColour.withAlpha(0.88f));
    if (hasTelemetry)
        for (int formant = 0; formant < FormantForgeModule::formantCount;
             ++formant)
        {
            const auto frequency = juce::jlimit(
                20.0f, 20000.0f,
                telemetry.values[static_cast<size_t>(
                    FormantForgeModule::actualFormant1Hz + formant)]);
            const auto proportion =
                std::log(frequency / 20.0f) / std::log(1000.0f);
            const auto x = spectrumPlot.getX()
                + proportion * spectrumPlot.getWidth();
            graphics.setColour(
                (formant % 2 == 0 ? accent : outputColour)
                    .withAlpha(0.72f));
            graphics.drawVerticalLine(
                juce::roundToInt(x), spectrumPlot.getY(),
                spectrumPlot.getBottom());
        }

    for (const auto control : { 3, 4, 5, 6, 7, 8, 9, 10 })
    {
        const auto bounds = trackBounds(area, control);
        graphics.setColour(juce::Colour(0xff151b24));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour(
            (control >= FormantForgeModule::mixControl
                 ? outputColour : accent).withAlpha(0.58f));
        if (control == FormantForgeModule::formantShiftControl)
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
        graphics.drawFittedText(
            juce::String(controlMetadata(type, control).label) + "  "
                + formatControlValue(type, control, value(control)),
            bounds.reduced(6.0f, 0.0f).toNearestInt(),
            juce::Justification::centred, 1);
    }
}
} // namespace

std::unique_ptr<ModuleView> createFormantForgeView(EffectGraph& graph)
{
    return std::make_unique<FormantForgeModuleView>(graph);
}
} // namespace megadsp::ui
