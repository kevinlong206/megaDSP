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
    impulseResponsePath = source.impulseResponsePath;
    impulseResponsePath2 = source.impulseResponsePath2;
    namModelPath = source.namModelPath;
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
        slot.impulseResponsePath.clear();
        slot.impulseResponsePath2.clear();
        slot.namModelPath.clear();
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
        if (source.impulseResponsePath.isNotEmpty())
            slot.setProperty(
                "impulseResponse", source.impulseResponsePath, nullptr);
        if (source.impulseResponsePath2.isNotEmpty())
            slot.setProperty(
                "impulseResponse2", source.impulseResponsePath2, nullptr);
        if (source.namModelPath.isNotEmpty())
            slot.setProperty("namModel", source.namModelPath, nullptr);
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
        destination.impulseResponsePath =
            child.getProperty("impulseResponse").toString();
        destination.impulseResponsePath2 =
            child.getProperty("impulseResponse2").toString();
        destination.namModelPath =
            child.getProperty("namModel").toString();
    }
}
} // namespace megadsp
