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
constexpr int stateSchemaVersion = 13;
constexpr int moduleTypeCount = 60;

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
    beatPermuter = 15,
    spectralPrism = 16,
    resonantMatrix = 17,
    wavefoldGarden = 18,
    gateExpander = 19,
    transientDesigner = 20,
    multibandCompressor = 21,
    studioPhaser = 22,
    studioFlanger = 23,
    diffusionDelay = 24,
    pitchBloom = 25,
    frequencyLab = 26,
    spatialOrbit = 27,
    signalDecay = 28,
    analogTape = 29,
    resonanceTamer = 30,
    spectralBalance = 31,
    phaseCoherence = 32,
    loudnessRider = 33,
    adaptiveClipper = 34,
    spectralDelayCanvas = 35,
    formantForge = 36,
    harmonicMirage = 37,
    chaosField = 38,
    timeMosaic = 39,
    guitarCabinet = 40,
    chordLoom = 41,
    volumeSwell = 42,
    spectralNebula = 43,
    midnightCurrent = 44,
    autoWah = 45,
    resonantSculpture = 46,
    neuralAmpModeler = 47,
    stageVocal = 48,
    vinylSimulator = 49,
    optoLeveler = 50,
    fetLimiter = 51,
    programEqualizer = 52,
    studioPlate = 53,
    velvetDrive = 54,
    wormholeConvolution = 55,
    bitcrusher = 56,
    portalPermuter = 57,
    irMosaic = 58,
    guitarTuner = 59
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
    glitchAndCreative,
    guitar
};

static_assert(
    static_cast<int>(ModuleType::guitarTuner) + 1 == moduleTypeCount);

enum class CompactVisualization
{
    response,
    dynamics,
    transfer,
    delay,
    space,
    impulse,
    wetSpectrum,
    stereo,
    modulation,
    grains,
    slices,
    spectral,
    resonator,
    transport,
    pitch,
    envelope,
    cabinet,
    tuner,
    bitcrusher,
    chaos,
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
    static constexpr int capacity = 24;
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
    std::array<ControlDefinition, controlsPerSlot> controls;
    ModuleFactory factory;
    ModuleCapability capabilities;
};

const std::array<ModuleDefinition, moduleTypeCount>& moduleRegistry();
const ModuleDefinition* findModuleDefinition(ModuleType type);
const ModuleDefinition& moduleDefinition(ModuleType type);
CompactVisualization compactVisualization(ModuleType type);
bool compactVisualizationShowsGainReduction(ModuleType);
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
enum class EqualizerBandMode
{
    bell,
    shelf,
    cut
};
EqualizerBandMode equalizerBandMode(float normalizedMode);
bool equalizerLowIsHighPass(float normalizedMode);
bool equalizerHighIsLowPass(float normalizedMode);
const std::array<std::array<float, 16>, 3>& reverbEarlyMilliseconds();
std::array<float, 2> reverbDecayRatios(int mode, float damping);
} // namespace megadsp
