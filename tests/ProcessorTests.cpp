#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ui/EffectGraph.h"
#include "ui/GuiLayout.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace
{
class ParameterEventRecorder final
    : public juce::AudioProcessorParameter::Listener
{
public:
    explicit ParameterEventRecorder(int parameterCount)
        : values(static_cast<size_t>(parameterCount)),
          gestures(static_cast<size_t>(parameterCount))
    {
    }

    ~ParameterEventRecorder() override
    {
        for (auto* parameter : watched)
            parameter->removeListener(this);
    }

    void watch(juce::RangedAudioParameter& parameter)
    {
        watched.push_back(&parameter);
        parameter.addListener(this);
    }

    void clear()
    {
        for (auto& parameterValues : values)
            parameterValues.clear();
        for (auto& parameterGestures : gestures)
            parameterGestures.clear();
    }

    void parameterValueChanged(int parameterIndex, float newValue) override
    {
        values[static_cast<size_t>(parameterIndex)].push_back(newValue);
    }

    void parameterGestureChanged(
        int parameterIndex, bool gestureIsStarting) override
    {
        gestures[static_cast<size_t>(parameterIndex)]
            .push_back(gestureIsStarting);
    }

    std::vector<juce::RangedAudioParameter*> watched;
    std::vector<std::vector<float>> values;
    std::vector<std::vector<bool>> gestures;
};

class ProcessorChangeRecorder final
    : public juce::AudioProcessorListener
{
public:
    void audioProcessorParameterChanged(
        juce::AudioProcessor*, int, float) override
    {
    }

    void audioProcessorChanged(
        juce::AudioProcessor*,
        const ChangeDetails& details) override
    {
        if (details.nonParameterStateChanged)
            ++nonParameterChanges;
    }

    int nonParameterChanges = 0;
};

class ProcessorTopologyTests final : public juce::UnitTest
{
public:
    ProcessorTopologyTests()
        : juce::UnitTest("Processor topology", "megaDSP")
    {
    }

    void runTest() override
    {
        const auto setType = [](MegaDSPAudioProcessor& processor,
                                int slot, megadsp::ModuleType type)
        {
            auto* parameter = processor.parameters.getParameter(
                megadsp::slotParameterId(slot, "type"));
            parameter->setValueNotifyingHost(
                parameter->convertTo0to1(static_cast<float>(type)));
        };
        const auto setBypass = [](MegaDSPAudioProcessor& processor,
                                 int slot, bool bypassed)
        {
            auto* parameter = processor.parameters.getParameter(
                megadsp::slotParameterId(slot, "bypass"));
            parameter->setValueNotifyingHost(
                parameter->convertTo0to1(bypassed ? 1.0f : 0.0f));
        };

        beginTest(
            "External same-count type replacement synchronizes and publishes");
        MegaDSPAudioProcessor processor;
        processor.prepareToPlay(48000.0, 512);
        setType(processor, 0, megadsp::ModuleType::equalizer);
        processor.flushPendingUpdatesForTesting();
        expectEquals(processor.getRack().activeSlotCount(), 1);
        expect(processor.getRack().activeModuleType(0)
               == megadsp::ModuleType::equalizer);
        const auto firstGeneration = processor.getTopologyGeneration();

        setType(processor, 0, megadsp::ModuleType::compressor);
        expect(processor.getRack().activeModuleType(0)
               == megadsp::ModuleType::equalizer);
        processor.flushPendingUpdatesForTesting();
        expectEquals(processor.getRack().activeSlotCount(), 1);
        expect(processor.getRack().activeModuleType(0)
               == megadsp::ModuleType::compressor);
        expect(dynamic_cast<const megadsp::CompressorModule*>(
                   processor.getRack().activeModuleInstance(0))
               != nullptr);
        expect(processor.getTopologyGeneration() > firstGeneration);

        beginTest("Capture histories clear on topology, selection, and reset");
        std::array<float, 1> captured {};
        processor.getVisualizationData().slotData(0).input.push(0.75f);
        setType(processor, 0, megadsp::ModuleType::equalizer);
        processor.flushPendingUpdatesForTesting();
        expectEquals(static_cast<int>(
            processor.getVisualizationData().slotData(0)
                .input.copyLatest(captured)), 0);

        setType(processor, 1, megadsp::ModuleType::compressor);
        processor.flushPendingUpdatesForTesting();
        processor.setSelectedTab(0);
        processor.getVisualizationData().slotData(1).input.push(0.5f);
        processor.setSelectedTab(1);
        expectEquals(static_cast<int>(
            processor.getVisualizationData().slotData(1)
                .input.copyLatest(captured)), 0);

        processor.getVisualizationData().slotData(1).input.push(0.25f);
        processor.resetModuleToDefaults(1);
        expectEquals(static_cast<int>(
            processor.getVisualizationData().slotData(1)
                .input.copyLatest(captured)), 0);

        beginTest(
            "Sparse restored state compacts in signal-flow order");
        MegaDSPAudioProcessor restoredProcessor;
        restoredProcessor.prepareToPlay(48000.0, 512);
        auto restoredState = restoredProcessor.parameters.copyState();
        const auto setStateValue = [&restoredState](
                                       const juce::String& id,
                                       float value)
        {
            for (auto child : restoredState)
                if (child.getProperty("id").toString() == id)
                {
                    child.setProperty("value", value, nullptr);
                    return;
                }
        };
        for (int slot = 0; slot < megadsp::slotCount; ++slot)
            setStateValue(
                megadsp::slotParameterId(slot, "type"),
                static_cast<float>(megadsp::ModuleType::empty));
        setStateValue(
            megadsp::slotParameterId(2, "type"),
            static_cast<float>(megadsp::ModuleType::delay));
        setStateValue(
            megadsp::slotParameterId(5, "type"),
            static_cast<float>(megadsp::ModuleType::limiter));
        setStateValue(megadsp::controlParameterId(2, 0), 0.17f);
        setStateValue(megadsp::controlParameterId(5, 0), 0.83f);
        restoredState.setProperty(
            "schemaVersion", megadsp::stateSchemaVersion, nullptr);
        auto xml = restoredState.createXml();
        juce::MemoryBlock stateData;
        expect(xml != nullptr);
        if (xml != nullptr)
            juce::AudioProcessor::copyXmlToBinary(*xml, stateData);
        restoredProcessor.setStateInformation(
            stateData.getData(), static_cast<int>(stateData.getSize()));
        restoredProcessor.flushPendingUpdatesForTesting();

        expectEquals(restoredProcessor.getRack().activeSlotCount(), 2);
        expect(restoredProcessor.getRack().activeModuleType(0)
               == megadsp::ModuleType::delay);
        expect(restoredProcessor.getRack().activeModuleType(1)
               == megadsp::ModuleType::limiter);
        expect(restoredProcessor.getRack().activeModuleType(2)
               == megadsp::ModuleType::empty);
        expectWithinAbsoluteError(
            restoredProcessor.parameters
                .getRawParameterValue(
                    megadsp::controlParameterId(0, 0))
                ->load(),
            0.17f, 0.0001f);
        expectWithinAbsoluteError(
            restoredProcessor.parameters
                .getRawParameterValue(
                    megadsp::controlParameterId(1, 0))
                ->load(),
            0.83f, 0.0001f);

        beginTest("Deferred first-tab removal rejects refresh focus reentry");
        {
            auto editorProcessor =
                std::make_unique<MegaDSPAudioProcessor>();
            editorProcessor->prepareToPlay(48000.0, 512);
            editorProcessor->addModule(megadsp::ModuleType::guitarCabinet);
            editorProcessor->addModule(megadsp::ModuleType::compressor);
            editorProcessor->setSelectedTab(0);
            auto editor = std::make_unique<MegaDSPAudioProcessorEditor>(
                *editorProcessor, 1100, 720);
            expectEquals(editor->tabCountForTesting(), 2);
            editor->simulateFocusGainDuringRefreshForTesting(1);
            expectEquals(editorProcessor->getSelectedTab(), 0);
            editor->clickCloseButtonForTesting(0);
            expectEquals(editorProcessor->getRack().activeSlotCount(), 1);
            expectEquals(editor->tabCountForTesting(), 1);
            expect(editorProcessor->getRack().moduleType(0)
                   == megadsp::ModuleType::compressor);
        }

        beginTest("Rotary Speaker paints safely after long system uptime");
        {
            MegaDSPAudioProcessor rotaryProcessor;
            rotaryProcessor.prepareToPlay(48000.0, 512);
            setType(
                rotaryProcessor, 0, megadsp::ModuleType::rotarySpeaker);
            rotaryProcessor.flushPendingUpdatesForTesting();
            megadsp::ui::EffectGraph graph(
                rotaryProcessor, 0, megadsp::ModuleType::rotarySpeaker);
            graph.setBounds(0, 0, 900, 450);
            juce::Image image(
                juce::Image::ARGB, graph.getWidth(), graph.getHeight(), true);
            juce::Graphics graphics(image);
            graph.paint(graphics);
            expect(!image.isNull());
        }

        beginTest("Bypassed slots suspend DSP and visualization capture");
        {
            MegaDSPAudioProcessor suspendedProcessor;
            suspendedProcessor.prepareToPlay(48000.0, 512);
            setType(
                suspendedProcessor, 0, megadsp::ModuleType::equalizer);
            suspendedProcessor.flushPendingUpdatesForTesting();
            suspendedProcessor.getVisualizationData().setSelectedSlot(0);
            juce::MidiBuffer bypassMidi;
            juce::AudioBuffer<float> active(2, 512);
            for (int channel = 0; channel < active.getNumChannels(); ++channel)
                juce::FloatVectorOperations::fill(
                    active.getWritePointer(channel), 0.25f,
                    active.getNumSamples());
            suspendedProcessor.processBlock(active, bypassMidi);
            std::array<float, 1024> bypassCapture {};
            const auto capturedBefore = suspendedProcessor
                .getVisualizationData().slotData(0).input.copyLatest(
                    bypassCapture);
            expect(capturedBefore > 0);

            setBypass(suspendedProcessor, 0, true);
            juce::AudioBuffer<float> bypassed(2, 512);
            for (int block = 0; block < 3; ++block)
            {
                for (int channel = 0; channel < bypassed.getNumChannels();
                     ++channel)
                    juce::FloatVectorOperations::fill(
                        bypassed.getWritePointer(channel), 0.25f,
                        bypassed.getNumSamples());
                suspendedProcessor.processBlock(bypassed, bypassMidi);
            }
            expect(suspendedProcessor.getRack().slotDspSuspended(0));
            expectEquals(
                suspendedProcessor.getVisualizationData().slotData(0)
                    .input.copyLatest(bypassCapture),
                capturedBefore);
            expectWithinAbsoluteError(
                suspendedProcessor.getRack().slotMeter(0), 0.0f, 0.000001f);
            for (int channel = 0; channel < bypassed.getNumChannels();
                 ++channel)
                expectWithinAbsoluteError(
                    bypassed.getMagnitude(
                        channel, 0, bypassed.getNumSamples()),
                    0.25f, 0.000001f);

            setBypass(suspendedProcessor, 0, false);
            suspendedProcessor.processBlock(bypassed, bypassMidi);
            expect(!suspendedProcessor.getRack().slotDspSuspended(0));
        }

        beginTest("Creative latency does not contribute to host PDC");
        {
            MegaDSPAudioProcessor latencyPolicyProcessor;
            latencyPolicyProcessor.prepareToPlay(48000.0, 512);
            const std::array creativeTypes {
                megadsp::ModuleType::spectralPrism,
                megadsp::ModuleType::pitchBloom,
                megadsp::ModuleType::spectralDelayCanvas,
                megadsp::ModuleType::harmonicMirage,
                megadsp::ModuleType::timeMosaic,
                megadsp::ModuleType::spectralNebula
            };
            for (const auto type : creativeTypes)
            {
                setType(latencyPolicyProcessor, 0, type);
                latencyPolicyProcessor.flushPendingUpdatesForTesting();
                expectEquals(latencyPolicyProcessor.getRack().latencySamples(), 0);
            }

            setType(
                latencyPolicyProcessor, 1,
                megadsp::ModuleType::resonanceTamer);
            latencyPolicyProcessor.flushPendingUpdatesForTesting();
            expectEquals(
                latencyPolicyProcessor.getRack().latencySamples(),
                megadsp::FixedLatencyStft::reportedLatencySamples);
            setBypass(latencyPolicyProcessor, 1, true);
            expectEquals(
                latencyPolicyProcessor.getRack().latencySamples(),
                megadsp::FixedLatencyStft::reportedLatencySamples);
        }

        beginTest("Latency modules warm up silently before bypass re-entry");
        {
            MegaDSPAudioProcessor latencyProcessor;
            latencyProcessor.prepareToPlay(48000.0, 512);
            setType(latencyProcessor, 0, megadsp::ModuleType::limiter);
            latencyProcessor.flushPendingUpdatesForTesting();
            juce::MidiBuffer latencyMidi;
            setBypass(latencyProcessor, 0, true);
            juce::AudioBuffer<float> steady(2, 512);
            for (int block = 0; block < 4; ++block)
            {
                for (int channel = 0; channel < steady.getNumChannels();
                     ++channel)
                    juce::FloatVectorOperations::fill(
                        steady.getWritePointer(channel), 0.1f,
                        steady.getNumSamples());
                latencyProcessor.processBlock(steady, latencyMidi);
            }
            expect(latencyProcessor.getRack().slotDspSuspended(0));
            setBypass(latencyProcessor, 0, false);
            for (int channel = 0; channel < steady.getNumChannels();
                 ++channel)
                juce::FloatVectorOperations::fill(
                    steady.getWritePointer(channel), 0.1f,
                    steady.getNumSamples());
            latencyProcessor.processBlock(steady, latencyMidi);
            expect(!latencyProcessor.getRack().slotDspSuspended(0));
            for (int channel = 0; channel < steady.getNumChannels();
                 ++channel)
                for (int sample = 0; sample < steady.getNumSamples();
                     ++sample)
                    expect(std::abs(steady.getSample(channel, sample))
                           > 0.09f);
        }

        beginTest(
            "Host bypass delay clears across zero-latency re-entry");
        MegaDSPAudioProcessor bypassProcessor;
        bypassProcessor.prepareToPlay(48000.0, 512);
        juce::MidiBuffer midi;
        const auto exerciseTransition =
            [this, &bypassProcessor, &setType, &midi](
                bool processZeroBypassed)
        {
            setType(
                bypassProcessor, 0, megadsp::ModuleType::limiter);
            bypassProcessor.flushPendingUpdatesForTesting();
            expectEquals(bypassProcessor.getLatencySamples(), 480);

            juce::AudioBuffer<float> fill(2, 480);
            fill.clear();
            for (int channel = 0; channel < fill.getNumChannels();
                 ++channel)
                juce::FloatVectorOperations::fill(
                    fill.getWritePointer(channel), 0.75f, 480);
            bypassProcessor.processBlock(fill, midi);

            setType(
                bypassProcessor, 0, megadsp::ModuleType::empty);
            bypassProcessor.flushPendingUpdatesForTesting();
            expectEquals(bypassProcessor.getLatencySamples(), 0);
            juce::AudioBuffer<float> zeroLatency(2, 1);
            zeroLatency.clear();
            if (processZeroBypassed)
                bypassProcessor.processBlockBypassed(
                    zeroLatency, midi);
            else
                bypassProcessor.processBlock(zeroLatency, midi);

            setType(
                bypassProcessor, 0, megadsp::ModuleType::limiter);
            bypassProcessor.flushPendingUpdatesForTesting();
            expectEquals(bypassProcessor.getLatencySamples(), 480);
            juce::AudioBuffer<float> reentered(2, 1);
            reentered.clear();
            reentered.setSample(0, 0, 1.0f);
            reentered.setSample(1, 0, 1.0f);
            bypassProcessor.processBlockBypassed(reentered, midi);
            expectWithinAbsoluteError(
                reentered.getSample(0, 0), 0.0f, 0.000001f);
            expectWithinAbsoluteError(
                reentered.getSample(1, 0), 0.0f, 0.000001f);
        };
        exerciseTransition(false);
        exerciseTransition(true);

        beginTest("Instance identity is normalized, notified, and restored");
        MegaDSPAudioProcessor identityProcessor;
        ProcessorChangeRecorder identityChanges;
        identityProcessor.addListener(&identityChanges);
        expect(identityProcessor.getInstanceName().isEmpty());
        expectEquals(identityProcessor.getBackgroundThemeIndex(), 0);
        identityProcessor.setInstanceName("  Vocal\nLead\t ");
        identityProcessor.setBackgroundThemeIndex(15);
        expectEquals(
            identityProcessor.getInstanceName(), juce::String("Vocal Lead"));
        expectEquals(identityProcessor.getBackgroundThemeIndex(), 15);
        expectEquals(identityChanges.nonParameterChanges, 2);
        identityProcessor.loadFactoryPreset(1);
        expectEquals(
            identityProcessor.getInstanceName(), juce::String("Vocal Lead"));
        expectEquals(identityProcessor.getBackgroundThemeIndex(), 15);

        juce::MemoryBlock identityState;
        identityProcessor.getStateInformation(identityState);
        MegaDSPAudioProcessor identityRestored;
        identityRestored.setStateInformation(
            identityState.getData(),
            static_cast<int>(identityState.getSize()));
        expectEquals(
            identityRestored.getInstanceName(), juce::String("Vocal Lead"));
        expectEquals(identityRestored.getBackgroundThemeIndex(), 15);

        const auto presetFile =
            juce::File::getSpecialLocation(
                juce::File::tempDirectory)
                .getNonexistentChildFile(
                    "megaDSP-identity-test", ".megadsp", false);
        expect(identityProcessor.savePreset(presetFile).wasOk());
        MegaDSPAudioProcessor presetRestored;
        expect(presetRestored.loadPreset(presetFile).wasOk());
        expectEquals(
            presetRestored.getInstanceName(), juce::String("Vocal Lead"));
        expectEquals(presetRestored.getBackgroundThemeIndex(), 15);
        expect(presetFile.deleteFile());

        identityRestored.parameters.state.setProperty(
            "backgroundTheme", 999, nullptr);
        expectEquals(identityRestored.getBackgroundThemeIndex(), 15);
        identityRestored.setInstanceName(
            juce::String::repeatedString("x", 40));
        expectEquals(
            identityRestored.getInstanceName().length(),
            megadsp::ui::instanceNameMaximumLength);
        identityProcessor.removeListener(&identityChanges);

        beginTest(
            "Factory presets publish each parameter's final value exactly once");
        MegaDSPAudioProcessor presetProcessor;
        presetProcessor.prepareToPlay(48000.0, 512);
        ParameterEventRecorder recorder(
            presetProcessor.getParameters().size());
        for (int slot = 0; slot < megadsp::slotCount; ++slot)
        {
            recorder.watch(*presetProcessor.parameters.getParameter(
                megadsp::slotParameterId(slot, "type")));
            recorder.watch(*presetProcessor.parameters.getParameter(
                megadsp::slotParameterId(slot, "bypass")));
            for (int control = 0;
                 control < megadsp::controlsPerSlot; ++control)
                recorder.watch(*presetProcessor.parameters.getParameter(
                    megadsp::controlParameterId(slot, control)));
        }

        const auto verifyFinalEvents =
            [this, &recorder]()
        {
            auto valueEventCount = size_t {};
            for (const auto* parameter : recorder.watched)
            {
                const auto index =
                    static_cast<size_t>(parameter->getParameterIndex());
                const auto& parameterValues = recorder.values[index];
                const auto& parameterGestures = recorder.gestures[index];
                expectEquals(
                    static_cast<int>(parameterValues.size()), 1);
                if (parameterValues.size() == 1)
                    expectWithinAbsoluteError(
                        parameterValues.front(), parameter->getValue(),
                        0.000001f);
                expectEquals(
                    static_cast<int>(parameterGestures.size()), 2);
                if (parameterGestures.size() == 2)
                {
                    expect(parameterGestures[0]);
                    expect(!parameterGestures[1]);
                }
                valueEventCount += parameterValues.size();
            }
            expectEquals(
                static_cast<int>(valueEventCount),
                megadsp::slotCount
                    * (2 + megadsp::controlsPerSlot));
        };

        presetProcessor.loadFactoryPreset(1);
        verifyFinalEvents();
        expect(presetProcessor.getRack().activeModuleType(0)
               == megadsp::ModuleType::equalizer);
        expect(presetProcessor.getRack().activeModuleType(1)
               == megadsp::ModuleType::compressor);
        expect(presetProcessor.getRack().activeModuleType(2)
               == megadsp::ModuleType::limiter);
        expectWithinAbsoluteError(
            presetProcessor.parameters.getRawParameterValue(
                megadsp::controlParameterId(0, 1))->load(),
            0.46f, 0.000001f);

        recorder.clear();
        presetProcessor.loadFactoryPreset(1);
        verifyFinalEvents();

        recorder.clear();
        presetProcessor.loadFactoryPreset(0);
        verifyFinalEvents();
        expectEquals(presetProcessor.getRack().activeSlotCount(), 0);
        for (int slot = 0; slot < megadsp::slotCount; ++slot)
        {
            expect(!presetProcessor.isSlotBypassed(slot));
            for (int control = 0;
                 control < megadsp::controlsPerSlot; ++control)
                expectWithinAbsoluteError(
                    presetProcessor.parameters.getRawParameterValue(
                        megadsp::controlParameterId(slot, control))->load(),
                    0.5f, 0.000001f);
        }
    }
};

ProcessorTopologyTests processorTopologyTests;
} // namespace
