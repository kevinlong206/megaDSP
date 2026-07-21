#include "EffectGraph.h"

#include "GraphStyle.h"
#include "ModuleView.h"
#include "ModuleViewFactory.h"

#include <algorithm>

namespace megadsp::ui
{
class EffectGraph::GraphAccessibilityHandler final
    : public juce::AccessibilityHandler
{
public:
    explicit GraphAccessibilityHandler(EffectGraph& graphToUse)
        : juce::AccessibilityHandler(
              graphToUse, roleFor(graphToUse),
              makeActions(graphToUse),
              makeInterfaces(graphToUse)),
          graph(graphToUse)
    {
    }

    juce::String getTitle() const override
    {
        const auto control = graph.focusedControlLabel();
        return control.isEmpty()
            ? graph.getTitle()
            : graph.getTitle() + " — " + control;
    }

    juce::String getDescription() const override
    {
        return graph.focusedControlReadout();
    }

    juce::String getHelp() const override
    {
        return graph.getHelpText();
    }

    juce::AccessibleState getCurrentState() const override
    {
        auto state = juce::AccessibilityHandler::getCurrentState();
        if (focusedKind(graph) != ControlKind::toggle)
            return state;
        state = state.withCheckable();
        return graph.moduleView != nullptr && graph.focusedControl >= 0
                   && graph.moduleView->keyboardAccessibilityValue(
                          graph.focusedControl) >= 0.5
            ? state.withChecked() : state;
    }

private:
    class ValueInterface final
        : public juce::AccessibilityValueInterface
    {
    public:
        explicit ValueInterface(EffectGraph& graphToUse)
            : graph(graphToUse)
        {
        }

        bool isReadOnly() const override
        {
            return graph.moduleView == nullptr
                   || graph.focusedControl < 0;
        }

        double getCurrentValue() const override
        {
            if (isReadOnly())
                return 0.0;
            return graph.moduleView->keyboardAccessibilityValue(
                graph.focusedControl);
        }

        juce::String getCurrentValueAsString() const override
        {
            return graph.focusedControlReadout();
        }

        void setValue(double newValue) override
        {
            graph.setFocusedAccessibilityValue(newValue);
        }

        void setValueAsString(
            const juce::String& newValue) override
        {
            graph.setFocusedAccessibilityValueAsString(newValue);
        }

        AccessibleValueRange getRange() const override
        {
            if (isReadOnly())
                return { { 0.0, 1.0 }, 1.0 };
            const auto kind =
                graph.moduleView->keyboardKind(graph.focusedControl);
            if (kind == ControlKind::toggle
                || kind == ControlKind::choice)
                return {};
            return {
                { 0.0,
                  graph.moduleView->keyboardAccessibilityMaximum(
                      graph.focusedControl) },
                graph.moduleView->keyboardAccessibilityInterval(
                    graph.focusedControl)
            };
        }

    private:
        EffectGraph& graph;
    };

    static juce::AccessibilityActions makeActions(
        EffectGraph& graph)
    {
        juce::AccessibilityActions actions;
        actions.addAction(
            juce::AccessibilityActionType::focus,
            [&graph] { graph.grabKeyboardFocus(); });
        const auto kind = focusedKind(graph);
        if (kind == ControlKind::toggle || kind == ControlKind::choice)
            actions.addAction(
                juce::AccessibilityActionType::press,
                [&graph] { graph.activateFocusedControl(); });
        if (kind == ControlKind::toggle)
            actions.addAction(
                juce::AccessibilityActionType::toggle,
                [&graph] { graph.activateFocusedControl(); });
        return actions;
    }

    static ControlKind focusedKind(const EffectGraph& graph)
    {
        return graph.moduleView != nullptr && graph.focusedControl >= 0
            ? graph.moduleView->keyboardKind(graph.focusedControl)
            : ControlKind::rotary;
    }

    static juce::AccessibilityRole roleFor(const EffectGraph& graph)
    {
        switch (focusedKind(graph))
        {
            case ControlKind::toggle:
                return juce::AccessibilityRole::toggleButton;
            case ControlKind::choice:
                return juce::AccessibilityRole::comboBox;
            case ControlKind::rotary:
            case ControlKind::horizontal:
            case ControlKind::level:
                return juce::AccessibilityRole::slider;
        }
        return juce::AccessibilityRole::slider;
    }

    static juce::AccessibilityHandler::Interfaces makeInterfaces(
        EffectGraph& graph)
    {
        return juce::AccessibilityHandler::Interfaces {
            std::make_unique<ValueInterface>(graph)
        };
    }

    EffectGraph& graph;
};

EffectGraph::EffectGraph(MegaDSPAudioProcessor& processorToUse, int slotToUse,
                         ModuleType typeToUse)
    : processor(processorToUse), slot(slotToUse), type(typeToUse),
      fft(graphFftOrder),
      window(graphFftSize, juce::dsp::WindowingFunction<float>::hann)
{
    setOpaque(true);
    moduleView = createModuleView(moduleDefinition(type).presentation, *this);
    jassert(moduleView != nullptr);
    if (moduleView != nullptr)
    {
        moduleView->configureAccessibility(*this);
        setWantsKeyboardFocus(moduleView->usesFullPanel());
        if (moduleView->usesFullPanel())
        {
            setHelpText(
                getHelpText()
                + " Keyboard: Left/Right selects a control; Up/Down edits "
                  "(hold Shift for fine changes); Space or Return activates "
                  "choices and switches; Home restores the selected default. "
                  "Tab/Shift+Tab leaves the graph.");
            refreshFocusedControl(false);
        }
    }
    startTimerHz(30);
}

EffectGraph::~EffectGraph()
{
    endGestures();
}

void EffectGraph::paint(juce::Graphics& graphics)
{
    graphics.fillAll(
        backgroundColour(processor.getBackgroundThemeIndex()).darker(0.18f));
    auto area = getLocalBounds().toFloat().reduced(12.0f);
    drawGrid(graphics, area);
    if (moduleView != nullptr)
        moduleView->paint(graphics, area);
    const auto keyboardReadout =
        hasKeyboardFocus(false) ? focusedControlReadout()
                                : juce::String {};
    const auto readout =
        dragPrimary >= 0 && dragReadout.isNotEmpty()
            ? dragReadout : keyboardReadout;
    if (readout.isNotEmpty())
    {
        auto bubble = area.withSizeKeepingCentre(
            juce::jmin(300.0f, area.getWidth()), 28.0f);
        bubble.setY(area.getY() + 8.0f);
        graphics.setColour(juce::Colour(0xdd242c37));
        graphics.fillRoundedRectangle(bubble, 5.0f);
        graphics.setColour(juce::Colours::white);
        graphics.drawFittedText(
            readout, bubble.reduced(7.0f, 0.0f).toNearestInt(),
            juce::Justification::centred, 1, 0.72f);
    }
    if (hasKeyboardFocus(false))
    {
        graphics.setColour(accent);
        graphics.drawRoundedRectangle(
            getLocalBounds().toFloat().reduced(2.5f), 7.0f, 2.0f);
    }
}

void EffectGraph::mouseDown(const juce::MouseEvent& event)
{
    if (!getLocalBounds().toFloat().reduced(12.0f).contains(event.position))
        return;
    if (usesFullPanel())
        grabKeyboardFocus();
    if (moduleView != nullptr)
    {
        moduleView->mouseDown(event);
        if (dragPrimary >= 0)
        {
            const auto controls = moduleView->keyboardControls();
            if (std::find(controls.begin(), controls.end(), dragPrimary)
                != controls.end()
                && focusedControl != dragPrimary)
            {
                focusedControl = dragPrimary;
                notifyFocusedControlChanged(true);
            }
        }
    }
}

void EffectGraph::mouseDrag(const juce::MouseEvent& event)
{
    if (moduleView != nullptr)
        moduleView->mouseDrag(event);
}

void EffectGraph::mouseUp(const juce::MouseEvent&)
{
    endGestures();
    dragReadout.clear();
    repaint();
}

void EffectGraph::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (moduleView != nullptr)
        moduleView->mouseDoubleClick(event);
}

void EffectGraph::mouseWheelMove(
    const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (moduleView != nullptr)
        moduleView->mouseWheelMove(event, wheel);
}

bool EffectGraph::keyPressed(const juce::KeyPress& key)
{
    if (!usesFullPanel())
        return false;
    const auto modifiers = key.getModifiers();
    if (modifiers.isCommandDown() || modifiers.isCtrlDown()
        || modifiers.isAltDown())
        return false;
    if (key.getKeyCode() == juce::KeyPress::leftKey)
        return moveFocusedControl(-1);
    if (key.getKeyCode() == juce::KeyPress::rightKey)
        return moveFocusedControl(1);
    if (key.getKeyCode() == juce::KeyPress::upKey)
        return adjustFocusedControl(
            1, key.getModifiers().isShiftDown());
    if (key.getKeyCode() == juce::KeyPress::downKey)
        return adjustFocusedControl(
            -1, key.getModifiers().isShiftDown());
    if (key.getKeyCode() == juce::KeyPress::spaceKey
        || key.getKeyCode() == juce::KeyPress::returnKey)
        return activateFocusedControl();
    if (key.getKeyCode() == juce::KeyPress::homeKey)
        return resetFocusedControl();
    return false;
}

void EffectGraph::focusGained(FocusChangeType)
{
    refreshFocusedControl(true);
    repaint();
}

void EffectGraph::focusLost(FocusChangeType)
{
    repaint();
}

bool EffectGraph::isInterestedInFileDrag(const juce::StringArray& files)
{
    const auto supportsImpulseResponse = hasCapability(
        moduleDefinition(type).capabilities,
        ModuleCapability::impulseResponse);
    if (supportsImpulseResponse)
        return processor.getRack().activeModuleHasCapability(
                   slot, ModuleCapability::impulseResponse)
               && files.size() == 1
               && juce::File(files[0]).existsAsFile();
    return moduleView != nullptr
           && moduleView->isInterestedInFileDrag(files);
}

void EffectGraph::filesDropped(
    const juce::StringArray& files, int x, int y)
{
    if (hasCapability(
            moduleDefinition(type).capabilities,
            ModuleCapability::impulseResponse))
    {
        if (!isInterestedInFileDrag(files))
            return;
        const auto result = processor.loadImpulseResponse(
            slot, juce::File(files[0]));
        if (result.failed())
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Could not load impulse response",
                result.getErrorMessage());
        repaint();
        return;
    }
    if (moduleView != nullptr)
        moduleView->filesDropped(files, x, y);
}

bool EffectGraph::usesFullPanel() const
{
    return moduleView != nullptr && moduleView->usesFullPanel();
}

float EffectGraph::value(int control) const
{
    if (auto* raw = processor.parameters.getRawParameterValue(
            controlParameterId(slot, control)))
        return raw->load();
    return 0.5f;
}

juce::RangedAudioParameter* EffectGraph::parameter(int control) const
{
    return processor.parameters.getParameter(
        controlParameterId(slot, control));
}

void EffectGraph::setValue(int control, float normalized)
{
    if (auto* target = parameter(control))
        target->setValueNotifyingHost(
            juce::jlimit(0.0f, 1.0f, normalized));
}

void EffectGraph::beginGestures()
{
    focusKeyboardControl(dragPrimary);
    if (auto* target = parameter(dragPrimary))
        target->beginChangeGesture();
    if (dragSecondary >= 0)
        if (auto* target = parameter(dragSecondary))
            target->beginChangeGesture();
}

void EffectGraph::endGestures()
{
    if (dragPrimary >= 0)
        if (auto* target = parameter(dragPrimary))
            target->endChangeGesture();
    if (dragSecondary >= 0)
        if (auto* target = parameter(dragSecondary))
            target->endChangeGesture();
    dragPrimary = dragSecondary = -1;
}

void EffectGraph::focusKeyboardControl(int control)
{
    if (!usesFullPanel() || moduleView == nullptr || control < 0)
        return;
    const auto controls = moduleView->keyboardControls();
    if (std::find(controls.begin(), controls.end(), control)
            == controls.end()
        || focusedControl == control)
        return;
    focusedControl = control;
    notifyFocusedControlChanged(true);
}

void EffectGraph::timerCallback()
{
    auto& data = processor.getVisualizationData().slotData(slot);
    data.input.copyLatest(inputSamples);
    data.output.copyLatest(outputSamples);
    data.outputLeft.copyLatest(stereoLeftSamples);
    data.outputRight.copyLatest(stereoRightSamples);
    data.gainReduction.copyLatest(gainHistory);
    data.inputLevel.copyLatest(inputLevels);
    data.outputLevel.copyLatest(outputLevels);
    data.gainReductionLevel.copyLatest(gainReductionLevels);
    if (moduleView != nullptr)
        moduleView->timerCallback();
    refreshFocusedControl(true);
    calculateSpectrum(inputSamples, inputSpectrum);
    calculateSpectrum(outputSamples, outputSpectrum);
    repaint();
}

std::unique_ptr<juce::AccessibilityHandler>
EffectGraph::createAccessibilityHandler()
{
    if (!usesFullPanel())
        return std::make_unique<juce::AccessibilityHandler>(
            *this, juce::AccessibilityRole::image);
    return std::make_unique<GraphAccessibilityHandler>(*this);
}

void EffectGraph::refreshFocusedControl(bool notifyAccessibility)
{
    if (!usesFullPanel() || moduleView == nullptr)
    {
        focusedControl = -1;
        return;
    }
    const auto controls = moduleView->keyboardControls();
    const auto previous = focusedControl;
    if (controls.empty())
        focusedControl = -1;
    else if (std::find(
                 controls.begin(), controls.end(), focusedControl)
             == controls.end())
        focusedControl = controls.front();
    if (previous != focusedControl)
    {
        if (notifyAccessibility)
            notifyFocusedControlChanged(true);
        else
            invalidateAccessibilityHandler();
    }
}

bool EffectGraph::moveFocusedControl(int direction)
{
    if (moduleView == nullptr || direction == 0)
        return false;
    const auto controls = moduleView->keyboardControls();
    if (controls.empty())
        return false;
    auto iterator =
        std::find(controls.begin(), controls.end(), focusedControl);
    auto index = iterator == controls.end()
        ? 0
        : static_cast<int>(std::distance(controls.begin(), iterator));
    index = (index + (direction > 0 ? 1
                                    : static_cast<int>(controls.size()) - 1))
            % static_cast<int>(controls.size());
    focusedControl = controls[static_cast<size_t>(index)];
    notifyFocusedControlChanged(true);
    repaint();
    return true;
}

bool EffectGraph::adjustFocusedControl(
    int direction, bool fine)
{
    if (moduleView == nullptr || focusedControl < 0
        || !moduleView->adjustKeyboardControl(
            focusedControl, direction, fine))
        return false;
    notifyFocusedControlChanged(false);
    return true;
}

bool EffectGraph::activateFocusedControl()
{
    if (moduleView == nullptr || focusedControl < 0
        || !moduleView->pressKeyboardControl(focusedControl))
        return false;
    notifyFocusedControlChanged(false);
    return true;
}

bool EffectGraph::resetFocusedControl()
{
    if (moduleView == nullptr || focusedControl < 0
        || !moduleView->resetKeyboardControl(focusedControl))
        return false;
    notifyFocusedControlChanged(false);
    return true;
}

bool EffectGraph::setFocusedAccessibilityValue(double newValue)
{
    if (moduleView == nullptr || focusedControl < 0
        || !moduleView->setKeyboardAccessibilityValue(
            focusedControl, newValue))
        return false;
    notifyFocusedControlChanged(false);
    return true;
}

bool EffectGraph::setFocusedAccessibilityValueAsString(
    const juce::String& newValue)
{
    if (moduleView == nullptr || focusedControl < 0
        || !moduleView->setKeyboardAccessibilityValueAsString(
            focusedControl, newValue))
        return false;
    notifyFocusedControlChanged(false);
    return true;
}

juce::String EffectGraph::focusedControlLabel() const
{
    return moduleView != nullptr && focusedControl >= 0
        ? moduleView->keyboardLabel(focusedControl)
        : juce::String {};
}

juce::String EffectGraph::focusedControlValueText() const
{
    return moduleView != nullptr && focusedControl >= 0
        ? moduleView->keyboardValueText(focusedControl)
        : juce::String {};
}

juce::String EffectGraph::focusedControlReadout() const
{
    const auto label = focusedControlLabel();
    const auto valueText = focusedControlValueText();
    return label.isEmpty() ? juce::String {}
                           : label + ": " + valueText;
}

void EffectGraph::notifyFocusedControlChanged(bool focusedControlChanged)
{
    repaint();
    if (focusedControlChanged)
        invalidateAccessibilityHandler();
    if (auto* handler = getAccessibilityHandler())
    {
        if (focusedControlChanged)
            handler->notifyAccessibilityEvent(
                juce::AccessibilityEvent::titleChanged);
        handler->notifyAccessibilityEvent(
            juce::AccessibilityEvent::valueChanged);
    }
}

void EffectGraph::calculateSpectrum(
    const std::array<float, graphFftSize>& samples,
    std::array<float, graphFftSize / 2>& destination)
{
    fftBuffer.fill(0.0f);
    std::copy(samples.begin(), samples.end(), fftBuffer.begin());
    window.multiplyWithWindowingTable(fftBuffer.data(), graphFftSize);
    fft.performFrequencyOnlyForwardTransform(fftBuffer.data());
    for (int bin = 0; bin < graphFftSize / 2; ++bin)
        destination[static_cast<size_t>(bin)] =
            juce::Decibels::gainToDecibels(
                fftBuffer[static_cast<size_t>(bin)]
                    / static_cast<float>(graphFftSize),
                -100.0f);
}

void EffectGraph::drawGrid(
    juce::Graphics& graphics, juce::Rectangle<float> area)
{
    graphics.setColour(juce::Colour(0xff27303b));
    for (int line = 1; line < 6; ++line)
    {
        const auto x = area.getX() + area.getWidth()
            * static_cast<float>(line) / 6.0f;
        const auto y = area.getY() + area.getHeight()
            * static_cast<float>(line) / 6.0f;
        graphics.drawVerticalLine(
            juce::roundToInt(x), area.getY(), area.getBottom());
        graphics.drawHorizontalLine(
            juce::roundToInt(y), area.getX(), area.getRight());
    }
}
} // namespace megadsp::ui
