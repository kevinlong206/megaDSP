#pragma once

#include "DspModules.h"
#include "SnapshotState.h"
#include "VisualizationData.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <optional>

namespace megadsp
{
class EffectRack
{
public:
    struct PreparedImpulseResponseLoad
    {
        int slot = -1;
        ModuleType type = ModuleType::empty;
        DspModule* module = nullptr;
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
                 double bpm);

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
    bool commitImpulseResponse(PreparedImpulseResponseLoad&);
    void cancelImpulseResponse(PreparedImpulseResponseLoad&) noexcept;
    juce::Result loadImpulseResponse(int slot, const juce::File&);
    juce::Result clearImpulseResponse(int slot);
    void reloadImpulseResponses();
    juce::String pendingImpulseResponsePath(int slot) const;
    void discardPendingImpulseResponse(
        int slot, const juce::String& path);
    juce::String impulseResponseName(int slot) const;
    juce::String impulseResponsePath(int slot) const;
    ImpulseResponsePreview impulseResponsePreview(int slot) const;
    juce::Result loadNamModel(int slot, const juce::File&);
    juce::Result prepareNamModel(
        int slot, const juce::File&, PreparedNamModelLoad&);
    bool commitNamModel(PreparedNamModelLoad&);
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
                      double bpm);
    ModuleType requestedType(const Slot&) const;
    DspModule* selectedModule(Slot&);
    const DspModule* selectedModule(const Slot&) const;
    void rebuildSlot(Slot&, ModuleType);
    void synchronizeModuleDataState(int slot);
    juce::String impulsePath(int slot) const;
    void setImpulsePath(int slot, const juce::String&);
    juce::String namPath(int slot) const;
    void setNamPath(int slot, const juce::String&);
};
} // namespace megadsp
