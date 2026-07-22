#include "EffectRack.h"
#include "Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include <atomic>
#include <thread>
#include <type_traits>

static_assert(std::is_trivially_copyable_v<
              megadsp::ContinuousTelemetrySnapshot>);
static_assert(std::is_trivially_copyable_v<
              megadsp::EventTelemetrySnapshot>);

namespace
{
class TelemetryTestModule final
    : public megadsp::DspModule,
      public megadsp::ContinuousTelemetryCapability,
      public megadsp::EventTelemetryCapability
{
public:
    void prepare(const juce::dsp::ProcessSpec&) override {}
    void reset() override
    {
        continuous.clear();
        events.clear();
    }
    void process(
        juce::AudioBuffer<float>&, const megadsp::ControlValues&,
        const megadsp::ProcessEnvironment&) override
    {
    }

    bool readContinuousTelemetry(
        megadsp::ContinuousTelemetrySnapshot& snapshot)
        const noexcept override
    {
        return continuous.read(snapshot);
    }

    bool readEventTelemetry(
        megadsp::EventTelemetrySnapshot& snapshot)
        const noexcept override
    {
        return events.read(snapshot);
    }

    megadsp::ContinuousTelemetryCapability*
        continuousTelemetryCapability() noexcept override
    {
        return this;
    }

    const megadsp::ContinuousTelemetryCapability*
        continuousTelemetryCapability() const noexcept override
    {
        return this;
    }

    megadsp::EventTelemetryCapability*
        eventTelemetryCapability() noexcept override
    {
        return this;
    }

    const megadsp::EventTelemetryCapability*
        eventTelemetryCapability() const noexcept override
    {
        return this;
    }

    megadsp::FixedAtomicSnapshot<megadsp::ContinuousTelemetrySnapshot>
        continuous;
    megadsp::FixedAtomicSnapshot<megadsp::EventTelemetrySnapshot> events;
};

class TelemetryFoundationTests final : public juce::UnitTest
{
public:
    TelemetryFoundationTests()
        : juce::UnitTest("Telemetry foundation", "megaDSP")
    {
    }

    void runTest() override
    {
        beginTest("Fixed snapshots publish coherent continuous state");
        megadsp::FixedAtomicSnapshot<
            megadsp::ContinuousTelemetrySnapshot> storage;
        megadsp::ContinuousTelemetrySnapshot source;
        source.sequence = 42;
        source.valueCount = 3;
        source.values[0] = -18.0f;
        source.values[1] = 0.75f;
        source.values[2] = 6.0f;
        source.historyValueCount = 2;
        source.historyCount = 1;
        source.historyWritePosition = 1;
        source.history[0][0] = -24.0f;
        source.history[1][0] = 4.5f;
        storage.publish(source);

        megadsp::ContinuousTelemetrySnapshot read;
        expect(storage.read(read));
        expect(read.sequence == 42);
        expectEquals(static_cast<int>(read.valueCount), 3);
        expectEquals(read.values[0], -18.0f);
        expectEquals(read.history[1][0], 4.5f);

        storage.clear();
        expect(storage.read(read));
        expect(read.sequence == 0);
        expectEquals(static_cast<int>(read.valueCount), 0);
        expectEquals(static_cast<int>(read.historyCount), 0);

        beginTest("Fixed snapshots do not expose torn publications");
        std::atomic<bool> writerDone { false };
        std::atomic<bool> torn { false };
        std::thread writer([&]
        {
            for (std::uint64_t marker = 1; marker <= 20000; ++marker)
            {
                megadsp::ContinuousTelemetrySnapshot next;
                next.sequence = marker;
                next.valueCount = static_cast<std::uint32_t>(
                    next.values.size());
                next.values.fill(static_cast<float>(marker));
                storage.publish(next);
            }
            writerDone.store(true, std::memory_order_release);
        });
        while (!writerDone.load(std::memory_order_acquire))
        {
            megadsp::ContinuousTelemetrySnapshot candidate;
            if (!storage.read(candidate) || candidate.sequence == 0)
                continue;
            for (const auto value : candidate.values)
                if (std::abs(
                        value - static_cast<float>(candidate.sequence))
                    > 0.0f)
                    torn.store(true, std::memory_order_relaxed);
        }
        writer.join();
        expect(!torn.load(std::memory_order_relaxed));

        beginTest("Optional capabilities advertise only implemented paths");
        TelemetryTestModule module;
        const auto expected =
            megadsp::ModuleCapability::continuousTelemetry
            | megadsp::ModuleCapability::eventTelemetry;
        expect(module.capabilities() == expected);
        expect(module.grainVisualizationCapability() == nullptr);
        expect(module.beatPermutationVisualizationCapability() == nullptr);

        megadsp::EventTelemetrySnapshot eventSource;
        eventSource.sequence = 9;
        eventSource.eventCount = 1;
        eventSource.events[0].sequence = 77;
        eventSource.events[0].kind = 3;
        eventSource.events[0].position = { 0.25f, -0.5f, 0.75f };
        eventSource.events[0].values[0] = 12.0f;
        module.events.publish(eventSource);
        megadsp::EventTelemetrySnapshot eventRead;
        expect(module.readEventTelemetry(eventRead));
        expectEquals(static_cast<int>(eventRead.eventCount), 1);
        expect(eventRead.events[0].sequence == 77);
        expectEquals(eventRead.events[0].position[1], -0.5f);
        module.reset();
        expect(module.readEventTelemetry(eventRead));
        expectEquals(static_cast<int>(eventRead.eventCount), 0);

        beginTest("Rack telemetry reads are selected-slot-only and optional");
        juce::AudioProcessorGraph owner;
        juce::AudioProcessorValueTreeState state(
            owner, nullptr, "telemetryState",
            megadsp::createParameterLayout());
        megadsp::EffectRack rack(state);
        rack.prepare(48000.0, 64, 1);
        auto* type = state.getParameter(
            megadsp::slotParameterId(0, "type"));
        type->setValueNotifyingHost(type->convertTo0to1(
            static_cast<float>(megadsp::ModuleType::equalizer)));
        rack.synchronizeModules();
        megadsp::ContinuousTelemetrySnapshot continuous;
        megadsp::EventTelemetrySnapshot events;
        expect(!rack.readContinuousTelemetry(0, continuous));
        expect(!rack.readEventTelemetry(0, events));
        rack.visualizationData().setSelectedSlot(0);
        expect(!rack.readContinuousTelemetry(0, continuous));
        expect(!rack.readEventTelemetry(0, events));
        expect(!rack.readContinuousTelemetry(1, continuous));

        beginTest("Rack captures telemetry only for the selected capable slot");
        type->setValueNotifyingHost(type->convertTo0to1(
            static_cast<float>(megadsp::ModuleType::studioPhaser)));
        rack.synchronizeModules();
        rack.visualizationData().setSelectedSlot(0);
        juce::AudioBuffer<float> telemetryBlock(1, 64);
        telemetryBlock.clear();
        rack.process(telemetryBlock, nullptr, 120.0);
        expect(rack.readContinuousTelemetry(0, continuous));
        expect(continuous.sequence == 1);
        expect(!rack.readEventTelemetry(0, events));
        const auto capturedSequence = continuous.sequence;

        rack.visualizationData().setSelectedSlot(1);
        rack.process(telemetryBlock, nullptr, 120.0);
        expect(!rack.readContinuousTelemetry(0, continuous));
        rack.visualizationData().setSelectedSlot(0);
        expect(rack.readContinuousTelemetry(0, continuous));
        expect(continuous.sequence == capturedSequence);

        beginTest("Process environment defaults to no telemetry capture");
        const megadsp::ProcessEnvironment defaultEnvironment;
        const megadsp::ProcessEnvironment selectedEnvironment {
            nullptr, 120.0, true
        };
        expect(!defaultEnvironment.captureTelemetry);
        expect(selectedEnvironment.captureTelemetry);
    }
};

TelemetryFoundationTests telemetryFoundationTests;
} // namespace
