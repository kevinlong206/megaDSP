#pragma once

#include "modules/ModuleRegistry.h"

#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <atomic>

namespace megadsp
{
inline constexpr int snapshotCount = 8;

struct RackSlotState
{
    std::atomic<float> type { 0.0f };
    std::atomic<float> bypass { 0.0f };
    std::array<std::atomic<float>, controlsPerSlot> controls;
    juce::String impulseResponsePath;
    juce::String namModelPath;

    RackSlotState();
    void copyFrom(const RackSlotState&);
};

struct RackState
{
    std::array<RackSlotState, slotCount> slots;
    std::atomic<float> inputGainDb { 0.0f };
    std::atomic<float> outputGainDb { 0.0f };
    std::atomic<bool> populated { false };
    std::atomic<bool> ready { false };
    juce::String error;

    void copyFrom(const RackState&);
    void clear();
    juce::ValueTree toValueTree(int index) const;
    void fromValueTree(const juce::ValueTree&);
};
} // namespace megadsp
