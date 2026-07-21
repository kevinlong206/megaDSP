#include "Parameters.h"

#include <cmath>

namespace megadsp
{
namespace
{
float exponential(float low, float high, float normalized)
{
    return low * std::pow(high / low, juce::jlimit(0.0f, 1.0f, normalized));
}

float exponentialNormalized(float low, float high, float plain)
{
    return juce::jlimit(0.0f, 1.0f,
        std::log(juce::jlimit(low, high, plain) / low) / std::log(high / low));
}
} // namespace

const std::array<ModuleDescriptor, moduleTypeCount>& moduleDescriptors()
{
    static const std::array<ModuleDescriptor, moduleTypeCount> descriptors = []
    {
        std::array<ModuleDescriptor, moduleTypeCount> result {};
        for (int stableType = 0; stableType < moduleTypeCount; ++stableType)
        {
            const auto type = static_cast<ModuleType>(stableType);
            const auto& definition = moduleDefinition(type);
            auto& descriptor = result[static_cast<size_t>(stableType)];
            descriptor.type = definition.type;
            descriptor.name = definition.displayName;
            for (int control = 0; control < controlsPerSlot; ++control)
                descriptor.controlNames[static_cast<size_t>(control)] =
                    definition.controls[static_cast<size_t>(control)].name;
        }
        return result;
    }();
    return descriptors;
}

const ModuleDescriptor& descriptorFor(ModuleType type)
{
    const auto stableType = juce::jlimit(
        0, moduleTypeCount - 1, static_cast<int>(type));
    return moduleDescriptors()[static_cast<size_t>(stableType)];
}

juce::ValueTree migrateStateToCurrentSchema(juce::ValueTree state)
{
    const auto sourceSchema = static_cast<int>(
        state.getProperty("schemaVersion", 0));
    auto value = [&state](const juce::String& id, float fallback)
    {
        for (const auto child : state)
            if (child.getProperty("id").toString() == id)
                return static_cast<float>(
                    static_cast<double>(child.getProperty("value", fallback)));
        return fallback;
    };
    auto setValue = [&state](const juce::String& id, float newValue)
    {
        for (auto child : state)
            if (child.getProperty("id").toString() == id)
            {
                child.setProperty("value", newValue, nullptr);
                return;
            }
    };

    if (sourceSchema == 2)
    {
        for (int slot = 0; slot < slotCount; ++slot)
        {
            const auto type = static_cast<ModuleType>(juce::roundToInt(
                value(slotParameterId(slot, "type"), 0.0f)));
            if (type == ModuleType::compressor)
                setValue(controlParameterId(slot, 8), 0.0f);
            else if (type == ModuleType::limiter)
                setValue(controlParameterId(slot, 4), 0.0f);
        }
    }
    if (sourceSchema < 5)
    {
        for (int slot = 0; slot < slotCount; ++slot)
        {
            const auto type = static_cast<ModuleType>(juce::roundToInt(
                value(slotParameterId(slot, "type"), 0.0f)));
            if (type != ModuleType::equalizer)
                continue;
            const auto legacyLowRolloff =
                sourceSchema == 4
                && value(controlParameterId(slot, 0), 0.5f) <= 0.075f;
            const auto legacyHighRolloff =
                sourceSchema == 4
                && value(controlParameterId(slot, 6), 0.5f) >= 0.925f;
            setValue(controlParameterId(slot, 10),
                     legacyLowRolloff ? 1.0f : 0.0f);
            setValue(controlParameterId(slot, 11),
                     legacyHighRolloff ? 1.0f : 0.0f);
        }
    }
    if (sourceSchema == 5)
    {
        for (int slot = 0; slot < slotCount; ++slot)
        {
            const auto type = static_cast<ModuleType>(juce::roundToInt(
                value(slotParameterId(slot, "type"), 0.0f)));
            if (type != ModuleType::randomGranulizer)
                continue;
            const auto legacySize = exponential(
                15.0f, 500.0f,
                value(controlParameterId(slot, 1), 0.5f));
            const auto migratedSize = exponentialNormalized(
                50.0f, 2000.0f, legacySize);
            setValue(controlParameterId(slot, 1), migratedSize);
            setValue(controlParameterId(slot, 4), migratedSize);
        }
    }
    if (sourceSchema < 7)
    {
        for (int slot = 0; slot < slotCount; ++slot)
        {
            const auto type = static_cast<ModuleType>(juce::roundToInt(
                value(slotParameterId(slot, "type"), 0.0f)));
            if (type == ModuleType::algorithmicReverb)
            {
                const auto mix = juce::jlimit(
                    0.0f, 1.0f,
                    value(controlParameterId(slot, 3), 0.0f));
                setValue(controlParameterId(slot, 2), std::cos(
                    mix * juce::MathConstants<float>::halfPi));
                setValue(controlParameterId(slot, 3), std::sin(
                    mix * juce::MathConstants<float>::halfPi));
            }
            else if (type == ModuleType::convolutionReverb)
            {
                const auto mix = juce::jlimit(
                    0.0f, 1.0f,
                    value(controlParameterId(slot, 2), 0.0f));
                setValue(controlParameterId(slot, 2), std::sin(
                    mix * juce::MathConstants<float>::halfPi));
                setValue(controlParameterId(slot, 4), std::cos(
                    mix * juce::MathConstants<float>::halfPi));
            }
        }
    }
    for (int slot = 0; slot < slotCount; ++slot)
        state.removeProperty("advanced" + juce::String(slot), nullptr);
    state.setProperty("schemaVersion", stateSchemaVersion, nullptr);
    return state;
}

juce::String slotParameterId(int slot, const juce::String& suffix)
{
    return "slot" + juce::String(slot + 1).paddedLeft('0', 2) + "." + suffix;
}

juce::String controlParameterId(int slot, int control)
{
    return slotParameterId(slot, "control" + juce::String(control + 1).paddedLeft('0', 2));
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using APF = juce::AudioParameterFloat;
    using APC = juce::AudioParameterChoice;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    juce::StringArray moduleNames;
    for (int type = 0; type < moduleTypeCount; ++type)
        moduleNames.add(moduleDefinition(static_cast<ModuleType>(type)).displayName);

    for (int slot = 0; slot < slotCount; ++slot)
    {
        const auto prefix = "Slot " + juce::String(slot + 1) + " ";
        layout.add(std::make_unique<APC>(
            juce::ParameterID(slotParameterId(slot, "type"), 1),
            prefix + "Module", moduleNames, 0,
            juce::AudioParameterChoiceAttributes().withAutomatable(false)));
        layout.add(std::make_unique<APF>(
            juce::ParameterID(slotParameterId(slot, "bypass"), 1),
            prefix + "Bypass", juce::NormalisableRange<float>(0.0f, 1.0f),
            0.0f));

        for (int control = 0; control < controlsPerSlot; ++control)
        {
            layout.add(std::make_unique<APF>(
                juce::ParameterID(controlParameterId(slot, control), 1),
                prefix + "Control " + juce::String(control + 1),
                juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));
        }
    }

    layout.add(std::make_unique<APF>(
        juce::ParameterID("global.input", 1), "Input Gain",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f));
    layout.add(std::make_unique<APF>(
        juce::ParameterID("global.output", 1), "Output Gain",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f));

    return layout;
}
} // namespace megadsp
