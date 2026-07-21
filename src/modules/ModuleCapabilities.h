#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>
#include <memory>

namespace megadsp
{
enum class ModuleCapability : std::uint32_t
{
    none = 0,
    impulseResponse = 1u << 0,
    grainVisualization = 1u << 1
};

constexpr ModuleCapability operator|(
    ModuleCapability first, ModuleCapability second) noexcept
{
    return static_cast<ModuleCapability>(
        static_cast<std::uint32_t>(first)
        | static_cast<std::uint32_t>(second));
}

constexpr bool hasCapability(
    ModuleCapability flags, ModuleCapability capability) noexcept
{
    return (static_cast<std::uint32_t>(flags)
            & static_cast<std::uint32_t>(capability)) != 0;
}

constexpr ModuleCapability allModuleCapabilities =
    ModuleCapability::impulseResponse
    | ModuleCapability::grainVisualization;

constexpr int impulseResponsePreviewSize = 256;
using ImpulseResponsePreview =
    std::array<float, impulseResponsePreviewSize>;

class PreparedImpulseResponse
{
public:
    virtual ~PreparedImpulseResponse() = default;
};

using PreparedImpulseResponsePtr =
    std::unique_ptr<PreparedImpulseResponse>;

class ImpulseResponseCapability
{
public:
    virtual ~ImpulseResponseCapability() = default;
    virtual juce::Result loadImpulseResponse(const juce::File&) = 0;
    virtual juce::Result prepareImpulseResponse(
        const juce::File&, PreparedImpulseResponsePtr&) = 0;
    virtual bool commitPreparedImpulseResponse(
        PreparedImpulseResponsePtr&) = 0;
    virtual void cancelPreparedImpulseResponse(
        PreparedImpulseResponsePtr&) noexcept = 0;
    virtual void clearImpulseResponse() = 0;
    virtual bool hasImpulseResponse() const noexcept = 0;
    virtual bool isImpulseResponseReady() const noexcept = 0;
    virtual juce::String impulseResponseName() const = 0;
    virtual ImpulseResponsePreview impulseResponsePreview() const = 0;
    virtual double currentImpulseResponseTailSeconds() const noexcept = 0;
};

struct GrainVisualEvent
{
    std::uint32_t sequence = 0;
    float historyPosition = 0.0f;
    float durationSeconds = 0.0f;
    float progress = 1.0f;
    float pan = 0.0f;
    float filter = 1.0f;
    bool reverse = false;
};

constexpr int grainVisualEventCount = 24;
using GrainVisualEvents =
    std::array<GrainVisualEvent, grainVisualEventCount>;

class GrainVisualizationCapability
{
public:
    virtual ~GrainVisualizationCapability() = default;
    virtual GrainVisualEvents grainVisualEvents() const noexcept = 0;
};
} // namespace megadsp
