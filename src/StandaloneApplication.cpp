#include "StandaloneAudioSettings.h"
#include <juce_audio_plugin_client/detail/juce_CreatePluginFilter.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace
{
class StandaloneWindow final : public juce::DocumentWindow
{
public:
    explicit StandaloneWindow(std::unique_ptr<juce::StandalonePluginHolder> holder)
        : DocumentWindow(
              JucePlugin_Name,
              juce::LookAndFeel::getDefaultLookAndFeel().findColour(
                  juce::ResizableWindow::backgroundColourId),
              juce::DocumentWindow::allButtons),
          pluginHolder(std::move(holder))
    {
        setUsingNativeTitleBar(true);
        setResizable(true, false);
        updateContent();
        centreWithSize(getWidth(), getHeight());
        if (auto* settings = pluginHolder->settings.get())
        {
            const juce::Point<int> position {
                settings->getIntValue("windowX", getX()),
                settings->getIntValue("windowY", getY())
            };
            if (auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForPoint(position))
            {
                const auto area = display->userArea;
                setTopLeftPosition(
                    juce::jlimit(area.getX(), juce::jmax(area.getX(), area.getRight() - getWidth()), position.x),
                    juce::jlimit(area.getY(), juce::jmax(area.getY(), area.getBottom() - getHeight()), position.y));
            }
        }
    }

    ~StandaloneWindow() override
    {
        settingsWindow.deleteAndZero();
        if (auto* settings = pluginHolder->settings.get())
        {
            settings->setValue("windowX", getX());
            settings->setValue("windowY", getY());
        }
        pluginHolder->stopPlaying();
        clearContentComponent();
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

    void saveState()
    {
        pluginHolder->savePluginState();
        pluginHolder->saveAudioDeviceState();
    }

private:
    class Content final : public juce::Component
    {
    public:
        explicit Content(StandaloneWindow& window)
            : editor(window.pluginHolder->processor->createEditorIfNeeded()),
              settingsButton("Settings"),
              muteInput("Mute audio input")
        {
            settingsButton.onClick = [&window, this]
            {
                window.showSettingsMenu(settingsButton);
            };
            muteInput.getToggleStateValue().referTo(
                window.pluginHolder->getMuteInputValue());
            addAndMakeVisible(settingsButton);
            addAndMakeVisible(muteInput);
            addAndMakeVisible(*editor);
            setSize(editor->getWidth(), editor->getHeight() + toolbarHeight);
        }

        void resized() override
        {
            const juce::ScopedValueSetter<bool> guard(resizing, true);
            auto area = getLocalBounds();
            auto toolbar = area.removeFromTop(toolbarHeight).reduced(8, 4);
            settingsButton.setBounds(toolbar.removeFromLeft(100));
            toolbar.removeFromLeft(12);
            muteInput.setBounds(toolbar.removeFromLeft(200));
            editor->setBounds(area);
        }

        void childBoundsChanged(juce::Component* child) override
        {
            if (child == editor.get() && !resizing)
                setSize(editor->getWidth(), editor->getHeight() + toolbarHeight);
        }

    private:
        std::unique_ptr<juce::AudioProcessorEditor> editor;
        juce::TextButton settingsButton;
        juce::ToggleButton muteInput;
        bool resizing = false;
    };

    void updateContent()
    {
        setContentOwned(new Content(*this), true);
        if (auto* editor = pluginHolder->processor->getActiveEditor())
            if (auto* limits = editor->getConstrainer())
            {
                const auto border = getContentComponentBorder();
                setResizeLimits(
                    limits->getMinimumWidth() + border.getLeftAndRight(),
                    limits->getMinimumHeight() + toolbarHeight + border.getTopAndBottom(),
                    limits->getMaximumWidth() + border.getLeftAndRight(),
                    limits->getMaximumHeight() + toolbarHeight + border.getTopAndBottom());
            }
    }

    void showSettingsMenu(juce::Button& button)
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Audio/MIDI Settings...");
        menu.addSeparator();
        menu.addItem(2, "Save current state...");
        menu.addItem(3, "Load a saved state...");
        menu.addItem(4, "Reset to default state");
        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(&button),
            [safe = juce::Component::SafePointer<StandaloneWindow>(this)](int result)
            {
                if (safe != nullptr)
                    safe->handleMenuResult(result);
            });
    }

    void handleMenuResult(int result)
    {
        switch (result)
        {
            case 1:
            {
                if (settingsWindow != nullptr)
                {
                    settingsWindow->toFront(true);
                    break;
                }
                juce::DialogWindow::LaunchOptions options;
                options.content.setOwned(new megadsp::StandaloneAudioSettings(
                    pluginHolder->deviceManager, pluginHolder->getMuteInputValue()));
                options.dialogTitle = "Audio/MIDI Settings";
                options.dialogBackgroundColour = getBackgroundColour();
                options.escapeKeyTriggersCloseButton = true;
                options.useNativeTitleBar = true;
                options.resizable = true;
                settingsWindow = options.launchAsync();
                break;
            }
            case 2: pluginHolder->askUserToSaveState(); break;
            case 3: pluginHolder->askUserToLoadState(); break;
            case 4:
                pluginHolder->stopPlaying();
                clearContentComponent();
                pluginHolder->deletePlugin();
                if (auto* settings = pluginHolder->settings.get())
                    settings->removeValue("filterState");
                pluginHolder->createPlugin();
                updateContent();
                pluginHolder->startPlaying();
                break;
            default: break;
        }
    }

    static constexpr int toolbarHeight = 38;
    std::unique_ptr<juce::StandalonePluginHolder> pluginHolder;
    juce::Component::SafePointer<juce::DialogWindow> settingsWindow;
};

class StandaloneApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JucePlugin_Name; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override
    {
        juce::PropertiesFile::Options options;
        options.applicationName = getApplicationName();
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
#if JUCE_LINUX || JUCE_BSD
        options.folderName = "~/.config";
#endif
        properties.setStorageParameters(options);
        window = std::make_unique<StandaloneWindow>(
            std::make_unique<juce::StandalonePluginHolder>(
                properties.getUserSettings(), false));
        window->setVisible(true);
    }

    void shutdown() override
    {
        window = nullptr;
        properties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (window != nullptr)
            window->saveState();
        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
            juce::Timer::callAfterDelay(100, []
            {
                if (auto* app = juce::JUCEApplication::getInstance())
                    app->systemRequestedQuit();
            });
        else
            quit();
    }

private:
    juce::ApplicationProperties properties;
    std::unique_ptr<StandaloneWindow> window;
};
}

START_JUCE_APPLICATION(StandaloneApplication)
