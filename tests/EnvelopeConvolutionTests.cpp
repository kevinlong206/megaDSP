#include "../src/DspModules.h"
#include "../src/PluginEditor.h"
#include "../src/ui/EffectGraph.h"
#include "../src/ui/GuiLayout.h"

#include <cmath>
#include <limits>

namespace megadsp
{
namespace
{
using Module = EnvelopeConvolutionModule;

class EnvelopeConvolutionTests final : public juce::UnitTest
{
public:
    EnvelopeConvolutionTests() : juce::UnitTest("Envelope Convolution", "megaDSP") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        beginTest("Separate appended module preserves the host contract");
        expectEquals(static_cast<int>(ModuleType::convolutionReverb), 11);
        expectEquals(static_cast<int>(ModuleType::envelopeModulator), 60);
        expectEquals(static_cast<int>(ModuleType::envelopeConvolution), 61);
        expectEquals(moduleTypeCount, 62);
        expectEquals(stateSchemaVersion, 14);
        auto created = createDspModule(ModuleType::envelopeConvolution);
        expect(dynamic_cast<Module*>(created.get()) != nullptr);
        expect(created->impulseResponseCapability() != nullptr);
        expect(created->continuousTelemetryCapability() != nullptr);
        expectEquals(created->latencySamples(), 0);
        expect(created->dryPathTiming() == DryPathTiming::immediate);
        auto controls = moduleDefaults(ModuleType::envelopeConvolution);
        for (int control = 0; control < Module::activeControlCount; ++control)
        {
            const auto text = formatControlValue(
                ModuleType::envelopeConvolution, control,
                controls[static_cast<size_t>(control)]);
            const auto parsed = parseControlValue(
                ModuleType::envelopeConvolution, control, text);
            expect(parsed.has_value(), text);
            if (parsed)
                expectWithinAbsoluteError(*parsed,
                    controls[static_cast<size_t>(control)], 0.003f, text);
        }

        beginTest("IR fixtures");
        const auto directory = juce::File::getSpecialLocation(
            juce::File::tempDirectory).getNonexistentChildFile(
                "megadsp-envelope-convolution", "", false);
        expect(directory.createDirectory().wasOk());
        juce::TemporaryFile impulse(directory.getChildFile("mono.wav"));
        juce::TemporaryFile delayedImpulse(directory.getChildFile("delayed.wav"));
        juce::TemporaryFile stereoImpulse(directory.getChildFile("stereo.wav"));
        expect(writeImpulse(impulse.getFile(), 1, 0));
        expect(writeImpulse(delayedImpulse.getFile(), 1, 8192));
        expect(writeImpulse(stereoImpulse.getFile(), 2, 0));
        const juce::dsp::ProcessSpec spec { 48000.0, 128, 2 };
        ProcessEnvironment environment;
        environment.captureTelemetry = true;
        environment.nonRealtime = true;
        Module module;
        module.prepare(spec);

        beginTest("Missing IR is immediate dry even at a fully wet endpoint");
        controls[0] = controls[1] = 1.0f;
        juce::AudioBuffer<float> block(2, 128);
        block.clear();
        block.setSample(0, 0, 0.25f);
        block.setSample(1, 17, -0.3f);
        module.process(block, controls, environment);
        expectEquals(block.getSample(0, 0), 0.25f);
        expectEquals(block.getSample(1, 17), -0.3f);
        expectEquals(block.getSample(1, 0), 0.0f);

        beginTest("Input dBFS maps quiet, midpoint and loud blends in both directions");
        expect(module.loadImpulseResponse(impulse.getFile()).wasOk());
        controls = moduleDefaults(ModuleType::envelopeConvolution);
        controls[Module::quietBlendControl] = 0.8f;
        controls[Module::loudBlendControl] = 0.2f;
        renderLevel(module, controls, 0.0001f, 96000, environment);
        auto snapshot = read(module);
        expectWithinAbsoluteError(snapshot.values[Module::currentBlend], 0.8f, 0.001f);
        renderLevel(module, controls, 0.5f, 96000, environment);
        snapshot = read(module);
        expectWithinAbsoluteError(snapshot.values[Module::currentBlend], 0.2f, 0.001f);
        renderLevel(module, controls, juce::Decibels::decibelsToGain(-30.0f),
                    192000, environment);
        snapshot = read(module);
        expectWithinAbsoluteError(snapshot.values[Module::currentBlend], 0.5f, 0.003f);
        std::swap(controls[0], controls[1]);
        renderLevel(module, controls, 0.0001f, 192000, environment);
        expectWithinAbsoluteError(read(module).values[Module::currentBlend], 0.2f, 0.003f);
        renderLevel(module, controls, 0.5f, 96000, environment);
        expectWithinAbsoluteError(read(module).values[Module::currentBlend], 0.8f, 0.001f);

        beginTest("Attack and release control measured response time");
        const auto response = [&](float attack, float release)
        {
            Module detector;
            detector.prepare(spec);
            expect(detector.loadImpulseResponse(impulse.getFile()).wasOk());
            auto values = moduleDefaults(ModuleType::envelopeConvolution);
            values[0] = 0.0f;
            values[1] = 1.0f;
            values[Module::attackControl] = attack;
            values[Module::releaseControl] = release;
            renderLevel(detector, values, 0.0f, 2048, environment);
            renderLevel(detector, values, 0.5f, 480, environment);
            const auto rising = read(detector).values[Module::currentBlend];
            renderLevel(detector, values, 0.5f, 96000, environment);
            renderLevel(detector, values, 0.0f, 4800, environment);
            return std::array<float, 2> {
                rising, read(detector).values[Module::currentBlend]
            };
        };
        const auto fast = response(0.0f, 0.0f);
        const auto slow = response(1.0f, 1.0f);
        expect(fast[0] > slow[0] + 0.2f);
        expect(fast[1] < slow[1] - 0.2f);

        beginTest("Rendered audio uses complementary linear dry/wet gains");
        for (const auto& file : { impulse.getFile(), stereoImpulse.getFile() })
        {
            Module blended, wetOnly;
            blended.prepare(spec);
            wetOnly.prepare(spec);
            expect(blended.loadImpulseResponse(file).wasOk());
            expect(wetOnly.loadImpulseResponse(file).wasOk());
            auto values = moduleDefaults(ModuleType::envelopeConvolution);
            values[0] = values[1] = 0.25f;
            auto wetValues = values;
            wetValues[0] = wetValues[1] = 1.0f;
            juce::AudioBuffer<float> wet(2, 128);
            float maximumError = 0.0f;
            for (int index = 0; index < 200; ++index)
            {
                fillSine(block, index * 128);
                juce::AudioBuffer<float> dry(block);
                wet.makeCopyOf(block);
                blended.process(block, values, environment);
                wetOnly.process(wet, wetValues, environment);
                for (int channel = 0; channel < 2; ++channel)
                    for (int sample = 0; sample < 128; ++sample)
                        maximumError = juce::jmax(maximumError, std::abs(
                            block.getSample(channel, sample)
                            - (0.75f * dry.getSample(channel, sample)
                               + 0.25f * wet.getSample(channel, sample))));
            }
            expect(maximumError < 0.000002f, juce::String(maximumError));
        }

        beginTest("Zero blend is exact dry while convolution history keeps running");
        Module tail;
        Module referenceTail;
        tail.prepare(spec);
        referenceTail.prepare(spec);
        expect(tail.loadImpulseResponse(delayedImpulse.getFile()).wasOk());
        expect(referenceTail.loadImpulseResponse(delayedImpulse.getFile()).wasOk());
        controls = moduleDefaults(ModuleType::envelopeConvolution);
        controls[0] = controls[1] = 0.0f;
        float dryError = 0.0f;
        float tailPeak = 0.0f;
        float tailError = 0.0f;
        auto referenceControls = controls;
        referenceControls[0] = referenceControls[1] = 1.0f;
        for (int index = 0; index < 150; ++index)
        {
            block.clear();
            if (index == 0)
                block.setSample(0, 0, 0.8f);
            if (index == 12)
                controls[0] = controls[1] = 1.0f;
            juce::AudioBuffer<float> dry(block);
            juce::AudioBuffer<float> reference(block);
            tail.process(block, controls, environment);
            referenceTail.process(reference, referenceControls, environment);
            if (index < 12)
                for (int channel = 0; channel < 2; ++channel)
                    for (int sample = 0; sample < 128; ++sample)
                        dryError = juce::jmax(dryError, std::abs(
                            block.getSample(channel, sample) - dry.getSample(channel, sample)));
            if (index > 12)
                tailPeak = juce::jmax(tailPeak, block.getMagnitude(0, 0, 128));
            if (index > 24)
                for (int channel = 0; channel < 2; ++channel)
                    for (int sample = 0; sample < 128; ++sample)
                        tailError = juce::jmax(tailError, std::abs(
                            block.getSample(channel, sample) - reference.getSample(channel, sample)));
        }
        expectEquals(dryError, 0.0f);
        expect(tailPeak > 0.01f, juce::String(tailPeak));
        expect(tailError < 0.000002f, juce::String(tailError));
        expect(tail.tailSeconds(controls) > 0.17);

        beginTest("Stereo-linked envelope, centered mono IR and encoded stereo IR");
        Module centered;
        centered.prepare(spec);
        expect(centered.loadImpulseResponse(impulse.getFile()).wasOk());
        controls[0] = controls[1] = 1.0f;
        float channelDifference = 0.0f;
        float centeredEnergy = 0.0f;
        for (int index = 0; index < 100; ++index)
        {
            fillSine(block, index * 128);
            block.clear(1, 0, 128);
            centered.process(block, controls, environment);
            for (int sample = 0; sample < 128; ++sample)
                channelDifference = juce::jmax(channelDifference, std::abs(
                    block.getSample(0, sample) - block.getSample(1, sample)));
            centeredEnergy += block.getMagnitude(0, 0, 128);
        }
        expect(channelDifference < 0.000001f);
        expect(centeredEnergy > 0.1f);
        Module stereo;
        stereo.prepare(spec);
        expect(stereo.loadImpulseResponse(stereoImpulse.getFile()).wasOk());
        float stereoDifference = 0.0f;
        for (int index = 0; index < 100; ++index)
        {
            fillSine(block, index * 128, 1.0f);
            stereo.process(block, controls, environment);
            for (int sample = 0; sample < 128; ++sample)
                stereoDifference = juce::jmax(stereoDifference, std::abs(
                    block.getSample(0, sample) - block.getSample(1, sample)));
        }
        expect(stereoDifference > 0.01f);

        beginTest("One-channel fallback keeps both prepared convolution channels coherent");
        Module fallback, duplicated;
        fallback.prepare(spec);
        duplicated.prepare(spec);
        expect(fallback.loadImpulseResponse(stereoImpulse.getFile()).wasOk());
        expect(duplicated.loadImpulseResponse(stereoImpulse.getFile()).wasOk());
        juce::AudioBuffer<float> mono(1, 128);
        float fallbackError = 0.0f;
        for (int index = 0; index < 150; ++index)
        {
            fillSine(block, index * 128, 1.0f);
            mono.copyFrom(0, 0, block, 0, 0, 128);
            if (index < 80)
                fallback.process(mono, controls, environment);
            else
                fallback.process(block, controls, environment);
            juce::AudioBuffer<float> reference(2, 128);
            fillSine(reference, index * 128, 1.0f);
            duplicated.process(reference, controls, environment);
            for (int channel = 0; channel < (index < 80 ? 1 : 2); ++channel)
                for (int sample = 0; sample < 128; ++sample)
                    fallbackError = juce::jmax(fallbackError, std::abs(
                        (index < 80 ? mono : block).getSample(channel, sample)
                        - reference.getSample(channel, sample)));
        }
        expect(fallbackError < 0.000002f, juce::String(fallbackError));

        beginTest("Automation, crossed thresholds, sample rates and oversized/mono buffers stay finite");
        for (const auto rate : { 22050.0, 44100.0, 48000.0, 96000.0 })
            for (const auto channels : { 1, 2 })
            {
                Module automated;
                automated.prepare({ rate, 128, static_cast<juce::uint32>(channels) });
                expect(automated.loadImpulseResponse(impulse.getFile()).wasOk());
                juce::AudioBuffer<float> audio(channels, 2049);
                auto values = moduleDefaults(ModuleType::envelopeConvolution);
                bool finite = true;
                float maximumStep = 0.0f;
                std::array<float, 2> previous {};
                for (int index = 0; index < 16; ++index)
                {
                    for (int control = 0; control < Module::activeControlCount; ++control)
                        values[static_cast<size_t>(control)] =
                            (index + control) % 2 == 0 ? 0.0f : 1.0f;
                    if (index == 8)
                        values[Module::quietLevelControl] = values[Module::loudLevelControl] = 1.0f;
                    fillSine(audio, index * audio.getNumSamples());
                    automated.process(audio, values, environment);
                    for (int channel = 0; channel < channels; ++channel)
                        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
                        {
                            const auto value = audio.getSample(channel, sample);
                            finite = finite && std::isfinite(value) && std::abs(value) < 8.0f;
                            maximumStep = juce::jmax(maximumStep, std::abs(
                                value - previous[static_cast<size_t>(channel)]));
                            previous[static_cast<size_t>(channel)] = value;
                        }
                }
                expect(finite);
                expect(maximumStep < 0.3f, juce::String(maximumStep));
            }

        beginTest("Nonfinite input/controls cannot poison audio and telemetry is capture-gated");
        Module guarded;
        guarded.prepare(spec);
        expect(guarded.loadImpulseResponse(impulse.getFile()).wasOk());
        controls.fill(std::numeric_limits<float>::quiet_NaN());
        block.clear();
        block.setSample(0, 0, std::numeric_limits<float>::quiet_NaN());
        block.setSample(1, 1, std::numeric_limits<float>::infinity());
        guarded.process(block, controls, environment);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < 128; ++sample)
                expect(std::isfinite(block.getSample(channel, sample)));
        const auto sequence = read(guarded).sequence;
        environment.captureTelemetry = false;
        renderLevel(guarded, controls, 0.1f, 4800, environment);
        expectEquals(read(guarded).sequence, sequence);
        environment.captureTelemetry = true;
        renderLevel(guarded, controls, 0.1f, 4800, environment);
        expect(read(guarded).sequence > sequence);

        runIntegration(impulse.getFile(), stereoImpulse.getFile());
        expect(impulse.deleteTemporaryFile());
        expect(delayedImpulse.deleteTemporaryFile());
        expect(stereoImpulse.deleteTemporaryFile());
        expect(directory.deleteFile());
    }

private:
    bool writeImpulse(const juce::File& file, int channels, int delay)
    {
        std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
        juce::WavAudioFormat format;
        auto writer = format.createWriterFor(stream,
            juce::AudioFormatWriterOptions {}.withSampleRate(48000.0)
                .withNumChannels(channels).withBitsPerSample(24));
        if (writer == nullptr)
            return false;
        juce::AudioBuffer<float> audio(channels, 12288);
        audio.clear();
        for (int channel = 0; channel < channels; ++channel)
        {
            audio.setSample(channel, delay, channel == 0 ? 0.5f : -0.3f);
            audio.setSample(channel, delay + 1536, channel == 0 ? 0.15f : 0.2f);
        }
        return writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
    }

    void fillSine(juce::AudioBuffer<float>& buffer, int offset, float right = 0.35f)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                buffer.setSample(channel, sample,
                    0.05f * (channel == 0 ? 1.0f : right)
                    * std::sin(juce::MathConstants<float>::twoPi
                               * 997.0f * static_cast<float>(offset + sample) / 48000.0f));
    }

    void renderLevel(Module& module, const ControlValues& controls,
                     float level, int samples, const ProcessEnvironment& environment)
    {
        juce::AudioBuffer<float> audio(2, 128);
        for (int offset = 0; offset < samples; offset += 128)
        {
            const auto count = juce::jmin(128, samples - offset);
            audio.setSize(2, count, false, false, true);
            for (int channel = 0; channel < 2; ++channel)
                juce::FloatVectorOperations::fill(audio.getWritePointer(channel), level, count);
            module.process(audio, controls, environment);
        }
    }

    ContinuousTelemetrySnapshot read(const Module& module)
    {
        ContinuousTelemetrySnapshot snapshot;
        expect(module.readContinuousTelemetry(snapshot));
        return snapshot;
    }

    void runIntegration(const juce::File& first, const juce::File& second)
    {
        beginTest("Graph endpoints, reverse undo and every widget size");
        MegaDSPAudioProcessor processor;
        processor.prepareToPlay(48000.0, 128);
        expect(processor.setImpulseResponseRootDirectory(first.getParentDirectory()).wasOk());
        processor.addModule(ModuleType::envelopeConvolution);
        expectEquals(static_cast<int>(processor.getParameters().size()), 114);
        ui::EffectGraph graph(processor, 0, ModuleType::envelopeConvolution);
        expect(processor.rebuildImpulseResponseLibrary().wasOk());
        graph.setCompactAssetFilterForTesting(first.getFileNameWithoutExtension());
        expectEquals(graph.compactImpulseResponseCountForTesting(), 1);
        graph.activateCompactImpulseResponseForTesting(0);
        processor.finishImpulseResponseLoadForTesting();
        expectEquals(processor.impulseResponsePath(0), first.getFullPathName());
        graph.setVisualizationActive(true);
        expect(processor.getVisualizationData().isSlotActive(0));
        for (const auto width : { 280, 420, 620 })
        {
            graph.setSize(width, 250);
            const auto plot = graph.compactEnvelopePlotAreaForTesting();
            graph.setCompactEnvelopeNodeForTesting(0, {
                plot.getX() + plot.getWidth() * 0.25f,
                plot.getBottom() - plot.getHeight() * 0.3f });
            graph.setCompactEnvelopeNodeForTesting(1, {
                plot.getX() + plot.getWidth() * 0.8f,
                plot.getBottom() - plot.getHeight() * 0.85f });
            expectWithinAbsoluteError(graph.value(Module::quietLevelControl), 0.25f, 0.0001f);
            expectWithinAbsoluteError(graph.value(Module::loudLevelControl), 0.8f, 0.0001f);
            graph.reverseCompactEnvelopeSpeedsForTesting();
            expectWithinAbsoluteError(graph.value(Module::quietBlendControl), 0.85f, 0.0001f);
            expect(processor.undo());
            expectWithinAbsoluteError(graph.value(Module::quietBlendControl), 0.3f, 0.0001f);
        }
        expect(graph.keyPressed(juce::KeyPress('r')));
        expect(!graph.keyPressed(juce::KeyPress(
            'z', juce::ModifierKeys::commandModifier, 'z')));
        MegaDSPAudioProcessorEditor editor(processor, 1100, 720);
        for (const auto size : { ui::WidgetSize::small, ui::WidgetSize::medium,
                                 ui::WidgetSize::large })
        {
            editor.setWidgetSizeForTesting(size);
            editor.refreshCardsForTesting();
            expect(editor.cardPresentsAllActiveControlsForTesting());
            expect(editor.cardChildrenFitForTesting());
            expect(editor.cardGraphsAvoidControlsForTesting());
            graph.setSize(editor.cardGraphWidthForTesting(0),
                          editor.cardGraphHeightForTesting(0));
            expect(graph.compactEnvelopePlotAreaForTesting().getHeight() >= 40.0f);
            juce::Image image(juce::Image::ARGB, graph.getWidth(), graph.getHeight(), true);
            juce::Graphics graphics(image);
            graph.paint(graphics);
        }

        beginTest("All IR banks, CPU buffer, state and prepared reordering are retained");
        expect(processor.loadImpulseResponse(0, first).wasOk());
        expect(processor.loadConvolutionImpulseResponseBank(0, 1, second).wasOk());
        expect(processor.selectConvolutionImpulseResponseBank(0, 1));
        expect(processor.setConvolutionWetBufferSamples(0, 512).wasOk());
        expectEquals(processor.activeConvolutionImpulseResponseBank(0), 1);
        expectEquals(processor.convolutionWetBufferSamples(0), 512);
        auto* instance = processor.getRack().activeModuleInstance(0);
        processor.addModule(ModuleType::envelopeModulator);
        processor.moveSlot(0, 1);
        expect(processor.getRack().activeModuleInstance(1) == instance);
        expectEquals(processor.convolutionImpulseResponseBankPath(1, 0), first.getFullPathName());
        expectEquals(processor.convolutionImpulseResponseBankPath(1, 1), second.getFullPathName());
        juce::MemoryBlock state;
        processor.getStateInformation(state);
        MegaDSPAudioProcessor restored;
        restored.prepareToPlay(48000.0, 128);
        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        restored.flushPendingUpdatesForTesting();
        expectEquals(static_cast<int>(restored.getRack().moduleType(1)), 61);
        expectEquals(restored.activeConvolutionImpulseResponseBank(1), 1);
        expectEquals(restored.convolutionWetBufferSamples(1), 512);
        expectEquals(restored.convolutionImpulseResponseBankPath(1, 0), first.getFullPathName());
        expectEquals(restored.convolutionImpulseResponseBankPath(1, 1), second.getFullPathName());

        beginTest("Async IR loading commits into the active bank and is undoable");
        bool finished = false;
        auto result = juce::Result::fail("Callback was not invoked.");
        expect(restored.loadImpulseResponseAsync(1, 0, first,
            [&](const juce::Result& loaded) { result = loaded; finished = true; }).wasOk());
        expectEquals(restored.convolutionImpulseResponseBankPath(1, 1), second.getFullPathName());
        expect(restored.getRack().activeModuleInstance(1)
                   ->impulseResponseCapability()->hasImpulseResponse());
        restored.finishImpulseResponseLoadForTesting();
        expect(finished);
        expect(result.wasOk(), result.getErrorMessage());
        expectEquals(restored.convolutionImpulseResponseBankPath(1, 1), first.getFullPathName());
        expect(restored.undo());
        expectEquals(restored.convolutionImpulseResponseBankPath(1, 1), second.getFullPathName());
    }
};

EnvelopeConvolutionTests envelopeConvolutionTests;
} // namespace
} // namespace megadsp
