#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ReaperEmbeddedUI.h"
#include "StandaloneAudioSettings.h"
#include "ui/EffectGraph.h"
#include "ui/GuiLayout.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <vector>
#include <thread>
#include <utility>

#if JUCE_MAC
#include <CoreFoundation/CoreFoundation.h>
#endif

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
        if (details.programChanged)
            ++programChanges;
        if (details.parameterInfoChanged)
        {
            ++parameterInfoChanges;
            parameterInfoThread = std::this_thread::get_id();
        }
    }

    int nonParameterChanges = 0;
    int programChanges = 0;
    int parameterInfoChanges = 0;
    std::thread::id parameterInfoThread;
};

class HostParameterMetadataTests final : public juce::UnitTest
{
public:
    HostParameterMetadataTests()
        : juce::UnitTest("Host parameter metadata", "megaDSP")
    {
    }

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        beginTest("Every module supplies names without changing the host contract");
        juce::AudioProcessorGraph owner;
        juce::AudioProcessorValueTreeState state(
            owner, nullptr, "metadata", megadsp::createParameterLayout());
        expectEquals(owner.getParameters().size(), 114);
        for (int slot = 0; slot < 8; ++slot)
        {
            auto* typeParameter = state.getParameter(
                megadsp::slotParameterId(slot, "type"));
            expect(!typeParameter->isAutomatable());
            for (int type = 0; type < megadsp::moduleTypeCount; ++type)
            {
                typeParameter->setValueNotifyingHost(
                    typeParameter->convertTo0to1(static_cast<float>(type)));
                const auto& definition = megadsp::moduleDefinition(
                    static_cast<megadsp::ModuleType>(type));
                for (int control = 0; control < 12; ++control)
                {
                    auto* parameter = state.getParameter(
                        megadsp::controlParameterId(slot, control));
                    const auto& metadata =
                        definition.controls[static_cast<size_t>(control)];
                    const auto label = type == 0
                        ? "Unassigned " + juce::String(control + 1)
                        : metadata.isActive()
                            ? juce::String(metadata.name)
                            : "Unused " + juce::String(control + 1);
                    const auto expected = "S" + juce::String(slot + 1)
                        + " " + label + (type == 0 ? juce::String()
                            : " - " + juce::String(definition.displayName));
                    expectEquals(parameter->getName(128), expected);
                    expectEquals(parameter->getName(8), expected.substring(0, 8));
                    expectEquals(parameter->getParameterIndex(),
                                 slot * 14 + control + 2);
                    expectEquals(parameter->paramID,
                        "slot" + juce::String(slot + 1).paddedLeft('0', 2)
                        + ".control"
                        + juce::String(control + 1).paddedLeft('0', 2));
                    expectEquals(parameter->getVersionHint(), 1);
                    expectEquals(parameter->getDefaultValue(), 0.5f);
                    expect(parameter->isAutomatable());
                    expect(!parameter->isDiscrete());
                    expectWithinAbsoluteError(
                        parameter->convertTo0to1(0.37f), 0.37f, 0.000001f);
                    expectEquals(parameter->getText(0.37f, 0),
                        megadsp::formatControlValue(
                            definition.type, control, 0.37f));
                }
            }
        }

        beginTest("Host text uses musical units, named choices and toggle labels");
        auto* type = state.getParameter("slot01.type");
        auto* control = state.getParameter("slot01.control01");
        type->setValueNotifyingHost(type->convertTo0to1(2.0f));
        expectEquals(control->getName(128),
                     juce::String("S1 Threshold - Compressor"));
        expectEquals(control->getText(0.7f, 0), juce::String("-18.0 dB"));
        expectEquals(control->getText(0.7f, 5), juce::String("-18.0"));
        expectWithinAbsoluteError(
            control->getValueForText("-18 dB"), 0.7f, 0.000001f);
        auto* detector = state.getParameter("slot01.control08");
        expectEquals(detector->getText(1.0f, 0), juce::String("External"));
        expectEquals(detector->getValueForText("External"), 1.0f);
        expectEquals(detector->getValueForText("Internal"), 0.0f);
        auto* makeup = state.getParameter("slot01.control09");
        expectEquals(makeup->getValueForText("On"), 1.0f);
        expectEquals(makeup->getValueForText("Off"), 0.0f);
        control->setValueNotifyingHost(0.37f);
        expectEquals(control->getValueForText("not a value"), 0.37f);

        type->setValueNotifyingHost(type->convertTo0to1(4.0f));
        expectEquals(control->getName(128), juce::String("S1 Time - Delay"));
        const auto milliseconds = control->getValueForText("250 ms");
        expectEquals(control->getText(milliseconds, 0), juce::String("250 ms"));
        auto* division = state.getParameter("slot01.control02");
        const auto divisionText = division->getText(0.5f, 0);
        expectEquals(division->getText(
            division->getValueForText(divisionText), 0), divisionText);
        state.getParameter("slot01.control03")->setValueNotifyingHost(1.0f);
        expectEquals(control->getName(128), juce::String("S1 Time - Delay"));
        expectEquals(division->getName(128), juce::String("S1 Division - Delay"));
        expectEquals(control->getValue(), 0.37f);

        beginTest("Topology, undo and restore coalesce metadata notifications");
        MegaDSPAudioProcessor processor;
        processor.prepareToPlay(48000.0, 512);
        ProcessorChangeRecorder changes;
        processor.addListener(&changes);
        processor.addModule(megadsp::ModuleType::compressor);
        processor.addModule(megadsp::ModuleType::delay);
        expectEquals(changes.parameterInfoChanges, 0);
        processor.flushPendingUpdatesForTesting();
        expectEquals(changes.parameterInfoChanges, 1);
        processor.flushPendingUpdatesForTesting();
        expectEquals(changes.parameterInfoChanges, 1);
        auto* first = processor.parameters.getParameter("slot01.control01");
        auto* second = processor.parameters.getParameter("slot02.control01");
        juce::MemoryBlock saved;
        processor.getStateInformation(saved);
        processor.moveSlot(0, 1);
        processor.flushPendingUpdatesForTesting();
        expectEquals(changes.parameterInfoChanges, 2);
        expectEquals(first->getName(128), juce::String("S1 Time - Delay"));
        expectEquals(second->getName(128),
                     juce::String("S2 Threshold - Compressor"));
        expect(processor.undo());
        processor.flushPendingUpdatesForTesting();
        expectEquals(changes.parameterInfoChanges, 3);
        expectEquals(first->getName(128),
                     juce::String("S1 Threshold - Compressor"));
        expect(processor.redo());
        processor.flushPendingUpdatesForTesting();
        expectEquals(changes.parameterInfoChanges, 4);
        processor.setStateInformation(saved.getData(),
                                     static_cast<int>(saved.getSize()));
        processor.flushPendingUpdatesForTesting();
        expectEquals(changes.parameterInfoChanges, 5);
        expectEquals(first->getName(128),
                     juce::String("S1 Threshold - Compressor"));
        first->setValueNotifyingHost(0.45f);
        processor.flushPendingUpdatesForTesting();
        expectEquals(changes.parameterInfoChanges, 5);
        processor.clearSlot(0);
        processor.flushPendingUpdatesForTesting();
        expectEquals(changes.parameterInfoChanges, 6);
        expectEquals(first->getName(128), juce::String("S1 Time - Delay"));
        processor.clearSlot(0);
        processor.flushPendingUpdatesForTesting();
        expectEquals(changes.parameterInfoChanges, 7);
        expectEquals(first->getName(128), juce::String("S1 Unassigned 1"));
        processor.loadFactoryPreset(1);
        processor.flushPendingUpdatesForTesting();
        expectEquals(changes.parameterInfoChanges, 8);
        expectEquals(first->getName(128),
                     juce::String("S1 Low Frequency - Parametric EQ"));
        processor.removeListener(&changes);

        beginTest("MIDI snapshot recall refreshes metadata off the audio thread");
        MegaDSPAudioProcessor snapshots;
        snapshots.prepareToPlay(48000.0, 512);
        snapshots.addModule(megadsp::ModuleType::compressor);
        snapshots.storeSnapshot(1, false);
        snapshots.flushPendingUpdatesForTesting();
        snapshots.parameters.getParameter("slot01.type")
            ->setValueNotifyingHost(
                snapshots.parameters.getParameter("slot01.type")
                    ->convertTo0to1(4.0f));
        snapshots.flushPendingUpdatesForTesting();
        auto* recalled = snapshots.parameters.getParameter("slot01.control01");
        expectEquals(recalled->getName(128), juce::String("S1 Time - Delay"));
        ProcessorChangeRecorder recallChanges;
        snapshots.addListener(&recallChanges);
        ParameterEventRecorder recallValues(snapshots.getParameters().size());
        recallValues.watch(*recalled);
        juce::AudioBuffer<float> audio(2, 512);
        audio.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::programChange(1, 0), 37);
        std::thread audioThread([&]
        {
            snapshots.processBlock(audio, midi);
        });
        audioThread.join();
        expectEquals(recallChanges.parameterInfoChanges, 0);
        snapshots.flushPendingUpdatesForTesting();
        expectEquals(recallChanges.parameterInfoChanges, 1);
        expect(recallChanges.parameterInfoThread == std::this_thread::get_id());
        expectEquals(recalled->getName(128),
                     juce::String("S1 Threshold - Compressor"));
        expect(recallValues.values[static_cast<size_t>(
            recalled->getParameterIndex())].empty());
        snapshots.flushPendingUpdatesForTesting();
        expectEquals(recallChanges.parameterInfoChanges, 1);
        snapshots.removeListener(&recallChanges);
    }
};

HostParameterMetadataTests hostParameterMetadataTests;

class RackScrollingTests final : public juce::UnitTest
{
public:
    RackScrollingTests()
        : juce::UnitTest("Rack wheel scrolling", "megaDSP")
    {
    }

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        juce::Component content;
        juce::Component child;
        content.addAndMakeVisible(child);
        child.setBounds(0, 0, 1800, 600);
        HorizontalRackViewport viewport;
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(false, true);
        viewport.setSize(300, 200);
        content.setSize(1800, 600);
        const auto wheelEvent = [](juce::Component& target, bool shift)
        {
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent(
                juce::Desktop::getInstance().getMainMouseSource(),
                { 20.0f, 20.0f },
                juce::ModifierKeys(
                    shift ? juce::ModifierKeys::shiftModifier : 0),
                0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                &target, &target, now, { 20.0f, 20.0f }, now, 0, false);
        };

        beginTest("Vertical wheel never becomes horizontal, including Shift and inertia");
        for (const auto shift : { false, true })
            for (const auto inertial : { false, true })
                for (const auto delta : { -0.25f, 0.25f })
                {
                    viewport.setViewPosition(500, 0);
                    juce::MouseWheelDetails wheel {};
                    wheel.deltaY = delta;
                    wheel.isInertial = inertial;
                    viewport.mouseWheelMove(wheelEvent(viewport, shift), wheel);
                    expectEquals(viewport.getViewPositionX(), 500);
                    child.mouseWheelMove(wheelEvent(child, shift), wheel);
                    expectEquals(viewport.getViewPositionX(), 500);
                }

        beginTest("Horizontal wheel keeps its own direction and magnitude");
        for (const auto delta : { -0.25f, 0.25f })
        {
            viewport.setViewPosition(500, 0);
            juce::MouseWheelDetails wheel {};
            wheel.deltaX = delta;
            viewport.mouseWheelMove(wheelEvent(viewport, false), wheel);
            const auto horizontalPosition = viewport.getViewPositionX();
            expect(delta < 0.0f ? horizontalPosition > 500
                                : horizontalPosition < 500);
            viewport.setViewPosition(500, 0);
            wheel.deltaY = -1.0f;
            child.mouseWheelMove(wheelEvent(child, false), wheel);
            expectEquals(viewport.getViewPositionX(), horizontalPosition);
        }

        beginTest("Arrow keys still move the horizontal scrollbar");
        viewport.setViewPosition(500, 0);
        expect(viewport.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
        expect(viewport.getHorizontalScrollBar().getCurrentRangeStart() > 500.0);
        expect(viewport.keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
        expectEquals(
            viewport.getHorizontalScrollBar().getCurrentRangeStart(), 500.0);

        beginTest("Ultra retains vertical wheel scrolling without horizontal movement");
        viewport.setVerticalScrolling(true);
        viewport.setScrollBarsShown(true, false);
        viewport.setViewPosition(0, 200);
        juce::MouseWheelDetails wheel {};
        wheel.deltaY = -0.25f;
        child.mouseWheelMove(wheelEvent(child, false), wheel);
        expect(viewport.getViewPositionY() > 200);
        expectEquals(viewport.getViewPositionX(), 0);
    }
};

RackScrollingTests rackScrollingTests;

class EmbeddedBitmap final : public reaper::REAPER_FXEMBED_IBitmap
{
public:
    EmbeddedBitmap(
        int widthToUse, int heightToUse,
        int advisoryScaleToUse = 0)
        : width(widthToUse), height(heightToUse),
          advisoryScale(advisoryScaleToUse),
          pixels(static_cast<size_t>(width * height))
    {
    }

    unsigned int* getBits() override { return pixels.data(); }
    int getWidth() override { return width; }
    int getHeight() override { return height; }
    int getRowSpan() override { return width; }
    bool resize(int newWidth, int newHeight) override
    {
        width = newWidth;
        height = newHeight;
        pixels.assign(
            static_cast<size_t>(width * height), 0u);
        return true;
    }
    reaper::INT_PTR Extended(int id, void*) override
    {
        return id == REAPER_FXEMBED_EXT_GET_ADVISORY_SCALING
            ? static_cast<reaper::INT_PTR>(advisoryScale)
            : reaper::INT_PTR {};
    }

    int width;
    int height;
    int advisoryScale;
    std::vector<unsigned int> pixels;
};

class StandaloneTestDevice final : public juce::AudioIODevice
{
public:
    StandaloneTestDevice() : AudioIODevice("Test Interface", "Standalone Test") {}
    juce::StringArray getOutputChannelNames() override { return { "Out 1", "Out 2", "Out 3", "Out 4" }; }
    juce::StringArray getInputChannelNames() override { return { "In 1", "In 2", "In 3", "In 4" }; }
    juce::Array<double> getAvailableSampleRates() override { return { 48000.0 }; }
    juce::Array<int> getAvailableBufferSizes() override { return { 64 }; }
    int getDefaultBufferSize() override { return 64; }
    juce::String open(const juce::BigInteger& inputs, const juce::BigInteger& outputs,
                      double, int) override
    {
        inputChannels = inputs;
        outputChannels = outputs;
        opened = true;
        return {};
    }
    void close() override { stop(); opened = false; }
    bool isOpen() override { return opened; }
    void start(juce::AudioIODeviceCallback* next) override
    {
        callback = next;
        if (callback != nullptr)
            callback->audioDeviceAboutToStart(this);
    }
    void stop() override
    {
        if (auto* previous = std::exchange(callback, nullptr))
            previous->audioDeviceStopped();
    }
    bool isPlaying() override { return callback != nullptr; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return 64; }
    double getCurrentSampleRate() override { return 48000.0; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return outputChannels; }
    juce::BigInteger getActiveInputChannels() const override { return inputChannels; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }

private:
    juce::BigInteger inputChannels, outputChannels;
    juce::AudioIODeviceCallback* callback = nullptr;
    bool opened = false;
};

class StandaloneTestDeviceType final : public juce::AudioIODeviceType
{
public:
    StandaloneTestDeviceType() : AudioIODeviceType("Standalone Test") {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames(bool) const override { return { "Test Interface" }; }
    int getDefaultDeviceIndex(bool) const override { return 0; }
    int getIndexOfDevice(juce::AudioIODevice*, bool) const override { return 0; }
    bool hasSeparateInputsAndOutputs() const override { return false; }
    juce::AudioIODevice* createDevice(const juce::String&, const juce::String&) override
    {
        return new StandaloneTestDevice;
    }
};

class StandaloneTestDeviceManager final : public juce::AudioDeviceManager
{
    void createAudioDeviceTypes(juce::OwnedArray<juce::AudioIODeviceType>& types) override
    {
        types.add(new StandaloneTestDeviceType);
    }
};

class ProcessorChannelLayoutTests final : public juce::UnitTest
{
public:
    ProcessorChannelLayoutTests()
        : juce::UnitTest("Processor channel layouts", "megaDSP")
    {
    }

    void runTest() override
    {
        beginTest("Standalone selects an individual input while retaining stereo output");
        {
            StandaloneTestDeviceManager manager;
            expectEquals(manager.initialise(2, 2, nullptr, false), juce::String {});
            MegaDSPAudioProcessor processor;
            processor.disableNonMainBuses();
            juce::AudioProcessorPlayer player;
            player.setProcessor(&processor);
            manager.addAudioCallback(&player);
            juce::Value muted(true);
            megadsp::StandaloneAudioSettings settings(manager, muted);
            std::function<juce::ListBox*(juce::Component&, const juce::String&)> findChannels =
                [&findChannels](juce::Component& component, const juce::String& text)
                    -> juce::ListBox*
                {
                    if (auto* label = dynamic_cast<juce::Label*>(&component))
                        if (label->getText() == text)
                            return dynamic_cast<juce::ListBox*>(label->getAttachedComponent());
                    for (auto* child : component.getChildren())
                        if (auto* found = findChannels(*child, text))
                            return found;
                    return nullptr;
                };
            auto* inputs = findChannels(settings, "Active input channels:");
            auto* outputs = findChannels(settings, "Active output channels:");
            expect(inputs != nullptr && outputs != nullptr);
            if (inputs != nullptr && outputs != nullptr)
            {
                expectEquals(inputs->getListBoxModel()->getNumRows(), 4);
                inputs->getListBoxModel()->returnKeyPressed(3);
                inputs->getListBoxModel()->returnKeyPressed(1);
                const auto selected = manager.getAudioDeviceSetup();
                expectEquals(selected.inputChannels.countNumberOfSetBits(), 1);
                expect(selected.inputChannels[3]);
                expectEquals(selected.outputChannels.countNumberOfSetBits(), 2);
                expectEquals(processor.getMainBusNumOutputChannels(), 2);
                std::array<float, 64> input, left {}, right {};
                input.fill(0.25f);
                const float* inputPointers[] { input.data() };
                float* outputPointers[] { left.data(), right.data() };
                player.audioDeviceIOCallbackWithContext(
                    inputPointers, 1, outputPointers, 2, 64, {});
                for (int sample = 0; sample < 64; ++sample)
                {
                    expectWithinAbsoluteError(left[static_cast<size_t>(sample)], 0.25f, 0.00001f);
                    expectWithinAbsoluteError(right[static_cast<size_t>(sample)], 0.25f, 0.00001f);
                }
                outputs->getListBoxModel()->returnKeyPressed(1);
                expectEquals(manager.getAudioDeviceSetup().outputChannels.countNumberOfSetBits(), 2);
                inputs->getListBoxModel()->returnKeyPressed(2);
                expectEquals(manager.getAudioDeviceSetup().inputChannels.countNumberOfSetBits(), 2);
                expectEquals(processor.getMainBusNumInputChannels(), 2);
                expectEquals(processor.getMainBusNumOutputChannels(), 2);
            }
            manager.removeAudioCallback(&player);
            player.setProcessor(nullptr);
        }

        beginTest("Only mono, mono-to-stereo, and stereo layouts are accepted");
        {
            MegaDSPAudioProcessor processor;
            for (const auto& input : { juce::AudioChannelSet::disabled(),
                                      juce::AudioChannelSet::mono(),
                                      juce::AudioChannelSet::stereo(),
                                      juce::AudioChannelSet::create5point1() })
                for (const auto& output : { juce::AudioChannelSet::disabled(),
                                           juce::AudioChannelSet::mono(),
                                           juce::AudioChannelSet::stereo(),
                                           juce::AudioChannelSet::create5point1() })
                {
                    auto layout = processor.getBusesLayout();
                    layout.inputBuses.set(0, input);
                    layout.outputBuses.set(0, output);
                    const auto supported =
                        (input == juce::AudioChannelSet::mono()
                         && (output == juce::AudioChannelSet::mono()
                             || output == juce::AudioChannelSet::stereo()))
                        || (input == juce::AudioChannelSet::stereo()
                            && output == juce::AudioChannelSet::stereo());
                    expect(processor.isBusesLayoutSupported(layout)
                           == supported);
                }
        }

        beginTest("Mono-to-stereo matches duplicated mono with sidechain and bypass");
        for (const auto sampleRate : { 44100.0, 48000.0, 96000.0 })
            for (const auto sidechainChannels : { 0, 1, 2 })
                for (const auto type : { megadsp::ModuleType::empty,
                                         megadsp::ModuleType::delay,
                                         megadsp::ModuleType::gateExpander,
                                         megadsp::ModuleType::limiter })
                {
                    MegaDSPAudioProcessor monoProcessor;
                    MegaDSPAudioProcessor stereoProcessor;
                    for (auto* processor : { &monoProcessor, &stereoProcessor })
                    {
                        auto layout = processor->getBusesLayout();
                        layout.inputBuses.set(
                            0, processor == &monoProcessor
                                ? juce::AudioChannelSet::mono()
                                : juce::AudioChannelSet::stereo());
                        layout.inputBuses.set(
                            1, sidechainChannels == 0
                                ? juce::AudioChannelSet::disabled()
                                : juce::AudioChannelSet::canonicalChannelSet(
                                      sidechainChannels));
                        expect(processor->setBusesLayout(layout));
                        processor->prepareToPlay(sampleRate, 64);
                        auto* parameter = processor->parameters.getParameter(
                            megadsp::slotParameterId(0, "type"));
                        parameter->setValueNotifyingHost(
                            parameter->convertTo0to1(static_cast<float>(type)));
                        processor->flushPendingUpdatesForTesting();
                        if (type == megadsp::ModuleType::gateExpander)
                            for (const auto control : { 8, 9 })
                                processor->parameters.getParameter(
                                    megadsp::controlParameterId(0, control))
                                    ->setValueNotifyingHost(1.0f);
                    }
                    juce::MidiBuffer midi;
                    // Nonzero MIDI offsets also split the oversized callback.
                    midi.addEvent(juce::MidiMessage::programChange(1, 1), 37);
                    auto stereoDifference = 0.0f;
                    for (int block = 0; block < 32; ++block)
                    {
                        constexpr int samples = 137;
                        juce::AudioBuffer<float> mono(
                            juce::jmax(2, 1 + sidechainChannels), samples);
                        juce::AudioBuffer<float> stereo(
                            2 + sidechainChannels, samples);
                        mono.clear();
                        stereo.clear();
                        for (int sample = 0; sample < samples; ++sample)
                        {
                            const auto input = 0.2f * std::sin(
                                static_cast<float>(block * samples + sample)
                                * 0.031f);
                            mono.setSample(0, sample, input);
                            stereo.setSample(0, sample, input);
                            stereo.setSample(1, sample, input);
                            for (int channel = 0; channel < sidechainChannels;
                                 ++channel)
                            {
                                const auto detector = 0.1f * std::cos(
                                    static_cast<float>(block * samples + sample)
                                    * (0.07f + 0.03f
                                        * static_cast<float>(channel)));
                                mono.setSample(1 + channel, sample, detector);
                                stereo.setSample(2 + channel, sample, detector);
                            }
                        }
                        if (block >= 24)
                        {
                            monoProcessor.processBlockBypassed(mono, midi);
                            stereoProcessor.processBlockBypassed(stereo, midi);
                        }
                        else
                        {
                            monoProcessor.processBlock(mono, midi);
                            stereoProcessor.processBlock(stereo, midi);
                        }
                        auto maximumError = 0.0f;
                        for (int sample = 0; sample < samples; ++sample)
                            for (int channel = 0; channel < 2; ++channel)
                            {
                                const auto actual = mono.getSample(channel, sample);
                                expect(std::isfinite(actual));
                                maximumError = juce::jmax(
                                    maximumError,
                                    std::abs(actual - stereo.getSample(
                                        channel, sample)));
                                if (block < 24)
                                    stereoDifference += std::abs(
                                        mono.getSample(0, sample)
                                        - mono.getSample(1, sample));
                            }
                        expectWithinAbsoluteError(
                            maximumError, 0.0f, 0.000001f);
                    }
                    if (type == megadsp::ModuleType::gateExpander
                        && sidechainChannels == 2)
                        expect(stereoDifference > 0.1f);
                }
    }
};

ProcessorChannelLayoutTests processorChannelLayoutTests;

class ProcessorTopologyTests final : public juce::UnitTest
{
public:
    ProcessorTopologyTests()
        : juce::UnitTest("Processor topology", "megaDSP")
    {
    }

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
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

        beginTest("Capture histories survive selection and clear on reset");
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
                .input.copyLatest(captured)), 1);
        expectEquals(captured[0], 0.5f);

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
            expectEquals(
                editor->commitHashForTesting(),
                juce::String(MEGADSP_COMMIT_HASH));
            expect(editor->cardChildrenFitForTesting());
            expect(editor->cardGraphsAvoidControlsForTesting());
            expectEquals(
                editor->activeVisualizationCardCountForTesting(), 0);
            expectEquals(
                static_cast<int>(
                    editorProcessor->getVisualizationData()
                        .getActiveSlotMask()),
                0);
            editor->setCardsEditorVisibleForTesting(true);
            expectEquals(
                editor->activeVisualizationCardCountForTesting(), 2);
            expect(
                !editorProcessor->getVisualizationData().isSlotActive(0),
                "The Guitar Cabinet IR list must not capture unused DSP "
                "telemetry.");
            expect(editorProcessor->getVisualizationData().isSlotActive(1));
            editor->simulateDescendantFocusForTesting(1);
            expectEquals(editorProcessor->getSelectedTab(), 1);
            expectEquals(
                editor->activeVisualizationCardCountForTesting(), 2);
            editor->simulateCardHoverForTesting(0);
            expectEquals(editorProcessor->getSelectedTab(), 0);
            expectEquals(
                editor->activeVisualizationCardCountForTesting(), 2);
            editor->simulateCardHoverExitForTesting(0);
            expectEquals(editorProcessor->getSelectedTab(), 1);
            expectEquals(
                editor->activeVisualizationCardCountForTesting(), 2);
            editorProcessor->setSelectedTab(0);
            editor->simulateFocusGainDuringRefreshForTesting(1);
            expectEquals(editorProcessor->getSelectedTab(), 0);
            editor->setCardsEditorVisibleForTesting(false);
            editor->clickCloseButtonForTesting(0);
            expectEquals(editorProcessor->getRack().activeSlotCount(), 1);
            expectEquals(editor->tabCountForTesting(), 1);
            expect(editorProcessor->getRack().moduleType(0)
                   == megadsp::ModuleType::compressor);
        }

        beginTest(
            "Eight widgets persist and overflow horizontally");
        {
            auto rackProcessor = std::make_unique<MegaDSPAudioProcessor>();
            rackProcessor->prepareToPlay(48000.0, 512);
            for (int slot = 0; slot < megadsp::slotCount; ++slot)
                rackProcessor->addModule(megadsp::ModuleType::equalizer);
            auto editor = std::make_unique<MegaDSPAudioProcessorEditor>(
                *rackProcessor, 820, 620);

            expectEquals(editor->cardCountForTesting(), megadsp::slotCount);
            expectEquals(
                editor->cardWidthForTesting(0),
                480);
            expect(editor->cardChildrenFitForTesting());
            expect(editor->cardGraphsAvoidControlsForTesting());
            expect(
                editor->headerMeterWidthForTesting() >= 72,
                "The header DSP readout must fit at compact editor widths.");
            expect(editor->rackOverflowsHorizontallyForTesting());
            expect(!editor->rackOverflowsVerticallyForTesting());
            expectEquals(
                editor->activeVisualizationCardCountForTesting(), 0);

            editor->refreshCardsForTesting();
            expectEquals(editor->cardCountForTesting(), megadsp::slotCount);
            expect(editor->rackOverflowsHorizontallyForTesting());

            editor->clickChainNavigatorForTesting(megadsp::slotCount - 1);
            expectEquals(
                editor->selectedCardForTesting(), megadsp::slotCount - 1);
            expect(editor->selectedCardVisibleForTesting());
            editor->clickChainNavigatorForTesting(0);
            expectEquals(editor->selectedCardForTesting(), 0);
            expect(editor->selectedCardVisibleForTesting());

            for (int slot = 0; slot < megadsp::slotCount - 1; ++slot)
                expect(editor->cardForTesting(slot)->keyPressed(
                    juce::KeyPress(juce::KeyPress::rightKey)));
            expectEquals(editor->selectedCardForTesting(), 7);
            expect(editor->selectedCardVisibleForTesting());
            for (int slot = megadsp::slotCount - 1; slot > 0; --slot)
                expect(editor->cardForTesting(slot)->keyPressed(
                    juce::KeyPress(juce::KeyPress::leftKey)));
            expectEquals(editor->selectedCardForTesting(), 0);
            expect(editor->selectedCardVisibleForTesting());

            editor->setWidgetSizeForTesting(
                megadsp::ui::WidgetSize::medium);
            const auto mediumGraphWidth =
                editor->cardGraphWidthForTesting(0);
            expectEquals(
                editor->cardWidthForTesting(0),
                640);
            editor->setWidgetSizeForTesting(
                megadsp::ui::WidgetSize::large);
            expect(
                editor->cardGraphWidthForTesting(0) > mediumGraphWidth,
                "Animated graph should follow module width changes.");
            expectEquals(
                editor->cardWidthForTesting(0),
                800);
            const auto graphHeightBeforeHostResize =
                editor->cardGraphHeightForTesting(0);
            editor->setSize(820, 760);
            expect(
                editor->cardGraphHeightForTesting(0)
                    > graphHeightBeforeHostResize,
                "Animated graph should follow host height changes.");
            editor->setCardsEditorVisibleForTesting(true);
            expectEquals(
                editor->activeVisualizationCardCountForTesting(),
                megadsp::slotCount);
            rackProcessor->getVisualizationData().clearActiveSlots();
            editor->setWidgetSizeForTesting(
                megadsp::ui::WidgetSize::medium);
            expectEquals(
                static_cast<int>(
                    rackProcessor->getVisualizationData()
                        .getActiveSlotMask()),
                (1 << megadsp::slotCount) - 1);
        }

        beginTest("Ultra compact keeps primary controls, expansion and vertical reordering");
        {
            MegaDSPAudioProcessor processor;
            processor.prepareToPlay(48000.0, 512);
            processor.addModule(megadsp::ModuleType::compressor);
            for (int slot = 1; slot < megadsp::slotCount; ++slot)
                processor.addModule(megadsp::ModuleType::delay);
            processor.parameters.getParameter(
                megadsp::controlParameterId(0, 0))->setValueNotifyingHost(0.37f);
            MegaDSPAudioProcessorEditor editor(processor, 820, 280);
            const auto fullHeight = editor.cardHeightForTesting(0);
            const auto* preparedCompressor = processor.getRack().activeModuleInstance(0);
            editor.setWidgetSizeForTesting(
                megadsp::ui::WidgetSize::ultraCompact);
            expectEquals(editor.cardCountForTesting(), 8);
            expectEquals(editor.chainInsertButtonCountForTesting(), 0);
            expect(editor.rackOverflowsVerticallyForTesting());
            expect(!editor.rackOverflowsHorizontallyForTesting());
            editor.setCardsEditorVisibleForTesting(true);
            expectEquals(editor.activeVisualizationCardCountForTesting(), 0);
            expect(editor.cardChildrenFitForTesting());
            for (int slot = 0; slot < 8; ++slot)
            {
                auto* card = editor.cardForTesting(slot);
                expectEquals(card->getHeight(), 36);
                if (slot > 0)
                    expect(card->getY()
                           > editor.cardForTesting(slot - 1)->getBottom());
                int visible = 0;
                for (auto* child : card->getChildren())
                    if (child->isVisible())
                    {
                        ++visible;
                        expect(dynamic_cast<juce::Label*>(child) != nullptr
                               || dynamic_cast<juce::Button*>(child) != nullptr
                               || dynamic_cast<juce::Slider*>(child) != nullptr
                               || dynamic_cast<juce::ComboBox*>(child) != nullptr);
                    }
                expect(visible >= 6, "Ultra must expose controls as well as title, grip, expansion and bypass.");
            }
            const auto toggleExpansion = [&](int slot)
            {
                auto* card = editor.cardForTesting(slot);
                juce::Button* expansion = nullptr;
                for (auto* child : card->getChildren())
                    if (auto* button = dynamic_cast<juce::Button*>(child))
                        if (button->isVisible()
                            && (button->getButtonText() == "+"
                                || button->getButtonText() == "-"))
                            expansion = button;
                expect(expansion != nullptr, "Every Ultra module needs an expansion button.");
                if (expansion != nullptr)
                    expansion->onClick();
            };
            for (int slot = 0; slot < 8; ++slot)
            {
                toggleExpansion(slot);
                expectEquals(editor.cardHeightForTesting(slot), fullHeight);
                expect(editor.cardChildrenFitForTesting());
            }
            editor.setCardsEditorVisibleForTesting(true);
            expectEquals(editor.activeVisualizationCardCountForTesting(), 8);
            editor.refreshCardsForTesting();
            for (int slot = 0; slot < 8; ++slot)
            {
                expectEquals(editor.cardHeightForTesting(slot), fullHeight);
                expect(editor.cardForTesting(slot)->keyPressed(
                    juce::KeyPress(juce::KeyPress::returnKey)));
                expectEquals(editor.cardHeightForTesting(slot), 36);
            }
            auto* ratio = static_cast<juce::Slider*>(nullptr);
            for (auto* child : editor.cardForTesting(0)->getChildren())
                if (child->isVisible() && child->getTitle() == "Ratio")
                    ratio = dynamic_cast<juce::Slider*>(child);
            expect(ratio != nullptr);
            if (ratio != nullptr)
            {
                expect(ratio->getSliderStyle() == juce::Slider::LinearHorizontal);
                ratio->setValue(0.63f, juce::sendNotificationSync);
                expectWithinAbsoluteError(
                    processor.parameters.getRawParameterValue(
                        megadsp::controlParameterId(0, 1))->load(), 0.63f, 0.0001f);
            }
            auto* first = editor.cardForTesting(0);
            expect(first->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
            expect(processor.isSlotBypassed(0));
            expect(first->keyPressed(juce::KeyPress('b', {}, 'b')));
            expect(!processor.isSlotBypassed(0));
            expect(first->keyPressed(juce::KeyPress(
                juce::KeyPress::downKey, juce::ModifierKeys::altModifier, 0)));
            editor.applyPendingCardMutationForTesting();
            expect(processor.getRack().moduleType(1)
                   == megadsp::ModuleType::compressor);
            toggleExpansion(1);
            auto* target = editor.cardForTesting(7);
            auto* dropTarget = dynamic_cast<juce::DragAndDropTarget*>(target);
            expect(dropTarget != nullptr);
            if (dropTarget != nullptr)
            {
                const juce::DragAndDropTarget::SourceDetails details(
                    1, editor.cardForTesting(1),
                    { 1, target->getHeight() - 1 });
                expect(dropTarget->isInterestedInDragSource(details));
                dropTarget->itemDragEnter(details);
                dropTarget->itemDropped(details);
                editor.applyPendingCardMutationForTesting();
            }
            expect(processor.getRack().moduleType(7)
                   == megadsp::ModuleType::compressor);
            expect(processor.getRack().activeModuleInstance(7) == preparedCompressor);
            expectEquals(editor.cardHeightForTesting(7), fullHeight);
            expectWithinAbsoluteError(
                processor.parameters.getParameter(
                    megadsp::controlParameterId(7, 0))->getValue(),
                0.37f, 0.0001f);
            target = editor.cardForTesting(0);
            dropTarget = dynamic_cast<juce::DragAndDropTarget*>(target);
            if (dropTarget != nullptr)
            {
                const juce::DragAndDropTarget::SourceDetails details(
                    7, editor.cardForTesting(7),
                    { target->getWidth() - 1, 1 });
                dropTarget->itemDragEnter(details);
                dropTarget->itemDropped(details);
                editor.applyPendingCardMutationForTesting();
            }
            expect(processor.getRack().moduleType(0)
                   == megadsp::ModuleType::compressor);
            expectEquals(editor.cardHeightForTesting(0), fullHeight);
            toggleExpansion(0);
            editor.clickChainNavigatorForTesting(7);
            expect(editor.selectedCardVisibleForTesting());
            expect(editor.cardForTesting(7)->keyPressed(
                juce::KeyPress(juce::KeyPress::upKey)));
            expectEquals(editor.selectedCardForTesting(), 6);
            expect(editor.selectedCardVisibleForTesting());
            editor.setSize(820, 620);
            expect(!editor.rackOverflowsVerticallyForTesting());
            editor.setWidgetSizeForTesting(megadsp::ui::WidgetSize::small);
            expectEquals(editor.chainInsertButtonCountForTesting(), 0);
            expect(editor.rackOverflowsHorizontallyForTesting());
            expect(editor.cardPresentsAllActiveControlsForTesting());
            expect(editor.cardChildrenFitForTesting());
            expectWithinAbsoluteError(
                processor.parameters.getParameter(
                    megadsp::controlParameterId(0, 0))->getValue(),
                0.37f, 0.0001f);
            editor.setWidgetSizeForTesting(
                megadsp::ui::WidgetSize::ultraCompact);
            editor.refreshCardsForTesting();
            expectEquals(editor.cardHeightForTesting(7), 36);
        }

        beginTest("Ultra contextual controls retain hidden settings and edit without topology changes");
        {
            MegaDSPAudioProcessor processor;
            processor.prepareToPlay(48000.0, 512);
            processor.addModule(megadsp::ModuleType::envelopeModulator);
            processor.parameters.getParameter(megadsp::controlParameterId(0, 7))
                ->setValueNotifyingHost(0.37f);
            processor.parameters.getParameter(megadsp::controlParameterId(0, 8))
                ->setValueNotifyingHost(0.42f);
            MegaDSPAudioProcessorEditor editor(processor, 820, 400);
            editor.setWidgetSizeForTesting(megadsp::ui::WidgetSize::ultraCompact);
            const auto topology = processor.getTopologyGeneration();
            juce::ComboBox* mode = nullptr;
            for (auto* child : editor.cardForTesting(0)->getChildren())
                if (child->isVisible() && child->getTitle() == "Mode")
                    mode = dynamic_cast<juce::ComboBox*>(child);
            expect(mode != nullptr);
            if (mode != nullptr)
                mode->setSelectedId(3, juce::sendNotificationSync);
            expectEquals(megadsp::discreteIndex(
                processor.parameters.getRawParameterValue(
                    megadsp::controlParameterId(0, 0))->load(), 3), 2);
            auto pitchVisible = false;
            auto tremoloVisible = false;
            for (auto* child : editor.cardForTesting(0)->getChildren())
            {
                pitchVisible = pitchVisible
                    || (child->isVisible() && child->getTitle() == "Pitch Depth");
                tremoloVisible = tremoloVisible
                    || (child->isVisible() && child->getTitle() == "Tremolo Depth");
            }
            expect(pitchVisible);
            expect(!tremoloVisible);
            expectWithinAbsoluteError(processor.parameters.getRawParameterValue(
                megadsp::controlParameterId(0, 7))->load(), 0.37f, 0.0001f);
            expectWithinAbsoluteError(processor.parameters.getRawParameterValue(
                megadsp::controlParameterId(0, 8))->load(), 0.42f, 0.0001f);
            expectEquals(processor.getTopologyGeneration(), topology);
            expect(editor.cardChildrenFitForTesting());
        }

        beginTest("Widget size is global and absent from project state");
        {
            MegaDSPAudioProcessor preferenceWriter;
            expect(preferenceWriter.setGlobalWidgetSizeId("medium").wasOk());
            MegaDSPAudioProcessor preferenceReader;
            expectEquals(
                preferenceReader.globalWidgetSizeId(),
                juce::String("medium"));
            expect(preferenceWriter.setGlobalWidgetSizeId(
                "ultra-compact").wasOk());
            expectEquals(
                preferenceReader.globalWidgetSizeId(),
                juce::String("ultra-compact"));
            preferenceReader.addModule(megadsp::ModuleType::equalizer);
            {
                MegaDSPAudioProcessorEditor editor(preferenceReader, 820, 280);
                expectEquals(editor.cardHeightForTesting(0), 36);
                expectEquals(editor.chainInsertButtonCountForTesting(), 0);
            }

            juce::MemoryBlock state;
            preferenceReader.getStateInformation(state);
            const auto widgetStateXml =
                juce::AudioProcessor::getXmlFromBinary(
                state.getData(), static_cast<int>(state.getSize()));
            expect(widgetStateXml != nullptr);
            expect(widgetStateXml == nullptr
                   || !widgetStateXml->hasAttribute("widgetSize"));

            expect(preferenceWriter.setGlobalWidgetSizeId("invalid").wasOk());
            expectEquals(
                preferenceReader.globalWidgetSizeId(),
                juce::String("small"));
        }

        beginTest("Convolution Reverb fits a compact widget");
        {
            auto convolutionProcessor =
                std::make_unique<MegaDSPAudioProcessor>();
            convolutionProcessor->prepareToPlay(48000.0, 512);
            convolutionProcessor->addModule(
                megadsp::ModuleType::convolutionReverb);
            auto editor = std::make_unique<MegaDSPAudioProcessorEditor>(
                *convolutionProcessor, 1100, 720);
            expectEquals(
                editor->cardWidthForTesting(0),
                megadsp::ui::widgetWidth(
                    megadsp::ui::WidgetSize::small));
            expect(editor->cardChildrenFitForTesting());
        }

        beginTest("Neural Amp Modeler fits a compact widget");
        {
            auto namProcessor =
                std::make_unique<MegaDSPAudioProcessor>();
            namProcessor->prepareToPlay(48000.0, 512);
            namProcessor->addModule(
                megadsp::ModuleType::neuralAmpModeler);
            auto editor = std::make_unique<MegaDSPAudioProcessorEditor>(
                *namProcessor, 1100, 720);
            expectEquals(
                editor->cardWidthForTesting(0),
                megadsp::ui::widgetWidth(
                    megadsp::ui::WidgetSize::small));
            expect(editor->cardChildrenFitForTesting());
        }

        beginTest("Every module fits every widget size");
        std::array<
            bool,
            static_cast<size_t>(
                megadsp::CompactVisualization::count)>
            compactVisualizationKinds {};
        for (int rawType = 1;
             rawType < megadsp::moduleTypeCount; ++rawType)
        {
            const auto visualization =
                megadsp::compactVisualization(
                    static_cast<megadsp::ModuleType>(rawType));
            const auto visualizationIndex =
                static_cast<size_t>(visualization);
            expect(
                visualizationIndex < compactVisualizationKinds.size(),
                "Module has an invalid compact visualization.");
            if (visualizationIndex < compactVisualizationKinds.size())
                compactVisualizationKinds[visualizationIndex] = true;
            auto moduleProcessor =
                std::make_unique<MegaDSPAudioProcessor>();
            moduleProcessor->prepareToPlay(48000.0, 512);
            moduleProcessor->addModule(
                static_cast<megadsp::ModuleType>(rawType));
            auto editor = std::make_unique<MegaDSPAudioProcessorEditor>(
                *moduleProcessor, 820, 280);
            if (rawType == static_cast<int>(
                               megadsp::ModuleType::compressor))
                expectEquals(
                    editor->cardControlTextForTesting(0, 3),
                    juce::String("150 ms"));
            if (rawType == static_cast<int>(
                               megadsp::ModuleType::delay))
                expect(
                    editor->cardRotaryControlsWorkForTesting(0),
                    "Compact knobs must be visible and use upward/downward "
                    "relative dragging.");
            for (const auto size : {
                     megadsp::ui::WidgetSize::small,
                     megadsp::ui::WidgetSize::medium,
                     megadsp::ui::WidgetSize::large })
            {
                editor->setWidgetSizeForTesting(size);
                expectEquals(
                    editor->cardWidthForTesting(0),
                    megadsp::ui::widgetWidth(
                        size, static_cast<megadsp::ModuleType>(rawType)));
                expect(editor->cardChildrenFitForTesting());
                expect(editor->cardGraphsAvoidControlsForTesting());
                expect(editor->cardPresentsAllActiveControlsForTesting());
            }
            std::array<float, megadsp::controlsPerSlot> retainedValues {};
            for (int control = 0; control < megadsp::controlsPerSlot; ++control)
                retainedValues[static_cast<size_t>(control)] =
                    moduleProcessor->parameters.getRawParameterValue(
                        megadsp::controlParameterId(0, control))->load();
            const auto* preparedModule = moduleProcessor->getRack().activeModuleInstance(0);
            editor->setWidgetSizeForTesting(
                megadsp::ui::WidgetSize::ultraCompact);
            expectEquals(editor->cardHeightForTesting(0), 36);
            expect(editor->cardChildrenFitForTesting());
            expectEquals(editor->activeVisualizationCardCountForTesting(), 0);
            int ultraControls = 0;
            for (auto* child : editor->cardForTesting(0)->getChildren())
                if (child->isVisible())
                    ++ultraControls;
            expect(ultraControls >= 4);
            const auto primaryControls = megadsp::ui::ultraCompactControlOrder(
                static_cast<megadsp::ModuleType>(rawType),
                megadsp::moduleDefaults(static_cast<megadsp::ModuleType>(rawType)),
                true, false);
            for (const auto control : primaryControls)
            {
                const juce::String label = megadsp::controlMetadata(
                    static_cast<megadsp::ModuleType>(rawType), control).label;
                expect(std::any_of(
                    editor->cardForTesting(0)->getChildren().begin(),
                    editor->cardForTesting(0)->getChildren().end(),
                    [&](const auto* child)
                    {
                        return child->isVisible() && child->getTitle() == label;
                    }), "Missing Ultra primary control: " + label);
            }
            expect(editor->cardForTesting(0)->keyPressed(
                juce::KeyPress(juce::KeyPress::returnKey)));
            expect(editor->cardHeightForTesting(0) > 36);
            expect(editor->cardChildrenFitForTesting());
            expect(editor->cardGraphsAvoidControlsForTesting());
            expect(editor->cardPresentsAllActiveControlsForTesting());
            editor->refreshCardsForTesting();
            expect(editor->cardHeightForTesting(0) > 36);
            expect(moduleProcessor->getRack().activeModuleInstance(0) == preparedModule);
            for (int control = 0; control < megadsp::controlsPerSlot; ++control)
                expectWithinAbsoluteError(
                    moduleProcessor->parameters.getRawParameterValue(
                        megadsp::controlParameterId(0, control))->load(),
                    retainedValues[static_cast<size_t>(control)], 0.000001f);
        }
        expect(std::all_of(
            compactVisualizationKinds.begin(),
            compactVisualizationKinds.end(),
            [](bool present) { return present; }),
            "Every compact visualization kind should be assigned.");
        expect(
            megadsp::compactVisualization(
                megadsp::ModuleType::convolutionReverb)
            == megadsp::CompactVisualization::wetSpectrum);
        expect(
            megadsp::compactVisualization(
                megadsp::ModuleType::neuralAmpModeler)
            == megadsp::CompactVisualization::spectral);
        expect(megadsp::compactVisualizationShowsGainReduction(
            megadsp::ModuleType::compressor));
        expect(!megadsp::compactVisualizationShowsGainReduction(
            megadsp::ModuleType::beatPermuter));

        beginTest("Factory compressor values use musical units immediately");
        {
            auto factoryProcessor =
                std::make_unique<MegaDSPAudioProcessor>();
            factoryProcessor->prepareToPlay(48000.0, 512);
            factoryProcessor->loadFactoryPreset(2);
            auto editor = std::make_unique<MegaDSPAudioProcessorEditor>(
                *factoryProcessor, 1100, 720);
            expectEquals(
                editor->cardControlTextForTesting(1, 3),
                juce::String("36 ms"));
        }

#if JUCE_MAC
        beginTest("Native editor repairs a worker-initialized message thread and completes module menus");
        {
            auto rackProcessor = std::make_unique<MegaDSPAudioProcessor>();
            rackProcessor->prepareToPlay(48000.0, 512);
            auto* manager = juce::MessageManager::getInstance();
            std::thread initializer([manager]
            {
                manager->setCurrentThreadAsMessageThread();
            });
            initializer.join();
            expect(!manager->isThisTheMessageThread());
            std::unique_ptr<juce::AudioProcessorEditor> editor(
                rackProcessor->createEditor());
            expect(manager->isThisTheMessageThread(),
                   "Cocoa editor creation must bind JUCE to the macOS main thread.");
            editor->setTopLeftPosition(-10000, -10000);
            editor->addToDesktop(juce::ComponentPeer::windowIgnoresMouseClicks);
            editor->setVisible(true);
            std::function<juce::Button*(juce::Component&)> findInsert =
                [&findInsert](juce::Component& component) -> juce::Button*
                {
                    if (component.getTitle()
                        == "Insert processor at signal-flow position 1")
                        return dynamic_cast<juce::Button*>(&component);
                    for (auto* child : component.getChildren())
                        if (auto* found = findInsert(*child))
                            return found;
                    return nullptr;
                };
            auto* insert = findInsert(*editor);
            expect(insert != nullptr);
            if (insert != nullptr)
            {
                insert->onClick();
                auto* menu = juce::Component::getCurrentlyModalComponent();
                expect(menu != nullptr);
                if (menu != nullptr)
                {
                    auto menuResult = std::make_shared<std::atomic<int>>(-1);
                    juce::ModalComponentManager::getInstance()->attachCallback(
                        menu, juce::ModalCallbackFunction::create([menuResult](int result)
                        {
                            menuResult->store(result);
                        }));
                    std::function<juce::AccessibilityHandler*(
                        juce::Component&, const juce::String&)> findItem =
                        [&findItem](juce::Component& component, const juce::String& title)
                            -> juce::AccessibilityHandler*
                        {
                            if (auto* handler = component.getAccessibilityHandler())
                                if (handler->getTitle() == title)
                                    return handler;
                            for (auto* child : component.getChildren())
                                if (auto* found = findItem(*child, title))
                                    return found;
                            return nullptr;
                        };
                    const auto& definition = megadsp::moduleDefinition(
                        megadsp::ModuleType::compressor);
                    auto* category = findItem(
                        *menu, megadsp::moduleCategoryName(definition.category));
                    expect(category != nullptr);
                    if (category != nullptr)
                        category->getActions().invoke(juce::AccessibilityActionType::press);
                    if (auto* submenu = juce::Component::getCurrentlyModalComponent())
                    {
                        auto* item = findItem(*submenu, definition.displayName);
                        expect(item != nullptr);
                        if (item != nullptr)
                            item->getActions().invoke(juce::AccessibilityActionType::press);
                    }
                    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 500.0;
                    while (menuResult->load() < 0
                           && juce::Time::getMillisecondCounterHiRes() < deadline)
                        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, false);
                    expect(menuResult->load() > 0,
                           "Module menu completion result: " + juce::String(menuResult->load()));
                    expectEquals(rackProcessor->getRack().activeSlotCount(), 1);
                    expect(rackProcessor->getRack().moduleType(0)
                           == megadsp::ModuleType::compressor);
                    expect(juce::Component::getCurrentlyModalComponent() == nullptr);
                    expect(rackProcessor->canUndo());
                }
            }
            manager->setCurrentThreadAsMessageThread();
            juce::PopupMenu::dismissAllActiveMenus();
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.03, false);
        }
#endif

        beginTest("Ultra keeps an add-module button for empty and partially populated racks");
        {
            auto rackProcessor = std::make_unique<MegaDSPAudioProcessor>();
            rackProcessor->prepareToPlay(48000.0, 512);
            MegaDSPAudioProcessorEditor editor(*rackProcessor, 1100, 720);
            editor.setWidgetSizeForTesting(megadsp::ui::WidgetSize::ultraCompact);
            std::function<juce::Button*(juce::Component&)> findAdd =
                [&findAdd](juce::Component& component) -> juce::Button*
                {
                    if (auto* button = dynamic_cast<juce::Button*>(&component))
                        if (button->getButtonText() == "+ ADD MODULE")
                            return button;
                    for (auto* child : component.getChildren())
                        if (auto* found = findAdd(*child))
                            return found;
                    return nullptr;
                };
            auto* add = findAdd(editor);
            expect(add != nullptr);
            if (add != nullptr)
            {
                expect(add->isVisible() && !add->getBounds().isEmpty());
                rackProcessor->addModule(megadsp::ModuleType::equalizer);
                editor.refreshCardsForTesting();
                expect(add->isVisible() && !add->getBounds().isEmpty());
                expect(add->getParentComponent()->getLocalBounds().contains(add->getBounds()));
                for (int slot = 1; slot < megadsp::slotCount; ++slot)
                    rackProcessor->addModule(megadsp::ModuleType::equalizer);
                editor.refreshCardsForTesting();
                expect(!add->isVisible());
            }
        }

        beginTest("Widgets reorder and insert at every boundary");
        {
            auto rackProcessor = std::make_unique<MegaDSPAudioProcessor>();
            rackProcessor->prepareToPlay(48000.0, 512);
            rackProcessor->addModule(megadsp::ModuleType::equalizer);
            rackProcessor->addModule(megadsp::ModuleType::compressor);
            rackProcessor->addModule(megadsp::ModuleType::delay);
            auto editor = std::make_unique<MegaDSPAudioProcessorEditor>(
                *rackProcessor, 1100, 720);

            const std::array<megadsp::ui::WidgetSize, 3> sizes {
                megadsp::ui::WidgetSize::small,
                megadsp::ui::WidgetSize::medium,
                megadsp::ui::WidgetSize::large
            };
            const std::array<int, 3> equalizerWidths { 480, 640, 800 };
            const std::array<int, 3> otherWidths { 300, 440, 640 };
            for (size_t index = 0; index < sizes.size(); ++index)
            {
                editor->setWidgetSizeForTesting(sizes[index]);
                expectEquals(editor->cardWidthForTesting(0), equalizerWidths[index]);
                expectEquals(editor->cardWidthForTesting(1), otherWidths[index]);
                expectEquals(editor->cardWidthForTesting(2), otherWidths[index]);
                for (int slot = 1; slot < 3; ++slot)
                    expectEquals(
                        editor->cardForTesting(slot)->getX()
                            - editor->cardForTesting(slot - 1)->getRight(),
                        megadsp::ui::moduleCardGap + 24);
                const auto* lastCard = editor->cardForTesting(2);
                expectEquals(
                    lastCard->getParentComponent()->getWidth(),
                    lastCard->getRight() + 24 + 8);
                editor->clickChainNavigatorForTesting(2);
                expect(editor->selectedCardVisibleForTesting());
            }
            editor->setWidgetSizeForTesting(megadsp::ui::WidgetSize::small);
            expectEquals(editor->chainInsertButtonCountForTesting(), 4);
            editor->reorderChainNavigatorForTesting(0, 2, true);
            expect(rackProcessor->getRack().moduleType(0)
                   == megadsp::ModuleType::compressor);
            expect(rackProcessor->getRack().moduleType(1)
                   == megadsp::ModuleType::delay);
            expect(rackProcessor->getRack().moduleType(2)
                   == megadsp::ModuleType::equalizer);
            expectEquals(editor->cardWidthForTesting(0), 300);
            expectEquals(editor->cardWidthForTesting(2), 480);
            expect(editor->selectedCardVisibleForTesting());
            expectEquals(editor->selectedCardForTesting(), 2);

            editor->insertModuleFromChainForTesting(
                megadsp::ModuleType::saturator, 0);
            expect(rackProcessor->getRack().moduleType(0)
                   == megadsp::ModuleType::saturator);
            expect(rackProcessor->getRack().moduleType(1)
                   == megadsp::ModuleType::compressor);
            expectEquals(editor->selectedCardForTesting(), 0);
            expectEquals(editor->chainInsertButtonCountForTesting(), 5);

            editor->insertModuleFromChainForTesting(
                megadsp::ModuleType::limiter, 2);
            expect(rackProcessor->getRack().moduleType(2)
                   == megadsp::ModuleType::limiter);
            expect(rackProcessor->getRack().moduleType(3)
                   == megadsp::ModuleType::delay);
            expect(rackProcessor->getRack().moduleType(4)
                   == megadsp::ModuleType::equalizer);
            expectEquals(editor->selectedCardForTesting(), 2);
            expectEquals(editor->chainInsertButtonCountForTesting(), 6);

            expect(!rackProcessor->isSlotBypassed(2));
            editor->doubleClickChainNavigatorForTesting(2);
            expect(rackProcessor->isSlotBypassed(2));
            editor->doubleClickChainNavigatorForTesting(2);
            expect(!rackProcessor->isSlotBypassed(2));

            editor->insertModuleFromChainForTesting(
                megadsp::ModuleType::algorithmicReverb, 5);
            expect(rackProcessor->getRack().moduleType(5)
                   == megadsp::ModuleType::algorithmicReverb);
            expectEquals(editor->selectedCardForTesting(), 5);
            expectEquals(editor->chainInsertButtonCountForTesting(), 7);

            editor->removeModuleFromChainForTesting(1);
            expectEquals(editor->cardCountForTesting(), 5);
            expect(rackProcessor->getRack().moduleType(0)
                   == megadsp::ModuleType::saturator);
            expect(rackProcessor->getRack().moduleType(1)
                   == megadsp::ModuleType::limiter);
            expect(rackProcessor->getRack().moduleType(2)
                   == megadsp::ModuleType::delay);
            expectEquals(editor->selectedCardForTesting(), 1);
            expectEquals(editor->chainInsertButtonCountForTesting(), 6);
        }

        beginTest("Horizontal rack follows topology, reorder, and saved selection");
        {
            auto rackProcessor = std::make_unique<MegaDSPAudioProcessor>();
            rackProcessor->prepareToPlay(48000.0, 512);
            rackProcessor->addModule(megadsp::ModuleType::equalizer);
            rackProcessor->addModule(megadsp::ModuleType::compressor);
            rackProcessor->setSelectedTab(0);
            auto editor = std::make_unique<MegaDSPAudioProcessorEditor>(
                *rackProcessor, 1100, 720);

            editor->reorderCardForTesting(0, 1);
            expect(rackProcessor->getRack().moduleType(0)
                   == megadsp::ModuleType::compressor);
            expect(rackProcessor->getRack().moduleType(1)
                   == megadsp::ModuleType::equalizer);
            expectEquals(editor->selectedCardForTesting(), 1);
            expectEquals(
                editor->activeVisualizationCardCountForTesting(), 0);

            rackProcessor->addModule(megadsp::ModuleType::delay);
            editor->refreshCardsForTesting();
            expectEquals(editor->cardCountForTesting(), 3);
            expectEquals(editor->selectedCardForTesting(), 2);
            expectEquals(rackProcessor->getSelectedTab(), 2);

            rackProcessor->setSelectedTab(1);
            auto buildDirectory =
                juce::File::getCurrentWorkingDirectory();
            if (!buildDirectory.getChildFile("CMakeCache.txt").existsAsFile())
                buildDirectory = buildDirectory.getChildFile("build");
            const auto presetFile = buildDirectory.getChildFile(
                "megaDSP-rack-workflow-test.megadsp");
            presetFile.deleteFile();
            expect(rackProcessor->savePreset(presetFile).wasOk());
            rackProcessor->loadFactoryPreset(4);
            editor->refreshCardsForTesting();
            expectEquals(
                editor->cardCountForTesting(),
                rackProcessor->getRack().activeSlotCount());
            expectEquals(editor->selectedCardForTesting(), 1);
            expectEquals(
                editor->activeVisualizationCardCountForTesting(), 0);

            expect(rackProcessor->loadPreset(presetFile).wasOk());
            editor->refreshCardsForTesting();
            expectEquals(editor->cardCountForTesting(), 3);
            expectEquals(editor->selectedCardForTesting(), 1);
            expectEquals(rackProcessor->getSelectedTab(), 1);
            expect(presetFile.deleteFile());
        }

        beginTest("Horizontal rack follows snapshot recall and asset drop types");
        {
            auto rackProcessor = std::make_unique<MegaDSPAudioProcessor>();
            rackProcessor->prepareToPlay(48000.0, 512);
            rackProcessor->addModule(
                megadsp::ModuleType::convolutionReverb);
            rackProcessor->addModule(
                megadsp::ModuleType::neuralAmpModeler);
            rackProcessor->setSelectedTab(1);
            auto editor = std::make_unique<MegaDSPAudioProcessorEditor>(
                *rackProcessor, 1100, 720);

            expect(editor->cardAcceptsAssetDropForTesting(
                0, "room.WAV"));
            expect(!editor->cardAcceptsAssetDropForTesting(
                0, "amp.nam"));
            expect(editor->cardAcceptsAssetDropForTesting(
                1, "amp.NAM"));
            expect(!editor->cardAcceptsAssetDropForTesting(
                1, "room.flac"));
            expect(editor->cardHasAssetSearchForTesting(0));
            expect(editor->cardHasAssetSearchForTesting(1));
            expect(editor->cardChildrenFitForTesting());

            rackProcessor->storeSnapshot(1, false);
            rackProcessor->removeSlot(1);
            editor->refreshCardsForTesting();
            expectEquals(editor->cardCountForTesting(), 1);
            rackProcessor->recallSnapshot(0);
            rackProcessor->flushPendingUpdatesForTesting();
            editor->refreshCardsForTesting();
            expectEquals(editor->cardCountForTesting(), 2);
            expectEquals(
                editor->activeVisualizationCardCountForTesting(), 0);
            expectEquals(
                editor->selectedCardForTesting(),
                rackProcessor->getSelectedTab());
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

        beginTest("Compact spectra use a perceptual frequency axis");
        {
            expectEquals(
                static_cast<int>(
                    megadsp::ui::EffectGraph::spectrumBinForProportion(
                        0.0f, 48000.0)),
                1);
            expectEquals(
                static_cast<int>(
                    megadsp::ui::EffectGraph::spectrumBinForProportion(
                        1.0f / 3.0f, 48000.0)),
                4);
            expectEquals(
                static_cast<int>(
                    megadsp::ui::EffectGraph::spectrumBinForProportion(
                        2.0f / 3.0f, 48000.0)),
                43);
            expectEquals(
                static_cast<int>(
                    megadsp::ui::EffectGraph::spectrumBinForProportion(
                        1.0f, 48000.0)),
                427);
        }

        beginTest("Fresh dynamics histories represent silence");
        {
            MegaDSPAudioProcessor dynamicsProcessor;
            dynamicsProcessor.prepareToPlay(48000.0, 512);
            setType(
                dynamicsProcessor, 0,
                megadsp::ModuleType::compressor);
            dynamicsProcessor.flushPendingUpdatesForTesting();
            megadsp::ui::EffectGraph graph(
                dynamicsProcessor, 0,
                megadsp::ModuleType::compressor);
            graph.setBounds(0, 0, 384, 120);
            graph.setVisualizationActive(true);
            graph.refreshVisualizationForTesting();
            expect(std::all_of(
                graph.inputLevelData().begin(),
                graph.inputLevelData().end(),
                [](float value)
                {
                    return std::abs(value + 60.0f) < 0.000001f;
                }));
            expect(std::all_of(
                graph.outputLevelData().begin(),
                graph.outputLevelData().end(),
                [](float value)
                {
                    return std::abs(value + 60.0f) < 0.000001f;
                }));
            graph.setCompactCompressorThresholdFromYForTesting(0.0f);
            expectWithinAbsoluteError(
                graph.value(0), 1.0f, 0.000001f);
            graph.setCompactCompressorThresholdFromYForTesting(
                static_cast<float>(graph.getHeight()));
            expectWithinAbsoluteError(
                graph.value(0), 0.0f, 0.000001f);
        }

        beginTest("Compact Equalizer nodes directly edit frequency and gain");
        {
            MegaDSPAudioProcessor equalizerProcessor;
            equalizerProcessor.prepareToPlay(48000.0, 512);
            setType(
                equalizerProcessor, 0,
                megadsp::ModuleType::equalizer);
            equalizerProcessor.flushPendingUpdatesForTesting();
            megadsp::ui::EffectGraph graph(
                equalizerProcessor, 0,
                megadsp::ModuleType::equalizer);
            graph.setBounds(0, 0, 384, 120);
            const auto compactPlot =
                graph.compactEqualizerPlotAreaForTesting();
            const auto oneKhzX = compactPlot.getX()
                + std::log(1000.0f / 20.0f) / std::log(1000.0f)
                    * compactPlot.getWidth();
            graph.setCompactEqualizerBandFromPointForTesting(
                1, { oneKhzX, compactPlot.getCentreY() });
            const auto expectedOneKhz = std::log(1000.0f / 150.0f)
                / std::log(7000.0f / 150.0f);
            expectWithinAbsoluteError(
                graph.value(3), expectedOneKhz, 0.000001f,
                "EQ node positions must follow the displayed frequency axis.");
            graph.setValue(4, 1.0f);
            expect(
                graph.compactEqualizerResponseDbForTesting(1000.0f)
                    > 15.0f,
                "The compact graph must expose the audible EQ response.");
            graph.setValue(4, 0.5f);
            expectWithinAbsoluteError(
                graph.compactEqualizerResponseDbForTesting(1000.0f),
                0.0f, 0.01f,
                "A neutral compact EQ must draw a flat response.");
            graph.setCompactEqualizerBandFromPointForTesting(
                1, { 10000.0f, -100.0f });
            expectWithinAbsoluteError(
                graph.value(3), 1.0f, 0.000001f);
            expectWithinAbsoluteError(
                graph.value(4), 1.0f, 0.000001f);
            graph.setCompactEqualizerBandFromPointForTesting(
                1, { -100.0f, 10000.0f });
            expectWithinAbsoluteError(
                graph.value(3), 0.0f, 0.000001f);
            expectWithinAbsoluteError(
                graph.value(4), 0.0f, 0.000001f);
            graph.setCompactEqualizerOutputFromYForTesting(-100.0f);
            expectWithinAbsoluteError(
                graph.value(9), 1.0f, 0.000001f);
            graph.setCompactEqualizerOutputFromYForTesting(10000.0f);
            expectWithinAbsoluteError(
                graph.value(9), 0.0f, 0.000001f);
            const auto previousLowShape = graph.value(10);
            expect(graph.keyPressed(juce::KeyPress(
                juce::KeyPress::leftKey)));
            expect(graph.keyPressed(juce::KeyPress(
                juce::KeyPress::returnKey)));
            expect(std::abs(graph.value(10) - previousLowShape)
                   > 0.1f);
        }

        beginTest("Compact Delay directly edits timing and feedback");
        {
            MegaDSPAudioProcessor delayProcessor;
            delayProcessor.prepareToPlay(48000.0, 512);
            setType(
                delayProcessor, 0, megadsp::ModuleType::delay);
            delayProcessor.flushPendingUpdatesForTesting();
            megadsp::ui::EffectGraph graph(
                delayProcessor, 0, megadsp::ModuleType::delay);
            graph.setBounds(0, 0, 384, 120);
            graph.setValue(2, 0.0f);
            graph.setCompactDelayFromPointForTesting(
                { 10000.0f, -100.0f });
            expectWithinAbsoluteError(
                graph.value(0), 1.0f, 0.000001f);
            expectWithinAbsoluteError(
                graph.value(3), 1.0f, 0.000001f);
            graph.setCompactDelayFromPointForTesting(
                { -100.0f, 10000.0f });
            expectWithinAbsoluteError(
                graph.value(0), 0.0f, 0.000001f);
            expectWithinAbsoluteError(
                graph.value(3), 0.0f, 0.000001f);
            expect(graph.keyPressed(juce::KeyPress(
                juce::KeyPress::rightKey)));
            expect(graph.value(0) > 0.0f);
            juce::Image image(
                juce::Image::ARGB, graph.getWidth(), graph.getHeight(), true);
            juce::Graphics graphics(image);
            graph.paint(graphics);
        }

        beginTest("NAM library list does not request DSP telemetry");
        {
            MegaDSPAudioProcessor namProcessor;
            namProcessor.prepareToPlay(48000.0, 512);
            setType(
                namProcessor, 0,
                megadsp::ModuleType::neuralAmpModeler);
            namProcessor.flushPendingUpdatesForTesting();
            megadsp::ui::EffectGraph graph(
                namProcessor, 0,
                megadsp::ModuleType::neuralAmpModeler);
            graph.setVisualizationActive(true);
            expect(
                !namProcessor.getVisualizationData().isSlotActive(0),
                "The compact NAM library must not capture unused DSP data.");
        }

        beginTest("Compact Guitar Tuner keeps one Precision display");
        {
            MegaDSPAudioProcessor tunerProcessor;
            tunerProcessor.prepareToPlay(48000.0, 512);
            setType(
                tunerProcessor, 0, megadsp::ModuleType::guitarTuner);
            tunerProcessor.flushPendingUpdatesForTesting();
            megadsp::ui::EffectGraph graph(
                tunerProcessor, 0, megadsp::ModuleType::guitarTuner);
            graph.setBounds(0, 0, 384, 120);
            juce::Image image(
                juce::Image::ARGB,
                graph.getWidth(), graph.getHeight(), true);
            juce::Graphics graphics(image);
            graph.paint(graphics);
            expect(image.getPixelAt(192, 60).getAlpha() > 0);
            expect(!graph.keyPressed(juce::KeyPress(
                juce::KeyPress::rightKey)));
            expect(!graph.keyPressed(juce::KeyPress(
                juce::KeyPress::upKey)));
        }

        beginTest("Every active graph owns visualization capture");
        {
            MegaDSPAudioProcessor graphProcessor;
            graphProcessor.prepareToPlay(48000.0, 512);
            setType(graphProcessor, 0, megadsp::ModuleType::equalizer);
            setType(graphProcessor, 1, megadsp::ModuleType::compressor);
            graphProcessor.flushPendingUpdatesForTesting();
            megadsp::ui::EffectGraph first(
                graphProcessor, 0, megadsp::ModuleType::equalizer);
            megadsp::ui::EffectGraph second(
                graphProcessor, 1, megadsp::ModuleType::compressor);
            expect(!first.isVisualizationActive());
            expect(!second.isVisualizationActive());

            first.setVisualizationActive(true);
            second.setVisualizationActive(true);
            expect(first.isVisualizationActive());
            expect(second.isVisualizationActive());
            expect(graphProcessor.getVisualizationData().isSlotActive(0));
            expect(graphProcessor.getVisualizationData().isSlotActive(1));
            graphProcessor.getVisualizationData().clearActiveSlots();
            first.setVisualizationActive(true);
            second.setVisualizationActive(true);
            expect(graphProcessor.getVisualizationData().isSlotActive(0));
            expect(graphProcessor.getVisualizationData().isSlotActive(1));
            second.setVisualizationActive(false);
            expect(graphProcessor.getVisualizationData().isSlotActive(0));
            expect(!graphProcessor.getVisualizationData().isSlotActive(1));
        }

        beginTest(
            "REAPER embedded UI adapts to TCP and MCP and edits safely");
        {
            MegaDSPAudioProcessor embeddedProcessor;
            embeddedProcessor.prepareToPlay(48000.0, 512);
            embeddedProcessor.addModule(megadsp::ModuleType::delay);
            embeddedProcessor.addModule(megadsp::ModuleType::compressor);
            embeddedProcessor.addModule(megadsp::ModuleType::equalizer);
            auto* embedded = dynamic_cast<megadsp::ReaperEmbeddedUI*>(
                embeddedProcessor.getVST3ClientExtensions());
            expect(embedded != nullptr);
            expectEquals(
                static_cast<int>(embedded->embed_message(
                    REAPER_FXEMBED_WM_IS_SUPPORTED, 0, 0)),
                1);

            void* queried = nullptr;
            expectEquals(
                embedded->queryIEditController(
                    reaper::IReaperUIEmbedInterface::iid, &queried),
                static_cast<int32_t>(Steinberg::kResultOk));
            expect(queried == static_cast<
                reaper::IReaperUIEmbedInterface*>(embedded));
            if (queried != nullptr)
                static_cast<reaper::IReaperUIEmbedInterface*>(
                    queried)->release();

            void* gainReductionQuery = nullptr;
            expectEquals(
                embedded->queryIEditController(
                    Presonus::IGainReductionInfo::iid,
                    &gainReductionQuery),
                static_cast<int32_t>(Steinberg::kResultOk));
            expect(gainReductionQuery == static_cast<
                Presonus::IGainReductionInfo*>(embedded));
            auto* gainReduction = static_cast<
                Presonus::IGainReductionInfo*>(gainReductionQuery);
            expectWithinAbsoluteError(
                gainReduction->getGainReductionValueInDb(),
                0.0, 0.000001);

            const auto tcpTiles =
                embedded->tileBoundsForTesting(480, 90, 1);
            expect(tcpTiles[0].getX() < tcpTiles[1].getX());
            expectEquals(tcpTiles[0].getY(), tcpTiles[1].getY());
            const auto mcpTiles =
                embedded->tileBoundsForTesting(110, 300, 2);
            expect(mcpTiles[0].getY() < mcpTiles[1].getY());
            expectEquals(mcpTiles[0].getX(), mcpTiles[1].getX());
            for (int slot = 0; slot < 3; ++slot)
            {
                expect(!tcpTiles[static_cast<size_t>(slot)].isEmpty());
                expect(!mcpTiles[static_cast<size_t>(slot)].isEmpty());
                expect(
                    embedded->primaryControlForTesting(slot) >= 0);
            }

            const auto tcpImage =
                embedded->renderForTesting(480, 90, 1);
            const auto mcpImage =
                embedded->renderForTesting(110, 300, 2);
            expect(tcpImage.isValid() && mcpImage.isValid());
            expect(
                tcpImage.getPixelAt(
                    tcpTiles[0].getCentreX(),
                    tcpTiles[0].getCentreY())
                    != tcpImage.getPixelAt(
                        tcpTiles[1].getCentreX(),
                        tcpTiles[1].getCentreY()),
                "Module-category tiles should remain visually distinct.");

            const auto retinaMcpTiles =
                embedded->tileBoundsForTesting(220, 600, 2, 2.0f);
            for (int slot = 0; slot < 3; ++slot)
            {
                const auto normal =
                    mcpTiles[static_cast<size_t>(slot)];
                const auto retina =
                    retinaMcpTiles[static_cast<size_t>(slot)];
                expectWithinAbsoluteError(
                    retina.getCentreX(),
                    normal.getCentreX() * 2, 2);
                expectWithinAbsoluteError(
                    retina.getCentreY(),
                    normal.getCentreY() * 2, 2);
                expectWithinAbsoluteError(
                    retina.getWidth(), normal.getWidth() * 2, 2);
                expectWithinAbsoluteError(
                    retina.getHeight(), normal.getHeight() * 2, 2);
            }
            expect(
                embedded->renderForTesting(
                    220, 600, 2, 2.0f).isValid(),
                "Retina MCP rendering should use scaled geometry.");

            EmbeddedBitmap bitmap(480, 90);
            reaper::REAPER_FXEMBED_DrawInfo paintInfo {};
            paintInfo.context = 1;
            paintInfo.width = bitmap.getWidth();
            paintInfo.height = bitmap.getHeight();
            expectEquals(
                static_cast<int>(embedded->embed_message(
                    REAPER_FXEMBED_WM_PAINT,
                    reinterpret_cast<Steinberg::TPtrInt>(&bitmap),
                    reinterpret_cast<Steinberg::TPtrInt>(&paintInfo))),
                1);
            expect(std::any_of(
                bitmap.pixels.begin(), bitmap.pixels.end(),
                [](unsigned int pixel) { return pixel != 0u; }));

            EmbeddedBitmap retinaBitmap(220, 600, 512);
            reaper::REAPER_FXEMBED_DrawInfo retinaPaintInfo {};
            retinaPaintInfo.context = 2;
            retinaPaintInfo.dpi = 512;
            retinaPaintInfo.width = retinaBitmap.getWidth();
            retinaPaintInfo.height = retinaBitmap.getHeight();
            retinaPaintInfo.flags =
                REAPER_FXEMBED_DRAWINFO_FLAG_IS_RETINA;
            expectEquals(
                static_cast<int>(embedded->embed_message(
                    REAPER_FXEMBED_WM_PAINT,
                    reinterpret_cast<Steinberg::TPtrInt>(
                        &retinaBitmap),
                    reinterpret_cast<Steinberg::TPtrInt>(
                        &retinaPaintInfo))),
                1);
            expect(std::any_of(
                retinaBitmap.pixels.begin(),
                retinaBitmap.pixels.end(),
                [](unsigned int pixel) { return pixel != 0u; }));

            juce::AudioBuffer<float> compressedSignal(2, 512);
            juce::MidiBuffer embeddedMidi;
            for (int block = 0; block < 8; ++block)
            {
                for (int channel = 0;
                     channel < compressedSignal.getNumChannels();
                     ++channel)
                    juce::FloatVectorOperations::fill(
                        compressedSignal.getWritePointer(channel),
                        1.0f, compressedSignal.getNumSamples());
                embeddedProcessor.processBlock(
                    compressedSignal, embeddedMidi);
            }
            expect(
                gainReduction->getGainReductionValueInDb() < -0.1,
                "REAPER should receive measured compressor reduction.");
            const auto reductionImage =
                embedded->renderForTesting(110, 300, 2);
            const auto compressorTile = mcpTiles[1];
            const auto meterY = compressorTile.getBottom() - 3;
            expect(
                reductionImage.getPixelAt(
                    compressorTile.getX()
                        + compressorTile.getWidth() / 8,
                    meterY)
                    != reductionImage.getPixelAt(
                        compressorTile.getX()
                            + compressorTile.getWidth() * 7 / 8,
                        meterY),
                "Gain reduction should fill from the right edge.");
            setBypass(embeddedProcessor, 1, true);
            expectWithinAbsoluteError(
                gainReduction->getGainReductionValueInDb(),
                0.0, 0.000001);
            setBypass(embeddedProcessor, 1, false);

            expect(embedded->dragPrimaryForTesting(0, -60));
            expect(embedded->dragPrimaryForTesting(0, 60));

            reaper::REAPER_FXEMBED_SizeHints hints {};
            hints.flags = 2;
            expectEquals(
                static_cast<int>(embedded->embed_message(
                    REAPER_FXEMBED_WM_GETMINMAXINFO, 0,
                    reinterpret_cast<Steinberg::TPtrInt>(&hints))),
                1);
            expect(hints.min_height >= 80);
            expect(hints.preferred_aspect < 65536);

            reaper::REAPER_FXEMBED_DrawInfo mouse {};
            mouse.context = 2;
            mouse.width = 110;
            mouse.height = 300;
            mouse.mouse_x = mcpTiles[0].getRight() - 8;
            mouse.mouse_y = mcpTiles[0].getY() + 8;
            expect(!embeddedProcessor.isSlotBypassed(0));
            embedded->embed_message(
                REAPER_FXEMBED_WM_LBUTTONDOWN, 0,
                reinterpret_cast<Steinberg::TPtrInt>(&mouse));
            expect(embeddedProcessor.isSlotBypassed(0));
            embedded->embed_message(
                REAPER_FXEMBED_WM_LBUTTONUP, 0,
                reinterpret_cast<Steinberg::TPtrInt>(&mouse));
            mouse.mouse_x = mcpTiles[1].getCentreX();
            mouse.mouse_y = mcpTiles[1].getCentreY();
            expectEquals(
                static_cast<int>(embedded->embed_message(
                    REAPER_FXEMBED_WM_HITTEST,
                    REAPER_FXEMBED_WM_LBUTTONDOWN,
                    reinterpret_cast<Steinberg::TPtrInt>(&mouse))),
                0);
            embedded->embed_message(
                REAPER_FXEMBED_WM_LBUTTONDOWN, 0,
                reinterpret_cast<Steinberg::TPtrInt>(&mouse));
            embedded->embed_message(
                REAPER_FXEMBED_WM_LBUTTONUP, 0,
                reinterpret_cast<Steinberg::TPtrInt>(&mouse));
            expectEquals(embeddedProcessor.getSelectedTab(), 1);
            mouse.mouse_x = mcpTiles[2].getCentreX();
            mouse.mouse_y = mcpTiles[2].getY() + 8;
            expectEquals(
                static_cast<int>(embedded->embed_message(
                    REAPER_FXEMBED_WM_HITTEST,
                    REAPER_FXEMBED_WM_LBUTTONDOWN,
                    reinterpret_cast<Steinberg::TPtrInt>(&mouse))),
                1);
            expectEquals(embeddedProcessor.getSelectedTab(), 2);
            mouse.mouse_y = mcpTiles[2].getCentreY();
            expectEquals(
                static_cast<int>(embedded->embed_message(
                    REAPER_FXEMBED_WM_HITTEST,
                    REAPER_FXEMBED_WM_LBUTTONDBLCLK,
                    reinterpret_cast<Steinberg::TPtrInt>(&mouse))),
                1);
            expectEquals(
                static_cast<int>(embedded->embed_message(
                    REAPER_FXEMBED_WM_HITTEST,
                    REAPER_FXEMBED_WM_RBUTTONDOWN,
                    reinterpret_cast<Steinberg::TPtrInt>(&mouse))),
                1);

            while (embeddedProcessor.getRack().activeSlotCount()
                   < megadsp::slotCount)
                embeddedProcessor.addModule(
                    megadsp::ModuleType::delay);
            const auto fullChainTiles =
                embedded->tileBoundsForTesting(90, 144, 2);
            for (int slot = 0; slot < megadsp::slotCount; ++slot)
            {
                const auto tile =
                    fullChainTiles[static_cast<size_t>(slot)];
                expect(!tile.isEmpty());
                if (slot > 0)
                    expect(
                        !tile.intersects(
                            fullChainTiles[
                                static_cast<size_t>(slot - 1)]));
            }
            expect(
                embedded->renderForTesting(90, 144, 2).isValid(),
                "All eight modules must remain renderable in a small MCP.");
            if (gainReduction != nullptr)
                gainReduction->release();
        }

        beginTest("Envelope Modulator graph edits a reversible level-to-speed window");
        {
            using Module = megadsp::EnvelopeModulatorModule;
            MegaDSPAudioProcessor modulationProcessor;
            modulationProcessor.prepareToPlay(48000.0, 256);
            modulationProcessor.addModule(megadsp::ModuleType::envelopeModulator);
            expect(dynamic_cast<const Module*>(
                modulationProcessor.getRack().activeModuleInstance(0)) != nullptr);
            megadsp::ui::EffectGraph graph(
                modulationProcessor, 0, megadsp::ModuleType::envelopeModulator);
            for (const auto width : { 280, 420, 620 })
            {
                graph.setBounds(0, 0, width, 190);
                const auto plot = graph.compactEnvelopePlotAreaForTesting();
                graph.setCompactEnvelopeNodeForTesting(
                    0, { plot.getX() + plot.getWidth() * 0.25f,
                         plot.getBottom() - plot.getHeight() * 0.3f });
                graph.setCompactEnvelopeNodeForTesting(
                    1, { plot.getX() + plot.getWidth() * 0.8f,
                         plot.getBottom() - plot.getHeight() * 0.85f });
                expectWithinAbsoluteError(graph.value(Module::quietLevelControl), 0.25f, 0.0001f);
                expectWithinAbsoluteError(graph.value(Module::loudLevelControl), 0.8f, 0.0001f);
                expectWithinAbsoluteError(graph.value(Module::quietSpeedControl), 0.3f, 0.0001f);
                expectWithinAbsoluteError(graph.value(Module::loudSpeedControl), 0.85f, 0.0001f);
                graph.reverseCompactEnvelopeSpeedsForTesting();
                expectWithinAbsoluteError(graph.value(Module::quietSpeedControl), 0.85f, 0.0001f);
                expectWithinAbsoluteError(graph.value(Module::loudSpeedControl), 0.3f, 0.0001f);
                expect(modulationProcessor.undo());
                expectWithinAbsoluteError(graph.value(Module::quietSpeedControl), 0.3f, 0.0001f);
                expectWithinAbsoluteError(graph.value(Module::loudSpeedControl), 0.85f, 0.0001f);
            }
            graph.setValue(Module::tremoloDepthControl, 0.37f);
            graph.setValue(Module::pitchDepthControl, 0.42f);
            expect(graph.keyPressed(juce::KeyPress('m')));
            expect(graph.keyPressed(juce::KeyPress('m')));
            expectEquals(megadsp::discreteIndex(graph.value(Module::modeControl), 3), 2);
            expectWithinAbsoluteError(graph.value(Module::tremoloDepthControl), 0.37f, 0.0001f);
            expectWithinAbsoluteError(graph.value(Module::pitchDepthControl), 0.42f, 0.0001f);
            expect(!graph.keyPressed(juce::KeyPress(
                'z', juce::ModifierKeys::commandModifier, 'z')));
            const auto defaults = megadsp::moduleDefaults(megadsp::ModuleType::envelopeModulator);
            expect(graph.keyPressed(juce::KeyPress(juce::KeyPress::homeKey)));
            expectWithinAbsoluteError(
                graph.value(Module::quietSpeedControl), defaults[Module::quietSpeedControl], 0.0001f);
            expectWithinAbsoluteError(
                graph.value(Module::quietLevelControl), defaults[Module::quietLevelControl], 0.0001f);
            graph.setCompactEnvelopeNodeForTesting(
                0, graph.compactEnvelopePlotAreaForTesting().getTopRight());
            expect(graph.value(Module::quietLevelControl)
                       <= graph.value(Module::loudLevelControl) - 1.0f / 72.0f + 0.000001f);

            MegaDSPAudioProcessorEditor editor(modulationProcessor, 1100, 720);
            for (const auto size : {
                     megadsp::ui::WidgetSize::small,
                     megadsp::ui::WidgetSize::medium,
                     megadsp::ui::WidgetSize::large })
            {
                editor.setWidgetSizeForTesting(size);
                editor.refreshCardsForTesting();
                expect(editor.cardPresentsAllActiveControlsForTesting());
                expect(editor.cardChildrenFitForTesting());
                expect(editor.cardGraphsAvoidControlsForTesting());
                graph.setSize(editor.cardGraphWidthForTesting(0),
                              editor.cardGraphHeightForTesting(0));
                expect(graph.compactEnvelopePlotAreaForTesting().getHeight() >= 40.0f);
            }
            juce::MemoryBlock state;
            modulationProcessor.getStateInformation(state);
            MegaDSPAudioProcessor restored;
            restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
            restored.flushPendingUpdatesForTesting();
            expectEquals(static_cast<int>(restored.getRack().moduleType(0)), 60);
            expectWithinAbsoluteError(
                restored.parameters.getRawParameterValue(
                    megadsp::controlParameterId(0, Module::pitchDepthControl))->load(),
                0.42f, 0.0001f);
        }

        beginTest("Telemetry-rich compact views render measured state");
        {
            constexpr std::array telemetryTypes {
                megadsp::ModuleType::delay,
                megadsp::ModuleType::autoWah,
                megadsp::ModuleType::stageVocal,
                megadsp::ModuleType::midnightCurrent,
                megadsp::ModuleType::resonantSculpture,
                megadsp::ModuleType::guitarTuner,
                megadsp::ModuleType::envelopeModulator,
                megadsp::ModuleType::analogTape,
                megadsp::ModuleType::vinylSimulator,
                megadsp::ModuleType::chaosField
            };
            for (const auto type : telemetryTypes)
            {
                MegaDSPAudioProcessor telemetryProcessor;
                telemetryProcessor.prepareToPlay(48000.0, 512);
                setType(telemetryProcessor, 0, type);
                telemetryProcessor.flushPendingUpdatesForTesting();
                megadsp::ui::EffectGraph graph(
                    telemetryProcessor, 0, type);
                graph.setBounds(0, 0, 384, 120);
                graph.setVisualizationActive(true);
                juce::MidiBuffer telemetryMidi;
                auto phase = 0.0f;
                for (int blockIndex = 0; blockIndex < 24; ++blockIndex)
                {
                    juce::AudioBuffer<float> block(2, 512);
                    for (int sample = 0; sample < block.getNumSamples();
                         ++sample)
                    {
                        const auto value = 0.2f * std::sin(phase);
                        phase += juce::MathConstants<float>::twoPi
                            * 220.0f / 48000.0f;
                        for (int channel = 0;
                             channel < block.getNumChannels(); ++channel)
                            block.setSample(channel, sample, value);
                    }
                    telemetryProcessor.processBlock(block, telemetryMidi);
                }
                megadsp::ContinuousTelemetrySnapshot telemetry;
                if (type == megadsp::ModuleType::guitarTuner)
                {
                    const auto timeout =
                        juce::Time::getMillisecondCounterHiRes() + 2000.0;
                    do
                    {
                        juce::Thread::sleep(10);
                        telemetryProcessor.readContinuousTelemetry(
                            0, telemetry);
                    }
                    while (telemetry.sequence == 0
                           && juce::Time::getMillisecondCounterHiRes()
                                  < timeout);
                }
                expect(
                    telemetryProcessor.readContinuousTelemetry(
                        0, telemetry),
                    megadsp::moduleDefinition(type).displayName);
                expect(telemetry.sequence != 0);
                graph.refreshVisualizationForTesting();
                juce::Image image(
                    juce::Image::ARGB, 384, 120, true);
                juce::Graphics graphics(image);
                graph.paint(graphics);
                expect(image.getPixelAt(192, 60).getAlpha() > 0);
            }
        }

        beginTest("Bypassed slots suspend DSP and visualization capture");
        {
            MegaDSPAudioProcessor suspendedProcessor;
            suspendedProcessor.prepareToPlay(48000.0, 512);
            setType(
                suspendedProcessor, 0, megadsp::ModuleType::equalizer);
            suspendedProcessor.flushPendingUpdatesForTesting();
            suspendedProcessor.getVisualizationData().setSlotActive(0, true);
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
            for (int channel = 0; channel < bypassed.getNumChannels();
                 ++channel)
                juce::FloatVectorOperations::fill(
                    bypassed.getWritePointer(channel), 0.25f,
                    bypassed.getNumSamples());
            suspendedProcessor.processBlock(bypassed, bypassMidi);
            expect(!suspendedProcessor.getRack().slotDspSuspended(0));
            const auto capturedAfter = suspendedProcessor
                .getVisualizationData().slotData(0).input.copyLatest(
                    bypassCapture);
            expect(capturedAfter > capturedBefore);
            expectWithinAbsoluteError(
                bypassCapture.back(), 0.25f, 0.000001f);
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

        beginTest("Unsafe rack output is rejected before host handoff");
        {
            MegaDSPAudioProcessor safetyProcessor;
            safetyProcessor.prepareToPlay(48000.0, 512);
            juce::AudioBuffer<float> unsafe(
                safetyProcessor.getTotalNumOutputChannels(), 512);
            for (int channel = 0; channel < unsafe.getNumChannels();
                 ++channel)
                juce::FloatVectorOperations::fill(
                    unsafe.getWritePointer(channel),
                    std::numeric_limits<float>::infinity(),
                    unsafe.getNumSamples());
            safetyProcessor.processBlock(unsafe, midi);
            for (int channel = 0; channel < unsafe.getNumChannels();
                 ++channel)
                expectWithinAbsoluteError(
                    unsafe.getMagnitude(
                        channel, 0, unsafe.getNumSamples()),
                    0.0f, 0.000001f);
        }

        beginTest("Full plugin presets round trip across standalone and plugin wrapper contexts");
        {
            const auto directory = MegaDSPAudioProcessor::presetDirectory()
                .getNonexistentChildFile("portable-preset-test", {}, false);
            expect(directory.createDirectory().wasOk());
            const auto file = directory.getChildFile("Full rack.megadsp");
            const std::array wrappers {
                juce::AudioProcessor::wrapperType_Standalone,
                juce::AudioProcessor::wrapperType_AudioUnit,
                juce::AudioProcessor::wrapperType_VST3,
                juce::AudioProcessor::wrapperType_Undefined // CLAP's JUCE adapter.
            };
            const auto create = [](juce::AudioProcessor::WrapperType wrapper)
            {
                juce::AudioProcessor::setTypeOfNextNewPlugin(wrapper);
                auto createdProcessor = std::make_unique<MegaDSPAudioProcessor>();
                juce::AudioProcessor::setTypeOfNextNewPlugin(
                    juce::AudioProcessor::wrapperType_Undefined);
                createdProcessor->prepareToPlay(48000.0, 64);
                return createdProcessor;
            };
            for (const auto sourceWrapper : wrappers)
            {
                auto source = create(sourceWrapper);
                source->setInstanceName("Whole Plugin");
                source->setBackgroundThemeIndex(12);
                source->addModule(megadsp::ModuleType::equalizer);
                source->addModule(megadsp::ModuleType::delay);
                source->parameters.getParameter("global.input")->setValueNotifyingHost(0.73f);
                source->parameters.getParameter("global.output")->setValueNotifyingHost(0.41f);
                auto eq = source->reEqSettings(0);
                eq.lookaheadMs = 5.0f;
                eq.bands[12].enabled = 1;
                eq.bands[12].frequency = 1750.0f;
                eq.bands[12].gainDb = 7.0f;
                source->setReEqSettings(0, eq);
                source->storeSnapshot(1, false);
                source->flushPendingUpdatesForTesting();
                auto alternate = eq;
                alternate.bands[12].gainDb = -4.0f;
                source->setReEqSettings(0, alternate);
                source->parameters.getParameter(megadsp::slotParameterId(1, "bypass"))
                    ->setValueNotifyingHost(0.0f);
                source->recallSnapshot(0);
                source->flushPendingUpdatesForTesting();
                source->setSelectedTab(1);
                source->setLiveSafeSnapshotsEnabled(true);
                source->rememberEditorSize(1200, 800);
                source->parameters.getParameter(megadsp::slotParameterId(1, "bypass"))
                    ->setValueNotifyingHost(1.0f);
                expect(source->isSlotBypassed(1), "Source preset must contain a bypassed delay.");
                expect(source->savePreset(file).wasOk());
                juce::MemoryBlock expectedData;
                source->getStateInformation(expectedData);
                auto expectedXml = juce::AudioProcessor::getXmlFromBinary(
                    expectedData.getData(), static_cast<int>(expectedData.getSize()));
                const auto expected = juce::ValueTree::fromXml(*expectedXml);
                for (const auto targetWrapper : wrappers)
                {
                    auto target = create(targetWrapper);
                    target->setInstanceName("Before recall");
                    target->addModule(megadsp::ModuleType::saturator);
                    expect(target->loadPreset(file).wasOk());
                    expectEquals(target->getParameters().size(), 114);
                    expectEquals(target->getInstanceName(), juce::String("Whole Plugin"));
                    expectEquals(target->getBackgroundThemeIndex(), 12);
                    expectEquals(target->getSelectedTab(), 1);
                    expect(target->isLiveSafeSnapshotsEnabled());
                    expect(target->isSlotBypassed(1), "Preset recall must restore bypass.");
                    expect(target->reEqSettings(0) == eq);
                    juce::MemoryBlock restoredData;
                    target->getStateInformation(restoredData);
                    auto restoredXml = juce::AudioProcessor::getXmlFromBinary(
                        restoredData.getData(), static_cast<int>(restoredData.getSize()));
                    const auto restored = juce::ValueTree::fromXml(*restoredXml);
                    expect(restored.isEquivalentTo(expected), "All serialized plugin state must round trip.");
                    expect(target->undo());
                    expectEquals(target->getInstanceName(), juce::String("Before recall"));
                    expect(target->getRack().moduleType(0) == megadsp::ModuleType::saturator);
                    expect(target->redo());
                    target->recallSnapshot(1);
                    target->flushPendingUpdatesForTesting();
                    expect(target->reEqSettings(0) == alternate);
                    expectWithinAbsoluteError(
                        target->parameters.getParameter(megadsp::slotParameterId(1, "bypass"))->getValue(),
                        0.0f, 0.000001f);
                }
            }
            expect(file.replaceWithText("<not-a-preset/>"));
            MegaDSPAudioProcessor untouched;
            untouched.setInstanceName("Retained");
            expect(untouched.loadPreset(file).failed());
            expectEquals(untouched.getInstanceName(), juce::String("Retained"));
            expect(directory.deleteRecursively());
        }

        beginTest("Preset picker refreshes shared files and name label commits or cancels renaming");
        {
            const auto directory = MegaDSPAudioProcessor::presetDirectory()
                .getNonexistentChildFile("preset-picker-test", {}, false);
            expect(directory.createDirectory().wasOk());
            const auto file = directory.getChildFile("Shared preset.megadsp");
            auto receiver = std::make_unique<MegaDSPAudioProcessor>();
            receiver->prepareToPlay(48000.0, 64);
            auto editor = std::make_unique<MegaDSPAudioProcessorEditor>(*receiver, 1100, 720);
            editor->setTopLeftPosition(-10000, -10000);
            editor->addToDesktop(juce::ComponentPeer::windowIgnoresMouseClicks);
            editor->setVisible(true);
            std::function<juce::Component*(juce::Component&, const juce::String&)> find =
                [&find](juce::Component& component, const juce::String& title) -> juce::Component*
                {
                    if (component.getTitle() == title)
                        return &component;
                    for (auto* child : component.getChildren())
                        if (auto* found = find(*child, title))
                            return found;
                    return nullptr;
                };
            auto* label = dynamic_cast<juce::Label*>(find(*editor, "Instance name"));
            auto* picker = dynamic_cast<PresetSelector*>(find(*editor, "Presets"));
            expect(label != nullptr && picker != nullptr);
            if (label != nullptr && picker != nullptr)
            {
                expectEquals(label->getText(), juce::String("Untitled"));
                label->grabKeyboardFocus();
                expect(editor->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
                expect(label->isBeingEdited());
                if (auto* text = label->getCurrentTextEditor())
                    text->setText("  Lead  ", false);
                label->hideEditor(false);
                expectEquals(receiver->getInstanceName(), juce::String("Lead"));
                expectEquals(label->getText(), juce::String("Lead"));
                label->showEditor();
                if (auto* text = label->getCurrentTextEditor())
                    text->setText("Cancelled", false);
                label->hideEditor(true);
                expectEquals(receiver->getInstanceName(), juce::String("Lead"));
                label->showEditor();
                if (auto* text = label->getCurrentTextEditor())
                    text->setText({}, false);
                label->hideEditor(false);
                expect(receiver->getInstanceName().isEmpty());
                expectEquals(label->getText(), juce::String("Untitled"));
                MegaDSPAudioProcessor sender;
                sender.prepareToPlay(48000.0, 64);
                sender.setInstanceName("Saved elsewhere");
                sender.addModule(megadsp::ModuleType::compressor);
                expect(sender.savePreset(file).wasOk());
                picker->refresh();
                const auto files = MegaDSPAudioProcessor::userPresetFiles();
                const auto index = files.indexOf(file);
                expect(index >= 0);
                if (index >= 0)
                    picker->setSelectedId(
                        receiver->getNumPrograms() + index + 1, juce::sendNotificationSync);
                expectEquals(receiver->getInstanceName(), juce::String("Saved elsewhere"));
                expectEquals(label->getText(), juce::String("Saved elsewhere"));
                expect(receiver->getRack().moduleType(0) == megadsp::ModuleType::compressor);
                expectEquals(picker->getTextWhenNothingSelected(), juce::String("Presets"));
            }
            expect(directory.deleteRecursively());
        }

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

        beginTest("Undo history restores parameter and topology edits");
        {
            MegaDSPAudioProcessor undoProcessor;
            undoProcessor.prepareToPlay(48000.0, 512);
            auto* input =
                undoProcessor.parameters.getParameter("global.input");
            expect(input != nullptr);
            const auto initialInput =
                input != nullptr ? input->getValue() : 0.5f;
            if (input != nullptr)
            {
                input->beginChangeGesture();
                input->setValueNotifyingHost(0.82f);
                input->endChangeGesture();
            }
            expect(undoProcessor.canUndo());
            expect(undoProcessor.undoDescription().contains("Input"));
            expect(undoProcessor.undo());
            if (input != nullptr)
                expectWithinAbsoluteError(
                    input->getValue(), initialInput, 0.000001f);
            expect(undoProcessor.redo());
            if (input != nullptr)
                expectWithinAbsoluteError(
                    input->getValue(), 0.82f, 0.000001f);

            undoProcessor.addModule(megadsp::ModuleType::equalizer);
            expectEquals(undoProcessor.getRack().activeSlotCount(), 1);
            expectEquals(
                undoProcessor.undoDescription(),
                juce::String("Add module"));
            expect(undoProcessor.undo());
            expectEquals(undoProcessor.getRack().activeSlotCount(), 0);
            expect(undoProcessor.redo());
            expectEquals(undoProcessor.getRack().activeSlotCount(), 1);

            auto editor = std::make_unique<MegaDSPAudioProcessorEditor>(
                undoProcessor, 820, 420);
            expect(editor->keyPressed(juce::KeyPress(
                'z', juce::ModifierKeys::commandModifier, 0)));
            expectEquals(undoProcessor.getRack().activeSlotCount(), 0);
            expect(editor->keyPressed(juce::KeyPress(
                'z',
                juce::ModifierKeys::commandModifier
                    | juce::ModifierKeys::shiftModifier,
                0)));
            expectEquals(undoProcessor.getRack().activeSlotCount(), 1);
        }

        beginTest("Snapshot operations undo as complete state changes");
        {
            MegaDSPAudioProcessor snapshotUndo;
            snapshotUndo.prepareToPlay(48000.0, 512);
            snapshotUndo.addModule(megadsp::ModuleType::delay);
            snapshotUndo.storeSnapshot(1, false);
            expectEquals(snapshotUndo.getSelectedSnapshot(), 1);
            expect(snapshotUndo.isSnapshotPopulated(1));
            expectEquals(
                snapshotUndo.undoDescription(),
                juce::String("Store snapshot"));
            expect(snapshotUndo.undo());
            expectEquals(snapshotUndo.getSelectedSnapshot(), 0);
            expect(!snapshotUndo.isSnapshotPopulated(1));
            expect(snapshotUndo.redo());
            expectEquals(snapshotUndo.getSelectedSnapshot(), 1);
            expect(snapshotUndo.isSnapshotPopulated(1));
        }

        beginTest(
            "Factory presets publish each parameter's final value exactly once");
        MegaDSPAudioProcessor presetProcessor;
        presetProcessor.prepareToPlay(48000.0, 512);
        ProcessorChangeRecorder presetChanges;
        presetProcessor.addListener(&presetChanges);
        expectEquals(presetProcessor.getNumPrograms(), 5);
        expectEquals(presetProcessor.getCurrentProgram(), 0);
        expectEquals(
            presetProcessor.getProgramName(0), juce::String("Init"));
        expectEquals(
            presetProcessor.getProgramName(1), juce::String("Vocal Polish"));
        expectEquals(
            presetProcessor.getProgramName(2), juce::String("Drum Smash"));
        expectEquals(
            presetProcessor.getProgramName(3), juce::String("Wide Delay"));
        expectEquals(
            presetProcessor.getProgramName(4), juce::String("Ambient Hall"));
        expect(presetProcessor.getProgramName(-1).isEmpty());
        expect(presetProcessor.getProgramName(5).isEmpty());
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

        presetProcessor.setCurrentProgram(1);
        verifyFinalEvents();
        expectEquals(presetProcessor.getCurrentProgram(), 1);
        expectEquals(presetChanges.programChanges, 1);
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
        expectEquals(presetProcessor.getCurrentProgram(), 0);
        expectEquals(presetChanges.programChanges, 3);
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
        recorder.clear();
        presetProcessor.setCurrentProgram(5);
        expectEquals(presetProcessor.getCurrentProgram(), 0);
        expectEquals(presetChanges.programChanges, 3);
        for (const auto& values : recorder.values)
            expect(values.empty());
        presetProcessor.removeListener(&presetChanges);

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

        beginTest("Schema 12 migrates Stage Vocal controls in snapshots");
        MegaDSPAudioProcessor vocalMigrationSource;
        vocalMigrationSource.prepareToPlay(48000.0, 512);
        juce::MemoryBlock vocalMigrationData;
        vocalMigrationSource.getStateInformation(vocalMigrationData);
        auto vocalMigrationXml =
            juce::AudioProcessor::getXmlFromBinary(
                vocalMigrationData.getData(),
                static_cast<int>(vocalMigrationData.getSize()));
        expect(vocalMigrationXml != nullptr);
        if (vocalMigrationXml != nullptr)
        {
            auto vocalMigrationTree =
                juce::ValueTree::fromXml(*vocalMigrationXml);
            vocalMigrationTree.setProperty(
                "schemaVersion", 12, nullptr);
            const auto snapshots =
                vocalMigrationTree.getChildWithName("SNAPSHOTS");
            for (auto snapshot : snapshots)
            {
                if (static_cast<int>(
                        snapshot.getProperty("index", -1)) != 1)
                    continue;
                snapshot.setProperty("populated", true, nullptr);
                for (auto slot : snapshot)
                {
                    if (static_cast<int>(
                            slot.getProperty("index", -1)) != 0)
                        continue;
                    slot.setProperty(
                        "type",
                        static_cast<int>(
                            megadsp::ModuleType::stageVocal),
                        nullptr);
                    for (int control = 0;
                         control < megadsp::controlsPerSlot; ++control)
                        slot.setProperty(
                            "control" + juce::String(control + 1),
                            0.03f + static_cast<float>(control) * 0.07f,
                            nullptr);
                }
            }
            auto migratedXml = vocalMigrationTree.createXml();
            juce::MemoryBlock migratedData;
            if (migratedXml != nullptr)
                juce::AudioProcessor::copyXmlToBinary(
                    *migratedXml, migratedData);
            MegaDSPAudioProcessor restoredVocal;
            restoredVocal.prepareToPlay(48000.0, 512);
            restoredVocal.setStateInformation(
                migratedData.getData(),
                static_cast<int>(migratedData.getSize()));
            restoredVocal.recallSnapshot(1);
            restoredVocal.flushPendingUpdatesForTesting();
            const auto defaults = megadsp::moduleDefaults(
                megadsp::ModuleType::stageVocal);
            auto* fastAmount = restoredVocal.parameters.getParameter(
                megadsp::controlParameterId(0, 1));
            auto* deEssFrequency =
                restoredVocal.parameters.getParameter(
                    megadsp::controlParameterId(0, 7));
            expect(fastAmount != nullptr);
            expect(deEssFrequency != nullptr);
            if (fastAmount != nullptr)
                expectWithinAbsoluteError(
                    fastAmount->getValue(), defaults[1], 0.000001f);
            if (deEssFrequency != nullptr)
                expectWithinAbsoluteError(
                    deEssFrequency->getValue(), 0.24f, 0.000001f);
        }

        beginTest("IR directory persists and adjacent browsing wraps");
        const auto irDirectory =
            juce::File::getSpecialLocation(juce::File::tempDirectory)
                .getNonexistentChildFile(
                    "megaDSP-ir-browser", {}, true);
        expect(irDirectory.createDirectory().wasOk());
        const auto writeImpulse = [this](const juce::File& file)
        {
            if (file.existsAsFile())
                expect(file.deleteFile());
            std::unique_ptr<juce::OutputStream> stream =
                file.createOutputStream();
            juce::WavAudioFormat format;
            auto writer = format.createWriterFor(
                stream, juce::AudioFormatWriterOptions {}
                            .withSampleRate(48000.0)
                            .withNumChannels(2)
                            .withBitsPerSample(24));
            expect(writer != nullptr);
            juce::AudioBuffer<float> impulse(2, 256);
            impulse.clear();
            impulse.setSample(0, 0, 1.0f);
            impulse.setSample(1, 0, 1.0f);
            expect(writer != nullptr
                   && writer->writeFromAudioSampleBuffer(
                       impulse, 0, impulse.getNumSamples()));
        };
        const auto firstIr = irDirectory.getChildFile("a-room.wav");
        const auto secondIr = irDirectory.getChildFile("B-room.wav");
        const auto nestedDirectory =
            irDirectory.getChildFile("Halls");
        expect(nestedDirectory.createDirectory().wasOk());
        const auto nestedIr =
            nestedDirectory.getChildFile("C-bright-hall.wav");
        const auto invalidIr =
            nestedDirectory.getChildFile("invalid.wav");
        writeImpulse(firstIr);
        writeImpulse(secondIr);
        writeImpulse(nestedIr);
        expect(invalidIr.replaceWithText("not audio"));
        {
            MegaDSPAudioProcessor irBrowser;
            irBrowser.prepareToPlay(48000.0, 512);
            setType(irBrowser, 0, megadsp::ModuleType::convolutionReverb);
            irBrowser.flushPendingUpdatesForTesting();
            expect(irBrowser.setImpulseResponseRootDirectory(
                irDirectory).wasOk());
            expectEquals(irBrowser.impulseResponseLibrarySize(), 3);
            irBrowser.refreshAssetLibrariesForTesting();
            const auto automaticallyAdded =
                irDirectory.getChildFile("automatic-room.wav");
            writeImpulse(automaticallyAdded);
            irBrowser.refreshAssetLibrariesForTesting();
            expectEquals(irBrowser.impulseResponseLibrarySize(), 4);
            MegaDSPAudioProcessor sharedIrCatalogBrowser;
            sharedIrCatalogBrowser.refreshAssetLibrariesForTesting();
            expectEquals(
                sharedIrCatalogBrowser.impulseResponseLibrarySize(), 4);
            expect(automaticallyAdded.deleteFile());
            irBrowser.refreshAssetLibrariesForTesting();
            expectEquals(irBrowser.impulseResponseLibrarySize(), 3);
            MegaDSPAudioProcessor freshIrBrowser;
            expectEquals(
                freshIrBrowser.impulseResponseRootDirectory()
                    .getFullPathName(),
                irDirectory.getFullPathName());
            expectEquals(
                freshIrBrowser.impulseResponseLibrarySize(), 3);
            const auto irCache =
                juce::File::getSpecialLocation(
                    juce::File::tempDirectory)
                    .getChildFile("megaDSP-tests")
                    .getChildFile(
                        "impulse-response-library-cache.xml");
            expect(
                irCache.existsAsFile(),
                "Rebuilding an IR library must persist its index.");
            expect(
                nestedIr.replaceWithText("cache probe"),
                "The IR cache probe must replace the source file.");
            MegaDSPAudioProcessor cachedIrBrowser;
            expectEquals(
                cachedIrBrowser.impulseResponseLibrarySize(), 3);
            expectEquals(
                static_cast<int>(
                    cachedIrBrowser.searchImpulseResponses("hall").size()),
                1);
            writeImpulse(nestedIr);
            expect(irCache.deleteFile());
            MegaDSPAudioProcessor coldIrSearch;
            const auto coldIrSearchStart =
                juce::Time::getHighResolutionTicks();
            const auto immediateIrMatches =
                coldIrSearch.searchImpulseResponses("bright-hall");
            const auto coldIrSearchMicros =
                juce::Time::highResolutionTicksToSeconds(
                    juce::Time::getHighResolutionTicks()
                    - coldIrSearchStart)
                * 1000000.0;
            expect(
                coldIrSearchMicros < 500000.0,
                "A cold IR search must schedule indexing off the UI thread.");
            expectEquals(
                static_cast<int>(immediateIrMatches.size()), 1);
            coldIrSearch.finishLibraryIndexForTesting();
            expectEquals(
                static_cast<int>(
                    coldIrSearch.searchImpulseResponses("hall").size()),
                1);
            expectEquals(
                static_cast<int>(
                    irBrowser.searchImpulseResponses("hall").size()),
                1);
            expectEquals(
                static_cast<int>(
                    irBrowser.searchImpulseResponses("stereo").size()),
                3);
            expectEquals(
                static_cast<int>(
                    irBrowser.searchImpulseResponses("sr:48000").size()),
                3);
            expectEquals(
                static_cast<int>(
                    irBrowser.searchImpulseResponses("short").size()),
                3);
            const auto hallMatches =
                irBrowser.searchImpulseResponses(
                    "hall stereo sr:48000 short");
            expectEquals(static_cast<int>(hallMatches.size()), 1);
            if (!hallMatches.empty())
                expectEquals(
                    hallMatches.front().file.getFullPathName(),
                    nestedIr.getFullPathName());
            expect(irBrowser.searchImpulseResponses("mono").empty());
            expectEquals(
                static_cast<int>(
                    irBrowser.searchImpulseResponses("hall bright").size()),
                1);
            expectEquals(
                static_cast<int>(
                    irBrowser.searchImpulseResponses("brigt").size()),
                1);
            expect(
                irBrowser.searchImpulseResponses("hall -bright").empty());
            const auto orderBeforeFavorite =
                irBrowser.searchImpulseResponses({});
            expect(irBrowser.setImpulseResponseFavorite(
                nestedIr, true).wasOk());
            const auto favoriteIrs =
                irBrowser.searchImpulseResponses("favorite");
            expectEquals(static_cast<int>(favoriteIrs.size()), 1);
            if (!favoriteIrs.empty())
                expectEquals(
                    favoriteIrs.front().file.getFullPathName(),
                    nestedIr.getFullPathName());
            const auto orderAfterFavorite =
                irBrowser.searchImpulseResponses({});
            expectEquals(
                static_cast<int>(orderAfterFavorite.size()),
                static_cast<int>(orderBeforeFavorite.size()));
            for (size_t index = 0;
                 index < std::min(
                     orderBeforeFavorite.size(),
                     orderAfterFavorite.size());
                 ++index)
                expectEquals(
                    orderAfterFavorite[index].file.getFullPathName(),
                    orderBeforeFavorite[index].file.getFullPathName(),
                    "Favoriting must not reorder IR results.");
            MegaDSPAudioProcessor favoriteIrRestored;
            expect(
                favoriteIrRestored.isImpulseResponseFavorite(nestedIr),
                "IR favorites must persist across processor instances.");

            beginTest(
                "Convolution and Guitar Cabinet widgets filter IR libraries");
            {
                MegaDSPAudioProcessor convolutionBrowser;
                convolutionBrowser.prepareToPlay(48000.0, 512);
                setType(
                    convolutionBrowser, 0,
                    megadsp::ModuleType::convolutionReverb);
                convolutionBrowser.flushPendingUpdatesForTesting();
                expect(convolutionBrowser.setImpulseResponseRootDirectory(
                    irDirectory).wasOk());
                megadsp::ui::EffectGraph convolutionGraph(
                    convolutionBrowser, 0,
                    megadsp::ModuleType::convolutionReverb);
                convolutionGraph.setBounds(0, 0, 384, 100);
                expectEquals(
                    convolutionGraph
                        .compactImpulseResponseCountForTesting(),
                    3);
                convolutionGraph
                    .setCompactImpulseFavoritesOnlyForTesting(true);
                expectEquals(
                    convolutionGraph
                        .compactImpulseResponseCountForTesting(),
                    1);
                convolutionGraph
                    .toggleCompactImpulseFavoriteForTesting(0);
                expectEquals(
                    convolutionGraph
                        .compactImpulseResponseCountForTesting(),
                    0);
                expect(!convolutionBrowser.isImpulseResponseFavorite(
                    nestedIr));
                expect(convolutionBrowser.setImpulseResponseFavorite(
                    nestedIr, true).wasOk());
                convolutionGraph
                    .setCompactImpulseFavoritesOnlyForTesting(false);
                convolutionGraph.scrollCompactNamLibraryForTesting(1);
                expectEquals(
                    convolutionGraph
                        .compactNamFirstVisibleModelForTesting(),
                    1);
                convolutionGraph
                    .toggleCompactImpulseFavoriteForTesting(2);
                expectEquals(
                    convolutionGraph
                        .compactNamFirstVisibleModelForTesting(),
                    1,
                    "Favoriting must preserve IR list scroll position.");
                expect(convolutionBrowser.setImpulseResponseFavorite(
                    nestedIr, true).wasOk());
                convolutionGraph.setCompactAssetFilterForTesting(
                    "BRIGHT-HALL");
                expectEquals(
                    convolutionGraph
                        .compactImpulseResponseCountForTesting(),
                    1);
                convolutionGraph
                    .activateCompactImpulseResponseForTesting(0);
                convolutionBrowser
                    .finishImpulseResponseLoadForTesting();
                expectEquals(
                    convolutionBrowser.impulseResponsePath(0),
                    nestedIr.getFullPathName());

                MegaDSPAudioProcessor cabinetBrowser;
                cabinetBrowser.prepareToPlay(48000.0, 512);
                setType(
                    cabinetBrowser, 0,
                    megadsp::ModuleType::guitarCabinet);
                cabinetBrowser.flushPendingUpdatesForTesting();
                expect(cabinetBrowser.setImpulseResponseRootDirectory(
                    irDirectory).wasOk());
                megadsp::ui::EffectGraph cabinetGraph(
                    cabinetBrowser, 0,
                    megadsp::ModuleType::guitarCabinet);
                cabinetGraph.setBounds(0, 0, 384, 100);
                cabinetGraph.setCompactAssetFilterForTesting("B-ROOM");
                expectEquals(
                    cabinetGraph
                        .compactImpulseResponseCountForTesting(),
                    1);
                cabinetGraph
                    .activateCompactImpulseResponseForTesting(0);
                cabinetBrowser.finishImpulseResponseLoadForTesting();
                expectEquals(
                    cabinetBrowser.impulseResponsePath(0),
                    secondIr.getFullPathName());
            }
            expect(irBrowser.setImpulseResponseFavorite(
                nestedIr, false).wasOk());

            bool asyncIrCompleted = false;
            juce::Result asyncIrResult = juce::Result::fail(
                "Async IR callback was not called.");
            const auto asyncIrStart =
                juce::Time::getHighResolutionTicks();
            expect(
                irBrowser.loadConvolutionImpulseResponseBankAsync(
                    0, 0, nestedIr,
                    [&asyncIrCompleted, &asyncIrResult](
                        const juce::Result& result)
                    {
                        asyncIrCompleted = true;
                        asyncIrResult = result;
                    }).wasOk(),
                "Selecting an IR result must schedule background loading.");
            const auto asyncIrMicros =
                juce::Time::highResolutionTicksToSeconds(
                    juce::Time::getHighResolutionTicks() - asyncIrStart)
                * 1000000.0;
            expect(
                asyncIrMicros < 500000.0,
                "Selecting an IR search result must not decode it on the "
                "calling thread.");
            expect(
                irBrowser.impulseResponsePath(0).isEmpty(),
                "The previous IR must remain active during preparation.");
            irBrowser.finishImpulseResponseLoadForTesting();
            expect(
                asyncIrCompleted,
                "The async IR completion callback must run.");
            expect(
                asyncIrResult.wasOk(),
                asyncIrResult.getErrorMessage());
            expectEquals(
                irBrowser.impulseResponseName(0),
                nestedIr.getFileName());
            expect(irBrowser.loadImpulseResponse(0, secondIr).wasOk());
            expectEquals(
                irBrowser.impulseResponseDirectory(0).getFullPathName(),
                irDirectory.getFullPathName());
            expectEquals(
                irBrowser.undoDescription(),
                juce::String("Load impulse response"));
            expect(irBrowser.undo());
            expectEquals(
                irBrowser.impulseResponseName(0),
                nestedIr.getFileName());
            expect(irBrowser.redo());
            expectEquals(
                irBrowser.impulseResponseName(0), secondIr.getFileName());
            expect(irBrowser.loadAdjacentImpulseResponse(0, 1).wasOk());
            expectEquals(
                irBrowser.impulseResponseName(0), nestedIr.getFileName());
            expect(irBrowser.loadAdjacentImpulseResponse(0, -1).wasOk());
            expectEquals(
                irBrowser.impulseResponseName(0), secondIr.getFileName());

            expect(irBrowser.loadConvolutionImpulseResponseBank(
                0, 0, firstIr).wasOk());
            expect(irBrowser.loadConvolutionImpulseResponseBank(
                0, 1, secondIr).wasOk());
            expect(irBrowser.loadConvolutionImpulseResponseBank(
                0, 2, nestedIr).wasOk());
            expect(irBrowser.setConvolutionWetBufferSamples(
                0, 512).wasOk());
            expectEquals(
                irBrowser.convolutionWetBufferSamples(0), 512);
            expect(irBrowser.setConvolutionWetBufferSamples(
                0, 123).failed());
            expect(irBrowser.selectConvolutionImpulseResponseBank(0, 1));
            expectEquals(
                irBrowser.activeConvolutionImpulseResponseBank(0), 1);
            expectEquals(
                irBrowser.convolutionImpulseResponseBankName(0, 0),
                firstIr.getFileName());
            expectEquals(
                irBrowser.convolutionImpulseResponseBankName(0, 1),
                secondIr.getFileName());
            expectEquals(
                irBrowser.convolutionImpulseResponseBankName(0, 2),
                nestedIr.getFileName());
            juce::MemoryBlock bankState;
            irBrowser.getStateInformation(bankState);
            MegaDSPAudioProcessor restoredBanks;
            restoredBanks.prepareToPlay(48000.0, 512);
            restoredBanks.setStateInformation(
                bankState.getData(),
                static_cast<int>(bankState.getSize()));
            restoredBanks.flushPendingUpdatesForTesting();
            expectEquals(
                restoredBanks.activeConvolutionImpulseResponseBank(0), 1);
            expectEquals(
                restoredBanks.convolutionWetBufferSamples(0), 512);
            expectEquals(
                restoredBanks.convolutionImpulseResponseBankName(0, 0),
                firstIr.getFileName());
            expectEquals(
                restoredBanks.convolutionImpulseResponseBankName(0, 1),
                secondIr.getFileName());
            expectEquals(
                restoredBanks.convolutionImpulseResponseBankName(0, 2),
                nestedIr.getFileName());

            irBrowser.clearImpulseResponse(0);
            expect(irBrowser.impulseResponsePath(0).isEmpty());
            expectEquals(
                irBrowser.impulseResponseDirectory(0).getFullPathName(),
                irDirectory.getFullPathName());
            juce::MemoryBlock irBrowserState;
            irBrowser.getStateInformation(irBrowserState);
            MegaDSPAudioProcessor restoredIrBrowser;
            restoredIrBrowser.prepareToPlay(48000.0, 512);
            restoredIrBrowser.setStateInformation(
                irBrowserState.getData(),
                static_cast<int>(irBrowserState.getSize()));
            expectEquals(
                restoredIrBrowser.impulseResponseDirectory(0).getFullPathName(),
                irDirectory.getFullPathName());
            expectEquals(
                restoredIrBrowser.impulseResponseLibrarySize(), 3);
            expect(restoredIrBrowser.loadAdjacentImpulseResponse(0, 1).wasOk());
            expectEquals(
                restoredIrBrowser.impulseResponseName(0),
                firstIr.getFileName());
        }
        {
            MegaDSPAudioProcessor wormhole;
            wormhole.prepareToPlay(48000.0, 512);
            setType(
                wormhole, 0,
                megadsp::ModuleType::wormholeConvolution);
            wormhole.flushPendingUpdatesForTesting();
            expect(wormhole.loadImpulseResponse(
                0, 0, firstIr).wasOk());
            expect(wormhole.loadImpulseResponse(
                0, 1, secondIr).wasOk());
            expectEquals(
                wormhole.impulseResponsePath(0, 0),
                firstIr.getFullPathName());
            expectEquals(
                wormhole.impulseResponsePath(0, 1),
                secondIr.getFullPathName());
            juce::MemoryBlock wormholeState;
            wormhole.getStateInformation(wormholeState);
            MegaDSPAudioProcessor restored;
            restored.prepareToPlay(48000.0, 512);
            restored.setStateInformation(
                wormholeState.getData(),
                static_cast<int>(wormholeState.getSize()));
            expectEquals(
                restored.impulseResponseName(0, 0),
                firstIr.getFileName());
            expectEquals(
                restored.impulseResponseName(0, 1),
                secondIr.getFileName());

            auto legacyXml = juce::AudioProcessor::getXmlFromBinary(
                wormholeState.getData(),
                static_cast<int>(wormholeState.getSize()));
            expect(legacyXml != nullptr);
            if (legacyXml != nullptr)
            {
                auto legacyTree = juce::ValueTree::fromXml(*legacyXml);
                legacyTree.setProperty("schemaVersion", 11, nullptr);
                for (auto snapshot :
                     legacyTree.getChildWithName("SNAPSHOTS"))
                    for (auto slot : snapshot)
                        slot.removeProperty("impulseResponse2", nullptr);
                auto updatedXml = legacyTree.createXml();
                juce::MemoryBlock legacyData;
                if (updatedXml != nullptr)
                    juce::AudioProcessor::copyXmlToBinary(
                        *updatedXml, legacyData);
                MegaDSPAudioProcessor legacyRestored;
                legacyRestored.prepareToPlay(48000.0, 512);
                legacyRestored.setStateInformation(
                    legacyData.getData(),
                    static_cast<int>(legacyData.getSize()));
                expectEquals(
                    legacyRestored.impulseResponseName(0, 0),
                    firstIr.getFileName());
                expect(legacyRestored.impulseResponsePath(0, 1).isEmpty());
            }
        }
        expect(firstIr.deleteFile());
        expect(secondIr.deleteFile());
        expect(irDirectory.deleteRecursively());

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

        beginTest("DSP performance meter reports realtime deadline load");
        MegaDSPAudioProcessor performanceProcessor;
        performanceProcessor.prepareToPlay(48000.0, 64);
        performanceProcessor.addModule(megadsp::ModuleType::delay);
        juce::AudioBuffer<float> performanceBuffer(2, 64);
        juce::MidiBuffer performanceMidi;
        for (int block = 0; block < 90; ++block)
        {
            performanceBuffer.clear();
            performanceProcessor.processBlock(
                performanceBuffer, performanceMidi);
        }
        auto performance =
            performanceProcessor.performanceSnapshot();
        expectWithinAbsoluteError(
            performance.sampleRate, 48000.0, 0.001);
        expectEquals(performance.blockSamples, 64);
        expectWithinAbsoluteError(
            performance.deadlineMilliseconds,
            1000.0f * 64.0f / 48000.0f, 0.0001f);
        expect(performance.currentLoad >= 0.0f);
        expect(performance.averageLoad > 0.0f);
        expect(performance.peakLoad >= performance.averageLoad);
        expect(performance.percentile95 >= 0.0f);
        expect(performance.percentile99
               >= performance.percentile95);
        expect(performance.historyCount > 0);
        expect(
            performance.slotAverageLoad[0] > 0.0f,
            "The active module must publish per-slot DSP cost.");
        expect(!performance.offline);
        expect(!performance.liveSafe);
        expect(!performance.hostBypassed);
        expect(performance.callbacksActive);
        expect(performance.effectDspActive());

        performanceProcessor.setLiveSafeSnapshotsEnabled(true);
        performanceProcessor.setNonRealtime(true);
        performanceBuffer.clear();
        performanceProcessor.processBlock(
            performanceBuffer, performanceMidi);
        performance =
            performanceProcessor.performanceSnapshot();
        expect(performance.liveSafe);
        expect(performance.offline);

        performanceProcessor.processBlockBypassed(
            performanceBuffer, performanceMidi);
        performance =
            performanceProcessor.performanceSnapshot();
        expect(performance.hostBypassed);
        expect(performance.callbacksActive);
        expect(!performance.effectDspActive());
        expect(performance.currentLoad >= 0.0f);
        for (int slot = 0; slot < megadsp::slotCount; ++slot)
        {
            expectEquals(performance.slotAverageLoad[static_cast<size_t>(slot)], 0.0f);
            expectEquals(performance.slotPeakLoad[static_cast<size_t>(slot)], 0.0f);
        }
        expectEquals(performance.otherSnapshotsAverageLoad, 0.0f);
        expectEquals(performance.otherSnapshotsPeakLoad, 0.0f);

        performanceProcessor.resetPerformanceMetrics();
        performance =
            performanceProcessor.performanceSnapshot();
        expectEquals(performance.historyCount, 0);
        expectEquals(
            static_cast<juce::int64>(performance.overrunCount),
            static_cast<juce::int64>(0));
        expectWithinAbsoluteError(
            performance.averageLoad, 0.0f, 0.000001f);

        beginTest("Stopped callbacks show idle without discarding historical measurements");
        {
            MegaDSPAudioProcessor idleProcessor;
            expect(!idleProcessor.performanceSnapshot().callbacksActive);
            idleProcessor.prepareToPlay(48000.0, 64);
            idleProcessor.addModule(megadsp::ModuleType::delay);
            expect(!idleProcessor.performanceSnapshot().callbacksActive);
            juce::AudioBuffer<float> block(2, 64);
            juce::MidiBuffer idleMidi;
            for (int index = 0; index < 90; ++index)
            {
                block.clear();
                idleProcessor.processBlock(block, idleMidi);
            }
            const auto before = idleProcessor.performanceSnapshot();
            expect(before.callbacksActive);
            expect(before.averageLoad > 0.0f);
            expect(before.slotAverageLoad[0] > 0.0f);
            expect(before.historyCount > 0);

            MegaDSPAudioProcessor largeBlockProcessor;
            largeBlockProcessor.prepareToPlay(48000.0, 32768);
            juce::AudioBuffer<float> largeBlock(2, 32768);
            largeBlock.clear();
            largeBlockProcessor.processBlock(largeBlock, idleMidi);
            juce::Thread::sleep(350);

            const auto idle = idleProcessor.performanceSnapshot();
            expect(!idle.callbacksActive);
            expect(!idle.hostBypassed, "Inactivity must not invent a host bypass notification.");
            expect(!idle.effectDspActive());
            expectEquals(idle.currentLoad, 0.0f);
            expectEquals(idle.averageLoad, before.averageLoad);
            expectEquals(idle.peakLoad, before.peakLoad);
            expectEquals(idle.percentile99, before.percentile99);
            expectEquals(idle.historyCount, before.historyCount);
            expect(idle.history == before.history);
            expectEquals(idle.slotAverageLoad[0], 0.0f);
            expectEquals(idle.slotPeakLoad[0], 0.0f);
            expectEquals(idle.otherSnapshotsAverageLoad, 0.0f);
            expectEquals(idle.otherSnapshotsPeakLoad, 0.0f);
            expect(largeBlockProcessor.performanceSnapshot().callbacksActive,
                   "A legitimate large block must not time out after only 250 ms.");

            block.clear();
            idleProcessor.processBlockBypassed(block, idleMidi);
            expect(idleProcessor.performanceSnapshot().callbacksActive);
            expect(idleProcessor.performanceSnapshot().hostBypassed);
            expect(!idleProcessor.performanceSnapshot().effectDspActive());
            juce::Thread::sleep(350);
            expect(!idleProcessor.performanceSnapshot().callbacksActive);
            expectEquals(idleProcessor.performanceSnapshot().currentLoad, 0.0f);
            block.clear();
            idleProcessor.processBlock(block, idleMidi);
            expect(idleProcessor.performanceSnapshot().effectDspActive());
            expect(!idleProcessor.performanceSnapshot().hostBypassed);
            idleProcessor.releaseResources();
            expect(!idleProcessor.performanceSnapshot().callbacksActive);
            expectEquals(idleProcessor.performanceSnapshot().currentLoad, 0.0f);
            idleProcessor.prepareToPlay(48000.0, 64);
            expect(!idleProcessor.performanceSnapshot().callbacksActive);
        }

        beginTest("An executing callback never becomes idle even when overdue");
        {
            class PausingPlayHead final : public juce::AudioPlayHead
            {
            public:
                juce::Optional<PositionInfo> getPosition() const override
                {
                    entered.signal();
                    resume.wait(5000);
                    return {};
                }

                mutable juce::WaitableEvent entered;
                mutable juce::WaitableEvent resume;
            };
            PausingPlayHead playHead;
            MegaDSPAudioProcessor busyProcessor;
            busyProcessor.prepareToPlay(48000.0, 64);
            busyProcessor.setPlayHead(&playHead);
            juce::AudioBuffer<float> block(2, 64);
            block.clear();
            juce::MidiBuffer busyMidi;
            std::thread callback([&] { busyProcessor.processBlock(block, busyMidi); });
            expect(playHead.entered.wait(2000));
            juce::Thread::sleep(350);
            expect(busyProcessor.performanceSnapshot().callbacksActive);
            expect(busyProcessor.performanceSnapshot().effectDspActive());
            playHead.resume.signal();
            callback.join();
            busyProcessor.setPlayHead(nullptr);
            expect(busyProcessor.performanceSnapshot().callbacksActive);
        }

        beginTest("Host bypass leaves effect capture unchanged while routing dry audio");
        {
            MegaDSPAudioProcessor bypassedProcessor;
            bypassedProcessor.prepareToPlay(48000.0, 64);
            bypassedProcessor.addModule(megadsp::ModuleType::delay);
            bypassedProcessor.setLiveSafeSnapshotsEnabled(true);
            bypassedProcessor.getVisualizationData().setSlotActive(0, true);
            juce::AudioBuffer<float> block(2, 64);
            juce::MidiBuffer hostBypassMidi;
            block.clear();
            bypassedProcessor.processBlock(block, hostBypassMidi);
            std::array<float, 1024> capture {};
            const auto capturedBefore = bypassedProcessor.getVisualizationData()
                .slotData(0).input.copyLatest(capture);
            expectEquals(capturedBefore, size_t { 64 });
            for (int index = 0; index < 12; ++index)
            {
                juce::FloatVectorOperations::fill(block.getWritePointer(0), 0.25f, 64);
                juce::FloatVectorOperations::fill(block.getWritePointer(1), -0.1f, 64);
                bypassedProcessor.processBlockBypassed(block, hostBypassMidi);
                for (int sample = 0; sample < 64; ++sample)
                {
                    expectEquals(block.getSample(0, sample), 0.25f);
                    expectEquals(block.getSample(1, sample), -0.1f);
                }
                const auto bypassed = bypassedProcessor.performanceSnapshot();
                expect(bypassed.callbacksActive);
                expect(bypassed.hostBypassed);
                expect(!bypassed.effectDspActive());
                expectEquals(bypassed.slotAverageLoad[0], 0.0f);
                expectEquals(bypassed.slotPeakLoad[0], 0.0f);
            }
            expectEquals(bypassedProcessor.getVisualizationData()
                             .slotData(0).input.copyLatest(capture),
                         capturedBefore);
            block.clear();
            bypassedProcessor.processBlock(block, hostBypassMidi);
            expect(bypassedProcessor.performanceSnapshot().effectDspActive());
            expect(bypassedProcessor.getVisualizationData()
                       .slotData(0).input.copyLatest(capture) > capturedBefore);
        }
    }
};

ProcessorTopologyTests processorTopologyTests;
} // namespace
