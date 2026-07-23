#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include "EffectRack.h"
#include "modules/ModuleRegistry.h"

#include <cstdio>
#include <type_traits>
#include <utility>

static_assert(std::is_same_v<
              decltype(std::declval<const megadsp::EffectRack&>()
                           .grainVisualEvents(0)),
              megadsp::GrainVisualEvents>);

namespace
{
class RackIntegrationTests final : public juce::UnitTest
{
public:
    RackIntegrationTests() : juce::UnitTest("Rack integration", "megaDSP") {}

    void runTest() override
    {
        beginTest("Rack has eight compact slots");
        expectEquals(megadsp::slotCount, 8);

        beginTest("Limiter gain is aligned with reported lookahead");
        megadsp::LimiterModule limiter;
        const juce::dsp::ProcessSpec spec { 48000.0, 512, 2 };
        limiter.prepare(spec);
        megadsp::ControlValues controls {};
        controls.fill(0.5f);
        controls[0] = 1.0f;
        controls[1] = 1.0f;
        controls[3] = 0.0f;
        juce::AudioBuffer<float> impulse(2, 512);
        impulse.clear();
        impulse.setSample(0, 0, 4.0f);
        impulse.setSample(1, 0, -4.0f);
        limiter.process(impulse, controls, {});
        const auto latency = limiter.latencySamples();
        expectEquals(latency, 480);
        expectWithinAbsoluteError(impulse.getSample(0, latency), 1.0f, 0.0001f);
        expectWithinAbsoluteError(impulse.getSample(1, latency), -1.0f, 0.0001f);

        beginTest("Maximum limiter lookahead aligns gain with transients");
        limiter.reset();
        controls[0] = 1.0f;
        controls[1] = 1.0f;
        controls[2] = 0.0f;
        controls[3] = 1.0f;
        controls[4] = 0.0f;
        juce::AudioBuffer<float> transient(2, 1024);
        transient.clear();
        transient.setSample(0, 0, 4.0f);
        transient.setSample(1, 0, 4.0f);
        transient.setSample(0, 1, 0.5f);
        transient.setSample(1, 1, 0.5f);
        limiter.process(transient, controls, {});
        expectWithinAbsoluteError(
            transient.getSample(0, latency), 1.0f, 0.0001f);
        expect(std::abs(transient.getSample(0, latency + 1)) < 0.2f);

        beginTest("Rack safely chunks blocks larger than prepare hint");
        juce::AudioProcessorGraph owner;
        juce::AudioProcessorValueTreeState state(
            owner, nullptr, "testState", megadsp::createParameterLayout());
        megadsp::EffectRack rack(state);
        for (int slot = 0; slot < megadsp::slotCount; ++slot)
        {
            expect(rack.activeModuleType(slot) == megadsp::ModuleType::empty);
            expect(rack.activeModuleInstance(slot) == nullptr);
        }
        rack.prepare(48000.0, 64, 2);
        if (auto* type = state.getParameter(megadsp::slotParameterId(0, "type")))
            type->setValueNotifyingHost(type->convertTo0to1(
                static_cast<float>(megadsp::ModuleType::equalizer)));
        expect(rack.activeModuleInstance(0) == nullptr);
        rack.synchronizeModules();
        expect(rack.activeModuleType(0) == megadsp::ModuleType::equalizer);
        expect(dynamic_cast<const megadsp::EqualizerModule*>(
                   rack.activeModuleInstance(0)) != nullptr);
        for (int slot = 1; slot < megadsp::slotCount; ++slot)
            expect(rack.activeModuleInstance(slot) == nullptr);
        juce::AudioBuffer<float> oversized(2, 4097);
        oversized.clear();
        oversized.setSample(0, 0, 1.0f);
        rack.process(oversized, nullptr, 120.0);
        for (int channel = 0; channel < oversized.getNumChannels(); ++channel)
            for (int sample = 0; sample < oversized.getNumSamples(); ++sample)
                expect(std::isfinite(oversized.getSample(channel, sample)));
        expectWithinAbsoluteError(rack.inputMeterDb(), 0.0f, 0.01f);
        expectWithinAbsoluteError(rack.outputMeterDb(), 0.0f, 0.01f);
        expectEquals(rack.inputMeterDb(), -100.0f);
        expectEquals(rack.outputMeterDb(), -100.0f);

        beginTest("Output trim is the metered final rack stage");
        if (auto* output = state.getParameter("global.output"))
            output->setValueNotifyingHost(output->convertTo0to1(-6.0f));
        juce::AudioBuffer<float> trimSettle(2, 1024);
        trimSettle.clear();
        rack.process(trimSettle, nullptr, 120.0);
        rack.inputMeterDb();
        rack.outputMeterDb();
        juce::AudioBuffer<float> trimmed(2, 512);
        trimmed.clear();
        trimmed.setSample(0, 0, 1.0f);
        trimmed.setSample(1, 0, 1.0f);
        rack.process(trimmed, nullptr, 120.0);
        expectWithinAbsoluteError(rack.inputMeterDb(), 0.0f, 0.01f);
        expectWithinAbsoluteError(rack.outputMeterDb(), -6.0f, 0.05f);

        beginTest("Rack routes appended Dynamic EQ module");
        if (auto* type = state.getParameter(
                megadsp::slotParameterId(0, "type")))
            type->setValueNotifyingHost(type->convertTo0to1(
                static_cast<float>(megadsp::ModuleType::dynamicEqualizer)));
        rack.synchronizeModules();
        const auto dynamicDefaults = megadsp::moduleDefaults(
            megadsp::ModuleType::dynamicEqualizer);
        for (int control = 0; control < megadsp::controlsPerSlot; ++control)
            if (auto* parameter = state.getParameter(
                    megadsp::controlParameterId(0, control)))
                parameter->setValueNotifyingHost(parameter->convertTo0to1(
                    dynamicDefaults[static_cast<size_t>(control)]));
        juce::AudioBuffer<float> dynamicRackBlock(2, 512);
        for (int sample = 0; sample < dynamicRackBlock.getNumSamples(); ++sample)
        {
            const auto value = 0.2f * std::sin(
                juce::MathConstants<float>::twoPi * 6000.0f
                * static_cast<float>(sample) / 48000.0f);
            dynamicRackBlock.setSample(0, sample, value);
            dynamicRackBlock.setSample(1, sample, value);
        }
        rack.process(dynamicRackBlock, nullptr, 120.0);
        expect(rack.moduleType(0) == megadsp::ModuleType::dynamicEqualizer);
        expect(rack.activeModuleType(0)
               == megadsp::ModuleType::dynamicEqualizer);
        expect(dynamic_cast<const megadsp::DynamicEqualizerModule*>(
                   rack.activeModuleInstance(0)) != nullptr);
        expect(std::isfinite(dynamicRackBlock.getMagnitude(
            0, dynamicRackBlock.getNumSamples())));

        beginTest("Rack routes appended Random Granulizer module");
        if (auto* type = state.getParameter(
                megadsp::slotParameterId(0, "type")))
            type->setValueNotifyingHost(type->convertTo0to1(
                static_cast<float>(megadsp::ModuleType::randomGranulizer)));
        rack.synchronizeModules();
        const auto grainDefaults = megadsp::moduleDefaults(
            megadsp::ModuleType::randomGranulizer);
        for (int control = 0; control < megadsp::controlsPerSlot; ++control)
            if (auto* parameter = state.getParameter(
                    megadsp::controlParameterId(0, control)))
                parameter->setValueNotifyingHost(parameter->convertTo0to1(
                    grainDefaults[static_cast<size_t>(control)]));
        juce::AudioBuffer<float> grainRackBlock(2, 16384);
        for (int sample = 0; sample < grainRackBlock.getNumSamples(); ++sample)
        {
            const auto value = 0.2f * std::sin(
                juce::MathConstants<float>::twoPi * 440.0f
                * static_cast<float>(sample) / 48000.0f);
            grainRackBlock.setSample(0, sample, value);
            grainRackBlock.setSample(1, sample, value);
        }
        for (int block = 0; block < 3; ++block)
            rack.process(grainRackBlock, nullptr, 120.0);
        expect(rack.moduleType(0) == megadsp::ModuleType::randomGranulizer);
        expect(rack.activeModuleType(0)
               == megadsp::ModuleType::randomGranulizer);
        expect(dynamic_cast<const megadsp::RandomGranulizerModule*>(
                   rack.activeModuleInstance(0)) != nullptr);
        expect(std::isfinite(grainRackBlock.getMagnitude(
            0, grainRackBlock.getNumSamples())));
        const auto rackGrainEvents = rack.grainVisualEvents(0);
        expect(std::any_of(
            rackGrainEvents.begin(), rackGrainEvents.end(),
            [](const auto& event) { return event.sequence != 0; }));
        expect(rack.activeModuleInstance(0)
                   ->grainVisualizationCapability() != nullptr);
        expect(rack.activeModuleInstance(0)
                   ->impulseResponseCapability() == nullptr);

        beginTest("Rack routes appended Vintage Chorus module");
        if (auto* type = state.getParameter(
                megadsp::slotParameterId(0, "type")))
            type->setValueNotifyingHost(type->convertTo0to1(
                static_cast<float>(megadsp::ModuleType::vintageChorus)));
        rack.synchronizeModules();
        const auto chorusDefaults = megadsp::moduleDefaults(
            megadsp::ModuleType::vintageChorus);
        for (int control = 0; control < megadsp::controlsPerSlot; ++control)
            if (auto* parameter = state.getParameter(
                    megadsp::controlParameterId(0, control)))
                parameter->setValueNotifyingHost(parameter->convertTo0to1(
                    chorusDefaults[static_cast<size_t>(control)]));
        juce::AudioBuffer<float> chorusRackBlock(2, 8192);
        for (int sample = 0; sample < chorusRackBlock.getNumSamples(); ++sample)
        {
            const auto value = 0.2f * std::sin(
                juce::MathConstants<float>::twoPi * 311.0f
                * static_cast<float>(sample) / 48000.0f);
            chorusRackBlock.setSample(0, sample, value);
            chorusRackBlock.setSample(1, sample, value);
        }
        rack.process(chorusRackBlock, nullptr, 120.0);
        expect(rack.moduleType(0) == megadsp::ModuleType::vintageChorus);
        expect(rack.activeModuleType(0)
               == megadsp::ModuleType::vintageChorus);
        expect(dynamic_cast<const megadsp::VintageChorusModule*>(
                   rack.activeModuleInstance(0)) != nullptr);
        expect(std::isfinite(chorusRackBlock.getMagnitude(
            0, chorusRackBlock.getNumSamples())));

        beginTest("Removing a slot compacts signal flow");
        auto setType = [&state, &rack](int slot, megadsp::ModuleType type)
        {
            if (auto* parameter = state.getParameter(
                    megadsp::slotParameterId(slot, "type")))
                parameter->setValueNotifyingHost(
                    parameter->convertTo0to1(static_cast<float>(type)));
            rack.synchronizeModules();
        };
        setType(0, megadsp::ModuleType::equalizer);
        setType(1, megadsp::ModuleType::compressor);
        setType(2, megadsp::ModuleType::delay);
        expectEquals(rack.activeSlotCount(), 3);
        rack.removeSlot(1);
        rack.synchronizeModules();
        expectEquals(rack.activeSlotCount(), 2);
        expect(rack.moduleType(1) == megadsp::ModuleType::delay);
        expect(rack.moduleType(2) == megadsp::ModuleType::empty);
        expect(rack.activeModuleType(1) == megadsp::ModuleType::delay);
        expect(rack.activeModuleInstance(2) == nullptr);

        beginTest("Compaction makes restored topology contiguous");
        setType(0, megadsp::ModuleType::empty);
        setType(1, megadsp::ModuleType::limiter);
        rack.compactSlots();
        rack.synchronizeModules();
        expect(rack.moduleType(0) == megadsp::ModuleType::limiter);
        expect(rack.moduleType(1) == megadsp::ModuleType::empty);
        expect(rack.activeModuleType(0) == megadsp::ModuleType::limiter);
        expect(rack.activeModuleInstance(1) == nullptr);
        expectEquals(rack.activeSlotCount(), 1);

        beginTest(
            "Sparse rack compaction preserves signal-flow order and controls");
        setType(0, megadsp::ModuleType::empty);
        setType(2, megadsp::ModuleType::delay);
        setType(5, megadsp::ModuleType::limiter);
        auto setControl = [&state](int slot, int control, float value)
        {
            auto* parameter = state.getParameter(
                megadsp::controlParameterId(slot, control));
            parameter->setValueNotifyingHost(
                parameter->convertTo0to1(value));
        };
        setControl(2, 0, 0.17f);
        setControl(5, 0, 0.83f);
        expect(rack.compactSlots());
        rack.synchronizeModules();
        expectEquals(rack.activeSlotCount(), 2);
        expect(rack.moduleType(0) == megadsp::ModuleType::delay);
        expect(rack.moduleType(1) == megadsp::ModuleType::limiter);
        expect(rack.moduleType(2) == megadsp::ModuleType::empty);
        expectWithinAbsoluteError(
            state.getRawParameterValue(
                megadsp::controlParameterId(0, 0))->load(),
            0.17f, 0.0001f);
        expectWithinAbsoluteError(
            state.getRawParameterValue(
                megadsp::controlParameterId(1, 0))->load(),
            0.83f, 0.0001f);

        beginTest("Impulse response paths follow slot reordering and clearing");
        setType(0, megadsp::ModuleType::convolutionReverb);
        setType(1, megadsp::ModuleType::empty);
        state.state.setProperty(
            "impulseResponse1", "/missing/test-ir.wav", nullptr);
        rack.moveSlot(0, 1);
        rack.synchronizeModules();
        expect(!state.state.hasProperty("impulseResponse1"));
        expectEquals(
            state.state.getProperty("impulseResponse2").toString(),
            juce::String("/missing/test-ir.wav"));
        rack.clearSlot(1);
        rack.synchronizeModules();
        expect(!state.state.hasProperty("impulseResponse2"));
        expect(rack.activeModuleInstance(1) == nullptr);

        beginTest("IR capability reports errors and survives rebuilds and moves");
        juce::AudioProcessorGraph capabilityOwner;
        juce::AudioProcessorValueTreeState capabilityState(
            capabilityOwner, nullptr, "capabilityState",
            megadsp::createParameterLayout());
        megadsp::EffectRack capabilityRack(capabilityState);
        capabilityRack.prepare(48000.0, 512, 2);
        auto setCapabilityType =
            [&capabilityState, &capabilityRack](
                int slot, megadsp::ModuleType type)
        {
            auto* parameter = capabilityState.getParameter(
                megadsp::slotParameterId(slot, "type"));
            parameter->setValueNotifyingHost(
                parameter->convertTo0to1(static_cast<float>(type)));
            capabilityRack.synchronizeModules();
        };
        expect(capabilityRack.loadImpulseResponse(
                   -1, juce::File("invalid.wav")).failed());
        expect(capabilityRack.clearImpulseResponse(-1).failed());
        expect(capabilityRack.loadImpulseResponse(
                   0, juce::File("invalid.wav")).failed());
        expect(capabilityRack.clearImpulseResponse(0).failed());
        expect(capabilityRack.impulseResponseName(0).isEmpty());
        expect(capabilityRack.impulseResponsePath(0).isEmpty());
        const auto emptyImpulsePreview =
            capabilityRack.impulseResponsePreview(0);
        expect(std::all_of(
            emptyImpulsePreview.begin(), emptyImpulsePreview.end(),
            [](float sample) { return sample == 0.0f; }));

        setCapabilityType(0, megadsp::ModuleType::equalizer);
        expect(capabilityRack.loadImpulseResponse(
                   0, juce::File("invalid.wav")).failed());
        expect(capabilityRack.clearImpulseResponse(0).failed());
        const auto emptyGrainEvents =
            capabilityRack.grainVisualEvents(0);
        expect(std::all_of(
            emptyGrainEvents.begin(), emptyGrainEvents.end(),
            [](const auto& event) { return event.sequence == 0; }));

        const auto rackImpulseFile =
            juce::File::getCurrentWorkingDirectory()
                .getNonexistentChildFile("megadsp-rack-ir", ".wav");
        {
            std::unique_ptr<juce::OutputStream> stream =
                rackImpulseFile.createOutputStream();
            juce::WavAudioFormat format;
            auto writer = format.createWriterFor(
                stream, juce::AudioFormatWriterOptions {}
                            .withSampleRate(48000.0)
                            .withNumChannels(2)
                            .withBitsPerSample(24));
            expect(writer != nullptr);
            juce::AudioBuffer<float> rackImpulse(2, 1024);
            rackImpulse.clear();
            rackImpulse.setSample(0, 0, 1.0f);
            rackImpulse.setSample(1, 0, 1.0f);
            rackImpulse.setSample(0, 700, 0.4f);
            rackImpulse.setSample(1, 700, 0.4f);
            expect(writer != nullptr
                   && writer->writeFromAudioSampleBuffer(
                       rackImpulse, 0, rackImpulse.getNumSamples()));
        }
        setCapabilityType(0, megadsp::ModuleType::convolutionReverb);
        const auto rackLoadResult =
            capabilityRack.loadImpulseResponse(0, rackImpulseFile);
        expect(rackLoadResult.wasOk(), rackLoadResult.getErrorMessage());
        expectEquals(capabilityRack.impulseResponsePath(0),
                     rackImpulseFile.getFullPathName());
        expectEquals(capabilityRack.impulseResponseName(0),
                     rackImpulseFile.getFileName());
        const auto* impulseCapability =
            capabilityRack.activeModuleInstance(0)
                ->impulseResponseCapability();
        expect(impulseCapability != nullptr);
        expect(impulseCapability != nullptr
               && impulseCapability->currentImpulseResponseTailSeconds()
                      > 0.0);

        beginTest(
            "Unrelated synchronization does not reload convolution IRs");
        auto setCapabilityControl =
            [&capabilityState](int control, float value)
        {
            auto* parameter = capabilityState.getParameter(
                megadsp::controlParameterId(0, control));
            parameter->setValueNotifyingHost(
                parameter->convertTo0to1(value));
        };
        setCapabilityControl(2, 1.0f);
        setCapabilityControl(4, 0.0f);
        juce::AudioBuffer<float> rackConvolutionBlock(2, 512);
        for (int block = 0; block < 4; ++block)
        {
            rackConvolutionBlock.clear();
            capabilityRack.process(
                rackConvolutionBlock, nullptr, 120.0);
        }
        rackConvolutionBlock.clear();
        rackConvolutionBlock.setSample(0, 0, 1.0f);
        rackConvolutionBlock.setSample(1, 0, 1.0f);
        capabilityRack.process(
            rackConvolutionBlock, nullptr, 120.0);
        setCapabilityType(1, megadsp::ModuleType::equalizer);
        rackConvolutionBlock.clear();
        capabilityRack.process(
            rackConvolutionBlock, nullptr, 120.0);
        expect(rackConvolutionBlock.getMagnitude(
                   0, rackConvolutionBlock.getNumSamples()) > 0.01f);
        capabilityRack.clearSlot(1);
        capabilityRack.synchronizeModules();

        capabilityRack.rebuildModules();
        expectEquals(capabilityRack.impulseResponseName(0),
                     rackImpulseFile.getFileName());
        capabilityRack.moveSlot(0, 1);
        capabilityRack.synchronizeModules();
        expect(capabilityRack.impulseResponsePath(0).isEmpty());
        expectEquals(capabilityRack.impulseResponsePath(1),
                     rackImpulseFile.getFullPathName());
        expectEquals(capabilityRack.impulseResponseName(1),
                     rackImpulseFile.getFileName());
        capabilityRack.moveSlot(1, 0);
        capabilityRack.synchronizeModules();
        capabilityRack.removeSlot(0);
        capabilityRack.synchronizeModules();
        expect(!capabilityState.state.hasProperty("impulseResponse1"));
        expect(capabilityRack.impulseResponsePath(0).isEmpty());
        rackImpulseFile.deleteFile();

        beginTest("Restored topology replaces the owned module instance");
        setType(0, megadsp::ModuleType::delay);
        expectEquals(rack.activeSlotCount(), 1);
        const auto* delayInstance = rack.activeModuleInstance(0);
        auto restoredTopology = state.copyState();
        setType(0, megadsp::ModuleType::compressor);
        expectEquals(rack.activeSlotCount(), 1);
        expect(rack.activeModuleInstance(0) != delayInstance);
        expect(dynamic_cast<const megadsp::CompressorModule*>(
                   rack.activeModuleInstance(0)) != nullptr);
        state.replaceState(restoredTopology);
        for (const auto child : restoredTopology)
        {
            const auto id = child.getProperty("id").toString();
            if (auto* parameter = state.getParameter(id))
            {
                const auto plainValue = static_cast<float>(
                    static_cast<double>(child.getProperty("value")));
                parameter->setValueNotifyingHost(
                    dynamic_cast<juce::AudioParameterBool*>(parameter) != nullptr
                        ? plainValue : parameter->convertTo0to1(plainValue));
            }
        }
        rack.synchronizeModules();
        expectEquals(rack.activeSlotCount(), 1);
        expect(rack.activeModuleType(0) == megadsp::ModuleType::delay);
        expect(dynamic_cast<const megadsp::DelayModule*>(
                   rack.activeModuleInstance(0)) != nullptr);

        beginTest("Visualization history keeps the newest bounded samples");
        megadsp::SampleHistory<32> history;
        for (int sample = 0; sample < 100; ++sample)
            history.push(static_cast<float>(sample));
        std::array<float, 8> latest {};
        expectEquals(static_cast<int>(history.copyLatest(latest)), 8);
        for (int sample = 0; sample < 8; ++sample)
            expectEquals(latest[static_cast<size_t>(sample)],
                         static_cast<float>(92 + sample));

        beginTest("Gain reduction history covers the latest ten seconds");
        megadsp::VisualizationData reductionVisualization;
        reductionVisualization.prepare(48000.0);
        reductionVisualization.captureGainReduction(0, 24.0f, 960);
        for (int interval = 0; interval < 500; ++interval)
            reductionVisualization.captureGainReduction(
                0, interval == 499 ? 8.0f : 1.0f, 960);
        std::array<float, 500> reductionWindow {};
        expectEquals(static_cast<int>(
                         reductionVisualization.slotData(0)
                             .gainReductionLevel.copyLatest(reductionWindow)),
                     500);
        expectWithinAbsoluteError(
            *std::max_element(reductionWindow.begin(), reductionWindow.end()),
            8.0f, 0.0001f);

        beginTest("Stereo visualization does not cancel anti-phase channels");
        megadsp::VisualizationData visualization;
        visualization.setSelectedSlot(0);
        juce::AudioBuffer<float> antiPhase(2, 1);
        antiPhase.setSample(0, 0, 1.0f);
        antiPhase.setSample(1, 0, -1.0f);
        visualization.captureInput(0, antiPhase);
        visualization.captureOutput(0, antiPhase);
        std::array<float, 1> captured {};
        visualization.slotData(0).input.copyLatest(captured);
        expectWithinAbsoluteError(std::abs(captured[0]), 1.0f, 0.0001f);
        std::array<float, 1> capturedLeft {};
        std::array<float, 1> capturedRight {};
        visualization.slotData(0).outputLeft.copyLatest(capturedLeft);
        visualization.slotData(0).outputRight.copyLatest(capturedRight);
        expectEquals(capturedLeft[0], 1.0f);
        expectEquals(capturedRight[0], -1.0f);

        beginTest("Semantic control mappings round trip displayed values");
        for (const auto& descriptor : megadsp::moduleDescriptors())
        {
            if (descriptor.type == megadsp::ModuleType::empty)
                continue;
            const auto defaults = megadsp::moduleDefaults(descriptor.type);
            for (int control = 0; control < megadsp::controlsPerSlot; ++control)
            {
                const auto& metadata = megadsp::controlMetadata(
                    descriptor.type, control);
                if (juce::String(metadata.label) == "-"
                    || metadata.kind == megadsp::ControlKind::toggle)
                    continue;
                const auto text = megadsp::formatControlValue(
                    descriptor.type, control,
                    defaults[static_cast<size_t>(control)]);
                const auto parsed = megadsp::parseControlValue(
                    descriptor.type, control, text);
                expect(parsed.has_value(),
                       juce::String(descriptor.name) + " "
                           + metadata.label + " did not parse");
                if (parsed.has_value())
                {
                    if (metadata.kind == megadsp::ControlKind::choice)
                    {
                        const auto options = megadsp::controlOptions(
                            descriptor.type, control);
                        expectEquals(
                            megadsp::discreteIndex(*parsed, options.size()),
                            megadsp::discreteIndex(
                                defaults[static_cast<size_t>(control)],
                                options.size()));
                    }
                    else
                    {
                        expectWithinAbsoluteError(
                            *parsed, defaults[static_cast<size_t>(control)],
                            0.03f);
                    }
                }
            }
        }

        beginTest("Discrete boundaries match DSP selection rules");
        expectEquals(megadsp::discreteIndex(0.0f, 3), 0);
        expectEquals(megadsp::discreteIndex(0.3332f, 3), 0);
        expectEquals(megadsp::discreteIndex(0.3334f, 3), 1);
        expectEquals(megadsp::discreteIndex(1.0f, 3), 2);
        expect(megadsp::equalizerBandMode(0.0f)
               == megadsp::EqualizerBandMode::bell);
        expect(megadsp::equalizerBandMode(0.5f)
               == megadsp::EqualizerBandMode::shelf);
        expect(megadsp::equalizerBandMode(1.0f)
               == megadsp::EqualizerBandMode::cut);
        expect(!megadsp::equalizerLowIsHighPass(0.5f));
        expect(megadsp::equalizerLowIsHighPass(1.0f));
        expect(!megadsp::equalizerHighIsLowPass(0.5f));
        expect(megadsp::equalizerHighIsLowPass(1.0f));
        expect(!megadsp::parseControlValue(
                    megadsp::ModuleType::compressor, 0, "not a level")
                    .has_value());
        for (int index = 0; index < 8; ++index)
            expectEquals(megadsp::discreteIndex(
                             megadsp::discreteValue(index, 8), 8),
                         index);

        beginTest("Musical defaults avoid generic half-value hazards");
        expectEquals(megadsp::stateSchemaVersion, 7);
        const auto reverbDefaults = megadsp::moduleDefaults(
            megadsp::ModuleType::algorithmicReverb);
        expectEquals(reverbDefaults[2], 1.0f);
        expectEquals(reverbDefaults[3], 0.10f);
        expectEquals(juce::String(megadsp::controlMetadata(
                         megadsp::ModuleType::algorithmicReverb, 2).label),
                     juce::String("Dry"));
        expectEquals(juce::String(megadsp::controlMetadata(
                         megadsp::ModuleType::algorithmicReverb, 3).label),
                     juce::String("Wet"));
        const auto convolutionDefaults = megadsp::moduleDefaults(
            megadsp::ModuleType::convolutionReverb);
        expectEquals(convolutionDefaults[2], 0.10f);
        expectEquals(convolutionDefaults[4], 1.0f);
        expectEquals(juce::String(megadsp::controlMetadata(
                         megadsp::ModuleType::convolutionReverb, 2).label),
                     juce::String("Wet"));
        expectEquals(juce::String(megadsp::controlMetadata(
                         megadsp::ModuleType::convolutionReverb, 4).label),
                     juce::String("Dry"));
        const auto compressorDefaults = megadsp::moduleDefaults(
            megadsp::ModuleType::compressor);
        expectEquals(compressorDefaults[5], 0.0f);
        expectEquals(compressorDefaults[6], 1.0f);
        expectEquals(compressorDefaults[7], 0.0f);
        const auto delayDefaults = megadsp::moduleDefaults(
            megadsp::ModuleType::delay);
        expectEquals(delayDefaults[4], 0.0f);
        expectEquals(delayDefaults[5], 1.0f);
        expectEquals(delayDefaults[8], 0.0f);
        const auto decoderDefaults = megadsp::moduleDefaults(
            megadsp::ModuleType::midSideDecoder);
        expectWithinAbsoluteError(decoderDefaults[0], 0.35f, 0.0001f);
        expectEquals(juce::String(megadsp::controlMetadata(
                         megadsp::ModuleType::midSideDecoder, 1).label),
                     juce::String("Swap Channels"));
        expectEquals(juce::String(megadsp::controlMetadata(
                         megadsp::ModuleType::midSideDecoder, 2).label),
                     juce::String("Mute Sides"));
        const auto limiterDefaults = megadsp::moduleDefaults(
            megadsp::ModuleType::limiter);
        expectEquals(
            megadsp::formatControlValue(
                megadsp::ModuleType::limiter, 0, limiterDefaults[0]),
            juce::String("-1.0 dB"));
        expectEquals(
            megadsp::formatControlValue(
                megadsp::ModuleType::limiter, 1, limiterDefaults[1]),
            juce::String("-1.0 dB"));
        expectEquals(
            megadsp::formatControlValue(
                megadsp::ModuleType::limiter, 2, limiterDefaults[2]),
            juce::String("100 ms"));
        expectEquals(
            megadsp::formatControlValue(
                megadsp::ModuleType::limiter, 3, limiterDefaults[3]),
            juce::String("5.0 ms"));
        expectEquals(limiterDefaults[4], 1.0f);
        const auto dynamicEqDefaults = megadsp::moduleDefaults(
            megadsp::ModuleType::dynamicEqualizer);
        expectEquals(megadsp::formatControlValue(
                         megadsp::ModuleType::dynamicEqualizer, 0,
                         dynamicEqDefaults[0]),
                     juce::String("6.00 kHz"));
        expectEquals(megadsp::formatControlValue(
                         megadsp::ModuleType::dynamicEqualizer, 2,
                         dynamicEqDefaults[2]),
                     juce::String("-6.0 dB"));
        expectEquals(megadsp::formatControlValue(
                         megadsp::ModuleType::dynamicEqualizer, 3,
                         dynamicEqDefaults[3]),
                     juce::String("-24.0 dB"));
        expectEquals(megadsp::formatControlValue(
                         megadsp::ModuleType::dynamicEqualizer, 7,
                         dynamicEqDefaults[7]),
                     juce::String("Bell"));
        expectEquals(megadsp::formatControlValue(
                         megadsp::ModuleType::dynamicEqualizer, 8,
                         dynamicEqDefaults[8]),
                     juce::String("RMS"));
        expectEquals(dynamicEqDefaults[9], 0.0f);
        expectEquals(dynamicEqDefaults[10], 0.0f);
        expectEquals(dynamicEqDefaults[11], 1.0f);
        const auto parsedDynamicFrequency = megadsp::parseControlValue(
            megadsp::ModuleType::dynamicEqualizer, 0, "6 kHz");
        expect(parsedDynamicFrequency.has_value());
        if (parsedDynamicFrequency.has_value())
            expectWithinAbsoluteError(
                *parsedDynamicFrequency, dynamicEqDefaults[0], 0.0001f);
        const auto randomGrainDefaults = megadsp::moduleDefaults(
            megadsp::ModuleType::randomGranulizer);
        expectEquals(megadsp::formatControlValue(
                         megadsp::ModuleType::randomGranulizer, 0,
                         randomGrainDefaults[0]),
                     juce::String("6 voices"));
        expectEquals(megadsp::formatControlValue(
                         megadsp::ModuleType::randomGranulizer, 1,
                         randomGrainDefaults[1]),
                     juce::String("80 ms"));
        expectEquals(megadsp::formatControlValue(
                         megadsp::ModuleType::randomGranulizer, 4,
                         randomGrainDefaults[4]),
                     juce::String("280 ms"));
        expectEquals(megadsp::formatControlValue(
                         megadsp::ModuleType::randomGranulizer, 4, 1.0f),
                     juce::String("2.00 s"));
        expectEquals(juce::String(megadsp::controlMetadata(
                         megadsp::ModuleType::randomGranulizer, 1).label),
                     juce::String("Size Minimum"));
        expectEquals(juce::String(megadsp::controlMetadata(
                         megadsp::ModuleType::randomGranulizer, 4).label),
                     juce::String("Size Maximum"));
        const auto parsedGrainSeconds = megadsp::parseControlValue(
            megadsp::ModuleType::randomGranulizer, 4, "1.5 s");
        expect(parsedGrainSeconds.has_value());
        if (parsedGrainSeconds.has_value())
            expectWithinAbsoluteError(
                megadsp::formatControlValue(
                    megadsp::ModuleType::randomGranulizer, 4,
                    *parsedGrainSeconds)
                    .getFloatValue(),
                1.5f, 0.001f);
        expectEquals(megadsp::formatControlValue(
                         megadsp::ModuleType::randomGranulizer, 8,
                         randomGrainDefaults[8]),
                     juce::String("12.0 kHz"));
        expectEquals(megadsp::formatControlValue(
                         megadsp::ModuleType::randomGranulizer, 9,
                         randomGrainDefaults[9]),
                     juce::String("10%"));
        expectEquals(megadsp::formatControlValue(
                         megadsp::ModuleType::randomGranulizer, 11,
                         randomGrainDefaults[11]),
                     juce::String("0.0 dB"));

        beginTest("Room scale names and centered pan language round trip");
        expectEquals(
            megadsp::formatControlValue(
                megadsp::ModuleType::algorithmicReverb, 1,
                reverbDefaults[1]),
            juce::String("Natural  ") + juce::String::charToString(0x00b7)
                + "  100%");
        const auto compactRoom = megadsp::parseControlValue(
            megadsp::ModuleType::algorithmicReverb, 1, "Compact");
        const auto largeRoom = megadsp::parseControlValue(
            megadsp::ModuleType::algorithmicReverb, 1, "Large");
        const auto vastRoom = megadsp::parseControlValue(
            megadsp::ModuleType::algorithmicReverb, 1, "Vast");
        expect(compactRoom.has_value());
        expect(largeRoom.has_value());
        expect(vastRoom.has_value());
        if (compactRoom.has_value())
            expectEquals(megadsp::formatControlValue(
                            megadsp::ModuleType::algorithmicReverb, 1,
                            *compactRoom)
                            .upToFirstOccurrenceOf("  ", false, false),
                        juce::String("Compact"));
        if (largeRoom.has_value())
            expectWithinAbsoluteError(
                *largeRoom, (150.0f - 25.0f) / 175.0f, 0.000001f);
        if (vastRoom.has_value())
            expectEquals(*vastRoom, 1.0f);
        const auto exactRoom = megadsp::parseControlValue(
            megadsp::ModuleType::algorithmicReverb, 1, "Large 137%");
        expect(exactRoom.has_value());
        if (exactRoom.has_value())
            expectWithinAbsoluteError(
                *exactRoom, (137.0f - 25.0f) / 175.0f, 0.000001f);

        expectEquals(megadsp::formatControlValue(
                        megadsp::ModuleType::stereoWidth, 4, 0.5f),
                     juce::String("C"));
        expectEquals(megadsp::formatControlValue(
                        megadsp::ModuleType::stereoWidth, 4, 0.325f),
                     juce::String("L 35"));
        expectEquals(megadsp::formatControlValue(
                        megadsp::ModuleType::stereoWidth, 4, 0.675f),
                     juce::String("R 35"));
        for (const auto& pan : {
                 std::pair<juce::String, float> { "L 35", 0.325f },
                 std::pair<juce::String, float> { "C", 0.5f },
                 std::pair<juce::String, float> { "R 35", 0.675f } })
        {
            const auto parsed = megadsp::parseControlValue(
                megadsp::ModuleType::stereoWidth, 4, pan.first);
            expect(parsed.has_value());
            if (parsed.has_value())
                expectWithinAbsoluteError(*parsed, pan.second, 0.000001f);
        }

        beginTest("Musician-facing labels retain their stable control indices");
        const auto expectLabel = [this](
                                    megadsp::ModuleType module, int control,
                                    const char* expected)
        {
            expectEquals(
                juce::String(megadsp::controlMetadata(module, control).label),
                juce::String(expected));
            expectEquals(
                juce::String(megadsp::descriptorFor(module)
                                .controlNames[static_cast<size_t>(control)]),
                juce::String(expected));
        };
        expectLabel(megadsp::ModuleType::compressor, 5, "Manual Trim");
        expectLabel(
            megadsp::ModuleType::algorithmicReverb, 1, "Room Scale");
        expectLabel(megadsp::ModuleType::rotarySpeaker, 2, "Rotor Balance");
        expectLabel(megadsp::ModuleType::rotarySpeaker, 4, "Motion");
        expectLabel(megadsp::ModuleType::rotarySpeaker, 7, "Spin-up");
        expectLabel(megadsp::ModuleType::rotarySpeaker, 8, "Cabinet Color");
        expectLabel(megadsp::ModuleType::rotarySpeaker, 9, "Ambience");
        expectLabel(
            megadsp::ModuleType::convolutionReverb, 3, "Output Trim");
        const std::array<const char*, megadsp::controlsPerSlot> grainLabels {
            "Voices", "Size Minimum", "Grain Rate", "Capture Range",
            "Size Maximum", "Reverse Chance", "Stereo Spread",
            "Rhythmic Delay Chance", "Brightness", "Regeneration",
            "Mix", "Output"
        };
        for (int control = 0; control < megadsp::controlsPerSlot; ++control)
            expectLabel(
                megadsp::ModuleType::randomGranulizer, control,
                grainLabels[static_cast<size_t>(control)]);
        expectLabel(megadsp::ModuleType::vintageChorus, 4, "Density");
        expectLabel(megadsp::ModuleType::vintageChorus, 6, "Regeneration");
        expectEquals(
            megadsp::moduleDefaults(megadsp::ModuleType::compressor)[5],
            compressorDefaults[5]);
        expectEquals(
            megadsp::moduleDefaults(megadsp::ModuleType::randomGranulizer)[4],
            randomGrainDefaults[4]);
        expectEquals(
            megadsp::moduleDefaults(megadsp::ModuleType::vintageChorus)[6],
            (8.0f + 75.0f) / 150.0f);

        beginTest("Mode and Sync context never rewrites inactive values");
        auto compressorContext = compressorDefaults;
        expect(!megadsp::isControlContextuallyVisible(
            megadsp::ModuleType::compressor, 5, compressorContext));
        compressorContext[8] = 0.0f;
        expect(megadsp::isControlContextuallyVisible(
            megadsp::ModuleType::compressor, 5, compressorContext));
        compressorContext[8] = 1.0f;
        compressorContext[5] = 0.1f;
        expect(megadsp::isControlContextuallyVisible(
            megadsp::ModuleType::compressor, 5, compressorContext));

        auto delayContext = delayDefaults;
        const auto retainedFreeTime = delayContext[0];
        expect(!megadsp::isControlContextuallyVisible(
            megadsp::ModuleType::delay, 0, delayContext));
        expect(megadsp::isControlContextuallyVisible(
            megadsp::ModuleType::delay, 6, delayContext));
        expect(!megadsp::isControlContextuallyVisible(
            megadsp::ModuleType::delay, 7, delayContext));
        delayContext[5] = 0.0f;
        expect(megadsp::isControlContextuallyVisible(
            megadsp::ModuleType::delay, 0, delayContext));
        expect(!megadsp::isControlContextuallyVisible(
            megadsp::ModuleType::delay, 6, delayContext));
        expectEquals(delayContext[0], retainedFreeTime);
        expect(!megadsp::isControlContextuallyEnabled(
            megadsp::ModuleType::delay, 4, delayContext, false, true));

        auto tremoloContext = megadsp::moduleDefaults(
            megadsp::ModuleType::tremolo);
        const auto retainedPitchDepth = tremoloContext[5];
        expect(megadsp::isControlContextuallyVisible(
            megadsp::ModuleType::tremolo, 1, tremoloContext));
        expect(!megadsp::isControlContextuallyVisible(
            megadsp::ModuleType::tremolo, 3, tremoloContext));
        expect(megadsp::isControlContextuallyVisible(
            megadsp::ModuleType::tremolo, 4, tremoloContext));
        expect(!megadsp::isControlContextuallyVisible(
            megadsp::ModuleType::tremolo, 5, tremoloContext));
        expect(!megadsp::isControlContextuallyVisible(
            megadsp::ModuleType::tremolo, 8, tremoloContext));
        tremoloContext[0] = megadsp::discreteValue(1, 3);
        expect(megadsp::isControlContextuallyVisible(
            megadsp::ModuleType::tremolo, 8, tremoloContext));
        tremoloContext[0] = megadsp::discreteValue(2, 3);
        expect(!megadsp::isControlContextuallyVisible(
            megadsp::ModuleType::tremolo, 4, tremoloContext));
        expect(megadsp::isControlContextuallyVisible(
            megadsp::ModuleType::tremolo, 5, tremoloContext));
        tremoloContext[2] = 1.0f;
        expect(!megadsp::isControlContextuallyVisible(
            megadsp::ModuleType::tremolo, 1, tremoloContext));
        expect(megadsp::isControlContextuallyVisible(
            megadsp::ModuleType::tremolo, 3, tremoloContext));
        expectEquals(tremoloContext[5], retainedPitchDepth);

        beginTest("Direct graph controls do not fall back to a generic bank");
        const auto expectGraphOwned = [this](
                                          megadsp::ModuleType module,
                                          const auto& graphControls)
        {
            const auto defaults = megadsp::moduleDefaults(module);
            for (const auto control : graphControls)
                expect(!megadsp::isControlContextuallyVisible(
                    module, control, defaults));
        };
        expectGraphOwned(
            megadsp::ModuleType::algorithmicReverb,
            std::array<int, 4> { 0, 1, 10, 11 });
        expectGraphOwned(
            megadsp::ModuleType::stereoWidth,
            std::array<int, 2> { 0, 2 });
        expectGraphOwned(
            megadsp::ModuleType::rotarySpeaker,
            std::array<int, 5> { 2, 4, 5, 6, 7 });
        expectGraphOwned(
            megadsp::ModuleType::convolutionReverb,
            std::array<int, 2> { 0, 1 });
        expect(megadsp::controlMetadata(
                   megadsp::ModuleType::algorithmicReverb, 2).kind
               == megadsp::ControlKind::level);
        expect(megadsp::controlMetadata(
                   megadsp::ModuleType::algorithmicReverb, 3).kind
               == megadsp::ControlKind::level);
        expect(megadsp::controlMetadata(
                   megadsp::ModuleType::convolutionReverb, 2).kind
               == megadsp::ControlKind::level);
        expect(megadsp::controlMetadata(
                   megadsp::ModuleType::convolutionReverb, 4).kind
               == megadsp::ControlKind::level);

        beginTest("UX presentation preserves every normalized module default");
        const auto linearDefault = [](float low, float high, float plain)
        {
            return (plain - low) / (high - low);
        };
        const auto exponentialDefault = [](float low, float high, float plain)
        {
            return std::log(plain / low) / std::log(high / low);
        };
        const auto expectDefaults = [this](
                                        megadsp::ModuleType module,
                                        const std::array<
                                            float,
                                            megadsp::controlsPerSlot>& expected)
        {
            const auto actual = megadsp::moduleDefaults(module);
            for (int control = 0; control < megadsp::controlsPerSlot;
                 ++control)
                expectEquals(
                    actual[static_cast<size_t>(control)],
                    expected[static_cast<size_t>(control)],
                    juce::String(megadsp::moduleDefinition(module).displayName)
                        + " control " + juce::String(control + 1));
        };
        expectDefaults(megadsp::ModuleType::compressor, {
            linearDefault(-60.0f, 0.0f, -18.0f),
            exponentialDefault(1.0f, 20.0f, 3.0f),
            exponentialDefault(0.1f, 100.0f, 10.0f),
            exponentialDefault(10.0f, 1000.0f, 150.0f),
            linearDefault(0.0f, 18.0f, 6.0f),
            0.0f, 1.0f, 0.0f, 1.0f, 0.5f, 0.5f, 0.5f
        });
        expectDefaults(megadsp::ModuleType::delay, {
            exponentialDefault(1.0f, 2000.0f, 250.0f),
            0.30f / 0.95f, 0.20f,
            exponentialDefault(800.0f, 20000.0f, 8000.0f),
            0.0f, 1.0f, megadsp::discreteValue(5, 8),
            exponentialDefault(0.05f, 8.0f, 0.5f),
            0.0f, 0.5f, 0.5f, 0.5f
        });
        expectDefaults(megadsp::ModuleType::algorithmicReverb, {
            exponentialDefault(0.2f, 12.0f, 2.0f),
            linearDefault(25.0f, 200.0f, 100.0f),
            1.0f, 0.10f, megadsp::discreteValue(0, 3),
            20.0f / 250.0f, 0.55f, 0.20f,
            100.0f / 150.0f, 0.50f,
            exponentialDefault(20.0f, 1000.0f, 120.0f),
            exponentialDefault(2000.0f, 20000.0f, 12000.0f)
        });
        expectDefaults(megadsp::ModuleType::stereoWidth, {
            0.60f, 0.22f,
            exponentialDefault(20.0f, 500.0f, 120.0f),
            exponentialDefault(500.0f, 8000.0f, 1800.0f),
            0.5f, 1.0f, 0.5f, 1.0f, 0.5f, 0.5f, 0.5f, 0.5f
        });
        expectDefaults(megadsp::ModuleType::tremolo, {
            megadsp::discreteValue(0, 3),
            exponentialDefault(0.05f, 20.0f, 4.0f),
            0.0f, megadsp::discreteValue(4, 8), 0.70f,
            linearDefault(0.0f, 100.0f, 24.0f), 0.0f,
            linearDefault(0.0f, 180.0f, 0.0f),
            exponentialDefault(100.0f, 4000.0f, 700.0f),
            1.0f, linearDefault(-12.0f, 12.0f, 0.0f), 0.5f
        });
        expectDefaults(megadsp::ModuleType::rotarySpeaker, {
            megadsp::discreteValue(1, 3),
            linearDefault(0.0f, 24.0f, 6.0f), 0.5f,
            exponentialDefault(500.0f, 1400.0f, 800.0f),
            0.75f, linearDefault(20.0f, 200.0f, 70.0f),
            linearDefault(0.0f, 180.0f, 110.0f), 0.5f,
            0.65f, 0.18f, 1.0f,
            linearDefault(-12.0f, 12.0f, 0.0f)
        });
        expectDefaults(megadsp::ModuleType::convolutionReverb, {
            exponentialDefault(20.0f, 1000.0f, 20.0f),
            exponentialDefault(2000.0f, 20000.0f, 20000.0f),
            0.10f, linearDefault(-18.0f, 18.0f, 0.0f),
            1.0f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f
        });
        expectDefaults(megadsp::ModuleType::dynamicEqualizer, {
            exponentialDefault(20.0f, 20000.0f, 6000.0f),
            exponentialDefault(0.2f, 12.0f, 3.0f),
            linearDefault(-18.0f, 12.0f, -6.0f),
            linearDefault(-60.0f, 0.0f, -24.0f),
            exponentialDefault(1.0f, 10.0f, 3.0f),
            exponentialDefault(0.1f, 100.0f, 5.0f),
            exponentialDefault(10.0f, 1000.0f, 100.0f),
            megadsp::discreteValue(0, 3),
            megadsp::discreteValue(1, 2),
            0.0f, 0.0f, 1.0f
        });
        expectDefaults(megadsp::ModuleType::randomGranulizer, {
            linearDefault(1.0f, 16.0f, 6.0f),
            exponentialDefault(50.0f, 2000.0f, 80.0f),
            exponentialDefault(0.5f, 30.0f, 6.0f),
            0.35f,
            exponentialDefault(50.0f, 2000.0f, 280.0f),
            0.20f, 0.65f, 0.30f,
            exponentialDefault(500.0f, 20000.0f, 12000.0f),
            0.10f / 0.80f, 0.35f,
            linearDefault(-18.0f, 12.0f, 0.0f)
        });
        expectDefaults(megadsp::ModuleType::vintageChorus, {
            megadsp::discreteValue(0, 4),
            exponentialDefault(0.05f, 8.0f, 0.8f),
            0.45f,
            exponentialDefault(2.0f, 30.0f, 9.0f),
            linearDefault(1.0f, 6.0f, 2.0f),
            0.5f,
            linearDefault(-75.0f, 75.0f, 8.0f),
            exponentialDefault(800.0f, 18000.0f, 10000.0f),
            0.25f, 0.5f, 0.40f,
            linearDefault(-18.0f, 12.0f, 0.0f)
        });
        expectDefaults(megadsp::ModuleType::beatPermuter, {
            megadsp::discreteValue(3, 6), 0.35f,
            megadsp::discreteValue(0, 4),
            linearDefault(1.0f, 8.0f, 4.0f),
            linearDefault(1.0f, 8.0f, 2.0f),
            linearDefault(20.0f, 100.0f, 90.0f),
            0.5f, 0.20f, 0.25f, 0.10f / 0.75f, 0.35f,
            linearDefault(-18.0f, 12.0f, 0.0f)
        });
        expectDefaults(megadsp::ModuleType::spectralPrism, {
            linearDefault(-100.0f, 100.0f, 20.0f),
            exponentialDefault(80.0f, 8000.0f, 1000.0f),
            0.5f, 0.20f, 0.0f,
            exponentialDefault(0.02f, 4.0f, 0.15f),
            0.15f, 0.15f, 0.35f, 0.65f, 0.40f,
            linearDefault(-18.0f, 12.0f, 0.0f)
        });
        expectDefaults(megadsp::ModuleType::resonantMatrix, {
            exponentialDefault(27.5f, 440.0f, 110.0f),
            megadsp::discreteValue(3, 6),
            linearDefault(1.0f, 4.0f, 2.0f),
            megadsp::discreteValue(0, 4),
            exponentialDefault(0.10f, 12.0f, 2.5f),
            exponentialDefault(500.0f, 20000.0f, 8000.0f),
            linearDefault(0.0f, 30.0f, 4.0f),
            exponentialDefault(0.02f, 2.0f, 0.10f),
            linearDefault(0.0f, 50.0f, 6.0f),
            linearDefault(0.0f, 150.0f, 80.0f), 0.25f,
            linearDefault(-18.0f, 12.0f, 0.0f)
        });
        expectDefaults(megadsp::ModuleType::wavefoldGarden, {
            megadsp::discreteValue(0, 4),
            linearDefault(0.0f, 36.0f, 6.0f),
            linearDefault(1.0f, 8.0f, 2.0f), 0.5f, 0.35f,
            linearDefault(-100.0f, 100.0f, 25.0f),
            exponentialDefault(0.1f, 100.0f, 8.0f),
            exponentialDefault(10.0f, 1000.0f, 120.0f),
            exponentialDefault(500.0f, 20000.0f, 12000.0f),
            0.20f, 0.45f,
            linearDefault(-18.0f, 12.0f, 0.0f)
        });
        expectDefaults(megadsp::ModuleType::gateExpander, {
            linearDefault(-80.0f, 0.0f, -36.0f),
            linearDefault(0.0f, 80.0f, 24.0f),
            exponentialDefault(0.05f, 100.0f, 2.0f),
            linearDefault(0.0f, 500.0f, 50.0f),
            exponentialDefault(5.0f, 2000.0f, 180.0f),
            linearDefault(0.0f, 18.0f, 6.0f),
            exponentialDefault(20.0f, 2000.0f, 80.0f),
            exponentialDefault(1000.0f, 20000.0f, 12000.0f),
            0.0f, 0.0f, 1.0f, 0.5f
        });
        expectDefaults(megadsp::ModuleType::transientDesigner, {
            0.5f, 0.5f, 0.5f,
            exponentialDefault(5.0f, 200.0f, 40.0f),
            exponentialDefault(80.0f, 8000.0f, 1500.0f),
            1.0f, 1.0f, linearDefault(-18.0f, 18.0f, 0.0f),
            0.5f, 0.5f, 0.5f, 0.5f
        });
        expectDefaults(megadsp::ModuleType::multibandCompressor, {
            exponentialDefault(40.0f, 800.0f, 180.0f),
            exponentialDefault(1000.0f, 12000.0f, 3500.0f),
            linearDefault(-60.0f, 0.0f, -18.0f),
            linearDefault(-60.0f, 0.0f, -18.0f),
            linearDefault(-60.0f, 0.0f, -18.0f),
            exponentialDefault(1.0f, 20.0f, 3.0f),
            exponentialDefault(0.1f, 100.0f, 15.0f),
            exponentialDefault(20.0f, 2000.0f, 180.0f),
            1.0f, 1.0f, 1.0f,
            linearDefault(-18.0f, 12.0f, 0.0f)
        });
        expectDefaults(megadsp::ModuleType::studioPhaser, {
            megadsp::discreteValue(2, 5),
            exponentialDefault(0.02f, 12.0f, 0.35f),
            0.0f, megadsp::discreteValue(4, 8), 0.60f,
            exponentialDefault(80.0f, 8000.0f, 900.0f),
            exponentialDefault(0.25f, 6.0f, 3.0f),
            linearDefault(-95.0f, 95.0f, 20.0f),
            linearDefault(0.0f, 180.0f, 90.0f),
            0.5f, linearDefault(-18.0f, 12.0f, 0.0f), 0.5f
        });
        expectDefaults(megadsp::ModuleType::studioFlanger, {
            megadsp::discreteValue(0, 4),
            exponentialDefault(0.02f, 12.0f, 0.2f),
            0.0f, megadsp::discreteValue(4, 8),
            linearDefault(0.0f, 10.0f, 2.5f),
            exponentialDefault(0.1f, 15.0f, 3.0f),
            linearDefault(-90.0f, 90.0f, 15.0f),
            linearDefault(0.0f, 180.0f, 90.0f),
            exponentialDefault(1000.0f, 20000.0f, 12000.0f),
            0.5f, linearDefault(-18.0f, 12.0f, 0.0f), 0.5f
        });
        expectDefaults(megadsp::ModuleType::diffusionDelay, {
            exponentialDefault(10.0f, 2000.0f, 500.0f),
            1.0f, megadsp::discreteValue(5, 8),
            0.35f / 0.90f, 0.30f, 0.15f,
            exponentialDefault(20.0f, 2000.0f, 120.0f),
            exponentialDefault(1000.0f, 20000.0f, 10000.0f),
            100.0f / 150.0f, 0.20f, 0.20f,
            linearDefault(-18.0f, 12.0f, 0.0f)
        });
        expectDefaults(megadsp::ModuleType::pitchBloom, {
            megadsp::discreteValue(2, 5), 0.5f,
            exponentialDefault(20.0f, 500.0f, 120.0f),
            0.30f / 0.85f, 0.35f, 0.75f,
            exponentialDefault(20.0f, 2000.0f, 180.0f),
            exponentialDefault(1000.0f, 20000.0f, 10000.0f),
            0.20f, 0.15f, linearDefault(-18.0f, 12.0f, 0.0f), 0.5f
        });
        expectDefaults(megadsp::ModuleType::frequencyLab, {
            linearDefault(-5000.0f, 5000.0f, 100.0f), 0.5f, 0.5f,
            exponentialDefault(0.02f, 20.0f, 0.2f), 0.0f, 0.5f,
            exponentialDefault(20.0f, 2000.0f, 40.0f),
            exponentialDefault(1000.0f, 20000.0f, 16000.0f),
            1.0f, linearDefault(-18.0f, 12.0f, 0.0f), 0.5f, 0.5f
        });
        expectDefaults(megadsp::ModuleType::spatialOrbit, {
            megadsp::discreteValue(0, 4),
            exponentialDefault(0.02f, 5.0f, 0.1f),
            0.0f, megadsp::discreteValue(2, 8),
            linearDefault(0.0f, 360.0f, 180.0f),
            linearDefault(0.0f, 200.0f, 100.0f),
            exponentialDefault(0.5f, 10.0f, 2.0f),
            0.15f, 0.35f,
            exponentialDefault(20.0f, 500.0f, 120.0f),
            1.0f, linearDefault(-18.0f, 12.0f, 0.0f)
        });
        expectDefaults(megadsp::ModuleType::signalDecay, {
            linearDefault(4.0f, 24.0f, 16.0f),
            exponentialDefault(1.0f, 48.0f, 32.0f),
            0.05f, 0.03f,
            exponentialDefault(1000.0f, 20000.0f, 14000.0f),
            linearDefault(-90.0f, -24.0f, -72.0f),
            0.08f, 0.05f, 0.10f, 1.0f,
            linearDefault(-18.0f, 12.0f, 0.0f), 0.5f
        });
        expectDefaults(megadsp::ModuleType::analogTape, {
            megadsp::discreteValue(1, 4),
            linearDefault(-18.0f, 18.0f, 0.0f),
            0.30f, 0.5f, megadsp::discreteValue(2, 4),
            0.35f, 0.20f, 0.20f, 0.15f, 0.20f, 1.0f,
            linearDefault(-18.0f, 12.0f, 0.0f)
        });
        expectDefaults(megadsp::ModuleType::resonanceTamer, {
            6.0f / 18.0f, megadsp::discreteValue(1, 3),
            megadsp::discreteValue(1, 3), 0.5f,
            exponentialDefault(20.0f, 2000.0f, 80.0f),
            exponentialDefault(1000.0f, 20000.0f, 12000.0f),
            0.65f, 1.0f, linearDefault(-18.0f, 12.0f, 0.0f),
            0.5f, 0.5f, 0.5f
        });
        expectDefaults(megadsp::ModuleType::spectralBalance, {
            megadsp::discreteValue(0, 5), 0.5f, 0.5f, 0.5f, 0.5f,
            exponentialDefault(0.5f, 30.0f, 5.0f),
            megadsp::discreteValue(1, 3), 0.65f,
            linearDefault(-18.0f, 12.0f, 0.0f),
            0.5f, 0.5f, 0.5f
        });
        expectDefaults(megadsp::ModuleType::phaseCoherence, {
            megadsp::discreteValue(0, 3),
            exponentialDefault(40.0f, 800.0f, 180.0f),
            0.75f, 0.5f, 0.5f, 0.75f,
            exponentialDefault(20.0f, 500.0f, 40.0f),
            linearDefault(-18.0f, 12.0f, 0.0f),
            0.5f, 0.5f, 0.5f, 0.5f
        });
        expectDefaults(megadsp::ModuleType::loudnessRider, {
            linearDefault(-36.0f, -8.0f, -20.0f), 0.5f,
            megadsp::discreteValue(1, 3),
            exponentialDefault(0.25f, 10.0f, 1.5f),
            0.4f, 0.5f, 0.65f,
            linearDefault(-70.0f, -30.0f, -56.0f),
            linearDefault(-18.0f, 12.0f, 0.0f),
            0.5f, 0.5f, 0.5f
        });
        expectDefaults(megadsp::ModuleType::adaptiveClipper, {
            0.25f, linearDefault(-12.0f, 0.0f, -1.0f),
            megadsp::discreteValue(0, 3), 0.65f, 0.55f,
            exponentialDefault(20.0f, 1000.0f, 120.0f),
            1.0f, megadsp::discreteValue(1, 3), 1.0f, 1.0f,
            linearDefault(-18.0f, 12.0f, 0.0f), 0.5f
        });
        expectDefaults(megadsp::ModuleType::spectralDelayCanvas, {
            1.0f, exponentialDefault(10.0f, 4000.0f, 500.0f),
            megadsp::discreteValue(3, 7), 0.5f, 0.75f, 1.0f,
            0.35f, 0.25f, 0.5f, 0.0f, 0.35f,
            linearDefault(-18.0f, 12.0f, 0.0f)
        });
        expectDefaults(megadsp::ModuleType::formantForge, {
            megadsp::discreteValue(0, 4), 0.35f, 0.45f, 0.5f, 0.45f,
            0.12f, exponentialDefault(0.02f, 6.0f, 0.15f),
            0.15f, 0.35f, 1.0f,
            linearDefault(-18.0f, 12.0f, 0.0f), 0.5f
        });
        expectDefaults(megadsp::ModuleType::harmonicMirage, {
            megadsp::discreteValue(0, 4), megadsp::discreteValue(1, 3),
            6.0f / 22.0f, 0.5f, 0.10f, 0.10f, 0.5f,
            0.65f, 0.35f, 0.35f,
            linearDefault(-18.0f, 12.0f, 0.0f), 0.5f
        });
        expectDefaults(megadsp::ModuleType::chaosField, {
            megadsp::discreteValue(0, 3),
            exponentialDefault(0.015f, 7.0f, 0.15f),
            0.0f, megadsp::discreteValue(3, 8), 0.50f,
            exponentialDefault(80.0f, 10000.0f, 1200.0f),
            exponentialDefault(2.0f, 600.0f, 40.0f),
            0.50f, 0.70f, 0.60f, 0.45f,
            linearDefault(-18.0f, 12.0f, 0.0f)
        });
        expectDefaults(megadsp::ModuleType::timeMosaic, {
            0.5f, 0.35f, 0.45f,
            0.5f, 0.4f, 0.75f, 0.0f, 0.0f, 0.5f, 0.35f,
            linearDefault(-18.0f, 12.0f, 0.0f), 0.5f
        });

        beginTest("Host parameter contract remains fixed");
        expectEquals(megadsp::moduleTypeCount, 40);
        expectEquals(megadsp::stateSchemaVersion, 7);
        expectEquals(static_cast<int>(megadsp::ModulePresentation::count), 40);
        expectEquals(static_cast<int>(megadsp::ModuleType::empty), 0);
        expectEquals(static_cast<int>(megadsp::ModuleType::equalizer), 1);
        expectEquals(static_cast<int>(megadsp::ModuleType::compressor), 2);
        expectEquals(static_cast<int>(megadsp::ModuleType::saturator), 3);
        expectEquals(static_cast<int>(megadsp::ModuleType::delay), 4);
        expectEquals(static_cast<int>(megadsp::ModuleType::limiter), 5);
        expectEquals(static_cast<int>(megadsp::ModuleType::algorithmicReverb), 6);
        expectEquals(static_cast<int>(megadsp::ModuleType::stereoWidth), 7);
        expectEquals(static_cast<int>(megadsp::ModuleType::midSideDecoder), 8);
        expectEquals(static_cast<int>(megadsp::ModuleType::tremolo), 9);
        expectEquals(static_cast<int>(megadsp::ModuleType::rotarySpeaker), 10);
        expectEquals(static_cast<int>(
                         megadsp::ModuleType::convolutionReverb), 11);
        expectEquals(static_cast<int>(
                         megadsp::ModuleType::dynamicEqualizer), 12);
        expectEquals(static_cast<int>(
                         megadsp::ModuleType::randomGranulizer), 13);
        expectEquals(static_cast<int>(
                         megadsp::ModuleType::vintageChorus), 14);
        expectEquals(static_cast<int>(
                         megadsp::ModuleType::beatPermuter), 15);
        expectEquals(static_cast<int>(
                         megadsp::ModuleType::spectralPrism), 16);
        expectEquals(static_cast<int>(
                         megadsp::ModuleType::resonantMatrix), 17);
        expectEquals(static_cast<int>(
                         megadsp::ModuleType::wavefoldGarden), 18);
        expectEquals(static_cast<int>(
                         megadsp::ModuleType::gateExpander), 19);
        expectEquals(static_cast<int>(
                         megadsp::ModuleType::transientDesigner), 20);
        expectEquals(static_cast<int>(
                         megadsp::ModuleType::multibandCompressor), 21);
        expectEquals(static_cast<int>(
                         megadsp::ModuleType::studioPhaser), 22);
        expectEquals(static_cast<int>(
                         megadsp::ModuleType::studioFlanger), 23);
        expectEquals(static_cast<int>(
                         megadsp::ModuleType::diffusionDelay), 24);
        expectEquals(static_cast<int>(
                         megadsp::ModuleType::pitchBloom), 25);
        expectEquals(static_cast<int>(
                         megadsp::ModuleType::frequencyLab), 26);
        expectEquals(static_cast<int>(
                         megadsp::ModuleType::spatialOrbit), 27);
        expectEquals(static_cast<int>(
                         megadsp::ModuleType::signalDecay), 28);
        expectEquals(static_cast<int>(
                         megadsp::ModuleType::analogTape), 29);
        expectEquals(static_cast<int>(
                         megadsp::ModuleType::resonanceTamer), 30);
        expectEquals(static_cast<int>(
                         megadsp::ModuleType::spectralBalance), 31);
        expectEquals(static_cast<int>(
                         megadsp::ModuleType::phaseCoherence), 32);
        expectEquals(static_cast<int>(
                         megadsp::ModuleType::loudnessRider), 33);
        expectEquals(static_cast<int>(
                         megadsp::ModuleType::adaptiveClipper), 34);
        expectEquals(static_cast<int>(
                         megadsp::ModuleType::spectralDelayCanvas), 35);
        expectEquals(static_cast<int>(
                         megadsp::ModuleType::formantForge), 36);
        expectEquals(static_cast<int>(
                         megadsp::ModuleType::harmonicMirage), 37);
        expectEquals(static_cast<int>(
                         megadsp::ModuleType::chaosField), 38);
        expectEquals(static_cast<int>(
                         megadsp::ModuleType::timeMosaic), 39);
        expectEquals(static_cast<int>(megadsp::moduleDescriptors().size()), 40);
        juce::AudioProcessorGraph contractOwner;
        juce::AudioProcessorValueTreeState contractState(
            contractOwner, nullptr, "contractState",
            megadsp::createParameterLayout());
        expectEquals(contractOwner.getParameters().size(),
                     megadsp::slotCount
                         * (megadsp::controlsPerSlot + 2) + 2);
        expectEquals(contractOwner.getParameters().size(), 114);
        expect(dynamic_cast<juce::AudioParameterFloat*>(
                   contractState.getParameter(
                       megadsp::slotParameterId(0, "bypass")))
               != nullptr);
        auto* moduleChoice = dynamic_cast<juce::AudioParameterChoice*>(
            contractState.getParameter(megadsp::slotParameterId(0, "type")));
        expect(moduleChoice != nullptr);
        if (moduleChoice != nullptr)
        {
            for (int stableType = 0;
                 stableType < megadsp::moduleTypeCount; ++stableType)
            {
                moduleChoice->setValueNotifyingHost(
                    moduleChoice->convertTo0to1(
                        static_cast<float>(stableType)));
                expectEquals(moduleChoice->getIndex(), stableType);
            }
            moduleChoice->setValueNotifyingHost(
                moduleChoice->convertTo0to1(0.0f));
        }

        beginTest("Immutable module registry is complete and valid");
        const auto registryErrors = megadsp::validateModuleRegistry();
        expect(registryErrors.isEmpty(), registryErrors.joinIntoString("\n"));
        expectEquals(static_cast<int>(megadsp::moduleRegistry().size()), 40);
        std::array<std::unique_ptr<megadsp::DspModule>,
                   megadsp::moduleTypeCount> factoryProducts {};
        for (int stableType = 0; stableType < megadsp::moduleTypeCount;
             ++stableType)
        {
            const auto type = static_cast<megadsp::ModuleType>(stableType);
            const auto* definition = megadsp::findModuleDefinition(type);
            expect(definition != nullptr);
            if (definition == nullptr)
               continue;
            expectEquals(static_cast<int>(definition->type), stableType);
            expectEquals(static_cast<int>(definition->presentation),
                         stableType);
            const auto expectedCapabilities =
                type == megadsp::ModuleType::convolutionReverb
                    ? megadsp::ModuleCapability::impulseResponse
                    : type == megadsp::ModuleType::randomGranulizer
                          ? megadsp::ModuleCapability::grainVisualization
                          : type == megadsp::ModuleType::beatPermuter
                                ? megadsp::ModuleCapability::
                                      beatPermutationVisualization
                          : type == megadsp::ModuleType::signalDecay
                              || type == megadsp::ModuleType::timeMosaic
                          ? megadsp::ModuleCapability::continuousTelemetry
                                | megadsp::ModuleCapability::eventTelemetry
                          : type == megadsp::ModuleType::diffusionDelay
                              || type == megadsp::ModuleType::pitchBloom
                          ? megadsp::ModuleCapability::eventTelemetry
                          : type == megadsp::ModuleType::studioPhaser
                              || type == megadsp::ModuleType::studioFlanger
                              || type == megadsp::ModuleType::frequencyLab
                              || type == megadsp::ModuleType::spatialOrbit
                              || type == megadsp::ModuleType::analogTape
                              || type == megadsp::ModuleType::resonanceTamer
                              || type == megadsp::ModuleType::spectralBalance
                              || type == megadsp::ModuleType::phaseCoherence
                              || type == megadsp::ModuleType::loudnessRider
                              || type == megadsp::ModuleType::adaptiveClipper
                              || type == megadsp::ModuleType::spectralDelayCanvas
                              || type == megadsp::ModuleType::formantForge
                              || type == megadsp::ModuleType::harmonicMirage
                              || type == megadsp::ModuleType::chaosField
                              || type == megadsp::ModuleType::gateExpander
                              || type == megadsp::ModuleType::transientDesigner
                              || type
                                     == megadsp::ModuleType::
                                            multibandCompressor
                          ? megadsp::ModuleCapability::continuousTelemetry
                          : megadsp::ModuleCapability::none;
            expect(definition->capabilities == expectedCapabilities);
            const auto& descriptor = megadsp::descriptorFor(type);
            expectEquals(juce::String(descriptor.name),
                        juce::String(definition->displayName));
            const auto defaults = megadsp::moduleDefaults(type);
            for (int control = 0; control < megadsp::controlsPerSlot;
                ++control)
            {
               const auto index = static_cast<size_t>(control);
               expectEquals(juce::String(descriptor.controlNames[index]),
                            juce::String(definition->controls[index].name));
               expectEquals(defaults[index],
                            definition->controls[index].defaultValue);
               expect(&megadsp::controlMetadata(type, control)
                      == &definition->controls[index].metadata);
               expect(megadsp::controlOptions(type, control)
                      == definition->controls[index].options.strings());
            }

            auto module = megadsp::createDspModule(type);
            if (type == megadsp::ModuleType::empty)
               expect(module == nullptr);
            else
            {
               expect(module != nullptr);
               expect((module->impulseResponseCapability() != nullptr)
                      == megadsp::hasCapability(
                          definition->capabilities,
                          megadsp::ModuleCapability::impulseResponse));
               expect((module->grainVisualizationCapability() != nullptr)
                      == megadsp::hasCapability(
                          definition->capabilities,
                          megadsp::ModuleCapability::grainVisualization));
                expect((module->beatPermutationVisualizationCapability()
                        != nullptr)
                       == megadsp::hasCapability(
                          definition->capabilities,
                          megadsp::ModuleCapability::
                              beatPermutationVisualization));
                expect((module->continuousTelemetryCapability() != nullptr)
                       == megadsp::hasCapability(
                           definition->capabilities,
                           megadsp::ModuleCapability::continuousTelemetry));
                expect((module->eventTelemetryCapability() != nullptr)
                       == megadsp::hasCapability(
                           definition->capabilities,
                           megadsp::ModuleCapability::eventTelemetry));
               factoryProducts[static_cast<size_t>(stableType)] =
                   std::move(module);
            }
        }
        for (int first = 1; first < megadsp::moduleTypeCount; ++first)
            for (int second = first + 1;
                second < megadsp::moduleTypeCount; ++second)
               expect(factoryProducts[static_cast<size_t>(first)].get()
                      != factoryProducts[static_cast<size_t>(second)].get());
        expect(megadsp::createDspModule(
                  static_cast<megadsp::ModuleType>(999)) == nullptr);

        beginTest("Appended choice and value contracts remain exact");
        const juce::StringArray tempoDivisions {
            "4 bars", "2 bars", "1 bar", "1/2",
            "1/4", "1/8", "1/8.", "1/16"
        };
        expect(megadsp::controlOptions(
                   megadsp::ModuleType::studioPhaser, 0)
               == juce::StringArray { "2", "4", "6", "8", "12" });
        expect(megadsp::controlOptions(
                   megadsp::ModuleType::studioPhaser, 3)
               == tempoDivisions);
        expect(megadsp::controlOptions(
                   megadsp::ModuleType::studioFlanger, 0)
               == juce::StringArray {
                      "Tape", "Through-Zero", "Jet", "BBD" });
        expect(megadsp::controlOptions(
                   megadsp::ModuleType::studioFlanger, 3)
               == tempoDivisions);
        expect(megadsp::controlOptions(
                   megadsp::ModuleType::diffusionDelay, 2)
               == juce::StringArray {
                      "1/32", "1/16", "1/16.", "1/8",
                      "1/8.", "1/4", "1/4.", "1/2" });
        expect(megadsp::controlOptions(
                   megadsp::ModuleType::diffusionDelay, 3).isEmpty());
        expect(megadsp::controlOptions(
                   megadsp::ModuleType::pitchBloom, 0)
               == juce::StringArray {
                      "Unison", "Fifth", "Octave", "Octave + Fifth",
                      "Two Octaves" });
        expect(megadsp::controlOptions(
                   megadsp::ModuleType::spatialOrbit, 0)
               == juce::StringArray {
                      "Circle", "Figure Eight", "Pendulum", "Wander" });
        expect(megadsp::controlOptions(
                   megadsp::ModuleType::spatialOrbit, 3)
               == tempoDivisions);
        expect(megadsp::controlOptions(
                  megadsp::ModuleType::equalizer, 10)
               == juce::StringArray {
                     "Bell", "Low Shelf", "High Pass" });
        expect(megadsp::controlOptions(
                  megadsp::ModuleType::equalizer, 11)
               == juce::StringArray {
                     "Bell", "High Shelf", "Low Pass" });
        const auto parsedLowShelf = megadsp::parseControlValue(
            megadsp::ModuleType::equalizer, 10, "Low Shelf");
        expect(parsedLowShelf.has_value());
        if (parsedLowShelf.has_value())
            expect(megadsp::equalizerBandMode(*parsedLowShelf)
                  == megadsp::EqualizerBandMode::shelf);
        expect(megadsp::controlOptions(
                  megadsp::ModuleType::analogTape, 0)
               == juce::StringArray {
                     "Worn Cassette", "Consumer Reel", "Ampex-Style Deck",
                     "Studer-Style Deck" });
        expect(megadsp::controlOptions(
                  megadsp::ModuleType::analogTape, 4)
               == juce::StringArray {
                     "3.75 ips", "7.5 ips", "15 ips", "30 ips" });
        const auto tapeDefaults =
            megadsp::moduleDefaults(megadsp::ModuleType::analogTape);
        expectEquals(megadsp::formatControlValue(
                        megadsp::ModuleType::analogTape, 3,
                        tapeDefaults[3]),
                    juce::String("Neutral"));
        const auto parsedTapeBias = megadsp::parseControlValue(
            megadsp::ModuleType::analogTape, 3, "Under 50%");
        expect(parsedTapeBias.has_value());
        if (parsedTapeBias.has_value())
            expectWithinAbsoluteError(*parsedTapeBias, 0.25f, 0.0001f);
        const auto transientDefaults =
            megadsp::moduleDefaults(megadsp::ModuleType::transientDesigner);
        expectEquals(megadsp::formatControlValue(
                         megadsp::ModuleType::transientDesigner, 7,
                         transientDefaults[7]),
                     juce::String("0.0 dB"));
        const auto parsedTransientMaximum = megadsp::parseControlValue(
            megadsp::ModuleType::transientDesigner, 7, "+18 dB");
        expect(parsedTransientMaximum.has_value());
        if (parsedTransientMaximum.has_value())
            expectEquals(*parsedTransientMaximum, 1.0f);
        const auto diffusionDefaults =
            megadsp::moduleDefaults(megadsp::ModuleType::diffusionDelay);
        expectEquals(megadsp::formatControlValue(
                         megadsp::ModuleType::diffusionDelay, 3,
                         diffusionDefaults[3]),
                     juce::String("35%"));
        const auto parsedDiffusionFeedback = megadsp::parseControlValue(
            megadsp::ModuleType::diffusionDelay, 3, "35%");
        expect(parsedDiffusionFeedback.has_value());
        if (parsedDiffusionFeedback.has_value())
            expectWithinAbsoluteError(
                *parsedDiffusionFeedback, diffusionDefaults[3], 0.0001f);

        beginTest("Every registered module prepares and processes through one slot");
        juce::AudioProcessorGraph factoryOwner;
        juce::AudioProcessorValueTreeState factoryState(
            factoryOwner, nullptr, "factoryState",
            megadsp::createParameterLayout());
        megadsp::EffectRack factoryRack(factoryState);
        factoryRack.prepare(48000.0, 512, 2);
        auto* factoryType = factoryState.getParameter(
            megadsp::slotParameterId(0, "type"));
        for (int stableType = 1; stableType < megadsp::moduleTypeCount;
             ++stableType)
        {
            const auto type = static_cast<megadsp::ModuleType>(stableType);
            const auto defaults = megadsp::moduleDefaults(type);
            for (int control = 0; control < megadsp::controlsPerSlot; ++control)
                if (auto* parameter = factoryState.getParameter(
                       megadsp::controlParameterId(0, control)))
                   parameter->setValueNotifyingHost(parameter->convertTo0to1(
                       defaults[static_cast<size_t>(control)]));
            factoryType->setValueNotifyingHost(
                factoryType->convertTo0to1(static_cast<float>(type)));
            factoryRack.synchronizeModules();
            expect(factoryRack.activeModuleType(0) == type);
            expect(factoryRack.activeModuleInstance(0) != nullptr);
            for (int slot = 1; slot < megadsp::slotCount; ++slot)
                expect(factoryRack.activeModuleInstance(slot) == nullptr);

            juce::AudioBuffer<float> block(2, 512);
            for (int sample = 0; sample < block.getNumSamples(); ++sample)
            {
                const auto value = 0.1f * std::sin(
                   juce::MathConstants<float>::twoPi * 440.0f
                   * static_cast<float>(sample) / 48000.0f);
                block.setSample(0, sample, value);
                block.setSample(1, sample, value * 0.9f);
            }
            factoryRack.process(block, nullptr, 120.0);
            for (int channel = 0; channel < block.getNumChannels(); ++channel)
                for (int sample = 0; sample < block.getNumSamples(); ++sample)
                   expect(std::isfinite(block.getSample(channel, sample)));
        }
        factoryType->setValueNotifyingHost(
            factoryType->convertTo0to1(
                static_cast<float>(megadsp::ModuleType::empty)));
        factoryRack.synchronizeModules();
        expect(factoryRack.activeModuleType(0) == megadsp::ModuleType::empty);
        expect(factoryRack.activeModuleInstance(0) == nullptr);

        beginTest("Registry categories preserve the approved browser map");
        const auto expectCategory = [this](
            megadsp::ModuleType type, megadsp::ModuleCategory category,
            const char* name)
        {
            const auto& definition = megadsp::moduleDefinition(type);
            expect(definition.category == category);
            expectEquals(juce::String(megadsp::moduleCategoryName(category)),
                        juce::String(name));
        };
        expectCategory(megadsp::ModuleType::equalizer,
                      megadsp::ModuleCategory::eqAndFilters,
                      "EQ & Filters");
        expectCategory(megadsp::ModuleType::dynamicEqualizer,
                      megadsp::ModuleCategory::eqAndFilters,
                      "EQ & Filters");
        expectCategory(megadsp::ModuleType::resonanceTamer,
                      megadsp::ModuleCategory::eqAndFilters,
                      "EQ & Filters");
        expectCategory(megadsp::ModuleType::spectralBalance,
                      megadsp::ModuleCategory::eqAndFilters,
                      "EQ & Filters");
        expectCategory(megadsp::ModuleType::compressor,
                      megadsp::ModuleCategory::dynamics, "Dynamics");
        expectCategory(megadsp::ModuleType::limiter,
                      megadsp::ModuleCategory::dynamics, "Dynamics");
        expectCategory(megadsp::ModuleType::gateExpander,
                      megadsp::ModuleCategory::dynamics, "Dynamics");
        expectCategory(megadsp::ModuleType::transientDesigner,
                      megadsp::ModuleCategory::dynamics, "Dynamics");
        expectCategory(megadsp::ModuleType::multibandCompressor,
                      megadsp::ModuleCategory::dynamics, "Dynamics");
        expectCategory(megadsp::ModuleType::loudnessRider,
                      megadsp::ModuleCategory::dynamics, "Dynamics");
        expectCategory(megadsp::ModuleType::adaptiveClipper,
                      megadsp::ModuleCategory::dynamics, "Dynamics");
        expectCategory(megadsp::ModuleType::saturator,
                      megadsp::ModuleCategory::saturationAndColor,
                      "Saturation & Color");
        expectCategory(megadsp::ModuleType::signalDecay,
                      megadsp::ModuleCategory::saturationAndColor,
                      "Saturation & Color");
        expectCategory(megadsp::ModuleType::analogTape,
                      megadsp::ModuleCategory::saturationAndColor,
                      "Saturation & Color");
        expectCategory(megadsp::ModuleType::harmonicMirage,
                      megadsp::ModuleCategory::saturationAndColor,
                      "Saturation & Color");
        expectCategory(megadsp::ModuleType::delay,
                      megadsp::ModuleCategory::delayAndEcho,
                      "Delay & Echo");
        expectCategory(megadsp::ModuleType::diffusionDelay,
                      megadsp::ModuleCategory::delayAndEcho,
                      "Delay & Echo");
        expectCategory(megadsp::ModuleType::spectralDelayCanvas,
                      megadsp::ModuleCategory::delayAndEcho,
                      "Delay & Echo");
        expectCategory(megadsp::ModuleType::algorithmicReverb,
                      megadsp::ModuleCategory::reverbAndSpace,
                      "Reverb & Space");
        expectCategory(megadsp::ModuleType::convolutionReverb,
                      megadsp::ModuleCategory::reverbAndSpace,
                      "Reverb & Space");
        expectCategory(megadsp::ModuleType::resonantMatrix,
                      megadsp::ModuleCategory::reverbAndSpace,
                      "Reverb & Space");
        expectCategory(megadsp::ModuleType::pitchBloom,
                      megadsp::ModuleCategory::reverbAndSpace,
                      "Reverb & Space");
        for (const auto type : {
                megadsp::ModuleType::tremolo,
                megadsp::ModuleType::rotarySpeaker,
                megadsp::ModuleType::vintageChorus,
                megadsp::ModuleType::studioPhaser,
                megadsp::ModuleType::studioFlanger,
                megadsp::ModuleType::chaosField })
            expectCategory(type, megadsp::ModuleCategory::modulation,
                          "Modulation");
        for (const auto type : {
                megadsp::ModuleType::stereoWidth,
                megadsp::ModuleType::midSideDecoder,
                megadsp::ModuleType::spatialOrbit,
                megadsp::ModuleType::phaseCoherence })
            expectCategory(type, megadsp::ModuleCategory::stereoAndUtility,
                          "Stereo & Utility");
        for (const auto type : {
                megadsp::ModuleType::randomGranulizer,
                megadsp::ModuleType::beatPermuter,
                megadsp::ModuleType::spectralPrism,
                megadsp::ModuleType::wavefoldGarden,
                megadsp::ModuleType::frequencyLab,
                megadsp::ModuleType::formantForge,
                megadsp::ModuleType::timeMosaic })
            expectCategory(type, megadsp::ModuleCategory::glitchAndCreative,
                          "Glitch & Creative");
        expect(megadsp::moduleDefinition(megadsp::ModuleType::empty).category
               == megadsp::ModuleCategory::none);
        expect(juce::String(megadsp::moduleCategoryName(
                  megadsp::ModuleCategory::none)).isEmpty());

        beginTest("Schema two migration activates auto gain only for matching modules");
        auto setContractValue = [&contractState](const juce::String& id,
                                                 float value)
        {
            auto* parameter = contractState.getParameter(id);
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        };
        setContractValue(megadsp::slotParameterId(0, "type"),
                         static_cast<float>(megadsp::ModuleType::compressor));
        setContractValue(megadsp::slotParameterId(1, "type"),
                         static_cast<float>(megadsp::ModuleType::limiter));
        setContractValue(megadsp::slotParameterId(2, "type"),
                         static_cast<float>(megadsp::ModuleType::delay));
        setContractValue(megadsp::controlParameterId(0, 8), 0.7f);
        setContractValue(megadsp::controlParameterId(1, 4), 0.7f);
        setContractValue(megadsp::controlParameterId(2, 4), 0.7f);
        setContractValue(megadsp::controlParameterId(2, 8), 0.6f);
        auto legacyState = contractState.copyState();
        legacyState.setProperty("schemaVersion", 2, nullptr);
        legacyState.setProperty("advanced0", true, nullptr);
        auto migrated = megadsp::migrateStateToCurrentSchema(legacyState);
        auto migratedValue = [&migrated](const juce::String& id)
        {
            for (const auto child : migrated)
                if (child.getProperty("id").toString() == id)
                    return static_cast<float>(
                        static_cast<double>(child.getProperty("value")));
            return -1.0f;
        };
        expectEquals(static_cast<int>(
                         migrated.getProperty("schemaVersion", 0)),
                     megadsp::stateSchemaVersion);
        expect(!migrated.hasProperty("advanced0"));
        expectEquals(migratedValue(
                         megadsp::controlParameterId(0, 8)), 0.0f);
        expectEquals(migratedValue(
                         megadsp::controlParameterId(1, 4)), 0.0f);
        expectWithinAbsoluteError(migratedValue(
                                      megadsp::controlParameterId(2, 4)),
                                  0.7f, 0.0001f);
        expectWithinAbsoluteError(migratedValue(
                                      megadsp::controlParameterId(2, 8)),
                                  0.6f, 0.0001f);

        beginTest("Schema three EQ edge peaks remain peaks after migration");
        setContractValue(megadsp::slotParameterId(3, "type"),
                         static_cast<float>(megadsp::ModuleType::equalizer));
        setContractValue(megadsp::controlParameterId(3, 0), 0.02f);
        setContractValue(megadsp::controlParameterId(3, 6), 0.98f);
        auto schemaThreeState = contractState.copyState();
        schemaThreeState.setProperty("schemaVersion", 3, nullptr);
        auto migratedEq =
            megadsp::migrateStateToCurrentSchema(schemaThreeState);
        auto migratedEqValue = [&migratedEq](const juce::String& id)
        {
            for (const auto child : migratedEq)
                if (child.getProperty("id").toString() == id)
                    return static_cast<float>(
                        static_cast<double>(child.getProperty("value")));
            return -1.0f;
        };
        expectWithinAbsoluteError(
            migratedEqValue(megadsp::controlParameterId(3, 0)),
            0.02f, 0.0001f);
        expectWithinAbsoluteError(
            migratedEqValue(megadsp::controlParameterId(3, 6)),
            0.98f, 0.0001f);
        expectEquals(migratedEqValue(
                         megadsp::controlParameterId(3, 10)), 0.0f);
        expectEquals(migratedEqValue(
                         megadsp::controlParameterId(3, 11)), 0.0f);

        beginTest("Schema four EQ edge rolloffs migrate to explicit modes");
        auto schemaFourState = contractState.copyState();
        schemaFourState.setProperty("schemaVersion", 4, nullptr);
        auto migratedSchemaFour =
            megadsp::migrateStateToCurrentSchema(schemaFourState);
        auto schemaFourValue = [&migratedSchemaFour](const juce::String& id)
        {
            for (const auto child : migratedSchemaFour)
                if (child.getProperty("id").toString() == id)
                    return static_cast<float>(
                        static_cast<double>(child.getProperty("value")));
            return -1.0f;
        };
        expectEquals(schemaFourValue(
                         megadsp::controlParameterId(3, 10)), 1.0f);
        expectEquals(schemaFourValue(
                         megadsp::controlParameterId(3, 11)), 1.0f);

        beginTest("Schema five migrates only Granulizer fixed grain size");
        const auto oldSize = std::log(120.0f / 15.0f)
                             / std::log(500.0f / 15.0f);
        auto schemaFiveState = contractState.copyState();
        schemaFiveState.setProperty("schemaVersion", 5, nullptr);
        auto setSchemaFiveValue = [&schemaFiveState](
                                      const juce::String& id, float newValue)
        {
            for (auto child : schemaFiveState)
                if (child.getProperty("id").toString() == id)
                    child.setProperty("value", newValue, nullptr);
        };
        setSchemaFiveValue(
            megadsp::slotParameterId(4, "type"),
            static_cast<float>(megadsp::ModuleType::randomGranulizer));
        setSchemaFiveValue(megadsp::controlParameterId(4, 1), oldSize);
        setSchemaFiveValue(megadsp::controlParameterId(4, 4), 0.93f);
        setSchemaFiveValue(
            megadsp::slotParameterId(5, "type"),
            static_cast<float>(megadsp::ModuleType::delay));
        setSchemaFiveValue(megadsp::controlParameterId(5, 1), oldSize);
        setSchemaFiveValue(megadsp::controlParameterId(5, 4), 0.73f);
        setSchemaFiveValue(
            megadsp::slotParameterId(6, "type"),
            static_cast<float>(megadsp::ModuleType::algorithmicReverb));
        setSchemaFiveValue(megadsp::controlParameterId(6, 2), 0.23f);
        setSchemaFiveValue(megadsp::controlParameterId(6, 3), 0.5f);
        auto migratedSchemaFive =
            megadsp::migrateStateToCurrentSchema(schemaFiveState);
        auto schemaFiveValue = [&migratedSchemaFive](const juce::String& id)
        {
            for (const auto child : migratedSchemaFive)
                if (child.getProperty("id").toString() == id)
                    return static_cast<float>(
                        static_cast<double>(child.getProperty("value")));
            return -1.0f;
        };
        const auto expectedSize = std::log(120.0f / 50.0f)
                                  / std::log(2000.0f / 50.0f);
        expectWithinAbsoluteError(schemaFiveValue(
                                      megadsp::controlParameterId(4, 1)),
                                  expectedSize, 0.0001f);
        expectWithinAbsoluteError(schemaFiveValue(
                                      megadsp::controlParameterId(4, 4)),
                                  expectedSize, 0.0001f);
        expectWithinAbsoluteError(schemaFiveValue(
                                      megadsp::controlParameterId(5, 1)),
                                  oldSize, 0.0001f);
        expectWithinAbsoluteError(schemaFiveValue(
                                      megadsp::controlParameterId(5, 4)),
                                  0.73f, 0.0001f);
        expectWithinAbsoluteError(schemaFiveValue(
                                     megadsp::controlParameterId(6, 2)),
                                  std::sqrt(0.5f), 0.000001f);
        expectWithinAbsoluteError(schemaFiveValue(
                                     megadsp::controlParameterId(6, 3)),
                                  std::sqrt(0.5f), 0.000001f);

        beginTest("Schema six migrates only reverb constant-power mixes");
        for (const auto legacyMix : { 0.0f, 0.5f, 1.0f })
        {
            auto schemaSixState = contractState.copyState();
            schemaSixState.setProperty("schemaVersion", 6, nullptr);
            auto setSchemaSixValue = [&schemaSixState](
                                        const juce::String& id, float newValue)
            {
                for (auto child : schemaSixState)
                    if (child.getProperty("id").toString() == id)
                        child.setProperty("value", newValue, nullptr);
            };
            setSchemaSixValue(
                megadsp::slotParameterId(6, "type"),
                static_cast<float>(megadsp::ModuleType::algorithmicReverb));
            setSchemaSixValue(megadsp::controlParameterId(6, 2), 0.23f);
            setSchemaSixValue(megadsp::controlParameterId(6, 3), legacyMix);
            setSchemaSixValue(
                megadsp::slotParameterId(7, "type"),
                static_cast<float>(megadsp::ModuleType::convolutionReverb));
            setSchemaSixValue(megadsp::controlParameterId(7, 2), legacyMix);
            setSchemaSixValue(megadsp::controlParameterId(7, 3), 0.61f);
            setSchemaSixValue(megadsp::controlParameterId(7, 4), 0.42f);
            setSchemaSixValue(
                megadsp::slotParameterId(5, "type"),
                static_cast<float>(megadsp::ModuleType::delay));
            setSchemaSixValue(megadsp::controlParameterId(5, 2), legacyMix);
            setSchemaSixValue(megadsp::controlParameterId(5, 4), 0.73f);

            auto migratedSchemaSix =
                megadsp::migrateStateToCurrentSchema(schemaSixState);
            auto schemaSixValue = [&migratedSchemaSix](
                                     const juce::String& id)
            {
                for (const auto child : migratedSchemaSix)
                    if (child.getProperty("id").toString() == id)
                        return static_cast<float>(
                            static_cast<double>(child.getProperty("value")));
                return -1.0f;
            };
            const auto angle =
                legacyMix * juce::MathConstants<float>::halfPi;
            expectWithinAbsoluteError(
                schemaSixValue(megadsp::controlParameterId(6, 2)),
                std::cos(angle), 0.000001f);
            expectWithinAbsoluteError(
                schemaSixValue(megadsp::controlParameterId(6, 3)),
                std::sin(angle), 0.000001f);
            expectWithinAbsoluteError(
                schemaSixValue(megadsp::controlParameterId(7, 2)),
                std::sin(angle), 0.000001f);
            expectWithinAbsoluteError(
                schemaSixValue(megadsp::controlParameterId(7, 3)),
                0.61f, 0.000001f);
            expectWithinAbsoluteError(
                schemaSixValue(megadsp::controlParameterId(7, 4)),
                std::cos(angle), 0.000001f);
            expectWithinAbsoluteError(
                schemaSixValue(megadsp::controlParameterId(5, 2)),
                legacyMix, 0.000001f);
            expectWithinAbsoluteError(
                schemaSixValue(megadsp::controlParameterId(5, 4)),
                0.73f, 0.000001f);
        }

        beginTest("Compressor auto makeup restores slow average reduction");
        megadsp::CompressorModule autoCompressor;
        autoCompressor.prepare({ 48000.0, 512, 2 });
        megadsp::ControlValues compressorControls {};
        compressorControls.fill(0.5f);
        compressorControls[0] = 0.5f;
        compressorControls[1] = 0.75f;
        compressorControls[2] = 0.15f;
        compressorControls[4] = 0.0f;
        compressorControls[5] = 0.0f;
        compressorControls[6] = 1.0f;
        compressorControls[8] = 0.0f;
        juce::AudioBuffer<float> compressedOff(2, 240000);
        for (int channel = 0; channel < 2; ++channel)
            compressedOff.clear(channel, 0, compressedOff.getNumSamples());
        compressedOff.applyGain(0.0f);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < compressedOff.getNumSamples(); ++sample)
                compressedOff.setSample(channel, sample, 0.5f);
        autoCompressor.process(compressedOff, compressorControls, {});
        const auto uncompensated = compressedOff.getSample(
            0, compressedOff.getNumSamples() - 1);
        autoCompressor.reset();
        compressorControls[8] = 1.0f;
        juce::AudioBuffer<float> compressedOn(2, 240000);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < compressedOn.getNumSamples(); ++sample)
                compressedOn.setSample(channel, sample, 0.5f);
        autoCompressor.process(compressedOn, compressorControls, {});
        const auto compensated = compressedOn.getSample(
            0, compressedOn.getNumSamples() - 1);
        expect(compensated > uncompensated * 1.5f);
        expect(std::isfinite(compensated));

        beginTest("Compressor dry mix remains unchanged with auto makeup");
        autoCompressor.reset();
        compressorControls[6] = 0.0f;
        juce::AudioBuffer<float> dryCompression(2, 512);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < dryCompression.getNumSamples(); ++sample)
                dryCompression.setSample(channel, sample, 0.25f);
        autoCompressor.process(dryCompression, compressorControls, {});
        expectWithinAbsoluteError(
            dryCompression.getSample(0, 511), 0.25f, 0.000001f);

        beginTest("Limiter auto gain removes static threshold drive");
        megadsp::LimiterModule autoLimiter;
        autoLimiter.prepare({ 48000.0, 512, 2 });
        megadsp::ControlValues limiterControls {};
        limiterControls.fill(0.5f);
        limiterControls[0] = 0.5f;
        limiterControls[1] = 11.0f / 12.0f;
        limiterControls[3] = 0.0f;
        limiterControls[4] = 0.0f;
        juce::AudioBuffer<float> limitedOff(2, 48000);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < limitedOff.getNumSamples(); ++sample)
                limitedOff.setSample(channel, sample, 0.01f);
        autoLimiter.process(limitedOff, limiterControls, {});
        const auto driven = limitedOff.getSample(
            0, limitedOff.getNumSamples() - 1);
        autoLimiter.reset();
        limiterControls[4] = 1.0f;
        juce::AudioBuffer<float> limitedOn(2, 48000);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < limitedOn.getNumSamples(); ++sample)
                limitedOn.setSample(channel, sample, 0.01f);
        autoLimiter.process(limitedOn, limiterControls, {});
        const auto matched = limitedOn.getSample(
            0, limitedOn.getNumSamples() - 1);
        expect(driven > matched * 2.0f);
        expectWithinAbsoluteError(matched, 0.01f, 0.0002f);

        beginTest("Limiter auto gain always respects ceiling");
        autoLimiter.reset();
        limiterControls[0] = 1.0f;
        limiterControls[1] = 0.0f;
        limiterControls[4] = 1.0f;
        juce::AudioBuffer<float> ceilingSafety(2, 48000);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < ceilingSafety.getNumSamples(); ++sample)
                ceilingSafety.setSample(channel, sample, 2.0f);
        autoLimiter.process(ceilingSafety, limiterControls, {});
        expect(ceilingSafety.getMagnitude(
                   0, 0, ceilingSafety.getNumSamples())
               <= juce::Decibels::decibelsToGain(-12.0f) + 0.0001f);
    }
};

RackIntegrationTests rackIntegrationTests;

class VintageChorusTests final : public juce::UnitTest
{
public:
    VintageChorusTests()
        : juce::UnitTest("Vintage Chorus", "megaDSP")
    {
    }

    void runTest() override
    {
        const auto defaults =
            megadsp::moduleDefaults(megadsp::ModuleType::vintageChorus);

        beginTest("Control contract, defaults, and parsing");
        expectEquals(static_cast<int>(megadsp::descriptorFor(
                         megadsp::ModuleType::vintageChorus).type),
                     static_cast<int>(megadsp::ModuleType::vintageChorus));
        expectEquals(megadsp::controlOptions(
                         megadsp::ModuleType::vintageChorus, 0).size(), 4);
        expectEquals(megadsp::formatControlValue(
                         megadsp::ModuleType::vintageChorus, 0, defaults[0]),
                     juce::String("Vintage BBD"));
        expectEquals(megadsp::formatControlValue(
                         megadsp::ModuleType::vintageChorus, 1, defaults[1]),
                     juce::String("0.80 Hz"));
        expectEquals(megadsp::formatControlValue(
                         megadsp::ModuleType::vintageChorus, 2, defaults[2]),
                     juce::String("45%"));
        expectEquals(megadsp::formatControlValue(
                         megadsp::ModuleType::vintageChorus, 3, defaults[3]),
                     juce::String("9.0 ms"));
        expectEquals(megadsp::formatControlValue(
                         megadsp::ModuleType::vintageChorus, 4, defaults[4]),
                     juce::String("2 voices"));
        expectEquals(megadsp::formatControlValue(
                         megadsp::ModuleType::vintageChorus, 5, defaults[5]),
                     juce::String("100%"));
        expectEquals(megadsp::formatControlValue(
                         megadsp::ModuleType::vintageChorus, 6, defaults[6]),
                     juce::String("+8%"));
        expectEquals(megadsp::formatControlValue(
                         megadsp::ModuleType::vintageChorus, 7, defaults[7]),
                     juce::String("10.0 kHz"));
        expectEquals(megadsp::formatControlValue(
                         megadsp::ModuleType::vintageChorus, 8, defaults[8]),
                     juce::String("25%"));
        expectEquals(megadsp::formatControlValue(
                         megadsp::ModuleType::vintageChorus, 9, defaults[9]),
                     juce::String("90") + juce::String::charToString(0x00b0));
        expectEquals(megadsp::formatControlValue(
                         megadsp::ModuleType::vintageChorus, 10, defaults[10]),
                     juce::String("40%"));
        expectEquals(megadsp::formatControlValue(
                         megadsp::ModuleType::vintageChorus, 11, defaults[11]),
                     juce::String("0.0 dB"));
        const auto parsedModel = megadsp::parseControlValue(
            megadsp::ModuleType::vintageChorus, 0, "Tri-Chorus");
        expect(parsedModel.has_value());
        if (parsedModel.has_value())
            expectEquals(megadsp::discreteIndex(*parsedModel, 4), 2);

        beginTest("Age is silent-input safe in every model");
        for (int model = 0; model < 4; ++model)
        {
            auto controls = defaults;
            controls[0] = megadsp::discreteValue(model, 4);
            controls[8] = 1.0f;
            controls[10] = 1.0f;
            megadsp::VintageChorusModule chorus;
            chorus.prepare({ 48000.0, 512, 2 });
            juce::AudioBuffer<float> silence(2, 48000);
            silence.clear();
            chorus.process(silence, controls, {});
            expect(silence.getMagnitude(0, silence.getNumSamples())
                       < 1.0e-7f,
                   "Model " + juce::String(model) + " emitted silence noise");
            expectFinite(silence);
        }

        beginTest("Zero mix is exact dry apart from output");
        auto dryControls = defaults;
        dryControls[8] = 1.0f;
        dryControls[10] = 0.0f;
        megadsp::VintageChorusModule dryChorus;
        dryChorus.prepare({ 48000.0, 512, 2 });
        juce::AudioBuffer<float> exactDry(2, 8192);
        juce::AudioBuffer<float> reference(2, 8192);
        fillSignal(exactDry);
        reference.makeCopyOf(exactDry);
        dryChorus.process(exactDry, dryControls, {});
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < exactDry.getNumSamples(); ++sample)
                expectEquals(exactDry.getSample(channel, sample),
                             reference.getSample(channel, sample));

        beginTest("All models modulate audibly and remain distinct");
        std::array<juce::AudioBuffer<float>, 4> models;
        for (int model = 0; model < 4; ++model)
        {
            auto controls = defaults;
            controls[0] = megadsp::discreteValue(model, 4);
            controls[2] = 0.78f;
            controls[4] = 1.0f;
            controls[10] = 1.0f;
            models[static_cast<size_t>(model)] =
                render(controls, 2, 96000);
            expect(rms(models[static_cast<size_t>(model)], 4096) > 0.015f);
            expectFinite(models[static_cast<size_t>(model)]);
        }
        for (int first = 0; first < 4; ++first)
            for (int second = first + 1; second < 4; ++second)
                expect(difference(models[static_cast<size_t>(first)],
                                  models[static_cast<size_t>(second)], 4096)
                           > 0.002f,
                       "Models " + juce::String(first) + " and "
                           + juce::String(second) + " were too similar");

        beginTest("Dimension has compatible mono fold-down");
        auto dimensionControls = defaults;
        dimensionControls[0] = megadsp::discreteValue(1, 4);
        dimensionControls[2] = 1.0f;
        dimensionControls[4] = 1.0f;
        dimensionControls[5] = 1.0f;
        dimensionControls[10] = 1.0f;
        auto dimension = render(dimensionControls, 2, 96000);
        double foldEnergy = 0.0;
        double channelEnergy = 0.0;
        for (int sample = 4096; sample < dimension.getNumSamples(); ++sample)
        {
            const auto left = dimension.getSample(0, sample);
            const auto right = dimension.getSample(1, sample);
            const auto fold = 0.5f * (left + right);
            foldEnergy += fold * fold;
            channelEnergy += 0.5 * (left * left + right * right);
        }
        expect(foldEnergy > channelEnergy * 0.22);

        beginTest("Model and voice automation has bounded transitions");
        megadsp::VintageChorusModule automated;
        automated.prepare({ 48000.0, 256, 2 });
        auto automatedControls = defaults;
        automatedControls[10] = 0.75f;
        float previous = 0.0f;
        float maximumJump = 0.0f;
        for (int block = 0; block < 120; ++block)
        {
            automatedControls[0] =
                megadsp::discreteValue(block % 4, 4);
            automatedControls[4] = (block % 2 == 0) ? 0.0f : 1.0f;
            juce::AudioBuffer<float> audio(2, 256);
            fillSignal(audio, block * 256);
            automated.process(audio, automatedControls, {});
            for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            {
                const auto current = audio.getSample(0, sample);
                maximumJump = juce::jmax(
                    maximumJump, std::abs(current - previous));
                previous = current;
            }
            expectFinite(audio);
        }
        expect(maximumJump < 0.35f);

        beginTest("Feedback extremes stay bounded through input and tail");
        for (const auto feedback : { 0.0f, 1.0f })
        {
            auto controls = defaults;
            controls[6] = feedback;
            controls[10] = 1.0f;
            megadsp::VintageChorusModule chorus;
            chorus.prepare({ 48000.0, 512, 2 });
            juce::AudioBuffer<float> sustained(2, 96000);
            fillSignal(sustained);
            chorus.process(sustained, controls, {});
            expectFinite(sustained);
            expect(sustained.getMagnitude(0, sustained.getNumSamples()) < 2.5f);
            juce::AudioBuffer<float> tail(2, 96000);
            tail.clear();
            chorus.process(tail, controls, {});
            expectFinite(tail);
            expect(tail.getMagnitude(0, tail.getNumSamples()) < 2.5f);
        }

        beginTest("Tone Age Width Depth and Rate have measurable effects");
        static constexpr std::array<int, 5> effectControls { 7, 8, 5, 2, 1 };
        for (const auto control : effectControls)
        {
            auto low = defaults;
            auto high = defaults;
            low[static_cast<size_t>(control)] = 0.0f;
            high[static_cast<size_t>(control)] = 1.0f;
            low[10] = high[10] = 0.82f;
            const auto lowOutput = render(low, 2, 96000);
            const auto highOutput = render(high, 2, 96000);
            expect(difference(lowOutput, highOutput, 4096) > 0.0002f,
                   juce::String(megadsp::controlMetadata(
                                    megadsp::ModuleType::vintageChorus,
                                    control).label)
                       + " had no measurable effect");
        }

        beginTest("Mono fallback is finite and audible");
        auto monoControls = defaults;
        monoControls[10] = 1.0f;
        const auto mono = render(monoControls, 1, 48000);
        expectFinite(mono);
        expect(rms(mono, 4096) > 0.02f);

        beginTest("Tail reporting is bounded and follows feedback");
        megadsp::VintageChorusModule tailChorus;
        auto noFeedback = defaults;
        auto highFeedback = defaults;
        noFeedback[6] = 0.5f;
        highFeedback[6] = 1.0f;
        const auto shortTail = tailChorus.tailSeconds(noFeedback);
        const auto longTail = tailChorus.tailSeconds(highFeedback);
        expect(shortTail >= 0.04 && shortTail <= 4.0);
        expect(longTail > shortTail);
        expect(longTail <= 4.0);
    }

private:
    static void fillSignal(juce::AudioBuffer<float>& buffer,
                           int sampleOffset = 0)
    {
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto time = static_cast<float>(sample + sampleOffset)
                              / 48000.0f;
            const auto value = 0.18f * std::sin(
                juce::MathConstants<float>::twoPi * 311.0f * time)
                + 0.07f * std::sin(
                    juce::MathConstants<float>::twoPi * 997.0f * time);
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                buffer.setSample(channel, sample,
                                 channel == 0 ? value : value * 0.93f);
        }
    }

    static juce::AudioBuffer<float> render(
        const megadsp::ControlValues& controls, int channels, int samples)
    {
        megadsp::VintageChorusModule chorus;
        chorus.prepare({ 48000.0, 512, static_cast<juce::uint32>(channels) });
        juce::AudioBuffer<float> output(channels, samples);
        fillSignal(output);
        chorus.process(output, controls, {});
        return output;
    }

    static float rms(const juce::AudioBuffer<float>& buffer, int start)
    {
        double energy = 0.0;
        int count = 0;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = start; sample < buffer.getNumSamples(); ++sample)
            {
                const auto value = buffer.getSample(channel, sample);
                energy += value * value;
                ++count;
            }
        return static_cast<float>(std::sqrt(energy / juce::jmax(1, count)));
    }

    static float difference(const juce::AudioBuffer<float>& first,
                            const juce::AudioBuffer<float>& second,
                            int start)
    {
        double energy = 0.0;
        int count = 0;
        for (int channel = 0; channel < first.getNumChannels(); ++channel)
            for (int sample = start; sample < first.getNumSamples(); ++sample)
            {
                const auto delta = first.getSample(channel, sample)
                                   - second.getSample(channel, sample);
                energy += delta * delta;
                ++count;
            }
        return static_cast<float>(std::sqrt(energy / juce::jmax(1, count)));
    }

    void expectFinite(const juce::AudioBuffer<float>& buffer)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                if (!std::isfinite(buffer.getSample(channel, sample)))
                {
                    expect(false, "Non-finite sample");
                    return;
                }
    }
};

VintageChorusTests vintageChorusTests;
} // namespace

int main(int argc, char** argv)
{
    class TestLogger final : public juce::Logger
    {
        void logMessage(const juce::String& message) override
        {
            std::fprintf(stderr, "%s\n", message.toRawUTF8());
            std::fflush(stderr);
        }
    };

    TestLogger logger;
    juce::Logger::setCurrentLogger(&logger);
    juce::UnitTestRunner runner;
    if (argc > 1)
        runner.runTestsWithName(argv[1]);
    else
        runner.runAllTests();
    auto exitCode = runner.getNumResults() == 0 ? 1 : 0;
    for (int index = 0; index < runner.getNumResults(); ++index)
        if (runner.getResult(index)->failures != 0)
            exitCode = 1;
    juce::Logger::setCurrentLogger(nullptr);
    return exitCode;
}
