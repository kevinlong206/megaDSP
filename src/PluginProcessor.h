#pragma once

#include "EffectRack.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cstdint>
#include <limits>

struct ImpulseResponseLibraryEntry
{
    juce::File file;
    juce::String relativePath;
    juce::String searchableText;
    double sampleRate = 0.0;
    double durationSeconds = 0.0;
    int channelCount = 0;
    bool favorite = false;
};

struct NamModelLibraryEntry
{
    juce::File file;
    juce::String relativePath;
    juce::String displayName;
    juce::String metadataTitle;
    juce::String architecture;
    juce::String captureDescription;
    juce::String gearType;
    juce::String searchableText;
    double sampleRate = 0.0;
    double inputLevelDbU = std::numeric_limits<double>::quiet_NaN();
    std::int64_t filesystemAddedTimeMs = 0;
    int submodelCount = 1;
    int cabIncluded = -1;
    bool isA2 = false;
    bool favorite = false;
};

enum class NamModelSort
{
    relevance,
    newestAdded
};

struct PluginPerformanceSnapshot
{
    static constexpr int historySize = 100;

    float currentLoad = 0.0f;
    float averageLoad = 0.0f;
    float peakLoad = 0.0f;
    float percentile95 = 0.0f;
    float percentile99 = 0.0f;
    float lastCallbackMilliseconds = 0.0f;
    float deadlineMilliseconds = 0.0f;
    double sampleRate = 0.0;
    int blockSamples = 0;
    std::uint64_t overrunCount = 0;
    std::array<float, historySize> history {};
    int historyCount = 0;
    std::array<float, megadsp::slotCount> slotAverageLoad {};
    std::array<float, megadsp::slotCount> slotPeakLoad {};
    float otherSnapshotsAverageLoad = 0.0f;
    float otherSnapshotsPeakLoad = 0.0f;
    bool offline = false;
    bool liveSafe = false;
    bool hostBypassed = false;
    bool callbacksActive = false;
    bool backgroundActivity = false;

    bool effectDspActive() const noexcept
    {
        return callbacksActive && !hostBypassed;
    }
};

namespace megadsp
{
class ReaperEmbeddedUI;
}

class MegaDSPAudioProcessor final : public juce::AudioProcessor,
                                    private juce::Timer,
                                    private juce::AsyncUpdater,
                                    private juce::AudioProcessorValueTreeState::Listener,
                                    private juce::AudioProcessorParameter::Listener
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
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    juce::VST3ClientExtensions* getVST3ClientExtensions() override;
    void beginUndoTransaction(const juce::String& name);
    void endUndoTransaction();
    bool canUndo() const;
    bool canRedo() const;
    juce::String undoDescription() const;
    juce::String redoDescription() const;
    bool undo();
    bool redo();
    void restoreUndoState(const juce::MemoryBlock&);
    PluginPerformanceSnapshot performanceSnapshot() const noexcept;
    void resetPerformanceMetrics() noexcept;

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
    megadsp::ReEqSettings reEqSettings(int slot) const;
    void setReEqSettings(
        int slot, const megadsp::ReEqSettings&, bool recordUndo = true);
    void upgradeEqualizer(int slot);
    juce::Result loadImpulseResponse(int slot, const juce::File& file);
    juce::Result loadImpulseResponse(
        int slot, int lane, const juce::File& file);
    using AssetLoadCallback =
        std::function<void(const juce::Result&)>;
    juce::Result loadImpulseResponseAsync(
        int slot, int lane, const juce::File&,
        AssetLoadCallback = {});
    juce::Result loadConvolutionImpulseResponseBankAsync(
        int slot, int bank, const juce::File&,
        AssetLoadCallback = {});
    juce::Result loadConvolutionImpulseResponseBank(
        int slot, int bank, const juce::File& file);
    bool selectConvolutionImpulseResponseBank(int slot, int bank);
    int activeConvolutionImpulseResponseBank(int slot) const;
    juce::String convolutionImpulseResponseBankPath(
        int slot, int bank) const;
    juce::String convolutionImpulseResponseBankName(
        int slot, int bank) const;
    juce::Result setConvolutionWetBufferSamples(
        int slot, int samples);
    int convolutionWetBufferSamples(int slot) const;
    juce::Result loadBuiltInImpulseResponse(int slot, int index);
    juce::Result loadAdjacentImpulseResponse(int slot, int direction);
    juce::Result setImpulseResponseRootDirectory(const juce::File&);
    using LibraryIndexProgress = std::function<void(double)>;
    enum class Library { namModels, impulseResponses };
    struct LibraryRebuildProgress
    {
        std::atomic<double> fraction { -1.0 };
        std::atomic<bool> finished { false };
        // Read the result only after acquiring finished.
        juce::Result result = juce::Result::ok();
    };
    juce::Result rebuildLibraryAsync(
        Library, const std::shared_ptr<LibraryRebuildProgress>&);
    juce::Result rebuildImpulseResponseLibrary(
        const LibraryIndexProgress& = {});
    std::vector<ImpulseResponseLibraryEntry> searchImpulseResponses(
        const juce::String& query, int maximumResults = 200);
    int impulseResponseLibrarySize();
    bool isImpulseResponseFavorite(const juce::File&) const;
    juce::Result setImpulseResponseFavorite(const juce::File&, bool);
    bool isImpulseResponseLibraryIndexing() const noexcept
    {
        return impulseResponseIndexInProgress.load(
            std::memory_order_acquire);
    }
    std::uint64_t assetLibraryGeneration() const noexcept
    {
        return libraryGeneration.load(std::memory_order_acquire);
    }
    juce::File impulseResponseRootDirectory() const;
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
    using NamModelLoadCallback =
        std::function<void(const juce::Result&)>;
    juce::Result loadNamModelAsync(
        int slot, const juce::File&, NamModelLoadCallback = {});
    juce::Result loadAdjacentNamModelAsync(
        int slot, int direction, NamModelLoadCallback = {});
    juce::Result setNamModelRootDirectory(const juce::File&);
    juce::Result rebuildNamModelLibrary(
        const LibraryIndexProgress& = {});
    std::vector<NamModelLibraryEntry> searchNamModels(
        const juce::String& query, int maximumResults = 200,
        NamModelSort sort = NamModelSort::relevance);
    int namModelLibrarySize();
    bool isNamModelFavorite(const juce::File&) const;
    juce::Result setNamModelFavorite(const juce::File&, bool);
    juce::Result deleteNamModel(int slot);
    juce::Result deleteNamModel(const juce::File&);
    bool isNamModelLibraryIndexing() const noexcept
    {
        return namModelIndexInProgress.load(
            std::memory_order_acquire);
    }
    juce::Result loadAdjacentNamModel(int slot, int direction);
    juce::File namModelRootDirectory() const;
    juce::File namCabinetRootDirectory() const;
    juce::Result setNamCabinetRootDirectory(const juce::File&);
    juce::Result loadNamCabinetAsync(
        int slot, const juce::File&, NamModelLoadCallback = {});
    juce::Result loadAdjacentNamCabinetAsync(
        int slot, int direction, NamModelLoadCallback = {});
    juce::Result setNamCabinetEnabled(int slot, bool);
    bool isNamCabinetEnabled(int slot) const
    {
        return getRack().isNamCabinetEnabled(slot);
    }
    juce::String namCabinetPath(int slot) const
    {
        return getRack().namCabinetPath(slot);
    }
    float namLevelMatchDb(int slot) const
    {
        return getRack().namLevelMatchDb(slot);
    }
    juce::Result clearNamModel(int slot);
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
    double getLastProcessedBpmForUI() const
    {
        return bpmForUI.load(std::memory_order_relaxed);
    }
    bool hasExternalSidechain() const
    {
        if (const auto* bus = getBus(true, 1))
            return bus->isEnabled() && bus->getNumberOfChannels() > 0;
        return false;
    }
    bool hasStereoOutput() const { return getTotalNumOutputChannels() > 1; }
    void rememberEditorSize(int width, int height);
    juce::String globalWidgetSizeId() const;
    juce::Result setGlobalWidgetSizeId(const juce::String&);
    static juce::File presetDirectory();
    static juce::Array<juce::File> userPresetFiles();
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
    void parameterValueChanged(int, float) override {}
    void parameterGestureChanged(int parameterIndex, bool starting) override;
    double currentBpm() const;
    void refreshLatency();
    bool updatePublishedParameterModuleTypes();
    juce::ValueTree copyPluginState();
    void restoreParameterState(const juce::ValueTree&);
    void finishTopologyMutation(
        bool compact, bool resetRack, bool publishAlways);
    bool beginTopologyMutation();
    bool cancelPendingAssetMutationForStateRestore();
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
    void recordPerformanceMetrics(
        std::uint64_t elapsedTicks, int blockSamples) noexcept;
    void mixLiveSafeSpills(juce::AudioBuffer<float>& main);
    void addSpillSnapshot(int index) noexcept;
    void processSpillSnapshots(
        juce::AudioBuffer<float>& main,
        const juce::AudioBuffer<float>* sidechain,
        double bpm);
    void ensureImpulseResponseLibrary();
    void ensureNamModelLibrary();
    void scheduleImpulseResponseLibraryRebuild();
    void scheduleNamModelLibraryRebuild();
    void pollAutomaticLibraryUpdates();
    void releaseAutomaticLibraryClaims();
    void finishPendingImpulseResponseLoad();
    void finishPendingNamModelLoad();
    void scheduleNamControlPreference(int parameterIndex);
    void flushPendingNamControlPreferences(bool force);
    void applyNamControlPreference(
        int snapshot, int slot, const std::array<float, 5>&);
    void disposeNamModelAsync(megadsp::PreparedNamModelPtr);
    void serializeSnapshots(juce::ValueTree&) const;
    void restoreSnapshots(const juce::ValueTree&, int sourceSchema);
    megadsp::RackState& selectedSnapshotState() noexcept
    {
        return snapshotStates[static_cast<size_t>(
            selectedSnapshot.load(std::memory_order_acquire))];
    }

    std::array<megadsp::RackState, megadsp::snapshotCount> snapshotStates;
    std::array<juce::String, megadsp::slotCount> typeParameterIds;
    std::array<std::atomic<int>, megadsp::slotCount> publishedParameterModuleTypes {};
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
    std::atomic<int> currentProgram { 0 };
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
    juce::AudioBuffer<float> monoStereoScratch;
    std::array<juce::AudioBuffer<float>,
               megadsp::snapshotCount> liveSafeSnapshotOutputs;
    std::atomic<int> processedSnapshotGraphCount { 0 };
    std::atomic<std::int64_t> snapshotRecallDurationMicros { 0 };
    double preparedSampleRate = 48000.0;
    std::atomic<double> bpmForUI { 120.0 };
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
    juce::CriticalSection impulseResponseLibraryLock;
    std::vector<ImpulseResponseLibraryEntry> impulseResponseLibrary;
    juce::String indexedImpulseResponseRoot;
    struct PendingImpulseResponseLoad
    {
        megadsp::EffectRack* rack = nullptr;
        megadsp::EffectRack::PreparedImpulseResponseLoad load;
        juce::Result result = juce::Result::ok();
        AssetLoadCallback callback;
        int snapshot = -1;
        bool selectConvolutionBank = false;
    };
    juce::CriticalSection pendingImpulseResponseLoadLock;
    juce::CriticalSection assetMutationCommitLock;
    std::unique_ptr<PendingImpulseResponseLoad>
        pendingImpulseResponseLoad;
    juce::ThreadPool impulseResponseLoadPool { 1 };
    juce::ThreadPool libraryIndexPool {
        1, juce::Thread::osDefaultStackSize,
        juce::Thread::Priority::background
    };
    std::atomic<bool> impulseResponseIndexInProgress { false };
    std::atomic<bool> namModelIndexInProgress { false };
    std::atomic<bool> impulseResponseLibraryValidated { false };
    std::atomic<bool> namModelLibraryValidated { false };
    std::atomic<std::uint64_t> libraryGeneration { 0 };
    std::uint64_t observedSharedImpulseResponseGeneration = 0;
    std::uint64_t observedSharedNamModelGeneration = 0;
    std::atomic<bool> automaticImpulseRebuildOwned { false };
    std::atomic<bool> automaticNamRebuildOwned { false };
    int automaticLibraryPollCounter = 0;
    juce::CriticalSection namModelLibraryLock;
    std::vector<NamModelLibraryEntry> namModelLibrary;
    juce::String indexedNamModelRoot;
    struct PendingNamModelLoad
    {
        megadsp::EffectRack* rack = nullptr;
        megadsp::EffectRack::PreparedNamModelLoad load;
        megadsp::EffectRack::PreparedNamModelLoad cabinetLoad;
        juce::Result result = juce::Result::ok();
        NamModelLoadCallback callback;
        int snapshot = -1;
        bool cabinetOnly = false;
        bool cabinetEnabled = false;
        bool clearCabinet = false;
        bool controlsStored = false;
        std::array<float, 5> controls {
            0.5f, 0.5f, 0.5f, 0.5f, 0.5f
        };
        juce::String preferenceModelPath;
        juce::String preferenceImpulsePath;
    };
    juce::CriticalSection pendingNamModelLoadLock;
    std::unique_ptr<PendingNamModelLoad> pendingNamModelLoad;
    juce::ThreadPool namModelLoadPool { 1 };
    juce::ThreadPool namModelDisposalPool { 1 };
    struct PendingNamControlPreference
    {
        juce::String modelPath;
        std::array<float, 5> controls {
            0.5f, 0.5f, 0.5f, 0.5f, 0.5f
        };
        int countdown = 0;
    };
    std::array<PendingNamControlPreference, megadsp::slotCount>
        pendingNamControlPreferences;
    juce::ThreadPool namPreferencePool {
        1, juce::Thread::osDefaultStackSize,
        juce::Thread::Priority::background
    };
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
    static constexpr int performanceHistogramBins = 401;
    std::atomic<std::uint64_t> performanceCallbackTicks { 0 };
    std::atomic<float> performanceCurrentLoad { 0.0f };
    std::atomic<float> performanceAverageLoad { 0.0f };
    std::atomic<float> performancePeakLoad { 0.0f };
    std::atomic<float> performanceLastCallbackMs { 0.0f };
    std::atomic<float> performanceDeadlineMs { 0.0f };
    std::atomic<float> performanceSampleRate { 0.0f };
    std::atomic<int> performanceBlockSamples { 0 };
    std::atomic<std::uint64_t> performanceOverruns { 0 };
    std::array<std::atomic<float>,
               PluginPerformanceSnapshot::historySize>
        performanceHistory {};
    std::atomic<int> performanceHistoryWriteIndex { 0 };
    std::atomic<int> performanceHistoryCount { 0 };
    std::array<std::atomic<std::uint32_t>,
               performanceHistogramBins>
        performanceHistogram {};
    std::array<std::atomic<float>, megadsp::slotCount>
        performanceSlotAverageLoad {};
    std::array<std::atomic<float>, megadsp::slotCount>
        performanceSlotPeakLoad {};
    std::atomic<float> performanceOtherSnapshotsAverageLoad { 0.0f };
    std::atomic<float> performanceOtherSnapshotsPeakLoad { 0.0f };
    std::atomic<bool> performanceOffline { false };
    std::atomic<bool> performanceLiveSafe { false };
    std::atomic<bool> performanceHostBypassed { false };
    std::atomic<std::uint64_t> performanceResetGeneration { 0 };
    std::uint64_t observedPerformanceResetGeneration = 0;
    float performanceAverageState = 0.0f;
    float performancePeakState = 0.0f;
    int performancePeakHoldSamples = 0;
    std::array<float, megadsp::slotCount>
        performanceSlotAverageState {};
    std::array<float, megadsp::slotCount>
        performanceSlotPeakState {};
    std::array<int, megadsp::slotCount>
        performanceSlotPeakHoldSamples {};
    float performanceOtherSnapshotsAverageState = 0.0f;
    float performanceOtherSnapshotsPeakState = 0.0f;
    int performanceOtherSnapshotsPeakHoldSamples = 0;
    int performanceProfileSnapshot = -1;
    int performanceHistorySamples = 0;
    float performanceHistoryMaximum = 0.0f;
    int selectedTab = 0;
    juce::UndoManager undoManager;
    juce::MemoryBlock pendingUndoBefore;
    juce::String pendingUndoName;
    int undoTransactionDepth = 0;
    bool restoringUndoState = false;
    std::unique_ptr<megadsp::ReaperEmbeddedUI> reaperEmbeddedUI;
#if defined(MEGADSP_TESTS)
public:
    void finishLibraryIndexForTesting();
    void refreshAssetLibrariesForTesting();
    void finishNamPreferenceSaveForTesting();
    void finishNamModelLoadForTesting();
    void finishImpulseResponseLoadForTesting();
#endif
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MegaDSPAudioProcessor)
};
