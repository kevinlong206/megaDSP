#include "DspModules.h"
#include "EffectRack.h"
#include "Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include <cmath>

namespace
{
class TimeTelemetryTests final : public juce::UnitTest
{
public:
    TimeTelemetryTests()
        : juce::UnitTest("Time telemetry", "megaDSP")
    {
    }

    void runTest() override
    {
        using namespace megadsp;
        const juce::dsp::ProcessSpec spec { 48000.0, 4096, 2 };
        const ProcessEnvironment capture { nullptr, 120.0, true };

        beginTest("Time module capability flags match their DSP");
        for (const auto type : {
                 ModuleType::diffusionDelay, ModuleType::pitchBloom })
        {
            auto module = createDspModule(type);
            expect(module != nullptr);
            expect(module->eventTelemetryCapability() != nullptr);
            expect(module->continuousTelemetryCapability() == nullptr);
            expect(hasCapability(
                module->capabilities(), ModuleCapability::eventTelemetry));
            expect(hasCapability(
                moduleDefinition(type).capabilities,
                ModuleCapability::eventTelemetry));
        }

        auto fillSignal = [](juce::AudioBuffer<float>& buffer, int offset)
        {
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                const auto time =
                    static_cast<float>(offset + sample) / 48000.0f;
                buffer.setSample(
                    0, sample,
                    0.28f * std::sin(
                        juce::MathConstants<float>::twoPi * 317.0f * time));
                buffer.setSample(
                    1, sample,
                    0.17f * std::sin(
                        juce::MathConstants<float>::twoPi * 463.0f * time));
            }
        };

        beginTest("Diffusion events require captured processed activity");
        DiffusionDelayModule diffusion;
        diffusion.prepare(spec);
        auto diffusionControls =
            moduleDefaults(ModuleType::diffusionDelay);
        diffusionControls[0] = 0.0f;
        diffusionControls[1] = 0.0f;
        diffusionControls[3] = 0.65f;
        diffusionControls[4] = 0.85f;
        diffusionControls[6] = 0.0f;
        diffusionControls[7] = 1.0f;
        juce::AudioBuffer<float> signal(2, 4096);
        fillSignal(signal, 0);
        diffusion.process(signal, diffusionControls, {});
        EventTelemetrySnapshot events;
        expect(diffusion.readEventTelemetry(events));
        expect(events.sequence == 0);
        expect(events.eventCount == 0);

        for (int block = 0; block < 3; ++block)
        {
            fillSignal(signal, block * signal.getNumSamples());
            diffusion.process(signal, diffusionControls, capture);
        }
        expect(diffusion.readEventTelemetry(events));
        expect(events.sequence == 3);
        bool foundPrimary = false;
        bool foundCloud = false;
        for (std::uint32_t index = 0; index < events.eventCount; ++index)
        {
            const auto& event = events.events[index];
            foundPrimary = foundPrimary
                || event.kind == static_cast<std::uint32_t>(
                    DiffusionDelayTelemetryEventKind::primaryRepeat);
            foundCloud = foundCloud
                || event.kind == static_cast<std::uint32_t>(
                    DiffusionDelayTelemetryEventKind::diffusionCloud);
            expect(event.progress >= 0.0f && event.progress < 1.0f);
            expectWithinAbsoluteError(
                event.values[static_cast<size_t>(
                    DiffusionDelayTelemetryValue::intervalSeconds)],
                0.010f, 0.001f);
            expect(event.values[static_cast<size_t>(
                       DiffusionDelayTelemetryValue::energy)] > 0.0f);
            expect(event.position[0] >= -1.0f
                   && event.position[0] <= 1.0f);
        }
        expect(foundPrimary);
        expect(foundCloud);
        const auto diffusionSequence = events.sequence;
        fillSignal(signal, 15000);
        diffusion.process(signal, diffusionControls, {});
        expect(diffusion.readEventTelemetry(events));
        expect(events.sequence == diffusionSequence);

        diffusion.reset();
        expect(diffusion.readEventTelemetry(events));
        expect(events.sequence == 0);
        expect(events.eventCount == 0);
        signal.clear();
        diffusion.process(signal, diffusionControls, capture);
        expect(diffusion.readEventTelemetry(events));
        expect(events.sequence == 1);
        expect(events.eventCount == 0);
        diffusion.prepare(spec);
        expect(diffusion.readEventTelemetry(events));
        expect(events.sequence == 0);
        expect(events.eventCount == 0);

        beginTest("Pitch events report actual interval energy and spread");
        PitchBloomModule pitch;
        pitch.prepare(spec);
        auto pitchControls = moduleDefaults(ModuleType::pitchBloom);
        pitchControls[0] = discreteValue(2, 5);
        pitchControls[1] = 0.5f;
        pitchControls[2] = 0.0f;
        pitchControls[3] = 0.55f;
        pitchControls[4] = 0.75f;
        pitchControls[5] = 1.0f;
        pitchControls[6] = 0.0f;
        pitchControls[7] = 1.0f;
        for (int block = 0; block < 4; ++block)
        {
            fillSignal(signal, block * signal.getNumSamples());
            pitch.process(signal, pitchControls, capture);
        }
        expect(pitch.readEventTelemetry(events));
        expect(events.sequence == 4);
        expect(events.eventCount > 0);
        for (std::uint32_t index = 0; index < events.eventCount; ++index)
        {
            const auto& event = events.events[index];
            expect(event.kind == static_cast<std::uint32_t>(
                PitchBloomTelemetryEventKind::shiftedRepeat));
            expectWithinAbsoluteError(
                event.values[static_cast<size_t>(
                    PitchBloomTelemetryValue::intervalSemitones)],
                12.0f, 0.02f);
            expectWithinAbsoluteError(
                event.values[static_cast<size_t>(
                    PitchBloomTelemetryValue::intervalSeconds)],
                0.020f, 0.001f);
            expect(event.values[static_cast<size_t>(
                       PitchBloomTelemetryValue::energy)] > 0.0f);
            expect(event.values[static_cast<size_t>(
                       PitchBloomTelemetryValue::stereoSpread)] >= 0.0f);
            expect(event.progress >= 0.0f && event.progress < 1.0f);
        }
        pitch.reset();
        expect(pitch.readEventTelemetry(events));
        expect(events.sequence == 0);
        expect(events.eventCount == 0);

        beginTest("Rack routes time telemetry to the selected slot only");
        juce::AudioProcessorGraph owner;
        juce::AudioProcessorValueTreeState state(
            owner, nullptr, "timeTelemetryState", createParameterLayout());
        EffectRack rack(state);
        rack.prepare(48000.0, 512, 2);
        const std::array<ModuleType, 2> types {
            ModuleType::diffusionDelay, ModuleType::pitchBloom
        };
        for (int slot = 0; slot < 2; ++slot)
        {
            auto* type = state.getParameter(slotParameterId(slot, "type"));
            type->setValueNotifyingHost(type->convertTo0to1(
                static_cast<float>(types[static_cast<size_t>(slot)])));
        }
        state.getParameter(controlParameterId(0, 0))
            ->setValueNotifyingHost(0.0f);
        state.getParameter(controlParameterId(0, 1))
            ->setValueNotifyingHost(0.0f);
        state.getParameter(controlParameterId(1, 2))
            ->setValueNotifyingHost(0.0f);
        rack.synchronizeModules();
        rack.visualizationData().setSelectedSlot(1);
        juce::AudioBuffer<float> routed(2, 512);
        for (int block = 0; block < 12; ++block)
        {
            fillSignal(routed, block * routed.getNumSamples());
            rack.process(routed, nullptr, 120.0);
        }
        expect(!rack.readEventTelemetry(0, events));
        expect(rack.readEventTelemetry(1, events));
        expect(events.sequence > 0);
        const auto* uncapturedDiffusion =
            rack.activeModuleInstance(0)->eventTelemetryCapability();
        expect(uncapturedDiffusion != nullptr);
        expect(uncapturedDiffusion->readEventTelemetry(events));
        expect(events.sequence == 0);

        const auto* capturedPitch =
            rack.activeModuleInstance(1)->eventTelemetryCapability();
        expect(capturedPitch != nullptr);
        expect(capturedPitch->readEventTelemetry(events));
        const auto pitchSequence = events.sequence;
        rack.visualizationData().setSelectedSlot(0);
        for (int block = 0; block < 6; ++block)
        {
            fillSignal(routed, block * routed.getNumSamples());
            rack.process(routed, nullptr, 120.0);
        }
        expect(rack.readEventTelemetry(0, events));
        expect(events.sequence > 0);
        expect(!rack.readEventTelemetry(1, events));
        expect(capturedPitch->readEventTelemetry(events));
        expect(events.sequence == pitchSequence);
    }
};

TimeTelemetryTests timeTelemetryTests;
} // namespace
