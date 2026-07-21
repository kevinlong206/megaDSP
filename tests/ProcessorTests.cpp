#include "PluginProcessor.h"

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
