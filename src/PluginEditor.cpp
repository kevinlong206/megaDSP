#include "PluginEditor.h"

#include "BuildInfo.h"
#include "Diagnostics.h"
#include "ui/EffectGraph.h"
#include "ui/GraphStyle.h"
#include "ui/GuiLayout.h"
#include "ui/ModuleBrowser.h"

#include <numeric>

namespace
{
using megadsp::ui::accent;
using megadsp::ui::backgroundColour;
using megadsp::ui::inputColour;
using megadsp::ui::modulePaletteColour;
using megadsp::ui::moduleSurfaceColour;
using megadsp::ui::outputColour;
using megadsp::ui::reductionColour;

class WidgetActionButton final : public juce::Button
{
public:
    enum class Icon
    {
        power,
        reset,
        remove
    };

    explicit WidgetActionButton(Icon iconToUse)
        : juce::Button(
              iconToUse == Icon::power ? "Bypass"
              : iconToUse == Icon::reset ? "Reset" : "Remove"),
          icon(iconToUse)
    {
    }

    void paintButton(
        juce::Graphics& graphics, bool highlighted, bool down) override
    {
        const auto enabled = isEnabled();
        const auto bypassed =
            icon == Icon::power && getToggleState();
        auto background = juce::Colour(0xff26313d);
        if (bypassed)
            background = juce::Colour(0xff3b292b);
        graphics.setColour(
            enabled
                ? background.brighter(
                      down ? 0.16f : highlighted ? 0.08f : 0.0f)
                : background.withAlpha(0.35f));
        graphics.fillRoundedRectangle(
            getLocalBounds().toFloat(), 5.0f);

        auto bounds = getLocalBounds().toFloat().reduced(6.0f);
        const auto centre = bounds.getCentre();
        const auto colour =
            !enabled ? juce::Colours::white.withAlpha(0.3f)
            : bypassed ? reductionColour.withAlpha(0.9f)
            : icon == Icon::power ? accent.withAlpha(0.95f)
                                  : juce::Colours::white.withAlpha(0.82f);
        graphics.setColour(colour);

        juce::Path path;
        if (icon == Icon::power)
        {
            const auto radius =
                0.42f * juce::jmin(bounds.getWidth(), bounds.getHeight());
            path.addCentredArc(
                centre.x, centre.y + 0.8f, radius, radius, 0.0f,
                0.24f * juce::MathConstants<float>::pi,
                1.76f * juce::MathConstants<float>::pi, true);
            path.startNewSubPath(
                centre.x, centre.y - radius - 1.0f);
            path.lineTo(centre.x, centre.y + 1.0f);
            graphics.strokePath(
                path, juce::PathStrokeType(
                          1.8f, juce::PathStrokeType::curved,
                          juce::PathStrokeType::rounded));
            if (bypassed)
            {
                graphics.drawLine(
                    bounds.getX(), bounds.getBottom(),
                    bounds.getRight(), bounds.getY(), 1.4f);
            }
        }
        else if (icon == Icon::reset)
        {
            const auto radius =
                0.4f * juce::jmin(bounds.getWidth(), bounds.getHeight());
            path.addCentredArc(
                centre.x, centre.y, radius, radius, 0.0f,
                -0.25f * juce::MathConstants<float>::pi,
                1.45f * juce::MathConstants<float>::pi, true);
            graphics.strokePath(
                path, juce::PathStrokeType(
                          1.7f, juce::PathStrokeType::curved,
                          juce::PathStrokeType::rounded));
            juce::Path arrow;
            arrow.startNewSubPath(
                centre.x - radius - 1.0f, centre.y - 1.0f);
            arrow.lineTo(
                centre.x - radius + 4.0f, centre.y - 3.5f);
            arrow.lineTo(
                centre.x - radius + 3.5f, centre.y + 2.5f);
            arrow.closeSubPath();
            graphics.fillPath(arrow);
        }
        else
        {
            const auto can = bounds.reduced(2.5f, 2.0f);
            graphics.drawRoundedRectangle(
                can.withTrimmedTop(3.5f), 1.5f, 1.5f);
            graphics.drawLine(
                can.getX() - 1.0f, can.getY() + 2.0f,
                can.getRight() + 1.0f, can.getY() + 2.0f, 1.5f);
            graphics.drawLine(
                centre.x - 2.5f, can.getY(),
                centre.x + 2.5f, can.getY(), 1.5f);
            graphics.drawVerticalLine(
                juce::roundToInt(centre.x),
                can.getY() + 6.0f, can.getBottom() - 2.0f);
        }
    }

private:
    Icon icon;
};

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
    slider.setScrollWheelEnabled(false);
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
    ~MeteredSlider() override
    {
        precisionOverlay.reset();
    }

    void setMusicalValueFunctions(
        std::function<juce::String(double)> formatterToUse,
        std::function<double(const juce::String&)> parserToUse)
    {
        formatter = std::move(formatterToUse);
        parser = std::move(parserToUse);
        updateText();
    }

    juce::String getTextFromValue(double value) override
    {
        return formatter != nullptr
            ? formatter(value)
            : juce::Slider::getTextFromValue(value);
    }

    double getValueFromText(const juce::String& text) override
    {
        return parser != nullptr
            ? parser(text)
            : juce::Slider::getValueFromText(text);
    }

    void enableCompactFineDragging()
    {
        compactFineDragging = true;
        setSliderSnapsToMousePosition(false);
        setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    }

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
        if (compactFineDragging
            && getSliderStyle()
                   == juce::Slider::RotaryHorizontalVerticalDrag)
            paintCompactRotary(graphics);
        else
            juce::Slider::paint(graphics);

        if (!meterSource)
            return;
        auto meter = getLocalBounds().toFloat().reduced(8.0f, 3.0f);
        const auto textWidth = static_cast<float>(
            juce::jlimit(38, 54, getWidth() / 2));
        meter.removeFromRight(textWidth + 5.0f);
        meter = meter.removeFromBottom(3.0f);
        graphics.setColour(juce::Colour(0xff141a21).withAlpha(0.9f));
        graphics.fillRoundedRectangle(meter, 1.5f);
        const auto levelDb = meterLevelDb;
        const auto levelProportion =
            juce::jlimit(0.0f, 1.0f, (levelDb + 60.0f) / 60.0f);
        auto fill = meter.withWidth(
            meter.getWidth() * levelProportion);
        graphics.setColour(levelDb > -6.0f ? reductionColour : outputColour);
        graphics.fillRoundedRectangle(fill, 1.5f);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        juce::Slider::mouseDown(event);
        dragStartValue = getValue();
        showPrecisionOverlay();
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!compactFineDragging)
        {
            juce::Slider::mouseDrag(event);
            if (precisionOverlay != nullptr)
                precisionOverlay->repaint();
            return;
        }
        applyCompactDrag(
            event.getDistanceFromDragStartY(),
            event.mods.isShiftDown(),
            juce::sendNotificationSync);
        if (precisionOverlay != nullptr)
            precisionOverlay->repaint();
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        juce::Slider::mouseUp(event);
        precisionOverlay.reset();
    }

#if defined(MEGADSP_TESTS)
    bool compactInteractionWorksForTesting()
    {
        if (!compactFineDragging || getWidth() <= 0 || getHeight() <= 0)
            return false;
        juce::Image image(
            juce::Image::ARGB, getWidth(), getHeight(), true);
        juce::Graphics graphics(image);
        paint(graphics);
        const auto knobCentre = juce::Point<int>(
            juce::jmin(getHeight(), 28) / 2 + 2,
            getHeight() / 2);
        const auto visible =
            image.getPixelAt(
                juce::jlimit(0, getWidth() - 1, knobCentre.x),
                juce::jlimit(0, getHeight() - 1, knobCentre.y))
                .getAlpha() > 0;
        const auto original = getValue();
        dragStartValue = proportionOfLengthToValue(0.5);
        applyCompactDrag(-60, false, juce::dontSendNotification);
        const auto upward = valueToProportionOfLength(getValue());
        setValue(dragStartValue, juce::dontSendNotification);
        applyCompactDrag(60, false, juce::dontSendNotification);
        const auto downward = valueToProportionOfLength(getValue());
        setValue(original, juce::dontSendNotification);
        return visible && upward > 0.5 && downward < 0.5;
    }
#endif

private:
    void applyCompactDrag(
        int verticalDistance, bool fine,
        juce::NotificationType notification)
    {
        const auto fineScale = fine ? 4.0 : 1.0;
        constexpr auto fullRangeDragPixels = 240.0;
        const auto proportion = valueToProportionOfLength(dragStartValue)
            - static_cast<double>(verticalDistance)
                / (fullRangeDragPixels * fineScale);
        setValue(
            proportionOfLengthToValue(
                juce::jlimit(0.0, 1.0, proportion)),
            notification);
    }

    void drawKnob(
        juce::Graphics& graphics, juce::Rectangle<float> bounds,
        float proportion, float strokeWidth) const
    {
        const auto diameter =
            juce::jmin(bounds.getWidth(), bounds.getHeight());
        const auto knob = bounds.withSizeKeepingCentre(diameter, diameter)
            .reduced(strokeWidth);
        const auto rotary = getRotaryParameters();
        const auto angle = juce::jmap(
            juce::jlimit(0.0f, 1.0f, proportion),
            rotary.startAngleRadians, rotary.endAngleRadians);
        const auto radius = knob.getWidth() * 0.5f;
        const auto centre = knob.getCentre();

        graphics.setColour(
            juce::Colour(0xff161d26).withMultipliedAlpha(
                isEnabled() ? 1.0f : 0.48f));
        graphics.fillEllipse(knob);
        graphics.setColour(
            findColour(juce::Slider::rotarySliderOutlineColourId)
                .brighter(0.32f)
                .withMultipliedAlpha(isEnabled() ? 1.0f : 0.48f));
        graphics.drawEllipse(knob, strokeWidth);

        juce::Path valueArc;
        valueArc.addCentredArc(
            centre.x, centre.y, radius - strokeWidth * 1.5f,
            radius - strokeWidth * 1.5f, 0.0f,
            rotary.startAngleRadians, angle, true);
        graphics.setColour(
            findColour(juce::Slider::rotarySliderFillColourId)
                .withMultipliedAlpha(isEnabled() ? 1.0f : 0.48f));
        graphics.strokePath(
            valueArc,
            juce::PathStrokeType(
                strokeWidth * 1.45f,
                juce::PathStrokeType::curved,
                juce::PathStrokeType::rounded));

        const auto indicatorStart = centre
            + juce::Point<float>(
                std::sin(angle), -std::cos(angle))
                * radius * 0.24f;
        const auto indicatorEnd = centre
            + juce::Point<float>(
                std::sin(angle), -std::cos(angle))
                * radius * 0.68f;
        graphics.setColour(
            juce::Colours::white.withMultipliedAlpha(
                isEnabled() ? 0.92f : 0.42f));
        graphics.drawLine(
            { indicatorStart, indicatorEnd },
            strokeWidth * 1.15f);
    }

    void paintCompactRotary(juce::Graphics& graphics)
    {
        auto area = getLocalBounds().toFloat().reduced(2.0f, 1.0f);
        const auto knobSize =
            juce::jmin(28.0f, area.getHeight());
        const auto knob = area.removeFromLeft(knobSize)
            .withSizeKeepingCentre(knobSize, knobSize);
        drawKnob(
            graphics, knob,
            static_cast<float>(
                valueToProportionOfLength(getValue())),
            1.35f);

        graphics.setColour(
            juce::Colours::white.withMultipliedAlpha(
                isEnabled() ? 0.88f : 0.45f));
        graphics.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        graphics.drawFittedText(
            getTextFromValue(getValue()),
            area.reduced(4.0f, 0.0f).toNearestInt(),
            juce::Justification::centredLeft, 1, 0.72f);

        if (hasKeyboardFocus(false))
        {
            graphics.setColour(
                findColour(juce::Slider::rotarySliderFillColourId)
                    .withAlpha(0.72f));
            graphics.drawRoundedRectangle(
                getLocalBounds().toFloat().reduced(0.75f),
                4.0f, 1.25f);
        }
    }

    class PrecisionOverlay final : public juce::Component
    {
    public:
        explicit PrecisionOverlay(MeteredSlider& ownerToUse)
            : owner(ownerToUse)
        {
            setInterceptsMouseClicks(false, false);
            setAlwaysOnTop(true);
        }

        void paint(juce::Graphics& graphics) override
        {
            auto panel = getLocalBounds().toFloat().reduced(2.0f);
            graphics.setColour(juce::Colour(0xf5161d26));
            graphics.fillRoundedRectangle(panel, 12.0f);
            graphics.setColour(accent.withAlpha(0.9f));
            graphics.drawRoundedRectangle(panel, 12.0f, 1.5f);

            graphics.setColour(juce::Colours::white.withAlpha(0.82f));
            graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            graphics.drawFittedText(
                owner.getTitle(),
                getLocalBounds().removeFromTop(24).reduced(8, 2),
                juce::Justification::centred, 1);

            auto knob = getLocalBounds().toFloat()
                .withSizeKeepingCentre(112.0f, 112.0f)
                .translated(0.0f, -2.0f);
            owner.drawKnob(
                graphics, knob,
                static_cast<float>(
                    owner.valueToProportionOfLength(owner.getValue())),
                3.0f);

            graphics.setColour(juce::Colours::white);
            graphics.setFont(juce::FontOptions(13.0f, juce::Font::bold));
            graphics.drawFittedText(
                owner.getTextFromValue(owner.getValue()),
                getLocalBounds().removeFromBottom(28).reduced(8, 2),
                juce::Justification::centred, 1);
        }

    private:
        MeteredSlider& owner;
    };

    void showPrecisionOverlay()
    {
        auto* host = getTopLevelComponent();
        if (host == nullptr || host == this)
            return;
        precisionOverlay = std::make_unique<PrecisionOverlay>(*this);
        host->addAndMakeVisible(*precisionOverlay);
        const auto centre = host->getLocalPoint(
            this, getLocalBounds().getCentre());
        auto bounds = juce::Rectangle<int>(156, 172)
            .withCentre(centre)
            .constrainedWithin(host->getLocalBounds().reduced(6));
        precisionOverlay->setBounds(bounds);
        precisionOverlay->toFront(false);
    }

    void timerCallback() override
    {
        if (meterSource)
            meterLevelDb =
                juce::jlimit(-60.0f, 0.0f, meterSource());
        repaint();
    }

    std::function<float()> meterSource;
    std::function<juce::String(double)> formatter;
    std::function<double(const juce::String&)> parser;
    float meterLevelDb = -60.0f;
    double dragStartValue = 0.0;
    bool compactFineDragging = false;
    std::unique_ptr<PrecisionOverlay> precisionOverlay;
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
        box.setScrollWheelEnabled(false);
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
    ModuleCard(
        MegaDSPAudioProcessorEditor& editorToUse, int slotToUse)
        : editor(editorToUse), processor(editorToUse.audioProcessor),
          slot(slotToUse),
          type(processor.getRack().moduleType(slot)),
          supportsImpulseResponse(
              processor.getRack().activeModuleHasCapability(
                  slot, megadsp::ModuleCapability::impulseResponse)),
          isConvolution(
              type == megadsp::ModuleType::convolutionReverb),
          isWormhole(type == megadsp::ModuleType::wormholeConvolution),
          supportsNamModel(
              processor.getRack().activeModuleHasCapability(
                  slot, megadsp::ModuleCapability::neuralAmpModel)),
          controlsOverlay(processor)
    {
        moduleName = megadsp::moduleDefinition(type).displayName;
        name.setText(moduleName, juce::dontSendNotification);
        name.setFont(juce::FontOptions(16.0f, juce::Font::bold));
        name.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(name);

        bypass.onClick = [this]
        {
            processor.toggleSlotBypass(slot);
            syncBypassState();
        };
        addAndMakeVisible(bypass);
        bypass.setTitle("Bypass " + moduleName);
        bypass.setDescription(
            "Toggles this module between processing and bypassed.");
        bypass.setTooltip("Bypass this module.");
        reset.setTitle("Reset " + moduleName);
        reset.setDescription(
            "Restores this module's musical starting point.");
        reset.setTooltip("Reset this module to its musical starting point.");
        reset.onClick = [this] { processor.resetModuleToDefaults(slot); };
        addAndMakeVisible(reset);
        remove.setTitle("Remove " + moduleName);
        remove.setDescription(
            "Removes this module and closes the gap in the rack.");
        remove.setTooltip("Remove " + moduleName + " from the rack.");
        remove.onClick = [this]
        {
            remove.setEnabled(false);
            editor.requestRemoveSlot(slot);
        };
        addAndMakeVisible(remove);

        if (supportsImpulseResponse)
        {
            assetName.setColour(
                juce::Label::textColourId,
                juce::Colours::white.withAlpha(0.72f));
            assetName.setFont(juce::FontOptions(10.0f));
            assetName.setJustificationType(
                juce::Justification::centredLeft);
            addAndMakeVisible(assetName);
            loadImpulse.setButtonText(isWormhole ? "Load A..." : "Load IR...");
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
                        safeThis->loadAssetFile(file, 0);
                    });
            };
            addAndMakeVisible(loadImpulse);
            if (isWormhole)
            {
                loadImpulse2.setButtonText("Load B...");
                loadImpulse2.setTooltip(
                    "Load the second parallel reverb impulse response.");
                loadImpulse2.onClick = [this]
                {
                    impulseChooser2 = std::make_unique<juce::FileChooser>(
                        "Load Wormhole impulse B",
                        processor.impulseResponseDirectory(slot),
                        "*.wav;*.aif;*.aiff;*.flac");
                    const auto flags =
                        juce::FileBrowserComponent::openMode
                        | juce::FileBrowserComponent::canSelectFiles;
                    juce::Component::SafePointer<ModuleCard> safeThis(this);
                    impulseChooser2->launchAsync(
                        flags, [safeThis](const juce::FileChooser& chooser)
                        {
                            if (safeThis == nullptr)
                                return;
                            const auto file = chooser.getResult();
                            if (file != juce::File {})
                                safeThis->loadAssetFile(file, 1);
                        });
                };
                addAndMakeVisible(loadImpulse2);
            }
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
            if (isConvolution)
            {
                impulseLibrary.setButtonText("IR Root...");
                impulseLibrary.setTooltip(
                    "Choose the persistent root directory or rebuild its "
                    "validated recursive index. Search is available directly "
                    "in the module.");
                impulseLibrary.onClick = [this]
                {
                    showImpulseLibraryMenu();
                };
                addAndMakeVisible(impulseLibrary);
            }
            clearImpulse.setButtonText("Clear IR");
            clearImpulse.setTooltip(
                "Unload the current impulse response.");
            clearImpulse.onClick = [this]
            {
                processor.clearImpulseResponse(slot);
                graph->repaint();
            };
            addAndMakeVisible(clearImpulse);
            if (isWormhole)
            {
                clearImpulse.setButtonText("Clear A");
                clearImpulse2.setButtonText("Clear B");
                clearImpulse2.onClick = [this]
                {
                    processor.clearImpulseResponse(slot, 1);
                    graph->repaint();
                };
                addAndMakeVisible(clearImpulse2);
            }
        }

        if (supportsNamModel)
        {
            assetName.setColour(
                juce::Label::textColourId,
                juce::Colours::white.withAlpha(0.72f));
            assetName.setFont(juce::FontOptions(10.0f));
            assetName.setJustificationType(
                juce::Justification::centredLeft);
            addAndMakeVisible(assetName);
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
            previousNam.setButtonText("<");
            previousNam.setTooltip(
                "Load the previous model in the validated NAM library.");
            previousNam.onClick = [this] { loadAdjacentNamModel(-1); };
            addAndMakeVisible(previousNam);
            nextNam.setButtonText(">");
            nextNam.setTooltip(
                "Load the next model in the validated NAM library.");
            nextNam.onClick = [this] { loadAdjacentNamModel(1); };
            addAndMakeVisible(nextNam);
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
            processor, slot, type, true);
        addAndMakeVisible(*graph);
        addAndMakeVisible(controlsOverlay);
        setWantsKeyboardFocus(true);
        setTitle(moduleName + " module widget, slot " + juce::String(slot + 1));
        setDescription(
            "Signal-flow position " + juce::String(slot + 1)
            + ". Use Left and Right to select adjacent module widgets.");
        setHelpText(
            "All module controls are available in this widget. Use Left and "
            "Right for adjacent widgets. Space or B toggles bypass. Drag the "
            "widget header to reorder.");
        addMouseListener(this, true);
        juce::Desktop::getInstance().addFocusChangeListener(this);
        syncBypassState();
        syncAssetName();

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
            labels[index].setFont(juce::FontOptions(9.5f));
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
                slider.setColour(
                    juce::Slider::rotarySliderFillColourId,
                    modulePaletteColour(type));
                slider.enableCompactFineDragging();
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
                slider.setMusicalValueFunctions(
                    [this, control](double value)
                    {
                        return megadsp::formatControlValue(
                            type, control, static_cast<float>(value));
                    },
                    [this, control, &slider](const juce::String& text)
                    {
                        return static_cast<double>(
                            megadsp::parseControlValue(type, control, text)
                                .value_or(
                                    static_cast<float>(slider.getValue())));
                    });
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
        const auto moduleColour = modulePaletteColour(type);
        const auto panelColour = moduleSurfaceColour(
            backgroundColour(processor.getBackgroundThemeIndex()),
            type, 0.24f);
        graphics.setColour(panelColour);
        auto cardBounds = getLocalBounds().toFloat().reduced(1.0f);
        graphics.fillRoundedRectangle(cardBounds, 9.0f);
        graphics.setColour(
            selected ? moduleColour.brighter(0.18f)
                     : moduleColour.withAlpha(0.56f));
        graphics.drawRoundedRectangle(
            cardBounds, 9.0f, selected ? 2.0f : 1.0f);
        if (dragOver)
        {
            graphics.setColour(moduleColour.brighter(0.2f));
            graphics.fillRect(
                dropAfter ? cardBounds.getRight() - 4.0f : 1.0f,
                8.0f, 3.0f, cardBounds.getHeight() - 16.0f);
        }
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(8);
        auto heading = bounds.removeFromTop(28);
        remove.setBounds(heading.removeFromRight(28).reduced(2));
        reset.setBounds(heading.removeFromRight(28).reduced(2));
        bypass.setBounds(heading.removeFromRight(28).reduced(2));
        name.setBounds(heading);
        if (supportsImpulseResponse || supportsNamModel)
        {
            bounds.removeFromTop(3);
            auto assetRow = bounds.removeFromTop(28);
            if (supportsNamModel)
            {
                loadNam.setButtonText("LOAD");
                clearNam.setButtonText("CLEAR");
                clearNam.setBounds(
                    assetRow.removeFromRight(48).reduced(1));
                nextNam.setBounds(
                    assetRow.removeFromRight(38).reduced(1));
                previousNam.setBounds(
                    assetRow.removeFromRight(38).reduced(1));
                loadNam.setBounds(
                    assetRow.removeFromRight(58).reduced(1));
            }
            else if (isWormhole)
            {
                clearImpulse2.setButtonText("B-");
                loadImpulse2.setButtonText("B+");
                clearImpulse.setButtonText("A-");
                loadImpulse.setButtonText("A+");
                clearImpulse2.setBounds(
                    assetRow.removeFromRight(38).reduced(1));
                loadImpulse2.setBounds(
                    assetRow.removeFromRight(38).reduced(1));
                clearImpulse.setBounds(
                    assetRow.removeFromRight(38).reduced(1));
                loadImpulse.setBounds(
                    assetRow.removeFromRight(38).reduced(1));
                previousImpulse.setBounds({});
                nextImpulse.setBounds({});
                impulseLibrary.setBounds({});
            }
            else
            {
                loadImpulse.setButtonText("LOAD");
                clearImpulse.setButtonText("CLEAR");
                previousImpulse.setButtonText("<");
                nextImpulse.setButtonText(">");
                clearImpulse.setBounds(
                    assetRow.removeFromRight(48).reduced(1));
                nextImpulse.setBounds(
                    assetRow.removeFromRight(38).reduced(1));
                previousImpulse.setBounds(
                    assetRow.removeFromRight(38).reduced(1));
                if (isConvolution)
                {
                    impulseLibrary.setButtonText("LIB");
                    impulseLibrary.setBounds(
                        assetRow.removeFromRight(40).reduced(1));
                }
                else
                    impulseLibrary.setBounds({});
                loadImpulse.setBounds(
                    assetRow.removeFromRight(58).reduced(1));
            }
            assetName.setBounds(assetRow.reduced(4, 1));
        }
        else
            assetName.setBounds({});
        bounds.removeFromTop(6);
        if (graph->usesFullPanel())
        {
            graph->setBounds(bounds);
            controlsOverlay.setVisible(false);
            controlsOverlay.setFrames({});
            return;
        }
        controlsOverlay.setVisible(true);
        for (int control = 0; control < megadsp::controlsPerSlot; ++control)
        {
            const auto index = static_cast<size_t>(control);
            labels[index].setBounds({});
            sliders[index].setBounds({});
            choices[index].setBounds({});
            toggles[index].setBounds({});
        }

        std::vector<int> mainControls;
        std::vector<std::pair<juce::String, std::vector<int>>> secondaryGroups;
        for (int control = 0; control < megadsp::controlsPerSlot; ++control)
        {
            if (!controlVisible[static_cast<size_t>(control)])
                continue;
            if (type == megadsp::ModuleType::compressor
                && (control == 0
                    || (control == 7
                        && !processor.hasExternalSidechain())))
                continue;
            if (type == megadsp::ModuleType::equalizer)
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
        if (type == megadsp::ModuleType::convolutionReverb)
        {
            constexpr std::array<int, 4> compactOrder {
                4, 2, 5, 3
            };
            std::stable_sort(
                mainControls.begin(), mainControls.end(),
                [&](int left, int right)
                {
                    const auto rank = [&](int control)
                    {
                        const auto found = std::find(
                            compactOrder.begin(),
                            compactOrder.end(), control);
                        return found == compactOrder.end()
                            ? static_cast<int>(compactOrder.size())
                            : static_cast<int>(
                                  std::distance(
                                      compactOrder.begin(), found));
                    };
                    return rank(left) < rank(right);
                });
        }

        controlPresented.fill(false);
        for (const auto control : mainControls)
            controlPresented[static_cast<size_t>(control)] = true;
        for (const auto& group : secondaryGroups)
            for (const auto control : group.second)
                controlPresented[static_cast<size_t>(control)] = true;
        for (int control = 0; control < megadsp::controlsPerSlot; ++control)
        {
            const auto index = static_cast<size_t>(control);
            const auto shown =
                controlVisible[index] && controlPresented[index];
            const auto enabled = shown && controlEnabled[index];
            const auto sliderShown =
                shown && sliderAttachments[index] != nullptr;
            const auto choiceShown =
                shown && choiceAttachments[index] != nullptr;
            const auto toggleShown =
                shown && toggleAttachments[index] != nullptr;
            sliders[index].setVisible(sliderShown);
            choices[index].setVisible(choiceShown);
            toggles[index].setVisible(toggleShown);
            labels[index].setVisible(shown && !toggleShown);
            sliders[index].setWantsKeyboardFocus(
                sliderShown && enabled);
            choices[index].setWantsKeyboardFocus(
                choiceShown && enabled);
            toggles[index].setWantsKeyboardFocus(
                toggleShown && enabled);
        }

        const auto visibleControlCount =
            static_cast<int>(mainControls.size())
            + std::accumulate(
                secondaryGroups.begin(), secondaryGroups.end(), 0,
                [](int total, const auto& group)
                {
                    return total + static_cast<int>(group.second.size());
                });
        if (visibleControlCount == 0)
        {
            controlsOverlay.setVisible(false);
            graph->setBounds(bounds.withTrimmedBottom(4));
            return;
        }
        const auto columns =
            type == megadsp::ModuleType::compressor ? 4
            : editor.widgetSize == megadsp::ui::WidgetSize::small
            ? 3
            : (editor.widgetSize
                       == megadsp::ui::WidgetSize::medium
                   ? 4
                   : 6);
        const auto rows = juce::jmax(
            1, (visibleControlCount + columns - 1) / columns);
        const auto rowHeight =
            editor.widgetSize == megadsp::ui::WidgetSize::small
            ? 38 : editor.widgetSize == megadsp::ui::WidgetSize::medium
                ? 40 : 42;
        const auto overlayHeight = juce::jmin(
            bounds.getHeight() - 34, rows * rowHeight + 8);
        const auto overlayBounds =
            bounds.removeFromBottom(overlayHeight).reduced(4, 2);
        graph->setBounds(bounds.withTrimmedBottom(4));
        controlsOverlay.setBounds(overlayBounds);
        controlsOverlay.setFrames({});
        std::vector<int> controls = mainControls;
        for (const auto& group : secondaryGroups)
            controls.insert(
                controls.end(), group.second.begin(), group.second.end());
        if (type == megadsp::ModuleType::compressor)
        {
            constexpr std::array<int, 9> compressorOrder {
                0, 1, 4, 2, 3, 8, 5, 6, 7
            };
            std::stable_sort(
                controls.begin(), controls.end(),
                [&](int left, int right)
                {
                    const auto rank =
                        [&](int control)
                        {
                            const auto found = std::find(
                                compressorOrder.begin(),
                                compressorOrder.end(), control);
                            return found == compressorOrder.end()
                                ? static_cast<int>(
                                      compressorOrder.size())
                                : static_cast<int>(
                                      std::distance(
                                          compressorOrder.begin(), found));
                        };
                    return rank(left) < rank(right);
                });
        }
        auto controlArea = overlayBounds.reduced(2);
        for (int position = 0;
             position < static_cast<int>(controls.size()); ++position)
        {
            const auto row = position / columns;
            const auto column = position % columns;
            const auto x = controlArea.getX()
                + column * controlArea.getWidth() / columns;
            const auto right = controlArea.getX()
                + (column + 1) * controlArea.getWidth() / columns;
            const auto y = controlArea.getY()
                + row * controlArea.getHeight() / rows;
            const auto bottom = controlArea.getY()
                + (row + 1) * controlArea.getHeight() / rows;
            layoutGroup(
                { x, y, right - x, bottom - y },
                { controls[static_cast<size_t>(position)] });
        }
    }

    int preferredHeight() const
    {
        return graph != nullptr
            ? graph->preferredCardHeight()
            : megadsp::ui::moduleCardHeight;
    }

    int graphHeightForTesting() const
    {
        return graph != nullptr ? graph->getHeight() : 0;
    }

    int graphWidthForTesting() const
    {
        return graph != nullptr ? graph->getWidth() : 0;
    }

    void setSelected(bool shouldBeSelected)
    {
        const auto changed = selected != shouldBeSelected;
        selected = shouldBeSelected;
        if (!graph->usesFullPanel())
            startTimerHz(selected ? 10 : 2);
        if (changed)
            repaint();
    }

    void setEditorVisible(bool editorVisible)
    {
        const auto active = editorVisible;
        graph->setVisualizationActive(active);
        for (auto& slider : sliders)
            slider.setMeterActive(active);
    }

    void reassertVisualizationCapture()
    {
        graph->setVisualizationActive(
            graph->isVisualizationActive());
    }

    void syncBypassState()
    {
        const auto isBypassed = processor.isSlotBypassed(slot);
        bypass.setToggleState(isBypassed, juce::dontSendNotification);
        bypass.setTitle(
            juce::String(isBypassed ? "Enable " : "Bypass ")
            + moduleName);
        bypass.setDescription(
            isBypassed
                ? "Enables processing for this module."
                : "Bypasses this module while preserving latency.");
        bypass.setTooltip(
            isBypassed ? "Module bypassed. Click to enable processing."
                       : "Module processing. Click to bypass.");
        bypass.repaint();
    }

    void syncAssetName()
    {
        if (supportsNamModel)
        {
            const auto path = processor.namModelPath(slot);
            assetName.setText(
                path.isEmpty()
                    ? "MODEL  No model loaded"
                    : "MODEL  "
                          + juce::File(path)
                                .getFileNameWithoutExtension(),
                juce::dontSendNotification);
        }
        else if (supportsImpulseResponse)
        {
            const auto impulse = processor.impulseResponseName(slot);
            assetName.setText(
                impulse.isEmpty()
                    ? "IR  No impulse loaded"
                    : "IR  " + impulse,
                juce::dontSendNotification);
        }
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
    bool graphAndControlsAreSeparatedForTesting() const
    {
        return graph->usesFullPanel()
            || !graph->getBounds().intersects(controlsOverlay.getBounds());
    }
    bool presentsAllActiveControlsForTesting() const
    {
        for (int control = 0; control < megadsp::controlsPerSlot; ++control)
        {
            const auto index = static_cast<size_t>(control);
            const auto unavailableCompressorDetector =
                type == megadsp::ModuleType::compressor
                && control == 7 && controlVisible[index]
                && !controlEnabled[index] && !controlPresented[index];
            const auto graphOwnedCompressorThreshold =
                type == megadsp::ModuleType::compressor
                && control == 0 && controlVisible[index]
                && !controlPresented[index];
            const auto graphOwnedEqualizerControl =
                type == megadsp::ModuleType::equalizer
                && controlVisible[index] && !controlPresented[index];
            if (unavailableCompressorDetector
                || graphOwnedCompressorThreshold
                || graphOwnedEqualizerControl)
                continue;
            if (controlVisible[index] != controlPresented[index])
                return false;
        }
        return true;
    }
    bool compactRotaryControlsWorkForTesting()
    {
        auto found = false;
        for (auto& slider : sliders)
        {
            if (!slider.isVisible()
                || slider.getSliderStyle()
                       != juce::Slider::RotaryHorizontalVerticalDrag)
                continue;
            found = true;
            if (!slider.compactInteractionWorksForTesting())
                return false;
        }
        return found;
    }
    juce::String controlTextForTesting(int control) const
    {
        if (!juce::isPositiveAndBelow(
                control, megadsp::controlsPerSlot))
            return {};
        const auto& slider =
            sliders[static_cast<size_t>(control)];
        for (int child = 0; child < slider.getNumChildComponents();
             ++child)
            if (const auto* label = dynamic_cast<const juce::Label*>(
                    slider.getChildComponent(child)))
                return label->getText();
        return const_cast<MeteredSlider&>(slider)
            .getTextFromValue(slider.getValue());
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
        if (key.getKeyCode() == juce::KeyPress::leftKey)
            return selectAdjacent(-1);
        if (key.getKeyCode() == juce::KeyPress::rightKey)
            return selectAdjacent(1);
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

    void loadAssetFile(const juce::File& file, int impulseLane = 0)
    {
        if (!acceptsAssetFile(file))
            return;
        editor.selectSlot(slot);
        const auto result = supportsNamModel
            ? processor.loadNamModel(slot, file)
            : processor.loadImpulseResponse(slot, impulseLane, file);
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

    void loadAdjacentNamModel(int direction)
    {
        const auto result =
            processor.loadAdjacentNamModel(slot, direction);
        if (result.failed())
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Could not browse NAM models",
                result.getErrorMessage());
        syncAssetName();
        graph->repaint();
    }

    void showImpulseLibraryMenu()
    {
        juce::PopupMenu menu;
        const auto root =
            processor.impulseResponseRootDirectory();
        menu.addSectionHeader(
            "IR LIBRARY - "
            + juce::String(processor.impulseResponseLibrarySize())
            + " FILES");
        menu.addItem(1, "Choose Root Directory...");
        menu.addItem(3, "Rebuild Index");
        if (root.isDirectory())
        {
            menu.addSeparator();
            menu.addSectionHeader(
                "ROOT: " + root.getFileName().substring(0, 36));
        }
        juce::Component::SafePointer<ModuleCard> safeThis(this);
        menu.showMenuAsync(
            juce::PopupMenu::Options{}.withTargetComponent(&impulseLibrary),
            [safeThis](int result)
            {
                if (safeThis == nullptr)
                    return;
                if (result == 1)
                    safeThis->chooseImpulseResponseRoot();
                else if (result == 3)
                {
                    const auto rebuilt =
                        safeThis->processor.rebuildImpulseResponseLibrary();
                    if (rebuilt.failed())
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::MessageBoxIconType::WarningIcon,
                            "Could not index IR library",
                            rebuilt.getErrorMessage());
                }
            });
    }

    void chooseImpulseResponseRoot()
    {
        impulseRootChooser = std::make_unique<juce::FileChooser>(
            "Choose impulse response root directory",
            processor.impulseResponseDirectory(slot));
        const auto flags =
            juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectDirectories;
        juce::Component::SafePointer<ModuleCard> safeThis(this);
        impulseRootChooser->launchAsync(
            flags, [safeThis](const juce::FileChooser& chooser)
            {
                if (safeThis == nullptr)
                    return;
                const auto directory = chooser.getResult();
                if (directory == juce::File())
                    return;
                const auto result =
                    safeThis->processor.setImpulseResponseRootDirectory(
                        directory);
                if (result.failed())
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::WarningIcon,
                        "Could not index IR library",
                        result.getErrorMessage());
            });
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
                visible && controlPresented[index]
                && sliderAttachments[index] != nullptr;
            const auto choiceVisible =
                visible && controlPresented[index]
                && choiceAttachments[index] != nullptr;
            const auto toggleVisible =
                visible && controlPresented[index]
                && toggleAttachments[index] != nullptr;
            sliders[index].setVisible(sliderVisible);
            choices[index].setVisible(choiceVisible);
            toggles[index].setVisible(toggleVisible);
            labels[index].setVisible(
                visible && controlPresented[index] && !toggleVisible);
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

    void timerCallback() override
    {
        updateContext();
        syncAssetName();
    }

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
        const auto viewLeft = editor.rackViewport.getViewPositionX();
        const auto viewRight = viewLeft + editor.rackViewport.getViewWidth();
        if (card->getX() < viewLeft)
            editor.rackViewport.setViewPosition(
                card->getX(), editor.rackViewport.getViewPositionY());
        else if (card->getRight() > viewRight)
            editor.rackViewport.setViewPosition(
                card->getRight() - editor.rackViewport.getViewWidth(),
                editor.rackViewport.getViewPositionY());
        return true;
    }

    void updateDropPosition(const SourceDetails& details)
    {
        const auto after = details.localPosition.x > getWidth() / 2;
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
                area.getHeight()).reduced(2, 1);
            if (toggleAttachments[index] != nullptr)
            {
                labels[index].setBounds({});
                const auto buttonWidth =
                    juce::jmin(220, juce::jmax(
                        1, controlArea.getWidth() - 8));
                toggles[index].setBounds(
                    juce::Rectangle<int>(
                        buttonWidth,
                        juce::jmin(24, controlArea.getHeight()))
                        .withCentre(controlArea.getCentre()));
                continue;
            }
            labels[index].setBounds(controlArea.removeFromTop(11));
            if (choiceAttachments[index] != nullptr)
                choices[index].setBounds(
                    controlArea.reduced(2, 0));
            else
            {
                sliders[index].setSliderStyle(
                    juce::Slider::RotaryHorizontalVerticalDrag);
                sliders[index].setTextBoxStyle(
                    juce::Slider::NoTextBox, false, 0, 0);
                sliders[index].setBounds(controlArea);
                sliders[index].updateText();
            }
        }
    }

    MegaDSPAudioProcessorEditor& editor;
    MegaDSPAudioProcessor& processor;
    int slot;
    megadsp::ModuleType type;
    juce::String moduleName;
    bool supportsImpulseResponse = false;
    bool isConvolution = false;
    bool isWormhole = false;
    bool supportsNamModel = false;
    juce::Label name;
    WidgetActionButton bypass { WidgetActionButton::Icon::power };
    juce::Label assetName;
    WidgetActionButton reset { WidgetActionButton::Icon::reset };
    WidgetActionButton remove { WidgetActionButton::Icon::remove };
    juce::TextButton loadImpulse;
    juce::TextButton previousImpulse;
    juce::TextButton nextImpulse;
    juce::TextButton impulseLibrary;
    juce::TextButton clearImpulse;
    std::unique_ptr<juce::FileChooser> impulseChooser;
    std::unique_ptr<juce::FileChooser> impulseRootChooser;
    juce::TextButton loadImpulse2;
    juce::TextButton clearImpulse2;
    std::unique_ptr<juce::FileChooser> impulseChooser2;
    juce::TextButton loadNam;
    juce::TextButton previousNam;
    juce::TextButton nextNam;
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
    std::array<bool, megadsp::controlsPerSlot> controlPresented {};
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

class MegaDSPAudioProcessorEditor::ChainIconButton final
    : public juce::Button
{
public:
    enum class Icon
    {
        add,
        remove
    };

    explicit ChainIconButton(Icon iconToUse)
        : juce::Button(iconToUse == Icon::add ? "Insert" : "Remove"),
          icon(iconToUse)
    {
    }

    void paintButton(
        juce::Graphics& graphics, bool highlighted, bool down) override
    {
        const auto enabled = isEnabled();
        const auto base = icon == Icon::add
            ? juce::Colour(0xff222c38)
            : juce::Colour(0xff33404e);
        graphics.setColour(
            enabled
                ? base.brighter(down ? 0.22f : highlighted ? 0.12f : 0.0f)
                : base.withAlpha(0.35f));
        graphics.fillRoundedRectangle(
            getLocalBounds().toFloat(), icon == Icon::add ? 4.0f : 3.0f);

        auto iconBounds = getLocalBounds().toFloat().reduced(
            icon == Icon::add ? 5.0f : 4.0f);
        const auto centre = iconBounds.getCentre();
        const auto radius = 0.5f * juce::jmin(
            iconBounds.getWidth(), iconBounds.getHeight());
        juce::Path path;
        path.startNewSubPath(centre.x - radius, centre.y);
        path.lineTo(centre.x + radius, centre.y);
        if (icon == Icon::add)
        {
            path.startNewSubPath(centre.x, centre.y - radius);
            path.lineTo(centre.x, centre.y + radius);
        }
        else
        {
            path.clear();
            path.startNewSubPath(
                centre.x - radius, centre.y - radius);
            path.lineTo(
                centre.x + radius, centre.y + radius);
            path.startNewSubPath(
                centre.x + radius, centre.y - radius);
            path.lineTo(
                centre.x - radius, centre.y + radius);
        }
        graphics.setColour(
            juce::Colours::white.withAlpha(enabled ? 0.9f : 0.35f));
        graphics.strokePath(
            path, juce::PathStrokeType(
                      1.6f, juce::PathStrokeType::curved,
                      juce::PathStrokeType::rounded));
    }

private:
    Icon icon;
};

class MegaDSPAudioProcessorEditor::ChainSlotButton final
    : public juce::TextButton,
      public juce::DragAndDropTarget
{
public:
    ChainSlotButton(
        MegaDSPAudioProcessorEditor& editorToUse, int slotToUse)
        : editor(editorToUse), slot(slotToUse)
    {
        removeButton.setTitle(
            "Remove processor from slot " + juce::String(slot + 1));
        removeButton.setDescription(
            "Removes this processor and closes the gap in the signal chain.");
        removeButton.setTooltip(
            "Remove this processor from the signal chain.");
        removeButton.onClick = [this]
        {
            editor.requestRemoveSlot(slot);
        };
        addAndMakeVisible(removeButton);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        dragStart = event.position;
        editor.selectSlot(slot);
        juce::TextButton::mouseDown(event);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (event.position.getDistanceFrom(dragStart) > 5.0f
            && !editor.isDragAndDropActive())
            editor.startDragging(slot, this);
    }

    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
        if (event.mods.isLeftButtonDown())
        {
            editor.audioProcessor.toggleSlotBypass(slot);
            editor.refreshChainNavigator();
        }
    }

    void resized() override
    {
        removeButton.setBounds(
            getLocalBounds().removeFromRight(18).reduced(1, 3));
    }

    void setDisplayText(
        const juce::String& text, bool isBypassed)
    {
        bypassed = isBypassed;
        setButtonText(text + (bypassed ? "  BYP" : ""));
        repaint();
    }

    bool isInterestedInDragSource(
        const SourceDetails& details) override
    {
        const auto* source = details.sourceComponent.get();
        const auto fromRack =
            dynamic_cast<const ModuleCard*>(source) != nullptr;
        const auto fromChain =
            dynamic_cast<const ChainSlotButton*>(source) != nullptr;
        const auto sourceSlot = static_cast<int>(details.description);
        return (fromRack || fromChain)
            && juce::isPositiveAndBelow(
                sourceSlot, static_cast<int>(editor.cards.size()))
            && sourceSlot != slot;
    }

    void itemDragEnter(const SourceDetails& details) override
    {
        dragOver = true;
        updateDropPosition(details);
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
        repaint();
        reorderFrom(
            static_cast<int>(details.description), dropAfter);
    }

    void paintButton(
        juce::Graphics& graphics, bool highlighted, bool down) override
    {
        juce::TextButton::paintButton(graphics, highlighted, down);
        if (bypassed)
        {
            graphics.setColour(juce::Colours::black.withAlpha(0.24f));
            graphics.fillRect(getLocalBounds().withTrimmedRight(19));
        }
        if (!dragOver)
            return;
        graphics.setColour(accent);
        const auto markerX = dropAfter ? getWidth() - 2 : 0;
        graphics.fillRect(markerX, 1, 3, juce::jmax(1, getHeight() - 2));
    }

    void reorderFrom(int source, bool after)
    {
        auto destination = slot + (after ? 1 : 0);
        if (source < destination)
            --destination;
        destination = juce::jlimit(
            0, static_cast<int>(editor.cards.size()) - 1, destination);
        editor.requestReorderSlot(source, destination);
    }

private:
    void updateDropPosition(const SourceDetails& details)
    {
        const auto newDropAfter =
            static_cast<float>(details.localPosition.x)
                >= static_cast<float>(getWidth()) * 0.5f;
        if (dropAfter != newDropAfter)
        {
            dropAfter = newDropAfter;
            repaint();
        }
    }

    MegaDSPAudioProcessorEditor& editor;
    int slot = 0;
    ChainIconButton removeButton { ChainIconButton::Icon::remove };
    juce::Point<float> dragStart;
    bool bypassed = false;
    bool dragOver = false;
    bool dropAfter = false;
};

MegaDSPAudioProcessorEditor::MegaDSPAudioProcessorEditor(
    MegaDSPAudioProcessor& processorToUse, int width, int height)
    : AudioProcessorEditor(processorToUse), audioProcessor(processorToUse)
{
    title.setText("megaDSP", juce::dontSendNotification);
    title.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    title.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(title);

    commitHashButton.setButtonText(
        juce::String::fromUTF8(
            megadsp::buildCommitHash.data(),
            static_cast<int>(megadsp::buildCommitHash.size())));
    commitHashButton.setTitle("Build commit");
    commitHashButton.setDescription(
        "Copies this megaDSP build's Git commit hash.");
    commitHashButton.setTooltip("Click to copy the build commit hash.");
    commitHashButton.setColour(
        juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    commitHashButton.setColour(
        juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    commitHashButton.setColour(
        juce::TextButton::textColourOffId, juce::Colour(0xffa8b3c0));
    commitHashButton.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    commitHashButton.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard(
            commitHashButton.getButtonText());
    };
    addAndMakeVisible(commitHashButton);

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
    factoryPresets.setScrollWheelEnabled(false);
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
    for (auto* button : {
             &smallWidgetsButton,
             &mediumWidgetsButton,
             &largeWidgetsButton })
    {
        button->setClickingTogglesState(false);
        snapshotsHost.addAndMakeVisible(*button);
    }
    smallWidgetsButton.setTooltip(
        "Use compact 300-pixel module widgets.");
    mediumWidgetsButton.setTooltip(
        "Use medium 440-pixel module widgets.");
    largeWidgetsButton.setTooltip(
        "Use large 640-pixel module widgets.");
    smallWidgetsButton.onClick = [this]
    {
        setWidgetSize(megadsp::ui::WidgetSize::small, true);
    };
    mediumWidgetsButton.onClick = [this]
    {
        setWidgetSize(megadsp::ui::WidgetSize::medium, true);
    };
    largeWidgetsButton.onClick = [this]
    {
        setWidgetSize(megadsp::ui::WidgetSize::large, true);
    };
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

    for (int position = 0; position <= megadsp::slotCount; ++position)
    {
        auto button = std::make_unique<ChainIconButton>(
            ChainIconButton::Icon::add);
        button->setTitle(
            "Insert processor at signal-flow position "
            + juce::String(position + 1));
        button->onClick = [this, position]
        {
            showModuleBrowser(position);
        };
        rackContent.addAndMakeVisible(*button);
        chainInsertButtons[static_cast<size_t>(position)] =
            std::move(button);
    }

    addAndMakeVisible(rackViewport);
    rackViewport.setViewedComponent(&rackContent, false);
    rackViewport.setScrollBarsShown(false, true);
    rackViewport.setWantsKeyboardFocus(false);
    rackViewport.setTitle("Module rack");
    rackViewport.setDescription(
        "Modules process audio from left to right.");
    addButton.setTooltip("Browse and add a module after the last widget.");
    addButton.onClick = [this] { showModuleBrowser(); };
    rackContent.addAndMakeVisible(addButton);
    widgetSize = megadsp::ui::widgetSizeFromId(
        audioProcessor.globalWidgetSizeId());
    refreshWidgetSizeButtons();
    selectedSlot = audioProcessor.getSelectedTab();
    refreshCards();

    setResizable(true, true);
    updateEditorResizeLimits();
    const auto limits = megadsp::ui::calculateEditorResizeLimits(
        knownDisplayUserArea);
    setSize(
        juce::jlimit(
            limits.getX(), limits.getWidth(), width),
        juce::jlimit(
            limits.getY(), limits.getHeight(), height));
#if defined(MEGADSP_CAPTURE_SCREENSHOTS)
    setSize(1000, 300);
    audioProcessor.setInstanceName("Amp Space");
    audioProcessor.setBackgroundThemeIndex(0);
    while (audioProcessor.getRack().activeSlotCount() > 0)
        audioProcessor.removeSlot(0);
    audioProcessor.addModule(megadsp::ModuleType::neuralAmpModeler);
    audioProcessor.addModule(megadsp::ModuleType::convolutionReverb);
    widgetSize = megadsp::ui::WidgetSize::small;
    refreshWidgetSizeButtons();
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
    audioProcessor.getVisualizationData().clearActiveSlots();
}

void MegaDSPAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    const auto theme = backgroundColour(
        audioProcessor.getBackgroundThemeIndex());
    graphics.fillAll(theme);
    graphics.setColour(theme.brighter(0.22f));
    graphics.fillRect(0, 0, getWidth(), 38);
}

void MegaDSPAudioProcessorEditor::visibilityChanged()
{
    const auto showing = isShowing();
    for (auto& card : cards)
        card->setEditorVisible(showing);
    if (!showing)
        audioProcessor.getVisualizationData().clearActiveSlots();
}

void MegaDSPAudioProcessorEditor::moved()
{
    updateEditorResizeLimits();
}

void MegaDSPAudioProcessorEditor::parentHierarchyChanged()
{
    updateEditorResizeLimits();
    resized();
    const auto showing = isShowing();
    for (auto& card : cards)
        card->setEditorVisible(showing);
    repaint();
}

void MegaDSPAudioProcessorEditor::updateEditorResizeLimits()
{
    const auto& displays =
        juce::Desktop::getInstance().getDisplays();
    const auto* display =
        displays.getDisplayForRect(getScreenBounds());
    if (display == nullptr)
        display = displays.getPrimaryDisplay();
    const auto userArea = display != nullptr
        ? display->userArea : juce::Rectangle<int> {};
    if (userArea == knownDisplayUserArea)
        return;
    knownDisplayUserArea = userArea;
    const auto limits =
        megadsp::ui::calculateEditorResizeLimits(userArea);
    setResizeLimits(
        limits.getX(), limits.getY(),
        limits.getWidth(), limits.getHeight());
}

bool MegaDSPAudioProcessorEditor::keyPressed(
    const juce::KeyPress& key)
{
    const auto modifiers = key.getModifiers();
    const auto shortcut =
        modifiers.isCommandDown() || modifiers.isCtrlDown();
    const auto keyCharacter = juce::CharacterFunctions::toLowerCase(
        static_cast<juce::juce_wchar>(key.getKeyCode()));
    if (shortcut && !modifiers.isAltDown() && keyCharacter == 'z')
    {
        const auto redo = modifiers.isShiftDown();
        const auto description = redo
            ? audioProcessor.redoDescription()
            : audioProcessor.undoDescription();
        const auto changed = redo
            ? audioProcessor.redo()
            : audioProcessor.undo();
        if (!changed)
            return false;
        status.setText(
            juce::String(redo ? "Redo: " : "Undo: ") + description,
            juce::dontSendNotification);
        refreshCards(true);
        refreshSnapshots();
        refreshIdentityPresentation();
        resized();
        repaint();
        return true;
    }
    return AudioProcessorEditor::keyPressed(key);
}

void MegaDSPAudioProcessorEditor::resized()
{
    if (getWidth() > 0 && getHeight() > 0)
        audioProcessor.rememberEditorSize(getWidth(), getHeight());
    auto bounds = getLocalBounds();
    auto headerTop = bounds.removeFromTop(38).reduced(8, 4);
    const auto identityLayout =
        megadsp::ui::calculateIdentityHeaderLayout(getWidth());
    auto titleArea =
        headerTop.removeFromLeft(identityLayout.titleWidth);
    title.setBounds(titleArea.removeFromTop(18));
    commitHashButton.setBounds(titleArea);
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
    auto outputArea = headerTop.removeFromRight(134);
    outputLabel.setBounds(outputArea.removeFromLeft(44));
    outputGain.setBounds(outputArea);
    auto inputArea = headerTop.removeFromLeft(126);
    inputLabel.setBounds(inputArea.removeFromLeft(32));
    inputGain.setBounds(inputArea);
    headerMeter->setBounds(headerTop.reduced(4, 1));
    status.setBounds({});

    snapshotsHost.setBounds(
        bounds.removeFromTop(24).reduced(8, 1));
    auto snapshotBounds = snapshotsHost.getLocalBounds();
    snapshotsLabel.setBounds(snapshotBounds.removeFromLeft(66));
    liveSafeButton.setBounds(
        snapshotBounds.removeFromRight(84).reduced(1));
    largeWidgetsButton.setBounds(
        snapshotBounds.removeFromRight(28).reduced(1));
    mediumWidgetsButton.setBounds(
        snapshotBounds.removeFromRight(28).reduced(1));
    smallWidgetsButton.setBounds(
        snapshotBounds.removeFromRight(28).reduced(1));
    const auto snapshotWidth =
        juce::jmax(1, snapshotBounds.getWidth()
                       / megadsp::snapshotCount);
    for (auto& button : snapshotButtons)
        button->setBounds(
            snapshotBounds.removeFromLeft(snapshotWidth).reduced(1));

    chainNavigatorHost.setBounds({});

    rackViewport.setBounds(bounds.reduced(8, 4));
    constexpr auto rackPadding = 8;
    const auto showAddButton =
        static_cast<int>(cards.size()) < megadsp::slotCount;
    const auto contentWidth = megadsp::ui::calculateRackContentWidth(
        static_cast<int>(cards.size()), widgetSize, showAddButton)
        + rackPadding * 2;
    const auto targetWidth =
        juce::jmax(rackViewport.getWidth(), contentWidth);
    const auto targetHeight =
        juce::jmax(1, rackViewport.getMaximumVisibleHeight());
    rackContent.setSize(targetWidth, targetHeight);
    for (auto& button : chainInsertButtons)
        button->setBounds({});
    auto cardBounds = rackContent.getLocalBounds().reduced(0, rackPadding);
    cardBounds.removeFromLeft(rackPadding);
    for (int index = 0; index < static_cast<int>(cards.size()); ++index)
    {
        if (showAddButton)
            chainInsertButtons[static_cast<size_t>(index)]->setBounds(
                cardBounds.removeFromLeft(24).reduced(2));
        auto& card = cards[static_cast<size_t>(index)];
        card->setBounds(
            cardBounds.removeFromLeft(
                megadsp::ui::widgetWidth(widgetSize)));
        if (index + 1 < static_cast<int>(cards.size()))
            cardBounds.removeFromLeft(megadsp::ui::moduleCardGap);
    }
    if (showAddButton)
        chainInsertButtons[cards.size()]->setBounds(
            cardBounds.removeFromLeft(24).reduced(2));
    addButton.setBounds({});
    for (auto& card : cards)
        card->reassertVisualizationCapture();
}

void MegaDSPAudioProcessorEditor::timerCallback()
{
    headerMeter->setLevels(audioProcessor.getRack().inputMeterDb(),
                           audioProcessor.getRack().outputMeterDb());
    for (auto& card : cards)
        card->syncBypassState();
    refreshChainNavigator();
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
        audioProcessor.getVisualizationData().clearActiveSlots();
        return;
    }
    selectedSlot = juce::jlimit(0, active - 1, slot);
    audioProcessor.setSelectedTab(selectedSlot);
    for (int index = 0; index < static_cast<int>(cards.size()); ++index)
        cards[static_cast<size_t>(index)]->setSelected(index == selectedSlot);
    for (int index = 0; index < megadsp::slotCount; ++index)
        if (auto* button =
                chainNavigatorButtons[static_cast<size_t>(index)].get())
            button->setToggleState(
                index == selectedSlot, juce::dontSendNotification);
    if (!isShowing())
        audioProcessor.getVisualizationData().clearActiveSlots();
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
            card->setEditorVisible(isShowing());
            cards.push_back(std::move(card));
        }
        addButton.setVisible(false);
    }
    refreshChainNavigator();
    selectSlot(juce::jmin(selectedSlot, juce::jmax(0, active - 1)));
    resized();
    revealSelectedCard(hadRackFocus);
}

void MegaDSPAudioProcessorEditor::refreshChainNavigator()
{
    const auto active = static_cast<int>(cards.size());
    const auto canInsert = active < megadsp::slotCount;
    chainNavigatorHost.setVisible(false);
    for (int slot = 0; slot < megadsp::slotCount; ++slot)
    {
        auto& button =
            chainNavigatorButtons[static_cast<size_t>(slot)];
        if (button == nullptr)
            continue;
        const auto visible = slot < active;
        button->setVisible(visible);
        if (!visible)
            continue;
        const auto type = audioProcessor.getRack().moduleType(slot);
        const auto name = megadsp::ui::compactTabName(type);
        const auto bypassed = audioProcessor.isSlotBypassed(slot);
        button->setDisplayText(
            juce::String(slot + 1) + "  " + name, bypassed);
        button->setDescription(
            "Jump to slot " + juce::String(slot + 1) + ", "
            + juce::String(megadsp::moduleDefinition(type).displayName)
            + (bypassed ? ", bypassed. " : ", active. ")
            + "Drag to rearrange signal flow; double-click to toggle bypass.");
        button->setTooltip(
            "Jump to slot " + juce::String(slot + 1) + ": "
            + juce::String(megadsp::moduleDefinition(type).displayName)
            + (bypassed ? " (BYPASSED). " : ". ")
            + "Drag to rearrange; double-click to toggle bypass.");
    }
    for (int position = 0; position <= megadsp::slotCount; ++position)
    {
        auto& button =
            chainInsertButtons[static_cast<size_t>(position)];
        const auto visible = canInsert && position <= active;
        button->setVisible(visible);
        button->setEnabled(canInsert);
        if (!visible)
            continue;
        const auto location = active == 0
            ? juce::String("as the first processor")
            : position == active
                ? juce::String("at the end of the chain")
                : position == 0
                    ? juce::String("at the beginning of the chain")
                    : juce::String("between slots ")
                          + juce::String(position) + " and "
                          + juce::String(position + 1);
        button->setDescription(
            canInsert ? "Insert a processor " + location + "."
                      : "The eight-slot chain is full.");
        button->setTooltip(
            canInsert ? "Insert a processor " + location + "."
                      : "The eight-slot chain is full.");
    }
}

void MegaDSPAudioProcessorEditor::revealSelectedCard(bool focus)
{
    if (!juce::isPositiveAndBelow(
            selectedSlot, static_cast<int>(cards.size())))
        return;
    auto* card = cards[static_cast<size_t>(selectedSlot)].get();
    const auto viewLeft = rackViewport.getViewPositionX();
    const auto viewRight = viewLeft + rackViewport.getViewWidth();
    if (card->getWidth() > rackViewport.getViewWidth())
        rackViewport.setViewPosition(
            card->getX(), rackViewport.getViewPositionY());
    else if (card->getX() < viewLeft)
        rackViewport.setViewPosition(
            card->getX(), rackViewport.getViewPositionY());
    else if (card->getRight() > viewRight)
        rackViewport.setViewPosition(
            card->getRight() - rackViewport.getViewWidth(),
            rackViewport.getViewPositionY());
    if (focus)
        card->grabKeyboardFocus();
}

void MegaDSPAudioProcessorEditor::setWidgetSize(
    megadsp::ui::WidgetSize size, bool persist)
{
    if (widgetSize == size)
        return;
    widgetSize = size;
    refreshWidgetSizeButtons();
    resized();
    revealSelectedCard(false);
    if (persist)
    {
        const auto result = audioProcessor.setGlobalWidgetSizeId(
            megadsp::ui::widgetSizeId(size));
        if (result.failed())
            showResult(result);
    }
}

void MegaDSPAudioProcessorEditor::refreshWidgetSizeButtons()
{
    const auto update =
        [this](juce::TextButton& button,
               megadsp::ui::WidgetSize size)
    {
        const auto selected = widgetSize == size;
        button.setToggleState(
            selected, juce::dontSendNotification);
        button.setColour(
            juce::TextButton::buttonColourId,
            selected ? accent : juce::Colour(0xff1a222c));
        button.setColour(
            juce::TextButton::textColourOffId,
            selected ? juce::Colours::black
                     : juce::Colours::white);
    };
    update(smallWidgetsButton, megadsp::ui::WidgetSize::small);
    update(mediumWidgetsButton, megadsp::ui::WidgetSize::medium);
    update(largeWidgetsButton, megadsp::ui::WidgetSize::large);
}

#if defined(MEGADSP_TESTS)
void MegaDSPAudioProcessorEditor::clickChainNavigatorForTesting(int slot)
{
    if (juce::isPositiveAndBelow(slot, static_cast<int>(cards.size())))
    {
        selectSlot(slot);
        revealSelectedCard(false);
    }
}

void MegaDSPAudioProcessorEditor::reorderChainNavigatorForTesting(
    int source, int target, bool dropAfter)
{
    if (juce::isPositiveAndBelow(
            source, static_cast<int>(cards.size()))
        && juce::isPositiveAndBelow(
            target, static_cast<int>(cards.size())))
    {
        auto destination = target + (dropAfter ? 1 : 0);
        if (source < destination)
            --destination;
        requestReorderSlot(
            source, juce::jlimit(
                        0, static_cast<int>(cards.size()) - 1,
                        destination));
    }
    applyPendingCardMutation();
}

void MegaDSPAudioProcessorEditor::insertModuleFromChainForTesting(
    megadsp::ModuleType type, int position)
{
    addModuleAt(type, position);
}

void MegaDSPAudioProcessorEditor::doubleClickChainNavigatorForTesting(
    int slot)
{
    if (!juce::isPositiveAndBelow(slot, static_cast<int>(cards.size())))
        return;
    audioProcessor.toggleSlotBypass(slot);
    refreshChainNavigator();
}

void MegaDSPAudioProcessorEditor::removeModuleFromChainForTesting(int slot)
{
    if (!juce::isPositiveAndBelow(slot, static_cast<int>(cards.size())))
        return;
    requestRemoveSlot(slot);
    applyPendingCardMutation();
}

int MegaDSPAudioProcessorEditor::chainNavigatorButtonCountForTesting() const
{
    return static_cast<int>(std::count_if(
        chainNavigatorButtons.begin(), chainNavigatorButtons.end(),
        [](const auto& button) { return button->isVisible(); }));
}

int MegaDSPAudioProcessorEditor::chainInsertButtonCountForTesting() const
{
    return static_cast<int>(std::count_if(
        chainInsertButtons.begin(), chainInsertButtons.end(),
        [](const auto& button) { return button->isVisible(); }));
}

bool MegaDSPAudioProcessorEditor::selectedCardVisibleForTesting() const
{
    if (!juce::isPositiveAndBelow(
            selectedSlot, static_cast<int>(cards.size())))
        return false;
    const auto* card = cards[static_cast<size_t>(selectedSlot)].get();
    const auto viewLeft = rackViewport.getViewPositionX();
    const auto viewRight = viewLeft + rackViewport.getViewWidth();
    if (card->getWidth() > rackViewport.getViewWidth())
        return card->getX() >= viewLeft && card->getX() < viewRight;
    return card->getX() >= viewLeft && card->getRight() <= viewRight;
}

int MegaDSPAudioProcessorEditor::cardHeightForTesting(int slot) const
{
    return juce::isPositiveAndBelow(
               slot, static_cast<int>(cards.size()))
        ? cards[static_cast<size_t>(slot)]->getHeight()
        : 0;
}

int MegaDSPAudioProcessorEditor::cardWidthForTesting(int slot) const
{
    return juce::isPositiveAndBelow(
               slot, static_cast<int>(cards.size()))
        ? cards[static_cast<size_t>(slot)]->getWidth()
        : 0;
}

int MegaDSPAudioProcessorEditor::cardGraphHeightForTesting(int slot) const
{
    return juce::isPositiveAndBelow(slot, static_cast<int>(cards.size()))
        ? cards[static_cast<size_t>(slot)]->graphHeightForTesting()
        : 0;
}

int MegaDSPAudioProcessorEditor::cardGraphWidthForTesting(int slot) const
{
    return juce::isPositiveAndBelow(slot, static_cast<int>(cards.size()))
        ? cards[static_cast<size_t>(slot)]->graphWidthForTesting()
        : 0;
}

int MegaDSPAudioProcessorEditor::activeVisualizationCardCountForTesting() const
{
    return static_cast<int>(std::count_if(
        cards.begin(), cards.end(),
        [](const auto& card)
        {
            return card->isVisualizationActiveForTesting();
        }));
}

void MegaDSPAudioProcessorEditor::setCardsEditorVisibleForTesting(bool visible)
{
    for (auto& card : cards)
        card->setEditorVisible(visible);
    if (!visible)
        audioProcessor.getVisualizationData().clearActiveSlots();
}

bool MegaDSPAudioProcessorEditor::cardChildrenFitForTesting() const
{
    return std::all_of(
        cards.begin(), cards.end(),
        [](const auto& card) { return card->childrenFitForTesting(); });
}

bool MegaDSPAudioProcessorEditor::cardGraphsAvoidControlsForTesting() const
{
    return std::all_of(
        cards.begin(), cards.end(),
        [](const auto& card)
        {
            return card->graphAndControlsAreSeparatedForTesting();
        });
}

bool MegaDSPAudioProcessorEditor::
    cardPresentsAllActiveControlsForTesting() const
{
    return std::all_of(
        cards.begin(), cards.end(),
        [](const auto& card)
        {
            return card->presentsAllActiveControlsForTesting();
        });
}

juce::String MegaDSPAudioProcessorEditor::cardControlTextForTesting(
    int slot, int control) const
{
    if (!juce::isPositiveAndBelow(
            slot, static_cast<int>(cards.size())))
        return {};
    return cards[static_cast<size_t>(slot)]
        ->controlTextForTesting(control);
}

bool MegaDSPAudioProcessorEditor::
    cardRotaryControlsWorkForTesting(int slot)
{
    return juce::isPositiveAndBelow(
               slot, static_cast<int>(cards.size()))
        && cards[static_cast<size_t>(slot)]
               ->compactRotaryControlsWorkForTesting();
}

bool MegaDSPAudioProcessorEditor::
    cardHeightsMatchPreferencesForTesting() const
{
    return std::all_of(
        cards.begin(), cards.end(),
        [](const auto& card)
        {
            return card->getHeight() == card->preferredHeight();
        });
}

bool MegaDSPAudioProcessorEditor::rackOverflowsVerticallyForTesting() const
{
    return rackContent.getHeight() > rackViewport.getHeight();
}

bool MegaDSPAudioProcessorEditor::rackOverflowsHorizontallyForTesting() const
{
    return rackContent.getWidth() > rackViewport.getWidth();
}

void MegaDSPAudioProcessorEditor::setWidgetSizeForTesting(
    megadsp::ui::WidgetSize size)
{
    setWidgetSize(size, false);
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

void MegaDSPAudioProcessorEditor::addModuleAt(
    megadsp::ModuleType type, int position)
{
    const auto active = audioProcessor.getRack().activeSlotCount();
    if (active >= megadsp::slotCount)
        return;
    const auto insertion = juce::jlimit(0, active, position);
    audioProcessor.beginUndoTransaction("Insert module");
    audioProcessor.addModule(type);
    if (audioProcessor.getRack().activeSlotCount() != active + 1)
    {
        audioProcessor.endUndoTransaction();
        return;
    }
    if (insertion < active)
        audioProcessor.moveSlot(active, insertion);
    audioProcessor.endUndoTransaction();
    selectedSlot = insertion;
    refreshCards();
    revealSelectedCard(true);
}

void MegaDSPAudioProcessorEditor::showModuleBrowser(int insertPosition)
{
    const auto active = audioProcessor.getRack().activeSlotCount();
    if (active >= megadsp::slotCount)
        return;
    const auto insertion = juce::jlimit(
        0, active, insertPosition < 0 ? active : insertPosition);

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
    auto* target = insertPosition < 0
        ? static_cast<juce::Component*>(&addButton)
        : static_cast<juce::Component*>(
              chainInsertButtons[static_cast<size_t>(insertion)].get());
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(target),
        [safeThis, insertion](int result)
        {
            if (safeThis == nullptr || result == 0)
                return;
            if (result == searchCommand)
            {
                safeThis->showModuleSearch(insertion);
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
            safeThis->addModuleAt(
                static_cast<megadsp::ModuleType>(stableType), insertion);
        });
}

void MegaDSPAudioProcessorEditor::showModuleSearch(int insertPosition)
{
    const auto active = audioProcessor.getRack().activeSlotCount();
    if (active >= megadsp::slotCount)
        return;
    const auto insertion = juce::jlimit(
        0, active, insertPosition < 0 ? active : insertPosition);

    juce::Component::SafePointer<MegaDSPAudioProcessorEditor> safeThis(this);
    auto browser = std::make_unique<megadsp::ui::ModuleBrowser>(
        backgroundColour(audioProcessor.getBackgroundThemeIndex()),
        [safeThis, insertion](megadsp::ModuleType type)
        {
            if (safeThis == nullptr
                || safeThis->audioProcessor.getRack().activeSlotCount()
                       >= megadsp::slotCount)
                return;
            safeThis->addModuleAt(type, insertion);
        });
    auto* browserPointer = browser.get();
    auto* target = insertPosition < 0
        ? static_cast<juce::Component*>(&addButton)
        : static_cast<juce::Component*>(
              chainInsertButtons[static_cast<size_t>(insertion)].get());
    auto& callout = juce::CallOutBox::launchAsynchronously(
        std::move(browser), getLocalArea(target, target->getLocalBounds()),
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
              screenshotOverlay->getLocalBounds(), true, 2.0f)
        : createComponentSnapshot(getLocalBounds(), true, 2.0f);
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
            audioProcessor.setInstanceName("Echo Space");
            audioProcessor.setBackgroundThemeIndex(2);
            audioProcessor.loadFactoryPreset(3);
            selectedSlot = 1;
            refreshCards();
            break;
        case 3:
            captureScreenshot("delay-movement.png");
            audioProcessor.setInstanceName("Drum Bus");
            audioProcessor.setBackgroundThemeIndex(3);
            audioProcessor.loadFactoryPreset(2);
            selectedSlot = 0;
            refreshCards();
            break;
        case 4:
            captureScreenshot("saturation-dynamics.png");
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
        case 5:
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
