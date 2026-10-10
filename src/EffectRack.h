#pragma once

#include "DspModules.h"
#include "SnapshotState.h"
#include "VisualizationData.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <optional>

namespace megadsp
{
struct RealtimeProcessProfile
{
    using SlotTicks = std::array<
        std::array<std::uint64_t, slotCount>, snapshotCount>;
    using SlotCalls = std::array<
        std::array<std::uint32_t, slotCount>, snapshotCount>;

    std::array<std::uint64_t, moduleTypeCount> moduleTicks {};
    std::array<std::uint64_t, moduleTypeCount> moduleMaximumTicks {};
    std::array<std::uint32_t, moduleTypeCount> moduleCalls {};
    std::array<std::uint64_t, snapshotCount> graphTicks {};
    std::array<std::uint64_t, snapshotCount> graphMaximumTicks {};
    std::array<std::uint32_t, snapshotCount> graphCalls {};
    SlotTicks slotTicks {};
    SlotTicks slotMaximumTicks {};
    SlotCalls slotCalls {};
    int currentSnapshot = 0;

    void reset() noexcept { *this = {}; }
    void add(const RealtimeProcessProfile& other) noexcept;
    void recordModule(ModuleType type, std::uint64_t ticks) noexcept;
    void recordSlot(int slot, std::uint64_t ticks) noexcept;
    void recordGraph(int snapshot, std::uint64_t ticks) noexcept;
};

class EffectRack
{
public:
    struct PreparedImpulseResponseLoad
    {
        int slot = -1;
        ModuleType type = ModuleType::empty;
        DspModule* module = nullptr;
        int lane = 0;
        int convolutionBank = -1;
        juce::String path;
        PreparedImpulseResponsePtr prepared;
    };

    struct PreparedNamModelLoad
    {
        int slot = -1;
        ModuleType type = ModuleType::empty;
        DspModule* module = nullptr;
        juce::String path;
        PreparedNamModelPtr prepared;
    };

    explicit EffectRack(RackState& state);
    explicit EffectRack(juce::AudioProcessorValueTreeState& state);

    void prepare(double sampleRate, int maximumBlockSize, int channelCount);
    bool synchronizeModules(bool restoreImpulseResponses = true);
    void rebuildModules(bool restoreModuleData = true);
    void reset();
    void process(juce::AudioBuffer<float>& main,
                 const juce::AudioBuffer<float>* sidechain,
                 double bpm,
                 RealtimeProcessProfile* profile = nullptr,
                 bool nonRealtime = true);

    ModuleType moduleType(int slot) const;
    ModuleType activeModuleType(int slot) const;
    const DspModule* activeModuleInstance(int slot) const;
    float slotMeter(int slot) const;
    float slotDetectorMeter(int slot) const;
    bool slotDspSuspended(int slot) const;
    int latencySamples() const;
    double tailSeconds() const;
    double snapshotSpillSeconds() const;
    float inputMeterDb() { return inputMeter.exchange(-100.0f); }
    float outputMeterDb() { return outputMeter.exchange(-100.0f); }
    VisualizationData& visualizationData() { return visualization; }
    void moveSlot(int source, int destination);
    void clearSlot(int slot);
    void clearModuleDataState();
    void removeSlot(int slot);
    bool compactSlots();
    int activeSlotCount() const;
    bool activeModuleHasCapability(
        int slot, ModuleCapability capability) const;
    juce::Result prepareImpulseResponse(
        int slot, const juce::File&, PreparedImpulseResponseLoad&);
    juce::Result prepareImpulseResponse(
        int slot, int lane, const juce::File&,
        PreparedImpulseResponseLoad&);
    juce::Result prepareBuiltInImpulseResponse(
        int slot, int index, PreparedImpulseResponseLoad&);
    bool commitImpulseResponse(PreparedImpulseResponseLoad&);
    juce::Result prepareConvolutionImpulseResponseBank(
        int slot, int bank, const juce::File&,
        PreparedImpulseResponseLoad&);
    bool commitConvolutionImpulseResponseBank(
        PreparedImpulseResponseLoad&);
    bool selectConvolutionImpulseResponseBank(int slot, int bank);
    int activeConvolutionImpulseResponseBank(int slot) const;
    juce::String convolutionImpulseResponseBankPath(
        int slot, int bank) const;
    juce::String convolutionImpulseResponseBankName(
        int slot, int bank) const;
    bool setConvolutionWetBufferSamples(int slot, int samples);
    int convolutionWetBufferSamples(int slot) const;
    void cancelImpulseResponse(PreparedImpulseResponseLoad&) noexcept;
    juce::Result loadImpulseResponse(int slot, const juce::File&);
    juce::Result loadImpulseResponse(int slot, int lane, const juce::File&);
    juce::Result loadBuiltInImpulseResponse(int slot, int index);
    juce::Result clearImpulseResponse(int slot);
    juce::Result clearImpulseResponse(int slot, int lane);
    void reloadImpulseResponses();
    juce::String pendingImpulseResponsePath(int slot) const;
    juce::String pendingImpulseResponsePath(int slot, int lane) const;
    void discardPendingImpulseResponse(
        int slot, const juce::String& path);
    void discardPendingImpulseResponse(
        int slot, int lane, const juce::String& path);
    juce::String impulseResponseName(int slot) const;
    juce::String impulseResponseName(int slot, int lane) const;
    juce::String impulseResponsePath(int slot) const;
    juce::String impulseResponsePath(int slot, int lane) const;
    ImpulseResponsePreview impulseResponsePreview(int slot) const;
    ImpulseResponsePreview impulseResponsePreview(int slot, int lane) const;
    juce::Result loadNamModel(int slot, const juce::File&);
    juce::Result prepareNamModel(
        int slot, const juce::File&, PreparedNamModelLoad&);
    bool commitNamModel(PreparedNamModelLoad&);
    juce::Result prepareNamCabinet(
        int slot, const juce::File&, PreparedNamModelLoad&);
    bool commitNamCabinet(PreparedNamModelLoad&);
    void clearNamCabinet(int slot);
    void setNamCabinetEnabled(int slot, bool);
    bool isNamCabinetEnabled(int slot) const;
    bool hasNamCabinet(int slot) const;
    juce::String namCabinetPath(int slot) const;
    float namLevelMatchDb(int slot) const;
    void cancelNamModel(PreparedNamModelLoad&) noexcept;
    juce::Result clearNamModel(int slot);
    juce::String namModelPath(int slot) const;
    juce::String pendingNamModelPath(int slot) const;
    void discardPendingNamModel(int slot, const juce::String& path);
    void reloadNamModels();
    GrainVisualEvents grainVisualEvents(int slot) const;
    BeatPermutationVisualEvents beatPermutationVisualEvents(int slot) const;
    bool readContinuousTelemetry(
        int slot, ContinuousTelemetrySnapshot&) const noexcept;
    bool readEventTelemetry(
        int slot, EventTelemetrySnapshot&) const noexcept;

private:
    struct Slot
    {
        std::atomic<float>* type = nullptr;
        std::atomic<float>* bypass = nullptr;
        std::array<std::atomic<float>*, controlsPerSlot> controls {};
        ModuleType activeType = ModuleType::empty;
        std::unique_ptr<DspModule> activeModule;
        juce::AudioBuffer<float> dryBuffer;
        std::array<std::vector<float>, 2> latencyBuffer;
        int latencyWritePosition = 0;
        int previousLatency = 0;
        juce::String synchronizedImpulsePath;
        juce::String observedImpulsePath;
        bool impulseRestorePending = false;
        juce::String synchronizedImpulsePath2;
        juce::String observedImpulsePath2;
        bool impulseRestorePending2 = false;
        juce::String synchronizedNamPath;
        juce::String observedNamPath;
        bool namRestorePending = false;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wet;
        bool dspSuspended = false;
        int reactivationSamplesRemaining = 0;

        ControlValues values() const;
    };

    RackState* state = nullptr;
    juce::AudioProcessorValueTreeState* legacyParameters = nullptr;
    std::array<Slot, slotCount> slots;
    std::atomic<float>* inputGain = nullptr;
    std::atomic<float>* outputGain = nullptr;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inputGainLinear;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputGainLinear;
    juce::dsp::ProcessSpec preparedSpec {};
    bool hasPrepareState = false;
    int maximumBlockSize = 0;
    std::atomic<float> inputMeter { -100.0f };
    std::atomic<float> outputMeter { -100.0f };
    VisualizationData visualization;

    void processChunk(juce::AudioBuffer<float>& main,
                      const juce::AudioBuffer<float>* sidechain,
                      double bpm,
                      RealtimeProcessProfile* profile,
                      bool nonRealtime);
    ModuleType requestedType(const Slot&) const;
    DspModule* selectedModule(Slot&);
    const DspModule* selectedModule(const Slot&) const;
    void rebuildSlot(Slot&, ModuleType);
    void synchronizeModuleDataState(int slot);
    juce::String impulsePath(int slot, int lane = 0) const;
    void setImpulsePath(int slot, int lane, const juce::String&);
    void setConvolutionImpulseBankPath(
        int slot, int bank, const juce::String&);
    juce::String namPath(int slot) const;
    void setNamPath(int slot, const juce::String&);
    void setStoredNamCabinetPath(int slot, const juce::String&);
    void setStoredNamCabinetEnabled(int slot, bool);
    void clearStoredNamCabinetState(int slot);
};
} // namespace megadsp
