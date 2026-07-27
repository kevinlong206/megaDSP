#include "PluginEditor.h"
#include "Diagnostics.h"
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
    }

    void setMeterActive(bool active)
    {
        if (active && meterSource)
            startTimerHz(30);
        else
        {
            stopTimer();
            meterLevelDb = -60.0f;
            repaint();
        }
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
        const auto levelDb = meterLevelDb;
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
    void timerCallback() override
    {
        if (meterSource)
            meterLevelDb =
                juce::jlimit(-60.0f, 0.0f, meterSource());
        repaint();
    }

    std::function<float()> meterSource;
    float meterLevelDb = -60.0f;
};

class SnapshotButton final : public juce::TextButton,
                             public juce::DragAndDropTarget
{
public:
    SnapshotButton(
        int indexToUse, juce::DragAndDropContainer& containerToUse,
        std::function<bool()> canDragToUse,
        std::function<void(int, int)> copyToUse)
        : juce::TextButton(juce::String(indexToUse + 1)),
          index(indexToUse), container(containerToUse),
          canDrag(std::move(canDragToUse)),
          copy(std::move(copyToUse))
    {
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        dragStart = event.position;
        juce::TextButton::mouseDown(event);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        juce::TextButton::mouseDrag(event);
        if (canDrag() && event.position.getDistanceFrom(dragStart) > 5.0f
            && !container.isDragAndDropActive())
            container.startDragging(
                "snapshot:" + juce::String(index), this);
    }

    bool isInterestedInDragSource(
        const SourceDetails& details) override
    {
        const auto source = snapshotIndex(details.description);
        return source >= 0 && source != index;
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
        repaint();
        const auto source = snapshotIndex(details.description);
        if (source >= 0 && source != index)
            copy(source, index);
    }

    void paintButton(
        juce::Graphics& graphics, bool highlighted,
        bool pressed) override
    {
        juce::TextButton::paintButton(graphics, highlighted, pressed);
        if (dragOver)
        {
            graphics.setColour(accent);
            graphics.drawRoundedRectangle(
                getLocalBounds().toFloat().reduced(1.5f), 4.0f, 2.0f);
        }
    }

private:
    static int snapshotIndex(const juce::var& description)
    {
        const auto text = description.toString();
        return text.startsWith("snapshot:")
                   ? text.fromFirstOccurrenceOf(
                             "snapshot:", false, false).getIntValue()
                   : -1;
    }

    int index = 0;
    juce::DragAndDropContainer& container;
    std::function<bool()> canDrag;
    std::function<void(int, int)> copy;
    juce::Point<float> dragStart;
    bool dragOver = false;
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
        auto readout = row.removeFromRight(70);
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

struct ControlGroupFrame
{
    juce::String name;
    juce::Rectangle<int> bounds;
    bool main = false;
};

class ControlsOverlay final : public juce::Component
{
public:
    explicit ControlsOverlay(MegaDSPAudioProcessor& processorToUse)
        : processor(processorToUse)
    {
        setInterceptsMouseClicks(false, false);
    }

    void setFrames(std::vector<ControlGroupFrame> newFrames)
    {
        frames = std::move(newFrames);
        repaint();
    }

    void paint(juce::Graphics& graphics) override
    {
        const auto panelColour =
            backgroundColour(processor.getBackgroundThemeIndex())
                .brighter(0.12f);
        for (const auto& frame : frames)
        {
            graphics.setColour(panelColour.withAlpha(
                frame.main ? 0.94f : 0.90f));
            graphics.fillRoundedRectangle(
                frame.bounds.toFloat(), 7.0f);
            graphics.setColour(
                frame.main ? accent : juce::Colour(0xff596575));
            graphics.drawRoundedRectangle(
                frame.bounds.toFloat(), 7.0f,
                frame.main ? 2.0f : 1.0f);
            auto frameBounds = frame.bounds;
            auto titleBounds =
                frameBounds.removeFromTop(18).withTrimmedLeft(10);
            graphics.setColour(panelColour);
            graphics.fillRect(titleBounds.withWidth(
                juce::jmin(titleBounds.getWidth(),
                           juce::jmax(44, frame.name.length() * 8))));
            graphics.setColour(
                frame.main ? accent : juce::Colour(0xffaab4c1));
            graphics.setFont(
                juce::FontOptions(10.0f, juce::Font::bold));
            graphics.drawText(
                frame.name.toUpperCase(), titleBounds,
                juce::Justification::centredLeft);
        }
    }

private:
    MegaDSPAudioProcessor& processor;
    std::vector<ControlGroupFrame> frames;
};

class MegaDSPAudioProcessorEditor::ModuleCard final
    : public juce::Component,
      public juce::DragAndDropTarget,
      public juce::FileDragAndDropTarget,
      private juce::FocusChangeListener,
      private juce::Timer
{
public:
    ModuleCard(MegaDSPAudioProcessorEditor& editorToUse, int slotToUse)
        : editor(editorToUse), processor(editorToUse.audioProcessor),
          slot(slotToUse),
          type(processor.getRack().moduleType(slot)),
          supportsImpulseResponse(
              processor.getRack().activeModuleHasCapability(
                  slot, megadsp::ModuleCapability::impulseResponse)),
          supportsNamModel(
              processor.getRack().activeModuleHasCapability(
                  slot, megadsp::ModuleCapability::neuralAmpModel)),
          controlsOverlay(processor)
    {
        moduleName = megadsp::moduleDefinition(type).displayName;
        name.setText("SLOT " + juce::String(slot + 1) + "  ·  " + moduleName,
                     juce::dontSendNotification);
        name.setFont(juce::FontOptions(22.0f, juce::Font::bold));
        name.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(name);

        bypass.onClick = [this]
        {
            processor.toggleSlotBypass(slot);
            syncBypassState();
        };
        addAndMakeVisible(bypass);
        reset.setButtonText("Reset");
        reset.setTooltip("Reset this module to its musical starting point.");
        reset.onClick = [this] { processor.resetModuleToDefaults(slot); };
        addAndMakeVisible(reset);
        remove.setButtonText("Remove");
        remove.setTooltip("Remove " + moduleName + " from the rack.");
        remove.onClick = [this]
        {
            remove.setEnabled(false);
            editor.requestRemoveSlot(slot);
        };
        addAndMakeVisible(remove);

        if (supportsImpulseResponse)
        {
            loadImpulse.setButtonText("Load IR...");
            loadImpulse.setTooltip(
                "Load a WAV, AIFF, or FLAC impulse response.");
            loadImpulse.onClick = [this]
            {
                impulseChooser = std::make_unique<juce::FileChooser>(
                    "Load impulse response",
                    processor.impulseResponseDirectory(slot),
                    "*.wav;*.aif;*.aiff;*.flac");
                const auto flags =
                    juce::FileBrowserComponent::openMode
                    | juce::FileBrowserComponent::canSelectFiles;
                juce::Component::SafePointer<ModuleCard> safeThis(this);
                impulseChooser->launchAsync(
                    flags, [safeThis](const juce::FileChooser& chooser)
                    {
                        if (safeThis == nullptr)
                            return;
                        const auto file = chooser.getResult();
                        if (file == juce::File {})
                            return;
                        safeThis->loadAssetFile(file);
                    });
            };
            addAndMakeVisible(loadImpulse);
            previousImpulse.setButtonText("< IR");
            previousImpulse.setTooltip(
                "Load the previous impulse response in this directory.");
            previousImpulse.onClick = [this]
            {
                loadAdjacentImpulseResponse(-1);
            };
            addAndMakeVisible(previousImpulse);
            nextImpulse.setButtonText("IR >");
            nextImpulse.setTooltip(
                "Load the next impulse response in this directory.");
            nextImpulse.onClick = [this]
            {
                loadAdjacentImpulseResponse(1);
            };
            addAndMakeVisible(nextImpulse);
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

        if (supportsNamModel)
        {
            loadNam.setButtonText("Load NAM...");
            loadNam.setTooltip("Load a .nam neural amp model.");
            loadNam.onClick = [this]
            {
                namChooser = std::make_unique<juce::FileChooser>(
                    "Load NAM model",
                    juce::File::getSpecialLocation(
                        juce::File::userMusicDirectory),
                    "*.nam");
                const auto flags =
                    juce::FileBrowserComponent::openMode
                    | juce::FileBrowserComponent::canSelectFiles;
                juce::Component::SafePointer<ModuleCard> safeThis(this);
                namChooser->launchAsync(
                    flags, [safeThis](const juce::FileChooser& chooser)
                    {
                        if (safeThis == nullptr) return;
                        const auto file = chooser.getResult();
                        if (file == juce::File {}) return;
                        safeThis->loadAssetFile(file);
                    });
            };
            addAndMakeVisible(loadNam);
            clearNam.setButtonText("Clear NAM");
            clearNam.setTooltip("Unload the current neural amp model.");
            clearNam.onClick = [this]
            {
                processor.clearNamModel(slot);
                graph->repaint();
            };
            addAndMakeVisible(clearNam);
        }

        graph = std::make_unique<megadsp::ui::EffectGraph>(
            processor, slot, type);
        addAndMakeVisible(*graph);
        addAndMakeVisible(controlsOverlay);
        setWantsKeyboardFocus(true);
        setTitle(moduleName + " module card, slot " + juce::String(slot + 1));
        setDescription(
            "Signal-flow position " + juce::String(slot + 1)
            + ". Use Up and Down to select adjacent module cards.");
        setHelpText(
            "Press Enter to select. Use Up and Down for adjacent cards. "
            "Space or B toggles bypass. Drag the card header to reorder.");
        addMouseListener(this, true);
        juce::Desktop::getInstance().addFocusChangeListener(this);
        syncBypassState();

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
        startTimerHz(2);
    }

    ~ModuleCard() override
    {
        juce::Desktop::getInstance().removeFocusChangeListener(this);
    }

    void paint(juce::Graphics& graphics) override
    {
        const auto panelColour = backgroundColour(
            processor.getBackgroundThemeIndex()).brighter(0.16f);
        graphics.setColour(panelColour);
        auto cardBounds = getLocalBounds().toFloat().reduced(1.0f);
        graphics.fillRoundedRectangle(cardBounds, 9.0f);
        graphics.setColour(selected ? accent : juce::Colour(0xff465263));
        graphics.drawRoundedRectangle(
            cardBounds, 9.0f, selected ? 2.0f : 1.0f);
        if (dragOver)
        {
            graphics.setColour(accent);
            graphics.fillRect(
                8.0f, dropAfter ? cardBounds.getBottom() - 4.0f : 1.0f,
                cardBounds.getWidth() - 16.0f, 3.0f);
        }
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(14);
        auto heading = bounds.removeFromTop(34);
        name.setBounds(heading.removeFromLeft(
            juce::jmin(310, juce::jmax(180, heading.getWidth() / 3))));
        remove.setBounds(heading.removeFromRight(72).reduced(2));
        reset.setBounds(heading.removeFromRight(64).reduced(2));
        bypass.setBounds(heading.removeFromRight(54).reduced(2));
        if (supportsImpulseResponse)
        {
            nextImpulse.setBounds(
                heading.removeFromRight(46).reduced(2));
            previousImpulse.setBounds(
                heading.removeFromRight(46).reduced(2));
            clearImpulse.setBounds(
                heading.removeFromRight(72).reduced(2));
            loadImpulse.setBounds(
                heading.removeFromRight(88).reduced(2));
        }
        if (supportsNamModel)
        {
            clearNam.setBounds(
                heading.removeFromRight(80).reduced(2));
            loadNam.setBounds(
                heading.removeFromRight(100).reduced(2));
        }
        bounds.removeFromTop(6);
        graph->setBounds(bounds);
        if (graph->usesFullPanel())
        {
            controlsOverlay.setVisible(false);
            controlsOverlay.setFrames({});
            return;
        }
        controlsOverlay.setVisible(true);

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

        const auto overlayHeight = juce::jlimit(
            112, 146, bounds.getHeight() * 56 / 100);
        const auto overlayBounds =
            bounds.removeFromBottom(overlayHeight).reduced(8, 5);
        controlsOverlay.setBounds(overlayBounds);
        auto overlayArea = controlsOverlay.getLocalBounds().reduced(1);
        std::vector<ControlGroupFrame> frames;
        const auto groupCount = static_cast<int>(secondaryGroups.size());
        const auto mainWidth = groupCount == 0
            ? overlayArea.getWidth()
            : juce::jlimit(
                  260, 420, overlayArea.getWidth() * 42 / 100);
        auto mainBounds =
            overlayArea.removeFromLeft(mainWidth).reduced(1);
        frames.push_back({ "Main", mainBounds, true });
        layoutGroup(
            mainBounds.translated(
                overlayBounds.getX(), overlayBounds.getY())
                .reduced(8, 10),
            mainControls);

        if (groupCount > 0)
            overlayArea.removeFromLeft(5);
        auto secondaryBounds = overlayArea;
        for (int groupIndex = 0; groupIndex < groupCount; ++groupIndex)
        {
            const auto groupsLeft = groupCount - groupIndex;
            auto groupBounds = secondaryBounds.removeFromLeft(
                secondaryBounds.getWidth() / groupsLeft).reduced(2, 1);
            frames.push_back({
                secondaryGroups[static_cast<size_t>(groupIndex)].first,
                groupBounds, false
            });
            layoutGroup(
                groupBounds.translated(
                    overlayBounds.getX(), overlayBounds.getY())
                    .reduced(7, 10),
                secondaryGroups[static_cast<size_t>(groupIndex)].second);
        }
        controlsOverlay.setFrames(std::move(frames));
    }

    void setSelected(bool shouldBeSelected)
    {
        const auto changed = selected != shouldBeSelected;
        selected = shouldBeSelected;
        setEditorVisible(editor.isShowing());
        if (!graph->usesFullPanel())
            startTimerHz(selected ? 10 : 2);
        if (changed)
            repaint();
    }

    void setEditorVisible(bool editorVisible)
    {
        const auto active = selected && editorVisible;
        graph->setVisualizationActive(active);
        for (auto& slider : sliders)
            slider.setMeterActive(active);
    }

    void syncBypassState()
    {
        const auto isBypassed = processor.isSlotBypassed(slot);
        bypass.setButtonText(isBypassed ? "BYP" : "ON");
        bypass.setToggleState(isBypassed, juce::dontSendNotification);
        bypass.setTooltip(
            isBypassed ? "BYP - module is bypassed"
                       : "ON - module is processing");
    }

#if defined(MEGADSP_TESTS)
    void clickCloseButtonForTesting() { remove.onClick(); }
    void simulateDescendantFocusForTesting()
    {
        globalFocusChanged(graph.get());
    }
    void simulateHoverExitForTesting()
    {
        editor.restoreFocusedCardAfterHover();
    }
    bool isVisualizationActiveForTesting() const
    {
        return graph->isVisualizationActive();
    }
    bool childrenFitForTesting() const
    {
        for (const auto* child : getChildren())
            if (child->isVisible()
                && !getLocalBounds().contains(child->getBounds()))
                return false;
        return true;
    }
    bool acceptsAssetDropForTesting(const juce::String& path) const
    {
        return acceptsAssetFile(juce::File(path));
    }
#endif

    bool isInterestedInFileDrag(
        const juce::StringArray& files) override
    {
        return std::any_of(
            files.begin(), files.end(),
            [this](const auto& path)
            {
                return acceptsAssetFile(juce::File(path));
            });
    }

    void filesDropped(
        const juce::StringArray& files, int, int) override
    {
        editor.selectSlot(slot);
        const auto asset = std::find_if(
            files.begin(), files.end(),
            [this](const auto& path)
            {
                return acceptsAssetFile(juce::File(path));
            });
        if (asset != files.end())
            loadAssetFile(juce::File(*asset));
    }

    void mouseEnter(const juce::MouseEvent&) override
    {
        editor.selectSlot(slot);
    }

    void mouseExit(const juce::MouseEvent& event) override
    {
        if (!getLocalBounds().toFloat().contains(
                event.getEventRelativeTo(this).position))
            editor.restoreFocusedCardAfterHover();
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        editor.selectSlot(slot);
        const auto* source = event.originalComponent;
        canStartDrag = source == this || source == &name;
        dragStart = event.getEventRelativeTo(this).position;
        if (source == this)
            grabKeyboardFocus();
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        const auto position = event.getEventRelativeTo(this).position;
        if (canStartDrag && position.getDistanceFrom(dragStart) > 5.0f
            && !editor.isDragAndDropActive())
            editor.startDragging(slot, this);
    }

    void focusGained(FocusChangeType) override
    {
        editor.focusSlot(slot);
    }

    void globalFocusChanged(juce::Component* focusedComponent) override
    {
        if (focusedComponent == this || isParentOf(focusedComponent))
            editor.focusSlot(slot);
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key.getKeyCode() == juce::KeyPress::upKey)
            return selectAdjacent(-1);
        if (key.getKeyCode() == juce::KeyPress::downKey)
            return selectAdjacent(1);
        if (key.getKeyCode() == juce::KeyPress::returnKey)
        {
            editor.selectSlot(slot);
            return true;
        }
        if (key.getKeyCode() == juce::KeyPress::spaceKey
            || key.getTextCharacter() == 'b'
            || key.getTextCharacter() == 'B')
        {
            processor.toggleSlotBypass(slot);
            syncBypassState();
            return true;
        }
        return false;
    }

    bool isInterestedInDragSource(const SourceDetails& details) override
    {
        return static_cast<int>(details.description) != slot;
    }

    void itemDragEnter(const SourceDetails& details) override
    {
        updateDropPosition(details);
        dragOver = true;
        repaint();
    }

    void itemDragMove(const SourceDetails& details) override
    {
        updateDropPosition(details);
    }

    void itemDragExit(const SourceDetails&) override
    {
        dragOver = false;
        repaint();
    }

    void itemDropped(const SourceDetails& details) override
    {
        dragOver = false;
        const auto source = static_cast<int>(details.description);
        auto destination = slot + (dropAfter ? 1 : 0);
        if (source < destination)
            --destination;
        destination = juce::jlimit(
            0, static_cast<int>(editor.cards.size()) - 1, destination);
        editor.requestReorderSlot(source, destination);
    }

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    bool acceptsAssetFile(const juce::File& file) const
    {
        const auto extension = file.getFileExtension().toLowerCase();
        if (supportsNamModel)
            return extension == ".nam";
        if (supportsImpulseResponse)
            return extension == ".wav" || extension == ".aif"
                   || extension == ".aiff" || extension == ".flac";
        return false;
    }

    void loadAssetFile(const juce::File& file)
    {
        if (!acceptsAssetFile(file))
            return;
        editor.selectSlot(slot);
        const auto result = supportsNamModel
            ? processor.loadNamModel(slot, file)
            : processor.loadImpulseResponse(slot, file);
        if (result.failed())
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                supportsNamModel ? "Could not load NAM model"
                                 : "Could not load impulse response",
                result.getErrorMessage());
        graph->repaint();
    }

    void loadAdjacentImpulseResponse(int direction)
    {
        editor.selectSlot(slot);
        const auto result =
            processor.loadAdjacentImpulseResponse(slot, direction);
        if (result.failed())
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Could not browse impulse responses",
                result.getErrorMessage());
        graph->repaint();
    }

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

    bool selectAdjacent(int direction)
    {
        const auto target = juce::jlimit(
            0, static_cast<int>(editor.cards.size()) - 1, slot + direction);
        if (!juce::isPositiveAndBelow(
                target, static_cast<int>(editor.cards.size())))
            return false;
        editor.selectSlot(target);
        auto* card = editor.cards[static_cast<size_t>(target)].get();
        card->grabKeyboardFocus();
        const auto viewTop = editor.rackViewport.getViewPositionY();
        const auto viewBottom = viewTop + editor.rackViewport.getViewHeight();
        if (card->getY() < viewTop)
            editor.rackViewport.setViewPosition(
                editor.rackViewport.getViewPositionX(), card->getY());
        else if (card->getBottom() > viewBottom)
            editor.rackViewport.setViewPosition(
                editor.rackViewport.getViewPositionX(),
                card->getBottom() - editor.rackViewport.getViewHeight());
        return true;
    }

    void updateDropPosition(const SourceDetails& details)
    {
        const auto after = details.localPosition.y > getHeight() / 2;
        if (after != dropAfter)
        {
            dropAfter = after;
            repaint();
        }
    }

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

    MegaDSPAudioProcessorEditor& editor;
    MegaDSPAudioProcessor& processor;
    int slot;
    megadsp::ModuleType type;
    juce::String moduleName;
    bool supportsImpulseResponse = false;
    bool supportsNamModel = false;
    juce::Label name;
    juce::ToggleButton bypass;
    juce::TextButton reset;
    juce::TextButton remove;
    juce::TextButton loadImpulse;
    juce::TextButton previousImpulse;
    juce::TextButton nextImpulse;
    juce::TextButton clearImpulse;
    std::unique_ptr<juce::FileChooser> impulseChooser;
    juce::TextButton loadNam;
    juce::TextButton clearNam;
    std::unique_ptr<juce::FileChooser> namChooser;
    std::unique_ptr<megadsp::ui::EffectGraph> graph;
    ControlsOverlay controlsOverlay;
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
    juce::Point<float> dragStart;
    bool selected = false;
    bool dragOver = false;
    bool dropAfter = false;
    bool canStartDrag = false;
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

    diagnosticsButton.setTitle("Diagnostics");
    diagnosticsButton.setDescription(
        "Configure diagnostic logging or open the diagnostics folder.");
    diagnosticsButton.onClick = [this] { showDiagnosticsMenu(); };
    addAndMakeVisible(diagnosticsButton);

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
        refreshCards();
        megadsp::Diagnostics::instance().log(
            "editor", "opened");
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

    snapshotsLabel.setText("SNAPSHOTS", juce::dontSendNotification);
    snapshotsLabel.setFont(
        juce::FontOptions(10.5f, juce::Font::bold));
    snapshotsLabel.setColour(
        juce::Label::textColourId, juce::Colour(0xffa8b3c0));
    snapshotsLabel.setJustificationType(
        juce::Justification::centredLeft);
    addAndMakeVisible(snapshotsHost);
    snapshotsHost.addAndMakeVisible(snapshotsLabel);
    liveSafeButton.setTooltip(
        "Continuously process populated snapshots on silence so recall "
        "does not create a CPU spike. Uses more steady CPU and defers "
        "latency updates until audio monitoring stops.");
    liveSafeButton.setTitle("Live Safe snapshot recall");
    liveSafeButton.setDescription(
        "Uses predictable steady CPU for safer live snapshot changes.");
    liveSafeButton.onClick = [this]
    {
        audioProcessor.setLiveSafeSnapshotsEnabled(
            liveSafeButton.getToggleState());
        refreshSnapshots();
    };
    snapshotsHost.addAndMakeVisible(liveSafeButton);
    for (int index = 0; index < megadsp::snapshotCount; ++index)
    {
        auto button = std::make_unique<SnapshotButton>(
            index, *this,
            [this, index]
            {
                return audioProcessor.isSnapshotPopulated(index)
                       && audioProcessor.isSnapshotReady(index);
            },
            [this](int source, int destination)
            {
                audioProcessor.copySnapshot(source, destination);
                refreshSnapshots();
            });
        button->setClickingTogglesState(false);
        button->onClick = [this, index]
        {
            const auto populated =
                audioProcessor.isSnapshotPopulated(index);
            const auto shift =
                juce::ModifierKeys::getCurrentModifiersRealtime()
                    .isShiftDown();
            if (shift)
                audioProcessor.storeSnapshot(index, populated);
            else
            {
                audioProcessor.recallSnapshot(index);
                refreshCards(true);
            }
            refreshSnapshots();
        };
        snapshotsHost.addAndMakeVisible(*button);
        snapshotButtons[static_cast<size_t>(index)] =
            std::move(button);
    }
    refreshSnapshots();

    addAndMakeVisible(rackViewport);
    rackViewport.setViewedComponent(&rackContent, false);
    rackViewport.setScrollBarsShown(true, false);
    rackViewport.setWantsKeyboardFocus(false);
    rackViewport.setTitle("Module rack");
    rackViewport.setDescription(
        "Modules process audio from top to bottom.");
    addButton.setTooltip("Browse and add a module after the last card.");
    addButton.onClick = [this] { showModuleBrowser(); };
    rackContent.addAndMakeVisible(addButton);
    selectedSlot = audioProcessor.getSelectedTab();
    refreshCards();

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
    refreshCards();
#endif
    startTimerHz(15);
}

MegaDSPAudioProcessorEditor::~MegaDSPAudioProcessorEditor()
{
    fileChooser.reset();
    megadsp::Diagnostics::instance().log(
        "editor", "closed");
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

void MegaDSPAudioProcessorEditor::visibilityChanged()
{
    const auto showing = isShowing();
    for (auto& card : cards)
        card->setEditorVisible(showing);
    if (!showing)
        audioProcessor.getVisualizationData().setSelectedSlot(-1);
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
    diagnosticsButton.setBounds(
        headerTop.removeFromRight(70).reduced(2));
    status.setBounds(headerTop);

    auto headerBottom = header.reduced(0, 2);
    auto inputArea = headerBottom.removeFromLeft(185);
    inputLabel.setBounds(inputArea.removeFromLeft(42));
    inputGain.setBounds(inputArea);
    auto outputArea = headerBottom.removeFromRight(205);
    outputLabel.setBounds(outputArea.removeFromLeft(70));
    outputGain.setBounds(outputArea);
    headerMeter->setBounds(headerBottom.reduced(8, 0));

    snapshotsHost.setBounds(
        bounds.removeFromTop(42).reduced(10, 3));
    auto snapshotBounds = snapshotsHost.getLocalBounds();
    snapshotsLabel.setBounds(snapshotBounds.removeFromLeft(82));
    liveSafeButton.setBounds(
        snapshotBounds.removeFromRight(94).reduced(2));
    const auto snapshotWidth =
        juce::jmax(1, snapshotBounds.getWidth()
                       / megadsp::snapshotCount);
    for (auto& button : snapshotButtons)
        button->setBounds(
            snapshotBounds.removeFromLeft(snapshotWidth).reduced(2));

    rackViewport.setBounds(bounds.reduced(10, 6));
    constexpr auto rackPadding = 8;
    const auto showAddButton = addButton.isVisible();
    const auto contentHeight = megadsp::ui::calculateRackContentHeight(
        static_cast<int>(cards.size()), showAddButton)
        + rackPadding * 2;
    const auto targetHeight =
        juce::jmax(rackViewport.getHeight(), contentHeight);
    rackContent.setSize(juce::jmax(1, rackViewport.getWidth()), targetHeight);
    rackContent.setSize(
        juce::jmax(1, rackViewport.getMaximumVisibleWidth()), targetHeight);
    auto cardBounds = rackContent.getLocalBounds().reduced(rackPadding, 0);
    cardBounds.removeFromTop(rackPadding);
    for (int index = 0; index < static_cast<int>(cards.size()); ++index)
    {
        auto& card = cards[static_cast<size_t>(index)];
        card->setBounds(
            cardBounds.removeFromTop(megadsp::ui::moduleCardHeight));
        if (index + 1 < static_cast<int>(cards.size()) || showAddButton)
            cardBounds.removeFromTop(megadsp::ui::moduleCardGap);
    }
    if (showAddButton)
        addButton.setBounds(
            cardBounds.removeFromTop(megadsp::ui::addModuleCardHeight));
}

void MegaDSPAudioProcessorEditor::timerCallback()
{
    headerMeter->setLevels(audioProcessor.getRack().inputMeterDb(),
                           audioProcessor.getRack().outputMeterDb());
    for (auto& card : cards)
        card->syncBypassState();
    refreshIdentityPresentation();
    if (knownSnapshotGeneration
        != audioProcessor.getSnapshotGeneration())
        refreshSnapshots();
    if (knownActiveSlots != audioProcessor.getRack().activeSlotCount()
        || knownTopologyGeneration
               != audioProcessor.getTopologyGeneration())
        refreshCards(true);
    else if (audioProcessor.getSelectedTab() != selectedSlot)
        selectSlot(audioProcessor.getSelectedTab());
#if defined(MEGADSP_CAPTURE_SCREENSHOTS)
    advanceScreenshotCapture();
#endif
}

void MegaDSPAudioProcessorEditor::refreshSnapshots()
{
    knownSnapshotGeneration = audioProcessor.getSnapshotGeneration();
    liveSafeButton.setToggleState(
        audioProcessor.isLiveSafeSnapshotsEnabled(),
        juce::dontSendNotification);
    const auto selected = audioProcessor.getSelectedSnapshot();
    for (int index = 0; index < megadsp::snapshotCount; ++index)
    {
        auto& button = *snapshotButtons[static_cast<size_t>(index)];
        const auto populated =
            audioProcessor.isSnapshotPopulated(index);
        const auto ready = audioProcessor.isSnapshotReady(index);
        const auto error = audioProcessor.snapshotError(index);
        const auto active = index == selected;
        juce::String stateText;
        if (!populated)
            stateText = "Empty";
        else if (!ready)
            stateText = "Preparing";
        else if (error.isNotEmpty())
            stateText = "Error";
        else if (active)
            stateText = "Active";
        else
            stateText = "Stored";

        button.setButtonText(
            error.isNotEmpty() ? "!" + juce::String(index + 1)
                               : juce::String(index + 1));
        button.setToggleState(active, juce::dontSendNotification);
        button.setColour(
            juce::TextButton::buttonColourId,
            populated ? juce::Colour(0xff2b3644)
                      : juce::Colour(0xff171e27));
        button.setColour(
            juce::TextButton::buttonOnColourId, accent);
        button.setTitle(
            "Snapshot " + juce::String(index + 1));
        button.setDescription(
            stateText + ". MIDI Program Change "
            + juce::String(index)
            + ". Click to recall; Shift-click to save.");
        button.setTooltip(
            "Snapshot " + juce::String(index + 1) + ": "
            + stateText + ". Program Change " + juce::String(index)
            + ". "
            + (error.isNotEmpty()
                   ? error
                   : populated
                       ? "Click to recall; Shift-click to overwrite. Drag to copy."
                       : "Click for a blank rack; Shift-click to save here."));
    }
}

void MegaDSPAudioProcessorEditor::selectSlot(int slot)
{
    if (refreshingCards)
        return;

    const auto active = audioProcessor.getRack().activeSlotCount();
    if (active <= 0)
    {
        selectedSlot = 0;
        audioProcessor.getVisualizationData().setSelectedSlot(-1);
        return;
    }
    selectedSlot = juce::jlimit(0, active - 1, slot);
    audioProcessor.setSelectedTab(selectedSlot);
    for (int index = 0; index < static_cast<int>(cards.size()); ++index)
        cards[static_cast<size_t>(index)]->setSelected(index == selectedSlot);
    if (!isShowing())
        audioProcessor.getVisualizationData().setSelectedSlot(-1);
}

void MegaDSPAudioProcessorEditor::restoreFocusedCardAfterHover()
{
    if (juce::isPositiveAndBelow(
            focusedSlot, static_cast<int>(cards.size())))
        selectSlot(focusedSlot);
}

void MegaDSPAudioProcessorEditor::focusSlot(int slot)
{
    focusedSlot = slot;
    selectSlot(slot);
}

void MegaDSPAudioProcessorEditor::refreshCards(
    bool followProcessorSelection)
{
    if (refreshingCards)
        return;

    const auto active = audioProcessor.getRack().activeSlotCount();
    const auto hadRackFocus = std::any_of(
        cards.begin(), cards.end(),
        [](const auto& card)
        {
            return card->hasKeyboardFocus(true);
        });
    if (followProcessorSelection)
        selectedSlot = audioProcessor.getSelectedTab();
    focusedSlot = active > 0
        ? juce::jlimit(0, active - 1,
                       focusedSlot >= 0 ? focusedSlot : selectedSlot)
        : -1;
    knownActiveSlots = active;
    knownTopologyGeneration = audioProcessor.getTopologyGeneration();
    {
        const juce::ScopedValueSetter<bool> refreshing(
            refreshingCards, true);
        cards.clear();
        for (int slot = 0; slot < active; ++slot)
        {
            auto card = std::make_unique<ModuleCard>(*this, slot);
            rackContent.addAndMakeVisible(*card);
            cards.push_back(std::move(card));
        }
        addButton.setVisible(active < megadsp::slotCount);
    }
    selectSlot(juce::jmin(selectedSlot, juce::jmax(0, active - 1)));
    resized();
    revealSelectedCard(hadRackFocus);
}

void MegaDSPAudioProcessorEditor::revealSelectedCard(bool focus)
{
    if (!juce::isPositiveAndBelow(
            selectedSlot, static_cast<int>(cards.size())))
        return;
    auto* card = cards[static_cast<size_t>(selectedSlot)].get();
    const auto viewTop = rackViewport.getViewPositionY();
    const auto viewBottom = viewTop + rackViewport.getViewHeight();
    if (card->getY() < viewTop)
        rackViewport.setViewPosition(
            rackViewport.getViewPositionX(), card->getY());
    else if (card->getBottom() > viewBottom)
        rackViewport.setViewPosition(
            rackViewport.getViewPositionX(),
            card->getBottom() - rackViewport.getViewHeight());
    if (focus)
        card->grabKeyboardFocus();
}

#if defined(MEGADSP_TESTS)
int MegaDSPAudioProcessorEditor::activeVisualizationCardCountForTesting() const
{
    return static_cast<int>(std::count_if(
        cards.begin(), cards.end(),
        [](const auto& card)
        {
            return card->isVisualizationActiveForTesting();
        }));
}

bool MegaDSPAudioProcessorEditor::cardChildrenFitForTesting() const
{
    return std::all_of(
        cards.begin(), cards.end(),
        [](const auto& card) { return card->childrenFitForTesting(); });
}

bool MegaDSPAudioProcessorEditor::cardHeightsAreUniformForTesting() const
{
    return std::all_of(
        cards.begin(), cards.end(),
        [](const auto& card)
        {
            return card->getHeight() == megadsp::ui::moduleCardHeight;
        });
}

bool MegaDSPAudioProcessorEditor::rackOverflowsVerticallyForTesting() const
{
    return rackContent.getHeight() > rackViewport.getHeight();
}

void MegaDSPAudioProcessorEditor::clickCloseButtonForTesting(int slot)
{
    if (juce::isPositiveAndBelow(slot, static_cast<int>(cards.size())))
    {
        cards[static_cast<size_t>(slot)]->grabKeyboardFocus();
        cards[static_cast<size_t>(slot)]->clickCloseButtonForTesting();
        applyPendingCardMutation();
    }
}

void MegaDSPAudioProcessorEditor::simulateDescendantFocusForTesting(int slot)
{
    if (juce::isPositiveAndBelow(slot, static_cast<int>(cards.size())))
        cards[static_cast<size_t>(slot)]
            ->simulateDescendantFocusForTesting();
}

void MegaDSPAudioProcessorEditor::simulateCardHoverExitForTesting(int slot)
{
    if (juce::isPositiveAndBelow(slot, static_cast<int>(cards.size())))
        cards[static_cast<size_t>(slot)]->simulateHoverExitForTesting();
}

void MegaDSPAudioProcessorEditor::simulateCardHoverForTesting(int slot)
{
    if (juce::isPositiveAndBelow(slot, static_cast<int>(cards.size())))
        selectSlot(slot);
}

void MegaDSPAudioProcessorEditor::simulateFocusGainDuringRefreshForTesting(
    int slot)
{
    if (!juce::isPositiveAndBelow(slot, static_cast<int>(cards.size())))
        return;
    const juce::ScopedValueSetter<bool> refreshing(refreshingCards, true);
    cards[static_cast<size_t>(slot)]->focusGained(
        juce::Component::focusChangedDirectly);
}

void MegaDSPAudioProcessorEditor::reorderCardForTesting(
    int source, int destination)
{
    requestReorderSlot(source, destination);
    applyPendingCardMutation();
}

void MegaDSPAudioProcessorEditor::refreshCardsForTesting()
{
    refreshCards(true);
}

bool MegaDSPAudioProcessorEditor::cardAcceptsAssetDropForTesting(
    int slot, const juce::String& path) const
{
    return juce::isPositiveAndBelow(slot, static_cast<int>(cards.size()))
        && cards[static_cast<size_t>(slot)]
               ->acceptsAssetDropForTesting(path);
}
#endif

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
            safeThis->refreshCards(true);
            safeThis->revealSelectedCard(true);
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
            safeThis->refreshCards(true);
            safeThis->revealSelectedCard(true);
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

void MegaDSPAudioProcessorEditor::showDiagnosticsMenu()
{
    constexpr int toggleCommand = 1;
    constexpr int openCommand = 2;
    auto& diagnostics = megadsp::Diagnostics::instance();
    juce::PopupMenu menu;
    menu.addItem(
        toggleCommand, "Enable Diagnostics", true,
        diagnostics.isEnabled());
    menu.addItem(openCommand, "Open Diagnostics Folder");

    juce::Component::SafePointer<MegaDSPAudioProcessorEditor> safeThis(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&diagnosticsButton),
        [safeThis](int result)
        {
            if (safeThis == nullptr || result == 0)
                return;
            auto& service = megadsp::Diagnostics::instance();
            if (result == openCommand)
            {
                service.openFolder();
                return;
            }
            if (result != toggleCommand)
                return;
            if (service.isEnabled())
            {
                service.setEnabled(false);
                safeThis->refreshIdentityPresentation();
                return;
            }

            const auto options = juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle("Enable megaDSP Diagnostics?")
                .withMessage(
                    "Diagnostics writes bounded local logs and installs fatal "
                    "crash and terminate handlers in the DAW process. Existing "
                    "host handling continues and installed handlers are restored when diagnostics is "
                    "disabled. Logs can include plugin configuration, module "
                    "topology, audio format, and error messages, but not audio.")
                .withButton("Enable")
                .withButton("Cancel")
                .withAssociatedComponent(safeThis.getComponent());
            juce::AlertWindow::showAsync(
                options,
                [safeThis](int button)
                {
                    if (safeThis == nullptr || button != 1)
                        return;
                    megadsp::Diagnostics::instance().setEnabled(true);
                    safeThis->refreshIdentityPresentation();
                });
        });
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
    const auto diagnosticsEnabled =
        megadsp::Diagnostics::instance().isEnabled();
    diagnosticsButton.setButtonText(
        diagnosticsEnabled ? "DIAG ON" : "DIAG");
    diagnosticsButton.setTooltip(
        diagnosticsEnabled
            ? "Diagnostics are enabled. Click to disable or open the log folder."
            : "Diagnostics are off. Click to configure diagnostic logging.");
    diagnosticsButton.setColour(
        juce::TextButton::buttonColourId,
        diagnosticsEnabled ? juce::Colour(0xff8b4b45)
                           : juce::Colour(0xff27313d));
    if (themeIndex != knownThemeIndex)
    {
        knownThemeIndex = themeIndex;
        repaint();
        for (auto& card : cards)
            card->repaint();
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
    const auto outputFile = outputDirectory.getChildFile(fileName);
    outputFile.deleteFile();
    auto stream = outputFile.createOutputStream();
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
            revealSelectedCard(false);
            break;
        case 1:
            captureScreenshot("visual-processing.png");
            audioProcessor.setInstanceName("Ambient Space");
            audioProcessor.setBackgroundThemeIndex(4);
            audioProcessor.loadFactoryPreset(4);
            selectedSlot = 1;
            refreshCards();
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
    const auto generation = audioProcessor.getTopologyGeneration();
    audioProcessor.removeSlot(slot);
    if (audioProcessor.getTopologyGeneration() == generation)
        return;
    selectedSlot = juce::jmin(slot,
        juce::jmax(0, audioProcessor.getRack().activeSlotCount() - 1));
    refreshCards();
}

void MegaDSPAudioProcessorEditor::reorderSlot(int source, int destination)
{
    const auto generation = audioProcessor.getTopologyGeneration();
    audioProcessor.moveSlot(source, destination);
    if (audioProcessor.getTopologyGeneration() == generation)
        return;
    selectedSlot = destination;
    refreshCards();
}

void MegaDSPAudioProcessorEditor::requestRemoveSlot(int slot)
{
    pendingCardMutation = PendingCardMutation::remove;
    pendingSourceSlot = slot;
    pendingDestinationSlot = -1;
    pendingTopologyGeneration = audioProcessor.getTopologyGeneration();
    pendingTopologyRequestGeneration =
        audioProcessor.getTopologyRequestGeneration();
    const juce::Component::SafePointer<MegaDSPAudioProcessorEditor>
        safeThis(this);
    juce::MessageManager::callAsync(
        [safeThis]
        {
            if (safeThis != nullptr)
                safeThis->applyPendingCardMutation();
        });
}

void MegaDSPAudioProcessorEditor::requestReorderSlot(
    int source, int destination)
{
    pendingCardMutation = PendingCardMutation::reorder;
    pendingSourceSlot = source;
    pendingDestinationSlot = destination;
    pendingTopologyGeneration = audioProcessor.getTopologyGeneration();
    pendingTopologyRequestGeneration =
        audioProcessor.getTopologyRequestGeneration();
    const juce::Component::SafePointer<MegaDSPAudioProcessorEditor>
        safeThis(this);
    juce::MessageManager::callAsync(
        [safeThis]
        {
            if (safeThis != nullptr)
                safeThis->applyPendingCardMutation();
        });
}

void MegaDSPAudioProcessorEditor::applyPendingCardMutation()
{
    const auto mutation = pendingCardMutation;
    const auto source = pendingSourceSlot;
    const auto destination = pendingDestinationSlot;
    const auto generation = pendingTopologyGeneration;
    const auto requestGeneration = pendingTopologyRequestGeneration;
    pendingCardMutation = PendingCardMutation::none;
    pendingSourceSlot = -1;
    pendingDestinationSlot = -1;

    if (mutation == PendingCardMutation::none
        || audioProcessor.getTopologyGeneration() != generation
        || audioProcessor.getTopologyRequestGeneration() != requestGeneration)
        return;
    if (mutation == PendingCardMutation::remove)
        removeSlot(source);
    else
        reorderSlot(source, destination);
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
        const auto result = save
            ? safeThis->audioProcessor.savePreset(file)
            : safeThis->audioProcessor.loadPreset(file);
        safeThis->showResult(result);
        if (!save && result.wasOk())
            safeThis->refreshCards(true);
    });
}

void MegaDSPAudioProcessorEditor::showResult(const juce::Result& result)
{
    status.setText(result.wasOk() ? "Preset ready" : result.getErrorMessage(),
                   juce::dontSendNotification);
}
