#pragma once

#include "ModuleCapabilities.h"

#include <juce_core/juce_core.h>

#include <array>
#include <memory>
#include <optional>

namespace megadsp
{
constexpr int slotCount = 8;
constexpr int controlsPerSlot = 12;
constexpr int stateSchemaVersion = 7;
constexpr int moduleTypeCount = 16;

enum class ModuleType
{
    empty = 0,
    equalizer = 1,
    compressor = 2,
    saturator = 3,
    delay = 4,
    limiter = 5,
    algorithmicReverb = 6,
    stereoWidth = 7,
    midSideDecoder = 8,
    tremolo = 9,
    rotarySpeaker = 10,
    convolutionReverb = 11,
    dynamicEqualizer = 12,
    randomGranulizer = 13,
    vintageChorus = 14,
    analogTape = 15
};

enum class ModuleCategory
{
    none,
    eqAndFilters,
    dynamics,
    saturationAndColor,
    delayAndEcho,
    reverbAndSpace,
    modulation,
    stereoAndUtility,
    glitchAndCreative
};

enum class ModulePresentation
{
    none = 0,
    equalizer,
    compressor,
    saturator,
    delay,
    limiter,
    algorithmicReverb,
    stereoWidth,
    midSideDecoder,
    tremolo,
    rotarySpeaker,
    convolutionReverb,
    dynamicEqualizer,
    randomGranulizer,
    vintageChorus,
    analogTape,
    count
};

enum class ControlKind
{
    rotary,
    horizontal,
    level,
    choice,
    toggle
};

struct ControlMetadata
{
    const char* label;
    ControlKind kind;
    bool essential;
    const char* group;
    const char* tooltip;
};

struct ControlOptions
{
    static constexpr int capacity = 8;
    std::array<const char*, capacity> values {};
    int count = 0;

    juce::StringArray strings() const;
};

struct ControlDefinition
{
    const char* name;
    ControlMetadata metadata;
    float defaultValue;
    ControlOptions options;

    bool isActive() const;
};

class DspModule;
using ModuleFactory = std::unique_ptr<DspModule> (*)();

struct ModuleDefinition
{
    ModuleType type;
    const char* displayName;
    ModuleCategory category;
    const char* description;
    const char* searchTags;
    ModulePresentation presentation;
    std::array<ControlDefinition, controlsPerSlot> controls;
    ModuleFactory factory;
    ModuleCapability capabilities;
};

const std::array<ModuleDefinition, moduleTypeCount>& moduleRegistry();
const ModuleDefinition* findModuleDefinition(ModuleType type);
const ModuleDefinition& moduleDefinition(ModuleType type);
const char* moduleCategoryName(ModuleCategory category);
std::unique_ptr<DspModule> createDspModule(ModuleType type);
juce::StringArray validateModuleRegistry();

const ControlMetadata& controlMetadata(ModuleType type, int control);
std::array<float, controlsPerSlot> moduleDefaults(ModuleType type);
juce::StringArray controlOptions(ModuleType type, int control);
juce::String formatControlValue(ModuleType type, int control, float normalized);
std::optional<float> parseControlValue(ModuleType type, int control,
                                       const juce::String& text);
bool isControlContextuallyVisible(
    ModuleType type, int control,
    const std::array<float, controlsPerSlot>& values);
bool isControlContextuallyEnabled(
    ModuleType type, int control,
    const std::array<float, controlsPerSlot>& values,
    bool hasStereoOutput, bool hasExternalSidechain);
int discreteIndex(float normalized, int optionCount);
float discreteValue(int index, int optionCount);
bool equalizerLowIsHighPass(float normalizedMode);
bool equalizerHighIsLowPass(float normalizedMode);
const std::array<std::array<float, 16>, 3>& reverbEarlyMilliseconds();
std::array<float, 2> reverbDecayRatios(int mode, float damping);
} // namespace megadsp
