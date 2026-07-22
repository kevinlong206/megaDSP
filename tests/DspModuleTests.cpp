#include "DspModules.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <limits>

namespace megadsp
{
namespace
{
class DspSanityTests final : public juce::UnitTest
{
public:
    DspSanityTests() : juce::UnitTest("DSP sanity", "megaDSP") {}

    void runTest() override
    {
        beginTest("All modules keep silence finite");
        juce::dsp::ProcessSpec spec { 48000.0, 512, 2 };
        ControlValues controls;
        controls.fill(0.5f);
        ProcessEnvironment environment;
        std::array<std::unique_ptr<DspModule>, 28> modules {
            std::make_unique<EqualizerModule>(),
            std::make_unique<CompressorModule>(),
            std::make_unique<SaturatorModule>(),
            std::make_unique<DelayModule>(),
            std::make_unique<StereoWidthModule>(),
            std::make_unique<MidSideDecoderModule>(),
            std::make_unique<TremoloModule>(),
            std::make_unique<RotarySpeakerModule>(),
            std::make_unique<LimiterModule>(),
            std::make_unique<AlgorithmicReverbModule>(),
            std::make_unique<ConvolutionReverbModule>(),
            std::make_unique<DynamicEqualizerModule>(),
            std::make_unique<RandomGranulizerModule>(),
            std::make_unique<VintageChorusModule>(),
            std::make_unique<BeatPermuterModule>(),
            std::make_unique<SpectralPrismModule>(),
            std::make_unique<ResonantMatrixModule>(),
            std::make_unique<WavefoldGardenModule>(),
            std::make_unique<GateExpanderModule>(),
            std::make_unique<TransientDesignerModule>(),
            std::make_unique<MultibandCompressorModule>(),
            std::make_unique<StudioPhaserModule>(),
            std::make_unique<StudioFlangerModule>(),
            std::make_unique<DiffusionDelayModule>(),
            std::make_unique<PitchBloomModule>(),
            std::make_unique<FrequencyLabModule>(),
            std::make_unique<SpatialOrbitModule>(),
            std::make_unique<SignalDecayModule>()
        };

        for (auto& module : modules)
        {
            juce::AudioBuffer<float> buffer(2, 512);
            buffer.clear();
            module->prepare(spec);
            module->process(buffer, controls, environment);
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                    expect(std::isfinite(buffer.getSample(channel, sample)));
        }

        auto sineRms = [](const juce::AudioBuffer<float>& buffer, int channel,
                          int startSample)
        {
            double sum = 0.0;
            const auto count = buffer.getNumSamples() - startSample;
            for (int sample = startSample; sample < buffer.getNumSamples(); ++sample)
            {
                const auto value = buffer.getSample(channel, sample);
                sum += static_cast<double>(value) * value;
            }
            return static_cast<float>(std::sqrt(sum / juce::jmax(1, count)));
        };
        auto makeSine = [](int channels, int samples, float frequency,
                           float amplitude)
        {
            juce::AudioBuffer<float> result(channels, samples);
            for (int sample = 0; sample < samples; ++sample)
            {
                const auto value = amplitude * std::sin(
                    juce::MathConstants<float>::twoPi * frequency
                    * static_cast<float>(sample) / 48000.0f);
                for (int channel = 0; channel < channels; ++channel)
                    result.setSample(channel, sample, value);
            }
            return result;
        };

        auto differenceRms = [](const juce::AudioBuffer<float>& first,
                                const juce::AudioBuffer<float>& second,
                                int startSample)
        {
            double energy = 0.0;
            int count = 0;
            for (int channel = 0; channel < first.getNumChannels(); ++channel)
                for (int sample = startSample; sample < first.getNumSamples(); ++sample)
                {
                    const auto difference = first.getSample(channel, sample)
                                            - second.getSample(channel, sample);
                    energy += static_cast<double>(difference) * difference;
                    ++count;
                }
            return static_cast<float>(std::sqrt(
                energy / static_cast<double>(juce::jmax(1, count))));
        };

        beginTest("Experimental modules preserve their documented dry paths");
        const auto dryReference = makeSine(2, 24000, 731.0f, 0.27f);
        for (const auto type : {
                 ModuleType::beatPermuter,
                 ModuleType::resonantMatrix })
        {
            auto dryControls = moduleDefaults(type);
            dryControls[10] = 0.0f;
            auto rendered = dryReference;
            auto module = createDspModule(type);
            module->prepare(spec);
            module->process(rendered, dryControls, environment);
            expect(differenceRms(rendered, dryReference, 0) < 0.000001f);
        }

        auto expectLatencyAlignedDry = [this, &spec, &dryReference](
                                           ModuleType type)
        {
            auto dryControls = moduleDefaults(type);
            dryControls[10] = 0.0f;
            auto rendered = dryReference;
            auto module = createDspModule(type);
            module->prepare(spec);
            const auto latency = module->latencySamples();
            module->process(rendered, dryControls, {});
            expect(latency > 0);
            for (int channel = 0; channel < rendered.getNumChannels(); ++channel)
            {
                expect(rendered.getMagnitude(channel, 0, latency) < 0.000001f);
                auto maximumDifference = 0.0f;
                for (int sample = latency;
                     sample < rendered.getNumSamples(); ++sample)
                    maximumDifference = juce::jmax(
                        maximumDifference,
                        std::abs(rendered.getSample(channel, sample)
                                 - dryReference.getSample(
                                     channel, sample - latency)));
                expect(maximumDifference < 0.00001f);
            }
        };
        expectLatencyAlignedDry(ModuleType::spectralPrism);
        expectLatencyAlignedDry(ModuleType::wavefoldGarden);

        beginTest("Experimental modules produce distinct bounded wet signals");
        {
            auto beatControls = moduleDefaults(ModuleType::beatPermuter);
            beatControls[0] = discreteValue(5, 6);
            beatControls[1] = 1.0f;
            beatControls[10] = 1.0f;
            BeatPermuterModule module;
            module.prepare(spec);
            auto rendered = makeSine(2, 48000, 317.0f, 0.25f);
            module.process(rendered, beatControls, environment);
            expect(rendered.getMagnitude(0, rendered.getNumSamples()) > 0.01f);
            expect(rendered.getMagnitude(0, rendered.getNumSamples()) < 4.0f);
            const auto events = module.visualEvents();
            expect(std::any_of(
                events.begin(), events.end(),
                [](const auto& event) { return event.sequence != 0; }));
        }
        {
            auto slowBeatControls =
                moduleDefaults(ModuleType::beatPermuter);
            slowBeatControls[0] = discreteValue(0, 6);
            slowBeatControls[1] = 1.0f;
            slowBeatControls[2] = discreteValue(1, 4);
            slowBeatControls[3] = 1.0f;
            slowBeatControls[4] = 1.0f;
            slowBeatControls[5] = 1.0f;
            slowBeatControls[10] = 1.0f;
            BeatPermuterModule module;
            module.prepare(spec);
            auto minimumLateBlockRms =
                std::numeric_limits<float>::max();
            for (int blockIndex = 0; blockIndex < 3600; ++blockIndex)
            {
                auto block = makeSine(2, 512, 223.0f, 0.24f);
                module.process(block, slowBeatControls, { nullptr, 30.0 });
                if (blockIndex >= 2000)
                    minimumLateBlockRms = juce::jmin(
                        minimumLateBlockRms, sineRms(block, 0, 0));
            }
            expect(minimumLateBlockRms > 0.001f);
        }
        {
            auto prismControls = moduleDefaults(ModuleType::spectralPrism);
            prismControls[0] = 0.85f;
            prismControls[2] = 0.70f;
            prismControls[10] = 1.0f;
            SpectralPrismModule module;
            module.prepare(spec);
            auto rendered = makeSine(2, 48000, 523.25f, 0.25f);
            module.process(rendered, prismControls, environment);
            expect(rendered.getMagnitude(0, rendered.getNumSamples()) > 0.001f);
            expect(rendered.getMagnitude(0, rendered.getNumSamples()) < 4.0f);
        }
        {
            auto matrixControls = moduleDefaults(ModuleType::resonantMatrix);
            matrixControls[10] = 1.0f;
            ResonantMatrixModule module;
            module.prepare(spec);
            juce::AudioBuffer<float> rendered(2, 48000);
            rendered.clear();
            rendered.setSample(0, 0, 1.0f);
            rendered.setSample(1, 0, 1.0f);
            module.process(rendered, matrixControls, environment);
            expect(rendered.getMagnitude(0, rendered.getNumSamples()) > 0.001f);
            expect(rendered.getMagnitude(0, rendered.getNumSamples()) < 4.0f);
            expect(module.tailSeconds(matrixControls) > 2.5);
        }
        {
            auto foldControls = moduleDefaults(ModuleType::wavefoldGarden);
            foldControls[1] = 0.8f;
            foldControls[10] = 1.0f;
            WavefoldGardenModule module;
            module.prepare(spec);
            auto rendered = makeSine(2, 48000, 659.25f, 0.35f);
            module.process(rendered, foldControls, environment);
            expect(rendered.getMagnitude(0, rendered.getNumSamples()) > 0.001f);
            expect(rendered.getMagnitude(0, rendered.getNumSamples()) < 4.0f);
        }

        beginTest("Experimental stereo fields cancel on duplicated-mono fold-down");
        for (const auto type : {
                 ModuleType::beatPermuter,
                 ModuleType::spectralPrism,
                 ModuleType::resonantMatrix,
                 ModuleType::wavefoldGarden })
        {
            auto foldDownControls = moduleDefaults(type);
            foldDownControls[10] = 1.0f;
            if (type == ModuleType::beatPermuter)
            {
                foldDownControls[0] = discreteValue(5, 6);
                foldDownControls[1] = 1.0f;
                foldDownControls[8] = 1.0f;
            }
            else if (type == ModuleType::spectralPrism)
                foldDownControls[8] = 1.0f;
            else
                foldDownControls[9] = 1.0f;

            auto mono = makeSine(1, 24000, 389.0f, 0.22f);
            auto stereo = makeSine(2, 24000, 389.0f, 0.22f);
            auto monoModule = createDspModule(type);
            auto stereoModule = createDspModule(type);
            monoModule->prepare({ 48000.0, 512, 1 });
            stereoModule->prepare(spec);
            monoModule->process(mono, foldDownControls, environment);
            stereoModule->process(stereo, foldDownControls, environment);
            auto maximumFoldDifference = 0.0f;
            for (int sample = 4096; sample < mono.getNumSamples(); ++sample)
                maximumFoldDifference = juce::jmax(
                    maximumFoldDifference,
                    std::abs(mono.getSample(0, sample)
                             - 0.5f * (stereo.getSample(0, sample)
                                       + stereo.getSample(1, sample))));
            expect(maximumFoldDifference < 0.0005f,
                   juce::String(moduleDefinition(type).displayName)
                       + " fold-down error "
                       + juce::String(maximumFoldDifference, 6));
        }

        beginTest("Experimental automation steps stay finite and bounded");
        for (const auto type : {
                 ModuleType::beatPermuter,
                 ModuleType::spectralPrism,
                 ModuleType::resonantMatrix,
                 ModuleType::wavefoldGarden })
        {
            auto module = createDspModule(type);
            module->prepare(spec);
            auto automated = makeSine(2, 32768, 271.0f, 0.25f);
            for (int offset = 0; offset < automated.getNumSamples();
                 offset += 127)
            {
                const auto blockSamples = juce::jmin(
                    127, automated.getNumSamples() - offset);
                std::array<float*, 2> pointers {
                    automated.getWritePointer(0, offset),
                    automated.getWritePointer(1, offset)
                };
                juce::AudioBuffer<float> block(
                    pointers.data(), 2, blockSamples);
                ControlValues automatedControls;
                automatedControls.fill(
                    (offset / 127) % 2 == 0 ? 0.0f : 1.0f);
                automatedControls[10] = 1.0f;
                automatedControls[11] = 0.6f;
                module->process(block, automatedControls, { nullptr, 37.0 });
            }
            expect(automated.getMagnitude(0, automated.getNumSamples()) < 16.0f,
                   moduleDefinition(type).displayName);
            for (int channel = 0; channel < automated.getNumChannels(); ++channel)
                for (int sample = 0; sample < automated.getNumSamples(); ++sample)
                    expect(std::isfinite(automated.getSample(channel, sample)));
        }

        beginTest("Random Granulizer dry mix is exact apart from Output");
        auto grainControls = moduleDefaults(ModuleType::randomGranulizer);
        grainControls[10] = 0.0f;
        grainControls[11] = 0.6f;
        auto grainDry = makeSine(2, 48000, 731.0f, 0.37f);
        const auto grainDryReference = grainDry;
        RandomGranulizerModule granulizer;
        granulizer.prepare(spec);
        granulizer.process(grainDry, grainControls, environment);
        expect(differenceRms(grainDry, grainDryReference, 0) < 0.000001f);

        beginTest("Random Granulizer emits audible bounded grains");
        granulizer.reset();
        grainControls = moduleDefaults(ModuleType::randomGranulizer);
        grainControls[7] = 0.0f;
        grainControls[10] = 1.0f;
        auto audibleGrains = makeSine(2, 96000, 523.25f, 0.35f);
        granulizer.process(audibleGrains, grainControls, environment);
        expect(sineRms(audibleGrains, 0, 24000) > 0.002f);
        expect(granulizer.maximumObservedVoiceCount() > 0);
        for (int channel = 0; channel < audibleGrains.getNumChannels(); ++channel)
            for (int sample = 0; sample < audibleGrains.getNumSamples(); ++sample)
                expect(std::isfinite(audibleGrains.getSample(channel, sample)));

        auto renderedDurations = [&](ControlValues durationControls)
        {
            RandomGranulizerModule effect;
            effect.prepare(spec);
            durationControls[2] = 1.0f;
            durationControls[7] = 0.0f;
            durationControls[10] = 1.0f;
            auto rendered = makeSine(2, 288000, 487.0f, 0.25f);
            effect.process(rendered, durationControls, environment);
            std::vector<float> durations;
            for (const auto& event : effect.visualEvents())
                if (event.sequence != 0)
                    durations.push_back(event.durationSeconds);
            return durations;
        };

        beginTest("Random Granulizer fixed size windows are exact");
        auto fixedSizeControls = moduleDefaults(ModuleType::randomGranulizer);
        fixedSizeControls[4] = fixedSizeControls[1];
        const auto fixedDurations = renderedDurations(fixedSizeControls);
        expect(!fixedDurations.empty());
        for (const auto duration : fixedDurations)
            expectWithinAbsoluteError(duration, 0.080f, 1.0f / 48000.0f);

        beginTest("Random Granulizer size spread is varied and bounded");
        auto spreadSizeControls = moduleDefaults(ModuleType::randomGranulizer);
        spreadSizeControls[1] = 0.0f;
        spreadSizeControls[4] = 1.0f;
        const auto spreadDurations = renderedDurations(spreadSizeControls);
        expect(spreadDurations.size() > 1);
        if (!spreadDurations.empty())
        {
            const auto bounds = std::minmax_element(
                spreadDurations.begin(), spreadDurations.end());
            expect(*bounds.first >= 0.050f - 1.0f / 48000.0f);
            expect(*bounds.second <= 2.0f + 1.0f / 48000.0f);
            expect(*bounds.second - *bounds.first > 0.050f);
        }

        beginTest("Random Granulizer accepts 50 ms and 2 second endpoints");
        auto endpointControls = moduleDefaults(ModuleType::randomGranulizer);
        endpointControls[1] = endpointControls[4] = 0.0f;
        const auto minimumDurations = renderedDurations(endpointControls);
        expect(!minimumDurations.empty());
        for (const auto duration : minimumDurations)
            expectWithinAbsoluteError(duration, 0.050f, 1.0f / 48000.0f);
        endpointControls[1] = endpointControls[4] = 1.0f;
        const auto maximumDurations = renderedDurations(endpointControls);
        expect(!maximumDurations.empty());
        for (const auto duration : maximumDurations)
            expectWithinAbsoluteError(duration, 2.0f, 1.0f / 48000.0f);

        beginTest("Random Granulizer crossing size handles stays canonical");
        auto crossedSizeControls = spreadSizeControls;
        crossedSizeControls[1] = 1.0f;
        crossedSizeControls[4] = 0.0f;
        const auto crossedDurations = renderedDurations(crossedSizeControls);
        expectEquals(crossedDurations.size(), spreadDurations.size());
        for (size_t index = 0;
             index < juce::jmin(crossedDurations.size(), spreadDurations.size());
             ++index)
            expectEquals(crossedDurations[index], spreadDurations[index]);

        beginTest("Random Granulizer never exceeds its fixed voice pool");
        granulizer.reset();
        grainControls[0] = 1.0f;
        grainControls[1] = grainControls[4] = 0.3f;
        grainControls[2] = 1.0f;
        grainControls[7] = 0.0f;
        auto denseGrains = makeSine(2, 120000, 311.0f, 0.25f);
        granulizer.process(denseGrains, grainControls, environment);
        expect(granulizer.maximumObservedVoiceCount() <= 16);
        expect(granulizer.maximumObservedVoiceCount() >= 8);
        expect(denseGrains.getMagnitude(0, denseGrains.getNumSamples()) < 8.0f);

        beginTest("Random Granulizer reverse pan and filter are distinct");
        auto renderGrains = [&](ControlValues renderControls)
        {
            RandomGranulizerModule effect;
            effect.prepare(spec);
            renderControls[2] = 1.0f;
            renderControls[7] = 0.0f;
            renderControls[10] = 1.0f;
            auto rendered = makeSine(2, 72000, 997.0f, 0.3f);
            for (int sample = 0; sample < rendered.getNumSamples(); ++sample)
                rendered.setSample(
                    1, sample,
                    0.17f * std::sin(
                        juce::MathConstants<float>::twoPi * 421.0f
                        * static_cast<float>(sample) / 48000.0f));
            effect.process(rendered, renderControls, environment);
            return rendered;
        };
        auto neutralGrainControls = moduleDefaults(ModuleType::randomGranulizer);
        neutralGrainControls[5] = 0.0f;
        neutralGrainControls[6] = 0.0f;
        neutralGrainControls[8] = 1.0f;
        const auto neutralGrains = renderGrains(neutralGrainControls);
        auto changedGrainControls = neutralGrainControls;
        changedGrainControls[5] = 1.0f;
        expect(differenceRms(
                   neutralGrains, renderGrains(changedGrainControls), 16000)
               > 0.001f);
        changedGrainControls = neutralGrainControls;
        changedGrainControls[6] = 1.0f;
        expect(differenceRms(
                   neutralGrains, renderGrains(changedGrainControls), 16000)
               > 0.001f);
        changedGrainControls = neutralGrainControls;
        changedGrainControls[8] = 0.0f;
        expect(differenceRms(
                   neutralGrains, renderGrains(changedGrainControls), 16000)
               > 0.001f);

        beginTest("Random Granulizer reset is deterministic and mono safe");
        granulizer.prepare({ 48000.0, 512, 1 });
        grainControls = moduleDefaults(ModuleType::randomGranulizer);
        grainControls[2] = 1.0f;
        grainControls[7] = 0.0f;
        auto monoFirst = makeSine(1, 72000, 659.25f, 0.3f);
        granulizer.process(monoFirst, grainControls, environment);
        const auto firstEvents = granulizer.visualEvents();
        granulizer.reset();
        auto monoSecond = makeSine(1, 72000, 659.25f, 0.3f);
        granulizer.process(monoSecond, grainControls, environment);
        const auto secondEvents = granulizer.visualEvents();
        expect(differenceRms(monoFirst, monoSecond, 0) == 0.0f);
        for (size_t index = 0; index < firstEvents.size(); ++index)
        {
            expectEquals(static_cast<int>(firstEvents[index].sequence),
                         static_cast<int>(secondEvents[index].sequence));
            expectEquals(firstEvents[index].durationSeconds,
                         secondEvents[index].durationSeconds);
        }
        expect(monoFirst.getMagnitude(0, monoFirst.getNumSamples()) < 4.0f);

        beginTest("Random Granulizer automation and feedback stay bounded");
        granulizer.prepare(spec);
        auto grainAutomated = makeSine(2, 96000, 233.0f, 0.45f);
        for (int offset = 0; offset < grainAutomated.getNumSamples(); offset += 128)
        {
            const auto samples = juce::jmin(
                128, grainAutomated.getNumSamples() - offset);
            std::array<float*, 2> pointers {
                grainAutomated.getWritePointer(0, offset),
                grainAutomated.getWritePointer(1, offset)
            };
            juce::AudioBuffer<float> block(pointers.data(), 2, samples);
            grainControls.fill((offset / 128) % 2 == 0 ? 0.0f : 1.0f);
            grainControls[9] = 1.0f;
            grainControls[10] = 1.0f;
            grainControls[11] = 0.6f;
            granulizer.process(block, grainControls, { nullptr, 37.0 });
        }
        expect(grainAutomated.getMagnitude(
                   0, grainAutomated.getNumSamples()) < 8.0f);
        for (int channel = 0; channel < grainAutomated.getNumChannels(); ++channel)
            for (int sample = 0; sample < grainAutomated.getNumSamples(); ++sample)
                expect(std::isfinite(
                    grainAutomated.getSample(channel, sample)));

        beginTest("Random Granulizer tail covers grain delay and feedback");
        grainControls = moduleDefaults(ModuleType::randomGranulizer);
        const auto defaultTail = granulizer.tailSeconds(grainControls);
        grainControls[1] = 1.0f;
        grainControls[4] = 0.0f;
        grainControls[7] = 1.0f;
        grainControls[9] = 1.0f;
        expect(granulizer.tailSeconds(grainControls) > defaultTail);
        expect(granulizer.tailSeconds(grainControls) >= 14.0);
        expect(granulizer.tailSeconds(grainControls) < 14.2);
        std::swap(grainControls[1], grainControls[4]);
        expectWithinAbsoluteError(
            granulizer.tailSeconds(grainControls), 14.1, 0.001);

        beginTest("Dynamic EQ is transparent at unity ratio and zero range");
        auto dynamicControls = moduleDefaults(ModuleType::dynamicEqualizer);
        dynamicControls[3] = 0.0f;
        dynamicControls[4] = 0.0f;
        auto unityRatioInput = makeSine(2, 48000, 6000.0f, 0.5f);
        auto unityRatioReference = unityRatioInput;
        DynamicEqualizerModule unityRatioDynamicEq;
        unityRatioDynamicEq.prepare(spec);
        unityRatioDynamicEq.process(unityRatioInput, dynamicControls, environment);
        float maximumDifference = 0.0f;
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < unityRatioInput.getNumSamples(); ++sample)
                maximumDifference = juce::jmax(
                    maximumDifference,
                    std::abs(unityRatioInput.getSample(channel, sample)
                             - unityRatioReference.getSample(channel, sample)));
        expect(maximumDifference < 0.00001f);
        dynamicControls = moduleDefaults(ModuleType::dynamicEqualizer);
        dynamicControls[2] = 18.0f / 30.0f;
        dynamicControls[3] = 0.0f;
        DynamicEqualizerModule zeroRangeDynamicEq;
        zeroRangeDynamicEq.prepare(spec);
        auto zeroRangeInput = unityRatioReference;
        zeroRangeDynamicEq.process(zeroRangeInput, dynamicControls, environment);
        maximumDifference = 0.0f;
        for (int sample = 0; sample < zeroRangeInput.getNumSamples(); ++sample)
            maximumDifference = juce::jmax(
                maximumDifference,
                std::abs(zeroRangeInput.getSample(0, sample)
                         - unityRatioReference.getSample(0, sample)));
        expect(maximumDifference < 0.00001f);

        beginTest("Dynamic EQ cuts only the focused band and respects Range");
        dynamicControls = moduleDefaults(ModuleType::dynamicEqualizer);
        dynamicControls[0] = std::log(6000.0f / 20.0f) / std::log(1000.0f);
        dynamicControls[2] = (-6.0f + 18.0f) / 30.0f;
        dynamicControls[3] = (-42.0f + 60.0f) / 60.0f;
        dynamicControls[4] = 1.0f;
        dynamicControls[5] = 0.0f;
        auto focusedCut = makeSine(2, 96000, 6000.0f, 0.5f);
        DynamicEqualizerModule cutDynamicEq;
        cutDynamicEq.prepare(spec);
        cutDynamicEq.process(focusedCut, dynamicControls, environment);
        const auto focusedGain = sineRms(focusedCut, 0, 48000)
                                 / (0.5f / std::sqrt(2.0f));
        expect(focusedGain < 0.58f && focusedGain > 0.47f,
               "Focused gain: " + juce::String(focusedGain, 3));
        expect(cutDynamicEq.meterValue() <= 6.001f);
        auto distantCut = makeSine(2, 96000, 500.0f, 0.5f);
        DynamicEqualizerModule distantDynamicEq;
        distantDynamicEq.prepare(spec);
        distantDynamicEq.process(distantCut, dynamicControls, environment);
        const auto distantGain = sineRms(distantCut, 0, 48000)
                                 / (0.5f / std::sqrt(2.0f));
        expect(distantGain > 0.95f,
               "Distant gain: " + juce::String(distantGain, 3));

        beginTest("Dynamic EQ supports bounded positive Range");
        dynamicControls[2] = (6.0f + 18.0f) / 30.0f;
        auto focusedBoost = makeSine(2, 96000, 6000.0f, 0.25f);
        DynamicEqualizerModule boostDynamicEq;
        boostDynamicEq.prepare(spec);
        boostDynamicEq.process(focusedBoost, dynamicControls, environment);
        const auto boostGain = sineRms(focusedBoost, 0, 48000)
                               / (0.25f / std::sqrt(2.0f));
        expect(boostGain > 1.75f && boostGain < 2.05f,
               "Boost gain: " + juce::String(boostGain, 3));
        expect(boostDynamicEq.meterValue() <= 6.001f);

        beginTest("Dynamic EQ stereo linking and mono processing stay safe");
        dynamicControls[2] = (-12.0f + 18.0f) / 30.0f;
        dynamicControls[11] = 1.0f;
        juce::AudioBuffer<float> linkedStereo(2, 96000);
        for (int sample = 0; sample < linkedStereo.getNumSamples(); ++sample)
        {
            const auto phase = juce::MathConstants<float>::twoPi * 6000.0f
                               * static_cast<float>(sample) / 48000.0f;
            linkedStereo.setSample(0, sample, 0.5f * std::sin(phase));
            linkedStereo.setSample(1, sample, 0.02f * std::sin(phase));
        }
        auto unlinkedStereo = linkedStereo;
        DynamicEqualizerModule linkedDynamicEq;
        linkedDynamicEq.prepare(spec);
        linkedDynamicEq.process(linkedStereo, dynamicControls, environment);
        dynamicControls[11] = 0.0f;
        DynamicEqualizerModule unlinkedDynamicEq;
        unlinkedDynamicEq.prepare(spec);
        unlinkedDynamicEq.process(unlinkedStereo, dynamicControls, environment);
        expect(sineRms(linkedStereo, 1, 48000)
               < sineRms(unlinkedStereo, 1, 48000) * 0.7f);
        DynamicEqualizerModule monoDynamicEq;
        monoDynamicEq.prepare({ 48000.0, 512, 1 });
        auto mono = makeSine(1, 48000, 6000.0f, 0.5f);
        monoDynamicEq.process(mono, dynamicControls, environment);
        expect(std::isfinite(mono.getMagnitude(0, mono.getNumSamples())));

        beginTest("Dynamic EQ Listen auditions the focused detector");
        dynamicControls = moduleDefaults(ModuleType::dynamicEqualizer);
        dynamicControls[10] = 1.0f;
        auto listenedFocused = makeSine(1, 48000, 6000.0f, 0.25f);
        DynamicEqualizerModule focusListener;
        focusListener.prepare({ 48000.0, 512, 1 });
        focusListener.process(listenedFocused, dynamicControls, environment);
        auto listenedDistant = makeSine(1, 48000, 300.0f, 0.25f);
        DynamicEqualizerModule distantListener;
        distantListener.prepare({ 48000.0, 512, 1 });
        distantListener.process(listenedDistant, dynamicControls, environment);
        expect(sineRms(listenedFocused, 0, 24000)
               > sineRms(listenedDistant, 0, 24000) * 20.0f);
        expect(listenedFocused.getMagnitude(0, listenedFocused.getNumSamples())
               <= 4.0f);

        beginTest("Unavailable Dynamic EQ sidechain falls back to input");
        dynamicControls[10] = 0.0f;
        dynamicControls[9] = 0.0f;
        auto internalDetection = makeSine(2, 48000, 6000.0f, 0.4f);
        auto unavailableDetection = internalDetection;
        DynamicEqualizerModule internalDynamicEq;
        DynamicEqualizerModule fallbackDynamicEq;
        internalDynamicEq.prepare(spec);
        fallbackDynamicEq.prepare(spec);
        internalDynamicEq.process(
            internalDetection, dynamicControls, environment);
        dynamicControls[9] = 1.0f;
        fallbackDynamicEq.process(
            unavailableDetection, dynamicControls, environment);
        expectWithinAbsoluteError(
            internalDetection.getSample(0, 47999),
            unavailableDetection.getSample(0, 47999), 0.000001f);

        beginTest("Dynamic EQ automation remains finite");
        DynamicEqualizerModule automatedDynamicEq;
        automatedDynamicEq.prepare(spec);
        juce::AudioBuffer<float> dynamicAutomation(2, 512);
        float dynamicPrevious = 0.0f;
        float dynamicMaximumJump = 0.0f;
        for (int block = 0; block < 120; ++block)
        {
            for (int sample = 0; sample < 512; ++sample)
            {
                const auto value = 0.3f * std::sin(
                    juce::MathConstants<float>::twoPi * 997.0f
                    * static_cast<float>(block * 512 + sample) / 48000.0f);
                dynamicAutomation.setSample(0, sample, value);
                dynamicAutomation.setSample(1, sample, -value * 0.7f);
            }
            dynamicControls = moduleDefaults(ModuleType::dynamicEqualizer);
            dynamicControls[0] = (block & 1) == 0 ? 0.0f : 1.0f;
            dynamicControls[1] = (block & 2) == 0 ? 0.0f : 1.0f;
            dynamicControls[2] = (block & 4) == 0 ? 0.0f : 1.0f;
            dynamicControls[3] = (block & 8) == 0 ? 0.0f : 1.0f;
            dynamicControls[7] = discreteValue(block % 3, 3);
            dynamicControls[8] = discreteValue(block % 2, 2);
            dynamicControls[10] = (block & 16) == 0 ? 0.0f : 1.0f;
            dynamicControls[11] = (block & 32) == 0 ? 0.0f : 1.0f;
            automatedDynamicEq.process(
                dynamicAutomation, dynamicControls, environment);
            for (int sample = 0; sample < 512; ++sample)
            {
                const auto output = dynamicAutomation.getSample(0, sample);
                expect(std::isfinite(output));
                dynamicMaximumJump = juce::jmax(
                    dynamicMaximumJump, std::abs(output - dynamicPrevious));
                dynamicPrevious = output;
            }
        }
        expect(dynamicMaximumJump < 4.0f,
               "Maximum automation jump: "
                   + juce::String(dynamicMaximumJump, 3));

        beginTest("Saturator smart compensation prevents drive loudness jumps");
        SaturatorModule compensatedSaturator;
        compensatedSaturator.prepare(spec);
        controls = moduleDefaults(ModuleType::saturator);
        controls[0] = 1.0f;
        controls[1] = 1.0f;
        controls[2] = 0.5f;
        controls[3] = 0.8f;
        controls[4] = 1.0f;
        juce::AudioBuffer<float> saturationBlock(2, 512);
        double inputEnergy = 0.0;
        double outputEnergy = 0.0;
        int measuredSamples = 0;
        int saturationPhaseSample = 0;
        const auto inputGain = juce::Decibels::decibelsToGain(-24.0f);
        for (int block = 0; block < 300; ++block)
        {
            for (int sample = 0; sample < saturationBlock.getNumSamples();
                 ++sample, ++saturationPhaseSample)
            {
                const auto input = inputGain * std::sin(
                    juce::MathConstants<float>::twoPi * 997.0f
                    * static_cast<float>(saturationPhaseSample) / 48000.0f);
                saturationBlock.setSample(0, sample, input);
                saturationBlock.setSample(1, sample, input);
                if (block >= 150)
                    inputEnergy += static_cast<double>(input) * input;
            }
            compensatedSaturator.process(
                saturationBlock, controls, environment);
            if (block >= 150)
            {
                for (int sample = 0; sample < saturationBlock.getNumSamples();
                     ++sample)
                {
                    const auto outputSample =
                        saturationBlock.getSample(0, sample);
                    outputEnergy += static_cast<double>(outputSample)
                                    * outputSample;
                    ++measuredSamples;
                }
            }
        }
        const auto inputRms = std::sqrt(
            inputEnergy / static_cast<double>(measuredSamples));
        const auto outputRms = std::sqrt(
            outputEnergy / static_cast<double>(measuredSamples));
        const auto levelDifferenceDb = juce::Decibels::gainToDecibels(
            static_cast<float>(outputRms / inputRms));
        expect(std::abs(levelDifferenceDb) < 1.0f,
               "Compensated level difference: "
                   + juce::String(levelDifferenceDb, 2) + " dB");

        beginTest("Convolution passes dry signal until an IR is loaded");
        ConvolutionReverbModule convolution;
        convolution.prepare(spec);
        controls = moduleDefaults(ModuleType::convolutionReverb);
        juce::AudioBuffer<float> unloaded(2, 512);
        for (int channel = 0; channel < unloaded.getNumChannels(); ++channel)
            for (int sample = 0; sample < unloaded.getNumSamples(); ++sample)
                unloaded.setSample(
                    channel, sample,
                    0.2f * std::sin(
                        juce::MathConstants<float>::twoPi * 440.0f
                        * static_cast<float>(sample) / 48000.0f));
        auto unloadedInput = unloaded;
        convolution.process(unloaded, controls, environment);
        for (int channel = 0; channel < unloaded.getNumChannels(); ++channel)
            for (int sample = 0; sample < unloaded.getNumSamples(); ++sample)
                expectWithinAbsoluteError(
                    unloaded.getSample(channel, sample),
                    unloadedInput.getSample(channel, sample), 0.000001f);

        beginTest("Unloaded convolution applies Dry and Output without fake wet");
        controls[2] = 1.0f;
        controls[3] = 2.0f / 3.0f;
        controls[4] = 0.25f;
        const auto unloadedGain =
            0.25f * juce::Decibels::decibelsToGain(6.0f);
        auto fillUnloaded = [&unloaded]
        {
            for (int channel = 0; channel < unloaded.getNumChannels(); ++channel)
                for (int sample = 0; sample < unloaded.getNumSamples(); ++sample)
                    unloaded.setSample(channel, sample, 0.2f);
        };
        for (int block = 0; block < 4; ++block)
        {
            fillUnloaded();
            convolution.process(unloaded, controls, environment);
        }
        for (int channel = 0; channel < unloaded.getNumChannels(); ++channel)
            for (int sample = 0; sample < unloaded.getNumSamples(); ++sample)
                expectWithinAbsoluteError(
                    unloaded.getSample(channel, sample),
                    0.2f * unloadedGain, 0.00001f);
        controls[4] = 0.0f;
        for (int block = 0; block < 4; ++block)
        {
            fillUnloaded();
            convolution.process(unloaded, controls, environment);
        }
        expect(unloaded.getMagnitude(0, unloaded.getNumSamples())
               < 0.000001f);

        beginTest("Convolution loads and previews an audio-file IR");
        const auto impulseFile =
            juce::File::getCurrentWorkingDirectory()
                .getNonexistentChildFile(
                    "megadsp-test-ir", ".wav");
        {
            std::unique_ptr<juce::OutputStream> stream =
                impulseFile.createOutputStream();
            juce::WavAudioFormat format;
            const auto options = juce::AudioFormatWriterOptions {}
                .withSampleRate(48000.0)
                .withNumChannels(2)
                .withBitsPerSample(24);
            auto writer = format.createWriterFor(stream, options);
            expect(writer != nullptr);
            juce::AudioBuffer<float> impulse(2, 4096);
            impulse.clear();
            impulse.setSample(0, 0, 1.0f);
            impulse.setSample(1, 0, 1.0f);
            impulse.setSample(0, 1200, 0.35f);
            impulse.setSample(1, 1800, -0.25f);
            expect(writer != nullptr
                   && writer->writeFromAudioSampleBuffer(
                       impulse, 0, impulse.getNumSamples()));
        }
        const auto loadResult =
            convolution.loadImpulseResponse(impulseFile);
        expect(loadResult.wasOk(), loadResult.getErrorMessage());
        expect(convolution.hasImpulseResponse());
        expect(convolution.tailSeconds(controls) > 0.08);
        expectEquals(convolution.impulseName(),
                     impulseFile.getFileName());
        const auto impulsePreview = convolution.impulsePreview();
        expect(*std::max_element(impulsePreview.begin(),
                                 impulsePreview.end()) > 0.99f);

        for (int attempt = 0;
             attempt < 200 && !convolution.isImpulseReady(); ++attempt)
            juce::Thread::sleep(1);
        expect(convolution.isImpulseReady(),
               "Convolution IR did not become ready.");
        controls = moduleDefaults(ModuleType::convolutionReverb);
        controls[2] = 1.0f;
        controls[4] = 0.0f;
        juce::AudioBuffer<float> convolutionBlock(2, 512);
        for (int block = 0; block < 4; ++block)
        {
            convolutionBlock.clear();
            convolution.process(convolutionBlock, controls, environment);
        }
        convolutionBlock.clear();
        convolutionBlock.setSample(0, 0, 1.0f);
        convolutionBlock.setSample(1, 0, 1.0f);
        convolution.process(convolutionBlock, controls, environment);
        const auto wetOnlyDirect = convolutionBlock.getSample(0, 0);
        float tailPeak = 0.0f;
        for (int block = 0; block < 8; ++block)
        {
            convolutionBlock.clear();
            convolution.process(convolutionBlock, controls, environment);
            tailPeak = juce::jmax(
                tailPeak, convolutionBlock.getMagnitude(
                    0, convolutionBlock.getNumSamples()));
            for (int channel = 0;
                 channel < convolutionBlock.getNumChannels(); ++channel)
                for (int sample = 0;
                     sample < convolutionBlock.getNumSamples(); ++sample)
                    expect(std::isfinite(
                        convolutionBlock.getSample(channel, sample)));
        }
        expect(tailPeak > 0.0001f,
               "Loaded IR did not produce its delayed reflection; peak="
                   + juce::String(tailPeak, 6));

        beginTest("Loaded convolution scales dry and wet independently");
        convolution.reset();
        controls[2] = 1.0f;
        controls[4] = 1.0f;
        for (int block = 0; block < 4; ++block)
        {
            convolutionBlock.clear();
            convolution.process(convolutionBlock, controls, environment);
        }
        convolutionBlock.clear();
        convolutionBlock.setSample(0, 0, 1.0f);
        convolutionBlock.setSample(1, 0, 1.0f);
        convolution.process(convolutionBlock, controls, environment);
        expectWithinAbsoluteError(
            convolutionBlock.getSample(0, 0), wetOnlyDirect + 1.0f, 0.0001f);

        convolution.reset();
        controls[2] = 0.0f;
        controls[4] = 0.40f;
        for (int block = 0; block < 4; ++block)
        {
            convolutionBlock.clear();
            convolution.process(convolutionBlock, controls, environment);
        }
        convolutionBlock.clear();
        convolutionBlock.setSample(0, 0, 1.0f);
        convolutionBlock.setSample(1, 0, 1.0f);
        convolution.process(convolutionBlock, controls, environment);
        expectWithinAbsoluteError(
            convolutionBlock.getSample(0, 0), 0.40f, 0.0001f);

        controls[4] = 0.0f;
        for (int block = 0; block < 4; ++block)
        {
            convolutionBlock.clear();
            convolution.process(convolutionBlock, controls, environment);
        }
        convolutionBlock.clear();
        convolutionBlock.setSample(0, 0, 1.0f);
        convolutionBlock.setSample(1, 0, 1.0f);
        convolution.process(convolutionBlock, controls, environment);
        expect(convolutionBlock.getMagnitude(
                   0, convolutionBlock.getNumSamples()) < 0.000001f);

        beginTest("Convolution gain automation remains finite");
        for (int block = 0; block < 100; ++block)
        {
            for (int sample = 0; sample < convolutionBlock.getNumSamples();
                 ++sample)
            {
                const auto input = 0.2f * std::sin(
                    juce::MathConstants<float>::twoPi * 337.0f
                    * static_cast<float>(
                        block * convolutionBlock.getNumSamples() + sample)
                    / 48000.0f);
                convolutionBlock.setSample(0, sample, input);
                convolutionBlock.setSample(1, sample, input);
            }
            controls[2] = (block & 1) == 0 ? 0.0f : 1.0f;
            controls[4] = (block & 1) == 0 ? 1.0f : 0.0f;
            convolution.process(convolutionBlock, controls, environment);
            for (int channel = 0;
                 channel < convolutionBlock.getNumChannels(); ++channel)
                for (int sample = 0;
                     sample < convolutionBlock.getNumSamples(); ++sample)
                    expect(std::isfinite(
                        convolutionBlock.getSample(channel, sample)));
        }

        ConvolutionReverbModule monoConvolution;
        monoConvolution.prepare({ 48000.0, 512, 1 });
        expect(monoConvolution.loadImpulseResponse(impulseFile).wasOk(),
               "Stereo IR could not be loaded for mono processing.");
        for (int attempt = 0;
             attempt < 200 && !monoConvolution.isImpulseReady(); ++attempt)
            juce::Thread::sleep(1);
        juce::AudioBuffer<float> monoImpulse(1, 512);
        monoImpulse.clear();
        monoImpulse.setSample(0, 0, 1.0f);
        monoConvolution.process(monoImpulse, controls, environment);
        for (int sample = 0; sample < monoImpulse.getNumSamples(); ++sample)
            expect(std::isfinite(monoImpulse.getSample(0, sample)));

        beginTest(
            "Convolution readiness follows pending replacements and clear");
        const auto replacementFile =
            juce::File::getCurrentWorkingDirectory()
                .getNonexistentChildFile(
                    "megadsp-test-ir-replacement", ".wav");
        {
            std::unique_ptr<juce::OutputStream> stream =
                replacementFile.createOutputStream();
            juce::WavAudioFormat format;
            auto writer = format.createWriterFor(
                stream, juce::AudioFormatWriterOptions {}
                            .withSampleRate(48000.0)
                            .withNumChannels(2)
                            .withBitsPerSample(24));
            expect(writer != nullptr);
            juce::AudioBuffer<float> replacement(2, 4096);
            replacement.clear();
            replacement.setSample(0, 0, -1.0f);
            replacement.setSample(1, 0, -1.0f);
            replacement.setSample(0, 1200, -0.35f);
            replacement.setSample(1, 1800, 0.25f);
            expect(writer != nullptr
                   && writer->writeFromAudioSampleBuffer(
                       replacement, 0, replacement.getNumSamples()));
        }

        ConvolutionReverbModule readinessConvolution;
        readinessConvolution.prepare(spec);
        expect(!readinessConvolution.hasImpulseResponse());
        expect(!readinessConvolution.isImpulseReady());
        expect(readinessConvolution.loadImpulseResponse(
                   impulseFile).wasOk());
        expect(readinessConvolution.hasImpulseResponse());
        expect(readinessConvolution.isImpulseReady());

        PreparedImpulseResponsePtr pendingReplacement;
        const auto prepareReplacement =
            readinessConvolution.prepareImpulseResponse(
                replacementFile, pendingReplacement);
        expect(prepareReplacement.wasOk(),
               prepareReplacement.getErrorMessage());
        expect(pendingReplacement != nullptr);
        expect(!readinessConvolution.hasImpulseResponse());
        expect(!readinessConvolution.isImpulseReady());
        expect(readinessConvolution.impulseName().isEmpty());
        expectEquals(
            readinessConvolution.currentImpulseResponseTailSeconds(),
            0.0);

        auto wetOnlyControls =
            moduleDefaults(ModuleType::convolutionReverb);
        wetOnlyControls[2] = 1.0f;
        wetOnlyControls[4] = 0.0f;
        juce::AudioBuffer<float> pendingWetOnly(2, 512);
        for (int block = 0; block < 4; ++block)
        {
            pendingWetOnly.clear();
            readinessConvolution.process(
                pendingWetOnly, wetOnlyControls, environment);
        }
        pendingWetOnly.clear();
        pendingWetOnly.setSample(0, 0, 1.0f);
        pendingWetOnly.setSample(1, 0, 1.0f);
        readinessConvolution.process(
            pendingWetOnly, wetOnlyControls, environment);
        expect(pendingWetOnly.getMagnitude(
                   0, pendingWetOnly.getNumSamples()) < 0.000001f);

        expect(readinessConvolution.commitPreparedImpulseResponse(
            pendingReplacement));
        expect(readinessConvolution.hasImpulseResponse());
        expect(readinessConvolution.isImpulseReady());
        expectEquals(readinessConvolution.impulseName(),
                     replacementFile.getFileName());
        pendingWetOnly.clear();
        pendingWetOnly.setSample(0, 0, 1.0f);
        pendingWetOnly.setSample(1, 0, 1.0f);
        readinessConvolution.process(
            pendingWetOnly, wetOnlyControls, environment);
        expect(pendingWetOnly.findMinMax(
                   0, 0, pendingWetOnly.getNumSamples())
                   .getStart() < -0.01f);

        readinessConvolution.clearImpulseResponse();
        expect(!readinessConvolution.hasImpulseResponse());
        expect(!readinessConvolution.isImpulseReady());
        expect(readinessConvolution.impulseName().isEmpty());
        expectEquals(
            readinessConvolution.currentImpulseResponseTailSeconds(),
            0.0);
        const auto clearedPreview =
            readinessConvolution.impulsePreview();
        expect(std::all_of(
            clearedPreview.begin(), clearedPreview.end(),
            [](float point) { return point == 0.0f; }));
        pendingWetOnly.clear();
        pendingWetOnly.setSample(0, 0, 1.0f);
        pendingWetOnly.setSample(1, 0, 1.0f);
        readinessConvolution.process(
            pendingWetOnly, wetOnlyControls, environment);
        expect(pendingWetOnly.getMagnitude(
                   0, pendingWetOnly.getNumSamples()) < 0.000001f);

        replacementFile.deleteFile();
        impulseFile.deleteFile();

        beginTest("EQ edge zones engage low and high rolloff filters");
        EqualizerModule rolloffEq;
        rolloffEq.prepare(spec);
        controls = moduleDefaults(ModuleType::equalizer);
        controls[0] = 0.0f;
        controls[10] = 1.0f;
        juce::AudioBuffer<float> dcResponse(2, 48000);
        for (int channel = 0; channel < dcResponse.getNumChannels(); ++channel)
            for (int sample = 0; sample < dcResponse.getNumSamples(); ++sample)
                dcResponse.setSample(channel, sample, 1.0f);
        rolloffEq.process(dcResponse, controls, environment);
        expect(std::abs(dcResponse.getSample(0, 47999)) < 0.001f);

        rolloffEq.reset();
        controls = moduleDefaults(ModuleType::equalizer);
        controls[6] = 1.0f;
        controls[11] = 1.0f;
        juce::AudioBuffer<float> highResponse(2, 48000);
        for (int channel = 0; channel < highResponse.getNumChannels(); ++channel)
            for (int sample = 0; sample < highResponse.getNumSamples(); ++sample)
                highResponse.setSample(channel, sample,
                                       (sample & 1) == 0 ? 1.0f : -1.0f);
        rolloffEq.process(highResponse, controls, environment);
        expect(highResponse.getMagnitude(47000, 1000) < 0.05f);

        beginTest("EQ rolloff topology transitions without discontinuity");
        EqualizerModule transitioningEq;
        transitioningEq.prepare(spec);
        controls = moduleDefaults(ModuleType::equalizer);
        controls[0] = 0.080f;
        juce::AudioBuffer<float> beforeRolloff(2, 12000);
        juce::AudioBuffer<float> afterRolloff(2, 12000);
        for (int sample = 0; sample < 24000; ++sample)
        {
            const auto input = 0.1f * std::sin(
                juce::MathConstants<float>::twoPi * 80.0f
                * static_cast<float>(sample) / 48000.0f);
            auto& target = sample < 12000 ? beforeRolloff : afterRolloff;
            const auto targetSample = sample < 12000 ? sample : sample - 12000;
            target.setSample(0, targetSample, input);
            target.setSample(1, targetSample, input);
        }
        transitioningEq.process(beforeRolloff, controls, environment);
        controls[10] = 1.0f;
        transitioningEq.process(afterRolloff, controls, environment);
        expect(std::abs(afterRolloff.getSample(0, 0)
                        - beforeRolloff.getSample(0, 11999)) < 0.05f);
        juce::AudioBuffer<float> automatedEq(2, 256);
        for (int block = 0; block < 64; ++block)
        {
            automatedEq.clear();
            automatedEq.addFrom(0, 0, afterRolloff, 0, 0, 256);
            automatedEq.copyFrom(1, 0, automatedEq, 0, 0, 256);
            controls[10] = block % 2 == 0 ? 1.0f : 0.0f;
            controls[11] = block % 2 == 0 ? 1.0f : 0.0f;
            transitioningEq.process(automatedEq, controls, environment);
            for (int channel = 0; channel < automatedEq.getNumChannels(); ++channel)
                for (int sample = 0; sample < automatedEq.getNumSamples(); ++sample)
                    expect(std::isfinite(
                        automatedEq.getSample(channel, sample)));
        }

        beginTest("Tremolo modes provide distinct artifact-safe modulation");
        TremoloModule tremolo;
        tremolo.prepare(spec);
        controls = moduleDefaults(ModuleType::tremolo);
        controls[0] = discreteValue(0, 3);
        controls[1] = std::log(2.0f / 0.05f) / std::log(20.0f / 0.05f);
        controls[4] = 1.0f;
        juce::AudioBuffer<float> amplitudeTremolo(2, 48000);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < amplitudeTremolo.getNumSamples(); ++sample)
                amplitudeTremolo.setSample(channel, sample, 0.5f);
        tremolo.process(amplitudeTremolo, controls, environment);
        expect(amplitudeTremolo.getMagnitude(0, 48000) > 0.45f);
        float minimumTremolo = 1.0f;
        float channelDifference = 0.0f;
        for (int sample = 12000; sample < 48000; ++sample)
        {
            minimumTremolo = juce::jmin(
                minimumTremolo, amplitudeTremolo.getSample(0, sample));
            channelDifference = juce::jmax(
                channelDifference,
                std::abs(amplitudeTremolo.getSample(0, sample)
                         - amplitudeTremolo.getSample(1, sample)));
        }
        expect(minimumTremolo < 0.05f);
        expect(channelDifference < 0.0001f);

        tremolo.reset();
        controls = moduleDefaults(ModuleType::tremolo);
        controls[0] = discreteValue(1, 3);
        controls[4] = 0.0f;
        juce::AudioBuffer<float> harmonicReconstruction(2, 4096);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < harmonicReconstruction.getNumSamples(); ++sample)
                harmonicReconstruction.setSample(
                    channel, sample, 0.4f * std::sin(
                        juce::MathConstants<float>::twoPi * 997.0f
                        * static_cast<float>(sample) / 48000.0f));
        auto harmonicInput = harmonicReconstruction;
        tremolo.process(harmonicReconstruction, controls, environment);
        float reconstructionError = 0.0f;
        for (int sample = 0; sample < harmonicReconstruction.getNumSamples(); ++sample)
            reconstructionError = juce::jmax(
                reconstructionError,
                std::abs(harmonicReconstruction.getSample(0, sample)
                         - harmonicInput.getSample(0, sample)));
        expect(reconstructionError < 0.0001f);

        tremolo.reset();
        controls = moduleDefaults(ModuleType::tremolo);
        controls[0] = discreteValue(2, 3);
        controls[5] = 0.5f;
        juce::AudioBuffer<float> vibratoSignal(2, 96000);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < vibratoSignal.getNumSamples(); ++sample)
                vibratoSignal.setSample(
                    channel, sample, 0.25f * std::sin(
                        juce::MathConstants<float>::twoPi * 440.0f
                        * static_cast<float>(sample) / 48000.0f));
        tremolo.process(vibratoSignal, controls, environment);
        expect(vibratoSignal.getMagnitude(0, 96000) > 0.15f);
        for (int sample = 0; sample < vibratoSignal.getNumSamples(); ++sample)
            expect(std::isfinite(vibratoSignal.getSample(0, sample)));

        beginTest("Rotary speaker creates stereo motion and remains mono safe");
        RotarySpeakerModule rotary;
        rotary.prepare(spec);
        controls = moduleDefaults(ModuleType::rotarySpeaker);
        controls[0] = discreteValue(2, 3);
        controls[6] = 0.75f;
        juce::AudioBuffer<float> rotaryStereo(2, 96000);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < rotaryStereo.getNumSamples(); ++sample)
                rotaryStereo.setSample(
                    channel, sample, 0.2f * std::sin(
                        juce::MathConstants<float>::twoPi * 330.0f
                        * static_cast<float>(sample) / 48000.0f));
        rotary.process(rotaryStereo, controls, environment);
        float stereoDifference = 0.0f;
        for (int sample = 48000; sample < rotaryStereo.getNumSamples(); ++sample)
        {
            stereoDifference += std::abs(
                rotaryStereo.getSample(0, sample)
                - rotaryStereo.getSample(1, sample));
            expect(std::isfinite(rotaryStereo.getSample(0, sample)));
            expect(std::isfinite(rotaryStereo.getSample(1, sample)));
        }
        expect(stereoDifference / 48000.0f > 0.005f);
        expect(rotaryStereo.getMagnitude(
                   0, 0, rotaryStereo.getNumSamples()) < 2.0f);
        expect(rotaryStereo.getMagnitude(
                   1, 0, rotaryStereo.getNumSamples()) < 2.0f);

        rotary.reset();
        juce::AudioBuffer<float> rotaryMono(1, 4096);
        for (int sample = 0; sample < rotaryMono.getNumSamples(); ++sample)
            rotaryMono.setSample(0, sample, sample == 0 ? 0.5f : 0.0f);
        rotary.process(rotaryMono, controls, environment);
        for (int sample = 0; sample < rotaryMono.getNumSamples(); ++sample)
            expect(std::isfinite(rotaryMono.getSample(0, sample)));

        beginTest("Modulation processors survive rapid automation at supported rates");
        for (const auto rate : { 44100.0, 96000.0, 192000.0 })
        {
            const juce::dsp::ProcessSpec rateSpec { rate, 512, 2 };
            TremoloModule automatedTremolo;
            RotarySpeakerModule automatedRotary;
            automatedTremolo.prepare(rateSpec);
            automatedRotary.prepare(rateSpec);
            for (int block = 0; block < 48; ++block)
            {
                juce::AudioBuffer<float> automated(2, 512);
                for (int sample = 0; sample < automated.getNumSamples(); ++sample)
                {
                    const auto input = 0.2f * std::sin(
                        juce::MathConstants<float>::twoPi * 440.0f
                        * static_cast<float>(block * 512 + sample)
                        / static_cast<float>(rate));
                    automated.setSample(0, sample, input);
                    automated.setSample(1, sample, input);
                }
                controls = moduleDefaults(ModuleType::tremolo);
                controls[0] = discreteValue(block % 3, 3);
                controls[1] = block % 2 == 0 ? 0.0f : 1.0f;
                controls[4] = block % 2 == 0 ? 0.0f : 1.0f;
                controls[5] = block % 2 == 0 ? 0.0f : 1.0f;
                controls[7] = block % 2 == 0 ? 0.0f : 1.0f;
                automatedTremolo.process(automated, controls, environment);
                controls = moduleDefaults(ModuleType::rotarySpeaker);
                controls[0] = discreteValue(block % 3, 3);
                controls[1] = block % 2 == 0 ? 0.0f : 1.0f;
                controls[4] = block % 2 == 0 ? 0.0f : 1.0f;
                controls[5] = block % 2 == 0 ? 0.0f : 1.0f;
                controls[6] = block % 2 == 0 ? 0.0f : 1.0f;
                automatedRotary.process(automated, controls, environment);
                for (int channel = 0; channel < 2; ++channel)
                    for (int sample = 0; sample < automated.getNumSamples(); ++sample)
                        expect(std::isfinite(automated.getSample(channel, sample)));
                const auto leftMagnitude =
                    automated.getMagnitude(0, 0, automated.getNumSamples());
                const auto rightMagnitude =
                    automated.getMagnitude(1, 0, automated.getNumSamples());
                expect(leftMagnitude < 4.0f,
                       "Left magnitude " + juce::String(leftMagnitude, 6)
                           + " at " + juce::String(rate, 0) + " Hz, block "
                           + juce::String(block));
                expect(rightMagnitude < 4.0f,
                       "Right magnitude " + juce::String(rightMagnitude, 6)
                           + " at " + juce::String(rate, 0) + " Hz, block "
                           + juce::String(block));
            }
        }

        beginTest("Limiter respects the configured ceiling");
        LimiterModule limiter;
        limiter.prepare(spec);
        juce::AudioBuffer<float> buffer(2, 1024);
        buffer.clear();
        buffer.setSample(0, 0, 4.0f);
        buffer.setSample(1, 0, -4.0f);
        controls.fill(0.5f);
        controls[1] = 0.5f;
        limiter.process(buffer, controls, environment);
        expect(buffer.getMagnitude(0, buffer.getNumSamples()) <= 1.0f);

        beginTest("Ping-pong delay alternates duplicated mono input");
        DelayModule pingPongDelay;
        pingPongDelay.prepare(spec);
        juce::AudioBuffer<float> pingPongResponse(2, 36000);
        pingPongResponse.clear();
        pingPongResponse.setSample(0, 0, 1.0f);
        pingPongResponse.setSample(1, 0, 1.0f);
        controls.fill(0.5f);
        controls[0] = std::log(250.0f) / std::log(2000.0f);
        controls[1] = 0.65f;
        controls[2] = 1.0f;
        controls[3] = 1.0f;
        controls[4] = 1.0f;
        controls[5] = 0.0f;
        controls[8] = 0.0f;
        pingPongDelay.process(pingPongResponse, controls, environment);
        const auto firstLeft = pingPongResponse.getMagnitude(0, 11990, 30);
        const auto firstRight = pingPongResponse.getMagnitude(1, 11990, 30);
        const auto secondLeft = pingPongResponse.getMagnitude(0, 23980, 40);
        const auto secondRight = pingPongResponse.getMagnitude(1, 23980, 40);
        expect(firstLeft > firstRight * 10.0f,
               "First repeat L/R: " + juce::String(firstLeft, 3) + "/"
                   + juce::String(firstRight, 3));
        expect(secondRight > secondLeft * 10.0f,
               "Second repeat L/R: " + juce::String(secondLeft, 3) + "/"
                   + juce::String(secondRight, 3));

        pingPongDelay.reset();
        pingPongResponse.clear();
        pingPongResponse.setSample(0, 0, 1.0f);
        pingPongDelay.process(pingPongResponse, controls, environment);
        expect(pingPongResponse.getMagnitude(0, 11990, 30) > 0.45f);
        expect(pingPongResponse.getMagnitude(1, 11990, 30) < 0.0001f);
        expect(pingPongResponse.getMagnitude(1, 23980, 40) > 0.05f);

        beginTest("Stereo width creates a mono-compatible side field");
        StereoWidthModule stereoWidth;
        stereoWidth.prepare(spec);
        juce::AudioBuffer<float> widthResponse(2, 12000);
        for (int sample = 0; sample < widthResponse.getNumSamples(); ++sample)
        {
            const auto input = 0.2f * std::sin(
                juce::MathConstants<float>::twoPi * 997.0f
                * static_cast<float>(sample) / 48000.0f);
            widthResponse.setSample(0, sample, input);
            widthResponse.setSample(1, sample, input);
        }
        controls = moduleDefaults(ModuleType::stereoWidth);
        controls[0] = 0.80f;
        controls[1] = 1.0f;
        controls[2] = 0.0f;
        controls[3] = 0.0f;
        controls[7] = 0.0f;
        double sideFieldEnergy = 0.0;
        double widthMonoDifference = 0.0;
        std::vector<float> monoReference(
            static_cast<size_t>(widthResponse.getNumSamples()));
        for (int sample = 0; sample < widthResponse.getNumSamples(); ++sample)
            monoReference[static_cast<size_t>(sample)] =
                widthResponse.getSample(0, sample)
                + widthResponse.getSample(1, sample);
        stereoWidth.process(widthResponse, controls, environment);
        for (int sample = 0; sample < widthResponse.getNumSamples(); ++sample)
        {
            const auto left = widthResponse.getSample(0, sample);
            const auto right = widthResponse.getSample(1, sample);
            const auto side = left - right;
            sideFieldEnergy += static_cast<double>(side) * side;
            widthMonoDifference += std::abs(
                left + right - monoReference[static_cast<size_t>(sample)]);
        }
        expect(sideFieldEnergy > 0.01);
        expect(widthMonoDifference < 0.01,
               "Summed-mono difference: "
                   + juce::String(widthMonoDifference, 6));

        beginTest("Stereo dimension remains mono-compatible off-centre");
        std::array<juce::AudioBuffer<float>, 2> balancedResponses {
            juce::AudioBuffer<float>(2, 12000),
            juce::AudioBuffer<float>(2, 12000)
        };
        for (auto& response : balancedResponses)
            for (int sample = 0; sample < response.getNumSamples(); ++sample)
            {
                response.setSample(0, sample, 0.18f * std::sin(
                    juce::MathConstants<float>::twoPi * 733.0f
                    * static_cast<float>(sample) / 48000.0f));
                response.setSample(1, sample, 0.11f * std::sin(
                    juce::MathConstants<float>::twoPi * 1187.0f
                    * static_cast<float>(sample) / 48000.0f));
            }
        for (int variant = 0; variant < 2; ++variant)
        {
            StereoWidthModule widener;
            widener.prepare(spec);
            controls = moduleDefaults(ModuleType::stereoWidth);
            controls[0] = 0.75f;
            controls[1] = static_cast<float>(variant);
            controls[4] = 0.78f;
            controls[7] = 0.0f;
            widener.process(
                balancedResponses[static_cast<size_t>(variant)],
                controls, environment);
        }
        double balancedMonoDifference = 0.0;
        for (int sample = 0; sample < balancedResponses[0].getNumSamples(); ++sample)
            balancedMonoDifference += std::abs(
                balancedResponses[0].getSample(0, sample)
                    + balancedResponses[0].getSample(1, sample)
                - balancedResponses[1].getSample(0, sample)
                - balancedResponses[1].getSample(1, sample));
        expect(balancedMonoDifference < 0.01,
               "Off-centre mono difference: "
                   + juce::String(balancedMonoDifference, 6));

        beginTest("Stereo width keeps the low-frequency foundation narrower");
        std::array<double, 2> widthBandEnergy {};
        for (int band = 0; band < 2; ++band)
        {
            StereoWidthModule widener;
            widener.prepare(spec);
            juce::AudioBuffer<float> response(2, 48000);
            const auto frequency = band == 0 ? 60.0f : 4000.0f;
            for (int sample = 0; sample < response.getNumSamples(); ++sample)
            {
                const auto input = 0.15f * std::sin(
                    juce::MathConstants<float>::twoPi * frequency
                    * static_cast<float>(sample) / 48000.0f);
                response.setSample(0, sample, input);
                response.setSample(1, sample, -input);
            }
            controls = moduleDefaults(ModuleType::stereoWidth);
            controls[0] = 0.5f;
            controls[1] = 0.0f;
            controls[2] = std::log(180.0f / 20.0f)
                          / std::log(500.0f / 20.0f);
            controls[7] = 0.0f;
            widener.process(response, controls, environment);
            for (int sample = 12000; sample < response.getNumSamples(); ++sample)
            {
                const auto side = response.getSample(0, sample)
                                  - response.getSample(1, sample);
                widthBandEnergy[static_cast<size_t>(band)] +=
                    static_cast<double>(side) * side;
            }
        }
        expect(widthBandEnergy[0] < widthBandEnergy[1] * 0.12,
               "Low/high side energy ratio: "
                   + juce::String(widthBandEnergy[0] / widthBandEnergy[1], 4));

        beginTest("Stereo width mono fallback preserves the signal");
        StereoWidthModule monoWidener;
        monoWidener.prepare({ 48000.0, 512, 1 });
        juce::AudioBuffer<float> monoWidthResponse(1, 4096);
        std::vector<float> monoWidthReference(4096);
        for (int sample = 0; sample < monoWidthResponse.getNumSamples(); ++sample)
        {
            const auto input = 0.2f * std::sin(
                juce::MathConstants<float>::twoPi * 541.0f
                * static_cast<float>(sample) / 48000.0f);
            monoWidthResponse.setSample(0, sample, input);
            monoWidthReference[static_cast<size_t>(sample)] = input;
        }
        controls = moduleDefaults(ModuleType::stereoWidth);
        controls[0] = 1.0f;
        controls[1] = 1.0f;
        monoWidener.process(monoWidthResponse, controls, environment);
        float monoWidthDifference = 0.0f;
        for (int sample = 0; sample < monoWidthResponse.getNumSamples(); ++sample)
            monoWidthDifference += std::abs(
                monoWidthResponse.getSample(0, sample)
                - monoWidthReference[static_cast<size_t>(sample)]);
        expect(monoWidthDifference < 0.0001f);

        beginTest("M/S decoder reconstructs left and right channels");
        MidSideDecoderModule decoder;
        decoder.prepare(spec);
        juce::AudioBuffer<float> decoded(2, 2048);
        for (int sample = 0; sample < decoded.getNumSamples(); ++sample)
        {
            decoded.setSample(0, sample, 0.25f);
            decoded.setSample(1, sample, 0.10f);
        }
        controls = moduleDefaults(ModuleType::midSideDecoder);
        controls[0] = 1.0f;
        decoder.process(decoded, controls, environment);
        expectWithinAbsoluteError(decoded.getSample(0, 2047), 0.35f, 0.0001f);
        expectWithinAbsoluteError(decoded.getSample(1, 2047), 0.15f, 0.0001f);

        beginTest("M/S decoder width, side mute, and channel swap are correct");
        decoder.reset();
        controls = moduleDefaults(ModuleType::midSideDecoder);
        juce::AudioBuffer<float> routed(2, 2048);
        for (int sample = 0; sample < routed.getNumSamples(); ++sample)
        {
            routed.setSample(0, sample, 0.25f);
            routed.setSample(1, sample, 0.10f);
        }
        decoder.process(routed, controls, environment);
        expectWithinAbsoluteError(routed.getSample(0, 2047), 0.285f, 0.0001f);
        expectWithinAbsoluteError(routed.getSample(1, 2047), 0.215f, 0.0001f);
        controls[1] = 1.0f;
        for (int sample = 0; sample < routed.getNumSamples(); ++sample)
        {
            routed.setSample(0, sample, 0.25f);
            routed.setSample(1, sample, 0.10f);
        }
        decoder.process(routed, controls, environment);
        expectWithinAbsoluteError(routed.getSample(0, 2047), 0.215f, 0.0001f);
        expectWithinAbsoluteError(routed.getSample(1, 2047), 0.285f, 0.0001f);
        controls[2] = 1.0f;
        for (int sample = 0; sample < routed.getNumSamples(); ++sample)
        {
            routed.setSample(0, sample, 0.25f);
            routed.setSample(1, sample, 0.10f);
        }
        decoder.process(routed, controls, environment);
        expectWithinAbsoluteError(routed.getSample(0, 2047), 0.25f, 0.0001f);
        expectWithinAbsoluteError(routed.getSample(1, 2047), 0.25f, 0.0001f);
        controls[1] = 0.0f;
        controls[2] = 0.0f;
        controls[0] = 0.0f;
        for (int sample = 0; sample < routed.getNumSamples(); ++sample)
        {
            routed.setSample(0, sample, 0.25f);
            routed.setSample(1, sample, 0.10f);
        }
        decoder.process(routed, controls, environment);
        expectWithinAbsoluteError(routed.getSample(0, 2047), 0.25f, 0.0001f);
        expectWithinAbsoluteError(routed.getSample(1, 2047), 0.25f, 0.0001f);

        beginTest("M/S decoder mono fallback treats input as Mid");
        MidSideDecoderModule monoDecoder;
        monoDecoder.prepare({ 48000.0, 512, 1 });
        juce::AudioBuffer<float> monoDecoded(1, 512);
        monoDecoded.clear();
        monoDecoded.setSample(0, 0, 0.4f);
        controls = moduleDefaults(ModuleType::midSideDecoder);
        controls[0] = 1.0f;
        controls[1] = 1.0f;
        monoDecoder.process(monoDecoded, controls, environment);
        expectWithinAbsoluteError(monoDecoded.getSample(0, 0), 0.4f, 0.0001f);

        beginTest("Algorithmic reverb has independent linear Dry and Wet gains");
        auto renderReverbGains = [&spec, &environment](
                                     float dry, float wet)
        {
            auto response = std::make_unique<juce::AudioBuffer<float>>(
                2, 24000);
            response->clear();
            response->setSample(0, 0, 1.0f);
            response->setSample(1, 0, 1.0f);
            auto gainControls = moduleDefaults(ModuleType::algorithmicReverb);
            gainControls[2] = dry;
            gainControls[3] = wet;
            gainControls[5] = 0.20f;
            AlgorithmicReverbModule reverb;
            reverb.prepare(spec);
            reverb.process(*response, gainControls, environment);
            return response;
        };
        const auto dryOnlyReverb = renderReverbGains(1.0f, 0.0f);
        const auto wetOnlyReverb = renderReverbGains(0.0f, 1.0f);
        const auto silentReverb = renderReverbGains(0.0f, 0.0f);
        const auto fullReverb = renderReverbGains(1.0f, 1.0f);
        expectWithinAbsoluteError(
            dryOnlyReverb->getSample(0, 0), 1.0f, 0.000001f);
        expect(dryOnlyReverb->getMagnitude(1, 23999) < 0.000001f);
        expect(wetOnlyReverb->getMagnitude(0, 2200) < 0.000001f);
        expect(wetOnlyReverb->getMagnitude(2400, 21600) > 0.0001f);
        expect(silentReverb->getMagnitude(0, 24000) < 0.000001f);
        for (int sample = 0; sample < fullReverb->getNumSamples(); ++sample)
            expectWithinAbsoluteError(
                fullReverb->getSample(0, sample),
                dryOnlyReverb->getSample(0, sample)
                    + wetOnlyReverb->getSample(0, sample),
                0.00001f);

        beginTest("Algorithmic Dry and Wet automation remains finite");
        AlgorithmicReverbModule gainAutomatedReverb;
        gainAutomatedReverb.prepare(spec);
        controls = moduleDefaults(ModuleType::algorithmicReverb);
        juce::AudioBuffer<float> gainAutomationBlock(2, 512);
        for (int block = 0; block < 120; ++block)
        {
            for (int sample = 0;
                 sample < gainAutomationBlock.getNumSamples(); ++sample)
            {
                const auto input = 0.2f * std::sin(
                    juce::MathConstants<float>::twoPi * 229.0f
                    * static_cast<float>(
                        block * gainAutomationBlock.getNumSamples() + sample)
                    / 48000.0f);
                gainAutomationBlock.setSample(0, sample, input);
                gainAutomationBlock.setSample(1, sample, input);
            }
            controls[2] = (block & 1) == 0 ? 0.0f : 1.0f;
            controls[3] = (block & 1) == 0 ? 1.0f : 0.0f;
            gainAutomatedReverb.process(
                gainAutomationBlock, controls, environment);
            for (int channel = 0;
                 channel < gainAutomationBlock.getNumChannels(); ++channel)
                for (int sample = 0;
                     sample < gainAutomationBlock.getNumSamples(); ++sample)
                    expect(std::isfinite(
                        gainAutomationBlock.getSample(channel, sample)));
        }

        beginTest("Reverb modes are finite and measurably distinct");
        std::array<float, 3> modeChecksums {};
        for (int mode = 0; mode < 3; ++mode)
        {
            AlgorithmicReverbModule reverb;
            reverb.prepare(spec);
            juce::AudioBuffer<float> response(2, 48000);
            response.clear();
            response.setSample(0, 0, 1.0f);
            response.setSample(1, 0, 1.0f);
            controls.fill(0.5f);
            controls[2] = 0.0f;
            controls[3] = 1.0f;
            controls[4] = (static_cast<float>(mode) + 0.1f) / 3.0f;
            controls[5] = 0.0f;
            reverb.process(response, controls, environment);
            float checksum = 0.0f;
            for (int sample = 0; sample < response.getNumSamples(); ++sample)
            {
                const auto value = response.getSample(0, sample);
                expect(std::isfinite(value));
                checksum += std::abs(value)
                            * (1.0f + static_cast<float>(sample % 17) * 0.01f);
            }
            expect(checksum > 0.01f);
            modeChecksums[static_cast<size_t>(mode)] = checksum;
        }
        expect(std::abs(modeChecksums[0] - modeChecksums[1]) > 0.001f,
               "Hall/Chamber checksums: "
                   + juce::String(modeChecksums[0], 4) + ", "
                   + juce::String(modeChecksums[1], 4));
        expect(std::abs(modeChecksums[1] - modeChecksums[2]) > 0.001f,
               "Chamber/Plate checksums: "
                   + juce::String(modeChecksums[1], 4) + ", "
                   + juce::String(modeChecksums[2], 4));

        beginTest("Premium reverb reaches a dense decorrelated late field");
        auto renderResponse = [&spec, &environment](
                                  int mode, float damping)
        {
            auto response = std::make_unique<juce::AudioBuffer<float>>(
                2, 48000 * 5);
            response->clear();
            response->setSample(0, 0, 1.0f);
            response->setSample(1, 0, 1.0f);
            ControlValues reverbControls {};
            reverbControls.fill(0.5f);
            reverbControls[0] = std::log(2.0f / 0.2f)
                                / std::log(12.0f / 0.2f);
            reverbControls[2] = 0.0f;
            reverbControls[3] = 1.0f;
            reverbControls[4] = discreteValue(mode, 3);
            reverbControls[5] = 0.0f;
            reverbControls[9] = damping;
            AlgorithmicReverbModule reverb;
            reverb.prepare(spec);
            reverb.process(*response, reverbControls, environment);
            return response;
        };
        auto hallResponse = renderResponse(0, 0.5f);
        int denseSamples = 0;
        const auto densityStart = juce::roundToInt(0.5 * spec.sampleRate);
        const auto densitySamples = juce::roundToInt(0.1 * spec.sampleRate);
        for (int sample = densityStart;
             sample < densityStart + densitySamples; ++sample)
            if (std::abs(hallResponse->getSample(0, sample)) > 0.000001f)
                ++denseSamples;
        expect(static_cast<float>(denseSamples)
                   / static_cast<float>(densitySamples) > 0.90f);

        double leftEnergy = 0.0;
        double rightEnergy = 0.0;
        double crossEnergy = 0.0;
        for (int sample = 24000; sample < 96000; ++sample)
        {
            const auto left = hallResponse->getSample(0, sample);
            const auto right = hallResponse->getSample(1, sample);
            leftEnergy += static_cast<double>(left) * left;
            rightEnergy += static_cast<double>(right) * right;
            crossEnergy += static_cast<double>(left) * right;
        }
        const auto correlation = crossEnergy / std::sqrt(
            juce::jmax(1.0e-20, leftEnergy * rightEnergy));
        expect(std::abs(correlation) < 0.95);
        expect(leftEnergy > 1.0e-8 && rightEnergy > 1.0e-8);

        beginTest("Premium reverb broadband decay follows requested T60");
        std::vector<double> schroeder(
            static_cast<size_t>(hallResponse->getNumSamples()), 0.0);
        double accumulatedEnergy = 0.0;
        for (int sample = hallResponse->getNumSamples() - 1;
             sample >= 0; --sample)
        {
            const auto value = hallResponse->getSample(0, sample);
            accumulatedEnergy += static_cast<double>(value) * value;
            schroeder[static_cast<size_t>(sample)] = accumulatedEnergy;
        }
        const auto totalEnergy = juce::jmax(1.0e-20, schroeder.front());
        auto crossing = [&](double targetDb)
        {
            for (int sample = 1; sample < hallResponse->getNumSamples(); ++sample)
            {
                const auto db = 10.0 * std::log10(
                    juce::jmax(1.0e-20,
                               schroeder[static_cast<size_t>(sample)])
                    / totalEnergy);
                if (db <= targetDb)
                    return static_cast<double>(sample) / spec.sampleRate;
            }
            return 5.0;
        };
        const auto measuredT60 = 2.0 * (crossing(-35.0) - crossing(-5.0));
        expect(measuredT60 > 1.4 && measuredT60 < 2.6,
               "Measured broadband T60: " + juce::String(measuredT60, 3));

        beginTest("Damping shortens high-frequency late decay");
        auto brightResponse = renderResponse(0, 0.0f);
        auto dampedResponse = renderResponse(0, 1.0f);
        auto lateHighEnergy = [&spec](const juce::AudioBuffer<float>& response)
        {
            const auto coefficient = std::exp(
                -juce::MathConstants<float>::twoPi * 4000.0f
                / static_cast<float>(spec.sampleRate));
            float state = 0.0f;
            double energy = 0.0;
            for (int sample = 0; sample < response.getNumSamples(); ++sample)
            {
                const auto input = response.getSample(0, sample);
                state = coefficient * state + (1.0f - coefficient) * input;
                const auto high = input - state;
                if (sample >= 48000 && sample < 96000)
                    energy += static_cast<double>(high) * high;
            }
            return energy;
        };
        expect(lateHighEnergy(*dampedResponse)
               < lateHighEnergy(*brightResponse) * 0.6);

        beginTest("Mode and size changes preserve continuous wet energy");
        AlgorithmicReverbModule transitioningReverb;
        transitioningReverb.prepare(spec);
        controls.fill(0.5f);
        controls[0] = 0.72f;
        controls[2] = 0.0f;
        controls[3] = 1.0f;
        controls[4] = discreteValue(0, 3);
        controls[5] = 0.0f;
        juce::AudioBuffer<float> beforeTransition(2, 48000);
        for (int sample = 0; sample < beforeTransition.getNumSamples(); ++sample)
        {
            const auto value = 0.15f * std::sin(
                juce::MathConstants<float>::twoPi * 311.0f
                * static_cast<float>(sample) / 48000.0f);
            beforeTransition.setSample(0, sample, value);
            beforeTransition.setSample(1, sample, value);
        }
        transitioningReverb.process(beforeTransition, controls, environment);
        controls[4] = discreteValue(2, 3);
        controls[1] = 0.82f;
        juce::AudioBuffer<float> afterTransition(2, 48000);
        for (int sample = 0; sample < afterTransition.getNumSamples(); ++sample)
        {
            const auto value = 0.15f * std::sin(
                juce::MathConstants<float>::twoPi * 311.0f
                * static_cast<float>(sample + 48000) / 48000.0f);
            afterTransition.setSample(0, sample, value);
            afterTransition.setSample(1, sample, value);
        }
        transitioningReverb.process(afterTransition, controls, environment);
        double transitionEnergy = 0.0;
        for (int sample = 0; sample < 4800; ++sample)
        {
            const auto value = afterTransition.getSample(0, sample);
            transitionEnergy += static_cast<double>(value) * value;
            expect(std::isfinite(value));
        }
        expect(transitionEnergy > 0.00001);
        expect(std::abs(afterTransition.getSample(0, 0)
                        - beforeTransition.getSample(0, 47999)) < 0.5f);

        beginTest("Rapid size automation remains finite and bounded");
        AlgorithmicReverbModule sizeAutomatedReverb;
        sizeAutomatedReverb.prepare(spec);
        controls.fill(0.5f);
        controls[0] = 1.0f;
        controls[2] = 0.0f;
        controls[3] = 1.0f;
        controls[4] = discreteValue(0, 3);
        controls[5] = 0.0f;
        controls[6] = 1.0f;
        controls[7] = 1.0f;
        juce::AudioBuffer<float> automationBlock(2, 512);
        float previous = 0.0f;
        float maximumSample = 0.0f;
        float maximumJump = 0.0f;
        for (int block = 0; block < 240; ++block)
        {
            for (int sample = 0; sample < automationBlock.getNumSamples(); ++sample)
            {
                const auto phaseSample = block * automationBlock.getNumSamples()
                                         + sample;
                const auto input = block < 80
                    ? 0.12f * std::sin(
                        juce::MathConstants<float>::twoPi * 173.0f
                        * static_cast<float>(phaseSample) / 48000.0f)
                    : 0.0f;
                automationBlock.setSample(0, sample, input);
                automationBlock.setSample(1, sample, input);
            }
            controls[1] = (block & 1) == 0 ? 0.0f : 1.0f;
            sizeAutomatedReverb.process(automationBlock, controls, environment);
            for (int sample = 0; sample < automationBlock.getNumSamples(); ++sample)
            {
                const auto value = automationBlock.getSample(0, sample);
                expect(std::isfinite(value));
                maximumSample = juce::jmax(maximumSample, std::abs(value));
                maximumJump =
                    juce::jmax(maximumJump, std::abs(value - previous));
                previous = value;
            }
        }
        expect(maximumSample < 4.0f,
               "Maximum automated size sample: "
                   + juce::String(maximumSample, 3));
        expect(maximumJump < 0.5f,
               "Maximum automated size jump: "
                   + juce::String(maximumJump, 3));

        beginTest("Reverb pre-delay postpones wet output");
        AlgorithmicReverbModule predelayedReverb;
        predelayedReverb.prepare(spec);
        juce::AudioBuffer<float> predelayedResponse(2, 12000);
        predelayedResponse.clear();
        predelayedResponse.setSample(0, 0, 1.0f);
        predelayedResponse.setSample(1, 0, 1.0f);
        controls.fill(0.5f);
        controls[2] = 0.0f;
        controls[3] = 1.0f;
        controls[4] = 0.9f;
        controls[5] = 0.4f;
        predelayedReverb.process(predelayedResponse, controls, environment);
        expect(predelayedResponse.getMagnitude(0, 4700) < 0.000001f);
        expect(predelayedResponse.getMagnitude(5000, 7000) > 0.0001f);

        beginTest("Reverb reports bounded decay plus pre-delay");
        controls.fill(0.5f);
        controls[0] = 1.0f;
        controls[4] = discreteValue(0, 3);
        controls[5] = 1.0f;
        expectWithinAbsoluteError(
            static_cast<float>(predelayedReverb.tailSeconds(controls)),
            15.64f, 0.001f);

        beginTest("Reverb mono output is invariant to stereo width");
        std::array<juce::AudioBuffer<float>, 2> monoResponses {
            juce::AudioBuffer<float>(1, 24000),
            juce::AudioBuffer<float>(1, 24000)
        };
        for (int variant = 0; variant < 2; ++variant)
        {
            AlgorithmicReverbModule reverb;
            reverb.prepare({ 48000.0, 512, 1 });
            monoResponses[static_cast<size_t>(variant)].clear();
            monoResponses[static_cast<size_t>(variant)].setSample(0, 0, 1.0f);
            controls.fill(0.5f);
            controls[2] = 0.0f;
            controls[3] = 1.0f;
            controls[8] = static_cast<float>(variant);
            reverb.process(monoResponses[static_cast<size_t>(variant)],
                           controls, environment);
        }
        float monoDifference = 0.0f;
        for (int sample = 0; sample < monoResponses[0].getNumSamples(); ++sample)
            monoDifference += std::abs(
                monoResponses[0].getSample(0, sample)
                - monoResponses[1].getSample(0, sample));
        expect(monoDifference < 0.0001f);

        beginTest("Premium reverb remains finite across supported sample rates");
        for (const auto testRate : { 44100.0, 96000.0, 192000.0 })
        {
            AlgorithmicReverbModule reverb;
            reverb.prepare({ testRate, 512, 2 });
            const auto sampleCount = juce::roundToInt(testRate);
            juce::AudioBuffer<float> response(2, sampleCount);
            response.clear();
            response.setSample(0, 0, 1.0f);
            response.setSample(1, 0, 1.0f);
            controls.fill(0.5f);
            controls[0] = 1.0f;
            controls[1] = 1.0f;
            controls[2] = 0.0f;
            controls[3] = 1.0f;
            controls[4] = discreteValue(
                testRate < 48000.0 ? 0 : testRate < 192000.0 ? 1 : 2, 3);
            controls[6] = 1.0f;
            controls[7] = 1.0f;
            reverb.process(response, controls, environment);
            const auto magnitude = response.getMagnitude(0, sampleCount);
            expect(std::isfinite(magnitude),
                   "Non-finite output at " + juce::String(testRate, 0) + " Hz");
            expect(magnitude > 0.0001f && magnitude < 4.0f,
                   "Magnitude at " + juce::String(testRate, 0)
                       + " Hz: " + juce::String(magnitude, 3));
        }

        beginTest("Maximum reverb decay stays bounded under sustained input");
        AlgorithmicReverbModule sustainedReverb;
        sustainedReverb.prepare(spec);
        juce::AudioBuffer<float> sustained(2, 480000);
        for (int sample = 0; sample < sustained.getNumSamples(); ++sample)
        {
            const auto value = 0.2f * std::sin(
                juce::MathConstants<float>::twoPi * 173.0f
                * static_cast<float>(sample) / 48000.0f);
            sustained.setSample(0, sample, value);
            sustained.setSample(1, sample, value);
        }
        controls.fill(0.5f);
        controls[0] = 1.0f;
        controls[2] = 0.0f;
        controls[3] = 1.0f;
        controls[4] = 0.9f;
        sustainedReverb.process(sustained, controls, environment);
        expect(std::isfinite(sustained.getMagnitude(0, sustained.getNumSamples())));
        expect(sustained.getMagnitude(0, sustained.getNumSamples()) < 3.1f);
    }
};

class NextTenDspAcceptanceTests final : public juce::UnitTest
{
public:
    NextTenDspAcceptanceTests()
        : juce::UnitTest("Next ten DSP acceptance", "megaDSP")
    {
    }

    void runTest() override
    {
        constexpr double rate = 48000.0;
        const juce::dsp::ProcessSpec stereoSpec { rate, 257, 2 };

        beginTest("Neutral and dry paths preserve input with declared latency");
        for (int moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex)
        {
            auto module = makeModule(moduleIndex);
            module->prepare(stereoSpec);
            auto controls = neutralControls(moduleIndex);
            juce::AudioBuffer<float> rendered(2, 4096);
            rendered.clear();
            rendered.setSample(0, 0, 0.75f);
            rendered.setSample(1, 3, -0.4f);
            module->process(rendered, controls, {});
            const auto latency = module->latencySamples();
            expect(latency >= 0 && latency < rendered.getNumSamples(),
                   name(moduleIndex) + " invalid latency");
            for (int channel = 0; channel < rendered.getNumChannels();
                 ++channel)
            {
                for (int sample = 0; sample < rendered.getNumSamples();
                     ++sample)
                {
                    const auto source = sample >= latency
                        ? (channel == 0 && sample - latency == 0 ? 0.75f
                           : channel == 1 && sample - latency == 3 ? -0.4f
                                                                    : 0.0f)
                        : 0.0f;
                    expectWithinAbsoluteError(
                        rendered.getSample(channel, sample), source, 0.00001f,
                        name(moduleIndex) + " dry alignment");
                }
            }
        }

        beginTest("All new modules survive silence, channel shapes, rates, reset, and automation");
        for (int moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex)
        {
            for (const auto sampleRate : { 8000.0, 44100.0, 96000.0 })
            {
                auto module = makeModule(moduleIndex);
                module->prepare({ sampleRate, 257, 1 });
                juce::AudioBuffer<float> silence(1, 521);
                silence.clear();
                ControlValues controls {};
                controls.fill(0.5f);
                module->process(silence, controls, { nullptr, 0.0 });
                expectFinite(silence, name(moduleIndex));
                auto monoSignal = makeSine(
                    1, 1024, 347.0f, 0.1f, sampleRate);
                module->process(monoSignal, controls, { nullptr, 120.0 });
                expectFinite(monoSignal, name(moduleIndex) + " mono");

                module->prepare({ sampleRate, 257, 2 });
                juce::AudioBuffer<float> automated(2, 4096);
                for (int offset = 0; offset < automated.getNumSamples();
                     offset += 64)
                {
                    const auto samples = juce::jmin(
                        64, automated.getNumSamples() - offset);
                    for (int sample = 0; sample < samples; ++sample)
                    {
                        const auto absoluteSample = offset + sample;
                        automated.setSample(
                            0, absoluteSample,
                            0.12f * std::sin(
                                juce::MathConstants<float>::twoPi * 173.0f
                                * static_cast<float>(absoluteSample)
                                / static_cast<float>(sampleRate)));
                        automated.setSample(
                            1, absoluteSample,
                            0.07f * std::sin(
                                juce::MathConstants<float>::twoPi * 911.0f
                                * static_cast<float>(absoluteSample)
                                / static_cast<float>(sampleRate)));
                    }
                    std::array<float*, 2> pointers {
                        automated.getWritePointer(0, offset),
                        automated.getWritePointer(1, offset)
                    };
                    juce::AudioBuffer<float> block(
                        pointers.data(), 2, samples);
                    controls.fill(
                        (offset / 64) % 2 == 0 ? 0.0f : 1.0f);
                    module->process(
                        block, controls,
                        { nullptr, (offset / 64) % 3 == 0
                                       ? std::numeric_limits<double>::quiet_NaN()
                                       : 37.0 });
                }
                expectFinite(automated, name(moduleIndex));
                expect(automated.getMagnitude(
                           0, automated.getNumSamples()) < 32.0f,
                       name(moduleIndex) + " unbounded automation");
            }

            auto module = makeModule(moduleIndex);
            module->prepare(stereoSpec);
            auto controls = stressControls(moduleIndex);
            auto first = makeAsymmetricSignal(2, 4096, rate);
            module->process(first, controls, { nullptr, 93.0 });
            module->reset();
            auto second = makeAsymmetricSignal(2, 4096, rate);
            module->process(second, controls, { nullptr, 93.0 });
            expect(maxDifference(first, second) < 0.000001f,
                   name(moduleIndex) + " reset is not deterministic");
        }

        beginTest("Gate range, hold, and hysteresis have objective timing");
        GateExpanderModule gate;
        gate.prepare(stereoSpec);
        ControlValues gateControls {};
        gateControls[0] = 0.70f;
        gateControls[1] = 1.0f;
        gateControls[2] = 0.0f;
        gateControls[3] = 0.10f;
        gateControls[4] = 0.0f;
        gateControls[5] = 0.67f;
        gateControls[6] = 0.0f;
        gateControls[7] = 1.0f;
        gateControls[10] = 1.0f;
        auto gateBlock = makeSine(2, 12000, 1000.0f, 0.0f, rate);
        gate.process(gateBlock, gateControls, {});
        gateBlock = makeSine(2, 6000, 1000.0f, 0.005f, rate);
        gate.process(gateBlock, gateControls, {});
        const auto closedRms = rms(gateBlock, 4000, 6000);
        gateBlock = makeSine(2, 6000, 1000.0f, 0.5f, rate);
        gate.process(gateBlock, gateControls, {});
        gateBlock = makeSine(2, 4800, 1000.0f, 0.04f, rate);
        gate.process(gateBlock, gateControls, {});
        expect(rms(gateBlock, 2400, 4800) > 0.020f,
               "Gate closed inside its hysteresis window");
        gateBlock = makeSine(2, 6000, 1000.0f, 0.005f, rate);
        gate.process(gateBlock, gateControls, {});
        const auto heldRms = rms(gateBlock, 256, 1024);
        const auto releasedRms = rms(gateBlock, 5000, 6000);
        expect(heldRms > 0.002f, "Gate hold ended too early");
        expect(releasedRms < heldRms * 0.08f,
               "Gate hold/release did not close");
        expect(closedRms < 0.0005f && releasedRms < 0.0005f,
               "Gate range is too shallow");
        expect(gate.meterValue() > 20.0f && gate.meterValue() <= 80.01f,
               "Gate reduction meter outside range: "
                   + juce::String(gate.meterValue(), 2));

        beginTest("Transient shaping is neutral and directional");
        auto transientControls = neutralControls(transientDesigner);
        transientControls[6] = 1.0f;
        auto transientInput = makeTransientTrain(2, 24000);
        auto neutralTransient = transientInput;
        TransientDesignerModule neutralDesigner;
        neutralDesigner.prepare(stereoSpec);
        neutralDesigner.process(neutralTransient, transientControls, {});
        expect(maxDifference(neutralTransient, transientInput) < 0.000001f);
        auto positiveTransient = transientInput;
        auto positiveControls = transientControls;
        positiveControls[0] = 1.0f;
        TransientDesignerModule positiveDesigner;
        positiveDesigner.prepare(stereoSpec);
        positiveDesigner.process(positiveTransient, positiveControls, {});
        auto negativeTransient = transientInput;
        auto negativeControls = transientControls;
        negativeControls[0] = 0.0f;
        TransientDesignerModule negativeDesigner;
        negativeDesigner.prepare(stereoSpec);
        negativeDesigner.process(negativeTransient, negativeControls, {});
        expect(rms(positiveTransient, 2000, 24000)
                   > rms(neutralTransient, 2000, 24000) * 1.08f,
               "Positive transient direction did not boost attacks");
        expect(rms(negativeTransient, 2000, 24000)
                   < rms(neutralTransient, 2000, 24000) * 0.92f,
               "Negative transient direction did not reduce attacks");

        beginTest("Multiband ratio 1 reconstructs all bands at flat gain");
        for (const auto frequency : { 100.0f, 1200.0f, 8000.0f })
        {
            MultibandCompressorModule multiband;
            multiband.prepare(stereoSpec);
            auto controls = neutralControls(multibandCompressor);
            controls[5] = 0.0f;
            controls[10] = 1.0f;
            auto signal = makeSine(2, 48000, frequency, 0.2f, rate);
            const auto referenceRms = rms(signal, 24000, 48000);
            multiband.process(signal, controls, {});
            const auto ratio = rms(signal, 24000, 48000) / referenceRms;
            expect(ratio > 0.985f && ratio < 1.015f,
                   "Flat reconstruction at " + juce::String(frequency, 0)
                       + " Hz: " + juce::String(ratio, 4));
        }

        beginTest("Phaser and flanger transitions are bounded and tempo fallback is stable");
        checkModulatorTransitions<StudioPhaserModule>(
            studioPhaser, 0, 5, 9, 10, stereoSpec);
        checkModulatorTransitions<StudioFlangerModule>(
            studioFlanger, 0, 4, 9, 10, stereoSpec);
        checkTempoFallback<StudioPhaserModule>(
            studioPhaser, 2, 3, 9, 10, stereoSpec);
        checkTempoFallback<StudioFlangerModule>(
            studioFlanger, 2, 3, 9, 10, stereoSpec);
        checkTempoFallback<SpatialOrbitModule>(
            spatialOrbit, 2, 3, 10, 11, stereoSpec);

        beginTest("Creative telemetry is capture-gated, coherent, and resettable");
        const ProcessEnvironment captureTelemetry { nullptr, 120.0, true };
        const ProcessEnvironment noTelemetry { nullptr, 120.0, false };
        for (const auto type : {
                 ModuleType::studioPhaser, ModuleType::studioFlanger,
                 ModuleType::frequencyLab, ModuleType::spatialOrbit,
                 ModuleType::signalDecay, ModuleType::analogTape })
        {
            auto module = createDspModule(type);
            module->prepare(stereoSpec);
            auto* capability = module->continuousTelemetryCapability();
            expect(capability != nullptr);
            ContinuousTelemetrySnapshot before;
            expect(capability != nullptr
                   && capability->readContinuousTelemetry(before));
            expect(before.sequence == 0);

            auto controls = moduleDefaults(type);
            auto uncaptured = makeSine(2, 256, 347.0f, 0.15f, rate);
            module->process(uncaptured, controls, noTelemetry);
            ContinuousTelemetrySnapshot afterUncaptured;
            expect(capability->readContinuousTelemetry(afterUncaptured));
            expect(afterUncaptured.sequence == 0);

            auto captured = makeSine(2, 256, 347.0f, 0.15f, rate);
            module->process(captured, controls, captureTelemetry);
            ContinuousTelemetrySnapshot published;
            expect(capability->readContinuousTelemetry(published));
            expect(published.sequence == 1);
            expect(published.valueCount > 0
                   && published.valueCount
                          <= continuousTelemetryValueCapacity);
            for (std::uint32_t index = 0; index < published.valueCount; ++index)
                expect(std::isfinite(
                    published.values[static_cast<size_t>(index)]));

            auto heldSequence = published.sequence;
            module->process(captured, controls, noTelemetry);
            expect(capability->readContinuousTelemetry(published));
            expect(published.sequence == heldSequence);
            module->reset();
            expect(capability->readContinuousTelemetry(published));
            expect(published.sequence == 0);
        }

        beginTest("Modulation telemetry reports DSP phase and crossfade weights");
        {
            StudioPhaserModule module;
            module.prepare(stereoSpec);
            auto controls = moduleDefaults(ModuleType::studioPhaser);
            controls[0] = discreteValue(4, StudioPhaserModule::topologyCount);
            auto block = makeSine(2, 2048, 317.0f, 0.15f, rate);
            module.process(block, controls, captureTelemetry);
            ContinuousTelemetrySnapshot snapshot;
            expect(module.readContinuousTelemetry(snapshot));
            expectEquals(
                juce::roundToInt(
                    snapshot.values[StudioPhaserModule::targetTopology]),
                4);
            auto mixSum = 0.0f;
            for (int index = 0; index < StudioPhaserModule::topologyCount;
                 ++index)
                mixSum += snapshot.values[static_cast<size_t>(
                    StudioPhaserModule::topologyMix0 + index)];
            expect(std::abs(mixSum - 1.0f) < 0.0001f);
        }
        {
            StudioFlangerModule module;
            module.prepare(stereoSpec);
            auto controls = moduleDefaults(ModuleType::studioFlanger);
            controls[0] = discreteValue(3, StudioFlangerModule::modelCount);
            auto block = makeSine(2, 2048, 317.0f, 0.15f, rate);
            module.process(block, controls, captureTelemetry);
            ContinuousTelemetrySnapshot snapshot;
            expect(module.readContinuousTelemetry(snapshot));
            expectEquals(
                juce::roundToInt(
                    snapshot.values[StudioFlangerModule::targetModel]),
                3);
            auto mixSum = 0.0f;
            for (int index = 0; index < StudioFlangerModule::modelCount;
                 ++index)
                mixSum += snapshot.values[static_cast<size_t>(
                    StudioFlangerModule::modelMix0 + index)];
            expect(std::abs(mixSum - 1.0f) < 0.0001f);
            expect(snapshot.values[StudioFlangerModule::selectedDelayMs]
                   >= 0.0f);
        }

        beginTest("Wander telemetry is deterministic and carries actual history");
        {
            auto controls = moduleDefaults(ModuleType::spatialOrbit);
            controls[0] = discreteValue(3, 4);
            SpatialOrbitModule module;
            module.prepare(stereoSpec);
            auto first = makeSine(2, 4096, 271.0f, 0.15f, rate);
            module.process(first, controls, captureTelemetry);
            ContinuousTelemetrySnapshot firstSnapshot;
            expect(module.readContinuousTelemetry(firstSnapshot));
            expectEquals(
                static_cast<int>(firstSnapshot.historyValueCount),
                static_cast<int>(
                    SpatialOrbitModule::telemetryHistoryValueCount));
            expect(firstSnapshot.historyCount == 1);
            module.reset();
            auto second = makeSine(2, 4096, 271.0f, 0.15f, rate);
            module.process(second, controls, captureTelemetry);
            ContinuousTelemetrySnapshot secondSnapshot;
            expect(module.readContinuousTelemetry(secondSnapshot));
            for (const auto index : {
                     SpatialOrbitModule::xPosition,
                     SpatialOrbitModule::yPosition,
                     SpatialOrbitModule::distanceMetres })
                expectEquals(firstSnapshot.values[index],
                             secondSnapshot.values[index]);
        }

        beginTest("Signal Decay reports clock, wear, and actual dropout events");
        {
            auto controls = moduleDefaults(ModuleType::signalDecay);
            controls[1] = 0.0f;
            controls[3] = 1.0f;
            controls[8] = 1.0f;
            SignalDecayModule module;
            module.prepare(stereoSpec);
            auto block = makeSine(2, 48000, 293.0f, 0.15f, rate);
            module.process(block, controls, captureTelemetry);
            ContinuousTelemetrySnapshot state;
            EventTelemetrySnapshot events;
            expect(module.readContinuousTelemetry(state));
            expect(module.readEventTelemetry(events));
            expect(state.values[SignalDecayModule::currentSampleRate]
                   >= 1000.0f);
            expect(state.values[SignalDecayModule::leftClockPhase] >= 0.0f
                   && state.values[SignalDecayModule::leftClockPhase] < 1.0f);
            expect(events.eventCount > 0);
            if (events.eventCount > 0)
                expectEquals(
                    static_cast<int>(events.events[0].kind),
                    static_cast<int>(SignalDecayModule::dropoutStarted));
        }

        beginTest("Analog Tape reports transport, modulation, and wear");
        {
            auto controls = moduleDefaults(ModuleType::analogTape);
            controls[6] = 1.0f;
            controls[7] = 1.0f;
            controls[8] = 1.0f;
            AnalogTapeModule module;
            module.prepare({ rate, 48000, 2 });
            auto block = makeSine(2, 48000, 293.0f, 0.15f, rate);
            module.process(block, controls, captureTelemetry);
            ContinuousTelemetrySnapshot state;
            expect(module.readContinuousTelemetry(state));
            expectEquals(
                static_cast<int>(state.valueCount),
                static_cast<int>(AnalogTapeModule::telemetryValueCount));
            expect(state.values[AnalogTapeModule::transportPhase] >= 0.0f
                   && state.values[AnalogTapeModule::transportPhase] < 1.0f);
            expect(std::abs(state.values[
                       AnalogTapeModule::modulationMilliseconds]) > 0.0f);
            expect(state.values[AnalogTapeModule::currentDropoutGain] > 0.0f
                   && state.values[AnalogTapeModule::currentDropoutGain]
                          <= 1.0f);
        }

        beginTest("Analog Tape aligns dry audio and distinguishes machines");
        {
            auto dryControls = moduleDefaults(ModuleType::analogTape);
            dryControls[10] = 0.0f;
            AnalogTapeModule dryModule;
            dryModule.prepare({ rate, 4096, 2 });
            auto dry = makeAsymmetricSignal(2, 4096, rate);
            const auto reference = dry;
            dryModule.process(dry, dryControls, {});
            const auto latency = dryModule.latencySamples();
            for (int channel = 0; channel < dry.getNumChannels(); ++channel)
                for (int sample = latency; sample < dry.getNumSamples();
                     ++sample)
                    expectWithinAbsoluteError(
                        dry.getSample(channel, sample),
                        reference.getSample(channel, sample - latency),
                        0.000001f);

            std::array<juce::AudioBuffer<float>, 4> rendered;
            for (int machine = 0; machine < 4; ++machine)
            {
                auto controls = moduleDefaults(ModuleType::analogTape);
                controls[0] = discreteValue(machine, 4);
                controls[1] = 1.0f;
                controls[2] = 1.0f;
                AnalogTapeModule module;
                module.prepare({ rate, 8192, 2 });
                rendered[static_cast<size_t>(machine)] =
                    makeSine(2, 8192, 997.0f, 0.2f, rate);
                module.process(
                    rendered[static_cast<size_t>(machine)], controls, {});
                expectFinite(
                    rendered[static_cast<size_t>(machine)],
                    "Analog Tape machine");
            }
            for (int machine = 1; machine < 4; ++machine)
                expect(maxDifference(
                           rendered[0],
                           rendered[static_cast<size_t>(machine)]) > 0.001f,
                       "Analog Tape machines were not distinct");
        }

        beginTest("Diffusion delay sync, fallback, feedback, and tail are bounded");
        ControlValues diffusionControls {};
        diffusionControls[0] = 0.0f;
        diffusionControls[1] = 1.0f;
        diffusionControls[2] = 0.0f;
        diffusionControls[3] = 0.0f;
        diffusionControls[4] = 0.0f;
        diffusionControls[6] = 0.0f;
        diffusionControls[7] = 1.0f;
        diffusionControls[10] = 1.0f;
        diffusionControls[11] = 0.6f;
        const auto diffusionAt120 = renderImpulse<DiffusionDelayModule>(
            diffusionControls, 120.0, 16000, stereoSpec);
        const auto diffusionFallback = renderImpulse<DiffusionDelayModule>(
            diffusionControls,
            std::numeric_limits<double>::quiet_NaN(), 16000, stereoSpec);
        expect(maxDifference(diffusionAt120, diffusionFallback) == 0.0f);
        checkTempoFallback<DiffusionDelayModule>(
            diffusionDelay, 1, 2, 10, 11, stereoSpec);
        const auto firstDiffusionEcho = firstAbove(
            diffusionAt120, 0, 0.0001f);
        expect(std::abs(firstDiffusionEcho - 3000) <= 2,
               "Unexpected synced delay: "
                   + juce::String(firstDiffusionEcho));
        DiffusionDelayModule diffusion;
        diffusion.prepare(stereoSpec);
        const auto shortDiffusionTail = diffusion.tailSeconds(
            diffusionControls);
        diffusionControls[3] = 0.85f;
        diffusionControls[4] = 1.0f;
        const auto longDiffusionTail = diffusion.tailSeconds(
            diffusionControls);
        expect(longDiffusionTail > shortDiffusionTail
               && longDiffusionTail <= 120.0);
        auto diffusionFeedback = makeSine(
            2, 96000, 317.0f, 0.15f, rate);
        diffusion.process(diffusionFeedback, diffusionControls, { nullptr, 120.0 });
        expectFinite(diffusionFeedback, "Diffusion Delay feedback");
        expect(diffusionFeedback.getMagnitude(
                   0, diffusionFeedback.getNumSamples()) < 3.0f);

        beginTest("Pitch bloom shifts pitch and reports bounded feedback tails");
        ControlValues pitchControls {};
        pitchControls[0] = 0.5f;
        pitchControls[1] = 0.5f;
        pitchControls[2] = 0.0f;
        pitchControls[3] = 0.0f;
        pitchControls[4] = 0.0f;
        pitchControls[6] = 0.0f;
        pitchControls[7] = 1.0f;
        pitchControls[9] = 1.0f;
        pitchControls[10] = 0.6f;
        PitchBloomModule pitch;
        pitch.prepare(stereoSpec);
        auto pitched = makeSine(2, 48000, 440.0f, 0.2f, rate);
        pitch.process(pitched, pitchControls, {});
        const auto shiftedFrequency =
            estimateFrequency(pitched, 0, 20000, 47000, rate);
        expect(shiftedFrequency > 820.0f && shiftedFrequency < 940.0f,
               "Measured octave pitch: "
                   + juce::String(shiftedFrequency, 2));
        const auto shortPitchTail = pitch.tailSeconds(pitchControls);
        pitchControls[3] = 1.0f;
        pitchControls[4] = 1.0f;
        const auto longPitchTail = pitch.tailSeconds(pitchControls);
        expect(longPitchTail > shortPitchTail && longPitchTail <= 120.0);
        pitch.reset();
        auto pitchFeedback = makeSine(
            2, 72000, 293.0f, 0.12f, rate);
        pitch.process(pitchFeedback, pitchControls, {});
        expectFinite(pitchFeedback, "Pitch Bloom feedback");
        expect(pitchFeedback.getMagnitude(
                   0, pitchFeedback.getNumSamples()) < 3.0f);

        beginTest("Frequency Lab translates a sinusoid by the requested hertz");
        ControlValues frequencyControls {};
        frequencyControls[0] = 0.55f;
        frequencyControls[1] = 0.5f;
        frequencyControls[2] = 0.5f;
        frequencyControls[3] = 0.0f;
        frequencyControls[4] = 0.0f;
        frequencyControls[5] = 0.5f;
        frequencyControls[6] = 0.0f;
        frequencyControls[7] = 1.0f;
        frequencyControls[8] = 1.0f;
        frequencyControls[9] = 0.6f;
        FrequencyLabModule frequencyLab;
        frequencyLab.prepare(stereoSpec);
        auto translated = makeSine(2, 48000, 1000.0f, 0.2f, rate);
        frequencyLab.process(translated, frequencyControls, {});
        const auto translatedFrequency =
            estimateFrequency(translated, 0, 10000, 47000, rate);
        expect(translatedFrequency > 1440.0f
                   && translatedFrequency < 1560.0f,
               "Measured translated frequency: "
                   + juce::String(translatedFrequency, 2));

        beginTest("Spatial Orbit mono and wander are safe and deterministic");
        auto orbitControls = stressControls(spatialOrbit);
        orbitControls[0] = 1.0f;
        orbitControls[10] = 1.0f;
        orbitControls[11] = 0.6f;
        SpatialOrbitModule orbit;
        orbit.prepare({ rate, 257, 1 });
        auto firstOrbit = makeSine(1, 48000, 271.0f, 0.2f, rate);
        orbit.process(firstOrbit, orbitControls, { nullptr, 0.0 });
        orbit.reset();
        auto secondOrbit = makeSine(1, 48000, 271.0f, 0.2f, rate);
        orbit.process(secondOrbit, orbitControls, { nullptr, 120.0 });
        expect(maxDifference(firstOrbit, secondOrbit) == 0.0f,
               "Orbit wander/reset is not deterministic");
        expectFinite(firstOrbit, "Spatial Orbit mono");
        expect(firstOrbit.getMagnitude(0, firstOrbit.getNumSamples()) < 1.0f);
        SpatialOrbitModule stereoOrbit;
        stereoOrbit.prepare(stereoSpec);
        auto asymmetricOrbit = makeAsymmetricSignal(2, 48000, rate);
        stereoOrbit.process(
            asymmetricOrbit, orbitControls, { nullptr, 120.0 });
        expect(channelDifference(asymmetricOrbit, 4000) > 0.001f,
               "Orbit collapsed asymmetric stereo input");

        beginTest("Signal Decay is deterministic and near-transparent at quality limits");
        ControlValues decayControls {};
        decayControls[0] = 1.0f;
        decayControls[1] = 1.0f;
        decayControls[2] = 0.0f;
        decayControls[3] = 0.0f;
        decayControls[4] = 1.0f;
        decayControls[5] = 0.0f;
        decayControls[6] = 0.0f;
        decayControls[7] = 0.0f;
        decayControls[8] = 0.0f;
        decayControls[9] = 1.0f;
        decayControls[10] = 0.6f;
        SignalDecayModule decay;
        decay.prepare(stereoSpec);
        auto decayInput = makeAsymmetricSignal(2, 48000, rate);
        auto firstDecay = decayInput;
        decay.process(firstDecay, decayControls, {});
        decay.reset();
        auto secondDecay = decayInput;
        decay.process(secondDecay, decayControls, {});
        expect(maxDifference(firstDecay, secondDecay) == 0.0f,
               "Signal Decay reset did not restore its random sequence");
        const auto decayLatency = decay.latencySamples();
        double decayError = 0.0;
        double decayReference = 0.0;
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = decayLatency + 4000;
                 sample < firstDecay.getNumSamples(); ++sample)
            {
                const auto reference =
                    decayInput.getSample(channel, sample - decayLatency);
                const auto difference =
                    firstDecay.getSample(channel, sample) - reference;
                decayError += static_cast<double>(difference) * difference;
                decayReference += static_cast<double>(reference) * reference;
            }
        const auto normalizedDecayError = std::sqrt(
            decayError / juce::jmax(1.0e-20, decayReference));
        expect(normalizedDecayError < 0.08,
               "Signal Decay quality-limit error: "
                   + juce::String(normalizedDecayError, 4));
        expect(firstDecay.getMagnitude(
                   0, firstDecay.getNumSamples()) < 0.5f);
    }

private:
    enum ModuleIndex
    {
        gateExpander,
        transientDesigner,
        multibandCompressor,
        studioPhaser,
        studioFlanger,
        diffusionDelay,
        pitchBloom,
        frequencyLab,
        spatialOrbit,
        signalDecay,
        moduleCount
    };

    static juce::String name(int index)
    {
        constexpr std::array<const char*, moduleCount> names {
            "Gate Expander", "Transient Designer", "Multiband Compressor",
            "Studio Phaser", "Studio Flanger", "Diffusion Delay",
            "Pitch Bloom", "Frequency Lab", "Spatial Orbit", "Signal Decay"
        };
        return names[static_cast<size_t>(index)];
    }

    static std::unique_ptr<DspModule> makeModule(int index)
    {
        switch (index)
        {
            case gateExpander: return std::make_unique<GateExpanderModule>();
            case transientDesigner:
                return std::make_unique<TransientDesignerModule>();
            case multibandCompressor:
                return std::make_unique<MultibandCompressorModule>();
            case studioPhaser: return std::make_unique<StudioPhaserModule>();
            case studioFlanger: return std::make_unique<StudioFlangerModule>();
            case diffusionDelay: return std::make_unique<DiffusionDelayModule>();
            case pitchBloom: return std::make_unique<PitchBloomModule>();
            case frequencyLab: return std::make_unique<FrequencyLabModule>();
            case spatialOrbit: return std::make_unique<SpatialOrbitModule>();
            default: return std::make_unique<SignalDecayModule>();
        }
    }

    static ControlValues neutralControls(int index)
    {
        ControlValues controls {};
        if (index == transientDesigner)
        {
            controls[0] = controls[1] = 0.5f;
            controls[6] = 1.0f;
            controls[7] = 0.5f;
        }
        else if (index == multibandCompressor)
        {
            controls[5] = 0.0f;
            controls[10] = 0.0f;
            controls[11] = 0.6f;
        }
        else if (index == studioPhaser)
            controls[10] = 0.6f;
        else if (index == studioFlanger)
            controls[10] = 0.6f;
        else if (index == diffusionDelay)
            controls[11] = 0.6f;
        else if (index == pitchBloom)
            controls[10] = 0.6f;
        else if (index == frequencyLab)
            controls[9] = 0.6f;
        else if (index == spatialOrbit)
            controls[11] = 0.6f;
        else if (index == signalDecay)
            controls[10] = 0.6f;
        return controls;
    }

    static ControlValues stressControls(int index)
    {
        ControlValues controls {};
        controls.fill(0.73f);
        if (index == transientDesigner)
            controls[7] = 0.5f;
        else if (index == multibandCompressor)
            controls[11] = 0.6f;
        else if (index == studioPhaser)
            controls[10] = 0.6f;
        else if (index == studioFlanger)
            controls[10] = 0.6f;
        else if (index == diffusionDelay)
            controls[11] = 0.6f;
        else if (index == pitchBloom)
            controls[10] = 0.6f;
        else if (index == frequencyLab)
            controls[9] = 0.6f;
        else if (index == spatialOrbit)
            controls[11] = 0.6f;
        else if (index == signalDecay)
            controls[10] = 0.6f;
        return controls;
    }

    static juce::AudioBuffer<float> makeSine(
        int channels, int samples, float frequency, float amplitude,
        double sampleRate)
    {
        juce::AudioBuffer<float> result(channels, samples);
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto value = amplitude * std::sin(
                juce::MathConstants<float>::twoPi * frequency
                * static_cast<float>(sample / sampleRate));
            for (int channel = 0; channel < channels; ++channel)
                result.setSample(channel, sample, value);
        }
        return result;
    }

    static juce::AudioBuffer<float> makeAsymmetricSignal(
        int channels, int samples, double sampleRate)
    {
        juce::AudioBuffer<float> result(channels, samples);
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto left = 0.15f * std::sin(
                juce::MathConstants<float>::twoPi * 233.0f
                * static_cast<float>(sample / sampleRate));
            result.setSample(0, sample, left);
            if (channels > 1)
                result.setSample(
                    1, sample,
                    0.09f * std::sin(
                        juce::MathConstants<float>::twoPi * 877.0f
                        * static_cast<float>(sample / sampleRate)));
        }
        return result;
    }

    static juce::AudioBuffer<float> makeTransientTrain(
        int channels, int samples)
    {
        juce::AudioBuffer<float> result(channels, samples);
        result.clear();
        for (int start = 512; start < samples; start += 1200)
            for (int offset = 0; offset < 160 && start + offset < samples;
                 ++offset)
            {
                const auto envelope = std::exp(
                    -static_cast<float>(offset) / 24.0f);
                const auto value = 0.55f * envelope
                    * std::sin(
                        juce::MathConstants<float>::twoPi * 2200.0f
                        * static_cast<float>(offset) / 48000.0f);
                for (int channel = 0; channel < channels; ++channel)
                    result.setSample(channel, start + offset, value);
            }
        return result;
    }

    static float rms(const juce::AudioBuffer<float>& buffer,
                     int start, int end)
    {
        double energy = 0.0;
        int count = 0;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = start;
                 sample < juce::jmin(end, buffer.getNumSamples()); ++sample)
            {
                const auto value = buffer.getSample(channel, sample);
                energy += static_cast<double>(value) * value;
                ++count;
            }
        return static_cast<float>(std::sqrt(
            energy / static_cast<double>(juce::jmax(1, count))));
    }

    static float maxDifference(const juce::AudioBuffer<float>& first,
                               const juce::AudioBuffer<float>& second)
    {
        auto maximum = 0.0f;
        for (int channel = 0; channel < first.getNumChannels(); ++channel)
            for (int sample = 0; sample < first.getNumSamples(); ++sample)
                maximum = juce::jmax(
                    maximum,
                    std::abs(first.getSample(channel, sample)
                             - second.getSample(channel, sample)));
        return maximum;
    }

    static float channelDifference(
        const juce::AudioBuffer<float>& buffer, int start)
    {
        double energy = 0.0;
        for (int sample = start; sample < buffer.getNumSamples(); ++sample)
        {
            const auto difference =
                buffer.getSample(0, sample) - buffer.getSample(1, sample);
            energy += static_cast<double>(difference) * difference;
        }
        return static_cast<float>(std::sqrt(
            energy / juce::jmax(1, buffer.getNumSamples() - start)));
    }

    static int firstAbove(const juce::AudioBuffer<float>& buffer,
                          int channel, float threshold)
    {
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            if (std::abs(buffer.getSample(channel, sample)) > threshold)
                return sample;
        return -1;
    }

    static float estimateFrequency(
        const juce::AudioBuffer<float>& buffer, int channel,
        int start, int end, double sampleRate)
    {
        int crossings = 0;
        auto previous = buffer.getSample(channel, start);
        for (int sample = start + 1;
             sample < juce::jmin(end, buffer.getNumSamples()); ++sample)
        {
            const auto current = buffer.getSample(channel, sample);
            if (previous <= 0.0f && current > 0.0f)
                ++crossings;
            previous = current;
        }
        return static_cast<float>(
            crossings * sampleRate
            / static_cast<double>(juce::jmax(1, end - start)));
    }

    template <typename Module>
    static juce::AudioBuffer<float> renderImpulse(
        const ControlValues& controls, double bpm, int samples,
        const juce::dsp::ProcessSpec& spec)
    {
        Module module;
        module.prepare(spec);
        juce::AudioBuffer<float> response(2, samples);
        response.clear();
        response.setSample(0, 0, 1.0f);
        response.setSample(1, 0, 1.0f);
        module.process(response, controls, { nullptr, bpm });
        return response;
    }

    template <typename Module>
    void checkModulatorTransitions(
        int moduleIndex, int modelControl, int modelCount,
        int mixControl, int outputControl,
        const juce::dsp::ProcessSpec& spec)
    {
        Module module;
        module.prepare(spec);
        auto controls = stressControls(moduleIndex);
        controls[static_cast<size_t>(modelControl)] = 0.0f;
        controls[static_cast<size_t>(mixControl)] = 1.0f;
        controls[static_cast<size_t>(outputControl)] = 0.6f;
        float previous = 0.0f;
        float maximum = 0.0f;
        float maximumJump = 0.0f;
        int absoluteSample = 0;
        for (int blockIndex = 0; blockIndex < 100; ++blockIndex)
        {
            controls[static_cast<size_t>(modelControl)] =
                (static_cast<float>(blockIndex % modelCount) + 0.1f)
                / static_cast<float>(modelCount);
            juce::AudioBuffer<float> block(2, 127);
            for (int sample = 0; sample < block.getNumSamples(); ++sample)
            {
                const auto value = 0.18f * std::sin(
                    juce::MathConstants<float>::twoPi * 311.0f
                    * static_cast<float>(absoluteSample++) / 48000.0f);
                block.setSample(0, sample, value);
                block.setSample(1, sample, value * 0.83f);
            }
            module.process(block, controls, { nullptr, 120.0 });
            expectFinite(block, name(moduleIndex));
            for (int sample = 0; sample < block.getNumSamples(); ++sample)
            {
                const auto value = block.getSample(0, sample);
                maximum = juce::jmax(maximum, std::abs(value));
                maximumJump =
                    juce::jmax(maximumJump, std::abs(value - previous));
                previous = value;
            }
        }
        expect(maximum < 2.0f, name(moduleIndex) + " transition magnitude");
        expect(maximumJump < 0.6f, name(moduleIndex) + " transition jump");
    }

    template <typename Module>
    void checkTempoFallback(
        int moduleIndex, int syncControl, int divisionControl,
        int mixControl, int outputControl,
        const juce::dsp::ProcessSpec& spec)
    {
        auto controls = stressControls(moduleIndex);
        controls[static_cast<size_t>(syncControl)] = 1.0f;
        controls[static_cast<size_t>(divisionControl)] = 0.43f;
        controls[static_cast<size_t>(mixControl)] = 1.0f;
        controls[static_cast<size_t>(outputControl)] = 0.6f;
        auto render = [&](double bpm)
        {
            Module module;
            module.prepare(spec);
            auto signal = makeSine(2, 24000, 281.0f, 0.14f, spec.sampleRate);
            module.process(signal, controls, { nullptr, bpm });
            return signal;
        };
        const auto at120 = render(120.0);
        const auto invalid = render(
            std::numeric_limits<double>::quiet_NaN());
        const auto at60 = render(60.0);
        expect(maxDifference(at120, invalid) == 0.0f,
               name(moduleIndex) + " tempo fallback");
        expect(maxDifference(at120, at60) > 0.001f,
               name(moduleIndex) + " tempo sync ignored BPM");
    }

    void expectFinite(
        const juce::AudioBuffer<float>& buffer, const juce::String& context)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                if (!std::isfinite(buffer.getSample(channel, sample)))
                {
                    expect(false, context + " produced a non-finite sample");
                    return;
                }
    }
};

DspSanityTests dspSanityTests;
NextTenDspAcceptanceTests nextTenDspAcceptanceTests;
} // namespace
} // namespace megadsp
