#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

namespace megadsp
{
class StandaloneAudioSettings final : public juce::Component
{
public:
    StandaloneAudioSettings(juce::AudioDeviceManager& manager, juce::Value& inputMuted)
        : selector(manager, 0, 2, 2, 2, true, false, false, false),
          muteInput("Mute audio input")
    {
        muteInput.getToggleStateValue().referTo(inputMuted);
        instructions.setText(
            "Select one input for mono or two for stereo. Keep two output channels enabled.",
            juce::dontSendNotification);
        instructions.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(muteInput);
        addAndMakeVisible(instructions);
        addAndMakeVisible(selector);
        setSize(620, 680);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(12);
        muteInput.setBounds(area.removeFromTop(28));
        instructions.setBounds(area.removeFromTop(40));
        selector.setBounds(area);
    }

private:
    juce::AudioDeviceSelectorComponent selector;
    juce::ToggleButton muteInput;
    juce::Label instructions;
};
}
