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

        beginTest("Snapshots store isolated live rack states");
        MegaDSPAudioProcessor snapshotProcessor;
        snapshotProcessor.prepareToPlay(48000.0, 512);
        snapshotProcessor.addModule(megadsp::ModuleType::saturator);
        auto* drive = snapshotProcessor.parameters.getParameter(
            megadsp::controlParameterId(0, 0));
        drive->setValueNotifyingHost(0.2f);
        snapshotProcessor.storeSnapshot(1, false);
        expect(snapshotProcessor.isSnapshotPopulated(1));
        expect(snapshotProcessor.isSnapshotReady(1));
        juce::AudioBuffer<float> snapshotBuffer(2, 512);
        snapshotBuffer.clear();
        juce::MidiBuffer snapshotMidi;
        snapshotProcessor.processBlock(snapshotBuffer, snapshotMidi);
        snapshotProcessor.flushPendingUpdatesForTesting();
        expectEquals(snapshotProcessor.getSelectedSnapshot(), 1);
        drive->setValueNotifyingHost(0.8f);
        snapshotProcessor.recallSnapshot(0);
        expectEquals(snapshotProcessor.getSelectedSnapshot(), 0);
        snapshotBuffer.clear();
        snapshotProcessor.processBlock(snapshotBuffer, snapshotMidi);
        snapshotProcessor.flushPendingUpdatesForTesting();
        expectEquals(snapshotProcessor.getSelectedSnapshot(), 0);
        expectWithinAbsoluteError(drive->getValue(), 0.2f, 0.000001f);
        snapshotProcessor.recallSnapshot(1);
        expectEquals(snapshotProcessor.getSelectedSnapshot(), 1);
        snapshotBuffer.clear();
        snapshotProcessor.processBlock(snapshotBuffer, snapshotMidi);
        snapshotProcessor.flushPendingUpdatesForTesting();
        expectWithinAbsoluteError(drive->getValue(), 0.8f, 0.000001f);

        beginTest("Recalling an unused snapshot opens a blank rack");
        MegaDSPAudioProcessor blankSnapshotProcessor;
        blankSnapshotProcessor.prepareToPlay(48000.0, 512);
        blankSnapshotProcessor.addModule(megadsp::ModuleType::compressor);
        blankSnapshotProcessor.recallSnapshot(4);
        expect(blankSnapshotProcessor.isSnapshotPopulated(4));
        expect(blankSnapshotProcessor.isSnapshotReady(4));
        expectEquals(blankSnapshotProcessor.getSelectedSnapshot(), 4);
        expectEquals(blankSnapshotProcessor.getRack().activeSlotCount(), 0);
        juce::AudioBuffer<float> blankSnapshotBuffer(2, 512);
        blankSnapshotBuffer.clear();
        juce::MidiBuffer blankSnapshotMidi;
        blankSnapshotProcessor.processBlock(
            blankSnapshotBuffer, blankSnapshotMidi);
        expectEquals(blankSnapshotProcessor.getSelectedSnapshot(), 4);

        beginTest("Snapshot copy preserves source and current selection");
        MegaDSPAudioProcessor copySnapshotProcessor;
        copySnapshotProcessor.prepareToPlay(48000.0, 512);
        copySnapshotProcessor.addModule(megadsp::ModuleType::saturator);
        auto* copyDrive = copySnapshotProcessor.parameters.getParameter(
            megadsp::controlParameterId(0, 0));
        copyDrive->setValueNotifyingHost(0.2f);
        copySnapshotProcessor.storeSnapshot(1, false);
        copySnapshotProcessor.processBlock(
            blankSnapshotBuffer, blankSnapshotMidi);
        copyDrive->setValueNotifyingHost(0.8f);
        copySnapshotProcessor.copySnapshot(0, 3);
        expect(copySnapshotProcessor.isSnapshotPopulated(3));
        expect(copySnapshotProcessor.isSnapshotReady(3));
        expectEquals(copySnapshotProcessor.getSelectedSnapshot(), 1);
        expectWithinAbsoluteError(
            copyDrive->getValue(), 0.8f, 0.000001f);
        copySnapshotProcessor.recallSnapshot(3);
        copySnapshotProcessor.processBlock(
            blankSnapshotBuffer, blankSnapshotMidi);
        copySnapshotProcessor.flushPendingUpdatesForTesting();
        expectWithinAbsoluteError(
            copyDrive->getValue(), 0.2f, 0.000001f);
        copySnapshotProcessor.recallSnapshot(0);
        copySnapshotProcessor.processBlock(
            blankSnapshotBuffer, blankSnapshotMidi);
        copySnapshotProcessor.flushPendingUpdatesForTesting();
        expectWithinAbsoluteError(
            copyDrive->getValue(), 0.2f, 0.000001f);

        beginTest("Live Safe keeps graph work constant across recall");
        MegaDSPAudioProcessor liveSafeProcessor;
        liveSafeProcessor.prepareToPlay(48000.0, 512);
        liveSafeProcessor.addModule(megadsp::ModuleType::compressor);
        juce::AudioBuffer<float> liveSafeBuffer(2, 512);
        juce::MidiBuffer liveSafeMidi;
        liveSafeProcessor.storeSnapshot(1, false);
        liveSafeBuffer.clear();
        liveSafeProcessor.processBlock(liveSafeBuffer, liveSafeMidi);
        liveSafeProcessor.storeSnapshot(2, false);
        liveSafeBuffer.clear();
        liveSafeProcessor.processBlock(liveSafeBuffer, liveSafeMidi);
        liveSafeProcessor.setLiveSafeSnapshotsEnabled(true);
        for (int channel = 0; channel < liveSafeBuffer.getNumChannels();
             ++channel)
            juce::FloatVectorOperations::fill(
                liveSafeBuffer.getWritePointer(channel), 0.1f,
                liveSafeBuffer.getNumSamples());
        liveSafeProcessor.processBlock(liveSafeBuffer, liveSafeMidi);
        expectEquals(
            liveSafeProcessor.processedSnapshotGraphCountForTesting(), 3);
        liveSafeProcessor.recallSnapshot(0);
        liveSafeProcessor.processBlock(liveSafeBuffer, liveSafeMidi);
        expectEquals(
            liveSafeProcessor.processedSnapshotGraphCountForTesting(), 3);
        expect(liveSafeProcessor.snapshotRecallDurationMicrosForTesting()
               >= 0);
        for (int channel = 0; channel < liveSafeBuffer.getNumChannels();
             ++channel)
            for (int sample = 0; sample < liveSafeBuffer.getNumSamples();
                 ++sample)
                expect(std::isfinite(
                    liveSafeBuffer.getSample(channel, sample)));

        beginTest("Live Safe processes mono layouts");
        MegaDSPAudioProcessor liveSafeMonoProcessor;
        juce::AudioProcessor::BusesLayout monoLayout;
        monoLayout.inputBuses.add(juce::AudioChannelSet::mono());
        monoLayout.inputBuses.add(juce::AudioChannelSet::disabled());
        monoLayout.outputBuses.add(juce::AudioChannelSet::mono());
        expect(liveSafeMonoProcessor.setBusesLayout(monoLayout));
        liveSafeMonoProcessor.prepareToPlay(44100.0, 127);
        liveSafeMonoProcessor.addModule(megadsp::ModuleType::compressor);
        liveSafeMonoProcessor.storeSnapshot(1, false);
        liveSafeMonoProcessor.setLiveSafeSnapshotsEnabled(true);
        juce::AudioBuffer<float> liveSafeMonoBuffer(1, 127);
        juce::MidiBuffer liveSafeMonoMidi;
        juce::FloatVectorOperations::fill(
            liveSafeMonoBuffer.getWritePointer(0), 0.1f,
            liveSafeMonoBuffer.getNumSamples());
        liveSafeMonoProcessor.processBlock(
            liveSafeMonoBuffer, liveSafeMonoMidi);
        expectEquals(
            liveSafeMonoProcessor
                .processedSnapshotGraphCountForTesting(),
            2);
        for (int sample = 0; sample < liveSafeMonoBuffer.getNumSamples();
             ++sample)
            expect(std::isfinite(
                liveSafeMonoBuffer.getSample(0, sample)));

        beginTest("Snapshot recall does not notify every host parameter");
        ParameterEventRecorder recallRecorder(
            liveSafeProcessor.getParameters().size());
        auto* recalledControl =
            liveSafeProcessor.parameters.getParameter(
                megadsp::controlParameterId(0, 0));
        recallRecorder.watch(*recalledControl);
        recallRecorder.clear();
        liveSafeProcessor.recallSnapshot(1);
        liveSafeBuffer.clear();
        liveSafeProcessor.processBlock(liveSafeBuffer, liveSafeMidi);
        liveSafeProcessor.flushPendingUpdatesForTesting();
        expect(recallRecorder.values[static_cast<size_t>(
                   recalledControl->getParameterIndex())].empty());

        beginTest("Live Safe handles eight snapshots and rapid MIDI recall");
        MegaDSPAudioProcessor liveSafeStressProcessor;
        liveSafeStressProcessor.prepareToPlay(48000.0, 64);
        liveSafeStressProcessor.addModule(
            megadsp::ModuleType::compressor);
        juce::AudioBuffer<float> liveSafeStressBuffer(2, 512);
        juce::MidiBuffer liveSafeStressMidi;
        for (int index = 1; index < megadsp::snapshotCount; ++index)
        {
            liveSafeStressProcessor.storeSnapshot(index, false);
            liveSafeStressBuffer.clear();
            liveSafeStressProcessor.processBlock(
                liveSafeStressBuffer, liveSafeStressMidi);
        }
        liveSafeStressProcessor.setLiveSafeSnapshotsEnabled(true);
        for (int sample = 0; sample < liveSafeStressBuffer.getNumSamples();
             sample += 47)
            liveSafeStressMidi.addEvent(
                juce::MidiMessage::programChange(
                    1, (sample / 47) % megadsp::snapshotCount),
                sample);
        for (int channel = 0;
             channel < liveSafeStressBuffer.getNumChannels(); ++channel)
            juce::FloatVectorOperations::fill(
                liveSafeStressBuffer.getWritePointer(channel), 0.1f,
                liveSafeStressBuffer.getNumSamples());
        liveSafeStressProcessor.processBlock(
            liveSafeStressBuffer, liveSafeStressMidi);
        expectEquals(
            liveSafeStressProcessor
                .processedSnapshotGraphCountForTesting(),
            megadsp::snapshotCount);
        for (int channel = 0;
             channel < liveSafeStressBuffer.getNumChannels(); ++channel)
            for (int sample = 0;
                 sample < liveSafeStressBuffer.getNumSamples(); ++sample)
                expect(std::isfinite(
                    liveSafeStressBuffer.getSample(channel, sample)));

        beginTest("Deferred topology rebuild stays with edited snapshot");
        MegaDSPAudioProcessor pinnedTopologyProcessor;
        pinnedTopologyProcessor.prepareToPlay(48000.0, 512);
        pinnedTopologyProcessor.addModule(
            megadsp::ModuleType::equalizer);
        pinnedTopologyProcessor.storeSnapshot(1, false);
        juce::AudioBuffer<float> pinnedTopologyBuffer(2, 512);
        juce::MidiBuffer pinnedTopologyMidi;
        pinnedTopologyBuffer.clear();
        pinnedTopologyProcessor.processBlock(
            pinnedTopologyBuffer, pinnedTopologyMidi);
        pinnedTopologyProcessor.recallSnapshot(0);
        auto* pinnedType =
            pinnedTopologyProcessor.parameters.getParameter(
                megadsp::slotParameterId(0, "type"));
        pinnedType->setValueNotifyingHost(
            pinnedType->convertTo0to1(static_cast<float>(
                megadsp::ModuleType::compressor)));
        pinnedTopologyProcessor.recallSnapshot(1);
        pinnedTopologyProcessor.flushPendingUpdatesForTesting();
        expect(pinnedTopologyProcessor.getRack().activeModuleType(0)
               == megadsp::ModuleType::equalizer);
        pinnedTopologyProcessor.recallSnapshot(0);
        pinnedTopologyBuffer.clear();
        pinnedTopologyProcessor.processBlock(
            pinnedTopologyBuffer, pinnedTopologyMidi);
        pinnedTopologyProcessor.flushPendingUpdatesForTesting();
        expect(pinnedTopologyProcessor.getRack().activeModuleType(0)
               == megadsp::ModuleType::compressor);

        beginTest("Program Change 0-7 recalls prepared snapshots");
        snapshotProcessor.recallSnapshot(0);
        snapshotBuffer.clear();
        snapshotProcessor.processBlock(snapshotBuffer, snapshotMidi);
        snapshotMidi.addEvent(
            juce::MidiMessage::programChange(16, 1), 128);
        snapshotBuffer.clear();
        snapshotProcessor.processBlock(snapshotBuffer, snapshotMidi);
        expectEquals(snapshotProcessor.getSelectedSnapshot(), 1);
        snapshotMidi.clear();
        snapshotMidi.addEvent(
            juce::MidiMessage::programChange(1, 12), 0);
        snapshotBuffer.clear();
        snapshotProcessor.processBlock(snapshotBuffer, snapshotMidi);
        expectEquals(snapshotProcessor.getSelectedSnapshot(), 1);

        beginTest("Schema 11 preserves all snapshots and selection");
        snapshotProcessor.flushPendingUpdatesForTesting();
        juce::MemoryBlock snapshotState;
        snapshotProcessor.getStateInformation(snapshotState);
        MegaDSPAudioProcessor snapshotRestored;
        snapshotRestored.prepareToPlay(48000.0, 512);
        snapshotRestored.setStateInformation(
            snapshotState.getData(),
            static_cast<int>(snapshotState.getSize()));
        expect(snapshotRestored.isSnapshotPopulated(0));
        expect(snapshotRestored.isSnapshotPopulated(1));
        expectEquals(snapshotRestored.getSelectedSnapshot(), 1);
        expect(snapshotRestored.getRack().activeModuleType(0)
               == megadsp::ModuleType::saturator);

        beginTest("Schema 10 snapshots initialize reverb ducking off");
        auto legacyDuckingState = snapshotRestored.parameters.copyState();
        legacyDuckingState.setProperty("schemaVersion", 10, nullptr);
        for (auto child : legacyDuckingState)
        {
            const auto id = child.getProperty("id").toString();
            if (id == megadsp::slotParameterId(0, "type"))
                child.setProperty(
                    "value",
                    static_cast<float>(
                        megadsp::ModuleType::algorithmicReverb),
                    nullptr);
            else if (id == megadsp::controlParameterId(0, 7))
                child.setProperty("value", 0.5f, nullptr);
        }
        if (const auto existing =
                legacyDuckingState.getChildWithName("SNAPSHOTS");
            existing.isValid())
            legacyDuckingState.removeChild(existing, nullptr);
        juce::ValueTree legacyDuckingSnapshots("SNAPSHOTS");
        legacyDuckingSnapshots.setProperty("selected", 0, nullptr);
        for (int snapshotIndex = 0; snapshotIndex < 2; ++snapshotIndex)
        {
            juce::ValueTree snapshot("SNAPSHOT");
            snapshot.setProperty("index", snapshotIndex, nullptr);
            snapshot.setProperty("populated", true, nullptr);
            juce::ValueTree slot("SLOT");
            slot.setProperty("index", 0, nullptr);
            slot.setProperty(
                "type",
                static_cast<float>(
                    snapshotIndex == 0
                        ? megadsp::ModuleType::algorithmicReverb
                        : megadsp::ModuleType::convolutionReverb),
                nullptr);
            for (int control = 0;
                 control < megadsp::controlsPerSlot; ++control)
                slot.setProperty(
                    "control" + juce::String(control + 1),
                    0.5f, nullptr);
            snapshot.addChild(slot, -1, nullptr);
            legacyDuckingSnapshots.addChild(snapshot, -1, nullptr);
        }
        legacyDuckingState.addChild(
            legacyDuckingSnapshots, -1, nullptr);
        auto legacyDuckingXml = legacyDuckingState.createXml();
        juce::MemoryBlock legacyDuckingData;
        expect(legacyDuckingXml != nullptr);
        if (legacyDuckingXml != nullptr)
            juce::AudioProcessor::copyXmlToBinary(
                *legacyDuckingXml, legacyDuckingData);
        MegaDSPAudioProcessor legacyDuckingRestored;
        legacyDuckingRestored.prepareToPlay(48000.0, 512);
        legacyDuckingRestored.setStateInformation(
            legacyDuckingData.getData(),
            static_cast<int>(legacyDuckingData.getSize()));
        expect(legacyDuckingRestored.getRack().activeModuleType(0)
               == megadsp::ModuleType::algorithmicReverb);
        expectEquals(
            legacyDuckingRestored.parameters
                .getRawParameterValue(
                    megadsp::controlParameterId(0, 7))
                ->load(),
            0.0f);
        legacyDuckingRestored.recallSnapshot(1);
        juce::AudioBuffer<float> legacyDuckingBuffer(2, 512);
        juce::MidiBuffer legacyDuckingMidi;
        legacyDuckingBuffer.clear();
        legacyDuckingRestored.processBlock(
            legacyDuckingBuffer, legacyDuckingMidi);
        legacyDuckingRestored.flushPendingUpdatesForTesting();
        expect(legacyDuckingRestored.getRack().activeModuleType(0)
               == megadsp::ModuleType::convolutionReverb);
        juce::MemoryBlock migratedDuckingData;
        legacyDuckingRestored.getStateInformation(
            migratedDuckingData);
        auto migratedDuckingXml =
            juce::AudioProcessor::getXmlFromBinary(
                migratedDuckingData.getData(),
                static_cast<int>(migratedDuckingData.getSize()));
        expect(migratedDuckingXml != nullptr);
        if (migratedDuckingXml != nullptr)
        {
            const auto migratedDuckingState =
                juce::ValueTree::fromXml(*migratedDuckingXml);
            const auto migratedSnapshots =
                migratedDuckingState.getChildWithName("SNAPSHOTS");
            for (const auto snapshot : migratedSnapshots)
                if (static_cast<int>(
                        snapshot.getProperty("index", -1)) == 1)
                    for (const auto slot : snapshot)
                        if (static_cast<int>(
                                slot.getProperty("index", -1)) == 0)
                            expectEquals(
                                static_cast<float>(static_cast<double>(
                                    slot.getProperty("control6", -1.0))),
                                0.0f);
        }

        beginTest("Live Safe mode persists with snapshot state");
        snapshotProcessor.setLiveSafeSnapshotsEnabled(true);
        snapshotState.reset();
        snapshotProcessor.getStateInformation(snapshotState);
        MegaDSPAudioProcessor liveSafeRestored;
        liveSafeRestored.prepareToPlay(48000.0, 512);
        liveSafeRestored.setStateInformation(
            snapshotState.getData(),
            static_cast<int>(snapshotState.getSize()));
        expect(liveSafeRestored.isLiveSafeSnapshotsEnabled());

        beginTest("Legacy schema 9 migrates into Snapshot 1 only");
        auto legacyState = snapshotRestored.parameters.copyState();
        legacyState.setProperty("schemaVersion", 9, nullptr);
        if (const auto snapshots =
                legacyState.getChildWithName("SNAPSHOTS");
            snapshots.isValid())
            legacyState.removeChild(snapshots, nullptr);
        auto legacyXml = legacyState.createXml();
        juce::MemoryBlock legacyData;
        expect(legacyXml != nullptr);
        if (legacyXml != nullptr)
            juce::AudioProcessor::copyXmlToBinary(*legacyXml, legacyData);
        MegaDSPAudioProcessor legacyRestored;
        legacyRestored.prepareToPlay(48000.0, 512);
        legacyRestored.setStateInformation(
            legacyData.getData(), static_cast<int>(legacyData.getSize()));
        expect(legacyRestored.isSnapshotPopulated(0));
        for (int index = 1; index < megadsp::snapshotCount; ++index)
            expect(!legacyRestored.isSnapshotPopulated(index));
        expectEquals(legacyRestored.getSelectedSnapshot(), 0);
        expect(legacyRestored.getRack().activeModuleType(0)
               == megadsp::ModuleType::saturator);

        beginTest("Missing snapshot assets report an explicit error");
        auto missingAssetState = snapshotRestored.parameters.copyState();
        missingAssetState.setProperty(
            "schemaVersion", megadsp::stateSchemaVersion, nullptr);
        if (const auto existing =
                missingAssetState.getChildWithName("SNAPSHOTS");
            existing.isValid())
            missingAssetState.removeChild(existing, nullptr);
        juce::ValueTree snapshots("SNAPSHOTS");
        snapshots.setProperty("selected", 0, nullptr);
        juce::ValueTree missingSnapshot("SNAPSHOT");
        missingSnapshot.setProperty("index", 0, nullptr);
        missingSnapshot.setProperty("populated", true, nullptr);
        juce::ValueTree missingSlot("SLOT");
        missingSlot.setProperty("index", 0, nullptr);
        missingSlot.setProperty(
            "type",
            static_cast<int>(megadsp::ModuleType::convolutionReverb),
            nullptr);
        missingSlot.setProperty(
            "impulseResponse",
            juce::File::getSpecialLocation(juce::File::tempDirectory)
                .getNonexistentChildFile(
                    "megaDSP-missing-snapshot-ir", ".wav", false)
                .getFullPathName(),
            nullptr);
        missingSnapshot.addChild(missingSlot, -1, nullptr);
        snapshots.addChild(missingSnapshot, -1, nullptr);
        missingAssetState.addChild(snapshots, -1, nullptr);
        auto missingAssetXml = missingAssetState.createXml();
        juce::MemoryBlock missingAssetData;
        expect(missingAssetXml != nullptr);
        if (missingAssetXml != nullptr)
            juce::AudioProcessor::copyXmlToBinary(
                *missingAssetXml, missingAssetData);
        MegaDSPAudioProcessor missingAssetRestored;
        missingAssetRestored.prepareToPlay(48000.0, 512);
        missingAssetRestored.setStateInformation(
            missingAssetData.getData(),
            static_cast<int>(missingAssetData.getSize()));
        expect(missingAssetRestored.isSnapshotReady(0));
        expect(missingAssetRestored.snapshotError(0).containsIgnoreCase("IR"));

        beginTest("Program Change applies at its sample offset");
        MegaDSPAudioProcessor offsetProcessor;
        offsetProcessor.prepareToPlay(48000.0, 512);
        auto* input = offsetProcessor.parameters.getParameter(
            "global.input");
        input->setValueNotifyingHost(input->convertTo0to1(-24.0f));
        offsetProcessor.storeSnapshot(1, false);
        juce::AudioBuffer<float> offsetBuffer(2, 512);
        offsetBuffer.clear();
        juce::MidiBuffer offsetMidi;
        offsetProcessor.processBlock(offsetBuffer, offsetMidi);
        input->setValueNotifyingHost(input->convertTo0to1(24.0f));
        offsetProcessor.recallSnapshot(0);
        offsetBuffer.clear();
        offsetProcessor.processBlock(offsetBuffer, offsetMidi);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < 512; ++sample)
                offsetBuffer.setSample(channel, sample, 1.0f);
        offsetMidi.addEvent(
            juce::MidiMessage::programChange(1, 1), 256);
        offsetProcessor.processBlock(offsetBuffer, offsetMidi);
        expect(offsetBuffer.getSample(0, 100) < 0.1f);
        expect(offsetBuffer.getSample(0, 500) > 1.0f);

        beginTest("Algorithmic reverb spills after snapshot recall");
        MegaDSPAudioProcessor spillProcessor;
        spillProcessor.prepareToPlay(48000.0, 512);
        spillProcessor.storeSnapshot(1, false);
        juce::AudioBuffer<float> spillBuffer(2, 512);
        spillBuffer.clear();
        juce::MidiBuffer spillMidi;
        spillProcessor.processBlock(spillBuffer, spillMidi);
        spillProcessor.recallSnapshot(0);
        spillBuffer.clear();
        spillProcessor.processBlock(spillBuffer, spillMidi);
        spillProcessor.addModule(
            megadsp::ModuleType::algorithmicReverb);
        spillBuffer.clear();
        spillBuffer.setSample(0, 0, 1.0f);
        spillBuffer.setSample(1, 0, 1.0f);
        spillProcessor.processBlock(spillBuffer, spillMidi);
        spillProcessor.recallSnapshot(1);
        auto tailEnergy = 0.0f;
        for (int block = 0; block < 12; ++block)
        {
            spillBuffer.clear();
            spillProcessor.processBlock(spillBuffer, spillMidi);
            if (block >= 2)
                tailEnergy += spillBuffer.getMagnitude(
                    0, 0, spillBuffer.getNumSamples());
        }
        expect(tailEnergy > 0.0001f);
        expect(spillProcessor.getTailLengthSeconds() > 0.0);

        beginTest("Rapid A-B-A recall reverses the active transition");
        MegaDSPAudioProcessor reverseProcessor;
        reverseProcessor.prepareToPlay(48000.0, 512);
        auto* reverseInput = reverseProcessor.parameters.getParameter(
            "global.input");
        reverseInput->setValueNotifyingHost(
            reverseInput->convertTo0to1(-12.0f));
        reverseProcessor.storeSnapshot(1, false);
        reverseInput->setValueNotifyingHost(
            reverseInput->convertTo0to1(12.0f));
        juce::AudioBuffer<float> reverseBuffer(2, 128);
        juce::MidiBuffer reverseMidi;
        for (int channel = 0; channel < reverseBuffer.getNumChannels();
             ++channel)
            juce::FloatVectorOperations::fill(
                reverseBuffer.getWritePointer(channel), 0.1f,
                reverseBuffer.getNumSamples());
        reverseProcessor.processBlock(reverseBuffer, reverseMidi);
        reverseProcessor.recallSnapshot(0);
        reverseProcessor.processBlock(reverseBuffer, reverseMidi);
        reverseProcessor.recallSnapshot(1);
        expectEquals(reverseProcessor.getSelectedSnapshot(), 1);
        reverseProcessor.processBlock(reverseBuffer, reverseMidi);
        for (int channel = 0; channel < reverseBuffer.getNumChannels();
             ++channel)
            for (int sample = 0; sample < reverseBuffer.getNumSamples();
                 ++sample)
                expect(std::isfinite(
                    reverseBuffer.getSample(channel, sample)));

        beginTest("A third spill fades the oldest and remains bounded");
        MegaDSPAudioProcessor boundedSpillProcessor;
        boundedSpillProcessor.prepareToPlay(48000.0, 512);
        boundedSpillProcessor.addModule(
            megadsp::ModuleType::algorithmicReverb);
        juce::AudioBuffer<float> boundedSpillBuffer(2, 512);
        juce::MidiBuffer boundedSpillMidi;
        for (int index = 1; index < 4; ++index)
        {
            boundedSpillProcessor.storeSnapshot(index, false);
            boundedSpillBuffer.clear();
            boundedSpillProcessor.processBlock(
                boundedSpillBuffer, boundedSpillMidi);
        }
        boundedSpillProcessor.prepareToPlay(48000.0, 512);
        for (int index = 0; index < 3; ++index)
        {
            boundedSpillProcessor.recallSnapshot(index);
            for (int block = 0; block < 2; ++block)
            {
                boundedSpillBuffer.clear();
                boundedSpillProcessor.processBlock(
                    boundedSpillBuffer, boundedSpillMidi);
            }
        }
        expectEquals(
            boundedSpillProcessor.activeSpillCountForTesting(), 3);
        expectEquals(
            boundedSpillProcessor.fadingSpillCountForTesting(), 1);
        for (int block = 0; block < 2; ++block)
        {
            boundedSpillBuffer.clear();
            boundedSpillProcessor.processBlock(
                boundedSpillBuffer, boundedSpillMidi);
        }
        expectEquals(
            boundedSpillProcessor.activeSpillCountForTesting(), 2);
        expectEquals(
            boundedSpillProcessor.fadingSpillCountForTesting(), 0);

        beginTest("Snapshots retain native rack latency");
        MegaDSPAudioProcessor nativeLatencyProcessor;
        nativeLatencyProcessor.prepareToPlay(48000.0, 512);
        nativeLatencyProcessor.storeSnapshot(1, false);
        juce::AudioBuffer<float> nativeLatencyBuffer(2, 512);
        juce::MidiBuffer nativeLatencyMidi;
        nativeLatencyBuffer.clear();
        nativeLatencyProcessor.processBlock(
            nativeLatencyBuffer, nativeLatencyMidi);
        nativeLatencyProcessor.addModule(megadsp::ModuleType::limiter);
        expectEquals(nativeLatencyProcessor.getRack().latencySamples(), 480);
        nativeLatencyProcessor.recallSnapshot(0);
        nativeLatencyBuffer.clear();
        nativeLatencyProcessor.processBlock(
            nativeLatencyBuffer, nativeLatencyMidi);
        expectEquals(nativeLatencyProcessor.getRack().latencySamples(), 0);
        expectEquals(
            nativeLatencyProcessor.activeSpillCountForTesting(), 1);
        nativeLatencyProcessor.recallSnapshot(1);
        nativeLatencyBuffer.clear();
        nativeLatencyProcessor.processBlock(
            nativeLatencyBuffer, nativeLatencyMidi);
        expectEquals(nativeLatencyProcessor.getRack().latencySamples(), 480);

        beginTest("Snapshot crossfades enter and leave with zero slope");
        MegaDSPAudioProcessor smoothTransitionProcessor;
        smoothTransitionProcessor.prepareToPlay(48000.0, 960);
        smoothTransitionProcessor.setLiveSafeSnapshotsEnabled(true);
        auto* smoothTransitionInput =
            smoothTransitionProcessor.parameters.getParameter("global.input");
        smoothTransitionInput->setValueNotifyingHost(
            smoothTransitionInput->convertTo0to1(-12.0f));
        smoothTransitionProcessor.storeSnapshot(1, false);
        smoothTransitionInput->setValueNotifyingHost(
            smoothTransitionInput->convertTo0to1(12.0f));
        smoothTransitionProcessor.recallSnapshot(0);
        juce::AudioBuffer<float> smoothTransitionBuffer(2, 960);
        juce::MidiBuffer smoothTransitionMidi;
        for (int block = 0; block < 3; ++block)
        {
            for (int channel = 0; channel < 2; ++channel)
                juce::FloatVectorOperations::fill(
                    smoothTransitionBuffer.getWritePointer(channel), 0.1f,
                    smoothTransitionBuffer.getNumSamples());
            smoothTransitionProcessor.processBlock(
                smoothTransitionBuffer, smoothTransitionMidi);
        }
        smoothTransitionProcessor.recallSnapshot(1);
        for (int channel = 0; channel < 2; ++channel)
            juce::FloatVectorOperations::fill(
                smoothTransitionBuffer.getWritePointer(channel), 0.1f,
                smoothTransitionBuffer.getNumSamples());
        smoothTransitionProcessor.processBlock(
            smoothTransitionBuffer, smoothTransitionMidi);
        const auto startStep = std::abs(
            smoothTransitionBuffer.getSample(0, 1)
            - smoothTransitionBuffer.getSample(0, 0));
        const auto middleStep = std::abs(
            smoothTransitionBuffer.getSample(0, 480)
            - smoothTransitionBuffer.getSample(0, 479));
        const auto endStep = std::abs(
            smoothTransitionBuffer.getSample(0, 959)
            - smoothTransitionBuffer.getSample(0, 958));
        const auto lastTransitionSample =
            smoothTransitionBuffer.getSample(0, 959);
        for (int channel = 0; channel < 2; ++channel)
            juce::FloatVectorOperations::fill(
                smoothTransitionBuffer.getWritePointer(channel), 0.1f,
                smoothTransitionBuffer.getNumSamples());
        smoothTransitionProcessor.processBlock(
            smoothTransitionBuffer, smoothTransitionMidi);
        const auto boundaryStep = std::abs(
            smoothTransitionBuffer.getSample(0, 0)
            - lastTransitionSample);
        expect(startStep < middleStep * 0.02f);
        expect(endStep < middleStep * 0.03f);
        expect(boundaryStep < middleStep * 0.03f);

        beginTest("Live Safe tremolo and delay recalls remain continuous");
        MegaDSPAudioProcessor modulationDelayProcessor;
        modulationDelayProcessor.prepareToPlay(48000.0, 64);
        modulationDelayProcessor.setLiveSafeSnapshotsEnabled(true);
        setType(
            modulationDelayProcessor, 0, megadsp::ModuleType::tremolo);
        modulationDelayProcessor.flushPendingUpdatesForTesting();
        modulationDelayProcessor.storeSnapshot(1, false);
        setType(
            modulationDelayProcessor, 0, megadsp::ModuleType::delay);
        modulationDelayProcessor.flushPendingUpdatesForTesting();
        modulationDelayProcessor.prepareToPlay(48000.0, 64);
        juce::AudioBuffer<float> modulationDelayBuffer(2, 64);
        juce::MidiBuffer modulationDelayMidi;
        auto phase = 0.0f;
        auto previousOutput = 0.0f;
        auto maximumStep = 0.0f;
        auto maximumRecallStep = 0.0f;
        for (int block = 0; block < 600; ++block)
        {
            if (block > 0 && block % 37 == 0)
                modulationDelayProcessor.recallSnapshot(
                    modulationDelayProcessor.getSelectedSnapshot() == 0
                        ? 1 : 0);
            for (int sample = 0; sample < 64; ++sample)
            {
                const auto inputSample = 0.15f * std::sin(phase);
                phase += juce::MathConstants<float>::twoPi * 220.0f
                         / 48000.0f;
                for (int channel = 0; channel < 2; ++channel)
                    modulationDelayBuffer.setSample(
                        channel, sample, inputSample);
            }
            modulationDelayProcessor.processBlock(
                modulationDelayBuffer, modulationDelayMidi);
            for (int sample = 0; sample < 64; ++sample)
            {
                const auto output =
                    modulationDelayBuffer.getSample(0, sample);
                const auto step = std::abs(output - previousOutput);
                maximumStep = juce::jmax(maximumStep, step);
                if (block > 0 && block % 37 == 0 && sample < 4)
                    maximumRecallStep =
                        juce::jmax(maximumRecallStep, step);
                previousOutput = output;
            }
        }
        expect(
            maximumRecallStep < 0.01f,
            "Recall step=" + juce::String(maximumRecallStep, 6)
                + ", maximum step=" + juce::String(maximumStep, 6));

        beginTest("Retained Live Safe fields fade in on recall");
        MegaDSPAudioProcessor retainedFieldProcessor;
        retainedFieldProcessor.prepareToPlay(48000.0, 512);
        retainedFieldProcessor.setLiveSafeSnapshotsEnabled(true);
        retainedFieldProcessor.addModule(megadsp::ModuleType::chordLoom);
        juce::AudioBuffer<float> retainedFieldBuffer(2, 512);
        juce::MidiBuffer retainedFieldMidi;
        auto retainedPhaseA = 0.0f;
        auto retainedPhaseB = 0.0f;
        auto retainedPhaseC = 0.0f;
        for (int block = 0; block < 100; ++block)
        {
            for (int sample = 0; sample < 512; ++sample)
            {
                const auto envelope = juce::jmin(
                    1.0f, static_cast<float>(block * 512 + sample)
                              / 288.0f);
                const auto value = envelope * 0.08f
                    * (std::sin(retainedPhaseA)
                       + std::sin(retainedPhaseB)
                       + std::sin(retainedPhaseC));
                retainedPhaseA +=
                    juce::MathConstants<float>::twoPi * 196.0f / 48000.0f;
                retainedPhaseB +=
                    juce::MathConstants<float>::twoPi * 246.94f / 48000.0f;
                retainedPhaseC +=
                    juce::MathConstants<float>::twoPi * 293.66f / 48000.0f;
                for (int channel = 0; channel < 2; ++channel)
                    retainedFieldBuffer.setSample(
                        channel, sample, value);
            }
            retainedFieldProcessor.processBlock(
                retainedFieldBuffer, retainedFieldMidi);
        }
        retainedFieldProcessor.recallSnapshot(1);
        for (int block = 0; block < 160; ++block)
        {
            retainedFieldBuffer.clear();
            retainedFieldProcessor.processBlock(
                retainedFieldBuffer, retainedFieldMidi);
        }
        retainedFieldProcessor.recallSnapshot(0);
        retainedFieldBuffer.clear();
        retainedFieldProcessor.processBlock(
            retainedFieldBuffer, retainedFieldMidi);
        expectWithinAbsoluteError(
            retainedFieldBuffer.getSample(0, 0), 0.0f, 0.000001f);
        expect(retainedFieldBuffer.getMagnitude(0, 512) > 0.00001f);

        beginTest("Snapshot spill budgets remain musically bounded");
        MegaDSPAudioProcessor spillBudgetProcessor;
        spillBudgetProcessor.prepareToPlay(48000.0, 512);
        spillBudgetProcessor.addModule(megadsp::ModuleType::chordLoom);
        expectWithinAbsoluteError(
            spillBudgetProcessor.getRack().snapshotSpillSeconds(),
            1.5, 0.000001);
        spillBudgetProcessor.addModule(
            megadsp::ModuleType::algorithmicReverb);
        expect(
            spillBudgetProcessor.getRack().snapshotSpillSeconds()
            <= 8.0);

        beginTest("PDC changes wait until monitoring becomes idle");
        MegaDSPAudioProcessor deferredLatencyProcessor;
        deferredLatencyProcessor.prepareToPlay(48000.0, 512);
        deferredLatencyProcessor.setLiveSafeSnapshotsEnabled(true);
        deferredLatencyProcessor.storeSnapshot(1, false);
        juce::AudioBuffer<float> deferredLatencyBuffer(2, 512);
        juce::MidiBuffer deferredLatencyMidi;
        deferredLatencyBuffer.clear();
        deferredLatencyProcessor.processBlock(
            deferredLatencyBuffer, deferredLatencyMidi);
        deferredLatencyProcessor.addModule(
            megadsp::ModuleType::limiter);
        deferredLatencyProcessor.settleAudioIdleForTesting();
        expectEquals(deferredLatencyProcessor.getLatencySamples(), 480);
        deferredLatencyProcessor.recallSnapshot(0);
        deferredLatencyBuffer.clear();
        deferredLatencyProcessor.processBlock(
            deferredLatencyBuffer, deferredLatencyMidi);
        deferredLatencyProcessor.flushPendingUpdatesForTesting();
        expectEquals(deferredLatencyProcessor.getLatencySamples(), 480);
        deferredLatencyProcessor.settleAudioIdleForTesting();
        expectEquals(deferredLatencyProcessor.getLatencySamples(), 0);

        beginTest("Large continuous blocks do not appear audio-idle");
        MegaDSPAudioProcessor largeBlockLatencyProcessor;
        largeBlockLatencyProcessor.prepareToPlay(44100.0, 8192);
        largeBlockLatencyProcessor.setLiveSafeSnapshotsEnabled(true);
        juce::AudioBuffer<float> largeBlockLatencyBuffer(2, 8192);
        juce::MidiBuffer largeBlockLatencyMidi;
        largeBlockLatencyBuffer.clear();
        largeBlockLatencyProcessor.processBlock(
            largeBlockLatencyBuffer, largeBlockLatencyMidi);
        largeBlockLatencyProcessor.addModule(
            megadsp::ModuleType::limiter);
        largeBlockLatencyProcessor.pollAudioIdleForTesting(5);
        expectEquals(largeBlockLatencyProcessor.getLatencySamples(), 0);
        largeBlockLatencyProcessor.settleAudioIdleForTesting();
        expectEquals(largeBlockLatencyProcessor.getLatencySamples(), 441);

        beginTest("Snapshot transitions chunk oversized host blocks");
        MegaDSPAudioProcessor oversizedSnapshotProcessor;
        oversizedSnapshotProcessor.prepareToPlay(48000.0, 64);
        oversizedSnapshotProcessor.storeSnapshot(1, false);
        juce::AudioBuffer<float> oversizedSnapshotBuffer(2, 4096);
        oversizedSnapshotBuffer.clear();
        juce::MidiBuffer oversizedSnapshotMidi;
        oversizedSnapshotProcessor.processBlock(
            oversizedSnapshotBuffer, oversizedSnapshotMidi);
        oversizedSnapshotProcessor.recallSnapshot(0);
        oversizedSnapshotMidi.addEvent(
            juce::MidiMessage::programChange(1, 1), 31);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < 4096; ++sample)
                oversizedSnapshotBuffer.setSample(
                    channel, sample, 0.1f);
        oversizedSnapshotProcessor.processBlock(
            oversizedSnapshotBuffer, oversizedSnapshotMidi);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < 4096; ++sample)
                expect(std::isfinite(
                    oversizedSnapshotBuffer.getSample(
                        channel, sample)));

        beginTest("Bypassed recall does not retain a stale transition");
        oversizedSnapshotProcessor.recallSnapshot(0);
        oversizedSnapshotMidi.clear();
        oversizedSnapshotBuffer.clear();
        oversizedSnapshotProcessor.processBlockBypassed(
            oversizedSnapshotBuffer, oversizedSnapshotMidi);
        expectEquals(
            oversizedSnapshotProcessor.getSelectedSnapshot(), 0);
        oversizedSnapshotMidi.addEvent(
            juce::MidiMessage::programChange(1, 1), 32);
        oversizedSnapshotProcessor.processBlockBypassed(
            oversizedSnapshotBuffer, oversizedSnapshotMidi);
        expectEquals(
            oversizedSnapshotProcessor.getSelectedSnapshot(), 1);
        oversizedSnapshotMidi.clear();
        oversizedSnapshotBuffer.clear();
        oversizedSnapshotProcessor.processBlock(
            oversizedSnapshotBuffer, oversizedSnapshotMidi);
        expectEquals(
            oversizedSnapshotProcessor.getSelectedSnapshot(), 1);
    }
};

ProcessorTopologyTests processorTopologyTests;
} // namespace
