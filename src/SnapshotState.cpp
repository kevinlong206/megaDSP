#include "SnapshotState.h"

namespace megadsp
{
RackSlotState::RackSlotState()
{
    for (auto& control : controls)
        control.store(0.5f, std::memory_order_relaxed);
}

void RackSlotState::copyFrom(const RackSlotState& source)
{
    type.store(source.type.load(std::memory_order_relaxed),
               std::memory_order_relaxed);
    bypass.store(source.bypass.load(std::memory_order_relaxed),
                 std::memory_order_relaxed);
    for (size_t index = 0; index < controls.size(); ++index)
        controls[index].store(
            source.controls[index].load(std::memory_order_relaxed),
            std::memory_order_relaxed);
    auto eq = source.equalizer.load();
    eq.clearAudition();
    equalizer.store(eq);
    impulseResponsePath = source.impulseResponsePath;
    impulseResponsePath2 = source.impulseResponsePath2;
    convolutionImpulseBankPaths = source.convolutionImpulseBankPaths;
    convolutionActiveBank = source.convolutionActiveBank;
    convolutionWetBufferSamples = source.convolutionWetBufferSamples;
    namModelPath = source.namModelPath;
    namCabinetPath = source.namCabinetPath;
    namCabinetEnabled = source.namCabinetEnabled;
    namCabinetStateStored = source.namCabinetStateStored;
}

void RackState::copyFrom(const RackState& source)
{
    for (size_t index = 0; index < slots.size(); ++index)
        slots[index].copyFrom(source.slots[index]);
    inputGainDb.store(source.inputGainDb.load(std::memory_order_relaxed),
                      std::memory_order_relaxed);
    outputGainDb.store(source.outputGainDb.load(std::memory_order_relaxed),
                       std::memory_order_relaxed);
    populated.store(source.populated.load(std::memory_order_relaxed),
                    std::memory_order_relaxed);
    ready.store(false, std::memory_order_relaxed);
    error.clear();
}

void RackState::clear()
{
    for (auto& slot : slots)
    {
        slot.type.store(0.0f, std::memory_order_relaxed);
        slot.bypass.store(0.0f, std::memory_order_relaxed);
        for (auto& control : slot.controls)
            control.store(0.5f, std::memory_order_relaxed);
        slot.equalizer.store({});
        slot.impulseResponsePath.clear();
        slot.impulseResponsePath2.clear();
        slot.convolutionImpulseBankPaths.fill({});
        slot.convolutionActiveBank = 0;
        slot.convolutionWetBufferSamples = 2048;
        slot.namModelPath.clear();
        slot.namCabinetPath.clear();
        slot.namCabinetEnabled = false;
        slot.namCabinetStateStored = false;
    }
    inputGainDb.store(0.0f, std::memory_order_relaxed);
    outputGainDb.store(0.0f, std::memory_order_relaxed);
    populated.store(false, std::memory_order_relaxed);
    ready.store(false, std::memory_order_relaxed);
    error.clear();
}

juce::ValueTree RackState::toValueTree(int index) const
{
    juce::ValueTree snapshot("SNAPSHOT");
    snapshot.setProperty("index", index, nullptr);
    snapshot.setProperty(
        "populated", populated.load(std::memory_order_relaxed), nullptr);
    snapshot.setProperty(
        "inputGain", inputGainDb.load(std::memory_order_relaxed), nullptr);
    snapshot.setProperty(
        "outputGain", outputGainDb.load(std::memory_order_relaxed), nullptr);

    for (int slotIndex = 0; slotIndex < slotCount; ++slotIndex)
    {
        const auto& source = slots[static_cast<size_t>(slotIndex)];
        juce::ValueTree slot("SLOT");
        slot.setProperty("index", slotIndex, nullptr);
        slot.setProperty(
            "type", source.type.load(std::memory_order_relaxed), nullptr);
        slot.setProperty(
            "bypass", source.bypass.load(std::memory_order_relaxed), nullptr);
        for (int control = 0; control < controlsPerSlot; ++control)
            slot.setProperty(
                "control" + juce::String(control + 1),
                source.controls[static_cast<size_t>(control)].load(
                    std::memory_order_relaxed),
                nullptr);
        if (const auto eq = source.equalizer.load(); eq.native)
            slot.addChild(eq.toValueTree(), -1, nullptr);
        if (source.impulseResponsePath.isNotEmpty())
            slot.setProperty(
                "impulseResponse", source.impulseResponsePath, nullptr);
        if (source.impulseResponsePath2.isNotEmpty())
            slot.setProperty(
                "impulseResponse2", source.impulseResponsePath2, nullptr);
        for (int bank = 0; bank < 3; ++bank)
            if (source.convolutionImpulseBankPaths[
                    static_cast<size_t>(bank)].isNotEmpty())
                slot.setProperty(
                    "convolutionBank" + juce::String(bank + 1),
                    source.convolutionImpulseBankPaths[
                        static_cast<size_t>(bank)],
                    nullptr);
        slot.setProperty(
            "convolutionActiveBank",
            juce::jlimit(0, 2, source.convolutionActiveBank), nullptr);
        slot.setProperty(
            "convolutionWetBuffer",
            source.convolutionWetBufferSamples, nullptr);
        if (source.namModelPath.isNotEmpty())
            slot.setProperty("namModel", source.namModelPath, nullptr);
        if (source.namCabinetStateStored)
        {
            slot.setProperty(
                "namCabinetEnabled", source.namCabinetEnabled, nullptr);
            if (source.namCabinetPath.isNotEmpty())
                slot.setProperty(
                    "namCabinet", source.namCabinetPath, nullptr);
        }
        snapshot.addChild(slot, -1, nullptr);
    }
    return snapshot;
}

void RackState::fromValueTree(const juce::ValueTree& snapshot)
{
    clear();
    populated.store(
        static_cast<bool>(snapshot.getProperty("populated", false)),
        std::memory_order_relaxed);
    inputGainDb.store(
        static_cast<float>(
            static_cast<double>(snapshot.getProperty("inputGain", 0.0))),
        std::memory_order_relaxed);
    outputGainDb.store(
        static_cast<float>(
            static_cast<double>(snapshot.getProperty("outputGain", 0.0))),
        std::memory_order_relaxed);

    for (const auto child : snapshot)
    {
        if (!child.hasType("SLOT"))
            continue;
        const auto slotIndex = static_cast<int>(child.getProperty("index", -1));
        if (!juce::isPositiveAndBelow(slotIndex, slotCount))
            continue;
        auto& destination = slots[static_cast<size_t>(slotIndex)];
        destination.type.store(
            static_cast<float>(
                static_cast<double>(child.getProperty("type", 0.0))),
            std::memory_order_relaxed);
        destination.bypass.store(
            static_cast<float>(
                static_cast<double>(child.getProperty("bypass", 0.0))),
            std::memory_order_relaxed);
        for (int control = 0; control < controlsPerSlot; ++control)
            destination.controls[static_cast<size_t>(control)].store(
                static_cast<float>(static_cast<double>(child.getProperty(
                    "control" + juce::String(control + 1), 0.5))),
                std::memory_order_relaxed);
        destination.equalizer.store(
            ReEqSettings::fromValueTree(child.getChildWithName("REEQ2")));
        destination.impulseResponsePath =
            child.getProperty("impulseResponse").toString();
        destination.impulseResponsePath2 =
            child.getProperty("impulseResponse2").toString();
        destination.convolutionActiveBank = juce::jlimit(
            0, 2,
            static_cast<int>(
                child.getProperty("convolutionActiveBank", 0)));
        destination.convolutionWetBufferSamples = static_cast<int>(
            child.getProperty("convolutionWetBuffer", 2048));
        for (int bank = 0; bank < 3; ++bank)
            destination.convolutionImpulseBankPaths[
                static_cast<size_t>(bank)] =
                    child.getProperty(
                        "convolutionBank" + juce::String(bank + 1))
                        .toString();
        if (destination.convolutionImpulseBankPaths[0].isEmpty())
            destination.convolutionImpulseBankPaths[0] =
                destination.impulseResponsePath;
        destination.namModelPath =
            child.getProperty("namModel").toString();
        destination.namCabinetStateStored =
            child.hasProperty("namCabinet")
            || child.hasProperty("namCabinetEnabled");
        destination.namCabinetPath =
            child.getProperty("namCabinet").toString();
        destination.namCabinetEnabled = static_cast<bool>(
            child.getProperty("namCabinetEnabled", false));
    }
}
} // namespace megadsp
