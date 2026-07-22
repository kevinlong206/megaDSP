#include "EffectRack.h"

namespace megadsp
{
namespace
{
void retainPeak(std::atomic<float>& destination, float value)
{
    auto current = destination.load(std::memory_order_relaxed);
    while (value > current
           && !destination.compare_exchange_weak(
               current, value, std::memory_order_relaxed))
    {
    }
}
} // namespace

ControlValues EffectRack::Slot::values() const
{
    ControlValues result {};
    for (int index = 0; index < controlsPerSlot; ++index)
        result[static_cast<size_t>(index)] = controls[static_cast<size_t>(index)]->load();
    return result;
}

EffectRack::EffectRack(juce::AudioProcessorValueTreeState& state)
    : parameters(state)
{
    for (int slot = 0; slot < slotCount; ++slot)
    {
        auto& current = slots[static_cast<size_t>(slot)];
        current.type = parameters.getRawParameterValue(slotParameterId(slot, "type"));
        current.bypass = parameters.getRawParameterValue(slotParameterId(slot, "bypass"));
        for (int control = 0; control < controlsPerSlot; ++control)
            current.controls[static_cast<size_t>(control)] =
                parameters.getRawParameterValue(controlParameterId(slot, control));
    }
    inputGain = parameters.getRawParameterValue("global.input");
    outputGain = parameters.getRawParameterValue("global.output");
}

ModuleType EffectRack::requestedType(const Slot& slot) const
{
    if (slot.type == nullptr)
        return ModuleType::empty;

    const auto type =
        static_cast<ModuleType>(juce::roundToInt(slot.type->load()));
    if (type == ModuleType::empty)
        return type;
    const auto* definition = findModuleDefinition(type);
    return definition != nullptr && definition->factory != nullptr
               ? type : ModuleType::empty;
}

DspModule* EffectRack::selectedModule(Slot& slot)
{
    if (requestedType(slot) != slot.activeType)
        return nullptr;
    jassert((slot.activeType == ModuleType::empty)
            == (slot.activeModule == nullptr));
    return slot.activeModule.get();
}

const DspModule* EffectRack::selectedModule(const Slot& slot) const
{
    if (requestedType(slot) != slot.activeType)
        return nullptr;
    jassert((slot.activeType == ModuleType::empty)
            == (slot.activeModule == nullptr));
    return slot.activeModule.get();
}

void EffectRack::rebuildSlot(Slot& slot, ModuleType type)
{
    slot.activeModule.reset();
    slot.activeType = ModuleType::empty;
    slot.synchronizedImpulsePath.clear();
    slot.observedImpulsePath.clear();
    slot.impulseRestorePending = false;
    slot.dryBuffer.setSize(0, 0);
    for (auto& channel : slot.latencyBuffer)
        std::vector<float>().swap(channel);
    slot.latencyWritePosition = 0;
    slot.previousLatency = 0;

    if (type == ModuleType::empty)
        return;

    auto module = createDspModule(type);
    if (module == nullptr)
        return;

    if (hasPrepareState)
    {
        module->prepare(preparedSpec);
        module->reset();
        slot.dryBuffer.setSize(
            static_cast<int>(preparedSpec.numChannels),
            static_cast<int>(preparedSpec.maximumBlockSize),
            false, true, true);
        const auto latencyCapacity =
            static_cast<size_t>(juce::jmax(0, module->latencySamples()));
        if (latencyCapacity > 0)
            for (auto& channel : slot.latencyBuffer)
                channel.assign(latencyCapacity, 0.0f);
        slot.wet.reset(preparedSpec.sampleRate, 0.01);
        slot.wet.setCurrentAndTargetValue(
            slot.bypass->load() >= 0.5f ? 0.0f : 1.0f);
    }

    slot.previousLatency = module->latencySamples();
    slot.activeType = type;
    slot.activeModule = std::move(module);
    jassert(slot.activeModule != nullptr);
}

void EffectRack::synchronizeModuleDataState(int slot)
{
    if (!juce::isPositiveAndBelow(slot, slotCount))
        return;
    auto& rackSlot = slots[static_cast<size_t>(slot)];
    auto* module = selectedModule(rackSlot);
    if (module == nullptr)
    {
        rackSlot.synchronizedImpulsePath.clear();
        rackSlot.observedImpulsePath.clear();
        rackSlot.impulseRestorePending = false;
        return;
    }
    auto* impulseResponse = module->impulseResponseCapability();
    if (impulseResponse == nullptr)
    {
        rackSlot.synchronizedImpulsePath.clear();
        rackSlot.observedImpulsePath.clear();
        rackSlot.impulseRestorePending = false;
        return;
    }

    const auto requestedPath = parameters.state.getProperty(
        "impulseResponse" + juce::String(slot + 1)).toString();
    if (requestedPath == rackSlot.observedImpulsePath)
        return;

    impulseResponse->clearImpulseResponse();
    rackSlot.synchronizedImpulsePath.clear();
    rackSlot.observedImpulsePath = requestedPath;
    rackSlot.impulseRestorePending = requestedPath.isNotEmpty();
}

void EffectRack::prepare(double sampleRate, int newMaximumBlockSize, int channelCount)
{
    maximumBlockSize = juce::jmax(1, newMaximumBlockSize);
    preparedSpec = {
        sampleRate, static_cast<juce::uint32>(maximumBlockSize),
        static_cast<juce::uint32>(channelCount)
    };
    hasPrepareState = true;
    rebuildModules();

    for (auto& slot : slots)
    {
        slot.wet.reset(sampleRate, 0.01);
        slot.wet.setCurrentAndTargetValue(slot.bypass->load() >= 0.5f ? 0.0f : 1.0f);
    }

    inputGainLinear.reset(sampleRate, 0.02);
    outputGainLinear.reset(sampleRate, 0.02);
    inputGainLinear.setCurrentAndTargetValue(
        juce::Decibels::decibelsToGain(inputGain->load()));
    outputGainLinear.setCurrentAndTargetValue(
        juce::Decibels::decibelsToGain(outputGain->load()));
    visualization.prepare(sampleRate);
}

bool EffectRack::synchronizeModules(bool restoreImpulseResponses)
{
    auto changed = false;
    for (int index = 0; index < slotCount; ++index)
    {
        auto& slot = slots[static_cast<size_t>(index)];
        const auto type = requestedType(slot);
        const auto ownershipIsValid =
            (slot.activeType == ModuleType::empty) == (slot.activeModule == nullptr);
        if (!ownershipIsValid || slot.activeType != type)
        {
            rebuildSlot(slot, type);
            changed = true;
        }
    }

    for (int index = 0; index < slotCount; ++index)
        synchronizeModuleDataState(index);
    if (restoreImpulseResponses)
        reloadImpulseResponses();
    return changed;
}

void EffectRack::rebuildModules()
{
    for (auto& slot : slots)
        rebuildSlot(slot, requestedType(slot));

    for (int index = 0; index < slotCount; ++index)
        synchronizeModuleDataState(index);
    reloadImpulseResponses();
}

void EffectRack::reset()
{
    for (auto& slot : slots)
    {
        if (slot.activeModule != nullptr)
            slot.activeModule->reset();
        for (auto& channel : slot.latencyBuffer)
            std::fill(channel.begin(), channel.end(), 0.0f);
        slot.latencyWritePosition = 0;
        slot.previousLatency = 0;
    }
}

void EffectRack::process(juce::AudioBuffer<float>& main,
                         const juce::AudioBuffer<float>* sidechain,
                         double bpm)
{
    if (main.getNumSamples() <= maximumBlockSize)
    {
        processChunk(main, sidechain, bpm);
        return;
    }

    for (int offset = 0; offset < main.getNumSamples(); offset += maximumBlockSize)
    {
        const auto samples = juce::jmin(maximumBlockSize, main.getNumSamples() - offset);
        std::array<float*, 2> mainPointers {};
        for (int channel = 0; channel < main.getNumChannels(); ++channel)
            mainPointers[static_cast<size_t>(channel)] =
                main.getWritePointer(channel, offset);
        juce::AudioBuffer<float> mainChunk(
            mainPointers.data(), main.getNumChannels(), samples);

        std::array<float*, 2> sidechainPointers {};
        std::optional<juce::AudioBuffer<float>> sidechainChunk;
        if (sidechain != nullptr)
        {
            for (int channel = 0; channel < sidechain->getNumChannels(); ++channel)
                sidechainPointers[static_cast<size_t>(channel)] =
                    const_cast<float*>(sidechain->getReadPointer(channel, offset));
            sidechainChunk.emplace(sidechainPointers.data(),
                                   sidechain->getNumChannels(), samples);
        }
        processChunk(mainChunk,
                     sidechainChunk.has_value() ? &*sidechainChunk : nullptr, bpm);
    }
}

void EffectRack::processChunk(juce::AudioBuffer<float>& main,
                              const juce::AudioBuffer<float>* sidechain,
                              double bpm)
{
    juce::ScopedNoDenormals noDenormals;
    inputGainLinear.setTargetValue(juce::Decibels::decibelsToGain(inputGain->load()));
    outputGainLinear.setTargetValue(juce::Decibels::decibelsToGain(outputGain->load()));
    main.applyGainRamp(0, main.getNumSamples(),
                       inputGainLinear.getCurrentValue(),
                       inputGainLinear.skip(main.getNumSamples()));
    float inputPeak = 0.0f;
    for (int channel = 0; channel < main.getNumChannels(); ++channel)
        inputPeak = juce::jmax(
            inputPeak, main.getMagnitude(channel, 0, main.getNumSamples()));
    retainPeak(inputMeter,
               juce::Decibels::gainToDecibels(inputPeak, -100.0f));

    const ProcessEnvironment environment { sidechain, bpm };
    for (int slotIndex = 0; slotIndex < slotCount; ++slotIndex)
    {
        auto& slot = slots[static_cast<size_t>(slotIndex)];
        auto* module = selectedModule(slot);
        if (module == nullptr)
            continue;

        const auto captureVisualization =
            visualization.getSelectedSlot() == slotIndex;
        if (captureVisualization)
            visualization.captureInput(slotIndex, main);

        const auto controlValues = slot.values();
        const auto moduleLatency = module->latencySamples();
        if (moduleLatency != slot.previousLatency)
        {
            for (auto& channel : slot.latencyBuffer)
                std::fill(channel.begin(), channel.end(), 0.0f);
            slot.latencyWritePosition = 0;
            slot.previousLatency = moduleLatency;
        }

        for (int channel = 0; channel < main.getNumChannels(); ++channel)
            slot.dryBuffer.copyFrom(channel, 0, main, channel, 0, main.getNumSamples());

        if (moduleLatency > 0)
        {
            const auto latencyCapacity =
                static_cast<int>(slot.latencyBuffer[0].size());
            jassert(moduleLatency <= latencyCapacity);
            if (moduleLatency > latencyCapacity)
                continue;
            for (int sample = 0; sample < main.getNumSamples(); ++sample)
            {
                for (int channel = 0; channel < main.getNumChannels(); ++channel)
                {
                    const auto index = static_cast<size_t>(juce::jmin(channel, 1));
                    auto& delayed = slot.latencyBuffer[index][
                        static_cast<size_t>(slot.latencyWritePosition)];
                    const auto input = slot.dryBuffer.getSample(channel, sample);
                    slot.dryBuffer.setSample(channel, sample, delayed);
                    delayed = input;
                }
                if (++slot.latencyWritePosition >= moduleLatency)
                    slot.latencyWritePosition = 0;
            }
        }

        module->process(main, controlValues, environment);
        if (captureVisualization)
        {
            visualization.captureOutput(slotIndex, main);
            visualization.captureGainReduction(
                slotIndex, module->meterValue(), main.getNumSamples());
        }
        slot.wet.setTargetValue(slot.bypass->load() >= 0.5f ? 0.0f : 1.0f);

        for (int sample = 0; sample < main.getNumSamples(); ++sample)
        {
            const auto wetAmount = slot.wet.getNextValue();
            for (int channel = 0; channel < main.getNumChannels(); ++channel)
            {
                const auto dry = slot.dryBuffer.getSample(channel, sample);
                const auto wetSample = main.getSample(channel, sample);
                main.setSample(channel, sample, dry + (wetSample - dry) * wetAmount);
            }
        }
    }

    main.applyGainRamp(0, main.getNumSamples(),
                       outputGainLinear.getCurrentValue(),
                       outputGainLinear.skip(main.getNumSamples()));
    float outputPeak = 0.0f;
    for (int channel = 0; channel < main.getNumChannels(); ++channel)
        outputPeak = juce::jmax(
            outputPeak, main.getMagnitude(channel, 0, main.getNumSamples()));
    retainPeak(outputMeter,
               juce::Decibels::gainToDecibels(outputPeak, -100.0f));
}

ModuleType EffectRack::moduleType(int slot) const
{
    if (!juce::isPositiveAndBelow(slot, slotCount))
        return ModuleType::empty;
    return requestedType(slots[static_cast<size_t>(slot)]);
}

ModuleType EffectRack::activeModuleType(int slot) const
{
    if (!juce::isPositiveAndBelow(slot, slotCount))
        return ModuleType::empty;
    return slots[static_cast<size_t>(slot)].activeType;
}

const DspModule* EffectRack::activeModuleInstance(int slot) const
{
    if (!juce::isPositiveAndBelow(slot, slotCount))
        return nullptr;
    const auto& rackSlot = slots[static_cast<size_t>(slot)];
    jassert((rackSlot.activeType == ModuleType::empty)
            == (rackSlot.activeModule == nullptr));
    return rackSlot.activeModule.get();
}

float EffectRack::slotMeter(int slot) const
{
    if (!juce::isPositiveAndBelow(slot, slotCount))
        return 0.0f;
    const auto* module = selectedModule(slots[static_cast<size_t>(slot)]);
    return module != nullptr ? module->meterValue() : 0.0f;
}

float EffectRack::slotDetectorMeter(int slot) const
{
    if (!juce::isPositiveAndBelow(slot, slotCount))
        return -100.0f;
    const auto* module = selectedModule(slots[static_cast<size_t>(slot)]);
    return module != nullptr ? module->detectorValue() : -100.0f;
}

int EffectRack::latencySamples() const
{
    int latency = 0;
    for (const auto& slot : slots)
    {
        if (const auto* module = selectedModule(slot))
            latency += module->latencySamples();
    }
    return latency;
}

double EffectRack::tailSeconds() const
{
    double tail = 0.0;
    for (const auto& slot : slots)
    {
        if (slot.bypass->load() < 0.5f)
            if (const auto* module = selectedModule(slot))
                tail += module->tailSeconds(slot.values());
    }
    return tail;
}

void EffectRack::moveSlot(int source, int destination)
{
    if (!juce::isPositiveAndBelow(source, slotCount)
        || !juce::isPositiveAndBelow(destination, slotCount)
        || source == destination)
        return;

    struct ParameterState
    {
        float type = 0.0f;
        float bypass = 0.0f;
        ControlValues controls {};
    };

    auto read = [this](int index)
    {
        ParameterState state;
        auto& slot = slots[static_cast<size_t>(index)];
        state.type = slot.type->load();
        state.bypass = slot.bypass->load();
        state.controls = slot.values();
        return state;
    };

    auto write = [this](int index, const ParameterState& state)
    {
        auto set = [](juce::RangedAudioParameter* parameter, float plainValue)
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
            parameter->endChangeGesture();
        };
        set(parameters.getParameter(slotParameterId(index, "type")), state.type);
        set(parameters.getParameter(slotParameterId(index, "bypass")), state.bypass);
        for (int control = 0; control < controlsPerSlot; ++control)
            set(parameters.getParameter(controlParameterId(index, control)),
                state.controls[static_cast<size_t>(control)]);
    };

    auto irProperty = [](int index)
    {
        return juce::Identifier("impulseResponse"
                                + juce::String(index + 1));
    };
    auto readIr = [this, &irProperty](int index)
    {
        return parameters.state.getProperty(irProperty(index)).toString();
    };
    auto writeIr = [this, &irProperty](int index, const juce::String& path)
    {
        if (path.isEmpty())
            parameters.state.removeProperty(irProperty(index), nullptr);
        else
            parameters.state.setProperty(irProperty(index), path, nullptr);
    };

    const auto moving = read(source);
    const auto movingIr = readIr(source);
    if (source < destination)
        for (int index = source; index < destination; ++index)
        {
            write(index, read(index + 1));
            writeIr(index, readIr(index + 1));
        }
    else
        for (int index = source; index > destination; --index)
        {
            write(index, read(index - 1));
            writeIr(index, readIr(index - 1));
        }
    write(destination, moving);
    writeIr(destination, movingIr);
}

void EffectRack::clearSlot(int slot)
{
    if (!juce::isPositiveAndBelow(slot, slotCount))
        return;
    clearImpulseResponse(slot);

    auto set = [](juce::RangedAudioParameter* parameter, float plainValue)
    {
        if (parameter == nullptr)
            return;
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
        parameter->endChangeGesture();
    };
    set(parameters.getParameter(slotParameterId(slot, "type")), 0.0f);
    set(parameters.getParameter(slotParameterId(slot, "bypass")), 0.0f);
    for (int control = 0; control < controlsPerSlot; ++control)
        set(parameters.getParameter(controlParameterId(slot, control)), 0.5f);
    parameters.state.removeProperty(
        "impulseResponse" + juce::String(slot + 1), nullptr);
}

void EffectRack::clearModuleDataState()
{
    for (int slot = 0; slot < slotCount; ++slot)
    {
        auto& rackSlot = slots[static_cast<size_t>(slot)];
        if (rackSlot.activeModule != nullptr)
            if (auto* impulseResponse =
                    rackSlot.activeModule->impulseResponseCapability())
                impulseResponse->clearImpulseResponse();
        rackSlot.synchronizedImpulsePath.clear();
        rackSlot.observedImpulsePath.clear();
        rackSlot.impulseRestorePending = false;
        parameters.state.removeProperty(
            "impulseResponse" + juce::String(slot + 1), nullptr);
    }
}

bool EffectRack::compactSlots()
{
    auto changed = false;
    for (int destination = 0; destination < slotCount; ++destination)
    {
        if (moduleType(destination) != ModuleType::empty)
            continue;
        int source = destination + 1;
        while (source < slotCount && moduleType(source) == ModuleType::empty)
            ++source;
        if (source >= slotCount)
            break;
        moveSlot(source, destination);
        changed = true;
    }
    return changed;
}

void EffectRack::removeSlot(int slot)
{
    const auto active = activeSlotCount();
    if (!juce::isPositiveAndBelow(slot, active))
        return;
    moveSlot(slot, active - 1);
    clearSlot(active - 1);
}

int EffectRack::activeSlotCount() const
{
    int count = 0;
    while (count < slotCount && moduleType(count) != ModuleType::empty)
        ++count;
    return count;
}

bool EffectRack::activeModuleHasCapability(
    int slot, ModuleCapability capability) const
{
    if (!juce::isPositiveAndBelow(slot, slotCount))
        return false;
    const auto& rackSlot = slots[static_cast<size_t>(slot)];
    const auto* module = selectedModule(rackSlot);
    const auto* definition = findModuleDefinition(rackSlot.activeType);
    return module != nullptr && definition != nullptr
           && hasCapability(definition->capabilities, capability)
           && hasCapability(module->capabilities(), capability);
}

juce::Result EffectRack::prepareImpulseResponse(
    int slot, const juce::File& file,
    PreparedImpulseResponseLoad& load)
{
    load = {};
    if (!juce::isPositiveAndBelow(slot, slotCount))
        return juce::Result::fail("Invalid impulse-response slot.");

    auto& rackSlot = slots[static_cast<size_t>(slot)];
    auto* module = selectedModule(rackSlot);
    if (module == nullptr)
        return juce::Result::fail(
            "The selected slot has no synchronized DSP module.");
    auto* impulseResponse = module->impulseResponseCapability();
    if (impulseResponse == nullptr)
        return juce::Result::fail(
            "The selected slot does not support impulse responses.");

    load.slot = slot;
    load.type = rackSlot.activeType;
    load.module = module;
    load.path = file.getFullPathName();
    auto result = impulseResponse->prepareImpulseResponse(
        file, load.prepared);
    if (result.failed())
        load = {};
    return result;
}

bool EffectRack::commitImpulseResponse(
    PreparedImpulseResponseLoad& load)
{
    if (!juce::isPositiveAndBelow(load.slot, slotCount)
        || load.prepared == nullptr)
        return false;

    auto& rackSlot = slots[static_cast<size_t>(load.slot)];
    auto* module = selectedModule(rackSlot);
    if (module == nullptr || module != load.module
        || rackSlot.activeType != load.type)
        return false;
    auto* impulseResponse = module->impulseResponseCapability();
    if (impulseResponse == nullptr
        || !impulseResponse->commitPreparedImpulseResponse(
            load.prepared))
        return false;

    rackSlot.synchronizedImpulsePath = load.path;
    rackSlot.observedImpulsePath = load.path;
    rackSlot.impulseRestorePending = false;
    parameters.state.setProperty(
        "impulseResponse" + juce::String(load.slot + 1),
        load.path, nullptr);
    return true;
}

void EffectRack::cancelImpulseResponse(
    PreparedImpulseResponseLoad& load) noexcept
{
    if (juce::isPositiveAndBelow(load.slot, slotCount))
    {
        auto& rackSlot = slots[static_cast<size_t>(load.slot)];
        auto* module = selectedModule(rackSlot);
        if (module != nullptr && module == load.module
            && rackSlot.activeType == load.type)
            if (auto* impulseResponse =
                    module->impulseResponseCapability())
                impulseResponse->cancelPreparedImpulseResponse(
                    load.prepared);
    }
    load = {};
}

juce::Result EffectRack::loadImpulseResponse(
    int slot, const juce::File& file)
{
    PreparedImpulseResponseLoad load;
    auto result = prepareImpulseResponse(slot, file, load);
    if (result.failed())
        return result;
    if (!commitImpulseResponse(load))
    {
        cancelImpulseResponse(load);
        return juce::Result::fail(
            "The impulse response request was superseded.");
    }
    return juce::Result::ok();
}

juce::Result EffectRack::clearImpulseResponse(int slot)
{
    if (!juce::isPositiveAndBelow(slot, slotCount))
        return juce::Result::fail("Invalid convolution slot.");
    auto& rackSlot = slots[static_cast<size_t>(slot)];
    auto* module = selectedModule(rackSlot);
    if (module == nullptr)
        return juce::Result::fail(
            "The selected slot has no synchronized DSP module.");
    auto* impulseResponse = module->impulseResponseCapability();
    if (impulseResponse == nullptr)
        return juce::Result::fail(
            "The selected slot does not support impulse responses.");
    impulseResponse->clearImpulseResponse();
    rackSlot.synchronizedImpulsePath.clear();
    rackSlot.observedImpulsePath.clear();
    rackSlot.impulseRestorePending = false;
    parameters.state.removeProperty(
        "impulseResponse" + juce::String(slot + 1), nullptr);
    return juce::Result::ok();
}

void EffectRack::reloadImpulseResponses()
{
    for (int slot = 0; slot < slotCount; ++slot)
    {
        const auto path = pendingImpulseResponsePath(slot);
        if (path.isNotEmpty())
            if (loadImpulseResponse(slot, juce::File(path)).failed())
                discardPendingImpulseResponse(slot, path);
    }
}

juce::String EffectRack::pendingImpulseResponsePath(int slot) const
{
    if (!juce::isPositiveAndBelow(slot, slotCount))
        return {};
    const auto& rackSlot = slots[static_cast<size_t>(slot)];
    const auto* module = selectedModule(rackSlot);
    if (module == nullptr
        || module->impulseResponseCapability() == nullptr)
        return {};
    const auto path = parameters.state.getProperty(
        "impulseResponse" + juce::String(slot + 1)).toString();
    return rackSlot.impulseRestorePending && path.isNotEmpty()
               && path == rackSlot.observedImpulsePath
               ? path : juce::String{};
}

void EffectRack::discardPendingImpulseResponse(
    int slot, const juce::String& path)
{
    if (!juce::isPositiveAndBelow(slot, slotCount))
        return;
    auto& rackSlot = slots[static_cast<size_t>(slot)];
    if (rackSlot.observedImpulsePath == path)
        rackSlot.impulseRestorePending = false;
}

juce::String EffectRack::impulseResponseName(int slot) const
{
    if (!juce::isPositiveAndBelow(slot, slotCount))
        return {};
    const auto& rackSlot = slots[static_cast<size_t>(slot)];
    if (const auto* module = selectedModule(rackSlot))
        if (const auto* impulseResponse =
                module->impulseResponseCapability())
            return impulseResponse->impulseResponseName();
    return {};
}

juce::String EffectRack::impulseResponsePath(int slot) const
{
    if (!juce::isPositiveAndBelow(slot, slotCount))
        return {};
    const auto& rackSlot = slots[static_cast<size_t>(slot)];
    const auto* module = selectedModule(rackSlot);
    if (module == nullptr
        || module->impulseResponseCapability() == nullptr)
        return {};
    return parameters.state.getProperty(
        "impulseResponse" + juce::String(slot + 1)).toString();
}

ImpulseResponsePreview EffectRack::impulseResponsePreview(int slot) const
{
    if (!juce::isPositiveAndBelow(slot, slotCount))
        return {};
    const auto& rackSlot = slots[static_cast<size_t>(slot)];
    if (const auto* module = selectedModule(rackSlot))
        if (const auto* impulseResponse =
                module->impulseResponseCapability())
            return impulseResponse->impulseResponsePreview();
    return {};
}

GrainVisualEvents EffectRack::grainVisualEvents(int slot) const
{
    if (!juce::isPositiveAndBelow(slot, slotCount))
        return {};
    const auto& rackSlot = slots[static_cast<size_t>(slot)];
    if (const auto* module = selectedModule(rackSlot))
        if (const auto* visualizationCapability =
                module->grainVisualizationCapability())
            return visualizationCapability->grainVisualEvents();
    return {};
}

BeatPermutationVisualEvents EffectRack::beatPermutationVisualEvents(
    int slot) const
{
    if (const auto* module = activeModuleInstance(slot))
    {
        if (const auto* visualizationCapability =
                module->beatPermutationVisualizationCapability())
            return visualizationCapability->beatPermutationVisualEvents();
    }
    return {};
}
} // namespace megadsp
