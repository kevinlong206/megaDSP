#include "EffectRack.h"
#include "modules/NeuralAmpModeler.h"

namespace megadsp
{
void RealtimeProcessProfile::add(
    const RealtimeProcessProfile& other) noexcept
{
    for (size_t index = 0; index < moduleTicks.size(); ++index)
    {
        moduleTicks[index] += other.moduleTicks[index];
        moduleMaximumTicks[index] = juce::jmax(
            moduleMaximumTicks[index], other.moduleMaximumTicks[index]);
        moduleCalls[index] += other.moduleCalls[index];
    }
    for (size_t index = 0; index < graphTicks.size(); ++index)
    {
        graphTicks[index] += other.graphTicks[index];
        graphMaximumTicks[index] = juce::jmax(
            graphMaximumTicks[index], other.graphMaximumTicks[index]);
        graphCalls[index] += other.graphCalls[index];
    }
}

void RealtimeProcessProfile::recordModule(
    ModuleType type, std::uint64_t ticks) noexcept
{
    const auto index = static_cast<size_t>(type);
    if (index >= moduleTicks.size())
        return;
    moduleTicks[index] += ticks;
    moduleMaximumTicks[index] = juce::jmax(
        moduleMaximumTicks[index], ticks);
    ++moduleCalls[index];
}

void RealtimeProcessProfile::recordGraph(
    int snapshot, std::uint64_t ticks) noexcept
{
    if (!juce::isPositiveAndBelow(snapshot, snapshotCount))
        return;
    const auto index = static_cast<size_t>(snapshot);
    graphTicks[index] += ticks;
    graphMaximumTicks[index] = juce::jmax(
        graphMaximumTicks[index], ticks);
    ++graphCalls[index];
}

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

int alignedLatencySamples(const DspModule& module)
{
    return module.dryPathTiming() == DryPathTiming::aligned
        ? module.latencySamples() : 0;
}
} // namespace

ControlValues EffectRack::Slot::values() const
{
    ControlValues result {};
    for (int index = 0; index < controlsPerSlot; ++index)
        result[static_cast<size_t>(index)] = controls[static_cast<size_t>(index)]->load();
    return result;
}

EffectRack::EffectRack(RackState& stateToUse)
    : state(&stateToUse)
{
    for (int slot = 0; slot < slotCount; ++slot)
    {
        auto& current = slots[static_cast<size_t>(slot)];
        auto& source = state->slots[static_cast<size_t>(slot)];
        current.type = &source.type;
        current.bypass = &source.bypass;
        for (int control = 0; control < controlsPerSlot; ++control)
            current.controls[static_cast<size_t>(control)] =
                &source.controls[static_cast<size_t>(control)];
    }
    inputGain = &state->inputGainDb;
    outputGain = &state->outputGainDb;
}

EffectRack::EffectRack(
    juce::AudioProcessorValueTreeState& stateToUse)
    : legacyParameters(&stateToUse)
{
    for (int slot = 0; slot < slotCount; ++slot)
    {
        auto& current = slots[static_cast<size_t>(slot)];
        current.type = stateToUse.getRawParameterValue(
            slotParameterId(slot, "type"));
        current.bypass = stateToUse.getRawParameterValue(
            slotParameterId(slot, "bypass"));
        for (int control = 0; control < controlsPerSlot; ++control)
            current.controls[static_cast<size_t>(control)] =
                stateToUse.getRawParameterValue(
                    controlParameterId(slot, control));
    }
    inputGain = stateToUse.getRawParameterValue("global.input");
    outputGain = stateToUse.getRawParameterValue("global.output");
}

juce::String EffectRack::impulsePath(int slot, int lane) const
{
    if (legacyParameters != nullptr)
        return legacyParameters->state.getProperty(
            "impulseResponse" + (lane == 0 ? juce::String {}
                                           : juce::String(lane + 1))
                + juce::String(slot + 1)).toString();
    const auto& rackSlot = state->slots[static_cast<size_t>(slot)];
    return lane == 0 ? rackSlot.impulseResponsePath
                     : rackSlot.impulseResponsePath2;
}

void EffectRack::setImpulsePath(
    int slot, int lane, const juce::String& path)
{
    if (legacyParameters != nullptr)
    {
        const auto property =
            juce::Identifier(
                "impulseResponse" + (lane == 0 ? juce::String {}
                                                : juce::String(lane + 1))
                + juce::String(slot + 1));
        if (path.isEmpty())
            legacyParameters->state.removeProperty(property, nullptr);
        else
            legacyParameters->state.setProperty(property, path, nullptr);
        return;
    }
    auto& rackSlot = state->slots[static_cast<size_t>(slot)];
    if (lane == 0)
        rackSlot.impulseResponsePath = path;
    else
        rackSlot.impulseResponsePath2 = path;
}

juce::String EffectRack::namPath(int slot) const
{
    if (legacyParameters != nullptr)
        return legacyParameters->state.getProperty(
            "neuralModel" + juce::String(slot + 1)).toString();
    return state->slots[static_cast<size_t>(slot)].namModelPath;
}

void EffectRack::setNamPath(int slot, const juce::String& path)
{
    if (legacyParameters != nullptr)
    {
        const auto property =
            juce::Identifier("neuralModel" + juce::String(slot + 1));
        if (path.isEmpty())
            legacyParameters->state.removeProperty(property, nullptr);
        else
            legacyParameters->state.setProperty(property, path, nullptr);
        return;
    }
    state->slots[static_cast<size_t>(slot)].namModelPath = path;
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
    slot.synchronizedImpulsePath2.clear();
    slot.observedImpulsePath2.clear();
    slot.impulseRestorePending2 = false;
    slot.synchronizedNamPath.clear();
    slot.observedNamPath.clear();
    slot.namRestorePending = false;
    slot.dryBuffer.setSize(0, 0);
    for (auto& channel : slot.latencyBuffer)
        std::vector<float>().swap(channel);
    slot.latencyWritePosition = 0;
    slot.previousLatency = 0;
    slot.dspSuspended = false;
    slot.reactivationSamplesRemaining = 0;

    if (type == ModuleType::empty)
        return;

    auto module = createDspModule(type);
    if (module == nullptr)
        return;

    if (hasPrepareState)
    {
        module->prepare(preparedSpec);
        module->reset();
        if (auto* impulse = module->impulseResponseCapability();
            impulse != nullptr
            && impulse->builtInImpulseResponseCount() > 0)
        {
            const auto index = discreteIndex(
                slot.controls[0]->load(),
                impulse->builtInImpulseResponseCount());
            PreparedImpulseResponsePtr prepared;
            if (impulse->prepareBuiltInImpulseResponse(
                    index, prepared).wasOk())
                impulse->commitPreparedImpulseResponse(prepared);
        }
        slot.dryBuffer.setSize(
            static_cast<int>(preparedSpec.numChannels),
            static_cast<int>(preparedSpec.maximumBlockSize),
            false, true, true);
        const auto latencyCapacity = static_cast<size_t>(
            juce::jmax(0, alignedLatencySamples(*module)));
        if (latencyCapacity > 0)
            for (auto& channel : slot.latencyBuffer)
                channel.assign(latencyCapacity, 0.0f);
        slot.wet.reset(preparedSpec.sampleRate, 0.01);
        slot.wet.setCurrentAndTargetValue(
            slot.bypass->load() >= 0.5f ? 0.0f : 1.0f);
        slot.dspSuspended = slot.bypass->load() >= 0.5f;
        slot.reactivationSamplesRemaining = 0;
    }

    slot.previousLatency = alignedLatencySamples(*module);
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
        rackSlot.synchronizedImpulsePath2.clear();
        rackSlot.observedImpulsePath2.clear();
        rackSlot.impulseRestorePending2 = false;
        rackSlot.synchronizedNamPath.clear();
        rackSlot.observedNamPath.clear();
        rackSlot.namRestorePending = false;
        return;
    }

    // IR capability
    auto* impulseResponse = module->impulseResponseCapability();
    if (impulseResponse == nullptr)
    {
        rackSlot.synchronizedImpulsePath.clear();
        rackSlot.observedImpulsePath.clear();
        rackSlot.impulseRestorePending = false;
        rackSlot.synchronizedImpulsePath2.clear();
        rackSlot.observedImpulsePath2.clear();
        rackSlot.impulseRestorePending2 = false;
    }
    else
    {
        for (int lane = 0;
             lane < impulseResponse->impulseResponseLaneCount(); ++lane)
        {
            const auto requestedPath = impulsePath(slot, lane);
            auto& synchronized = lane == 0
                ? rackSlot.synchronizedImpulsePath
                : rackSlot.synchronizedImpulsePath2;
            auto& observed = lane == 0
                ? rackSlot.observedImpulsePath
                : rackSlot.observedImpulsePath2;
            auto& pending = lane == 0
                ? rackSlot.impulseRestorePending
                : rackSlot.impulseRestorePending2;
            if (requestedPath != observed)
            {
                impulseResponse->clearImpulseResponse(lane);
                synchronized.clear();
                observed = requestedPath;
                pending = requestedPath.isNotEmpty();
            }
        }
    }

    // NAM capability
    auto* namCap = module->namModelCapability();
    if (namCap == nullptr)
    {
        rackSlot.synchronizedNamPath.clear();
        rackSlot.observedNamPath.clear();
        rackSlot.namRestorePending = false;
    }
    else
    {
        const auto requestedPath = namPath(slot);
        if (requestedPath != rackSlot.observedNamPath)
        {
            namCap->clearNamModel();
            rackSlot.synchronizedNamPath.clear();
            rackSlot.observedNamPath = requestedPath;
            rackSlot.namRestorePending = requestedPath.isNotEmpty();
        }
    }
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
        slot.dspSuspended =
            slot.activeModule != nullptr && slot.bypass->load() >= 0.5f;
        slot.reactivationSamplesRemaining = 0;
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

void EffectRack::rebuildModules(bool restoreModuleData)
{
    for (auto& slot : slots)
        rebuildSlot(slot, requestedType(slot));

    for (int index = 0; index < slotCount; ++index)
        synchronizeModuleDataState(index);
    if (restoreModuleData)
    {
        reloadImpulseResponses();
        reloadNamModels();
    }
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
        slot.dspSuspended =
            slot.activeModule != nullptr && slot.bypass->load() >= 0.5f;
        slot.reactivationSamplesRemaining = 0;
    }
}

void EffectRack::process(juce::AudioBuffer<float>& main,
                         const juce::AudioBuffer<float>* sidechain,
                         double bpm,
                         RealtimeProcessProfile* profile)
{
    if (main.getNumSamples() <= maximumBlockSize)
    {
        processChunk(main, sidechain, bpm, profile);
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
                     sidechainChunk.has_value() ? &*sidechainChunk : nullptr,
                     bpm, profile);
    }
}

void EffectRack::processChunk(juce::AudioBuffer<float>& main,
                              const juce::AudioBuffer<float>* sidechain,
                              double bpm,
                              RealtimeProcessProfile* profile)
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

    for (int slotIndex = 0; slotIndex < slotCount; ++slotIndex)
    {
        auto& slot = slots[static_cast<size_t>(slotIndex)];
        auto* module = selectedModule(slot);
        if (module == nullptr)
            continue;

        const auto bypassRequested = slot.bypass->load() >= 0.5f;
        if (bypassRequested)
            slot.wet.setTargetValue(0.0f);
        else if (!slot.dspSuspended
                 && slot.reactivationSamplesRemaining <= 0)
            slot.wet.setTargetValue(1.0f);
        const auto captureVisualization =
            !bypassRequested
            && visualization.isSlotActive(slotIndex);
        const ProcessEnvironment environment {
            sidechain, bpm, captureVisualization
        };
        if (captureVisualization)
            visualization.captureInput(slotIndex, main);

        const auto processingLatency = module->latencySamples();
        const auto moduleLatency = alignedLatencySamples(*module);
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

        if (bypassRequested && slot.dspSuspended)
        {
            for (int channel = 0; channel < main.getNumChannels(); ++channel)
                main.copyFrom(
                    channel, 0, slot.dryBuffer, channel, 0,
                    main.getNumSamples());
            continue;
        }

        if (!bypassRequested && slot.dspSuspended)
        {
            slot.dspSuspended = false;
            slot.reactivationSamplesRemaining = processingLatency;
            slot.wet.setCurrentAndTargetValue(0.0f);
            if (slot.reactivationSamplesRemaining <= 0)
                slot.wet.setTargetValue(1.0f);
        }
        const auto controlValues = slot.values();
        const auto profileStart = profile != nullptr
            ? juce::Time::getHighResolutionTicks() : 0;
        module->process(main, controlValues, environment);
        if (profile != nullptr)
            profile->recordModule(
                slot.activeType,
                static_cast<std::uint64_t>(
                    juce::Time::getHighResolutionTicks() - profileStart));
        if (captureVisualization)
        {
            visualization.captureOutput(slotIndex, main);
            visualization.captureGainReduction(
                slotIndex, module->meterValue(), main.getNumSamples());
        }
        for (int sample = 0; sample < main.getNumSamples(); ++sample)
        {
            auto wetAmount = 0.0f;
            if (!bypassRequested
                && slot.reactivationSamplesRemaining > 0)
            {
                if (--slot.reactivationSamplesRemaining == 0)
                    slot.wet.setTargetValue(1.0f);
            }
            else
            {
                wetAmount = slot.wet.getNextValue();
            }
            for (int channel = 0; channel < main.getNumChannels(); ++channel)
            {
                const auto dry = slot.dryBuffer.getSample(channel, sample);
                const auto wetSample = main.getSample(channel, sample);
                main.setSample(channel, sample, dry + (wetSample - dry) * wetAmount);
            }
        }
        if (bypassRequested && !slot.wet.isSmoothing()
            && slot.wet.getCurrentValue() <= 0.0f)
        {
            module->reset();
            slot.dspSuspended = true;
            slot.reactivationSamplesRemaining = 0;
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
    const auto& rackSlot = slots[static_cast<size_t>(slot)];
    if (rackSlot.bypass->load() >= 0.5f)
        return 0.0f;
    const auto* module = selectedModule(rackSlot);
    return module != nullptr ? module->meterValue() : 0.0f;
}

float EffectRack::slotDetectorMeter(int slot) const
{
    if (!juce::isPositiveAndBelow(slot, slotCount))
        return -100.0f;
    const auto& rackSlot = slots[static_cast<size_t>(slot)];
    if (rackSlot.bypass->load() >= 0.5f)
        return -100.0f;
    const auto* module = selectedModule(rackSlot);
    return module != nullptr ? module->detectorValue() : -100.0f;
}

bool EffectRack::slotDspSuspended(int slot) const
{
    return juce::isPositiveAndBelow(slot, slotCount)
        && slots[static_cast<size_t>(slot)].dspSuspended;
}

int EffectRack::latencySamples() const
{
    int latency = 0;
    for (const auto& slot : slots)
    {
        if (const auto* module = selectedModule(slot))
            latency += alignedLatencySamples(*module);
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

double EffectRack::snapshotSpillSeconds() const
{
    double tail = 0.0;
    for (const auto& slot : slots)
    {
        if (slot.bypass->load() < 0.5f)
            if (const auto* module = selectedModule(slot))
                tail += module->snapshotSpillSeconds(slot.values());
    }
    return juce::jlimit(0.0, 8.0, tail);
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
        ParameterState parameterState;
        auto& slot = slots[static_cast<size_t>(index)];
        parameterState.type = slot.type->load();
        parameterState.bypass = slot.bypass->load();
        parameterState.controls = slot.values();
        return parameterState;
    };

    auto write = [this](int index, const ParameterState& newState)
    {
        auto& targetSlot = slots[static_cast<size_t>(index)];
        targetSlot.type->store(newState.type, std::memory_order_relaxed);
        targetSlot.bypass->store(newState.bypass, std::memory_order_relaxed);
        for (int control = 0; control < controlsPerSlot; ++control)
            targetSlot.controls[static_cast<size_t>(control)]->store(
                newState.controls[static_cast<size_t>(control)],
                std::memory_order_relaxed);
    };

    auto irProperty = [](int index)
    {
        return juce::Identifier("impulseResponse"
                                + juce::String(index + 1));
    };
    auto readIr = [this, &irProperty](int index, int lane)
    {
        juce::ignoreUnused(irProperty);
        return impulsePath(index, lane);
    };
    auto writeIr = [this, &irProperty](
                       int index, int lane, const juce::String& path)
    {
        juce::ignoreUnused(irProperty);
        setImpulsePath(index, lane, path);
    };
    auto namProperty = [](int index)
    {
        return juce::Identifier("neuralModel" + juce::String(index + 1));
    };
    auto readNam = [this, &namProperty](int index)
    {
        juce::ignoreUnused(namProperty);
        return namPath(index);
    };
    auto writeNam = [this, &namProperty](int index, const juce::String& path)
    {
        juce::ignoreUnused(namProperty);
        setNamPath(index, path);
    };

    const auto moving = read(source);
    const std::array<juce::String, 2> movingIr {
        readIr(source, 0), readIr(source, 1)
    };
    const auto movingNam = readNam(source);
    if (source < destination)
        for (int index = source; index < destination; ++index)
        {
            write(index, read(index + 1));
            for (int lane = 0; lane < 2; ++lane)
                writeIr(index, lane, readIr(index + 1, lane));
            writeNam(index, readNam(index + 1));
        }
    else
        for (int index = source; index > destination; --index)
        {
            write(index, read(index - 1));
            for (int lane = 0; lane < 2; ++lane)
                writeIr(index, lane, readIr(index - 1, lane));
            writeNam(index, readNam(index - 1));
        }
    write(destination, moving);
    for (int lane = 0; lane < 2; ++lane)
        writeIr(destination, lane, movingIr[static_cast<size_t>(lane)]);
    writeNam(destination, movingNam);
}

void EffectRack::clearSlot(int slot)
{
    if (!juce::isPositiveAndBelow(slot, slotCount))
        return;
    clearImpulseResponse(slot);
    clearImpulseResponse(slot, 1);
    clearNamModel(slot);

    auto& destination = slots[static_cast<size_t>(slot)];
    destination.type->store(0.0f, std::memory_order_relaxed);
    destination.bypass->store(0.0f, std::memory_order_relaxed);
    for (int control = 0; control < controlsPerSlot; ++control)
        destination.controls[static_cast<size_t>(control)]->store(
            0.5f, std::memory_order_relaxed);
    setImpulsePath(slot, 0, {});
    setImpulsePath(slot, 1, {});
    setNamPath(slot, {});
}

void EffectRack::clearModuleDataState()
{
    for (int slot = 0; slot < slotCount; ++slot)
    {
        auto& rackSlot = slots[static_cast<size_t>(slot)];
        if (rackSlot.activeModule != nullptr)
        {
            if (auto* impulseResponse =
                    rackSlot.activeModule->impulseResponseCapability())
                for (int lane = 0;
                     lane < impulseResponse->impulseResponseLaneCount();
                     ++lane)
                    impulseResponse->clearImpulseResponse(lane);
            if (auto* namCap = rackSlot.activeModule->namModelCapability())
                namCap->clearNamModel();
        }
        rackSlot.synchronizedImpulsePath.clear();
        rackSlot.observedImpulsePath.clear();
        rackSlot.impulseRestorePending = false;
        rackSlot.synchronizedImpulsePath2.clear();
        rackSlot.observedImpulsePath2.clear();
        rackSlot.impulseRestorePending2 = false;
        rackSlot.synchronizedNamPath.clear();
        rackSlot.observedNamPath.clear();
        rackSlot.namRestorePending = false;
        setImpulsePath(slot, 0, {});
        setImpulsePath(slot, 1, {});
        setNamPath(slot, {});
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
    return prepareImpulseResponse(slot, 0, file, load);
}

juce::Result EffectRack::prepareImpulseResponse(
    int slot, int lane, const juce::File& file,
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

    if (!juce::isPositiveAndBelow(
            lane, impulseResponse->impulseResponseLaneCount()))
        return juce::Result::fail("Invalid impulse-response lane.");

    load.slot = slot;
    load.type = rackSlot.activeType;
    load.module = module;
    load.lane = lane;
    load.path = file.getFullPathName();
    const auto values = rackSlot.values();
    auto result = impulseResponse->prepareImpulseResponse(
        lane, file,
        impulseResponse->impulseResponseTransform(values.data(), lane),
        load.prepared);
    if (result.failed())
        load = {};
    return result;
}

juce::Result EffectRack::prepareBuiltInImpulseResponse(
    int slot, int index, PreparedImpulseResponseLoad& load)
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
    auto result = impulseResponse->prepareBuiltInImpulseResponse(
        index, load.prepared);
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
            load.lane, load.prepared))
        return false;

    auto& synchronized = load.lane == 0
        ? rackSlot.synchronizedImpulsePath
        : rackSlot.synchronizedImpulsePath2;
    auto& observed = load.lane == 0
        ? rackSlot.observedImpulsePath
        : rackSlot.observedImpulsePath2;
    auto& pending = load.lane == 0
        ? rackSlot.impulseRestorePending
        : rackSlot.impulseRestorePending2;
    synchronized = load.path;
    observed = load.path;
    pending = false;
    setImpulsePath(load.slot, load.lane, load.path);
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
                    load.lane, load.prepared);
    }
    load = {};
}

juce::Result EffectRack::loadImpulseResponse(
    int slot, const juce::File& file)
{
    return loadImpulseResponse(slot, 0, file);
}

juce::Result EffectRack::loadImpulseResponse(
    int slot, int lane, const juce::File& file)
{
    PreparedImpulseResponseLoad load;
    auto result = prepareImpulseResponse(slot, lane, file, load);
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

juce::Result EffectRack::loadBuiltInImpulseResponse(int slot, int index)
{
    PreparedImpulseResponseLoad load;
    auto result = prepareBuiltInImpulseResponse(slot, index, load);
    if (result.failed())
        return result;
    if (!commitImpulseResponse(load))
    {
        cancelImpulseResponse(load);
        return juce::Result::fail(
            "The cabinet impulse request was superseded.");
    }
    return juce::Result::ok();
}

juce::Result EffectRack::clearImpulseResponse(int slot)
{
    return clearImpulseResponse(slot, 0);
}

juce::Result EffectRack::clearImpulseResponse(int slot, int lane)
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
    if (!juce::isPositiveAndBelow(
            lane, impulseResponse->impulseResponseLaneCount()))
        return juce::Result::fail("Invalid impulse-response lane.");
    if (lane == 0 && impulseResponse->builtInImpulseResponseCount() > 0)
    {
        const auto index = discreteIndex(
            rackSlot.controls[0]->load(),
            impulseResponse->builtInImpulseResponseCount());
        auto result = loadBuiltInImpulseResponse(slot, index);
        if (result.failed())
            return result;
    }
    else
        impulseResponse->clearImpulseResponse(lane);
    auto& synchronized = lane == 0
        ? rackSlot.synchronizedImpulsePath
        : rackSlot.synchronizedImpulsePath2;
    auto& observed = lane == 0
        ? rackSlot.observedImpulsePath
        : rackSlot.observedImpulsePath2;
    auto& pending = lane == 0
        ? rackSlot.impulseRestorePending
        : rackSlot.impulseRestorePending2;
    synchronized.clear();
    observed.clear();
    pending = false;
    setImpulsePath(slot, lane, {});
    return juce::Result::ok();
}

void EffectRack::reloadImpulseResponses()
{
    for (int slot = 0; slot < slotCount; ++slot)
    {
        for (int lane = 0; lane < 2; ++lane)
        {
            const auto path = pendingImpulseResponsePath(slot, lane);
            if (path.isNotEmpty())
                if (loadImpulseResponse(
                        slot, lane, juce::File(path)).failed())
                    discardPendingImpulseResponse(slot, lane, path);
        }
    }
}

juce::String EffectRack::pendingImpulseResponsePath(int slot) const
{
    return pendingImpulseResponsePath(slot, 0);
}

juce::String EffectRack::pendingImpulseResponsePath(
    int slot, int lane) const
{
    if (!juce::isPositiveAndBelow(slot, slotCount))
        return {};
    const auto& rackSlot = slots[static_cast<size_t>(slot)];
    const auto* module = selectedModule(rackSlot);
    if (module == nullptr
        || module->impulseResponseCapability() == nullptr)
        return {};
    const auto path = impulsePath(slot, lane);
    const auto& observed = lane == 0
        ? rackSlot.observedImpulsePath : rackSlot.observedImpulsePath2;
    const auto pending = lane == 0
        ? rackSlot.impulseRestorePending : rackSlot.impulseRestorePending2;
    return pending && path.isNotEmpty()
               && path == observed
               ? path : juce::String{};
}

void EffectRack::discardPendingImpulseResponse(
    int slot, const juce::String& path)
{
    discardPendingImpulseResponse(slot, 0, path);
}

void EffectRack::discardPendingImpulseResponse(
    int slot, int lane, const juce::String& path)
{
    if (!juce::isPositiveAndBelow(slot, slotCount))
        return;
    auto& rackSlot = slots[static_cast<size_t>(slot)];
    auto& observed = lane == 0
        ? rackSlot.observedImpulsePath : rackSlot.observedImpulsePath2;
    auto& pending = lane == 0
        ? rackSlot.impulseRestorePending : rackSlot.impulseRestorePending2;
    if (observed == path)
        pending = false;
}

juce::String EffectRack::impulseResponseName(int slot) const
{
    return impulseResponseName(slot, 0);
}

juce::String EffectRack::impulseResponseName(int slot, int lane) const
{
    if (!juce::isPositiveAndBelow(slot, slotCount))
        return {};
    const auto& rackSlot = slots[static_cast<size_t>(slot)];
    if (const auto* module = selectedModule(rackSlot))
        if (const auto* impulseResponse =
                module->impulseResponseCapability())
            return impulseResponse->impulseResponseName(lane);
    return {};
}

juce::String EffectRack::impulseResponsePath(int slot) const
{
    return impulseResponsePath(slot, 0);
}

juce::String EffectRack::impulseResponsePath(int slot, int lane) const
{
    if (!juce::isPositiveAndBelow(slot, slotCount))
        return {};
    const auto& rackSlot = slots[static_cast<size_t>(slot)];
    const auto* module = selectedModule(rackSlot);
    if (module == nullptr
        || module->impulseResponseCapability() == nullptr)
        return {};
    return impulsePath(slot, lane);
}

ImpulseResponsePreview EffectRack::impulseResponsePreview(int slot) const
{
    return impulseResponsePreview(slot, 0);
}

ImpulseResponsePreview EffectRack::impulseResponsePreview(
    int slot, int lane) const
{
    if (!juce::isPositiveAndBelow(slot, slotCount))
        return {};
    const auto& rackSlot = slots[static_cast<size_t>(slot)];
    if (const auto* module = selectedModule(rackSlot))
        if (const auto* impulseResponse =
                module->impulseResponseCapability())
            return impulseResponse->impulseResponsePreview(lane);
    return {};
}

GrainVisualEvents EffectRack::grainVisualEvents(int slot) const
{
    if (!juce::isPositiveAndBelow(slot, slotCount))
        return {};
    const auto& rackSlot = slots[static_cast<size_t>(slot)];
    if (rackSlot.bypass->load() >= 0.5f)
        return {};
    if (const auto* module = selectedModule(rackSlot))
        if (const auto* visualizationCapability =
                module->grainVisualizationCapability())
            return visualizationCapability->grainVisualEvents();
    return {};
}

BeatPermutationVisualEvents EffectRack::beatPermutationVisualEvents(
    int slot) const
{
    if (!juce::isPositiveAndBelow(slot, slotCount)
        || slots[static_cast<size_t>(slot)].bypass->load() >= 0.5f)
        return {};
    if (const auto* module = activeModuleInstance(slot))
    {
        if (const auto* visualizationCapability =
                module->beatPermutationVisualizationCapability())
            return visualizationCapability->beatPermutationVisualEvents();
    }
    return {};
}

bool EffectRack::readContinuousTelemetry(
    int slot, ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    if (!juce::isPositiveAndBelow(slot, slotCount)
        || !visualization.isSlotActive(slot)
        || slots[static_cast<size_t>(slot)].bypass->load() >= 0.5f
        || !activeModuleHasCapability(
            slot, ModuleCapability::continuousTelemetry))
        return false;
    const auto& rackSlot = slots[static_cast<size_t>(slot)];
    const auto* module = selectedModule(rackSlot);
    const auto* capability =
        module != nullptr ? module->continuousTelemetryCapability() : nullptr;
    return capability != nullptr
           && capability->readContinuousTelemetry(snapshot);
}

bool EffectRack::readEventTelemetry(
    int slot, EventTelemetrySnapshot& snapshot) const noexcept
{
    if (!juce::isPositiveAndBelow(slot, slotCount)
        || !visualization.isSlotActive(slot)
        || slots[static_cast<size_t>(slot)].bypass->load() >= 0.5f
        || !activeModuleHasCapability(
            slot, ModuleCapability::eventTelemetry))
        return false;
    const auto& rackSlot = slots[static_cast<size_t>(slot)];
    const auto* module = selectedModule(rackSlot);
    const auto* capability =
        module != nullptr ? module->eventTelemetryCapability() : nullptr;
    return capability != nullptr
           && capability->readEventTelemetry(snapshot);
}

juce::Result EffectRack::loadNamModel(int slot, const juce::File& file)
{
    PreparedNamModelLoad load;
    auto result = prepareNamModel(slot, file, load);
    if (result.failed()) return result;
    if (!commitNamModel(load))
    {
        cancelNamModel(load);
        return juce::Result::fail("NAM model commit superseded.");
    }
    return juce::Result::ok();
}

juce::Result EffectRack::prepareNamModel(
    int slot, const juce::File& file, PreparedNamModelLoad& load)
{
    load = {};
    if (!juce::isPositiveAndBelow(slot, slotCount))
        return juce::Result::fail("Invalid NAM slot.");
    auto& rackSlot = slots[static_cast<size_t>(slot)];
    auto* module = selectedModule(rackSlot);
    if (module == nullptr)
        return juce::Result::fail("No synchronized module in slot.");
    auto* namCap = module->namModelCapability();
    if (namCap == nullptr)
        return juce::Result::fail("Slot does not support NAM models.");
    load.slot = slot;
    load.type = rackSlot.activeType;
    load.module = module;
    load.path = file.getFullPathName();
    auto result = namCap->prepareNamModel(file, load.prepared);
    if (result.failed()) load = {};
    return result;
}

bool EffectRack::commitNamModel(PreparedNamModelLoad& load)
{
    if (!juce::isPositiveAndBelow(load.slot, slotCount)
        || load.prepared == nullptr)
        return false;
    auto& rackSlot = slots[static_cast<size_t>(load.slot)];
    auto* module = selectedModule(rackSlot);
    if (module == nullptr || module != load.module
        || rackSlot.activeType != load.type)
        return false;
    auto* namCap = module->namModelCapability();
    if (namCap == nullptr
        || !namCap->commitPreparedNamModel(load.prepared))
        return false;
    const auto latencyCapacity = static_cast<size_t>(
        juce::jmax(0, module->latencySamples()));
    for (auto& channel : rackSlot.latencyBuffer)
        channel.assign(latencyCapacity, 0.0f);
    rackSlot.latencyWritePosition = 0;
    rackSlot.previousLatency = module->latencySamples();
    rackSlot.synchronizedNamPath = load.path;
    rackSlot.observedNamPath = load.path;
    rackSlot.namRestorePending = false;
    setNamPath(load.slot, load.path);
    return true;
}

void EffectRack::cancelNamModel(PreparedNamModelLoad& load) noexcept
{
    if (juce::isPositiveAndBelow(load.slot, slotCount))
    {
        auto& rackSlot = slots[static_cast<size_t>(load.slot)];
        auto* module = selectedModule(rackSlot);
        if (module != nullptr && module == load.module
            && rackSlot.activeType == load.type)
            if (auto* namCap = module->namModelCapability())
                namCap->cancelPreparedNamModel(load.prepared);
    }
    load = {};
}

juce::Result EffectRack::clearNamModel(int slot)
{
    if (!juce::isPositiveAndBelow(slot, slotCount))
        return juce::Result::fail("Invalid NAM slot.");
    auto& rackSlot = slots[static_cast<size_t>(slot)];
    auto* module = selectedModule(rackSlot);
    if (module != nullptr)
        if (auto* namCap = module->namModelCapability())
            namCap->clearNamModel();
    rackSlot.synchronizedNamPath.clear();
    rackSlot.observedNamPath.clear();
    rackSlot.namRestorePending = false;
    setNamPath(slot, {});
    return juce::Result::ok();
}

juce::String EffectRack::namModelPath(int slot) const
{
    if (!juce::isPositiveAndBelow(slot, slotCount)) return {};
    return namPath(slot);
}

juce::String EffectRack::pendingNamModelPath(int slot) const
{
    if (!juce::isPositiveAndBelow(slot, slotCount)) return {};
    const auto& rackSlot = slots[static_cast<size_t>(slot)];
    const auto* module = selectedModule(rackSlot);
    if (module == nullptr || module->namModelCapability() == nullptr)
        return {};
    const auto path = namPath(slot);
    return rackSlot.namRestorePending && path.isNotEmpty()
               ? path : juce::String();
}

void EffectRack::discardPendingNamModel(
    int slot, const juce::String& path)
{
    if (!juce::isPositiveAndBelow(slot, slotCount)) return;
    auto& rackSlot = slots[static_cast<size_t>(slot)];
    if (rackSlot.namRestorePending && rackSlot.observedNamPath == path)
        rackSlot.namRestorePending = false;
}

void EffectRack::reloadNamModels()
{
    for (int slot = 0; slot < slotCount; ++slot)
    {
        const auto path = pendingNamModelPath(slot);
        if (path.isEmpty()) continue;
        PreparedNamModelLoad load;
        if (prepareNamModel(slot, juce::File(path), load).failed())
        {
            discardPendingNamModel(slot, path);
            continue;
        }
        if (!commitNamModel(load))
            cancelNamModel(load);
    }
}
} // namespace megadsp
