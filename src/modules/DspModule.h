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

struct ProcessEnvironment
{
    const juce::AudioBuffer<float>* sidechain = nullptr;
    double bpm = 120.0;
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
    virtual double tailSeconds(const ControlValues&) const { return 0.0; }
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
    ModuleCapability capabilities() const noexcept
    {
        auto result = ModuleCapability::none;
        if (impulseResponseCapability() != nullptr)
            result = result | ModuleCapability::impulseResponse;
        if (grainVisualizationCapability() != nullptr)
            result = result | ModuleCapability::grainVisualization;
        return result;
    }
};
} // namespace megadsp
