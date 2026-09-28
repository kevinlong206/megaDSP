#pragma once

#include "ModuleRegistry.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace megadsp
{
using ControlValues = std::array<float, controlsPerSlot>;

class NamModelCapability;

struct ProcessEnvironment
{
    const juce::AudioBuffer<float>* sidechain = nullptr;
    double bpm = 120.0;
    bool captureTelemetry = false;
    bool nonRealtime = false;
};

enum class DryPathTiming
{
    aligned,
    immediate
};

class DspModule
{
public:
    virtual ~DspModule() = default;
    virtual void prepare(const juce::dsp::ProcessSpec& spec) = 0;
    virtual void reset() = 0;
    virtual void process(juce::AudioBuffer<float>& buffer,
                         const ControlValues& controls,
                         const ProcessEnvironment& environment) = 0;
    virtual int latencySamples() const { return 0; }
    virtual DryPathTiming dryPathTiming() const
    {
        return DryPathTiming::aligned;
    }
    virtual double tailSeconds(const ControlValues&) const { return 0.0; }
    virtual double snapshotSpillSeconds(
        const ControlValues& controls) const
    {
        return juce::jlimit(0.0, 8.0, tailSeconds(controls));
    }
    virtual float meterValue() const { return 0.0f; }
    virtual float detectorValue() const { return -100.0f; }
    virtual ImpulseResponseCapability* impulseResponseCapability() noexcept
    {
        return nullptr;
    }
    virtual const ImpulseResponseCapability*
        impulseResponseCapability() const noexcept
    {
        return nullptr;
    }
    virtual GrainVisualizationCapability*
        grainVisualizationCapability() noexcept
    {
        return nullptr;
    }
    virtual const GrainVisualizationCapability*
        grainVisualizationCapability() const noexcept
    {
        return nullptr;
    }
    virtual BeatPermutationVisualizationCapability*
        beatPermutationVisualizationCapability() noexcept
    {
        return nullptr;
    }
    virtual const BeatPermutationVisualizationCapability*
        beatPermutationVisualizationCapability() const noexcept
    {
        return nullptr;
    }
    virtual ContinuousTelemetryCapability*
        continuousTelemetryCapability() noexcept
    {
        return nullptr;
    }
    virtual const ContinuousTelemetryCapability*
        continuousTelemetryCapability() const noexcept
    {
        return nullptr;
    }
    virtual EventTelemetryCapability*
        eventTelemetryCapability() noexcept
    {
        return nullptr;
    }
    virtual const EventTelemetryCapability*
        eventTelemetryCapability() const noexcept
    {
        return nullptr;
    }
    virtual NamModelCapability* namModelCapability() noexcept
    {
        return nullptr;
    }
    virtual const NamModelCapability* namModelCapability() const noexcept
    {
        return nullptr;
    }
    ModuleCapability capabilities() const noexcept
    {
        auto result = ModuleCapability::none;
        if (impulseResponseCapability() != nullptr)
            result = result | ModuleCapability::impulseResponse;
        if (grainVisualizationCapability() != nullptr)
            result = result | ModuleCapability::grainVisualization;
        if (beatPermutationVisualizationCapability() != nullptr)
            result = result
                     | ModuleCapability::beatPermutationVisualization;
        if (continuousTelemetryCapability() != nullptr)
            result = result | ModuleCapability::continuousTelemetry;
        if (eventTelemetryCapability() != nullptr)
            result = result | ModuleCapability::eventTelemetry;
        if (namModelCapability() != nullptr)
            result = result | ModuleCapability::neuralAmpModel;
        return result;
    }
};
} // namespace megadsp
