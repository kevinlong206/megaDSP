#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ui/GraphStyle.h"
#include "ui/GuiLayout.h"

MegaDSPAudioProcessor::MegaDSPAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
      parameters(*this, nullptr, "megaDSPState", megadsp::createParameterLayout()),
      rack(parameters)
{
    for (int slot = 0; slot < megadsp::slotCount; ++slot)
        parameters.addParameterListener(
            megadsp::slotParameterId(slot, "type"), this);
    startTimerHz(30);
}

MegaDSPAudioProcessor::~MegaDSPAudioProcessor()
{
    stopTimer();
    for (int slot = 0; slot < megadsp::slotCount; ++slot)
        parameters.removeParameterListener(
            megadsp::slotParameterId(slot, "type"), this);
    cancelPendingUpdate();
}

void MegaDSPAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    rack.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    const auto bypassCapacity = static_cast<size_t>(
        juce::jmax(1, juce::roundToInt(sampleRate * 0.02) * megadsp::slotCount));
    for (auto& channel : hostBypassDelay)
        channel.assign(bypassCapacity, 0.0f);
    hostBypassWritePosition = 0;
    lastHostBypassLatency = 0;
    pendingLatency.store(rack.latencySamples());
    setLatencySamples(pendingLatency.load());
}

void MegaDSPAudioProcessor::releaseResources()
{
    rack.reset();
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
                                         juce::MidiBuffer&)
{
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

    rack.process(main, sidechainPointer, currentBpm());
    const auto latency = rack.latencySamples();
    if (latency != pendingLatency.exchange(latency))
        latencyDirty.store(true, std::memory_order_release);
}

void MegaDSPAudioProcessor::processBlockBypassed(
    juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
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
    return rack.tailSeconds();
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
    parameters.state.setProperty("instanceName", normalized, nullptr);
    updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
}

void MegaDSPAudioProcessor::parameterChanged(
    const juce::String&, float)
{
    topologyRequestGeneration.fetch_add(
        1, std::memory_order_release);
    topologyDirty.store(true, std::memory_order_release);
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
    const auto compacted = compact && rack.compactSlots();
    const auto modulesChanged = rack.synchronizeModules(false);
    if (resetRack || compacted || modulesChanged)
        rack.reset();
    selectedTab = juce::jlimit(
        0, juce::jmax(0, rack.activeSlotCount() - 1), selectedTab);
    suspendProcessing(false);
    refreshLatency();
    if (publishAlways || compacted || modulesChanged)
        topologyGeneration.fetch_add(1, std::memory_order_release);
    restorePendingImpulseResponses();
    topologyMutationInProgress.store(false, std::memory_order_release);
    if (topologyDirty.load(std::memory_order_acquire))
        triggerAsyncUpdate();
}

void MegaDSPAudioProcessor::restorePendingImpulseResponses()
{
    for (int slot = 0; slot < megadsp::slotCount; ++slot)
    {
        const auto path = rack.pendingImpulseResponsePath(slot);
        if (path.isEmpty())
            continue;

        megadsp::EffectRack::PreparedImpulseResponseLoad load;
        const auto result = rack.prepareImpulseResponse(
            slot, juce::File(path), load);
        if (result.failed())
        {
            rack.discardPendingImpulseResponse(slot, path);
            continue;
        }

        suspendProcessing(true);
        const auto committed = rack.commitImpulseResponse(load);
        suspendProcessing(false);
        if (!committed)
            rack.cancelImpulseResponse(load);
    }
}

void MegaDSPAudioProcessor::handleAsyncUpdate()
{
    if (!topologyDirty.exchange(false, std::memory_order_acq_rel))
        return;
    const auto requestGeneration =
        topologyRequestGeneration.load(std::memory_order_acquire);
    if (!beginTopologyMutation())
        return;
    finishTopologyMutation(true, false, false);
    if (topologyRequestGeneration.load(std::memory_order_acquire)
        != requestGeneration)
    {
        topologyDirty.store(true, std::memory_order_release);
        triggerAsyncUpdate();
    }
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
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destination);
}

void MegaDSPAudioProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
    {
        if (!xml->hasTagName(parameters.state.getType()))
            return;
        auto restored = juce::ValueTree::fromXml(*xml);
        const auto schemaVersion = static_cast<int>(
            restored.getProperty("schemaVersion", 0));
        if (schemaVersion < 2
            || schemaVersion > megadsp::stateSchemaVersion)
            return;
        restored = megadsp::migrateStateToCurrentSchema(restored);
        editorWidth = juce::jlimit(
            megadsp::ui::editorMinimumWidth,
            megadsp::ui::editorMaximumWidth,
            static_cast<int>(
                restored.getProperty("editorWidth", 1100)));
        editorHeight = juce::jlimit(
            megadsp::ui::editorMinimumHeight,
            megadsp::ui::editorMaximumHeight,
            static_cast<int>(
                restored.getProperty("editorHeight", 720)));
        selectedTab = static_cast<int>(
            restored.getProperty("selectedTab", 0));
        if (!beginTopologyMutation())
            return;
        restoreParameterState(restored);
        finishTopologyMutation(true, true, true);
        setSelectedTab(selectedTab);
    }
}

void MegaDSPAudioProcessor::moveSlot(int source, int destination)
{
    if (!beginTopologyMutation())
        return;
    rack.moveSlot(source, destination);
    finishTopologyMutation(false, true, true);
}

void MegaDSPAudioProcessor::clearSlot(int slot)
{
    if (!beginTopologyMutation())
        return;
    rack.clearSlot(slot);
    finishTopologyMutation(true, true, true);
}

void MegaDSPAudioProcessor::removeSlot(int slot)
{
    if (!beginTopologyMutation())
        return;
    rack.removeSlot(slot);
    finishTopologyMutation(false, true, true);
}

void MegaDSPAudioProcessor::addModule(megadsp::ModuleType type)
{
    const auto slot = rack.activeSlotCount();
    if (!juce::isPositiveAndBelow(slot, megadsp::slotCount)
        || type == megadsp::ModuleType::empty
        || megadsp::findModuleDefinition(type) == nullptr)
        return;
    if (!beginTopologyMutation())
        return;
    rack.clearSlot(slot);
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
    const auto type = rack.moduleType(slot);
    if (type == megadsp::ModuleType::empty)
        return;

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
    rack.reset();
    suspendProcessing(false);
    refreshLatency();
}

juce::Result MegaDSPAudioProcessor::loadImpulseResponse(
    int slot, const juce::File& file)
{
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
    auto result = rack.prepareImpulseResponse(slot, file, load);
    if (result.failed())
    {
        finishDataMutation();
        return result;
    }

    suspendProcessing(true);
    const auto committed = rack.commitImpulseResponse(load);
    suspendProcessing(false);
    if (!committed)
    {
        rack.cancelImpulseResponse(load);
        finishDataMutation();
        return juce::Result::fail(
            "The impulse response request was superseded.");
    }

    finishDataMutation();
    updateHostDisplay(
        ChangeDetails{}.withNonParameterStateChanged(true));
    return juce::Result::ok();
}

void MegaDSPAudioProcessor::clearImpulseResponse(int slot)
{
    auto expected = false;
    if (!topologyMutationInProgress.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return;
    const auto result = rack.clearImpulseResponse(slot);
    topologyMutationInProgress.store(false, std::memory_order_release);
    if (topologyDirty.load(std::memory_order_acquire))
        triggerAsyncUpdate();
    if (result.wasOk())
        updateHostDisplay(
            ChangeDetails{}.withNonParameterStateChanged(true));
}

void MegaDSPAudioProcessor::topologyChanged()
{
    if (!beginTopologyMutation())
        return;
    finishTopologyMutation(true, false, false);
}

void MegaDSPAudioProcessor::loadFactoryPreset(int presetIndex)
{
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
    rack.clearModuleDataState();
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
    state = megadsp::migrateStateToCurrentSchema(state);
    if (!beginTopologyMutation())
        return juce::Result::fail(
            "Another topology update is already in progress.");
    selectedTab = static_cast<int>(state.getProperty("selectedTab", 0));
    restoreParameterState(state);
    finishTopologyMutation(true, true, true);
    setSelectedTab(selectedTab);
    return juce::Result::ok();
}

void MegaDSPAudioProcessor::refreshLatency()
{
    pendingLatency.store(rack.latencySamples());
    latencyDirty.store(true, std::memory_order_release);
}

void MegaDSPAudioProcessor::restoreParameterState(const juce::ValueTree& state)
{
    parameters.replaceState(state);
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
    if (topologyDirty.load(std::memory_order_acquire)
        && !topologyMutationInProgress.load(std::memory_order_acquire))
        handleAsyncUpdate();
    if (latencyDirty.exchange(false, std::memory_order_acq_rel))
    {
        setLatencySamples(pendingLatency.load());
        updateHostDisplay(juce::AudioProcessorListener::ChangeDetails()
                              .withLatencyChanged(true));
    }
}


juce::AudioProcessorEditor* MegaDSPAudioProcessor::createEditor()
{
    return new MegaDSPAudioProcessorEditor(*this, editorWidth, editorHeight);
}

void MegaDSPAudioProcessor::rememberEditorSize(int width, int height)
{
    editorWidth = juce::jlimit(
        megadsp::ui::editorMinimumWidth,
        megadsp::ui::editorMaximumWidth, width);
    editorHeight = juce::jlimit(
        megadsp::ui::editorMinimumHeight,
        megadsp::ui::editorMaximumHeight, height);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MegaDSPAudioProcessor();
}
