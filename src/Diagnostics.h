#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace megadsp
{
enum class DiagnosticEvent : std::uint32_t
{
    audioConfiguration = 1,
    latencyChanged,
    topologyChanged,
    stateRejected,
    impulseResponseFailed,
    snapshotRecallWorkload,
    audioCallbackWorkload,
    snapshotGraphWorkload,
    moduleWorkload
};

class Diagnostics final : private juce::Thread
{
public:
    static Diagnostics& instance();

    bool isEnabled() const noexcept
    {
        return enabled.load(std::memory_order_acquire);
    }
    void setEnabled(bool);
    juce::File folder() const;
    void openFolder() const;

    std::uint32_t registerInstance();
    void unregisterInstance(std::uint32_t);
    void log(const char* category, const juce::String& message);
    void logRealtime(
        DiagnosticEvent, std::uint32_t instanceId,
        std::int64_t valueA = 0, std::int64_t valueB = 0,
        std::int64_t valueC = 0, std::int64_t valueD = 0) noexcept;
    void updateCrashContext(
        DiagnosticEvent, std::uint32_t instanceId,
        std::int64_t valueA = 0) noexcept;

private:
    Diagnostics();
    ~Diagnostics() override;

    struct Entry
    {
        std::atomic<int> state { 0 };
        std::uint64_t ticks = 0;
        std::uint32_t event = 0;
        std::uint32_t instanceId = 0;
        std::int64_t valueA = 0;
        std::int64_t valueB = 0;
        std::int64_t valueC = 0;
        std::int64_t valueD = 0;
        std::array<char, 192> text {};
    };

    void run() override;
    bool enqueue(
        std::uint32_t event, std::uint32_t instanceId,
        std::int64_t valueA, std::int64_t valueB,
        std::int64_t valueC, std::int64_t valueD,
        const char* text) noexcept;
    void drain();
    void writeHeader();
    void rotateIfNeeded();
    void stop(bool rememberDisabled);
    void installCrashHandlers();
    void uninstallCrashHandlers();

    static constexpr size_t queueSize = 256;
    std::array<Entry, queueSize> queue;
    std::atomic<std::uint64_t> writeSequence { 0 };
    std::atomic<std::uint64_t> droppedEntries { 0 };
    std::atomic<std::uint32_t> nextInstanceId { 1 };
    std::atomic<bool> enabled { false };
    std::unique_ptr<juce::FileOutputStream> output;
};
} // namespace megadsp
