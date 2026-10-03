#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Diagnostics.h"
#include "ReaperEmbeddedUI.h"
#include "ui/GraphStyle.h"
#include "ui/GuiLayout.h"

#include <NAM/wavenet/a2_fast.h>
#include <json.hpp>
#include <juce_audio_formats/juce_audio_formats.h>

#include <cstring>
#include <optional>

thread_local bool
    MegaDSPAudioProcessor::publishingSnapshotParameters = false;

namespace
{
class ProcessorStateUndoAction final : public juce::UndoableAction
{
public:
    ProcessorStateUndoAction(
        MegaDSPAudioProcessor& processorToUse,
        juce::MemoryBlock beforeToUse,
        juce::MemoryBlock afterToUse)
        : processor(processorToUse),
          before(std::move(beforeToUse)),
          after(std::move(afterToUse))
    {
    }

    bool perform() override
    {
        if (firstPerform)
        {
            firstPerform = false;
            return true;
        }
        processor.restoreUndoState(after);
        return true;
    }

    bool undo() override
    {
        processor.restoreUndoState(before);
        return true;
    }

    int getSizeInUnits() override { return 1; }

private:
    MegaDSPAudioProcessor& processor;
    juce::MemoryBlock before;
    juce::MemoryBlock after;
    bool firstPerform = true;
};

class ScopedProcessorUndoTransaction final
{
public:
    ScopedProcessorUndoTransaction(
        MegaDSPAudioProcessor& processorToUse,
        const juce::String& name)
        : processor(processorToUse)
    {
        processor.beginUndoTransaction(name);
    }

    ~ScopedProcessorUndoTransaction()
    {
        processor.endUndoTransaction();
    }

private:
    MegaDSPAudioProcessor& processor;
};

bool memoryBlocksMatch(
    const juce::MemoryBlock& left,
    const juce::MemoryBlock& right)
{
    return left.getSize() == right.getSize()
        && (left.getSize() == 0
            || std::memcmp(
                   left.getData(), right.getData(), left.getSize()) == 0);
}

juce::File globalSettingsFile()
{
#if defined(MEGADSP_TESTS)
    return juce::File::getSpecialLocation(
               juce::File::tempDirectory)
        .getChildFile("megaDSP-tests")
        .getChildFile("settings.xml");
#else
    return juce::File::getSpecialLocation(
               juce::File::userApplicationDataDirectory)
        .getChildFile("megaDSP")
        .getChildFile("settings.xml");
#endif
}

juce::PropertiesFile::Options globalSettingsOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "megaDSP";
    options.filenameSuffix = ".xml";
    options.folderName = "megaDSP";
    options.osxLibrarySubFolder = "Application Support";
    options.millisecondsBeforeSaving = 0;
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    return options;
}

juce::CriticalSection& globalSettingsLock()
{
    static juce::CriticalSection lock;
    return lock;
}

juce::String globalImpulseResponseRoot()
{
    const juce::ScopedLock lock(globalSettingsLock());
    juce::PropertiesFile settings(
        globalSettingsFile(), globalSettingsOptions());
    return settings.getValue("impulseResponseRootDirectory");
}

juce::String globalNamModelRoot()
{
    const juce::ScopedLock lock(globalSettingsLock());
    juce::PropertiesFile settings(
        globalSettingsFile(), globalSettingsOptions());
    return settings.getValue("namModelRootDirectory");
}

juce::String globalWidgetSizePreference()
{
    const juce::ScopedLock lock(globalSettingsLock());
    juce::PropertiesFile settings(
        globalSettingsFile(), globalSettingsOptions());
    return megadsp::ui::widgetSizeId(
        megadsp::ui::widgetSizeFromId(
            settings.getValue("widgetSize", "small")));
}

juce::Result persistGlobalWidgetSize(
    const juce::String& identifier)
{
    const auto size = megadsp::ui::widgetSizeFromId(identifier);
    const juce::ScopedLock lock(globalSettingsLock());
    const auto file = globalSettingsFile();
    const auto directoryResult =
        file.getParentDirectory().createDirectory();
    if (directoryResult.failed())
        return directoryResult;
    juce::PropertiesFile settings(
        file, globalSettingsOptions());
    settings.setValue(
        "widgetSize", megadsp::ui::widgetSizeId(size));
    return settings.saveIfNeeded()
        ? juce::Result::ok()
        : juce::Result::fail(
              "Could not save the global widget size.");
}

void inheritGlobalImpulseResponseRoot(juce::ValueTree& state)
{
    if (state.getProperty(
            "impulseResponseRootDirectory").toString().isNotEmpty())
        return;
    const auto root = juce::File(globalImpulseResponseRoot());
    if (!root.isDirectory())
        return;
    state.setProperty(
        "impulseResponseRootDirectory",
        root.getFullPathName(), nullptr);
    state.setProperty(
        "impulseResponseDirectory",
        root.getFullPathName(), nullptr);
}

void inheritGlobalNamModelRoot(juce::ValueTree& state)
{
    if (state.getProperty(
            "namModelRootDirectory").toString().isNotEmpty())
        return;
    const auto root = juce::File(globalNamModelRoot());
    if (root.isDirectory())
        state.setProperty(
            "namModelRootDirectory",
            root.getFullPathName(), nullptr);
}

juce::Result persistGlobalImpulseResponseRoot(const juce::File& root)
{
    const juce::ScopedLock lock(globalSettingsLock());
    const auto file = globalSettingsFile();
    const auto directoryResult =
        file.getParentDirectory().createDirectory();
    if (directoryResult.failed())
        return directoryResult;
    juce::PropertiesFile settings(
        file, globalSettingsOptions());
    settings.setValue(
        "impulseResponseRootDirectory",
        root.getFullPathName());
    return settings.saveIfNeeded()
        ? juce::Result::ok()
        : juce::Result::fail(
              "Could not save the global IR library root.");
}

juce::Result persistGlobalNamModelRoot(const juce::File& root)
{
    const juce::ScopedLock lock(globalSettingsLock());
    const auto file = globalSettingsFile();
    const auto directoryResult =
        file.getParentDirectory().createDirectory();
    if (directoryResult.failed())
        return directoryResult;
    juce::PropertiesFile settings(
        file, globalSettingsOptions());
    settings.setValue(
        "namModelRootDirectory", root.getFullPathName());
    return settings.saveIfNeeded()
        ? juce::Result::ok()
        : juce::Result::fail(
              "Could not save the global NAM library root.");
}

juce::Result seedGlobalImpulseResponseRoot(
    const juce::ValueTree& state)
{
    if (juce::File(globalImpulseResponseRoot()).isDirectory())
        return juce::Result::ok();
    const auto instanceRoot = juce::File(
        state.getProperty(
            "impulseResponseRootDirectory").toString());
    return instanceRoot.isDirectory()
        ? persistGlobalImpulseResponseRoot(instanceRoot)
        : juce::Result::ok();
}

juce::Result seedGlobalNamModelRoot(const juce::ValueTree& state)
{
    if (juce::File(globalNamModelRoot()).isDirectory())
        return juce::Result::ok();
    const auto instanceRoot = juce::File(
        state.getProperty("namModelRootDirectory").toString());
    return instanceRoot.isDirectory()
        ? persistGlobalNamModelRoot(instanceRoot)
        : juce::Result::ok();
}

juce::String namJsonString(
    const nlohmann::json& object, const char* key)
{
    const auto found = object.find(key);
    return found != object.end() && found->is_string()
        ? juce::String(found->get<std::string>())
        : juce::String();
}

double namJsonNumber(
    const nlohmann::json& object, const char* key) noexcept
{
    const auto found = object.find(key);
    return found != object.end() && found->is_number()
        ? found->get<double>() : std::numeric_limits<double>::quiet_NaN();
}

std::optional<NamModelLibraryEntry> inspectNamModel(
    const juce::File& file, const juce::File& root)
{
    if (!file.existsAsFile()
        || !file.hasFileExtension("nam"))
        return std::nullopt;
    const auto contents = file.loadFileAsString().toStdString();
    const auto json = nlohmann::json::parse(
        contents, nullptr, false);
    if (json.is_discarded() || !json.is_object())
        return std::nullopt;
    const auto architecture = json.find("architecture");
    const auto config = json.find("config");
    const auto weights = json.find("weights");
    const auto sampleRate = json.find("sample_rate");
    const auto version = json.find("version");
    if (architecture == json.end() || !architecture->is_string()
        || config == json.end() || !config->is_object()
        || weights == json.end() || !weights->is_array()
        || version == json.end() || !version->is_string()
        || sampleRate == json.end() || !sampleRate->is_number()
        || sampleRate->get<double>() <= 0.0)
        return std::nullopt;

    static const std::array<const char*, 7> supported {
        "Linear", "LSTM", "ConvNet", "WaveNet", "SlimmableWaveNet",
        "Container", "SlimmableContainer"
    };
    const auto architectureName =
        juce::String(architecture->get<std::string>());
    if (std::none_of(
            supported.begin(), supported.end(),
            [&architectureName](const char* candidate)
            {
                return architectureName == candidate;
            }))
        return std::nullopt;

    const nlohmann::json* effectiveModel = &json;
    auto submodelCount = 1;
    if (architectureName == "Container"
        || architectureName == "SlimmableContainer")
    {
        const auto submodels = config->find("submodels");
        if (submodels == config->end() || !submodels->is_array()
            || submodels->empty())
            return std::nullopt;
        submodelCount = static_cast<int>(submodels->size());
        double highest = -std::numeric_limits<double>::infinity();
        effectiveModel = nullptr;
        for (const auto& candidate : *submodels)
        {
            const auto maximum = candidate.find("max_value");
            const auto model = candidate.find("model");
            if (maximum == candidate.end() || !maximum->is_number()
                || model == candidate.end() || !model->is_object())
                continue;
            if (maximum->get<double>() > highest)
            {
                highest = maximum->get<double>();
                effectiveModel = &*model;
            }
        }
        if (effectiveModel == nullptr)
            return std::nullopt;
    }

    auto effectiveArchitecture = architectureName;
    if (effectiveModel != &json)
    {
        const auto nestedArchitecture =
            effectiveModel->find("architecture");
        if (nestedArchitecture != effectiveModel->end()
            && nestedArchitecture->is_string())
            effectiveArchitecture =
                juce::String(nestedArchitecture->get<std::string>());
    }
    const auto effectiveSupported = std::any_of(
        supported.begin(), supported.end(),
        [&effectiveArchitecture](const char* candidate)
        {
            return effectiveArchitecture == candidate;
        });
    const auto effectiveConfig = effectiveModel->find("config");
    const auto effectiveWeights = effectiveModel->find("weights");
    if (!effectiveSupported
        || effectiveConfig == effectiveModel->end()
        || !effectiveConfig->is_object()
        || effectiveWeights == effectiveModel->end()
        || !effectiveWeights->is_array()
        || effectiveWeights->empty())
        return std::nullopt;

    auto isA2 = false;
#if defined(NAM_ENABLE_A2_FAST)
    if (effectiveArchitecture == "WaveNet")
    {
        int channels = 0;
        isA2 = nam::wavenet::a2_fast::is_a2_shape(
            *effectiveConfig, &channels);
    }
#endif

    NamModelLibraryEntry entry;
    entry.file = file;
    entry.relativePath =
        file.getRelativePathFrom(root).replaceCharacter('\\', '/');
    entry.displayName = file.getFileNameWithoutExtension();
    entry.architecture = architectureName;
    if (effectiveArchitecture != architectureName)
        entry.architecture += " / " + effectiveArchitecture;
    entry.sampleRate = sampleRate->get<double>();
    entry.submodelCount = submodelCount;
    entry.isA2 = isA2;

    const auto metadata = json.find("metadata");
    if (metadata != json.end() && metadata->is_object())
    {
        const auto displayName = namJsonString(*metadata, "display_name");
        const auto metadataName = namJsonString(*metadata, "name");
        if (displayName.isNotEmpty())
            entry.metadataTitle = displayName;
        else if (metadataName.isNotEmpty())
            entry.metadataTitle = metadataName;
        if (entry.metadataTitle.isNotEmpty())
            entry.displayName = entry.metadataTitle;

        juce::StringArray capture;
        auto gear = namJsonString(*metadata, "gear_make");
        const auto gearModel = namJsonString(*metadata, "gear_model");
        if (gearModel.isNotEmpty())
            gear += (gear.isNotEmpty() ? " " : "") + gearModel;
        entry.gearType =
            namJsonString(*metadata, "gear_type").toLowerCase();
        if (gear.isEmpty())
            gear = entry.gearType;
        if (gear.isNotEmpty())
            capture.add(gear);
        const auto tone = namJsonString(*metadata, "tone_type");
        if (tone.isNotEmpty())
            capture.add(tone);
        const auto modeledBy = namJsonString(*metadata, "modeled_by");
        if (modeledBy.isNotEmpty())
            capture.add("by " + modeledBy);
        entry.captureDescription = capture.joinIntoString(" | ");
        entry.inputLevelDbU =
            namJsonNumber(*metadata, "input_level_dbu");
        const auto withCab = metadata->find("with_cab");
        if (withCab != metadata->end() && withCab->is_boolean())
            entry.cabIncluded = withCab->get<bool>() ? 1 : 0;
        else if (entry.gearType.contains("cab")
                 || entry.gearType == "full-rig")
            entry.cabIncluded = 1;
        else if (entry.gearType.contains("amp"))
            entry.cabIncluded = 0;
    }
    return entry;
}

float smoothSnapshotFade(float position) noexcept
{
    position = juce::jlimit(0.0f, 1.0f, position);
    return position * position * (3.0f - 2.0f * position);
}

int programChangeNumber(
    const juce::MidiMessageMetadata& metadata) noexcept
{
    if (metadata.data == nullptr || metadata.numBytes < 2
        || (metadata.data[0] & 0xf0u) != 0xc0u)
        return -1;
    return metadata.data[1] & 0x7f;
}

void applySnapshotTransitionWeight(
    juce::AudioBuffer<float>& buffer, float start, float end,
    bool incoming) noexcept
{
    const auto samples = buffer.getNumSamples();
    if (samples <= 0)
        return;
    const auto increment = (end - start) / static_cast<float>(samples);
    for (int sample = 0; sample < samples; ++sample)
    {
        const auto smoothPosition = smoothSnapshotFade(
            start + increment * static_cast<float>(sample));
        const auto gain =
            incoming ? smoothPosition : 1.0f - smoothPosition;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.setSample(
                channel, sample,
                buffer.getSample(channel, sample) * gain);
    }
}

void applySnapshotSpillFade(
    juce::AudioBuffer<float>& buffer,
    int samplesRemaining,
    int naturalFadeSamples,
    int retiringFadeSamplesRemaining,
    int retiringFadeSamplesTotal) noexcept
{
    const auto naturalFadeActive =
        samplesRemaining <= naturalFadeSamples + buffer.getNumSamples();
    const auto retiringFadeActive = retiringFadeSamplesTotal > 0;
    if (!naturalFadeActive && !retiringFadeActive)
        return;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        auto gain = 1.0f;
        if (naturalFadeActive)
            gain *= smoothSnapshotFade(
                static_cast<float>(samplesRemaining - sample)
                / static_cast<float>(naturalFadeSamples));
        if (retiringFadeActive)
            gain *= smoothSnapshotFade(
                static_cast<float>(
                    retiringFadeSamplesRemaining - sample)
                / static_cast<float>(retiringFadeSamplesTotal));
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.setSample(
                channel, sample,
                buffer.getSample(channel, sample) * gain);
    }
}
} // namespace

MegaDSPAudioProcessor::MegaDSPAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
      parameters(*this, nullptr, "megaDSPState", megadsp::createParameterLayout())
{
    inheritGlobalImpulseResponseRoot(parameters.state);
    inheritGlobalNamModelRoot(parameters.state);
    for (int slot = 0; slot < megadsp::slotCount; ++slot)
    {
        pendingBuiltInCabinetLoads[static_cast<size_t>(slot)].store(
            -1, std::memory_order_relaxed);
        pendingWormholeReloads[static_cast<size_t>(slot)].store(
            0, std::memory_order_relaxed);
        typeParameterIds[static_cast<size_t>(slot)] =
            megadsp::slotParameterId(slot, "type");
        bypassParameterIds[static_cast<size_t>(slot)] =
            megadsp::slotParameterId(slot, "bypass");
        for (int control = 0;
             control < megadsp::controlsPerSlot; ++control)
            controlParameterIds[static_cast<size_t>(slot)]
                               [static_cast<size_t>(control)] =
                megadsp::controlParameterId(slot, control);
    }
    for (int index = 0; index < megadsp::snapshotCount; ++index)
        snapshotRacks[static_cast<size_t>(index)] =
            std::make_unique<megadsp::EffectRack>(
                snapshotStates[static_cast<size_t>(index)]);
    snapshotStates[0].populated.store(true, std::memory_order_relaxed);
    captureParametersToSnapshot(0);
    diagnosticInstanceId =
        megadsp::Diagnostics::instance().registerInstance();
    for (auto* parameter : getParameters())
    {
        parameter->addListener(this);
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
            parameters.addParameterListener(ranged->paramID, this);
    }
    undoManager.setMaxNumberOfStoredUnits(64, 8);
    reaperEmbeddedUI =
        std::make_unique<megadsp::ReaperEmbeddedUI>(*this);
    startTimerHz(30);
}

MegaDSPAudioProcessor::~MegaDSPAudioProcessor()
{
    stopTimer();
    namModelLoadPool.removeAllJobs(true, 30000);
    {
        const juce::ScopedLock lock(pendingNamModelLoadLock);
        if (pendingNamModelLoad != nullptr
            && pendingNamModelLoad->load.prepared != nullptr)
            pendingNamModelLoad->rack->cancelNamModel(
                pendingNamModelLoad->load);
        pendingNamModelLoad.reset();
    }
    for (auto* parameter : getParameters())
    {
        parameter->removeListener(this);
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
            parameters.removeParameterListener(ranged->paramID, this);
    }
    cancelPendingUpdate();
    megadsp::Diagnostics::instance().unregisterInstance(
        diagnosticInstanceId);
}

juce::VST3ClientExtensions*
MegaDSPAudioProcessor::getVST3ClientExtensions()
{
    return reaperEmbeddedUI.get();
}

void MegaDSPAudioProcessor::beginUndoTransaction(
    const juce::String& name)
{
    auto* messageManager =
        juce::MessageManager::getInstanceWithoutCreating();
    if (restoringUndoState || messageManager == nullptr
        || !messageManager->isThisTheMessageThread())
        return;
    if (undoTransactionDepth++ != 0)
        return;
    pendingUndoName = name.trim().isNotEmpty()
        ? name.trim() : "Edit megaDSP";
    pendingUndoBefore.reset();
    getStateInformation(pendingUndoBefore);
}

void MegaDSPAudioProcessor::endUndoTransaction()
{
    if (restoringUndoState || undoTransactionDepth <= 0)
        return;
    if (--undoTransactionDepth != 0)
        return;

    juce::MemoryBlock after;
    getStateInformation(after);
    if (!memoryBlocksMatch(pendingUndoBefore, after))
    {
        undoManager.beginNewTransaction(pendingUndoName);
        undoManager.perform(new ProcessorStateUndoAction(
            *this, std::move(pendingUndoBefore), std::move(after)));
    }
    pendingUndoBefore.reset();
    pendingUndoName.clear();
}

bool MegaDSPAudioProcessor::canUndo() const
{
    return undoManager.canUndo();
}

bool MegaDSPAudioProcessor::canRedo() const
{
    return undoManager.canRedo();
}

juce::String MegaDSPAudioProcessor::undoDescription() const
{
    return undoManager.getUndoDescription();
}

juce::String MegaDSPAudioProcessor::redoDescription() const
{
    return undoManager.getRedoDescription();
}

bool MegaDSPAudioProcessor::undo()
{
    if (!canUndo())
        return false;
    const auto restored = undoManager.undo();
    if (restored)
        updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
    return restored;
}

bool MegaDSPAudioProcessor::redo()
{
    if (!canRedo())
        return false;
    const auto restored = undoManager.redo();
    if (restored)
        updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
    return restored;
}

void MegaDSPAudioProcessor::restoreUndoState(
    const juce::MemoryBlock& state)
{
    const juce::ScopedValueSetter<bool> restoring(
        restoringUndoState, true);
    undoTransactionDepth = 0;
    pendingUndoBefore.reset();
    pendingUndoName.clear();
    setStateInformation(
        state.getData(), static_cast<int>(state.getSize()));
}

void MegaDSPAudioProcessor::parameterGestureChanged(
    int parameterIndex, bool starting)
{
    auto* messageManager =
        juce::MessageManager::getInstanceWithoutCreating();
    if (restoringUndoState || messageManager == nullptr
        || !messageManager->isThisTheMessageThread())
        return;
    if (starting)
    {
        const auto& allParameters = getParameters();
        const auto name =
            juce::isPositiveAndBelow(parameterIndex, allParameters.size())
            ? allParameters[parameterIndex]->getName(64)
            : juce::String("Parameter");
        beginUndoTransaction("Adjust " + name);
    }
    else
        endUndoTransaction();
}

void MegaDSPAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    preparedSampleRate = sampleRate;
    preparedMaximumBlockSize = juce::jmax(1, samplesPerBlock);
    preparedChannelCount = juce::jmax(1, getTotalNumOutputChannels());
    snapshotTransition = {};
    for (auto& spill : spillSnapshots)
        spill = {};
    nextSpillOrder = 0;
    activeAudioSnapshot.store(
        selectedSnapshot.load(std::memory_order_acquire),
        std::memory_order_release);
    snapshotTailSamples.store(0, std::memory_order_release);
    pendingSnapshotRecall.store(-1, std::memory_order_release);
    for (int index = 0; index < megadsp::snapshotCount; ++index)
    {
        auto& state = snapshotStates[static_cast<size_t>(index)];
        snapshotRacks[static_cast<size_t>(index)]->prepare(
            sampleRate, samplesPerBlock, preparedChannelCount);
        state.ready.store(
            state.populated.load(std::memory_order_relaxed),
            std::memory_order_release);
    }
    snapshotScratch.setSize(preparedChannelCount, preparedMaximumBlockSize);
    snapshotZero.setSize(preparedChannelCount, preparedMaximumBlockSize);
    snapshotZero.clear();
    for (auto& output : liveSafeSnapshotOutputs)
    {
        output.setSize(
            preparedChannelCount, preparedMaximumBlockSize,
            false, true, false);
        output.clear();
    }
    const auto bypassCapacity = static_cast<size_t>(
        juce::jmax(1, juce::roundToInt(sampleRate * 0.02) * megadsp::slotCount));
    for (auto& channel : hostBypassDelay)
        channel.assign(bypassCapacity, 0.0f);
    hostBypassWritePosition = 0;
    lastHostBypassLatency = 0;
    pendingLatency.store(getRack().latencySamples());
    setLatencySamples(pendingLatency.load());
    latencyDirty.store(false, std::memory_order_release);
    audioCallbackGeneration.store(0, std::memory_order_release);
    lastAudioBlockSamples.store(0, std::memory_order_release);
    observedAudioCallbackGeneration = 0;
    audioIdlePollCount = 0;
    audioStreamActive.store(false, std::memory_order_release);
    megadsp::Diagnostics::instance().log(
        "prepare",
        "instance=" + juce::String(diagnosticInstanceId)
            + " sampleRate=" + juce::String(sampleRate, 1)
            + " maxBlock=" + juce::String(samplesPerBlock)
            + " channels=" + juce::String(getTotalNumOutputChannels()));
}

void MegaDSPAudioProcessor::releaseResources()
{
    for (auto& snapshotRack : snapshotRacks)
        snapshotRack->reset();
}

bool MegaDSPAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto mainInput = layouts.getChannelSet(true, 0);
    const auto mainOutput = layouts.getChannelSet(false, 0);
    if (mainInput != mainOutput
        || (mainInput != juce::AudioChannelSet::mono()
            && mainInput != juce::AudioChannelSet::stereo()))
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sidechain = layouts.getChannelSet(true, 1);
        if (!sidechain.isDisabled()
            && sidechain != juce::AudioChannelSet::mono()
            && sidechain != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void MegaDSPAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& midi)
{
    const auto diagnosticsEnabled =
        megadsp::Diagnostics::instance().isEnabled();
    const auto callbackStart = diagnosticsEnabled
        ? juce::Time::getHighResolutionTicks() : 0;
    captureRealtimeProfile =
        diagnosticsEnabled
        && (++diagnosticCallbackCount % 31u) == 0u;
    if (captureRealtimeProfile)
        realtimeProfile.reset();
    auto recalledSnapshot = false;
    audioStreamActive.store(true, std::memory_order_release);
    lastAudioBlockSamples.store(
        buffer.getNumSamples(), std::memory_order_relaxed);
    audioCallbackGeneration.fetch_add(1, std::memory_order_relaxed);
    for (int channel = getTotalNumInputChannels();
         channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    auto main = getBusBuffer(buffer, false, 0);
    const juce::AudioBuffer<float>* sidechainPointer = nullptr;
    juce::AudioBuffer<float> sidechain;
    if (getBusCount(true) > 1 && getChannelCountOfBus(true, 1) > 0)
    {
        sidechain = getBusBuffer(buffer, true, 1);
        sidechainPointer = &sidechain;
    }

    const auto bypassLatency = juce::jmin(
        getLatencySamples(), static_cast<int>(hostBypassDelay[0].size()));
    updateHostBypassLatency(bypassLatency);
    if (bypassLatency > 0)
    {
        for (int sample = 0; sample < main.getNumSamples(); ++sample)
        {
            for (int channel = 0; channel < main.getNumChannels(); ++channel)
                hostBypassDelay[static_cast<size_t>(juce::jmin(channel, 1))][
                    static_cast<size_t>(hostBypassWritePosition)] =
                    main.getSample(channel, sample);
            if (++hostBypassWritePosition >= bypassLatency)
                hostBypassWritePosition = 0;
        }
    }

    const auto bpm = currentBpm();
    const auto processRange = [this, &main, sidechainPointer, bpm](
                                  int start, int samples)
    {
        if (samples <= 0)
            return;
        std::array<float*, 2> mainPointers {};
        for (int channel = 0; channel < main.getNumChannels(); ++channel)
            mainPointers[static_cast<size_t>(channel)] =
                main.getWritePointer(channel, start);
        juce::AudioBuffer<float> mainRange(
            mainPointers.data(), main.getNumChannels(), samples);

        std::array<float*, 2> sidechainPointers {};
        std::optional<juce::AudioBuffer<float>> sidechainRange;
        if (sidechainPointer != nullptr)
        {
            for (int channel = 0;
                 channel < sidechainPointer->getNumChannels(); ++channel)
                sidechainPointers[static_cast<size_t>(channel)] =
                    const_cast<float*>(
                        sidechainPointer->getReadPointer(channel, start));
            sidechainRange.emplace(
                sidechainPointers.data(),
                sidechainPointer->getNumChannels(), samples);
        }
        processSnapshotRange(
            mainRange,
            sidechainRange.has_value() ? &*sidechainRange : nullptr,
            bpm);
    };

    if (const auto requested =
            pendingSnapshotRecall.exchange(-1, std::memory_order_acq_rel);
        requested >= 0)
    {
        beginSnapshotRecall(requested);
        recalledSnapshot = true;
    }

    auto position = 0;
    for (const auto metadata : midi)
    {
        const auto program = programChangeNumber(metadata);
        if (program < 0)
            continue;
        const auto eventPosition = juce::jlimit(
            position, main.getNumSamples(), metadata.samplePosition);
        processRange(position, eventPosition - position);
        beginSnapshotRecall(program);
        recalledSnapshot = true;
        position = eventPosition;
    }
    processRange(position, main.getNumSamples() - position);

    auto outputPeak = 0.0f;
    auto outputIsSafe = true;
    const auto outputChannels = juce::jmin(
        buffer.getNumChannels(), getTotalNumOutputChannels());
    for (int channel = 0; channel < outputChannels; ++channel)
    {
        const auto* samples = buffer.getReadPointer(channel);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto magnitude = std::abs(samples[sample]);
            if (!std::isfinite(magnitude))
            {
                outputIsSafe = false;
                outputPeak = std::numeric_limits<float>::infinity();
                break;
            }
            outputPeak = juce::jmax(outputPeak, magnitude);
            if (outputPeak > 8.0f)
            {
                outputIsSafe = false;
                break;
            }
        }
        if (!outputIsSafe)
            break;
    }
    if (!outputIsSafe)
    {
        for (int channel = 0; channel < outputChannels; ++channel)
            buffer.clear(channel, 0, buffer.getNumSamples());
        megadsp::Diagnostics::instance().logRealtime(
            megadsp::DiagnosticEvent::unsafeOutputRejected,
            diagnosticInstanceId,
            std::isfinite(outputPeak)
                ? static_cast<std::int64_t>(
                    juce::roundToInt(outputPeak * 1000.0f))
                : -1,
            activeAudioSnapshot.load(std::memory_order_relaxed));
    }

    const auto latency = getAudioRack().latencySamples();
    if (latency != pendingLatency.exchange(latency))
    {
        latencyDirty.store(true, std::memory_order_release);
        megadsp::Diagnostics::instance().logRealtime(
            megadsp::DiagnosticEvent::latencyChanged,
            diagnosticInstanceId, latency);
    }
    if (main.getNumSamples() != lastDiagnosticBlockSize
        || main.getNumChannels() != lastDiagnosticChannelCount)
    {
        lastDiagnosticBlockSize = main.getNumSamples();
        lastDiagnosticChannelCount = main.getNumChannels();
        megadsp::Diagnostics::instance().logRealtime(
            megadsp::DiagnosticEvent::audioConfiguration,
            diagnosticInstanceId, lastDiagnosticBlockSize,
            lastDiagnosticChannelCount);
    }
    const auto elapsedMicros = diagnosticsEnabled
        ? static_cast<std::int64_t>(
              juce::Time::highResolutionTicksToSeconds(
                  juce::Time::getHighResolutionTicks() - callbackStart)
              * 1000000.0)
        : 0;
    if (recalledSnapshot)
    {
        snapshotRecallDurationMicros.store(
            elapsedMicros, std::memory_order_release);
        megadsp::Diagnostics::instance().logRealtime(
            megadsp::DiagnosticEvent::snapshotRecallWorkload,
            diagnosticInstanceId, elapsedMicros,
            processedSnapshotGraphCount.load(
                std::memory_order_relaxed));
    }
    recordRealtimeDiagnostics(elapsedMicros, main.getNumSamples());
}

void MegaDSPAudioProcessor::processBlockBypassed(
    juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    audioStreamActive.store(true, std::memory_order_release);
    lastAudioBlockSamples.store(
        buffer.getNumSamples(), std::memory_order_relaxed);
    audioCallbackGeneration.fetch_add(1, std::memory_order_relaxed);
    for (auto& spill : spillSnapshots)
    {
        spill = {};
    }
    snapshotTransition = {};
    snapshotTailSamples.store(0, std::memory_order_release);

    const auto selectWithoutTransition = [this](int index)
    {
        if (!juce::isPositiveAndBelow(index, megadsp::snapshotCount)
            || !snapshotStates[static_cast<size_t>(index)]
                    .populated.load(std::memory_order_acquire)
            || !snapshotStates[static_cast<size_t>(index)]
                    .ready.load(std::memory_order_acquire))
            return;
        const auto previous =
            activeAudioSnapshot.exchange(index, std::memory_order_acq_rel);
        selectedSnapshot.store(index, std::memory_order_release);
        const auto activeVisualSlots =
            snapshotRacks[static_cast<size_t>(previous)]
                ->visualizationData().getActiveSlotMask();
        snapshotRacks[static_cast<size_t>(previous)]
            ->visualizationData().clearActiveSlots();
        snapshotRacks[static_cast<size_t>(index)]
            ->visualizationData().setActiveSlotMask(activeVisualSlots);
        snapshotUiDirty.store(true, std::memory_order_release);
        snapshotGeneration.fetch_add(1, std::memory_order_release);
    };
    if (const auto requested =
            pendingSnapshotRecall.exchange(-1, std::memory_order_acq_rel);
        requested >= 0)
        selectWithoutTransition(requested);
    for (const auto metadata : midi)
        if (const auto program = programChangeNumber(metadata);
            program >= 0)
            selectWithoutTransition(program);

    auto main = getBusBuffer(buffer, false, 0);
    const auto latency = juce::jmin(
        getLatencySamples(), static_cast<int>(hostBypassDelay[0].size()));
    updateHostBypassLatency(latency);
    if (latency <= 0)
        return;

    for (int sample = 0; sample < main.getNumSamples(); ++sample)
    {
        for (int channel = 0; channel < main.getNumChannels(); ++channel)
        {
            auto& delayed = hostBypassDelay[static_cast<size_t>(juce::jmin(channel, 1))][
                static_cast<size_t>(hostBypassWritePosition)];
            const auto input = main.getSample(channel, sample);
            main.setSample(channel, sample, delayed);
            delayed = input;
        }

        if (++hostBypassWritePosition >= latency)
            hostBypassWritePosition = 0;
    }
}

void MegaDSPAudioProcessor::recordRealtimeDiagnostics(
    std::int64_t elapsedMicros, int blockSamples) noexcept
{
    auto& diagnostics = megadsp::Diagnostics::instance();
    if (!diagnostics.isEnabled() || blockSamples <= 0
        || preparedSampleRate <= 0.0)
    {
        diagnosticWindowSamples = 0;
        diagnosticWindowCallbacks = 0;
        diagnosticTotalCallbackMicros = 0;
        diagnosticMaximumCallbackMicros = 0;
        diagnosticMinimumDeadlineMicros = 0;
        diagnosticOverrunCount = 0;
        diagnosticMaximumGraphCount = 0;
        accumulatedRealtimeProfile.reset();
        return;
    }

    const auto deadlineMicros = juce::jmax<std::int64_t>(
        1, static_cast<std::int64_t>(juce::roundToInt(
               1000000.0 * static_cast<double>(blockSamples)
               / preparedSampleRate)));
    diagnosticWindowSamples += blockSamples;
    ++diagnosticWindowCallbacks;
    diagnosticTotalCallbackMicros += elapsedMicros;
    diagnosticMaximumCallbackMicros = juce::jmax(
        diagnosticMaximumCallbackMicros, elapsedMicros);
    diagnosticMinimumDeadlineMicros =
        diagnosticMinimumDeadlineMicros == 0
        ? deadlineMicros
        : juce::jmin(diagnosticMinimumDeadlineMicros, deadlineMicros);
    if (elapsedMicros > deadlineMicros)
        ++diagnosticOverrunCount;
    diagnosticMaximumGraphCount = juce::jmax(
        diagnosticMaximumGraphCount,
        processedSnapshotGraphCount.load(std::memory_order_relaxed));
    if (captureRealtimeProfile)
        accumulatedRealtimeProfile.add(realtimeProfile);

    if (diagnosticWindowSamples
        < static_cast<std::int64_t>(
            juce::roundToInt(preparedSampleRate)))
        return;

    const auto averageMicros =
        diagnosticWindowCallbacks > 0
        ? diagnosticTotalCallbackMicros / diagnosticWindowCallbacks : 0;
    const auto packedOverrunsAndGraphs = static_cast<std::int64_t>(
        (static_cast<std::uint64_t>(
             static_cast<std::uint32_t>(diagnosticOverrunCount)) << 32)
        | static_cast<std::uint32_t>(diagnosticMaximumGraphCount));
    diagnostics.logRealtime(
        megadsp::DiagnosticEvent::audioCallbackWorkload,
        diagnosticInstanceId, averageMicros,
        diagnosticMaximumCallbackMicros,
        diagnosticMinimumDeadlineMicros,
        packedOverrunsAndGraphs);

    const auto ticksToMicros = [](std::uint64_t ticks)
    {
        return static_cast<std::int64_t>(
            juce::Time::highResolutionTicksToSeconds(
                static_cast<juce::int64>(ticks))
            * 1000000.0);
    };
    std::array<bool, megadsp::moduleTypeCount> reportedModules {};
    for (int rank = 0; rank < 4; ++rank)
    {
        size_t hottest = reportedModules.size();
        for (size_t index = 0; index < reportedModules.size(); ++index)
            if (!reportedModules[index]
                && accumulatedRealtimeProfile.moduleTicks[index] > 0
                && (hottest == reportedModules.size()
                    || accumulatedRealtimeProfile.moduleTicks[index]
                        > accumulatedRealtimeProfile.moduleTicks[hottest]))
                hottest = index;
        if (hottest == reportedModules.size())
            break;
        reportedModules[hottest] = true;
        const auto calls =
            accumulatedRealtimeProfile.moduleCalls[hottest];
        diagnostics.logRealtime(
            megadsp::DiagnosticEvent::moduleWorkload,
            diagnosticInstanceId, static_cast<std::int64_t>(hottest),
            calls > 0
                ? ticksToMicros(
                      accumulatedRealtimeProfile.moduleTicks[hottest])
                      / calls
                : 0,
            ticksToMicros(
                accumulatedRealtimeProfile.moduleMaximumTicks[hottest]),
            calls);
    }

    std::array<bool, megadsp::snapshotCount> reportedGraphs {};
    for (int rank = 0; rank < 4; ++rank)
    {
        size_t hottest = reportedGraphs.size();
        for (size_t index = 0; index < reportedGraphs.size(); ++index)
            if (!reportedGraphs[index]
                && accumulatedRealtimeProfile.graphTicks[index] > 0
                && (hottest == reportedGraphs.size()
                    || accumulatedRealtimeProfile.graphTicks[index]
                        > accumulatedRealtimeProfile.graphTicks[hottest]))
                hottest = index;
        if (hottest == reportedGraphs.size())
            break;
        reportedGraphs[hottest] = true;
        const auto calls =
            accumulatedRealtimeProfile.graphCalls[hottest];
        diagnostics.logRealtime(
            megadsp::DiagnosticEvent::snapshotGraphWorkload,
            diagnosticInstanceId, static_cast<std::int64_t>(hottest),
            calls > 0
                ? ticksToMicros(
                      accumulatedRealtimeProfile.graphTicks[hottest])
                      / calls
                : 0,
            ticksToMicros(
                accumulatedRealtimeProfile.graphMaximumTicks[hottest]),
            calls);
    }

    diagnosticWindowSamples = 0;
    diagnosticWindowCallbacks = 0;
    diagnosticTotalCallbackMicros = 0;
    diagnosticMaximumCallbackMicros = 0;
    diagnosticMinimumDeadlineMicros = 0;
    diagnosticOverrunCount = 0;
    diagnosticMaximumGraphCount = 0;
    accumulatedRealtimeProfile.reset();
}

void MegaDSPAudioProcessor::processProfiledRack(
    int snapshot,
    juce::AudioBuffer<float>& main,
    const juce::AudioBuffer<float>* sidechain,
    double bpm)
{
    auto* profile = captureRealtimeProfile ? &realtimeProfile : nullptr;
    const auto start = profile != nullptr
        ? juce::Time::getHighResolutionTicks() : 0;
    snapshotRacks[static_cast<size_t>(snapshot)]->process(
        main, sidechain, bpm, profile, isNonRealtime());
    if (profile != nullptr)
        profile->recordGraph(
            snapshot, static_cast<std::uint64_t>(
                juce::Time::getHighResolutionTicks() - start));
}

void MegaDSPAudioProcessor::updateHostBypassLatency(
    int latency) noexcept
{
    if (latency == lastHostBypassLatency)
        return;
    for (auto& channel : hostBypassDelay)
        std::fill(channel.begin(), channel.end(), 0.0f);
    hostBypassWritePosition = 0;
    lastHostBypassLatency = latency;
}

double MegaDSPAudioProcessor::currentBpm() const
{
    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
            if (const auto bpm = position->getBpm())
                return *bpm;
    return 120.0;
}

double MegaDSPAudioProcessor::getTailLengthSeconds() const
{
    const auto spillSeconds =
        static_cast<double>(
            snapshotTailSamples.load(std::memory_order_acquire))
        / juce::jmax(1.0, preparedSampleRate);
    return juce::jmax(getAudioRack().tailSeconds(), spillSeconds);
}

int MegaDSPAudioProcessor::getBackgroundThemeIndex() const
{
    return megadsp::ui::safeBackgroundThemeIndex(static_cast<int>(
        parameters.state.getProperty("backgroundTheme", 0)));
}

void MegaDSPAudioProcessor::setBackgroundThemeIndex(int index)
{
    const auto safeIndex = megadsp::ui::safeBackgroundThemeIndex(index);
    const auto storedIndex = static_cast<int>(
        parameters.state.getProperty("backgroundTheme", 0));
    if (safeIndex == storedIndex)
        return;
    ScopedProcessorUndoTransaction undo(*this, "Change theme");
    parameters.state.setProperty("backgroundTheme", safeIndex, nullptr);
    updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
}

juce::String MegaDSPAudioProcessor::getInstanceName() const
{
    return megadsp::ui::normalizeInstanceName(
        parameters.state.getProperty("instanceName").toString());
}

void MegaDSPAudioProcessor::setInstanceName(const juce::String& name)
{
    const auto normalized = megadsp::ui::normalizeInstanceName(name);
    if (normalized
        == parameters.state.getProperty("instanceName").toString())
        return;
    ScopedProcessorUndoTransaction undo(*this, "Rename instance");
    parameters.state.setProperty("instanceName", normalized, nullptr);
    updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
}

void MegaDSPAudioProcessor::setLiveSafeSnapshotsEnabled(bool enabled)
{
    if (liveSafeSnapshots.exchange(
            enabled, std::memory_order_acq_rel) == enabled)
        return;
    ScopedProcessorUndoTransaction undo(*this, "Toggle Live Safe");
    parameters.state.setProperty(
        "liveSafeSnapshots", enabled, nullptr);
    snapshotGeneration.fetch_add(1, std::memory_order_release);
    updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
}

void MegaDSPAudioProcessor::parameterChanged(
    const juce::String& parameterId, float value)
{
    if (publishingSnapshotParameters)
        return;

    const auto snapshotIndex =
        selectedSnapshot.load(std::memory_order_acquire);
    auto& state =
        snapshotStates[static_cast<size_t>(snapshotIndex)];
    if (parameterId == "global.input")
    {
        state.inputGainDb.store(value, std::memory_order_relaxed);
        return;
    }
    if (parameterId == "global.output")
    {
        state.outputGainDb.store(value, std::memory_order_relaxed);
        return;
    }

    for (int slot = 0; slot < megadsp::slotCount; ++slot)
    {
        auto& destination = state.slots[static_cast<size_t>(slot)];
        if (parameterId == typeParameterIds[static_cast<size_t>(slot)])
        {
            destination.type.store(value, std::memory_order_relaxed);
            topologyRequestGeneration.fetch_add(
                1, std::memory_order_release);
            pendingTopologySnapshotMask.fetch_or(
                std::uint32_t { 1 }
                    << static_cast<unsigned int>(snapshotIndex),
                std::memory_order_release);
            topologyDirty.store(true, std::memory_order_release);
            return;
        }
        if (parameterId == bypassParameterIds[static_cast<size_t>(slot)])
        {
            destination.bypass.store(value, std::memory_order_relaxed);
            return;
        }
        for (int control = 0;
             control < megadsp::controlsPerSlot; ++control)
        {
            if (parameterId
                != controlParameterIds[static_cast<size_t>(slot)]
                                      [static_cast<size_t>(control)])
                continue;
            destination.controls[static_cast<size_t>(control)].store(
                value, std::memory_order_relaxed);
            if (control == 0
                && static_cast<megadsp::ModuleType>(
                       juce::roundToInt(destination.type.load(
                           std::memory_order_relaxed)))
                       == megadsp::ModuleType::guitarCabinet)
                pendingBuiltInCabinetLoads[static_cast<size_t>(slot)].store(
                    megadsp::discreteIndex(
                        value,
                        megadsp::GuitarCabinetModule::builtInCount),
                    std::memory_order_release);
            if (static_cast<megadsp::ModuleType>(
                    juce::roundToInt(destination.type.load(
                        std::memory_order_relaxed)))
                    == megadsp::ModuleType::wormholeConvolution)
            {
                const auto lane = control <= 2 ? 0 : control >= 4 && control <= 6
                    ? 1 : -1;
                if (lane >= 0)
                    pendingWormholeReloads[static_cast<size_t>(slot)].fetch_or(
                        static_cast<std::uint8_t>(1u << lane),
                        std::memory_order_release);
            }
            return;
        }
    }
}

void MegaDSPAudioProcessor::captureParametersToSnapshot(int index)
{
    if (!juce::isPositiveAndBelow(index, megadsp::snapshotCount))
        return;
    auto& state = snapshotStates[static_cast<size_t>(index)];
    for (int slot = 0; slot < megadsp::slotCount; ++slot)
    {
        auto& destination = state.slots[static_cast<size_t>(slot)];
        if (const auto* value = parameters.getRawParameterValue(
                megadsp::slotParameterId(slot, "type")))
            destination.type.store(
                value->load(), std::memory_order_relaxed);
        if (const auto* value = parameters.getRawParameterValue(
                megadsp::slotParameterId(slot, "bypass")))
            destination.bypass.store(
                value->load(), std::memory_order_relaxed);
        for (int control = 0;
             control < megadsp::controlsPerSlot; ++control)
            if (const auto* value = parameters.getRawParameterValue(
                    megadsp::controlParameterId(slot, control)))
                destination.controls[static_cast<size_t>(control)].store(
                    value->load(), std::memory_order_relaxed);
    }
    if (const auto* value =
            parameters.getRawParameterValue("global.input"))
        state.inputGainDb.store(value->load(), std::memory_order_relaxed);
    if (const auto* value =
            parameters.getRawParameterValue("global.output"))
        state.outputGainDb.store(value->load(), std::memory_order_relaxed);
    state.populated.store(true, std::memory_order_release);
}

void MegaDSPAudioProcessor::publishSnapshotToParameters(
    int index, bool notifyHost)
{
    if (!juce::isPositiveAndBelow(index, megadsp::snapshotCount))
        return;
    const auto& state = snapshotStates[static_cast<size_t>(index)];
    publishingSnapshotParameters = true;
    const auto setPlain =
        [this, notifyHost](
            const juce::String& id, float plainValue)
    {
        if (auto* parameter = parameters.getParameter(id))
        {
            const auto normalized =
                parameter->convertTo0to1(plainValue);
            if (std::abs(parameter->getValue() - normalized)
                <= 1.0e-7f)
                return;
            if (notifyHost)
                parameter->setValueNotifyingHost(normalized);
            else
                parameter->setValue(normalized);
        }
    };
    for (int slot = 0; slot < megadsp::slotCount; ++slot)
    {
        const auto& source = state.slots[static_cast<size_t>(slot)];
        setPlain(
            megadsp::slotParameterId(slot, "type"),
            source.type.load(std::memory_order_relaxed));
        setPlain(
            megadsp::slotParameterId(slot, "bypass"),
            source.bypass.load(std::memory_order_relaxed));
        for (int control = 0;
             control < megadsp::controlsPerSlot; ++control)
            setPlain(
                megadsp::controlParameterId(slot, control),
                source.controls[static_cast<size_t>(control)].load(
                    std::memory_order_relaxed));
        const auto irProperty =
            juce::Identifier("impulseResponse" + juce::String(slot + 1));
        const auto irProperty2 =
            juce::Identifier("impulseResponse2" + juce::String(slot + 1));
        const auto namProperty =
            juce::Identifier("neuralModel" + juce::String(slot + 1));
        if (source.impulseResponsePath.isEmpty())
            parameters.state.removeProperty(irProperty, nullptr);
        else
            parameters.state.setProperty(
                irProperty, source.impulseResponsePath, nullptr);
        if (source.impulseResponsePath2.isEmpty())
            parameters.state.removeProperty(irProperty2, nullptr);
        else
            parameters.state.setProperty(
                irProperty2, source.impulseResponsePath2, nullptr);
        if (source.namModelPath.isEmpty())
            parameters.state.removeProperty(namProperty, nullptr);
        else
            parameters.state.setProperty(
                namProperty, source.namModelPath, nullptr);
    }
    setPlain(
        "global.input",
        state.inputGainDb.load(std::memory_order_relaxed));
    setPlain(
        "global.output",
        state.outputGainDb.load(std::memory_order_relaxed));
    publishingSnapshotParameters = false;
}

bool MegaDSPAudioProcessor::isSnapshotPopulated(int index) const noexcept
{
    return juce::isPositiveAndBelow(index, megadsp::snapshotCount)
           && snapshotStates[static_cast<size_t>(index)]
                  .populated.load(std::memory_order_acquire);
}

bool MegaDSPAudioProcessor::isSnapshotReady(int index) const noexcept
{
    return juce::isPositiveAndBelow(index, megadsp::snapshotCount)
           && snapshotStates[static_cast<size_t>(index)]
                  .ready.load(std::memory_order_acquire);
}

juce::String MegaDSPAudioProcessor::snapshotError(int index) const
{
    return juce::isPositiveAndBelow(index, megadsp::snapshotCount)
               ? snapshotStates[static_cast<size_t>(index)].error
               : juce::String();
}

void MegaDSPAudioProcessor::prepareSnapshot(int index)
{
    if (!juce::isPositiveAndBelow(index, megadsp::snapshotCount))
        return;
    auto& state = snapshotStates[static_cast<size_t>(index)];
    state.ready.store(false, std::memory_order_release);
    state.error.clear();
    snapshotRacks[static_cast<size_t>(index)]->rebuildModules(false);
    restoreSnapshotAssets(index);
    state.ready.store(true, std::memory_order_release);
}

void MegaDSPAudioProcessor::restoreSnapshotAssets(int index)
{
    if (!juce::isPositiveAndBelow(index, megadsp::snapshotCount))
        return;
    auto& rack = *snapshotRacks[static_cast<size_t>(index)];
    auto& state = snapshotStates[static_cast<size_t>(index)];
    for (int slot = 0; slot < megadsp::slotCount; ++slot)
    {
        if (rack.moduleType(slot)
            == megadsp::ModuleType::convolutionReverb)
        {
            auto& slotState =
                state.slots[static_cast<size_t>(slot)];
            rack.setConvolutionWetBufferSamples(
                slot, slotState.convolutionWetBufferSamples);
            for (int bank = 0;
                 bank < megadsp::ConvolutionReverbModule::
                            impulseBankCount;
                 ++bank)
            {
                const auto& path =
                    slotState.convolutionImpulseBankPaths[
                        static_cast<size_t>(bank)];
                if (path.isEmpty())
                    continue;
                megadsp::EffectRack::PreparedImpulseResponseLoad load;
                auto result =
                    rack.prepareConvolutionImpulseResponseBank(
                        slot, bank, juce::File(path), load);
                if (result.wasOk()
                    && !rack.commitConvolutionImpulseResponseBank(load))
                    result = juce::Result::fail(
                        "The impulse response was superseded.");
                if (result.failed())
                    state.error +=
                        "IR bank " + juce::String(bank + 1)
                        + " slot " + juce::String(slot + 1)
                        + ": " + result.getErrorMessage() + " ";
            }
            rack.selectConvolutionImpulseResponseBank(
                slot, slotState.convolutionActiveBank);
            continue;
        }
        const std::array<juce::String, 2> impulsePaths {
            state.slots[static_cast<size_t>(slot)].impulseResponsePath,
            state.slots[static_cast<size_t>(slot)].impulseResponsePath2
        };
        for (int lane = 0; lane < 2; ++lane)
        {
            const auto& ir = impulsePaths[static_cast<size_t>(lane)];
            if (ir.isNotEmpty())
            {
                const auto result = rack.loadImpulseResponse(
                    slot, lane, juce::File(ir));
                if (result.failed())
                    state.error += "IR " + juce::String(lane == 0 ? "A" : "B")
                        + " slot " + juce::String(slot + 1)
                        + ": " + result.getErrorMessage() + " ";
            }
        }
        const auto nam =
            state.slots[static_cast<size_t>(slot)].namModelPath;
        if (nam.isNotEmpty())
        {
            const auto result = rack.loadNamModel(slot, juce::File(nam));
            if (result.failed())
                state.error += "NAM slot " + juce::String(slot + 1)
                               + ": " + result.getErrorMessage() + " ";
        }
    }
}

void MegaDSPAudioProcessor::recallSnapshot(int index)
{
    if (!juce::isPositiveAndBelow(index, megadsp::snapshotCount))
        return;
    if (index == getSelectedSnapshot())
        return;
    ScopedProcessorUndoTransaction undo(*this, "Recall snapshot");
    if (!isSnapshotPopulated(index))
    {
        suspendProcessing(true);
        auto& state = snapshotStates[static_cast<size_t>(index)];
        state.clear();
        state.populated.store(true, std::memory_order_release);
        prepareSnapshot(index);
        suspendProcessing(false);
    }
    if (!isSnapshotReady(index))
        return;
    selectedSnapshot.store(index, std::memory_order_release);
    pendingSnapshotRecall.store(index, std::memory_order_release);
    snapshotUiDirty.store(true, std::memory_order_release);
    snapshotGeneration.fetch_add(1, std::memory_order_release);
}

void MegaDSPAudioProcessor::storeSnapshot(int index, bool overwrite)
{
    if (!juce::isPositiveAndBelow(index, megadsp::snapshotCount)
        || (isSnapshotPopulated(index) && !overwrite))
        return;
    ScopedProcessorUndoTransaction undo(*this, "Store snapshot");
    const auto source = getSelectedSnapshot();
    suspendProcessing(true);
    snapshotStates[static_cast<size_t>(index)].copyFrom(
        snapshotStates[static_cast<size_t>(source)]);
    snapshotStates[static_cast<size_t>(index)]
        .populated.store(true, std::memory_order_release);
    prepareSnapshot(index);
    suspendProcessing(false);
    recallSnapshot(index);
}

void MegaDSPAudioProcessor::copySnapshot(int source, int destination)
{
    if (!juce::isPositiveAndBelow(source, megadsp::snapshotCount)
        || !juce::isPositiveAndBelow(destination, megadsp::snapshotCount)
        || source == destination
        || !isSnapshotPopulated(source)
        || !isSnapshotReady(source))
        return;

    ScopedProcessorUndoTransaction undo(*this, "Copy snapshot");
    suspendProcessing(true);
    for (auto& spill : spillSnapshots)
    {
        if (spill.snapshot >= 0)
            snapshotRacks[static_cast<size_t>(spill.snapshot)]->reset();
        spill = {};
    }
    if (snapshotTransition.outgoing >= 0)
        snapshotRacks[static_cast<size_t>(
            snapshotTransition.outgoing)]->reset();
    snapshotTransition = {};
    snapshotTailSamples.store(0, std::memory_order_release);

    snapshotStates[static_cast<size_t>(destination)].copyFrom(
        snapshotStates[static_cast<size_t>(source)]);
    snapshotStates[static_cast<size_t>(destination)]
        .populated.store(true, std::memory_order_release);
    prepareSnapshot(destination);
    suspendProcessing(false);

    if (destination == getSelectedSnapshot())
        publishSnapshotToParameters(destination);
    topologyGeneration.fetch_add(1, std::memory_order_release);
    snapshotGeneration.fetch_add(1, std::memory_order_release);
    updateHostDisplay(
        ChangeDetails{}.withNonParameterStateChanged(true));
    refreshLatency();
}

void MegaDSPAudioProcessor::beginSnapshotRecall(int index) noexcept
{
    if (!juce::isPositiveAndBelow(index, megadsp::snapshotCount)
        || !snapshotStates[static_cast<size_t>(index)]
                .populated.load(std::memory_order_acquire)
        || !snapshotStates[static_cast<size_t>(index)]
                .ready.load(std::memory_order_acquire))
        return;

    const auto current =
        activeAudioSnapshot.load(std::memory_order_acquire);
    if (index == current)
        return;
    if (snapshotTransition.outgoing == index
        && snapshotTransition.incoming == current)
    {
        std::swap(
            snapshotTransition.outgoing,
            snapshotTransition.incoming);
        snapshotTransition.samplesDone = juce::jlimit(
            0, snapshotTransition.totalSamples,
            snapshotTransition.totalSamples
                - snapshotTransition.samplesDone);
        snapshotTransition.fadeIncomingOutput = false;
        const auto activeVisualSlots =
            snapshotRacks[static_cast<size_t>(current)]
                ->visualizationData().getActiveSlotMask();
        snapshotRacks[static_cast<size_t>(current)]
            ->visualizationData().clearActiveSlots();
        snapshotRacks[static_cast<size_t>(index)]
            ->visualizationData().setActiveSlotMask(activeVisualSlots);
        activeAudioSnapshot.store(index, std::memory_order_release);
        selectedSnapshot.store(index, std::memory_order_release);
        snapshotUiDirty.store(true, std::memory_order_release);
        snapshotGeneration.fetch_add(1, std::memory_order_release);
        return;
    }
    if (snapshotTransition.outgoing >= 0)
        addSpillSnapshot(snapshotTransition.outgoing);
    auto incomingWasSpilling = false;
    for (auto& spill : spillSnapshots)
        if (spill.snapshot == index)
        {
            incomingWasSpilling = true;
            spill = {};
        }

    snapshotTransition.outgoing = current;
    snapshotTransition.incoming = index;
    snapshotTransition.samplesDone = 0;
    snapshotTransition.totalSamples = juce::jmax(
        1, juce::roundToInt(preparedSampleRate * 0.02));
    snapshotTransition.fadeIncomingOutput = !incomingWasSpilling;
    const auto activeVisualSlots =
        snapshotRacks[static_cast<size_t>(current)]
            ->visualizationData().getActiveSlotMask();
    snapshotRacks[static_cast<size_t>(current)]
        ->visualizationData().clearActiveSlots();
    snapshotRacks[static_cast<size_t>(index)]
        ->visualizationData().setActiveSlotMask(activeVisualSlots);
    activeAudioSnapshot.store(index, std::memory_order_release);
    selectedSnapshot.store(index, std::memory_order_release);
    snapshotTailSamples.store(
        juce::jmax(
            snapshotTailSamples.load(std::memory_order_relaxed),
            snapshotTransition.totalSamples
                + juce::roundToInt(
                    snapshotRacks[static_cast<size_t>(current)]
                        ->snapshotSpillSeconds()
                    * preparedSampleRate)),
        std::memory_order_release);
    snapshotUiDirty.store(true, std::memory_order_release);
    snapshotGeneration.fetch_add(1, std::memory_order_release);
}

void MegaDSPAudioProcessor::addSpillSnapshot(int index) noexcept
{
    if (!juce::isPositiveAndBelow(index, megadsp::snapshotCount)
        || index == activeAudioSnapshot.load(std::memory_order_acquire))
        return;
    for (const auto& spill : spillSnapshots)
        if (spill.snapshot == index)
            return;
    const auto tailSamples = juce::jlimit(
        0, juce::roundToInt(preparedSampleRate * 30.0),
        juce::jmax(
            snapshotRacks[static_cast<size_t>(index)]->latencySamples(),
            juce::roundToInt(
                snapshotRacks[static_cast<size_t>(index)]
                    ->snapshotSpillSeconds()
                * preparedSampleRate)));
    if (tailSamples <= 0)
        return;
    snapshotTailSamples.store(
        juce::jmax(
            snapshotTailSamples.load(std::memory_order_relaxed),
            tailSamples),
        std::memory_order_release);

    auto activeSpills = 0;
    for (const auto& spill : spillSnapshots)
        if (spill.snapshot >= 0 && spill.fadeSamplesRemaining <= 0)
            ++activeSpills;
    if (activeSpills >= 2)
    {
        auto* oldest = static_cast<SpillState*>(nullptr);
        for (auto& spill : spillSnapshots)
            if (spill.snapshot >= 0 && spill.fadeSamplesRemaining <= 0
                && (oldest == nullptr || spill.order < oldest->order))
                oldest = &spill;
        jassert(oldest != nullptr);
        oldest->fadeSamplesTotal = juce::jmax(
            1, juce::roundToInt(preparedSampleRate * 0.02));
        oldest->fadeSamplesRemaining = oldest->fadeSamplesTotal;
    }
    for (auto& spill : spillSnapshots)
        if (spill.snapshot < 0)
        {
            spill.snapshot = index;
            spill.samplesRemaining = tailSamples;
            spill.naturalFadeSamples = juce::jmax(
                1, juce::jmin(
                       tailSamples,
                       juce::roundToInt(preparedSampleRate * 0.5)));
            spill.order = ++nextSpillOrder;
            return;
        }

    auto* oldest = &spillSnapshots[0];
    for (auto& spill : spillSnapshots)
        if (spill.order < oldest->order)
            oldest = &spill;
    *oldest = {
        index,
        tailSamples,
        juce::jmax(
            1, juce::jmin(
                   tailSamples,
                   juce::roundToInt(preparedSampleRate * 0.5))),
        0,
        0,
        ++nextSpillOrder
    };
}

void MegaDSPAudioProcessor::processSpillSnapshots(
    juce::AudioBuffer<float>& main,
    const juce::AudioBuffer<float>*,
    double bpm)
{
    for (auto& spill : spillSnapshots)
    {
        if (spill.snapshot < 0)
            continue;
        snapshotZero.clear();
        std::array<float*, 2> pointers {};
        for (int channel = 0; channel < main.getNumChannels(); ++channel)
            pointers[static_cast<size_t>(channel)] =
                snapshotZero.getWritePointer(channel);
        juce::AudioBuffer<float> tail(
            pointers.data(), main.getNumChannels(), main.getNumSamples());
        processProfiledRack(spill.snapshot, tail, nullptr, bpm);

        applySnapshotSpillFade(
            tail, spill.samplesRemaining,
            spill.naturalFadeSamples,
            spill.fadeSamplesRemaining, spill.fadeSamplesTotal);
        spill.fadeSamplesRemaining = juce::jmax(
            0, spill.fadeSamplesRemaining - main.getNumSamples());
        for (int channel = 0; channel < main.getNumChannels(); ++channel)
            main.addFrom(
                channel, 0, tail, channel, 0, main.getNumSamples());
        spill.samplesRemaining -= main.getNumSamples();
        if (spill.samplesRemaining <= 0
            || (spill.fadeSamplesTotal > 0
                && spill.fadeSamplesRemaining <= 0))
        {
            spill = {};
        }
    }
    auto remaining = 0;
    for (const auto& spill : spillSnapshots)
        if (spill.snapshot >= 0)
            remaining = juce::jmax(remaining, spill.samplesRemaining);
    if (snapshotTransition.outgoing >= 0)
        remaining = juce::jmax(
            remaining,
            juce::jmax(
                0,
                snapshotTransition.totalSamples
                    - snapshotTransition.samplesDone
                    - main.getNumSamples())
                + juce::roundToInt(
                    snapshotRacks[static_cast<size_t>(
                        snapshotTransition.outgoing)]
                        ->snapshotSpillSeconds()
                    * preparedSampleRate));
    snapshotTailSamples.store(remaining, std::memory_order_release);
}

void MegaDSPAudioProcessor::mixLiveSafeSpills(
    juce::AudioBuffer<float>& main)
{
    for (auto& spill : spillSnapshots)
    {
        if (spill.snapshot < 0)
            continue;
        auto& tail =
            liveSafeSnapshotOutputs[static_cast<size_t>(spill.snapshot)];
        applySnapshotSpillFade(
            tail, spill.samplesRemaining,
            spill.naturalFadeSamples,
            spill.fadeSamplesRemaining, spill.fadeSamplesTotal);
        spill.fadeSamplesRemaining = juce::jmax(
            0, spill.fadeSamplesRemaining - main.getNumSamples());
        for (int channel = 0; channel < main.getNumChannels(); ++channel)
            main.addFrom(
                channel, 0, tail, channel, 0, main.getNumSamples());
        spill.samplesRemaining -= main.getNumSamples();
        if (spill.samplesRemaining <= 0
            || (spill.fadeSamplesTotal > 0
                && spill.fadeSamplesRemaining <= 0))
            spill = {};
    }

    auto remaining = 0;
    for (const auto& spill : spillSnapshots)
        if (spill.snapshot >= 0)
            remaining = juce::jmax(remaining, spill.samplesRemaining);
    if (snapshotTransition.outgoing >= 0)
        remaining = juce::jmax(
            remaining,
            juce::jmax(
                0,
                snapshotTransition.totalSamples
                    - snapshotTransition.samplesDone
                    - main.getNumSamples())
                + juce::roundToInt(
                    snapshotRacks[static_cast<size_t>(
                        snapshotTransition.outgoing)]
                        ->snapshotSpillSeconds()
                    * preparedSampleRate));
    snapshotTailSamples.store(remaining, std::memory_order_release);
}

void MegaDSPAudioProcessor::processLiveSafeSnapshotRange(
    juce::AudioBuffer<float>& main,
    const juce::AudioBuffer<float>* sidechain,
    double bpm)
{
    const auto active =
        activeAudioSnapshot.load(std::memory_order_acquire);
    const auto transitioning = snapshotTransition.outgoing >= 0;
    auto start = 1.0f;
    auto end = 1.0f;
    if (transitioning)
    {
        start = juce::jlimit(
            0.0f, 1.0f,
            static_cast<float>(snapshotTransition.samplesDone)
                / static_cast<float>(snapshotTransition.totalSamples));
        end = juce::jlimit(
            0.0f, 1.0f,
            static_cast<float>(
                snapshotTransition.samplesDone + main.getNumSamples())
                / static_cast<float>(
                    snapshotTransition.totalSamples));
    }

    auto processed = 0;
    for (int index = 0; index < megadsp::snapshotCount; ++index)
    {
        const auto& state =
            snapshotStates[static_cast<size_t>(index)];
        if (!state.populated.load(std::memory_order_acquire)
            || !state.ready.load(std::memory_order_acquire))
            continue;

        auto& storage =
            liveSafeSnapshotOutputs[static_cast<size_t>(index)];
        std::array<float*, 2> pointers {};
        for (int channel = 0; channel < main.getNumChannels(); ++channel)
            pointers[static_cast<size_t>(channel)] =
                storage.getWritePointer(channel);
        juce::AudioBuffer<float> output(
            pointers.data(), main.getNumChannels(), main.getNumSamples());

        const auto incoming =
            transitioning && index == snapshotTransition.incoming;
        const auto outgoing =
            transitioning && index == snapshotTransition.outgoing;
        const auto receivesLiveInput =
            transitioning ? incoming || outgoing : index == active;
        if (receivesLiveInput)
        {
            for (int channel = 0; channel < main.getNumChannels(); ++channel)
                output.copyFrom(
                    channel, 0, main, channel, 0,
                    main.getNumSamples());
            if (outgoing
                || (incoming
                    && !snapshotTransition.fadeIncomingOutput))
                applySnapshotTransitionWeight(
                    output, start, end, incoming);
        }
        else
        {
            output.clear();
        }

        processProfiledRack(
            index, output, receivesLiveInput ? sidechain : nullptr, bpm);
        if (incoming && snapshotTransition.fadeIncomingOutput)
            applySnapshotTransitionWeight(
                output, start, end, true);
        ++processed;
    }
    processedSnapshotGraphCount.store(
        processed, std::memory_order_release);

    main.clear();
    const auto addSnapshot = [this, &main](int index)
    {
        if (!juce::isPositiveAndBelow(index, megadsp::snapshotCount))
            return;
        const auto& output =
            liveSafeSnapshotOutputs[static_cast<size_t>(index)];
        for (int channel = 0; channel < main.getNumChannels(); ++channel)
            main.addFrom(
                channel, 0, output, channel, 0,
                main.getNumSamples());
    };
    if (transitioning)
    {
        addSnapshot(snapshotTransition.outgoing);
        addSnapshot(snapshotTransition.incoming);
    }
    else
    {
        addSnapshot(active);
    }
    mixLiveSafeSpills(main);

    if (transitioning)
    {
        snapshotTransition.samplesDone += main.getNumSamples();
        if (snapshotTransition.samplesDone
            >= snapshotTransition.totalSamples)
        {
            addSpillSnapshot(snapshotTransition.outgoing);
            snapshotTransition = {};
        }
    }
}

void MegaDSPAudioProcessor::processSnapshotRange(
    juce::AudioBuffer<float>& main,
    const juce::AudioBuffer<float>* sidechain,
    double bpm)
{
    if (snapshotTransition.outgoing >= 0)
    {
        const auto remaining = juce::jmax(
            1, snapshotTransition.totalSamples
                   - snapshotTransition.samplesDone);
        if (main.getNumSamples() > remaining)
        {
            for (int part = 0; part < 2; ++part)
            {
                const auto offset = part == 0 ? 0 : remaining;
                const auto samples = part == 0
                                         ? remaining
                                         : main.getNumSamples() - remaining;
                std::array<float*, 2> mainPointers {};
                for (int channel = 0;
                     channel < main.getNumChannels(); ++channel)
                    mainPointers[static_cast<size_t>(channel)] =
                        main.getWritePointer(channel, offset);
                juce::AudioBuffer<float> mainPart(
                    mainPointers.data(), main.getNumChannels(), samples);

                std::array<float*, 2> sidechainPointers {};
                std::optional<juce::AudioBuffer<float>> sidechainPart;
                if (sidechain != nullptr)
                {
                    for (int channel = 0;
                         channel < sidechain->getNumChannels(); ++channel)
                        sidechainPointers[static_cast<size_t>(channel)] =
                            const_cast<float*>(
                                sidechain->getReadPointer(channel, offset));
                    sidechainPart.emplace(
                        sidechainPointers.data(),
                        sidechain->getNumChannels(), samples);
                }
                processSnapshotRange(
                    mainPart,
                    sidechainPart.has_value() ? &*sidechainPart : nullptr,
                    bpm);
            }
            return;
        }
    }

    if (main.getNumSamples() > preparedMaximumBlockSize)
    {
        for (int offset = 0; offset < main.getNumSamples();
             offset += preparedMaximumBlockSize)
        {
            const auto samples = juce::jmin(
                preparedMaximumBlockSize,
                main.getNumSamples() - offset);
            std::array<float*, 2> mainPointers {};
            for (int channel = 0;
                 channel < main.getNumChannels(); ++channel)
                mainPointers[static_cast<size_t>(channel)] =
                    main.getWritePointer(channel, offset);
            juce::AudioBuffer<float> mainChunk(
                mainPointers.data(), main.getNumChannels(), samples);

            std::array<float*, 2> sidechainPointers {};
            std::optional<juce::AudioBuffer<float>> sidechainChunk;
            if (sidechain != nullptr)
            {
                for (int channel = 0;
                     channel < sidechain->getNumChannels(); ++channel)
                    sidechainPointers[static_cast<size_t>(channel)] =
                        const_cast<float*>(
                            sidechain->getReadPointer(channel, offset));
                sidechainChunk.emplace(
                    sidechainPointers.data(),
                    sidechain->getNumChannels(), samples);
            }
            processSnapshotRange(
                mainChunk,
                sidechainChunk.has_value() ? &*sidechainChunk : nullptr,
                bpm);
        }
        return;
    }

    if (liveSafeSnapshots.load(std::memory_order_acquire))
    {
        processLiveSafeSnapshotRange(main, sidechain, bpm);
        return;
    }

    processedSnapshotGraphCount.store(
        snapshotTransition.outgoing >= 0 ? 2 : 1,
        std::memory_order_release);
    if (snapshotTransition.outgoing < 0)
    {
        processProfiledRack(
            activeAudioSnapshot.load(std::memory_order_relaxed),
            main, sidechain, bpm);
        processSpillSnapshots(main, sidechain, bpm);
        return;
    }

    for (int channel = 0; channel < main.getNumChannels(); ++channel)
        snapshotScratch.copyFrom(
            channel, 0, main, channel, 0, main.getNumSamples());
    std::array<float*, 2> pointers {};
    for (int channel = 0; channel < main.getNumChannels(); ++channel)
        pointers[static_cast<size_t>(channel)] =
            snapshotScratch.getWritePointer(channel);
    juce::AudioBuffer<float> outgoing(
        pointers.data(), main.getNumChannels(), main.getNumSamples());

    const auto start = juce::jlimit(
        0.0f, 1.0f,
        static_cast<float>(snapshotTransition.samplesDone)
            / static_cast<float>(snapshotTransition.totalSamples));
    const auto end = juce::jlimit(
        0.0f, 1.0f,
        static_cast<float>(
            snapshotTransition.samplesDone + main.getNumSamples())
            / static_cast<float>(snapshotTransition.totalSamples));
    applySnapshotTransitionWeight(outgoing, start, end, false);
    if (!snapshotTransition.fadeIncomingOutput)
        applySnapshotTransitionWeight(main, start, end, true);
    processProfiledRack(
        snapshotTransition.outgoing, outgoing, sidechain, bpm);
    processProfiledRack(
        snapshotTransition.incoming, main, sidechain, bpm);
    if (snapshotTransition.fadeIncomingOutput)
        applySnapshotTransitionWeight(main, start, end, true);
    for (int channel = 0; channel < main.getNumChannels(); ++channel)
        main.addFrom(
            channel, 0, outgoing, channel, 0, main.getNumSamples());
    processSpillSnapshots(main, sidechain, bpm);

    snapshotTransition.samplesDone += main.getNumSamples();
    if (snapshotTransition.samplesDone
        >= snapshotTransition.totalSamples)
    {
        addSpillSnapshot(snapshotTransition.outgoing);
        snapshotTransition = {};
    }
}

bool MegaDSPAudioProcessor::beginTopologyMutation()
{
    auto expected = false;
    if (!topologyMutationInProgress.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
    {
        topologyDirty.store(true, std::memory_order_release);
        return false;
    }
    suspendProcessing(true);
    return true;
}

void MegaDSPAudioProcessor::finishTopologyMutation(
    bool compact, bool resetRack, bool publishAlways)
{
    auto& rack = getRack();
    const auto compacted = compact && rack.compactSlots();
    const auto modulesChanged = rack.synchronizeModules(false);
    if (resetRack || compacted || modulesChanged)
    {
        rack.reset();
        rack.visualizationData().clear();
    }
    selectedTab = juce::jlimit(
        0, juce::jmax(0, rack.activeSlotCount() - 1), selectedTab);
    selectedSnapshotState().populated.store(
        true, std::memory_order_release);
    selectedSnapshotState().ready.store(
        true, std::memory_order_release);
    if (compacted)
        publishSnapshotToParameters(getSelectedSnapshot());
    suspendProcessing(false);
    refreshLatency();
    if (publishAlways || compacted || modulesChanged)
        topologyGeneration.fetch_add(1, std::memory_order_release);
    const auto generation =
        topologyGeneration.load(std::memory_order_acquire);
    megadsp::Diagnostics::instance().logRealtime(
        megadsp::DiagnosticEvent::topologyChanged,
        diagnosticInstanceId, static_cast<std::int64_t>(generation),
        rack.activeSlotCount());
    juce::StringArray moduleNames;
    for (int slot = 0; slot < rack.activeSlotCount(); ++slot)
        moduleNames.add(
            megadsp::moduleDefinition(rack.moduleType(slot)).displayName);
    megadsp::Diagnostics::instance().log(
        "topology",
        "instance=" + juce::String(diagnosticInstanceId)
            + " generation=" + juce::String(generation)
            + " modules=[" + moduleNames.joinIntoString(", ") + "]");
    restorePendingImpulseResponses();
    restorePendingNamModels();
    refreshLatency();
    topologyMutationInProgress.store(false, std::memory_order_release);
    if (topologyDirty.load(std::memory_order_acquire))
        triggerAsyncUpdate();
}

void MegaDSPAudioProcessor::restorePendingImpulseResponses()
{
    auto& rack = getRack();
    for (int slot = 0; slot < megadsp::slotCount; ++slot)
    {
        for (int lane = 0; lane < 2; ++lane)
        {
            const auto path =
                rack.pendingImpulseResponsePath(slot, lane);
            if (path.isEmpty())
                continue;
            megadsp::EffectRack::PreparedImpulseResponseLoad load;
            const auto result = rack.prepareImpulseResponse(
                slot, lane, juce::File(path), load);
            if (result.failed())
            {
                rack.discardPendingImpulseResponse(slot, lane, path);
                continue;
            }
            suspendProcessing(true);
            const auto committed = rack.commitImpulseResponse(load);
            suspendProcessing(false);
            if (!committed)
                rack.cancelImpulseResponse(load);
        }
    }
}

void MegaDSPAudioProcessor::restorePendingNamModels()
{
    auto& rack = getRack();
    for (int slot = 0; slot < megadsp::slotCount; ++slot)
    {
        const auto path = rack.pendingNamModelPath(slot);
        if (path.isEmpty())
            continue;

        megadsp::EffectRack::PreparedNamModelLoad load;
        const auto result = rack.prepareNamModel(
            slot, juce::File(path), load);
        if (result.failed())
        {
            rack.discardPendingNamModel(slot, path);
            continue;
        }

        suspendProcessing(true);
        const auto committed = rack.commitNamModel(load);
        refreshLatency();
        suspendProcessing(false);
        if (!committed)
            rack.cancelNamModel(load);
    }
}

void MegaDSPAudioProcessor::handleAsyncUpdate()
{
    if (!topologyDirty.exchange(false, std::memory_order_acq_rel))
        return;
    const auto requestGeneration =
        topologyRequestGeneration.load(std::memory_order_acquire);
    auto snapshotMask = pendingTopologySnapshotMask.exchange(
        0, std::memory_order_acq_rel);
    if (snapshotMask == 0)
        snapshotMask =
            std::uint32_t { 1 }
            << static_cast<unsigned int>(getSelectedSnapshot());

    auto expected = false;
    if (!topologyMutationInProgress.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
    {
        pendingTopologySnapshotMask.fetch_or(
            snapshotMask, std::memory_order_release);
        topologyDirty.store(true, std::memory_order_release);
        return;
    }

    suspendProcessing(true);
    for (int index = 0; index < megadsp::snapshotCount; ++index)
    {
        const auto bit =
            std::uint32_t { 1 } << static_cast<unsigned int>(index);
        if ((snapshotMask & bit) == 0)
            continue;
        auto& rack = *snapshotRacks[static_cast<size_t>(index)];
        const auto compacted = rack.compactSlots();
        const auto changed = rack.synchronizeModules(false);
        if (compacted || changed)
        {
            rack.reset();
            rack.visualizationData().clear();
        }
        snapshotStates[static_cast<size_t>(index)]
            .populated.store(true, std::memory_order_release);
        snapshotStates[static_cast<size_t>(index)]
            .ready.store(true, std::memory_order_release);
        if (index == getSelectedSnapshot() && compacted)
            publishSnapshotToParameters(index);
    }
    suspendProcessing(false);
    topologyGeneration.fetch_add(1, std::memory_order_release);
    refreshLatency();
    topologyMutationInProgress.store(false, std::memory_order_release);

    if (topologyRequestGeneration.load(std::memory_order_acquire)
            != requestGeneration
        || pendingTopologySnapshotMask.load(std::memory_order_acquire) != 0)
    {
        topologyDirty.store(true, std::memory_order_release);
        triggerAsyncUpdate();
    }
}

void MegaDSPAudioProcessor::serializeSnapshots(
    juce::ValueTree& root) const
{
    if (const auto existing = root.getChildWithName("SNAPSHOTS");
        existing.isValid())
        root.removeChild(existing, nullptr);
    juce::ValueTree snapshots("SNAPSHOTS");
    snapshots.setProperty(
        "selected",
        selectedSnapshot.load(std::memory_order_acquire), nullptr);
    for (int index = 0; index < megadsp::snapshotCount; ++index)
        snapshots.addChild(
            snapshotStates[static_cast<size_t>(index)]
                .toValueTree(index),
            -1, nullptr);
    root.addChild(snapshots, -1, nullptr);
}

void MegaDSPAudioProcessor::restoreSnapshots(
    const juce::ValueTree& root, int sourceSchema)
{
    for (auto& state : snapshotStates)
        state.clear();

    auto selected = 0;
    const auto snapshots = root.getChildWithName("SNAPSHOTS");
    if (sourceSchema >= 10 && snapshots.isValid())
    {
        selected = juce::jlimit(
            0, megadsp::snapshotCount - 1,
            static_cast<int>(snapshots.getProperty("selected", 0)));
        for (const auto child : snapshots)
        {
            if (!child.hasType("SNAPSHOT"))
                continue;
            const auto index =
                static_cast<int>(child.getProperty("index", -1));
            if (juce::isPositiveAndBelow(
                    index, megadsp::snapshotCount))
            {
                snapshotStates[static_cast<size_t>(index)]
                    .fromValueTree(child);
                if (sourceSchema < 11)
                {
                    auto& state =
                        snapshotStates[static_cast<size_t>(index)];
                    for (auto& slot : state.slots)
                    {
                        const auto type = static_cast<megadsp::ModuleType>(
                            juce::roundToInt(slot.type.load(
                                std::memory_order_relaxed)));
                        if (type
                            == megadsp::ModuleType::algorithmicReverb)
                            slot.controls[7].store(
                                0.0f, std::memory_order_relaxed);
                        else if (type
                                 == megadsp::ModuleType::convolutionReverb)
                            slot.controls[5].store(
                                0.0f, std::memory_order_relaxed);
                    }
                }
                if (sourceSchema < 13)
                {
                    auto& state =
                        snapshotStates[static_cast<size_t>(index)];
                    for (auto& slot : state.slots)
                    {
                        if (static_cast<megadsp::ModuleType>(
                                juce::roundToInt(slot.type.load(
                                    std::memory_order_relaxed)))
                            != megadsp::ModuleType::stageVocal)
                            continue;
                        std::array<float, megadsp::controlsPerSlot>
                            legacy {};
                        for (int control = 0;
                             control < megadsp::controlsPerSlot; ++control)
                            legacy[static_cast<size_t>(control)] =
                                slot.controls[static_cast<size_t>(control)]
                                    .load(std::memory_order_relaxed);
                        const auto migrated =
                            megadsp::migrateStageVocalControlsFromSchema12(
                                legacy);
                        for (int control = 0;
                             control < megadsp::controlsPerSlot; ++control)
                            slot.controls[static_cast<size_t>(control)].store(
                                migrated[static_cast<size_t>(control)],
                                std::memory_order_relaxed);
                    }
                }
            }
        }
    }
    else
    {
        captureParametersToSnapshot(0);
        for (int slot = 0; slot < megadsp::slotCount; ++slot)
        {
            snapshotStates[0].slots[static_cast<size_t>(slot)]
                .impulseResponsePath =
                root.getProperty(
                    "impulseResponse" + juce::String(slot + 1)).toString();
            snapshotStates[0].slots[static_cast<size_t>(slot)]
                .convolutionImpulseBankPaths[0] =
                    snapshotStates[0].slots[static_cast<size_t>(slot)]
                        .impulseResponsePath;
            snapshotStates[0].slots[static_cast<size_t>(slot)]
                .impulseResponsePath2 =
                root.getProperty(
                    "impulseResponse2" + juce::String(slot + 1)).toString();
            snapshotStates[0].slots[static_cast<size_t>(slot)]
                .namModelPath =
                root.getProperty(
                    "neuralModel" + juce::String(slot + 1)).toString();
        }
        snapshotStates[0].populated.store(
            true, std::memory_order_release);
    }

    if (!snapshotStates[static_cast<size_t>(selected)]
             .populated.load(std::memory_order_acquire))
        selected = 0;
    for (int index = 0; index < megadsp::snapshotCount; ++index)
    {
        auto& state = snapshotStates[static_cast<size_t>(index)];
        if (state.populated.load(std::memory_order_acquire))
        {
            prepareSnapshot(index);
        }
        else
        {
            snapshotRacks[static_cast<size_t>(index)]
                ->rebuildModules();
            state.ready.store(false, std::memory_order_release);
        }
    }
    selectedSnapshot.store(selected, std::memory_order_release);
    activeAudioSnapshot.store(selected, std::memory_order_release);
    snapshotTransition = {};
    for (auto& spill : spillSnapshots)
        spill = {};
    snapshotTailSamples.store(0, std::memory_order_release);
    publishSnapshotToParameters(selected, false);
    snapshotGeneration.fetch_add(1, std::memory_order_release);
}

void MegaDSPAudioProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    auto state = parameters.copyState();
    for (auto child : state)
    {
        if (auto* parameter = parameters.getParameter(
                child.getProperty("id").toString());
            dynamic_cast<juce::AudioParameterBool*>(parameter) != nullptr)
        {
            child.setProperty("value", parameter->getValue(), nullptr);
        }
    }
    state.setProperty("schemaVersion", megadsp::stateSchemaVersion, nullptr);
    state.setProperty("selectedTab", selectedTab, nullptr);
    state.setProperty("editorWidth", editorWidth, nullptr);
    state.setProperty("editorHeight", editorHeight, nullptr);
    serializeSnapshots(state);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destination);
}

void MegaDSPAudioProcessor::setStateInformation(const void* data, int size)
{
    if (!restoringUndoState)
    {
        undoManager.clearUndoHistory();
        undoTransactionDepth = 0;
        pendingUndoBefore.reset();
        pendingUndoName.clear();
    }
    if (auto xml = getXmlFromBinary(data, size))
    {
        if (!xml->hasTagName(parameters.state.getType()))
        {
            megadsp::Diagnostics::instance().logRealtime(
                megadsp::DiagnosticEvent::stateRejected,
                diagnosticInstanceId, 1, size);
            return;
        }
        auto restored = juce::ValueTree::fromXml(*xml);
        const auto schemaVersion = static_cast<int>(
            restored.getProperty("schemaVersion", 0));
        if (schemaVersion < 2
            || schemaVersion > megadsp::stateSchemaVersion)
        {
            megadsp::Diagnostics::instance().logRealtime(
                megadsp::DiagnosticEvent::stateRejected,
                diagnosticInstanceId, schemaVersion, size);
            return;
        }
        restored = megadsp::migrateStateToCurrentSchema(restored);
        editorWidth = juce::jlimit(
            megadsp::ui::editorMinimumWidth,
            megadsp::ui::editorStateMaximumWidth,
            static_cast<int>(
                restored.getProperty("editorWidth", 1100)));
        editorHeight = juce::jlimit(
            megadsp::ui::editorMinimumHeight,
            megadsp::ui::editorStateMaximumHeight,
            static_cast<int>(
                restored.getProperty("editorHeight", 720)));
        selectedTab = static_cast<int>(
            restored.getProperty("selectedTab", 0));
        if (!beginTopologyMutation())
            return;
        restoreParameterState(restored);
        restoreSnapshots(restored, schemaVersion);
        finishTopologyMutation(true, true, true);
        setSelectedTab(selectedTab);
        megadsp::Diagnostics::instance().log(
            "state",
            "restored instance=" + juce::String(diagnosticInstanceId)
                + " schema=" + juce::String(schemaVersion));
    }
}

void MegaDSPAudioProcessor::moveSlot(int source, int destination)
{
    if (source == destination)
        return;
    ScopedProcessorUndoTransaction undo(*this, "Reorder module");
    if (!beginTopologyMutation())
        return;
    getRack().moveSlot(source, destination);
    publishSnapshotToParameters(getSelectedSnapshot());
    finishTopologyMutation(false, true, true);
}

void MegaDSPAudioProcessor::clearSlot(int slot)
{
    ScopedProcessorUndoTransaction undo(*this, "Clear module");
    if (!beginTopologyMutation())
        return;
    getRack().clearSlot(slot);
    publishSnapshotToParameters(getSelectedSnapshot());
    finishTopologyMutation(true, true, true);
}

void MegaDSPAudioProcessor::removeSlot(int slot)
{
    if (!juce::isPositiveAndBelow(slot, getRack().activeSlotCount()))
        return;
    ScopedProcessorUndoTransaction undo(*this, "Remove module");
    if (!beginTopologyMutation())
        return;
    getRack().removeSlot(slot);
    publishSnapshotToParameters(getSelectedSnapshot());
    finishTopologyMutation(false, true, true);
}

void MegaDSPAudioProcessor::addModule(megadsp::ModuleType type)
{
    const auto slot = getRack().activeSlotCount();
    if (!juce::isPositiveAndBelow(slot, megadsp::slotCount)
        || type == megadsp::ModuleType::empty
        || megadsp::findModuleDefinition(type) == nullptr)
        return;
    ScopedProcessorUndoTransaction undo(*this, "Add module");
    if (!beginTopologyMutation())
        return;
    getRack().clearSlot(slot);
    const auto defaults = megadsp::moduleDefaults(type);
    for (int control = 0; control < megadsp::controlsPerSlot; ++control)
        if (auto* parameter = parameters.getParameter(
                megadsp::controlParameterId(slot, control)))
            parameter->setValueNotifyingHost(
                parameter->convertTo0to1(defaults[static_cast<size_t>(control)]));
    if (auto* parameter = parameters.getParameter(
            megadsp::slotParameterId(slot, "type")))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(
            parameter->convertTo0to1(static_cast<float>(type)));
        parameter->endChangeGesture();
    }
    finishTopologyMutation(false, true, true);
    selectedTab = slot;
}

void MegaDSPAudioProcessor::resetModuleToDefaults(int slot)
{
    if (!juce::isPositiveAndBelow(slot, megadsp::slotCount))
        return;
    const auto type = getRack().moduleType(slot);
    if (type == megadsp::ModuleType::empty)
        return;

    ScopedProcessorUndoTransaction undo(*this, "Reset module");
    suspendProcessing(true);
    const auto defaults = megadsp::moduleDefaults(type);
    for (int control = 0; control < megadsp::controlsPerSlot; ++control)
    {
        if (auto* parameter = parameters.getParameter(
                megadsp::controlParameterId(slot, control)))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(
                parameter->convertTo0to1(defaults[static_cast<size_t>(control)]));
            parameter->endChangeGesture();
        }
    }
    getRack().reset();
    getRack().visualizationData().slotData(slot).clear();
    suspendProcessing(false);
    refreshLatency();
}

juce::Result MegaDSPAudioProcessor::loadImpulseResponse(
    int slot, const juce::File& file)
{
    return loadImpulseResponse(slot, 0, file);
}

juce::Result MegaDSPAudioProcessor::loadImpulseResponse(
    int slot, int lane, const juce::File& file)
{
    ScopedProcessorUndoTransaction undo(*this, "Load impulse response");
    if (lane == 0
        && getRack().moduleType(slot)
               == megadsp::ModuleType::convolutionReverb)
        return loadConvolutionImpulseResponseBank(
            slot,
            getRack().activeConvolutionImpulseResponseBank(slot),
            file);

    auto expected = false;
    if (!topologyMutationInProgress.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return juce::Result::fail(
            "A topology update is already in progress.");
    const auto finishDataMutation = [this]
    {
        topologyMutationInProgress.store(
            false, std::memory_order_release);
        if (topologyDirty.load(std::memory_order_acquire))
            triggerAsyncUpdate();
    };

    megadsp::EffectRack::PreparedImpulseResponseLoad load;
    auto result =
        getRack().prepareImpulseResponse(slot, lane, file, load);
    if (result.failed())
    {
        megadsp::Diagnostics::instance().log(
            "ir-failed",
            "instance=" + juce::String(diagnosticInstanceId)
                + " slot=" + juce::String(slot)
                + " file=" + file.getFileName()
                + " error=load-rejected");
        finishDataMutation();
        return result;
    }

    suspendProcessing(true);
    const auto committed = getRack().commitImpulseResponse(load);
    suspendProcessing(false);
    if (!committed)
    {
        getRack().cancelImpulseResponse(load);
        finishDataMutation();
        return juce::Result::fail(
            "The impulse response request was superseded.");
    }

    finishDataMutation();
    if (parameters.state.getProperty(
            "impulseResponseRootDirectory").toString().isEmpty())
    {
        parameters.state.setProperty(
            "impulseResponseRootDirectory",
            file.getParentDirectory().getFullPathName(), nullptr);
        parameters.state.setProperty(
            "impulseResponseDirectory",
            file.getParentDirectory().getFullPathName(), nullptr);
        const juce::ScopedLock lock(impulseResponseLibraryLock);
        indexedImpulseResponseRoot.clear();
        const auto persisted =
            persistGlobalImpulseResponseRoot(
                file.getParentDirectory());
        if (persisted.failed())
            megadsp::Diagnostics::instance().log(
                "ir-root-persist-failed",
                persisted.getErrorMessage());
    }
    publishSnapshotToParameters(getSelectedSnapshot());
    updateHostDisplay(
        ChangeDetails{}.withNonParameterStateChanged(true));
    return juce::Result::ok();
}

juce::Result
MegaDSPAudioProcessor::loadConvolutionImpulseResponseBank(
    int slot, int bank, const juce::File& file)
{
    ScopedProcessorUndoTransaction undo(
        *this, "Load convolution impulse response");
    auto expected = false;
    if (!topologyMutationInProgress.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return juce::Result::fail(
            "A topology update is already in progress.");
    const auto finishDataMutation = [this]
    {
        topologyMutationInProgress.store(
            false, std::memory_order_release);
        if (topologyDirty.load(std::memory_order_acquire))
            triggerAsyncUpdate();
    };

    megadsp::EffectRack::PreparedImpulseResponseLoad load;
    auto result = getRack().prepareConvolutionImpulseResponseBank(
        slot, bank, file, load);
    if (result.failed())
    {
        finishDataMutation();
        return result;
    }

    suspendProcessing(true);
    const auto committed =
        getRack().commitConvolutionImpulseResponseBank(load);
    const auto selected = committed
        && getRack().selectConvolutionImpulseResponseBank(slot, bank);
    suspendProcessing(false);
    if (!selected)
    {
        getRack().cancelImpulseResponse(load);
        finishDataMutation();
        return juce::Result::fail(
            "The impulse response request was superseded.");
    }
    finishDataMutation();

    if (parameters.state.getProperty(
            "impulseResponseRootDirectory").toString().isEmpty())
    {
        parameters.state.setProperty(
            "impulseResponseRootDirectory",
            file.getParentDirectory().getFullPathName(), nullptr);
        parameters.state.setProperty(
            "impulseResponseDirectory",
            file.getParentDirectory().getFullPathName(), nullptr);
        const juce::ScopedLock lock(impulseResponseLibraryLock);
        indexedImpulseResponseRoot.clear();
        const auto persisted =
            persistGlobalImpulseResponseRoot(
                file.getParentDirectory());
        if (persisted.failed())
            megadsp::Diagnostics::instance().log(
                "ir-root-persist-failed",
                persisted.getErrorMessage());
    }
    publishSnapshotToParameters(getSelectedSnapshot());
    updateHostDisplay(
        ChangeDetails{}.withNonParameterStateChanged(true));
    return juce::Result::ok();
}

bool MegaDSPAudioProcessor::selectConvolutionImpulseResponseBank(
    int slot, int bank)
{
    ScopedProcessorUndoTransaction undo(*this, "Select impulse bank");
    suspendProcessing(true);
    const auto selected =
        getRack().selectConvolutionImpulseResponseBank(slot, bank);
    suspendProcessing(false);
    if (selected)
    {
        publishSnapshotToParameters(getSelectedSnapshot());
        updateHostDisplay(
            ChangeDetails{}.withNonParameterStateChanged(true));
    }
    return selected;
}

int MegaDSPAudioProcessor::activeConvolutionImpulseResponseBank(
    int slot) const
{
    return getRack().activeConvolutionImpulseResponseBank(slot);
}

juce::String
MegaDSPAudioProcessor::convolutionImpulseResponseBankPath(
    int slot, int bank) const
{
    return getRack().convolutionImpulseResponseBankPath(slot, bank);
}

juce::String
MegaDSPAudioProcessor::convolutionImpulseResponseBankName(
    int slot, int bank) const
{
    return getRack().convolutionImpulseResponseBankName(slot, bank);
}

juce::Result MegaDSPAudioProcessor::setConvolutionWetBufferSamples(
    int slot, int samples)
{
    if (std::find(
            megadsp::ConvolutionReverbModule::
                wetProcessingLatencyChoices.begin(),
            megadsp::ConvolutionReverbModule::
                wetProcessingLatencyChoices.end(),
            samples)
        == megadsp::ConvolutionReverbModule::
               wetProcessingLatencyChoices.end())
        return juce::Result::fail(
            "Unsupported convolution wet buffer size.");

    ScopedProcessorUndoTransaction undo(
        *this, "Change convolution buffer");
    auto expected = false;
    if (!topologyMutationInProgress.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return juce::Result::fail(
            "A topology update is already in progress.");
    const auto finishDataMutation = [this]
    {
        topologyMutationInProgress.store(
            false, std::memory_order_release);
        if (topologyDirty.load(std::memory_order_acquire))
            triggerAsyncUpdate();
    };

    auto& rack = getRack();
    if (!rack.setConvolutionWetBufferSamples(slot, samples))
    {
        finishDataMutation();
        return juce::Result::fail(
            "The selected slot is not Convolution Reverb.");
    }

    for (int bank = 0;
         bank < megadsp::ConvolutionReverbModule::impulseBankCount;
         ++bank)
    {
        const auto path =
            rack.convolutionImpulseResponseBankPath(slot, bank);
        if (path.isEmpty())
            continue;
        megadsp::EffectRack::PreparedImpulseResponseLoad load;
        auto result = rack.prepareConvolutionImpulseResponseBank(
            slot, bank, juce::File(path), load);
        if (result.failed())
        {
            finishDataMutation();
            return result;
        }
        suspendProcessing(true);
        const auto committed =
            rack.commitConvolutionImpulseResponseBank(load);
        suspendProcessing(false);
        if (!committed)
        {
            rack.cancelImpulseResponse(load);
            finishDataMutation();
            return juce::Result::fail(
                "The wet-buffer update was superseded.");
        }
    }

    finishDataMutation();
    publishSnapshotToParameters(getSelectedSnapshot());
    updateHostDisplay(
        ChangeDetails{}.withNonParameterStateChanged(true));
    return juce::Result::ok();
}

int MegaDSPAudioProcessor::convolutionWetBufferSamples(
    int slot) const
{
    return getRack().convolutionWetBufferSamples(slot);
}

juce::Result MegaDSPAudioProcessor::loadBuiltInImpulseResponse(
    int slot, int index)
{
    auto expected = false;
    if (!topologyMutationInProgress.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return juce::Result::fail(
            "A topology update is already in progress.");
    megadsp::EffectRack::PreparedImpulseResponseLoad load;
    auto result =
        getRack().prepareBuiltInImpulseResponse(slot, index, load);
    if (result.failed())
    {
        topologyMutationInProgress.store(false, std::memory_order_release);
        return result;
    }
    suspendProcessing(true);
    const auto committed = getRack().commitImpulseResponse(load);
    suspendProcessing(false);
    if (!committed)
    {
        getRack().cancelImpulseResponse(load);
        topologyMutationInProgress.store(false, std::memory_order_release);
        return juce::Result::fail(
            "The cabinet impulse request was superseded.");
    }
    topologyMutationInProgress.store(false, std::memory_order_release);
    publishSnapshotToParameters(getSelectedSnapshot());
    updateHostDisplay(
        ChangeDetails{}.withNonParameterStateChanged(true));
    return juce::Result::ok();
}

juce::File MegaDSPAudioProcessor::impulseResponseDirectory(int slot) const
{
    const auto root = impulseResponseRootDirectory();
    if (root.isDirectory())
        return root;

    const auto currentPath = impulseResponsePath(slot);
    if (currentPath.isNotEmpty())
    {
        const auto currentDirectory =
            juce::File(currentPath).getParentDirectory();
        if (currentDirectory.isDirectory())
            return currentDirectory;
    }

    const auto persistedDirectory = juce::File(
        parameters.state.getProperty(
            "impulseResponseDirectory").toString());
    if (persistedDirectory.isDirectory())
        return persistedDirectory;
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory);
}

juce::File MegaDSPAudioProcessor::impulseResponseRootDirectory() const
{
    auto path = parameters.state.getProperty(
        "impulseResponseRootDirectory").toString();
    if (path.isEmpty())
        path = parameters.state.getProperty(
            "impulseResponseDirectory").toString();
    auto root = juce::File(path);
    if (root.isDirectory())
        return root;
    root = juce::File(globalImpulseResponseRoot());
    return root.isDirectory() ? root : juce::File();
}

juce::Result MegaDSPAudioProcessor::setImpulseResponseRootDirectory(
    const juce::File& directory)
{
    if (!directory.isDirectory())
        return juce::Result::fail(
            "Choose an existing impulse response directory.");
    parameters.state.setProperty(
        "impulseResponseRootDirectory",
        directory.getFullPathName(), nullptr);
    parameters.state.setProperty(
        "impulseResponseDirectory",
        directory.getFullPathName(), nullptr);
    {
        const juce::ScopedLock lock(impulseResponseLibraryLock);
        indexedImpulseResponseRoot.clear();
        impulseResponseLibrary.clear();
    }
    const auto result = rebuildImpulseResponseLibrary();
    if (result.wasOk())
    {
        const auto persisted =
            persistGlobalImpulseResponseRoot(directory);
        if (persisted.failed())
            return persisted;
    }
    updateHostDisplay(
        ChangeDetails{}.withNonParameterStateChanged(true));
    return result;
}

juce::Result MegaDSPAudioProcessor::rebuildImpulseResponseLibrary()
{
    const auto root = impulseResponseRootDirectory();
    if (!root.isDirectory())
        return juce::Result::fail(
            "Choose an impulse response root directory first.");

    juce::Array<juce::File> files;
    root.findChildFiles(files, juce::File::findFiles, true);
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::vector<ImpulseResponseLibraryEntry> rebuilt;
    rebuilt.reserve(static_cast<size_t>(files.size()));
    for (const auto& file : files)
    {
        const auto extension = file.getFileExtension().toLowerCase();
        if (extension != ".wav" && extension != ".aif"
            && extension != ".aiff" && extension != ".flac")
            continue;
        std::unique_ptr<juce::AudioFormatReader> reader(
            formats.createReaderFor(file));
        if (reader == nullptr || reader->lengthInSamples <= 0
            || reader->sampleRate <= 0.0 || reader->numChannels == 0)
            continue;
        rebuilt.push_back({
            file,
            file.getRelativePathFrom(root).replaceCharacter('\\', '/'),
            reader->sampleRate,
            static_cast<double>(reader->lengthInSamples)
                / reader->sampleRate,
            static_cast<int>(reader->numChannels)
        });
    }
    std::sort(
        rebuilt.begin(), rebuilt.end(),
        [](const auto& left, const auto& right)
        {
            const auto order = left.relativePath.compareIgnoreCase(
                right.relativePath);
            return order != 0
                ? order < 0
                : left.file.getFullPathName()
                      < right.file.getFullPathName();
        });
    {
        const juce::ScopedLock lock(impulseResponseLibraryLock);
        impulseResponseLibrary = std::move(rebuilt);
        indexedImpulseResponseRoot = root.getFullPathName();
    }
    return juce::Result::ok();
}

void MegaDSPAudioProcessor::ensureImpulseResponseLibrary()
{
    const auto root =
        impulseResponseRootDirectory().getFullPathName();
    if (root.isEmpty())
    {
        const juce::ScopedLock lock(impulseResponseLibraryLock);
        indexedImpulseResponseRoot.clear();
        impulseResponseLibrary.clear();
        return;
    }
    {
        const juce::ScopedLock lock(impulseResponseLibraryLock);
        if (root.isNotEmpty() && root == indexedImpulseResponseRoot)
            return;
    }
    rebuildImpulseResponseLibrary();
}

std::vector<ImpulseResponseLibraryEntry>
MegaDSPAudioProcessor::searchImpulseResponses(
    const juce::String& query, int maximumResults)
{
    ensureImpulseResponseLibrary();
    maximumResults = juce::jmax(0, maximumResults);
    if (maximumResults == 0)
        return {};
    const auto tokens = juce::StringArray::fromTokens(
        query.toLowerCase(), " \t\r\n", "\"");
    std::vector<ImpulseResponseLibraryEntry> matches;
    const juce::ScopedLock lock(impulseResponseLibraryLock);
    matches.reserve(juce::jmin(
        static_cast<size_t>(maximumResults),
        impulseResponseLibrary.size()));
    for (const auto& entry : impulseResponseLibrary)
    {
        auto matched = true;
        const auto searchable = entry.relativePath.toLowerCase();
        for (const auto& token : tokens)
        {
            if (token.isEmpty())
                continue;
            if (token == "mono")
                matched = entry.channelCount == 1;
            else if (token == "stereo")
                matched = entry.channelCount >= 2;
            else if (token == "short")
                matched = entry.durationSeconds < 1.0;
            else if (token == "medium")
                matched = entry.durationSeconds >= 1.0
                    && entry.durationSeconds <= 3.0;
            else if (token == "long")
                matched = entry.durationSeconds > 3.0;
            else if (token.startsWith("sr:")
                     || token.startsWith("rate:"))
            {
                const auto requested = token.fromFirstOccurrenceOf(
                    ":", false, false).getDoubleValue();
                matched = requested > 0.0
                    && std::abs(entry.sampleRate - requested) < 1.0;
            }
            else
                matched = searchable.contains(token);
            if (!matched)
                break;
        }
        if (!matched)
            continue;
        matches.push_back(entry);
        if (static_cast<int>(matches.size()) >= maximumResults)
            break;
    }
    return matches;
}

int MegaDSPAudioProcessor::impulseResponseLibrarySize()
{
    ensureImpulseResponseLibrary();
    const juce::ScopedLock lock(impulseResponseLibraryLock);
    return static_cast<int>(impulseResponseLibrary.size());
}

juce::Result MegaDSPAudioProcessor::loadAdjacentImpulseResponse(
    int slot, int direction)
{
    if (direction == 0)
        return juce::Result::fail(
            "Choose either the previous or next impulse response.");

    ensureImpulseResponseLibrary();
    std::vector<ImpulseResponseLibraryEntry> impulseResponses;
    {
        const juce::ScopedLock lock(impulseResponseLibraryLock);
        impulseResponses = impulseResponseLibrary;
    }
    if (impulseResponses.empty())
        return juce::Result::fail(
            "The impulse response library contains no valid WAV, AIFF, or "
            "FLAC files.");

    const auto currentFile = juce::File(impulseResponsePath(slot));
    const auto current = std::find_if(
        impulseResponses.begin(), impulseResponses.end(),
        [&currentFile](const ImpulseResponseLibraryEntry& candidate)
        {
            return candidate.file == currentFile;
        });
    int target = direction > 0
        ? 0 : static_cast<int>(impulseResponses.size()) - 1;
    if (current != impulseResponses.end())
    {
        const auto currentIndex =
            static_cast<int>(std::distance(impulseResponses.begin(), current));
        const auto count = static_cast<int>(impulseResponses.size());
        target = (currentIndex + (direction > 0 ? 1 : count - 1)) % count;
    }
    return loadImpulseResponse(
        slot, impulseResponses[static_cast<size_t>(target)].file);
}

void MegaDSPAudioProcessor::clearImpulseResponse(int slot)
{
    clearImpulseResponse(slot, 0);
}

void MegaDSPAudioProcessor::clearImpulseResponse(int slot, int lane)
{
    ScopedProcessorUndoTransaction undo(*this, "Clear impulse response");
    auto expected = false;
    if (!topologyMutationInProgress.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return;
    suspendProcessing(true);
    const auto result = getRack().clearImpulseResponse(slot, lane);
    suspendProcessing(false);
    topologyMutationInProgress.store(false, std::memory_order_release);
    if (topologyDirty.load(std::memory_order_acquire))
        triggerAsyncUpdate();
    if (result.wasOk())
    {
        publishSnapshotToParameters(getSelectedSnapshot());
        updateHostDisplay(
            ChangeDetails{}.withNonParameterStateChanged(true));
    }
}

juce::File MegaDSPAudioProcessor::namModelRootDirectory() const
{
    auto root = juce::File(parameters.state.getProperty(
        "namModelRootDirectory").toString());
    if (root.isDirectory())
        return root;
    root = juce::File(globalNamModelRoot());
    return root.isDirectory() ? root : juce::File();
}

juce::Result MegaDSPAudioProcessor::setNamModelRootDirectory(
    const juce::File& directory)
{
    if (!directory.isDirectory())
        return juce::Result::fail(
            "Choose an existing NAM model directory.");
    parameters.state.setProperty(
        "namModelRootDirectory",
        directory.getFullPathName(), nullptr);
    {
        const juce::ScopedLock lock(namModelLibraryLock);
        indexedNamModelRoot.clear();
        namModelLibrary.clear();
    }
    const auto result = rebuildNamModelLibrary();
    if (result.failed())
        return result;
    const auto persisted = persistGlobalNamModelRoot(directory);
    if (persisted.failed())
        return persisted;
    updateHostDisplay(
        ChangeDetails{}.withNonParameterStateChanged(true));
    return juce::Result::ok();
}

juce::Result MegaDSPAudioProcessor::rebuildNamModelLibrary()
{
    const auto root = namModelRootDirectory();
    if (!root.isDirectory())
        return juce::Result::fail(
            "Choose a NAM model root directory first.");

    juce::Array<juce::File> files;
    root.findChildFiles(files, juce::File::findFiles, true);
    std::vector<NamModelLibraryEntry> rebuilt;
    rebuilt.reserve(static_cast<size_t>(files.size()));
    for (const auto& file : files)
    {
        if (!file.hasFileExtension("nam"))
            continue;
        if (const auto entry = inspectNamModel(file, root))
            rebuilt.push_back(*entry);
    }
    std::sort(
        rebuilt.begin(), rebuilt.end(),
        [](const auto& left, const auto& right)
        {
            const auto order = left.relativePath.compareIgnoreCase(
                right.relativePath);
            return order != 0
                ? order < 0
                : left.file.getFullPathName()
                      < right.file.getFullPathName();
        });
    {
        const juce::ScopedLock lock(namModelLibraryLock);
        namModelLibrary = std::move(rebuilt);
        indexedNamModelRoot = root.getFullPathName();
    }
    return juce::Result::ok();
}

void MegaDSPAudioProcessor::ensureNamModelLibrary()
{
    const auto root = namModelRootDirectory().getFullPathName();
    if (root.isEmpty())
    {
        const juce::ScopedLock lock(namModelLibraryLock);
        indexedNamModelRoot.clear();
        namModelLibrary.clear();
        return;
    }
    {
        const juce::ScopedLock lock(namModelLibraryLock);
        if (root == indexedNamModelRoot)
            return;
    }
    rebuildNamModelLibrary();
}

std::vector<NamModelLibraryEntry>
MegaDSPAudioProcessor::searchNamModels(
    const juce::String& query, int maximumResults)
{
    ensureNamModelLibrary();
    maximumResults = juce::jmax(0, maximumResults);
    if (maximumResults == 0)
        return {};
    const auto tokens = juce::StringArray::fromTokens(
        query.toLowerCase(), " \t\r\n", "\"");
    std::vector<NamModelLibraryEntry> matches;
    const juce::ScopedLock lock(namModelLibraryLock);
    matches.reserve(juce::jmin(
        static_cast<size_t>(maximumResults),
        namModelLibrary.size()));
    for (const auto& entry : namModelLibrary)
    {
        const auto searchable =
            (entry.relativePath + " "
             + entry.file.getFileNameWithoutExtension() + " "
             + entry.metadataTitle + " " + entry.architecture + " "
             + entry.captureDescription + " " + entry.gearType)
                .toLowerCase();
        auto matched = true;
        for (const auto& token : tokens)
        {
            if (token.isEmpty())
                continue;
            if (token == "a1")
                matched = !entry.isA2;
            else if (token == "a2")
                matched = entry.isA2;
            else if (token == "cab")
                matched = entry.cabIncluded == 1;
            else if (token == "nocab")
                matched = entry.cabIncluded == 0;
            else if (token == "amp")
                matched = entry.gearType == "amp"
                    || (entry.gearType.isEmpty()
                        && entry.cabIncluded == 0);
            else if (token == "rig" || token == "full-rig")
                matched = entry.gearType == "full-rig";
            else if (token == "pedal")
                matched = entry.gearType.contains("pedal");
            else if (token.startsWith("sr:")
                     || token.startsWith("rate:"))
            {
                const auto requested = token.fromFirstOccurrenceOf(
                    ":", false, false).getDoubleValue();
                matched = requested > 0.0
                    && std::abs(entry.sampleRate - requested) < 1.0;
            }
            else
                matched = searchable.contains(token);
            if (!matched)
                break;
        }
        if (!matched)
            continue;
        matches.push_back(entry);
        if (static_cast<int>(matches.size()) >= maximumResults)
            break;
    }
    return matches;
}

int MegaDSPAudioProcessor::namModelLibrarySize()
{
    ensureNamModelLibrary();
    const juce::ScopedLock lock(namModelLibraryLock);
    return static_cast<int>(namModelLibrary.size());
}

juce::Result MegaDSPAudioProcessor::loadAdjacentNamModel(
    int slot, int direction)
{
    if (direction == 0)
        return juce::Result::fail(
            "Choose either the previous or next NAM model.");

    ensureNamModelLibrary();
    std::vector<NamModelLibraryEntry> models;
    {
        const juce::ScopedLock lock(namModelLibraryLock);
        models = namModelLibrary;
    }
    if (models.empty())
        return juce::Result::fail(
            "The NAM model library contains no valid .nam files.");

    const auto currentFile = juce::File(namModelPath(slot));
    const auto current = std::find_if(
        models.begin(), models.end(),
        [&currentFile](const NamModelLibraryEntry& candidate)
        {
            return candidate.file == currentFile;
        });
    int target =
        direction > 0 ? 0 : static_cast<int>(models.size()) - 1;
    if (current != models.end())
    {
        const auto currentIndex =
            static_cast<int>(std::distance(models.begin(), current));
        const auto count = static_cast<int>(models.size());
        target = (currentIndex + (direction > 0 ? 1 : count - 1)) % count;
    }
    return loadNamModel(slot, models[static_cast<size_t>(target)].file);
}

juce::Result MegaDSPAudioProcessor::loadAdjacentNamModelAsync(
    int slot, int direction, NamModelLoadCallback callback)
{
    if (direction == 0)
        return juce::Result::fail(
            "Choose either the previous or next NAM model.");

    ensureNamModelLibrary();
    juce::File targetFile;
    {
        const juce::ScopedLock lock(namModelLibraryLock);
        if (namModelLibrary.empty())
            return juce::Result::fail(
                "The NAM model library contains no valid .nam files.");
        const auto currentFile = juce::File(namModelPath(slot));
        const auto current = std::find_if(
            namModelLibrary.begin(), namModelLibrary.end(),
            [&currentFile](const NamModelLibraryEntry& candidate)
            {
                return candidate.file == currentFile;
            });
        auto target = direction > 0
            ? 0 : static_cast<int>(namModelLibrary.size()) - 1;
        if (current != namModelLibrary.end())
        {
            const auto currentIndex = static_cast<int>(
                std::distance(namModelLibrary.begin(), current));
            const auto count = static_cast<int>(namModelLibrary.size());
            target =
                (currentIndex + (direction > 0 ? 1 : count - 1)) % count;
        }
        targetFile =
            namModelLibrary[static_cast<size_t>(target)].file;
    }
    return loadNamModelAsync(slot, targetFile, std::move(callback));
}

juce::Result MegaDSPAudioProcessor::loadNamModelAsync(
    int slot, const juce::File& file, NamModelLoadCallback callback)
{
    auto expected = false;
    if (!topologyMutationInProgress.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return juce::Result::fail(
            "A model or topology update is already in progress.");

    const auto snapshot = getSelectedSnapshot();
    auto* rack = snapshotRacks[static_cast<size_t>(snapshot)].get();
    namModelLoadPool.addJob(std::function<void()> {
        [this, rack, snapshot, slot, file,
         completionCallback = std::move(callback)]() mutable
        {
            auto completed = std::make_unique<PendingNamModelLoad>();
            completed->rack = rack;
            completed->snapshot = snapshot;
            completed->callback = std::move(completionCallback);
            completed->result =
                rack->prepareNamModel(slot, file, completed->load);
            {
                const juce::ScopedLock lock(pendingNamModelLoadLock);
                pendingNamModelLoad = std::move(completed);
            }
        } });
    return juce::Result::ok();
}

void MegaDSPAudioProcessor::finishPendingNamModelLoad()
{
    std::unique_ptr<PendingNamModelLoad> completed;
    {
        const juce::ScopedLock lock(pendingNamModelLoadLock);
        completed = std::move(pendingNamModelLoad);
    }
    if (completed == nullptr)
        return;

    auto result = completed->result;
    if (result.wasOk())
    {
        ScopedProcessorUndoTransaction undo(*this, "Load NAM model");
        suspendProcessing(true);
        const auto committed =
            completed->rack->commitNamModel(completed->load);
        refreshLatency();
        suspendProcessing(false);
        if (!committed)
        {
            completed->rack->cancelNamModel(completed->load);
            result = juce::Result::fail(
                "NAM model commit superseded.");
        }
        else
        {
            if (completed->snapshot == getSelectedSnapshot())
                publishSnapshotToParameters(completed->snapshot);
            updateHostDisplay(
                ChangeDetails{}.withNonParameterStateChanged(true));
        }
    }
    topologyMutationInProgress.store(false, std::memory_order_release);
    if (topologyDirty.load(std::memory_order_acquire))
        triggerAsyncUpdate();
    if (completed->callback)
        completed->callback(result);
}

juce::Result MegaDSPAudioProcessor::loadNamModel(
    int slot, const juce::File& file)
{
    ScopedProcessorUndoTransaction undo(*this, "Load NAM model");
    auto expected = false;
    if (!topologyMutationInProgress.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return juce::Result::fail(
            "A topology update is already in progress.");

    const auto finishDataMutation = [this]
    {
        topologyMutationInProgress.store(
            false, std::memory_order_release);
        if (topologyDirty.load(std::memory_order_acquire))
            triggerAsyncUpdate();
    };

    megadsp::EffectRack::PreparedNamModelLoad load;
    auto result = getRack().prepareNamModel(slot, file, load);
    if (result.failed())
    {
        finishDataMutation();
        return result;
    }

    suspendProcessing(true);
    const auto committed = getRack().commitNamModel(load);
    refreshLatency();
    suspendProcessing(false);
    if (!committed)
    {
        getRack().cancelNamModel(load);
        finishDataMutation();
        return juce::Result::fail("NAM model commit superseded.");
    }

    finishDataMutation();
    publishSnapshotToParameters(getSelectedSnapshot());
    updateHostDisplay(
        ChangeDetails{}.withNonParameterStateChanged(true));
    return juce::Result::ok();
}

juce::Result MegaDSPAudioProcessor::clearNamModel(int slot)
{
    ScopedProcessorUndoTransaction undo(*this, "Clear NAM model");
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

void MegaDSPAudioProcessor::topologyChanged()
{
    if (!beginTopologyMutation())
        return;
    finishTopologyMutation(true, false, false);
}

void MegaDSPAudioProcessor::loadFactoryPreset(int presetIndex)
{
    ScopedProcessorUndoTransaction undo(*this, "Load factory preset");
    struct PresetSlot
    {
        megadsp::ModuleType type = megadsp::ModuleType::empty;
        float bypass = 0.0f;
        std::array<float, megadsp::controlsPerSlot> controls {};
    };

    std::array<PresetSlot, megadsp::slotCount> finalState;
    for (auto& slot : finalState)
        slot.controls.fill(0.5f);

    auto setSlot = [&finalState](int slot, megadsp::ModuleType type,
                       std::initializer_list<std::pair<int, float>> values)
    {
        if (!juce::isPositiveAndBelow(slot, megadsp::slotCount))
            return;
        auto& destination = finalState[static_cast<size_t>(slot)];
        destination.type = type;
        destination.bypass = 0.0f;
        for (const auto& [control, value] : values)
            if (juce::isPositiveAndBelow(
                    control, megadsp::controlsPerSlot))
                destination.controls[static_cast<size_t>(control)] = value;
    };

    if (presetIndex == 1)
    {
        setSlot(0, megadsp::ModuleType::equalizer,
                { { 1, 0.46f }, { 4, 0.58f }, { 7, 0.54f } });
        setSlot(1, megadsp::ModuleType::compressor,
                { { 0, 0.64f }, { 1, 0.38f }, { 2, 0.25f },
                  { 3, 0.45f }, { 5, 0.20f }, { 6, 0.78f },
                  { 7, 0.0f }, { 8, 0.0f } });
        setSlot(2, megadsp::ModuleType::limiter,
                { { 0, 0.72f }, { 1, 0.92f }, { 2, 0.35f },
                  { 3, 0.35f }, { 4, 0.0f } });
    }
    else if (presetIndex == 2)
    {
        setSlot(0, megadsp::ModuleType::saturator,
                { { 0, 0.42f }, { 1, 0.70f }, { 3, 0.38f }, { 4, 0.72f } });
        setSlot(1, megadsp::ModuleType::compressor,
                { { 0, 0.48f }, { 1, 0.72f }, { 2, 0.08f },
                  { 3, 0.28f }, { 4, 0.25f }, { 6, 0.90f },
                  { 7, 0.0f }, { 8, 0.0f } });
        setSlot(2, megadsp::ModuleType::limiter,
                { { 0, 0.68f }, { 1, 0.90f }, { 2, 0.28f },
                  { 3, 0.45f }, { 4, 0.0f } });
    }
    else if (presetIndex == 3)
    {
        setSlot(0, megadsp::ModuleType::equalizer,
                { { 1, 0.45f }, { 4, 0.52f }, { 7, 0.56f } });
        setSlot(1, megadsp::ModuleType::delay,
                { { 1, 0.42f }, { 2, 0.36f }, { 3, 0.72f },
                  { 4, 1.0f }, { 5, 1.0f }, { 6, 0.43f },
                  { 7, 0.28f }, { 8, 0.18f } });
    }
    else if (presetIndex == 4)
    {
        setSlot(0, megadsp::ModuleType::equalizer,
                { { 1, 0.47f }, { 4, 0.52f }, { 7, 0.54f } });
        setSlot(1, megadsp::ModuleType::algorithmicReverb,
                { { 0, 0.72f }, { 1, 0.62f }, { 2, 0.48f },
                  { 3, 0.38f }, { 4, 0.10f }, { 5, 0.10f },
                  { 6, 0.78f }, { 7, 0.28f }, { 8, 0.72f },
                  { 9, 0.55f }, { 10, 0.34f }, { 11, 0.70f } });
    }

    if (!beginTopologyMutation())
        return;
    getRack().clearModuleDataState();
    const auto setPlain = [this](const juce::String& id, float value)
    {
        if (auto* parameter = parameters.getParameter(id))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(
                parameter->convertTo0to1(value));
            parameter->endChangeGesture();
        }
    };
    for (int slot = 0; slot < megadsp::slotCount; ++slot)
    {
        const auto& source = finalState[static_cast<size_t>(slot)];
        setPlain(
            megadsp::slotParameterId(slot, "type"),
            static_cast<float>(source.type));
        setPlain(megadsp::slotParameterId(slot, "bypass"), source.bypass);
        for (int control = 0; control < megadsp::controlsPerSlot; ++control)
            setPlain(
                megadsp::controlParameterId(slot, control),
                source.controls[static_cast<size_t>(control)]);
    }
    finishTopologyMutation(true, true, true);
}

juce::Result MegaDSPAudioProcessor::savePreset(const juce::File& file)
{
    auto state = parameters.copyState();
    state.setProperty("schemaVersion", megadsp::stateSchemaVersion, nullptr);
    state.setProperty("selectedTab", selectedTab, nullptr);
    serializeSnapshots(state);
    auto xml = state.createXml();
    if (xml == nullptr)
        return juce::Result::fail("Could not serialize preset state.");

    const auto temporary = file.getSiblingFile(file.getFileName() + ".tmp");
    if (!temporary.replaceWithText(xml->toString()))
        return juce::Result::fail("Could not write preset.");
    if (!temporary.moveFileTo(file))
    {
        temporary.deleteFile();
        return juce::Result::fail("Could not finalize preset.");
    }
    return juce::Result::ok();
}

juce::Result MegaDSPAudioProcessor::loadPreset(const juce::File& file)
{
    if (!file.existsAsFile())
        return juce::Result::fail("Preset does not exist.");
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || !xml->hasTagName(parameters.state.getType()))
        return juce::Result::fail("Preset is not a valid megaDSP preset.");
    auto state = juce::ValueTree::fromXml(*xml);
    const auto schemaVersion = static_cast<int>(
        state.getProperty("schemaVersion", 0));
    if (schemaVersion < 2
        || schemaVersion > megadsp::stateSchemaVersion)
        return juce::Result::fail("Preset uses an incompatible megaDSP state format.");
    ScopedProcessorUndoTransaction undo(*this, "Load preset");
    state = megadsp::migrateStateToCurrentSchema(state);
    if (!beginTopologyMutation())
        return juce::Result::fail(
            "Another topology update is already in progress.");
    selectedTab = static_cast<int>(state.getProperty("selectedTab", 0));
    restoreParameterState(state);
    restoreSnapshots(state, schemaVersion);
    finishTopologyMutation(true, true, true);
    setSelectedTab(selectedTab);
    return juce::Result::ok();
}

void MegaDSPAudioProcessor::refreshLatency()
{
    const auto latency = getAudioRack().latencySamples();
    pendingLatency.store(latency);
    if (!liveSafeSnapshots.load(std::memory_order_acquire)
        || !audioStreamActive.load(std::memory_order_acquire))
    {
        latencyDirty.store(false, std::memory_order_release);
        setLatencySamples(latency);
        updateHostDisplay(juce::AudioProcessorListener::ChangeDetails()
                              .withLatencyChanged(true));
        return;
    }
    latencyDirty.store(true, std::memory_order_release);
}

void MegaDSPAudioProcessor::restoreParameterState(const juce::ValueTree& state)
{
    parameters.replaceState(state);
    const auto seeded =
        seedGlobalImpulseResponseRoot(parameters.state);
    if (seeded.failed())
        megadsp::Diagnostics::instance().log(
            "ir-root-persist-failed",
            seeded.getErrorMessage());
    inheritGlobalImpulseResponseRoot(parameters.state);
    const auto namSeeded =
        seedGlobalNamModelRoot(parameters.state);
    if (namSeeded.failed())
        megadsp::Diagnostics::instance().log(
            "nam-root-persist-failed",
            namSeeded.getErrorMessage());
    inheritGlobalNamModelRoot(parameters.state);
    liveSafeSnapshots.store(
        static_cast<bool>(
            parameters.state.getProperty(
                "liveSafeSnapshots", false)),
        std::memory_order_release);
    parameters.state.setProperty(
        "instanceName",
        megadsp::ui::normalizeInstanceName(
            parameters.state.getProperty("instanceName").toString()),
        nullptr);
    parameters.state.setProperty(
        "backgroundTheme",
        megadsp::ui::safeBackgroundThemeIndex(static_cast<int>(
            parameters.state.getProperty("backgroundTheme", 0))),
        nullptr);
    for (const auto child : state)
    {
        const auto parameterId = child.getProperty("id").toString();
        if (auto* parameter = parameters.getParameter(parameterId))
        {
            const auto plainValue = static_cast<float>(
                static_cast<double>(child.getProperty("value")));
            const auto normalizedValue =
                dynamic_cast<juce::AudioParameterBool*>(parameter) != nullptr
                    ? plainValue
                    : parameter->convertTo0to1(plainValue);
            parameter->setValueNotifyingHost(normalizedValue);
        }
    }
}

void MegaDSPAudioProcessor::timerCallback()
{
    finishPendingNamModelLoad();

    for (int slot = 0; slot < megadsp::slotCount; ++slot)
    {
        const auto lanes =
            pendingWormholeReloads[static_cast<size_t>(slot)].exchange(
                0, std::memory_order_acq_rel);
        for (int lane = 0; lane < 2; ++lane)
        {
            const auto bit = static_cast<std::uint8_t>(1u << lane);
            if ((lanes & bit) == 0)
                continue;
            const auto path = impulseResponsePath(slot, lane);
            if (path.isEmpty())
                continue;
            const auto result =
                loadImpulseResponse(slot, lane, juce::File(path));
            if (result.failed()
                && result.getErrorMessage().containsIgnoreCase("in progress"))
                pendingWormholeReloads[static_cast<size_t>(slot)].fetch_or(
                    bit, std::memory_order_release);
        }

    }

    for (int slot = 0; slot < megadsp::slotCount; ++slot)
    {
        const auto index =
            pendingBuiltInCabinetLoads[static_cast<size_t>(slot)].exchange(
                -1, std::memory_order_acq_rel);
        if (index < 0)
            continue;
        const auto result = loadBuiltInImpulseResponse(slot, index);
        if (result.failed()
            && result.getErrorMessage().containsIgnoreCase("in progress"))
            pendingBuiltInCabinetLoads[static_cast<size_t>(slot)].store(
                index, std::memory_order_release);
    }

    const auto callbackGeneration =
        audioCallbackGeneration.load(std::memory_order_acquire);
    if (callbackGeneration != observedAudioCallbackGeneration)
    {
        observedAudioCallbackGeneration = callbackGeneration;
        audioIdlePollCount = 0;
        audioStreamActive.store(true, std::memory_order_release);
    }
    else if (audioStreamActive.load(std::memory_order_acquire))
    {
        const auto blockDurationPolls = juce::roundToInt(
            30.0 * static_cast<double>(
                       lastAudioBlockSamples.load(std::memory_order_acquire))
            / juce::jmax(1.0, preparedSampleRate));
        if (++audioIdlePollCount >= juce::jmax(5, blockDurationPolls + 3))
            audioStreamActive.store(false, std::memory_order_release);
    }

    if (snapshotUiDirty.exchange(false, std::memory_order_acq_rel))
    {
        const auto selected = getSelectedSnapshot();
        publishSnapshotToParameters(selected, false);
        selectedTab = juce::jlimit(
            0, juce::jmax(
                   0, getRack().activeSlotCount() - 1),
            selectedTab);
        topologyGeneration.fetch_add(1, std::memory_order_release);
        refreshLatency();
        updateHostDisplay(
            ChangeDetails{}.withNonParameterStateChanged(true));
    }
    if (topologyDirty.load(std::memory_order_acquire)
        && !topologyMutationInProgress.load(std::memory_order_acquire))
        handleAsyncUpdate();
    if ((!liveSafeSnapshots.load(std::memory_order_acquire)
         || !audioStreamActive.load(std::memory_order_acquire))
        && latencyDirty.exchange(false, std::memory_order_acq_rel))
    {
        setLatencySamples(pendingLatency.load());
        updateHostDisplay(juce::AudioProcessorListener::ChangeDetails()
                              .withLatencyChanged(true));
    }
}

#if defined(MEGADSP_TESTS)
void MegaDSPAudioProcessor::finishNamModelLoadForTesting()
{
    const auto deadline =
        juce::Time::getMillisecondCounterHiRes() + 30000.0;
    while (namModelLoadPool.getNumJobs() > 0
           && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::Thread::sleep(1);
    finishPendingNamModelLoad();
}
#endif


juce::AudioProcessorEditor* MegaDSPAudioProcessor::createEditor()
{
    return new MegaDSPAudioProcessorEditor(*this, editorWidth, editorHeight);
}

void MegaDSPAudioProcessor::rememberEditorSize(int width, int height)
{
    editorWidth = juce::jlimit(
        megadsp::ui::editorMinimumWidth,
        megadsp::ui::editorStateMaximumWidth, width);
    editorHeight = juce::jlimit(
        megadsp::ui::editorMinimumHeight,
        megadsp::ui::editorStateMaximumHeight, height);
}

juce::String MegaDSPAudioProcessor::globalWidgetSizeId() const
{
    return globalWidgetSizePreference();
}

juce::Result MegaDSPAudioProcessor::setGlobalWidgetSizeId(
    const juce::String& identifier)
{
    return persistGlobalWidgetSize(identifier);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MegaDSPAudioProcessor();
}
