#pragma once

#include "modules/ModuleRegistry.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

namespace megadsp
{
struct ModuleDescriptor
{
    ModuleType type;
    const char* name;
    std::array<const char*, controlsPerSlot> controlNames;
};

const std::array<ModuleDescriptor, moduleTypeCount>& moduleDescriptors();
const ModuleDescriptor& descriptorFor(ModuleType type);
juce::ValueTree migrateStateToCurrentSchema(juce::ValueTree state);
juce::String slotParameterId(int slot, const juce::String& suffix);
juce::String controlParameterId(int slot, int control);
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
} // namespace megadsp
