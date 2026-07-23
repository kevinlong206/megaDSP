#include "PluginEditor.h"
#include "ui/EffectGraph.h"
#include "ui/GraphStyle.h"
#include "ui/GuiLayout.h"
#include "ui/ModuleBrowser.h"

namespace
{
using megadsp::ui::accent;
using megadsp::ui::backgroundColour;
using megadsp::ui::inputColour;
using megadsp::ui::outputColour;
using megadsp::ui::reductionColour;

class ThemePalette final : public juce::Component
{
public:
    ThemePalette(int selectedTheme,
                 std::function<void(int)> themeChosen)
        : onThemeChosen(std::move(themeChosen))
    {
        setTitle("Background color");
        setDescription("Choose a background color for this megaDSP instance.");
        heading.setText("INSTANCE COLOR", juce::dontSendNotification);
        heading.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        heading.setColour(
            juce::Label::textColourId, juce::Colours::white.withAlpha(0.9f));
        addAndMakeVisible(heading);

        for (int index = 0;
             index < megadsp::ui::backgroundThemeCount(); ++index)
        {
            const auto& theme = megadsp::ui::backgroundTheme(index);
            auto button = std::make_unique<juce::TextButton>(theme.name);
            button->setTitle(theme.name);
            button->setDescription(
                "Use " + juce::String(theme.name) + " as the background color.");
            button->setTooltip(
                "Use " + juce::String(theme.name) + " as the background color.");
            button->setColour(
                juce::TextButton::buttonColourId, theme.colour.brighter(0.25f));
            button->setColour(
                juce::TextButton::buttonOnColourId,
                theme.colour.brighter(0.48f));
            button->setColour(
                juce::TextButton::textColourOffId, juce::Colours::white);
            button->setColour(
                juce::TextButton::textColourOnId, juce::Colours::white);
            button->setToggleState(
                index == selectedTheme, juce::dontSendNotification);
            button->onClick = [this, index]
            {
                onThemeChosen(index);
                if (auto* callout =
                        findParentComponentOfClass<juce::CallOutBox>())
                    callout->dismiss();
            };
            addAndMakeVisible(*button);
            buttons.push_back(std::move(button));
        }
        constexpr auto columns = 4;
        const auto rows =
            (megadsp::ui::backgroundThemeCount() + columns - 1)
            / columns;
        setSize(456, 44 + rows * 44);
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(juce::Colour(0xff151b23));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(10);
        heading.setBounds(area.removeFromTop(24));
        constexpr auto columns = 4;
        const auto rows =
            (megadsp::ui::backgroundThemeCount() + columns - 1)
            / columns;
        const auto buttonHeight = area.getHeight() / rows;
        for (int index = 0; index < static_cast<int>(buttons.size()); ++index)
        {
            const auto column = index % columns;
            const auto row = index / columns;
            auto bounds = juce::Rectangle<int>(
                area.getX() + column * area.getWidth() / columns,
                area.getY() + row * buttonHeight,
                area.getWidth() / columns, buttonHeight);
            buttons[static_cast<size_t>(index)]->setBounds(bounds.reduced(3));
        }
    }

private:
    std::function<void(int)> onThemeChosen;
    juce::Label heading;
    std::vector<std::unique_ptr<juce::TextButton>> buttons;
};

void configureSlider(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 20);
    slider.setColour(juce::Slider::rotarySliderFillColourId, accent);
    slider.setColour(juce::Slider::rotarySliderOutlineColourId,
                     juce::Colour(0xff3a4553));
    slider.setColour(juce::Slider::textBoxOutlineColourId,
                     juce::Colours::transparentBlack);
}

class MeteredSlider final : public juce::Slider,
                            private juce::Timer
{
public:
    void setMeterSource(std::function<float()> source)
    {
        meterSource = std::move(source);
        startTimerHz(30);
    }

    void paint(juce::Graphics& graphics) override
    {
        if (!meterSource)
        {
            juce::Slider::paint(graphics);
            return;
        }

        auto trackArea = getLocalBounds().toFloat().reduced(8.0f, 5.0f);
        trackArea.removeFromBottom(22.0f);
        auto meter = juce::Rectangle<float>(
            18.0f, trackArea.getHeight()).withCentre(trackArea.getCentre());
        graphics.setColour(juce::Colour(0xff141a21));
        graphics.fillRoundedRectangle(meter, 5.0f);
        const auto levelDb = juce::jlimit(-60.0f, 0.0f, meterSource());
        const auto proportion = (levelDb + 60.0f) / 60.0f;
        auto fill = meter;
        fill.setY(meter.getBottom() - meter.getHeight() * proportion);
        fill.setBottom(meter.getBottom());
        graphics.setColour(levelDb > -6.0f ? reductionColour : outputColour);
        graphics.fillRoundedRectangle(fill, 5.0f);

        const auto threshold = static_cast<float>(
            valueToProportionOfLength(getValue()));
        const auto thresholdY =
            meter.getBottom() - threshold * meter.getHeight();
        graphics.setColour(accent);
        graphics.drawHorizontalLine(
            juce::roundToInt(thresholdY),
            meter.getX() - 7.0f, meter.getRight() + 7.0f);
        graphics.fillEllipse(meter.getCentreX() - 5.0f,
                             thresholdY - 5.0f, 10.0f, 10.0f);
        graphics.setColour(juce::Colours::white);
        graphics.drawEllipse(meter.getCentreX() - 5.0f,
                             thresholdY - 5.0f, 10.0f, 10.0f, 1.0f);
    }

private:
    void timerCallback() override { repaint(); }
    std::function<float()> meterSource;
};

class SemanticToggleButton final : public juce::ToggleButton
{
public:
    void setPresentation(
        const megadsp::ui::TogglePresentation& presentation)
    {
        const auto valueChanged = stateText != presentation.stateText;
        if (!valueChanged
            && semanticLabel == presentation.semanticLabel
            && accessibilityDescription
                   == presentation.accessibilityDescription
            && tooltipText == presentation.tooltip)
            return;
        stateText = presentation.stateText;
        semanticLabel = presentation.semanticLabel;
        accessibilityDescription =
            presentation.accessibilityDescription;
        tooltipText = presentation.tooltip;
        setButtonText(presentation.buttonText);
        setTitle(semanticLabel);
        setDescription(accessibilityDescription);
        setTooltip(tooltipText);
        if (valueChanged)
        {
            if (auto* handler = getAccessibilityHandler())
                handler->notifyAccessibilityEvent(
                    juce::AccessibilityEvent::valueChanged);
        }
        repaint();
    }

    void paintButton(juce::Graphics& graphics, bool highlighted,
                     bool down) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(3.0f, 2.0f);
        const auto font =
            juce::Font(juce::FontOptions(12.5f, juce::Font::bold));
        constexpr auto gap = 5.0f;
        const auto tickSize =
            juce::jmin(18.0f, bounds.getHeight() - 2.0f);
        const auto contentWidth = juce::jmin(
            bounds.getWidth(),
            tickSize + gap + juce::GlyphArrangement::getStringWidth(
                                 font, getButtonText()));
        auto content = juce::Rectangle<float>(
            contentWidth, bounds.getHeight())
                           .withCentre(bounds.getCentre());
        auto tick = content.removeFromLeft(tickSize);
        tick.setY(content.getCentreY() - tickSize * 0.5f);
        tick.setHeight(tickSize);
        getLookAndFeel().drawTickBox(
            graphics, *this, tick.getX(), tick.getY(),
            tick.getWidth(), tick.getHeight(), getToggleState(),
            isEnabled(), highlighted, down);

        auto text = content.withTrimmedLeft(gap);
        graphics.setFont(font);
        graphics.setColour(
            isEnabled() ? juce::Colours::white.withAlpha(0.90f)
                        : juce::Colour(0xff687383));
        graphics.drawFittedText(
            getButtonText(), text.toNearestInt(),
            juce::Justification::centredLeft, 1, 0.70f);
        if (hasKeyboardFocus(false))
        {
            graphics.setColour(accent);
            graphics.drawRoundedRectangle(
                getLocalBounds().toFloat().reduced(1.0f), 4.0f, 1.5f);
        }
    }

private:
    class ToggleValueInterface final
        : public juce::AccessibilityTextValueInterface
    {
    public:
        explicit ToggleValueInterface(
            SemanticToggleButton& buttonToUse)
            : button(buttonToUse)
        {
        }

        bool isReadOnly() const override { return true; }
        juce::String getCurrentValueAsString() const override
        {
            return button.stateText;
        }
        void setValueAsString(const juce::String&) override {}

    private:
        SemanticToggleButton& button;
    };

    class ToggleAccessibilityHandler final
        : public juce::AccessibilityHandler
    {
    public:
        explicit ToggleAccessibilityHandler(
            SemanticToggleButton& buttonToUse)
            : juce::AccessibilityHandler(
                  buttonToUse, juce::AccessibilityRole::toggleButton,
                  makeActions(buttonToUse),
                  juce::AccessibilityHandler::Interfaces {
                      std::make_unique<ToggleValueInterface>(
                          buttonToUse) }),
              button(buttonToUse)
        {
        }

        juce::AccessibleState getCurrentState() const override
        {
            auto state =
                juce::AccessibilityHandler::getCurrentState()
                    .withCheckable();
            return button.getToggleState()
                ? state.withChecked() : state;
        }

        juce::String getHelp() const override
        {
            return button.getTooltip();
        }

    private:
        static juce::AccessibilityActions makeActions(
            SemanticToggleButton& button)
        {
            juce::AccessibilityActions actions;
            actions.addAction(
                juce::AccessibilityActionType::press,
                [&button] { button.triggerClick(); });
            actions.addAction(
                juce::AccessibilityActionType::toggle,
                [&button] { button.triggerClick(); });
            return actions;
        }

        SemanticToggleButton& button;
    };

    std::unique_ptr<juce::AccessibilityHandler>
        createAccessibilityHandler() override
    {
        return std::make_unique<ToggleAccessibilityHandler>(*this);
    }

    juce::String stateText;
    juce::String semanticLabel;
    juce::String accessibilityDescription;
    juce::String tooltipText;
};

class ChoiceParameterAttachment final
{
public:
    ChoiceParameterAttachment(juce::RangedAudioParameter& parameter,
                             juce::ComboBox& boxToUse,
                             const juce::StringArray& optionsToUse,
                             std::function<void(int)> stateChanged)
        : box(boxToUse), options(optionsToUse),
          callback(std::move(stateChanged)),
          attachment(parameter,
              [this](float value)
              {
                  const auto index =
                     megadsp::discreteIndex(value, options.size());
                  box.setSelectedItemIndex(
                     index, juce::dontSendNotification);
                  callback(index);
              })
    {
        box.addItemList(options, 1);
        box.onChange = [this]
        {
            attachment.setValueAsCompleteGesture(megadsp::discreteValue(
                box.getSelectedItemIndex(), options.size()));
            callback(box.getSelectedItemIndex());
        };
        attachment.sendInitialUpdate();
    }

    ~ChoiceParameterAttachment() { box.onChange = nullptr; }

private:
    juce::ComboBox& box;
    juce::StringArray options;
    std::function<void(int)> callback;
    juce::ParameterAttachment attachment;
};

class ToggleParameterAttachment final
{
public:
    ToggleParameterAttachment(juce::RangedAudioParameter& parameter,
                             juce::ToggleButton& buttonToUse,
                             std::function<void(bool)> stateChanged)
        : button(buttonToUse), callback(std::move(stateChanged)),
          attachment(parameter,
              [this](float value)
              {
                  const auto state = value >= 0.5f;
                  button.setToggleState(state, juce::dontSendNotification);
                  callback(state);
              })
    {
        button.setClickingTogglesState(true);
        button.onClick = [this]
        {
            const auto state = button.getToggleState();
            attachment.setValueAsCompleteGesture(state ? 1.0f : 0.0f);
            callback(state);
        };
        attachment.sendInitialUpdate();
    }

    ~ToggleParameterAttachment() { button.onClick = nullptr; }

private:
    juce::ToggleButton& button;
    std::function<void(bool)> callback;
    juce::ParameterAttachment attachment;
};
} // namespace

class MegaDSPAudioProcessorEditor::ModuleTab final
    : public juce::Component,
      public juce::DragAndDropTarget,
      public juce::SettableTooltipClient
{
public:
    class CloseButton final : public juce::Button
    {
    public:
        CloseButton() : juce::Button("Remove module") {}

        void paintButton(juce::Graphics& graphics, bool highlighted,
                         bool pressed) override
        {
            const auto colour = pressed ? juce::Colours::white
                              : highlighted ? juce::Colour(0xffff8b8b)
                                            : juce::Colour(0xff9ba6b5);
            graphics.setColour(colour);
            const auto area = getLocalBounds().toFloat().reduced(5.5f);
            graphics.drawLine(
                juce::Line<float>(area.getTopLeft(), area.getBottomRight()),
                2.0f);
            graphics.drawLine(
                juce::Line<float>(area.getTopRight(), area.getBottomLeft()),
                2.0f);
        }
    };

    class BypassButton final : public juce::ToggleButton
    {
    public:
        BypassButton() : juce::ToggleButton("Bypass module") {}

        void paintButton(juce::Graphics& graphics, bool highlighted,
                         bool pressed) override
        {
            const auto isBypassed = getToggleState();
            auto area = getLocalBounds().toFloat().reduced(2.0f);
            auto colour = isBypassed ? juce::Colour(0xffa84b4b)
                                     : juce::Colour(0xff344151);
            if (highlighted || pressed)
                colour = colour.brighter(0.18f);
            graphics.setColour(colour);
            graphics.fillRoundedRectangle(area, 4.0f);
            graphics.setColour(
                isBypassed ? juce::Colours::white
                           : juce::Colour(0xffc9d2dd));
            graphics.setFont(
                juce::FontOptions(10.0f, juce::Font::bold));
            graphics.drawFittedText(
                isBypassed ? "BYP" : "ON", getLocalBounds(),
                juce::Justification::centred, 1, 0.7f);
        }
    };

    class TabAccessibilityHandler final
        : public juce::AccessibilityHandler
    {
    public:
        explicit TabAccessibilityHandler(ModuleTab& tabToUse)
            : juce::AccessibilityHandler(
                  tabToUse, juce::AccessibilityRole::button,
                  makeActions(tabToUse)),
              tab(tabToUse)
        {
        }

        juce::AccessibleState getCurrentState() const override
        {
            auto state =
                juce::AccessibilityHandler::getCurrentState()
                    .withSelectable();
            return tab.selected ? state.withSelected() : state;
        }

        juce::String getTitle() const override
        {
            return tab.name + " effect, slot "
                   + juce::String(tab.slot + 1);
        }

        juce::String getDescription() const override
        {
            return juce::String(tab.selected ? "Selected, " : "")
                   + (tab.bypassed ? "bypassed" : "enabled");
        }

        juce::String getHelp() const override
        {
            return "Press Enter to select this tab. Use Left and Right to "
                   "select adjacent tabs. Press Space or B to toggle bypass.";
        }

    private:
        static juce::AccessibilityActions makeActions(ModuleTab& tab)
        {
            juce::AccessibilityActions actions;
            actions.addAction(
                juce::AccessibilityActionType::press,
                [&tab] { tab.selectAndFocus(); });
            actions.addAction(
                juce::AccessibilityActionType::focus,
                [&tab] { tab.selectAndFocus(); });
            return actions;
        }

        ModuleTab& tab;
    };

    ModuleTab(MegaDSPAudioProcessorEditor& editorToUse, int slotToUse,
              megadsp::ModuleType typeToUse)
        : editor(editorToUse), slot(slotToUse),
          name(megadsp::moduleDefinition(typeToUse).displayName),
          displayName(megadsp::ui::compactTabName(typeToUse))
    {
        close.setTooltip("Remove " + name);
        close.setTitle("Remove " + name);
        close.setDescription("Removes this effect from the rack.");
        close.setHelpText("Press to remove " + name + " from slot "
                          + juce::String(slot + 1) + ".");
        close.onClick = [this] { editor.removeSlot(slot); };
        addAndMakeVisible(close);
        bypass.onClick = [this]
        {
            editor.audioProcessor.toggleSlotBypass(slot);
            syncBypassState();
        };
        addAndMakeVisible(bypass);
        setWantsKeyboardFocus(true);
        setTooltip(name + " — slot " + juce::String(slot + 1));
        setHelpText(
            "Press Enter to select. Use Left and Right to select adjacent "
            "tabs. Double-click, Space, or B toggles bypass. Drag to reorder.");
        syncBypassState();
    }

    void setSelected(bool shouldBeSelected)
    {
        if (selected == shouldBeSelected)
            return;
        selected = shouldBeSelected;
        repaint();
        if (auto* handler = getAccessibilityHandler())
            handler->notifyAccessibilityEvent(
                juce::AccessibilityEvent::valueChanged);
    }

    void syncBypassState()
    {
        const auto newState = editor.audioProcessor.isSlotBypassed(slot);
        bypass.setToggleState(newState, juce::dontSendNotification);
        bypass.setTitle("Bypass " + name);
        bypass.setDescription(
            name + (newState ? " is bypassed." : " is enabled."));
        bypass.setHelpText(
            "Press to " + juce::String(newState ? "enable " : "bypass ")
            + name + ".");
        bypass.setTooltip(
            newState ? "BYP — " + name + " is bypassed"
                     : "ON — " + name + " is enabled");
        if (newState != bypassed)
        {
            bypassed = newState;
            repaint();
            bypass.repaint();
            if (auto* handler = getAccessibilityHandler())
                handler->notifyAccessibilityEvent(
                    juce::AccessibilityEvent::valueChanged);
        }
    }

    void paint(juce::Graphics& graphics) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        const auto theme = backgroundColour(
            editor.audioProcessor.getBackgroundThemeIndex());
        graphics.setColour(theme.brighter(selected ? 0.42f : 0.20f));
        graphics.fillRoundedRectangle(bounds, 6.0f);
        graphics.setColour(selected ? accent : juce::Colour(0xff566273));
        graphics.drawRoundedRectangle(bounds, 6.0f, selected ? 2.0f : 1.0f);
        graphics.setColour(bypassed ? juce::Colour(0xff78818d)
                                    : juce::Colours::white);
        graphics.setFont(juce::FontOptions(12.0f, selected ? juce::Font::bold
                                                          : juce::Font::plain));
        graphics.drawFittedText(
            juce::String(slot + 1) + " " + displayName,
            getLocalBounds().reduced(6, 0).withTrimmedRight(58),
            juce::Justification::centredLeft,
            1, 0.5f);
    }

    void resized() override
    {
        const auto buttonHeight = juce::jmax(24, getHeight() - 8);
        close.setBounds(getWidth() - 30, 4, 26, buttonHeight);
        bypass.setBounds(getWidth() - 58, 4, 26, buttonHeight);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        dragStart = event.position;
        selectAndFocus();
        if (event.getNumberOfClicks() >= 2)
            toggleBypass();
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (event.position.getDistanceFrom(dragStart) > 5.0f
            && !editor.isDragAndDropActive())
            editor.startDragging(slot, this);
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        const auto modifiers = key.getModifiers();
        if (modifiers.isCommandDown() || modifiers.isCtrlDown()
            || modifiers.isAltDown())
            return false;
        if (key.getKeyCode() == juce::KeyPress::returnKey)
        {
            selectAndFocus();
            return true;
        }
        if (key.getKeyCode() == juce::KeyPress::leftKey)
            return selectAdjacent(-1);
        if (key.getKeyCode() == juce::KeyPress::rightKey)
            return selectAdjacent(1);
        if (key.getKeyCode() == juce::KeyPress::spaceKey
            || key.getTextCharacter() == 'b'
            || key.getTextCharacter() == 'B')
        {
            toggleBypass();
            return true;
        }
        return false;
    }

    void focusGained(FocusChangeType) override
    {
        if (!selected)
            editor.selectSlot(slot);
    }

    bool isInterestedInDragSource(const SourceDetails& details) override
    {
        return static_cast<int>(details.description) != slot;
    }

    void itemDragEnter(const SourceDetails&) override
    {
        dragOver = true;
        repaint();
    }

    void itemDragExit(const SourceDetails&) override
    {
        dragOver = false;
        repaint();
    }

    void itemDropped(const SourceDetails& details) override
    {
        dragOver = false;
        editor.reorderSlot(static_cast<int>(details.description), slot);
    }

private:
    std::unique_ptr<juce::AccessibilityHandler>
        createAccessibilityHandler() override
    {
        return std::make_unique<TabAccessibilityHandler>(*this);
    }

    void selectAndFocus()
    {
        editor.selectSlot(slot);
        grabKeyboardFocus();
    }

    bool selectAdjacent(int direction)
    {
        const auto count = static_cast<int>(editor.tabs.size());
        if (count <= 0 || direction == 0)
            return false;
        const auto target =
            juce::jlimit(0, count - 1, slot + (direction < 0 ? -1 : 1));
        auto* targetTab = editor.tabs[static_cast<size_t>(target)].get();
        if (targetTab == nullptr)
            return false;
        if (target == slot)
        {
            grabKeyboardFocus();
            return true;
        }
        targetTab->selectAndFocus();
        return true;
    }

    void toggleBypass()
    {
        editor.audioProcessor.toggleSlotBypass(slot);
        syncBypassState();
    }

    MegaDSPAudioProcessorEditor& editor;
    int slot;
    juce::String name;
    juce::String displayName;
    CloseButton close;
    BypassButton bypass;
    juce::Point<float> dragStart;
    bool selected = false;
    bool dragOver = false;
    bool bypassed = false;
};

class MegaDSPAudioProcessorEditor::HeaderMeter final
    : public juce::Component,
      public juce::SettableTooltipClient
{
public:
    void setLevels(float inputDb, float outputDb)
    {
        updateChannel(0, inputDb);
        updateChannel(1, outputDb);
        if (outputDb >= -0.05f)
            clipped = true;
        repaint();
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        clipped = false;
        repaint();
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.setColour(juce::Colour(0xff151a21));
        graphics.fillRoundedRectangle(getLocalBounds().toFloat(), 5.0f);
        auto bounds = getLocalBounds().reduced(7, 4);
        drawChannel(graphics, bounds.removeFromTop(bounds.getHeight() / 2),
                    "IN", 0);
        drawChannel(graphics, bounds, "OUT", 1);
    }

private:
    void updateChannel(int channel, float level)
    {
        const auto index = static_cast<size_t>(channel);
        levels[index] = juce::jmax(level, levels[index] - 2.0f);
        if (level >= peaks[index])
        {
            peaks[index] = level;
            holdFrames[index] = 23;
        }
        else if (holdFrames[index] > 0)
            --holdFrames[index];
        else
            peaks[index] = juce::jmax(level, peaks[index] - 0.75f);
    }

    static float meterPosition(float db)
    {
        return juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
    }

    static juce::Colour levelColour(float db)
    {
        if (db >= -1.0f)
            return juce::Colour(0xffff4f55);
        if (db >= -6.0f)
            return juce::Colour(0xffffa85b);
        if (db >= -12.0f)
            return juce::Colour(0xffffd166);
        return outputColour;
    }

    void drawChannel(juce::Graphics& graphics, juce::Rectangle<int> row,
                     const juce::String& label, int channel)
    {
        const auto index = static_cast<size_t>(channel);
        graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        graphics.setColour(juce::Colour(0xffa8b3c0));
        graphics.drawText(label, row.removeFromLeft(28),
                          juce::Justification::centredLeft);
        auto readout = row.removeFromRight(channel == 1 ? 70 : 52);
        auto meter = row.reduced(2, 4);
        graphics.setColour(juce::Colour(0xff303845));
        graphics.fillRoundedRectangle(meter.toFloat(), 2.0f);
        const auto meterWidth = static_cast<float>(meter.getWidth());
        const auto fillWidth = meterWidth * meterPosition(levels[index]);
        graphics.setColour(levelColour(levels[index]));
        graphics.fillRoundedRectangle(
            meter.withWidth(juce::roundToInt(fillWidth)).toFloat(), 2.0f);
        for (const auto marker : { -18.0f, -12.0f, -6.0f, -3.0f })
        {
            const auto x = meter.getX()
                + juce::roundToInt(meterWidth * meterPosition(marker));
            graphics.setColour(juce::Colours::black.withAlpha(0.32f));
            graphics.drawVerticalLine(x, static_cast<float>(meter.getY()),
                                      static_cast<float>(meter.getBottom()));
        }
        const auto peakX = meter.getX()
            + juce::roundToInt(meterWidth * meterPosition(peaks[index]));
        graphics.setColour(levelColour(peaks[index]));
        graphics.drawVerticalLine(peakX, static_cast<float>(meter.getY()),
                                  static_cast<float>(meter.getBottom()));
        graphics.setColour(channel == 1 && clipped
                               ? juce::Colour(0xffff4f55)
                               : juce::Colours::white);
        const auto text = channel == 1 && clipped
            ? "CLIP"
            : juce::String(levels[index], 1) + " dB";
        graphics.drawText(text, readout, juce::Justification::centredRight);
    }

    std::array<float, 2> levels { -100.0f, -100.0f };
    std::array<float, 2> peaks { -100.0f, -100.0f };
    std::array<int, 2> holdFrames {};
    bool clipped = false;
};

class MegaDSPAudioProcessorEditor::ModulePanel final
    : public juce::Component,
      private juce::Timer
{
public:
    ModulePanel(MegaDSPAudioProcessor& processorToUse, int slotToUse)
        : processor(processorToUse), slot(slotToUse),
          type(processor.getRack().moduleType(slot)),
          supportsImpulseResponse(
              processor.getRack().activeModuleHasCapability(
                  slot, megadsp::ModuleCapability::impulseResponse))
    {
        name.setText(megadsp::descriptorFor(type).name, juce::dontSendNotification);
        name.setFont(juce::FontOptions(22.0f, juce::Font::bold));
        name.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(name);

        reset.setButtonText("Reset");
        reset.setTooltip("Reset this module to its musical starting point.");
        reset.onClick = [this] { processor.resetModuleToDefaults(slot); };
        addAndMakeVisible(reset);

        if (supportsImpulseResponse)
        {
            loadImpulse.setButtonText("Load IR...");
            loadImpulse.setTooltip(
                "Load a WAV, AIFF, or FLAC impulse response.");
            loadImpulse.onClick = [this]
            {
                impulseChooser = std::make_unique<juce::FileChooser>(
                    "Load impulse response",
                    juce::File::getSpecialLocation(
                        juce::File::userMusicDirectory),
                    "*.wav;*.aif;*.aiff;*.flac");
                const auto flags =
                    juce::FileBrowserComponent::openMode
                    | juce::FileBrowserComponent::canSelectFiles;
                juce::Component::SafePointer<ModulePanel> safeThis(this);
                impulseChooser->launchAsync(
                    flags, [safeThis](const juce::FileChooser& chooser)
                    {
                        if (safeThis == nullptr)
                            return;
                        const auto file = chooser.getResult();
                        if (file == juce::File {})
                            return;
                        const auto result =
                            safeThis->processor.loadImpulseResponse(
                                safeThis->slot, file);
                        if (result.failed())
                            juce::AlertWindow::showMessageBoxAsync(
                                juce::MessageBoxIconType::WarningIcon,
                                "Could not load impulse response",
                                result.getErrorMessage());
                        safeThis->graph->repaint();
                    });
            };
            addAndMakeVisible(loadImpulse);
            clearImpulse.setButtonText("Clear IR");
            clearImpulse.setTooltip(
                "Unload the current impulse response.");
            clearImpulse.onClick = [this]
            {
                processor.clearImpulseResponse(slot);
                graph->repaint();
            };
            addAndMakeVisible(clearImpulse);
        }

        graph = std::make_unique<megadsp::ui::EffectGraph>(
            processor, slot, type);
        addAndMakeVisible(*graph);

        if (graph->usesFullPanel())
            return;

        const auto defaults = megadsp::moduleDefaults(type);
        for (int control = 0; control < megadsp::controlsPerSlot; ++control)
        {
            const auto& metadata = megadsp::controlMetadata(type, control);
            if (juce::String(metadata.label) == "-")
                continue;

            controlPresent[static_cast<size_t>(control)] = true;
            const auto index = static_cast<size_t>(control);
            labels[index].setText(metadata.label, juce::dontSendNotification);
            labels[index].setJustificationType(juce::Justification::centred);
            labels[index].setFont(juce::FontOptions(12.0f));
            labels[index].setTooltip(metadata.tooltip);
            addAndMakeVisible(labels[index]);

            if (metadata.kind == megadsp::ControlKind::choice)
            {
                choices[index].setTitle(metadata.label);
                choices[index].setTooltip(metadata.tooltip);
                addAndMakeVisible(choices[index]);
                choiceAttachments[index] = std::make_unique<ChoiceParameterAttachment>(
                    *processor.parameters.getParameter(
                        megadsp::controlParameterId(slot, control)),
                    choices[index], megadsp::controlOptions(type, control),
                    [this](int) { updateContext(); });
            }
            else if (metadata.kind == megadsp::ControlKind::toggle)
            {
                toggles[index].setTitle(metadata.label);
                addAndMakeVisible(toggles[index]);
                toggleAttachments[index] = std::make_unique<ToggleParameterAttachment>(
                    *processor.parameters.getParameter(
                        megadsp::controlParameterId(slot, control)),
                    toggles[index],
                    [this, control](bool state)
                    {
                        updateToggle(control, state);
                        updateContext();
                    });
            }
            else
            {
                auto& slider = sliders[index];
                configureSlider(slider);
                if (metadata.kind == megadsp::ControlKind::horizontal)
                {
                    slider.setSliderStyle(juce::Slider::LinearHorizontal);
                    slider.setTextBoxStyle(juce::Slider::TextBoxRight,
                                           false, 72, 20);
                }
                else if (metadata.kind == megadsp::ControlKind::level)
                {
                    slider.setSliderStyle(juce::Slider::LinearVertical);
                }
                slider.setTitle(metadata.label);
                slider.setTooltip(metadata.tooltip);
                addAndMakeVisible(slider);
                if (type == megadsp::ModuleType::compressor && control == 0)
                {
                    slider.setMeterSource([this]
                    {
                        std::array<float, 1> latest {};
                        const auto count = processor.getVisualizationData()
                            .slotData(slot).inputLevel.copyLatest(latest);
                        return count > 0 ? latest[0] : -60.0f;
                    });
                }
                slider.setDoubleClickReturnValue(true, defaults[index]);
                sliderAttachments[index] = std::make_unique<SliderAttachment>(
                    processor.parameters,
                    megadsp::controlParameterId(slot, control),
                    slider);
                slider.textFromValueFunction = [this, control](double value)
                {
                    return megadsp::formatControlValue(
                        type, control, static_cast<float>(value));
                };
                slider.valueFromTextFunction = [this, control, &slider](
                                                   const juce::String& text)
                {
                    return static_cast<double>(
                        megadsp::parseControlValue(type, control, text)
                            .value_or(static_cast<float>(slider.getValue())));
                };
                slider.updateText();
            }
        }
        updateContext(true);
        startTimerHz(10);
    }

    void paint(juce::Graphics& graphics) override
    {
        const auto panelColour = backgroundColour(
            processor.getBackgroundThemeIndex()).brighter(0.16f);
        graphics.setColour(panelColour);
        graphics.fillRoundedRectangle(getLocalBounds().toFloat(), 9.0f);
        for (const auto& frame : groupFrames)
        {
            graphics.setColour(frame.main ? accent : juce::Colour(0xff3a4553));
            graphics.drawRoundedRectangle(frame.bounds.toFloat(), 7.0f,
                                          frame.main ? 2.0f : 1.0f);
            auto frameBounds = frame.bounds;
            auto titleBounds = frameBounds.removeFromTop(18).withTrimmedLeft(10);
            graphics.setColour(panelColour);
            graphics.fillRect(titleBounds.withWidth(
                juce::jmin(titleBounds.getWidth(),
                           juce::jmax(44, frame.name.length() * 8))));
            graphics.setColour(frame.main ? accent : juce::Colour(0xff9ba6b5));
            graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            graphics.drawText(frame.name.toUpperCase(), titleBounds,
                              juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(14);
        auto heading = bounds.removeFromTop(34);
        name.setBounds(heading.removeFromLeft(260));
        reset.setBounds(heading.removeFromRight(64).reduced(2));
        if (supportsImpulseResponse)
        {
            clearImpulse.setBounds(
                heading.removeFromRight(72).reduced(2));
            loadImpulse.setBounds(
                heading.removeFromRight(88).reduced(2));
        }
        bounds.removeFromTop(6);
        if (graph->usesFullPanel())
        {
            graph->setBounds(bounds);
            groupFrames.clear();
            return;
        }
        graph->setBounds(bounds.removeFromTop(
            juce::jlimit(170, 270, bounds.getHeight() * 44 / 100)));
        bounds.removeFromTop(8);
        groupFrames.clear();

        std::vector<int> mainControls;
        std::vector<std::pair<juce::String, std::vector<int>>> secondaryGroups;
        for (int control = 0; control < megadsp::controlsPerSlot; ++control)
        {
            if (!controlVisible[static_cast<size_t>(control)])
                continue;
            const auto& metadata = megadsp::controlMetadata(type, control);
            if (metadata.essential)
            {
                mainControls.push_back(control);
                continue;
            }
            const juce::String groupName(metadata.group);
            auto group = std::find_if(
                secondaryGroups.begin(), secondaryGroups.end(),
                [&groupName](const auto& candidate)
                {
                    return candidate.first == groupName;
                });
            if (group == secondaryGroups.end())
            {
                secondaryGroups.emplace_back(groupName, std::vector<int> {});
                group = std::prev(secondaryGroups.end());
            }
            group->second.push_back(control);
        }
        if (supportsImpulseResponse
            && mainControls.size() == 2
            && mainControls[0] == 2 && mainControls[1] == 4)
            std::swap(mainControls[0], mainControls[1]);

        const auto mainHeight = juce::jmax(92, bounds.getHeight() / 2);
        auto mainBounds = bounds.removeFromTop(mainHeight).reduced(1);
        groupFrames.push_back({ "Main", mainBounds, true });
        layoutGroup(mainBounds.reduced(8, 10), mainControls);
        bounds.removeFromTop(5);

        auto secondaryBounds = bounds;
        const auto groupCount = static_cast<int>(secondaryGroups.size());
        for (int groupIndex = 0; groupIndex < groupCount; ++groupIndex)
        {
            const auto groupsLeft = groupCount - groupIndex;
            auto groupBounds = secondaryBounds.removeFromLeft(
                secondaryBounds.getWidth() / groupsLeft).reduced(2, 1);
            groupFrames.push_back({
                secondaryGroups[static_cast<size_t>(groupIndex)].first,
                groupBounds, false
            });
            layoutGroup(
                groupBounds.reduced(7, 10),
                secondaryGroups[static_cast<size_t>(groupIndex)].second);
        }
    }

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    void updateToggle(int control, bool state)
    {
        auto& button = toggles[static_cast<size_t>(control)];
        juce::String unavailableReason;
        if (((type == megadsp::ModuleType::compressor && control == 7)
             || (type == megadsp::ModuleType::dynamicEqualizer
                 && control == 9)
             || (type == megadsp::ModuleType::gateExpander
                 && control == 8))
            && !processor.hasExternalSidechain())
            unavailableReason =
                "External sidechain is unavailable; the processor "
                "continues to use its internal detector.";
        else if (type == megadsp::ModuleType::delay && control == 4
                 && !processor.hasStereoOutput())
            unavailableReason =
                "Ping Pong is unavailable without stereo output.";
        button.setPresentation(megadsp::ui::togglePresentation(
            type, control, state, unavailableReason));
    }

    std::array<float, megadsp::controlsPerSlot> currentValues() const
    {
        std::array<float, megadsp::controlsPerSlot> values {};
        for (int control = 0; control < megadsp::controlsPerSlot; ++control)
        {
            if (auto* raw = processor.parameters.getRawParameterValue(
                    megadsp::controlParameterId(slot, control)))
                values[static_cast<size_t>(control)] = raw->load();
        }
        return values;
    }

    void updateContext(bool forceLayout = false)
    {
        const auto values = currentValues();
        const auto stereo = processor.hasStereoOutput();
        const auto sidechain = processor.hasExternalSidechain();
        bool layoutChanged = forceLayout;
        for (int control = 0; control < megadsp::controlsPerSlot; ++control)
        {
            const auto index = static_cast<size_t>(control);
            const auto visible = controlPresent[index]
                && megadsp::isControlContextuallyVisible(
                    type, control, values);
            const auto enabled = visible
                && megadsp::isControlContextuallyEnabled(
                    type, control, values, stereo, sidechain);
            layoutChanged = layoutChanged
                            || visible != controlVisible[index];
            controlVisible[index] = visible;
            controlEnabled[index] = enabled;

            const auto sliderVisible =
                visible && sliderAttachments[index] != nullptr;
            const auto choiceVisible =
                visible && choiceAttachments[index] != nullptr;
            const auto toggleVisible =
                visible && toggleAttachments[index] != nullptr;
            sliders[index].setVisible(sliderVisible);
            choices[index].setVisible(choiceVisible);
            toggles[index].setVisible(toggleVisible);
            labels[index].setVisible(visible && !toggleVisible);
            sliders[index].setEnabled(enabled);
            choices[index].setEnabled(enabled);
            toggles[index].setEnabled(enabled);
            labels[index].setEnabled(enabled);
            sliders[index].setWantsKeyboardFocus(sliderVisible && enabled);
            choices[index].setWantsKeyboardFocus(choiceVisible && enabled);
            toggles[index].setWantsKeyboardFocus(toggleVisible && enabled);
            auto labelColour = enabled
                ? juce::Colours::white.withAlpha(0.86f)
                : juce::Colour(0xffa0aab8);
            if (type == megadsp::ModuleType::compressor
                && control == 5 && visible && enabled)
                labelColour = accent;
            labels[index].setColour(
                juce::Label::textColourId, labelColour);
            if (toggleVisible)
                updateToggle(control, values[index] >= 0.5f);
        }

        if (layoutChanged)
            resized();
        repaint();
    }

    void timerCallback() override { updateContext(); }

    void layoutGroup(juce::Rectangle<int> area,
                     const std::vector<int>& controls)
    {
        if (controls.empty())
            return;
        const auto columns = static_cast<int>(controls.size());
        for (int position = 0; position < static_cast<int>(controls.size()); ++position)
        {
            const auto control = controls[static_cast<size_t>(position)];
            const auto index = static_cast<size_t>(control);
            auto controlArea = juce::Rectangle<int>(
                area.getX() + position * area.getWidth() / columns,
                area.getY(), area.getWidth() / columns,
                area.getHeight()).reduced(3, 1);
            if (toggleAttachments[index] != nullptr)
            {
                labels[index].setBounds({});
                const auto buttonWidth =
                    juce::jmin(220, juce::jmax(
                        1, controlArea.getWidth() - 8));
                toggles[index].setBounds(
                    juce::Rectangle<int>(
                        buttonWidth,
                        juce::jmin(34, controlArea.getHeight()))
                        .withCentre(controlArea.getCentre()));
                continue;
            }
            labels[index].setBounds(controlArea.removeFromTop(22));
            if (choiceAttachments[index] != nullptr)
                choices[index].setBounds(
                    controlArea.removeFromTop(32).reduced(4, 2));
            else
            {
                if (megadsp::controlMetadata(type, control).kind
                        == megadsp::ControlKind::horizontal)
                {
                    sliders[index].setTextBoxStyle(
                        controlArea.getWidth() < 120
                            ? juce::Slider::TextBoxBelow
                            : juce::Slider::TextBoxRight,
                        false, 72, 20);
                }
                sliders[index].setBounds(controlArea);
            }
        }
    }

    MegaDSPAudioProcessor& processor;
    int slot;
    megadsp::ModuleType type;
    bool supportsImpulseResponse = false;
    juce::Label name;
    juce::TextButton reset;
    juce::TextButton loadImpulse;
    juce::TextButton clearImpulse;
    std::unique_ptr<juce::FileChooser> impulseChooser;
    std::unique_ptr<megadsp::ui::EffectGraph> graph;
    struct GroupFrame
    {
        juce::String name;
        juce::Rectangle<int> bounds;
        bool main = false;
    };
    std::vector<GroupFrame> groupFrames;
    std::array<MeteredSlider, megadsp::controlsPerSlot> sliders;
    std::array<juce::ComboBox, megadsp::controlsPerSlot> choices;
    std::array<SemanticToggleButton, megadsp::controlsPerSlot> toggles;
    std::array<juce::Label, megadsp::controlsPerSlot> labels;
    std::array<bool, megadsp::controlsPerSlot> controlPresent {};
    std::array<bool, megadsp::controlsPerSlot> controlVisible {};
    std::array<bool, megadsp::controlsPerSlot> controlEnabled {};
    std::array<std::unique_ptr<SliderAttachment>, megadsp::controlsPerSlot>
        sliderAttachments;
    std::array<std::unique_ptr<ChoiceParameterAttachment>,
               megadsp::controlsPerSlot> choiceAttachments;
    std::array<std::unique_ptr<ToggleParameterAttachment>,
               megadsp::controlsPerSlot> toggleAttachments;
};

MegaDSPAudioProcessorEditor::MegaDSPAudioProcessorEditor(
    MegaDSPAudioProcessor& processorToUse, int width, int height)
    : AudioProcessorEditor(processorToUse), audioProcessor(processorToUse)
{
    title.setText("megaDSP", juce::dontSendNotification);
    title.setFont(juce::FontOptions(26.0f, juce::Font::bold));
    title.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(title);

    instanceLabel.setText("INSTANCE", juce::dontSendNotification);
    instanceLabel.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    instanceLabel.setJustificationType(juce::Justification::centredRight);
    instanceLabel.setColour(
        juce::Label::textColourId, juce::Colour(0xffa8b3c0));
    addAndMakeVisible(instanceLabel);

    instanceName.setTitle("Instance name");
    instanceName.setDescription(
        "A saved label for identifying this megaDSP instance.");
    instanceName.setTooltip(
        "Name this plugin instance, for example Vocal or Guitar.");
    instanceName.setMultiLine(false);
    instanceName.setReturnKeyStartsNewLine(false);
    instanceName.setInputRestrictions(
        megadsp::ui::instanceNameMaximumLength);
    instanceName.setTextToShowWhenEmpty(
        "Untitled", juce::Colour(0xff7f8b99));
    instanceName.setColour(
        juce::TextEditor::backgroundColourId, juce::Colour(0xff111821));
    instanceName.setColour(
        juce::TextEditor::outlineColourId, juce::Colour(0xff3a4553));
    instanceName.setColour(
        juce::TextEditor::focusedOutlineColourId, accent);
    instanceName.setText(
        audioProcessor.getInstanceName(), juce::dontSendNotification);
    instanceName.onReturnKey = [this]
    {
        commitInstanceName();
        instanceName.giveAwayKeyboardFocus();
    };
    instanceName.onEscapeKey = [this]
    {
        instanceName.setText(
            audioProcessor.getInstanceName(), juce::dontSendNotification);
        instanceName.giveAwayKeyboardFocus();
    };
    instanceName.onFocusLost = [this] { commitInstanceName(); };
    addAndMakeVisible(instanceName);

    themeButton.setTitle("Instance color");
    themeButton.setDescription(
        "Choose the background color for this megaDSP instance.");
    themeButton.onClick = [this] { showThemePalette(); };
    addAndMakeVisible(themeButton);

    status.setJustificationType(juce::Justification::centredRight);
    status.setColour(juce::Label::textColourId, juce::Colour(0xff9ba6b5));
    addAndMakeVisible(status);
    saveButton.onClick = [this] { choosePreset(true); };
    loadButton.onClick = [this] { choosePreset(false); };
    addAndMakeVisible(saveButton);
    addAndMakeVisible(loadButton);

    factoryPresets.addItemList(
        { "Init", "Vocal Polish", "Drum Smash", "Wide Delay", "Ambient Hall" }, 1);
    factoryPresets.setTextWhenNothingSelected("Factory Racks");
    factoryPresets.onChange = [this]
    {
        const auto preset = factoryPresets.getSelectedItemIndex();
        if (preset < 0)
            return;
        audioProcessor.loadFactoryPreset(preset);
        factoryPresets.setSelectedItemIndex(
            -1, juce::dontSendNotification);
        selectSlot(0);
        refreshTabs();
    };
    addAndMakeVisible(factoryPresets);

    refreshIdentityPresentation();

    configureSlider(inputGain);
    configureSlider(outputGain);
    inputGain.setSliderStyle(juce::Slider::LinearHorizontal);
    outputGain.setSliderStyle(juce::Slider::LinearHorizontal);
    inputGain.setTextBoxStyle(juce::Slider::TextBoxRight, false, 58, 22);
    outputGain.setTextBoxStyle(juce::Slider::TextBoxRight, false, 58, 22);
    inputGain.setTextValueSuffix(" dB");
    outputGain.setTextValueSuffix(" dB");
    inputLabel.setText("INPUT", juce::dontSendNotification);
    outputLabel.setText("OUT TRIM", juce::dontSendNotification);
    inputLabel.setColour(juce::Label::textColourId, juce::Colour(0xffa8b3c0));
    outputLabel.setColour(juce::Label::textColourId, juce::Colour(0xffa8b3c0));
    addAndMakeVisible(inputGain);
    addAndMakeVisible(outputGain);
    addAndMakeVisible(inputLabel);
    addAndMakeVisible(outputLabel);
    inputAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, "global.input", inputGain);
    outputAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, "global.output", outputGain);
    headerMeter = std::make_unique<HeaderMeter>();
    headerMeter->setTooltip("Input and final output sample peaks. Click to clear CLIP.");
    addAndMakeVisible(*headerMeter);

    addAndMakeVisible(tabHost);
    addButton.setTooltip("Browse and add a module.");
    addButton.onClick = [this] { showModuleBrowser(); };
    tabHost.addAndMakeVisible(addButton);
    selectedSlot = audioProcessor.getSelectedTab();
    refreshTabs();

    setResizable(true, true);
    setResizeLimits(
        megadsp::ui::editorMinimumWidth,
        megadsp::ui::editorMinimumHeight,
        megadsp::ui::editorMaximumWidth,
        megadsp::ui::editorMaximumHeight);
    setSize(
        juce::jlimit(
            megadsp::ui::editorMinimumWidth,
            megadsp::ui::editorMaximumWidth, width),
        juce::jlimit(
            megadsp::ui::editorMinimumHeight,
            megadsp::ui::editorMaximumHeight, height));
#if defined(MEGADSP_CAPTURE_SCREENSHOTS)
    setSize(1100, 720);
    audioProcessor.setInstanceName("Vocal Chain");
    audioProcessor.setBackgroundThemeIndex(0);
    audioProcessor.loadFactoryPreset(1);
    selectedSlot = 0;
    refreshTabs();
#endif
    startTimerHz(15);
}

MegaDSPAudioProcessorEditor::~MegaDSPAudioProcessorEditor()
{
    fileChooser.reset();
    audioProcessor.getVisualizationData().setSelectedSlot(-1);
}

void MegaDSPAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    const auto theme = backgroundColour(
        audioProcessor.getBackgroundThemeIndex());
    graphics.fillAll(theme);
    graphics.setColour(theme.brighter(0.22f));
    graphics.fillRect(0, 0, getWidth(), 92);
}

void MegaDSPAudioProcessorEditor::resized()
{
    if (getWidth() > 0 && getHeight() > 0)
        audioProcessor.rememberEditorSize(getWidth(), getHeight());
    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop(92).reduced(10, 5);
    auto headerTop = header.removeFromTop(34);
    const auto identityLayout =
        megadsp::ui::calculateIdentityHeaderLayout(getWidth());
    title.setBounds(
        headerTop.removeFromLeft(identityLayout.titleWidth));
    instanceLabel.setBounds(
        headerTop.removeFromLeft(identityLayout.labelWidth));
    instanceName.setBounds(
        headerTop.removeFromLeft(identityLayout.nameWidth).reduced(2));
    themeButton.setBounds(
        headerTop.removeFromLeft(identityLayout.themeWidth).reduced(2));
    factoryPresets.setBounds(
        headerTop.removeFromLeft(identityLayout.presetWidth).reduced(2));
    saveButton.setBounds(
        headerTop.removeFromLeft(identityLayout.saveWidth).reduced(2));
    loadButton.setBounds(
        headerTop.removeFromLeft(identityLayout.loadWidth).reduced(2));
    status.setBounds(headerTop);

    auto headerBottom = header.reduced(0, 2);
    auto inputArea = headerBottom.removeFromLeft(185);
    inputLabel.setBounds(inputArea.removeFromLeft(42));
    inputGain.setBounds(inputArea);
    auto outputArea = headerBottom.removeFromRight(205);
    outputLabel.setBounds(outputArea.removeFromLeft(70));
    outputGain.setBounds(outputArea);
    headerMeter->setBounds(headerBottom.reduced(8, 0));

    tabHost.setBounds(bounds.removeFromTop(52).reduced(10, 5));
    auto tabBounds = tabHost.getLocalBounds();
    const auto active = static_cast<int>(tabs.size());
    const auto tabLayout = megadsp::ui::calculateTabLayout(
        tabBounds.getWidth(), active, active < megadsp::slotCount);
    for (auto& tab : tabs)
        tab->setBounds(
            tabBounds.removeFromLeft(tabLayout.tabWidth).reduced(2));
    if (tabLayout.addButtonWidth > 0)
        addButton.setBounds(
            tabBounds.removeFromLeft(tabLayout.addButtonWidth).reduced(2));

    if (modulePanel != nullptr)
        modulePanel->setBounds(bounds.reduced(10, 6));
}

void MegaDSPAudioProcessorEditor::timerCallback()
{
    headerMeter->setLevels(audioProcessor.getRack().inputMeterDb(),
                           audioProcessor.getRack().outputMeterDb());
    for (auto& tab : tabs)
        tab->syncBypassState();
    refreshIdentityPresentation();
    if (knownActiveSlots != audioProcessor.getRack().activeSlotCount()
        || knownTopologyGeneration
               != audioProcessor.getTopologyGeneration())
        refreshTabs();
#if defined(MEGADSP_CAPTURE_SCREENSHOTS)
    advanceScreenshotCapture();
#endif
}

void MegaDSPAudioProcessorEditor::selectSlot(int slot)
{
    const auto active = audioProcessor.getRack().activeSlotCount();
    if (active <= 0)
    {
        selectedSlot = 0;
        modulePanel.reset();
        audioProcessor.getVisualizationData().setSelectedSlot(-1);
        return;
    }
    selectedSlot = juce::jlimit(0, active - 1, slot);
    audioProcessor.setSelectedTab(selectedSlot);
    modulePanel = std::make_unique<ModulePanel>(audioProcessor, selectedSlot);
    addAndMakeVisible(*modulePanel);
    for (int index = 0; index < static_cast<int>(tabs.size()); ++index)
        tabs[static_cast<size_t>(index)]->setSelected(index == selectedSlot);
    resized();
}

void MegaDSPAudioProcessorEditor::refreshTabs()
{
    tabs.clear();
    const auto active = audioProcessor.getRack().activeSlotCount();
    knownActiveSlots = active;
    knownTopologyGeneration = audioProcessor.getTopologyGeneration();
    for (int slot = 0; slot < active; ++slot)
    {
        auto tab = std::make_unique<ModuleTab>(
            *this, slot, audioProcessor.getRack().moduleType(slot));
        tabHost.addAndMakeVisible(*tab);
        tabs.push_back(std::move(tab));
    }
    addButton.setVisible(active < megadsp::slotCount);
    selectSlot(juce::jmin(selectedSlot, juce::jmax(0, active - 1)));
    resized();
}

void MegaDSPAudioProcessorEditor::showModuleBrowser()
{
    if (audioProcessor.getRack().activeSlotCount() >= megadsp::slotCount)
        return;

    constexpr int searchCommand = 1;
    constexpr int moduleCommandBase = 100;
    juce::PopupMenu menu;
    menu.addItem(searchCommand, "Search Modules...");
    menu.addSeparator();
    for (const auto& group : megadsp::ui::filterAndGroupModules({}))
    {
        juce::PopupMenu category;
        for (const auto type : group.modules)
            category.addItem(
                moduleCommandBase + static_cast<int>(type),
                megadsp::moduleDefinition(type).displayName);
        menu.addSubMenu(
            megadsp::moduleCategoryName(group.category), category);
    }

    juce::Component::SafePointer<MegaDSPAudioProcessorEditor> safeThis(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&addButton),
        [safeThis](int result)
        {
            if (safeThis == nullptr || result == 0)
                return;
            if (result == searchCommand)
            {
                safeThis->showModuleSearch();
                return;
            }
            if (result < moduleCommandBase)
                return;
            const auto stableType = result - moduleCommandBase;
            if (!juce::isPositiveAndBelow(
                    stableType, megadsp::moduleTypeCount)
                || safeThis->audioProcessor.getRack().activeSlotCount()
                       >= megadsp::slotCount)
                return;
            safeThis->audioProcessor.addModule(
                static_cast<megadsp::ModuleType>(stableType));
            safeThis->selectedSlot =
                safeThis->audioProcessor.getRack().activeSlotCount() - 1;
            safeThis->refreshTabs();
        });
}

void MegaDSPAudioProcessorEditor::showModuleSearch()
{
    if (audioProcessor.getRack().activeSlotCount() >= megadsp::slotCount)
        return;

    juce::Component::SafePointer<MegaDSPAudioProcessorEditor> safeThis(this);
    auto browser = std::make_unique<megadsp::ui::ModuleBrowser>(
        backgroundColour(audioProcessor.getBackgroundThemeIndex()),
        [safeThis](megadsp::ModuleType type)
        {
            if (safeThis == nullptr
                || safeThis->audioProcessor.getRack().activeSlotCount()
                       >= megadsp::slotCount)
                return;
            safeThis->audioProcessor.addModule(type);
            safeThis->selectedSlot =
                safeThis->audioProcessor.getRack().activeSlotCount() - 1;
            safeThis->refreshTabs();
        });
    auto* browserPointer = browser.get();
    auto& callout = juce::CallOutBox::launchAsynchronously(
        std::move(browser), getLocalArea(&addButton, addButton.getLocalBounds()),
        this);
    callout.setDismissalMouseClicksAreAlwaysConsumed(true);
    browserPointer->focusSearch();
}

void MegaDSPAudioProcessorEditor::commitInstanceName()
{
    audioProcessor.setInstanceName(instanceName.getText());
    instanceName.setText(
        audioProcessor.getInstanceName(), juce::dontSendNotification);
}

void MegaDSPAudioProcessorEditor::showThemePalette()
{
    juce::Component::SafePointer<MegaDSPAudioProcessorEditor> safeThis(this);
    auto palette = std::make_unique<ThemePalette>(
        audioProcessor.getBackgroundThemeIndex(),
        [safeThis](int index)
        {
            if (safeThis == nullptr)
                return;
            safeThis->audioProcessor.setBackgroundThemeIndex(index);
            safeThis->refreshIdentityPresentation();
            safeThis->repaint();
        });
    auto& callout = juce::CallOutBox::launchAsynchronously(
        std::move(palette),
        getLocalArea(&themeButton, themeButton.getLocalBounds()), this);
    callout.setDismissalMouseClicksAreAlwaysConsumed(true);
}

void MegaDSPAudioProcessorEditor::refreshIdentityPresentation()
{
    if (!instanceName.hasKeyboardFocus(true))
    {
        const auto savedName = audioProcessor.getInstanceName();
        if (instanceName.getText() != savedName)
            instanceName.setText(savedName, juce::dontSendNotification);
    }

    const auto themeIndex = audioProcessor.getBackgroundThemeIndex();
    const auto& theme = megadsp::ui::backgroundTheme(themeIndex);
    themeButton.setTooltip(
        "Instance color: " + juce::String(theme.name)
        + ". Click to choose another background color.");
    themeButton.setColour(
        juce::TextButton::buttonColourId, theme.colour.brighter(0.32f));
    themeButton.setColour(
        juce::TextButton::buttonOnColourId, theme.colour.brighter(0.50f));
    themeButton.repaint();
    if (themeIndex != knownThemeIndex)
    {
        knownThemeIndex = themeIndex;
        repaint();
        if (modulePanel != nullptr)
            modulePanel->repaint();
    }
}

#if defined(MEGADSP_CAPTURE_SCREENSHOTS)
void MegaDSPAudioProcessorEditor::captureScreenshot(
    const juce::String& fileName)
{
    const auto outputDirectory = juce::File(
        juce::SystemStats::getEnvironmentVariable(
            "MEGADSP_SCREENSHOT_DIR", {}));
    if (outputDirectory == juce::File())
        return;
    outputDirectory.createDirectory();
    auto image = screenshotOverlay != nullptr
                     && fileName == "module-browser.png"
        ? screenshotOverlay->createComponentSnapshot(
              screenshotOverlay->getLocalBounds(), true, 1.0f)
        : createComponentSnapshot(getLocalBounds(), true, 1.0f);
    auto stream = outputDirectory.getChildFile(fileName).createOutputStream();
    if (stream != nullptr)
        juce::PNGImageFormat().writeImageToStream(image, *stream);
}

void MegaDSPAudioProcessorEditor::advanceScreenshotCapture()
{
    if (++screenshotDelay < 8)
        return;
    screenshotDelay = 0;

    switch (screenshotPhase++)
    {
        case 0:
            captureScreenshot("rack-overview.png");
            selectSlot(1);
            break;
        case 1:
            captureScreenshot("visual-processing.png");
            audioProcessor.setInstanceName("Ambient Space");
            audioProcessor.setBackgroundThemeIndex(4);
            audioProcessor.loadFactoryPreset(4);
            selectedSlot = 1;
            refreshTabs();
            break;
        case 2:
            captureScreenshot("reverb-modulation.png");
            audioProcessor.setInstanceName("Creative Bus");
            audioProcessor.setBackgroundThemeIndex(5);
            refreshIdentityPresentation();
            screenshotOverlay =
                std::make_unique<megadsp::ui::ModuleBrowser>(
                    backgroundColour(
                        audioProcessor.getBackgroundThemeIndex()),
                    [](megadsp::ModuleType) {});
            screenshotOverlay->setBounds(
                getLocalBounds().withSizeKeepingCentre(680, 520));
            addAndMakeVisible(*screenshotOverlay);
            screenshotOverlay->toFront(false);
            captureScreenshot("module-browser.png");
            break;
        case 3:
            juce::JUCEApplicationBase::quit();
            break;
        default:
            break;
    }
}
#endif

void MegaDSPAudioProcessorEditor::removeSlot(int slot)
{
    audioProcessor.removeSlot(slot);
    selectedSlot = juce::jmin(slot,
        juce::jmax(0, audioProcessor.getRack().activeSlotCount() - 1));
    refreshTabs();
}

void MegaDSPAudioProcessorEditor::reorderSlot(int source, int destination)
{
    audioProcessor.moveSlot(source, destination);
    selectedSlot = destination;
    refreshTabs();
}

void MegaDSPAudioProcessorEditor::choosePreset(bool save)
{
    const auto directory = juce::File::getSpecialLocation(
                               juce::File::userApplicationDataDirectory)
                               .getChildFile("megaDSP")
                               .getChildFile("Presets");
    directory.createDirectory();
    fileChooser = std::make_unique<juce::FileChooser>(
        save ? "Save megaDSP preset" : "Load megaDSP preset",
        directory, "*.megadsp");
    const auto flags = save
                           ? juce::FileBrowserComponent::saveMode
                                 | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::warnAboutOverwriting
                           : juce::FileBrowserComponent::openMode
                                 | juce::FileBrowserComponent::canSelectFiles;
    juce::Component::SafePointer<MegaDSPAudioProcessorEditor> safeThis(this);
    fileChooser->launchAsync(flags, [safeThis, save](const juce::FileChooser& chooser)
    {
        if (safeThis == nullptr)
            return;
        auto file = chooser.getResult();
        if (file == juce::File())
            return;
        if (save && !file.hasFileExtension("megadsp"))
            file = file.withFileExtension("megadsp");
        safeThis->showResult(
            save ? safeThis->audioProcessor.savePreset(file)
                 : safeThis->audioProcessor.loadPreset(file));
        safeThis->refreshTabs();
    });
}

void MegaDSPAudioProcessorEditor::showResult(const juce::Result& result)
{
    status.setText(result.wasOk() ? "Preset ready" : result.getErrorMessage(),
                   juce::dontSendNotification);
}
