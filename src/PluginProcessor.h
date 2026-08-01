#pragma once

#include "EffectRack.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
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
    bool acceptsMidi() const override { return true; }
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
    megadsp::EffectRack& getRack()
    {
        return *snapshotRacks[static_cast<size_t>(
            selectedSnapshot.load(std::memory_order_acquire))];
    }
    const megadsp::EffectRack& getRack() const
    {
        return *snapshotRacks[static_cast<size_t>(
            selectedSnapshot.load(std::memory_order_acquire))];
    }
    megadsp::VisualizationData& getVisualizationData()
    {
        return getRack().visualizationData();
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
    juce::Result loadImpulseResponse(
        int slot, int lane, const juce::File& file);
    juce::Result loadBuiltInImpulseResponse(int slot, int index);
    juce::Result loadAdjacentImpulseResponse(int slot, int direction);
    void clearImpulseResponse(int slot);
    void clearImpulseResponse(int slot, int lane);
    juce::File impulseResponseDirectory(int slot) const;
    juce::String impulseResponseName(int slot) const
    {
        return getRack().impulseResponseName(slot);
    }
    juce::String impulseResponseName(int slot, int lane) const
    {
        return getRack().impulseResponseName(slot, lane);
    }
    juce::String impulseResponsePath(int slot) const
    {
        return getRack().impulseResponsePath(slot);
    }
    juce::String impulseResponsePath(int slot, int lane) const
    {
        return getRack().impulseResponsePath(slot, lane);
    }
    juce::Result loadNamModel(int slot, const juce::File& file);
    juce::Result clearNamModel(int slot)
    {
        if (!beginTopologyMutation())
            return juce::Result::fail("Topology mutation in progress.");
        auto result = getRack().clearNamModel(slot);
        refreshLatency();
        suspendProcessing(false);
        topologyMutationInProgress.store(false, std::memory_order_release);
        publishSnapshotToParameters(getSelectedSnapshot());
        updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
        return result;
    }
    juce::String namModelPath(int slot) const
    {
        return getRack().namModelPath(slot);
    }
    megadsp::ImpulseResponsePreview impulseResponsePreview(int slot) const
    {
        return getRack().impulseResponsePreview(slot);
    }
    megadsp::ImpulseResponsePreview impulseResponsePreview(
        int slot, int lane) const
    {
        return getRack().impulseResponsePreview(slot, lane);
    }
    bool readContinuousTelemetry(
        int slot, megadsp::ContinuousTelemetrySnapshot& snapshot) const noexcept
    {
        return getRack().readContinuousTelemetry(slot, snapshot);
    }
    bool readEventTelemetry(
        int slot, megadsp::EventTelemetrySnapshot& snapshot) const noexcept
    {
        return getRack().readEventTelemetry(slot, snapshot);
    }
    void topologyChanged();
    std::uint64_t getTopologyGeneration() const noexcept
    {
        return topologyGeneration.load(std::memory_order_acquire);
    }
    std::uint64_t getTopologyRequestGeneration() const noexcept
    {
        return topologyRequestGeneration.load(std::memory_order_acquire);
    }
    void loadFactoryPreset(int presetIndex);
    int getSelectedTab() const { return selectedTab; }
    void setSelectedTab(int tab)
    {
        const auto nextTab = juce::jlimit(
            0, juce::jmax(0, getRack().activeSlotCount() - 1), tab);
        selectedTab = nextTab;
    }
    int getBackgroundThemeIndex() const;
    void setBackgroundThemeIndex(int index);
    juce::String getInstanceName() const;
    void setInstanceName(const juce::String&);
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
    int getSelectedSnapshot() const noexcept
    {
        return selectedSnapshot.load(std::memory_order_acquire);
    }
    bool isSnapshotPopulated(int index) const noexcept;
    bool isSnapshotReady(int index) const noexcept;
    juce::String snapshotError(int index) const;
    void recallSnapshot(int index);
    void storeSnapshot(int index, bool overwrite);
    void copySnapshot(int source, int destination);
    bool isLiveSafeSnapshotsEnabled() const noexcept
    {
        return liveSafeSnapshots.load(std::memory_order_acquire);
    }
    void setLiveSafeSnapshotsEnabled(bool enabled);
    std::uint64_t getSnapshotGeneration() const noexcept
    {
        return snapshotGeneration.load(std::memory_order_acquire);
    }
    int activeSpillCountForTesting() const noexcept
    {
        return static_cast<int>(std::count_if(
            spillSnapshots.begin(), spillSnapshots.end(),
            [](const auto& spill) { return spill.snapshot >= 0; }));
    }
    int fadingSpillCountForTesting() const noexcept
    {
        return static_cast<int>(std::count_if(
            spillSnapshots.begin(), spillSnapshots.end(),
            [](const auto& spill)
            {
                return spill.snapshot >= 0
                       && spill.fadeSamplesRemaining > 0;
            }));
    }
    int processedSnapshotGraphCountForTesting() const noexcept
    {
        return processedSnapshotGraphCount.load(
            std::memory_order_acquire);
    }
    std::int64_t snapshotRecallDurationMicrosForTesting() const noexcept
    {
        return snapshotRecallDurationMicros.load(
            std::memory_order_acquire);
    }

#if defined(MEGADSP_TESTS)
    void flushPendingUpdatesForTesting()
    {
        handleUpdateNowIfNeeded();
        timerCallback();
    }
    void settleAudioIdleForTesting()
    {
        for (int index = 0; index < 32; ++index)
            timerCallback();
    }
    void pollAudioIdleForTesting(int count)
    {
        for (int index = 0; index < count; ++index)
            timerCallback();
    }
#endif

private:
    megadsp::EffectRack& getAudioRack()
    {
        return *snapshotRacks[static_cast<size_t>(
            activeAudioSnapshot.load(std::memory_order_acquire))];
    }
    const megadsp::EffectRack& getAudioRack() const
    {
        return *snapshotRacks[static_cast<size_t>(
            activeAudioSnapshot.load(std::memory_order_acquire))];
    }
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
    void restorePendingNamModels();
    void updateHostBypassLatency(int latency) noexcept;
    void captureParametersToSnapshot(int index);
    void publishSnapshotToParameters(
        int index, bool notifyHost = true);
    void prepareSnapshot(int index);
    void restoreSnapshotAssets(int index);
    void beginSnapshotRecall(int index) noexcept;
    void processSnapshotRange(
        juce::AudioBuffer<float>& main,
        const juce::AudioBuffer<float>* sidechain,
        double bpm);
    void processLiveSafeSnapshotRange(
        juce::AudioBuffer<float>& main,
        const juce::AudioBuffer<float>* sidechain,
        double bpm);
    void processProfiledRack(
        int snapshot,
        juce::AudioBuffer<float>& main,
        const juce::AudioBuffer<float>* sidechain,
        double bpm);
    void recordRealtimeDiagnostics(
        std::int64_t elapsedMicros, int blockSamples) noexcept;
    void mixLiveSafeSpills(juce::AudioBuffer<float>& main);
    void addSpillSnapshot(int index) noexcept;
    void processSpillSnapshots(
        juce::AudioBuffer<float>& main,
        const juce::AudioBuffer<float>* sidechain,
        double bpm);
    void serializeSnapshots(juce::ValueTree&) const;
    void restoreSnapshots(const juce::ValueTree&, int sourceSchema);
    megadsp::RackState& selectedSnapshotState() noexcept
    {
        return snapshotStates[static_cast<size_t>(
            selectedSnapshot.load(std::memory_order_acquire))];
    }

    std::array<megadsp::RackState, megadsp::snapshotCount> snapshotStates;
    std::array<juce::String, megadsp::slotCount> typeParameterIds;
    std::array<juce::String, megadsp::slotCount> bypassParameterIds;
    std::array<std::array<juce::String, megadsp::controlsPerSlot>,
               megadsp::slotCount>
        controlParameterIds;
    std::array<std::atomic<int>, megadsp::slotCount>
        pendingBuiltInCabinetLoads {};
    std::array<std::atomic<std::uint8_t>, megadsp::slotCount>
        pendingWormholeReloads {};
    std::array<std::unique_ptr<megadsp::EffectRack>,
               megadsp::snapshotCount> snapshotRacks;
    std::atomic<int> selectedSnapshot { 0 };
    std::atomic<int> activeAudioSnapshot { 0 };
    std::atomic<int> pendingSnapshotRecall { -1 };
    std::atomic<bool> snapshotUiDirty { false };
    std::atomic<std::uint64_t> snapshotGeneration { 0 };
    std::atomic<bool> liveSafeSnapshots { false };
    static thread_local bool publishingSnapshotParameters;
    struct SnapshotTransition
    {
        int outgoing = -1;
        int incoming = -1;
        int samplesDone = 0;
        int totalSamples = 0;
        bool fadeIncomingOutput = false;
    };
    struct SpillState
    {
        int snapshot = -1;
        int samplesRemaining = 0;
        int naturalFadeSamples = 0;
        int fadeSamplesRemaining = 0;
        int fadeSamplesTotal = 0;
        std::uint64_t order = 0;
    };
    SnapshotTransition snapshotTransition;
    std::array<SpillState, 3> spillSnapshots;
    std::uint64_t nextSpillOrder = 0;
    std::atomic<int> snapshotTailSamples { 0 };
    juce::AudioBuffer<float> snapshotScratch;
    juce::AudioBuffer<float> snapshotZero;
    std::array<juce::AudioBuffer<float>,
               megadsp::snapshotCount> liveSafeSnapshotOutputs;
    std::atomic<int> processedSnapshotGraphCount { 0 };
    std::atomic<std::int64_t> snapshotRecallDurationMicros { 0 };
    double preparedSampleRate = 48000.0;
    int preparedMaximumBlockSize = 0;
    int preparedChannelCount = 2;
    std::atomic<int> pendingLatency { 0 };
    std::atomic<bool> latencyDirty { false };
    std::atomic<std::uint64_t> audioCallbackGeneration { 0 };
    std::atomic<bool> audioStreamActive { false };
    std::atomic<int> lastAudioBlockSamples { 0 };
    std::uint64_t observedAudioCallbackGeneration = 0;
    int audioIdlePollCount = 0;
    std::atomic<bool> topologyDirty { false };
    std::atomic<std::uint32_t> pendingTopologySnapshotMask { 0 };
    std::atomic<std::uint64_t> topologyRequestGeneration { 0 };
    std::atomic<std::uint64_t> topologyGeneration { 0 };
    int editorWidth = 1100;
    int editorHeight = 720;
    std::array<std::vector<float>, 2> hostBypassDelay;
    int hostBypassWritePosition = 0;
    int lastHostBypassLatency = 0;
    std::atomic<bool> topologyMutationInProgress { false };
    std::uint32_t diagnosticInstanceId = 0;
    int lastDiagnosticBlockSize = 0;
    int lastDiagnosticChannelCount = 0;
    std::uint64_t diagnosticCallbackCount = 0;
    std::int64_t diagnosticWindowSamples = 0;
    std::int64_t diagnosticTotalCallbackMicros = 0;
    std::int64_t diagnosticMaximumCallbackMicros = 0;
    std::int64_t diagnosticMinimumDeadlineMicros = 0;
    int diagnosticWindowCallbacks = 0;
    int diagnosticOverrunCount = 0;
    int diagnosticMaximumGraphCount = 0;
    bool captureRealtimeProfile = false;
    megadsp::RealtimeProcessProfile realtimeProfile;
    megadsp::RealtimeProcessProfile accumulatedRealtimeProfile;
    int selectedTab = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MegaDSPAudioProcessor)
};
