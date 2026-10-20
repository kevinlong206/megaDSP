#include "../src/DspModules.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace
{
class OptoLevelerTests final : public juce::UnitTest
{
public:
    OptoLevelerTests() : juce::UnitTest("Opto Leveler", "megaDSP") {}

    void runTest() override
    {
        using namespace megadsp;
        constexpr auto sampleRate = 48000.0;
        const juce::dsp::ProcessSpec spec { sampleRate, 512, 2 };
        ProcessEnvironment environment;

        beginTest("Registry appends Opto Leveler without shifting old types");
        expectEquals(static_cast<int>(ModuleType::vinylSimulator), 49);
        expectEquals(static_cast<int>(ModuleType::optoLeveler), 50);
        expectEquals(static_cast<int>(ModuleType::fetLimiter), 51);
        expectEquals(moduleTypeCount, 62);
        const auto& definition = moduleDefinition(ModuleType::optoLeveler);
        expect(definition.category == ModuleCategory::dynamics);
        expect(definition.capabilities
               == ModuleCapability::continuousTelemetry);
        for (int control = 0; control < OptoLevelerModule::activeControlCount;
             ++control)
            expect(definition.controls[static_cast<size_t>(control)]
                       .isActive());
        for (int control = OptoLevelerModule::activeControlCount;
             control < controlsPerSlot; ++control)
            expect(!definition.controls[static_cast<size_t>(control)]
                        .isActive());

        beginTest("Control values use musician-facing units");
        const auto defaults = moduleDefaults(ModuleType::optoLeveler);
        expect(formatControlValue(
                   ModuleType::optoLeveler, OptoLevelerModule::gainControl,
                   defaults[OptoLevelerModule::gainControl])
                   .endsWith("dB"));
        expect(formatControlValue(
                   ModuleType::optoLeveler, OptoLevelerModule::outputControl,
                   defaults[OptoLevelerModule::outputControl])
                   .endsWith("dB"));
        expect(formatControlValue(
                   ModuleType::optoLeveler,
                   OptoLevelerModule::peakReductionControl,
                   defaults[OptoLevelerModule::peakReductionControl])
                   .endsWith("%"));
        const auto parsedGain = parseControlValue(
            ModuleType::optoLeveler, OptoLevelerModule::gainControl,
            "0.0 dB");
        expect(parsedGain.has_value());
        expectWithinAbsoluteError(
            parsedGain.value_or(0.0f), (0.0f + 12.0f) / 36.0f, 0.0001f);
        const auto parsedLowCut = parseControlValue(
            ModuleType::optoLeveler,
            OptoLevelerModule::sidechainLowCutControl, "60 Hz");
        expect(parsedLowCut.has_value());
        expectWithinAbsoluteError(
            parsedLowCut.value_or(0.0f),
            defaults[OptoLevelerModule::sidechainLowCutControl], 0.01f);

        beginTest("Reset makes reprocessing deterministic");
        OptoLevelerModule first;
        OptoLevelerModule second;
        first.prepare(spec);
        second.prepare(spec);
        auto drivenControls = defaults;
        drivenControls[OptoLevelerModule::peakReductionControl] = 0.85f;
        drivenControls[OptoLevelerModule::cellAgeControl] = 0.7f;
        drivenControls[OptoLevelerModule::hfEmphasisControl] = 0.6f;
        juce::AudioBuffer<float> firstBlock(2, 512);
        juce::AudioBuffer<float> secondBlock(2, 512);
        for (int block = 0; block < 20; ++block)
        {
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < 512; ++sample)
                {
                    const auto input = 0.4f * std::sin(
                        juce::MathConstants<float>::twoPi * 220.0f
                        * static_cast<float>(block * 512 + sample)
                        / static_cast<float>(sampleRate));
                    firstBlock.setSample(channel, sample, input);
                    secondBlock.setSample(channel, sample, input);
                }
            first.process(firstBlock, drivenControls, environment);
            second.process(secondBlock, drivenControls, environment);
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < 512; ++sample)
                    expectWithinAbsoluteError(
                        firstBlock.getSample(channel, sample),
                        secondBlock.getSample(channel, sample), 0.0f);
        }
        first.reset();
        second.reset();
        for (int block = 0; block < 20; ++block)
        {
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < 512; ++sample)
                {
                    const auto input = 0.4f * std::sin(
                        juce::MathConstants<float>::twoPi * 220.0f
                        * static_cast<float>(block * 512 + sample)
                        / static_cast<float>(sampleRate));
                    firstBlock.setSample(channel, sample, input);
                    secondBlock.setSample(channel, sample, input);
                }
            first.process(firstBlock, drivenControls, environment);
            second.process(secondBlock, drivenControls, environment);
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < 512; ++sample)
                    expectWithinAbsoluteError(
                        firstBlock.getSample(channel, sample),
                        secondBlock.getSample(channel, sample), 0.0f);
        }

        beginTest("Output is finite and bounded for mono and stereo");
        OptoLevelerModule monoModule;
        OptoLevelerModule stereoModule;
        monoModule.prepare({ sampleRate, 512, 1 });
        stereoModule.prepare(spec);
        auto hotControls = defaults;
        hotControls[OptoLevelerModule::peakReductionControl] = 1.0f;
        hotControls[OptoLevelerModule::gainControl] = 1.0f;
        juce::AudioBuffer<float> monoBlock(1, 512);
        juce::AudioBuffer<float> stereoBlock(2, 512);
        auto monoMaximum = 0.0f;
        auto stereoMaximum = 0.0f;
        for (int block = 0; block < 60; ++block)
        {
            for (int sample = 0; sample < 512; ++sample)
            {
                const auto input = 0.9f * std::sin(
                    juce::MathConstants<float>::twoPi * 660.0f
                    * static_cast<float>(block * 512 + sample)
                    / static_cast<float>(sampleRate));
                monoBlock.setSample(0, sample, input);
                stereoBlock.setSample(0, sample, input);
                stereoBlock.setSample(1, sample, input * 0.6f);
            }
            monoModule.process(monoBlock, hotControls, environment);
            stereoModule.process(stereoBlock, hotControls, environment);
            for (int sample = 0; sample < 512; ++sample)
            {
                const auto monoOut = monoBlock.getSample(0, sample);
                expect(std::isfinite(monoOut));
                monoMaximum = juce::jmax(monoMaximum, std::abs(monoOut));
                for (int channel = 0; channel < 2; ++channel)
                {
                    const auto stereoOut =
                        stereoBlock.getSample(channel, sample);
                    expect(std::isfinite(stereoOut));
                    stereoMaximum =
                        juce::jmax(stereoMaximum, std::abs(stereoOut));
                }
            }
        }
        expect(monoMaximum < 4.0f);
        expect(stereoMaximum < 4.0f);

        beginTest(
            "Program-dependent attack and dual-memory release shape "
            "gain reduction over time");
        OptoLevelerModule timing;
        timing.prepare(spec);
        environment.captureTelemetry = true;
        auto timingControls = defaults;
        timingControls[OptoLevelerModule::peakReductionControl] = 0.9f;
        timingControls[OptoLevelerModule::cellAgeControl] = 0.9f;
        juce::AudioBuffer<float> timingBlock(2, 512);
        timingBlock.clear();
        // Warm the cell up on silence first.
        for (int block = 0; block < 4; ++block)
            timing.process(timingBlock, timingControls, environment);
        // Then hit it with a sustained loud tone and watch reduction grow
        // in from zero, with the slow memory lagging further behind.
        auto firstBlockReduction = -1.0f;
        auto laterBlockReduction = -1.0f;
        auto firstBlockMemory = -1.0f;
        auto laterBlockMemory = -1.0f;
        for (int block = 0; block < 60; ++block)
        {
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < 512; ++sample)
                    timingBlock.setSample(channel, sample, 0.95f * std::sin(
                        juce::MathConstants<float>::twoPi * 1000.0f
                        * static_cast<float>(block * 512 + sample)
                        / static_cast<float>(sampleRate)));
            timing.process(timingBlock, timingControls, environment);
            ContinuousTelemetrySnapshot snapshot;
            expect(timing.readContinuousTelemetry(snapshot));
            if (block == 0)
            {
                firstBlockReduction =
                    snapshot.values[OptoLevelerModule::gainReductionDb];
                firstBlockMemory =
                    snapshot.values[OptoLevelerModule::cellMemory];
            }
            if (block == 59)
            {
                laterBlockReduction =
                    snapshot.values[OptoLevelerModule::gainReductionDb];
                laterBlockMemory =
                    snapshot.values[OptoLevelerModule::cellMemory];
            }
        }
        // Reduction ramps in progressively (attack is not instantaneous).
        expect(laterBlockReduction > firstBlockReduction);
        // The slow memory cell builds up persistence across the sustain.
        expect(laterBlockMemory > firstBlockMemory);

        beginTest("Compress and Limit curves are audibly distinct");
        OptoLevelerModule compressModule;
        OptoLevelerModule limitModule;
        compressModule.prepare(spec);
        limitModule.prepare(spec);
        auto compressControls = defaults;
        compressControls[OptoLevelerModule::peakReductionControl] = 0.9f;
        compressControls[OptoLevelerModule::compressLimitControl] = 0.0f;
        auto limitControls = compressControls;
        limitControls[OptoLevelerModule::compressLimitControl] = 1.0f;
        juce::AudioBuffer<float> compressBlock(2, 512);
        juce::AudioBuffer<float> limitBlock(2, 512);
        environment.captureTelemetry = true;
        auto compressReduction = 0.0f;
        auto limitReduction = 0.0f;
        for (int block = 0; block < 60; ++block)
        {
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < 512; ++sample)
                {
                    const auto input = 0.95f * std::sin(
                        juce::MathConstants<float>::twoPi * 500.0f
                        * static_cast<float>(block * 512 + sample)
                        / static_cast<float>(sampleRate));
                    compressBlock.setSample(channel, sample, input);
                    limitBlock.setSample(channel, sample, input);
                }
            compressModule.process(compressBlock, compressControls,
                                    environment);
            limitModule.process(limitBlock, limitControls, environment);
            ContinuousTelemetrySnapshot compressSnapshot;
            ContinuousTelemetrySnapshot limitSnapshot;
            expect(compressModule.readContinuousTelemetry(compressSnapshot));
            expect(limitModule.readContinuousTelemetry(limitSnapshot));
            compressReduction =
                compressSnapshot.values[OptoLevelerModule::gainReductionDb];
            limitReduction =
                limitSnapshot.values[OptoLevelerModule::gainReductionDb];
        }
        expect(std::abs(limitReduction - compressReduction) > 0.5f);

        beginTest("Mix zero is latency-aligned clean audio apart from Output");
        OptoLevelerModule clean;
        clean.prepare(spec);
        auto cleanControls = defaults;
        cleanControls[OptoLevelerModule::mixControl] = 0.0f;
        cleanControls[OptoLevelerModule::outputControl] = 1.0f;
        juce::AudioBuffer<float> cleanBlock(2, 512);
        const auto latency = clean.latencySamples();
        expect(latency > 0);
        environment.captureTelemetry = false;
        auto absoluteSample = 0;
        auto delayedImpulse = 0.0f;
        for (int block = 0; block < 4; ++block)
        {
            cleanBlock.clear();
            if (block == 0)
            {
                cleanBlock.setSample(0, 0, 1.0f);
                cleanBlock.setSample(1, 0, 1.0f);
            }
            clean.process(cleanBlock, cleanControls, environment);
            for (int sample = 0; sample < cleanBlock.getNumSamples();
                 ++sample, ++absoluteSample)
                if (absoluteSample == latency)
                    delayedImpulse = cleanBlock.getSample(0, sample);
        }
        const auto expectedOutputGain = juce::Decibels::decibelsToGain(
            12.0f);
        expectWithinAbsoluteError(
            delayedImpulse, expectedOutputGain, 0.001f);

        beginTest("Synthetic stereo processing cancels to the mono rendering");
        OptoLevelerModule mono;
        OptoLevelerModule stereo;
        mono.prepare({ sampleRate, 512, 1 });
        stereo.prepare(spec);
        auto foldControls = defaults;
        foldControls[OptoLevelerModule::peakReductionControl] = 0.7f;
        foldControls[OptoLevelerModule::stereoLinkControl] = 1.0f;
        juce::AudioBuffer<float> monoFoldBlock(1, 512);
        juce::AudioBuffer<float> stereoFoldBlock(2, 512);
        environment.captureTelemetry = false;
        for (int block = 0; block < 30; ++block)
        {
            for (int sample = 0; sample < 512; ++sample)
            {
                const auto input = 0.5f * std::sin(
                    juce::MathConstants<float>::twoPi * 330.0f
                    * static_cast<float>(block * 512 + sample)
                    / static_cast<float>(sampleRate));
                monoFoldBlock.setSample(0, sample, input);
                stereoFoldBlock.setSample(0, sample, input);
                stereoFoldBlock.setSample(1, sample, input);
            }
            mono.process(monoFoldBlock, foldControls, environment);
            stereo.process(stereoFoldBlock, foldControls, environment);
            for (int sample = 0; sample < 512; ++sample)
            {
                const auto foldDown = 0.5f
                    * (stereoFoldBlock.getSample(0, sample)
                       + stereoFoldBlock.getSample(1, sample));
                expectWithinAbsoluteError(
                    foldDown, monoFoldBlock.getSample(0, sample), 0.0001f);
            }
        }

        beginTest("Telemetry only populates when capture is requested");
        OptoLevelerModule telemetryModule;
        telemetryModule.prepare(spec);
        auto telemetryControls = defaults;
        telemetryControls[OptoLevelerModule::peakReductionControl] = 0.8f;
        juce::AudioBuffer<float> telemetryBlock(2, 512);
        environment.captureTelemetry = false;
        for (int block = 0; block < 5; ++block)
        {
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < 512; ++sample)
                    telemetryBlock.setSample(channel, sample, 0.6f * std::sin(
                        juce::MathConstants<float>::twoPi * 400.0f
                        * static_cast<float>(block * 512 + sample)
                        / static_cast<float>(sampleRate)));
            telemetryModule.process(telemetryBlock, telemetryControls,
                                     environment);
        }
        // FixedAtomicSnapshot::read() always succeeds once reset() has
        // published its cleared snapshot; the real capture-gating signal is
        // that the sequence never advances past that cleared publication
        // while captureTelemetry stays off.
        ContinuousTelemetrySnapshot ungatedSnapshot;
        expect(telemetryModule.readContinuousTelemetry(ungatedSnapshot));
        expectEquals(static_cast<int>(ungatedSnapshot.sequence), 0);

        environment.captureTelemetry = true;
        auto historySeen = false;
        auto sequenceSeen = false;
        for (int block = 0; block < 40; ++block)
        {
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < 512; ++sample)
                    telemetryBlock.setSample(channel, sample, 0.6f * std::sin(
                        juce::MathConstants<float>::twoPi * 400.0f
                        * static_cast<float>(block * 512 + sample)
                        / static_cast<float>(sampleRate)));
            telemetryModule.process(telemetryBlock, telemetryControls,
                                     environment);
            ContinuousTelemetrySnapshot gatedSnapshot;
            expect(telemetryModule.readContinuousTelemetry(gatedSnapshot));
            expect(gatedSnapshot.valueCount
                   == OptoLevelerModule::telemetryValueCount);
            historySeen = historySeen || gatedSnapshot.historyCount > 0;
            sequenceSeen = sequenceSeen || gatedSnapshot.sequence > 0;
        }
        expect(historySeen);
        expect(sequenceSeen);
    }
};

static OptoLevelerTests optoLevelerTests;
} // namespace
