#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <type_traits>

namespace megadsp
{
enum class ModuleCapability : std::uint32_t
{
    none = 0,
    impulseResponse = 1u << 0,
    grainVisualization = 1u << 1,
    beatPermutationVisualization = 1u << 2,
    continuousTelemetry = 1u << 3,
    eventTelemetry = 1u << 4
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
    | ModuleCapability::grainVisualization
    | ModuleCapability::beatPermutationVisualization
    | ModuleCapability::continuousTelemetry
    | ModuleCapability::eventTelemetry;

template <typename Snapshot>
class FixedAtomicSnapshot
{
public:
    // One realtime writer may publish while readers take bounded snapshots.
    static_assert(std::is_trivially_copyable_v<Snapshot>);
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

    void publish(const Snapshot& snapshot) noexcept
    {
        publication.fetch_add(1, std::memory_order_acq_rel);
        const auto* source =
            reinterpret_cast<const std::byte*>(std::addressof(snapshot));
        for (size_t index = 0; index < wordCount; ++index)
        {
            std::uint32_t word = 0;
            const auto offset = index * sizeof(word);
            const auto bytes = juce::jmin(
                sizeof(word), sizeof(Snapshot) - offset);
            std::memcpy(&word, source + offset, bytes);
            words[index].store(word, std::memory_order_relaxed);
        }
        publication.fetch_add(1, std::memory_order_release);
    }

    bool read(Snapshot& destination) const noexcept
    {
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            const auto before =
                publication.load(std::memory_order_acquire);
            if ((before & 1u) != 0)
                continue;

            Snapshot candidate {};
            auto* target =
                reinterpret_cast<std::byte*>(std::addressof(candidate));
            for (size_t index = 0; index < wordCount; ++index)
            {
                const auto word =
                    words[index].load(std::memory_order_relaxed);
                const auto offset = index * sizeof(word);
                const auto bytes = juce::jmin(
                    sizeof(word), sizeof(Snapshot) - offset);
                std::memcpy(target + offset, &word, bytes);
            }

            if (publication.load(std::memory_order_acquire) == before)
            {
                destination = candidate;
                return true;
            }
        }
        return false;
    }

    void clear() noexcept
    {
        publish(Snapshot {});
    }

private:
    static constexpr size_t wordCount =
        (sizeof(Snapshot) + sizeof(std::uint32_t) - 1)
        / sizeof(std::uint32_t);

    std::array<std::atomic<std::uint32_t>, wordCount> words {};
    std::atomic<std::uint32_t> publication { 0 };
};

constexpr size_t continuousTelemetryValueCapacity = 8;
constexpr size_t continuousTelemetryHistoryCapacity = 64;
constexpr size_t continuousTelemetryHistoryValueCapacity = 4;

struct ContinuousTelemetrySnapshot
{
    std::uint64_t sequence = 0;
    // Modules define value and history-lane meanings with local enums.
    std::uint32_t valueCount = 0;
    std::array<float, continuousTelemetryValueCapacity> values {};
    std::uint32_t historyValueCount = 0;
    std::uint32_t historyCount = 0;
    // The next circular-history element to replace.
    std::uint32_t historyWritePosition = 0;
    std::array<
        std::array<float, continuousTelemetryHistoryCapacity>,
        continuousTelemetryHistoryValueCapacity> history {};
};

inline void appendContinuousTelemetryHistory(
    ContinuousTelemetrySnapshot& snapshot,
    const std::array<float, continuousTelemetryHistoryValueCapacity>& values,
    std::uint32_t valueCount) noexcept
{
    const auto lanes = juce::jmin(
        valueCount,
        static_cast<std::uint32_t>(
            continuousTelemetryHistoryValueCapacity));
    const auto position = snapshot.historyWritePosition
        % static_cast<std::uint32_t>(continuousTelemetryHistoryCapacity);
    snapshot.historyValueCount = lanes;
    for (std::uint32_t lane = 0; lane < lanes; ++lane)
        snapshot.history[lane][position] = values[lane];
    snapshot.historyCount = juce::jmin(
        snapshot.historyCount + 1,
        static_cast<std::uint32_t>(continuousTelemetryHistoryCapacity));
    snapshot.historyWritePosition =
        (position + 1)
        % static_cast<std::uint32_t>(continuousTelemetryHistoryCapacity);
}

inline float continuousTelemetryHistoryValue(
    const ContinuousTelemetrySnapshot& snapshot,
    std::uint32_t lane, std::uint32_t chronologicalIndex) noexcept
{
    if (lane >= snapshot.historyValueCount
        || chronologicalIndex >= snapshot.historyCount)
        return 0.0f;
    const auto capacity =
        static_cast<std::uint32_t>(continuousTelemetryHistoryCapacity);
    const auto oldest = snapshot.historyCount < capacity
        ? 0u : snapshot.historyWritePosition % capacity;
    return snapshot.history[lane][
        (oldest + chronologicalIndex) % capacity];
}

constexpr size_t eventTelemetryCapacity = 32;

struct TelemetryEvent
{
    // Event kind, flags, positions, and values are module-defined fixed fields.
    std::uint64_t sequence = 0;
    std::uint32_t kind = 0;
    std::uint32_t flags = 0;
    float progress = 0.0f;
    std::array<float, 3> position {};
    std::array<float, 4> values {};
};

struct EventTelemetrySnapshot
{
    std::uint64_t sequence = 0;
    std::uint32_t eventCount = 0;
    std::array<TelemetryEvent, eventTelemetryCapacity> events {};
};

class ContinuousTelemetryCapability
{
public:
    virtual ~ContinuousTelemetryCapability() = default;
    virtual bool readContinuousTelemetry(
        ContinuousTelemetrySnapshot&) const noexcept = 0;
};

class EventTelemetryCapability
{
public:
    virtual ~EventTelemetryCapability() = default;
    virtual bool readEventTelemetry(
        EventTelemetrySnapshot&) const noexcept = 0;
};

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

struct BeatPermutationVisualEvent
{
    std::uint32_t sequence = 0;
    float sourcePosition = 0.0f;
    float progress = 1.0f;
    float windowSize = 0.0f;
    float repeatCount = 0.0f;
    float gate = 1.0f;
    float stereoBias = 0.0f;
    int pattern = 0;
    bool reverse = false;
};

constexpr int beatPermutationVisualEventCount = 16;
using BeatPermutationVisualEvents =
    std::array<BeatPermutationVisualEvent, beatPermutationVisualEventCount>;

class BeatPermutationVisualizationCapability
{
public:
    virtual ~BeatPermutationVisualizationCapability() = default;
    virtual BeatPermutationVisualEvents
        beatPermutationVisualEvents() const noexcept = 0;
};
} // namespace megadsp
