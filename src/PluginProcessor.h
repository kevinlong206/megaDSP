#pragma once

#include "EffectRack.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdint>

class MegaDSPAudioProcessor final : public juce::AudioProcessor,
                                    private juce::Timer,
                                    private juce::AsyncUpdater,
                                    private juce::AudioProcessorValueTreeState::Listener
{
public:
    MegaDSPAudioProcessor();
    ~MegaDSPAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState parameters;
    megadsp::EffectRack& getRack() { return rack; }
    megadsp::VisualizationData& getVisualizationData()
    {
        return rack.visualizationData();
    }
    void moveSlot(int source, int destination);
    void clearSlot(int slot);
    void removeSlot(int slot);
    bool isSlotBypassed(int slot) const
    {
        if (!juce::isPositiveAndBelow(slot, megadsp::slotCount))
            return false;
        if (const auto* value = parameters.getRawParameterValue(
                megadsp::slotParameterId(slot, "bypass")))
            return value->load() >= 0.5f;
        return false;
    }
    void toggleSlotBypass(int slot)
    {
        if (!juce::isPositiveAndBelow(slot, megadsp::slotCount))
            return;
        if (auto* parameter = parameters.getParameter(
                megadsp::slotParameterId(slot, "bypass")))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(
                parameter->convertTo0to1(isSlotBypassed(slot) ? 0.0f : 1.0f));
            parameter->endChangeGesture();
        }
    }
    void addModule(megadsp::ModuleType type);
    void resetModuleToDefaults(int slot);
    juce::Result loadImpulseResponse(int slot, const juce::File& file);
    void clearImpulseResponse(int slot);
    juce::String impulseResponseName(int slot) const
    {
        return rack.impulseResponseName(slot);
    }
    juce::String impulseResponsePath(int slot) const
    {
        return rack.impulseResponsePath(slot);
    }
    megadsp::ImpulseResponsePreview impulseResponsePreview(int slot) const
    {
        return rack.impulseResponsePreview(slot);
    }
    void topologyChanged();
    std::uint64_t getTopologyGeneration() const noexcept
    {
        return topologyGeneration.load(std::memory_order_acquire);
    }
    void loadFactoryPreset(int presetIndex);
    int getSelectedTab() const { return selectedTab; }
    void setSelectedTab(int tab)
    {
        selectedTab = juce::jlimit(0, juce::jmax(0, rack.activeSlotCount() - 1), tab);
        rack.visualizationData().setSelectedSlot(selectedTab);
    }
    int getBackgroundThemeIndex() const
    {
        return juce::jlimit(
            0, 9,
            static_cast<int>(parameters.state.getProperty(
                "backgroundTheme", 0)));
    }
    void setBackgroundThemeIndex(int index)
    {
        const auto safeIndex = juce::jlimit(0, 9, index);
        if (safeIndex == getBackgroundThemeIndex())
            return;
        parameters.state.setProperty("backgroundTheme", safeIndex, nullptr);
        updateHostDisplay(
            ChangeDetails{}.withNonParameterStateChanged(true));
    }
    double getCurrentBpmForUI() const { return currentBpm(); }
    bool hasExternalSidechain() const
    {
        if (const auto* bus = getBus(true, 1))
            return bus->isEnabled() && bus->getNumberOfChannels() > 0;
        return false;
    }
    bool hasStereoOutput() const { return getTotalNumOutputChannels() > 1; }
    void rememberEditorSize(int width, int height);
    juce::Result savePreset(const juce::File&);
    juce::Result loadPreset(const juce::File&);

#if defined(MEGADSP_TESTS)
    void flushPendingUpdatesForTesting()
    {
        handleUpdateNowIfNeeded();
        timerCallback();
    }
#endif

private:
    void timerCallback() override;
    void handleAsyncUpdate() override;
    void parameterChanged(
        const juce::String&, float) override;
    double currentBpm() const;
    void refreshLatency();
    void restoreParameterState(const juce::ValueTree&);
    void finishTopologyMutation(
        bool compact, bool resetRack, bool publishAlways);
    bool beginTopologyMutation();
    void restorePendingImpulseResponses();
    void updateHostBypassLatency(int latency) noexcept;

    megadsp::EffectRack rack;
    std::atomic<int> pendingLatency { 0 };
    std::atomic<bool> latencyDirty { false };
    std::atomic<bool> topologyDirty { false };
    std::atomic<std::uint64_t> topologyRequestGeneration { 0 };
    std::atomic<std::uint64_t> topologyGeneration { 0 };
    int editorWidth = 1100;
    int editorHeight = 720;
    std::array<std::vector<float>, 2> hostBypassDelay;
    int hostBypassWritePosition = 0;
    int lastHostBypassLatency = 0;
    std::atomic<bool> topologyMutationInProgress { false };
    int selectedTab = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MegaDSPAudioProcessor)
};
