#include "DspModules.h"
#include "EffectRack.h"
#include "Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include <cmath>

namespace
{
class DynamicsTelemetryTests final : public juce::UnitTest
{
public:
    DynamicsTelemetryTests()
        : juce::UnitTest("Dynamics telemetry", "megaDSP")
    {
    }

    void runTest() override
    {
        using namespace megadsp;
        const juce::dsp::ProcessSpec spec { 48000.0, 4096, 2 };
        const ProcessEnvironment capture { nullptr, 120.0, true };

        beginTest("Dynamics capabilities match registry flags");
        for (const auto type : {
                 ModuleType::gateExpander,
                 ModuleType::transientDesigner,
                 ModuleType::multibandCompressor })
        {
            auto module = createDspModule(type);
            expect(module != nullptr);
            expect(module->continuousTelemetryCapability() != nullptr);
            expect(hasCapability(
                module->capabilities(),
                ModuleCapability::continuousTelemetry));
            expect(hasCapability(
                moduleDefinition(type).capabilities,
                ModuleCapability::continuousTelemetry));
        }

        beginTest("Gate publishes detector envelope attenuation and state");
        GateExpanderModule gate;
        gate.prepare(spec);
        auto gateControls = moduleDefaults(ModuleType::gateExpander);
        gateControls[0] = 1.0f;
        gateControls[1] = 1.0f;
        gateControls[2] = 0.0f;
        gateControls[3] = 0.0f;
        gateControls[4] = 0.0f;
        gateControls[5] = 0.0f;
        gateControls[8] = 0.0f;
        gateControls[9] = 0.0f;
        juce::AudioBuffer<float> silence(2, 4096);
        silence.clear();
        gate.process(silence, gateControls, {});
        ContinuousTelemetrySnapshot snapshot;
        expect(gate.readContinuousTelemetry(snapshot));
        expect(snapshot.sequence == 0);
        gate.process(silence, gateControls, capture);
        expect(gate.readContinuousTelemetry(snapshot));
        expect(snapshot.sequence == 1);
        expectEquals(
            static_cast<int>(snapshot.valueCount),
            static_cast<int>(GateExpanderModule::telemetryValueCount));
        expect(snapshot.values[GateExpanderModule::detectorDb] <= -99.0f);
        expect(snapshot.values[GateExpanderModule::gainEnvelopeDb] < 0.0f);
        expect(snapshot.values[GateExpanderModule::attenuationDb] > 1.0f);
        expectEquals(
            snapshot.values[GateExpanderModule::openFraction], 0.0f);
        expectEquals(static_cast<int>(snapshot.historyCount), 1);
        gate.reset();
        expect(gate.readContinuousTelemetry(snapshot));
        expect(snapshot.sequence == 0);
        expect(snapshot.historyCount == 0);

        beginTest("Transient publishes DSP envelope and shaping components");
        TransientDesignerModule transient;
        transient.prepare(spec);
        auto transientControls =
            moduleDefaults(ModuleType::transientDesigner);
        transientControls[0] = 1.0f;
        transientControls[1] = 1.0f;
        transientControls[2] = 1.0f;
        transientControls[3] = 0.65f;
        transientControls[4] = 0.55f;
        juce::AudioBuffer<float> transients(2, 4096);
        transients.clear();
        for (int sample = 0; sample < transients.getNumSamples(); ++sample)
        {
            const auto tone = 0.08f * std::sin(
                juce::MathConstants<float>::twoPi * 1000.0f
                * static_cast<float>(sample) / 48000.0f);
            const auto impulse = sample % 256 == 0 ? 0.9f : 0.0f;
            transients.setSample(0, sample, tone + impulse);
            transients.setSample(1, sample, tone + impulse);
        }
        transient.process(transients, transientControls, capture);
        expect(transient.readContinuousTelemetry(snapshot));
        expect(snapshot.sequence == 1);
        expectEquals(
            static_cast<int>(snapshot.valueCount),
            static_cast<int>(
                TransientDesignerModule::telemetryValueCount));
        expect(snapshot.values[
                   TransientDesignerModule::fastEnvelopeDb] > -100.0f);
        expect(snapshot.values[
                   TransientDesignerModule::slowEnvelopeDb] > -100.0f);
        expect(snapshot.values[
                   TransientDesignerModule::attackShapingDb] > 0.0f);
        expect(snapshot.values[
                   TransientDesignerModule::appliedShapingDb] != 0.0f);

        beginTest("Multiband publishes actual reduction and active state");
        MultibandCompressorModule multiband;
        multiband.prepare(spec);
        auto multibandControls =
            moduleDefaults(ModuleType::multibandCompressor);
        multibandControls[2] = 0.0f;
        multibandControls[3] = 0.0f;
        multibandControls[4] = 0.0f;
        multibandControls[5] = 1.0f;
        multibandControls[6] = 0.0f;
        multibandControls[10] = 1.0f;
        juce::AudioBuffer<float> broadband(2, 4096);
        for (int sample = 0; sample < broadband.getNumSamples(); ++sample)
        {
            const auto time = static_cast<float>(sample) / 48000.0f;
            const auto signal =
                0.32f * std::sin(
                    juce::MathConstants<float>::twoPi * 100.0f * time)
                + 0.32f * std::sin(
                    juce::MathConstants<float>::twoPi * 1200.0f * time)
                + 0.32f * std::sin(
                    juce::MathConstants<float>::twoPi * 8000.0f * time);
            broadband.setSample(0, sample, signal);
            broadband.setSample(1, sample, signal);
        }
        multiband.process(broadband, multibandControls, capture);
        expect(multiband.readContinuousTelemetry(snapshot));
        expect(snapshot.sequence == 1);
        for (int band = 0; band < 3; ++band)
        {
            const auto index = static_cast<size_t>(band);
            expect(snapshot.values[
                       static_cast<size_t>(
                           MultibandCompressorModule::lowReductionDb)
                       + index]
                   > 0.0f);
            expect(snapshot.values[
                       static_cast<size_t>(
                           MultibandCompressorModule::lowActive)
                       + index]
                   > 0.0f);
        }

        beginTest("History helper presents coherent chronological view data");
        ContinuousTelemetrySnapshot history;
        for (int marker = 0; marker < 66; ++marker)
            appendContinuousTelemetryHistory(
                history,
                { static_cast<float>(marker),
                  static_cast<float>(marker + 100), 0.0f, 0.0f },
                2);
        expectEquals(
            static_cast<int>(history.historyCount),
            static_cast<int>(continuousTelemetryHistoryCapacity));
        expectEquals(
            continuousTelemetryHistoryValue(history, 0, 0), 2.0f);
        expectEquals(
            continuousTelemetryHistoryValue(history, 1, 63), 165.0f);

        beginTest("Rack captures telemetry for the selected slot only");
        juce::AudioProcessorGraph owner;
        juce::AudioProcessorValueTreeState state(
            owner, nullptr, "dynamicsTelemetryState",
            createParameterLayout());
        EffectRack rack(state);
        rack.prepare(48000.0, 512, 2);
        const std::array<ModuleType, 3> types {
            ModuleType::gateExpander,
            ModuleType::transientDesigner,
            ModuleType::multibandCompressor
        };
        for (int slot = 0; slot < 3; ++slot)
        {
            auto* type = state.getParameter(slotParameterId(slot, "type"));
            type->setValueNotifyingHost(type->convertTo0to1(
                static_cast<float>(types[static_cast<size_t>(slot)])));
        }
        rack.synchronizeModules();
        rack.visualizationData().setSelectedSlot(1);
        auto routedSignal = transients;
        routedSignal.setSize(2, 512, true, true, true);
        rack.process(routedSignal, nullptr, 120.0);
        expect(!rack.readContinuousTelemetry(0, snapshot));
        expect(rack.readContinuousTelemetry(1, snapshot));
        expect(snapshot.sequence > 0);
        expect(!rack.readContinuousTelemetry(2, snapshot));
        const auto* uncaptured = rack.activeModuleInstance(0)
            ->continuousTelemetryCapability();
        expect(uncaptured != nullptr);
        expect(uncaptured->readContinuousTelemetry(snapshot));
        expect(snapshot.sequence == 0);
    }
};

DynamicsTelemetryTests dynamicsTelemetryTests;
} // namespace
