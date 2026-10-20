#include "../src/DspModules.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace
{
class VinylSimulatorTests final : public juce::UnitTest
{
public:
    VinylSimulatorTests()
        : juce::UnitTest("Vinyl Simulator", "megaDSP")
    {
    }

    void runTest() override
    {
        using namespace megadsp;
        constexpr auto sampleRate = 48000.0;
        const juce::dsp::ProcessSpec spec { sampleRate, 512, 2 };
        ProcessEnvironment environment;

        beginTest("Registry appends Vinyl Simulator without shifting old types");
        expectEquals(static_cast<int>(ModuleType::stageVocal), 48);
        expectEquals(static_cast<int>(ModuleType::vinylSimulator), 49);
        expectEquals(moduleTypeCount, 61);
        const auto& definition =
            moduleDefinition(ModuleType::vinylSimulator);
        expect(definition.category == ModuleCategory::saturationAndColor);
        expect(definition.capabilities
               == ModuleCapability::continuousTelemetry);
        for (int control = 0; control < controlsPerSlot; ++control)
            expect(definition.controls[static_cast<size_t>(control)].isActive());

        beginTest("Control values use musician-facing units");
        const auto defaults = moduleDefaults(ModuleType::vinylSimulator);
        expect(formatControlValue(
                   ModuleType::vinylSimulator,
                   VinylSimulatorModule::surfaceNoiseControl,
                   defaults[VinylSimulatorModule::surfaceNoiseControl])
                   .endsWith("dB"));
        expect(formatControlValue(
                   ModuleType::vinylSimulator,
                   VinylSimulatorModule::wowControl,
                   defaults[VinylSimulatorModule::wowControl])
                   .endsWith("ms"));
        const auto parsed = parseControlValue(
            ModuleType::vinylSimulator,
            VinylSimulatorModule::rumbleControl, "-60 dB");
        expect(parsed.has_value());
        expectWithinAbsoluteError(
            parsed.value_or(0.0f),
            (-60.0f + 90.0f) / 54.0f, 0.0001f);

        beginTest("Mix zero is latency-aligned clean audio");
        VinylSimulatorModule clean;
        clean.prepare(spec);
        auto cleanControls = defaults;
        cleanControls[VinylSimulatorModule::mixControl] = 0.0f;
        juce::AudioBuffer<float> cleanBlock(2, 512);
        const auto latency = clean.latencySamples();
        expect(latency > 0);
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
        expectWithinAbsoluteError(delayedImpulse, 1.0f, 0.00001f);

        beginTest("Reset makes stochastic processing deterministic");
        VinylSimulatorModule first;
        VinylSimulatorModule second;
        first.prepare(spec);
        second.prepare(spec);
        auto wornControls = defaults;
        wornControls[VinylSimulatorModule::conditionControl] = 0.8f;
        wornControls[VinylSimulatorModule::crackleControl] = 0.9f;
        wornControls[VinylSimulatorModule::dustControl] = 1.0f;
        wornControls[VinylSimulatorModule::scratchControl] = 0.8f;
        wornControls[VinylSimulatorModule::surfaceNoiseControl] = 0.75f;
        wornControls[VinylSimulatorModule::rumbleControl] = 0.7f;
        wornControls[VinylSimulatorModule::wowControl] = 0.8f;
        wornControls[VinylSimulatorModule::flutterControl] = 0.7f;
        wornControls[VinylSimulatorModule::stereoControl] = 0.8f;
        juce::AudioBuffer<float> firstBlock(2, 512);
        juce::AudioBuffer<float> secondBlock(2, 512);
        for (int block = 0; block < 40; ++block)
        {
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < 512; ++sample)
                {
                    const auto input = 0.1f * std::sin(
                        juce::MathConstants<float>::twoPi * 440.0f
                        * static_cast<float>(block * 512 + sample)
                        / static_cast<float>(sampleRate));
                    firstBlock.setSample(channel, sample, input);
                    secondBlock.setSample(channel, sample, input);
                }
            first.process(firstBlock, wornControls, environment);
            second.process(secondBlock, wornControls, environment);
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < 512; ++sample)
                    expectWithinAbsoluteError(
                        firstBlock.getSample(channel, sample),
                        secondBlock.getSample(channel, sample), 0.0f);
        }

        beginTest("Surface texture is finite bounded and telemetered");
        VinylSimulatorModule texture;
        texture.prepare(spec);
        auto textureControls = wornControls;
        textureControls[VinylSimulatorModule::mixControl] = 1.0f;
        environment.captureTelemetry = true;
        juce::AudioBuffer<float> textureBlock(2, 512);
        auto maximum = 0.0f;
        auto crackleSeen = false;
        auto historySeen = false;
        for (int block = 0; block < 300; ++block)
        {
            textureBlock.clear();
            texture.process(textureBlock, textureControls, environment);
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < 512; ++sample)
                {
                    const auto output =
                        textureBlock.getSample(channel, sample);
                    expect(std::isfinite(output));
                    maximum = juce::jmax(maximum, std::abs(output));
                }
            ContinuousTelemetrySnapshot telemetry;
            expect(texture.readContinuousTelemetry(telemetry));
            crackleSeen = crackleSeen
                || telemetry.values[
                       VinylSimulatorModule::leftCrackleActivity] > 0.0001f
                || telemetry.values[
                       VinylSimulatorModule::rightCrackleActivity] > 0.0001f;
            historySeen = historySeen || telemetry.historyCount > 0;
        }
        expect(maximum > 0.0001f);
        expect(maximum < 4.0f);
        expect(crackleSeen);
        expect(historySeen);

        beginTest("Synthetic stereo texture cancels to the mono rendering");
        VinylSimulatorModule mono;
        VinylSimulatorModule stereo;
        mono.prepare({ sampleRate, 512, 1 });
        stereo.prepare(spec);
        juce::AudioBuffer<float> monoBlock(1, 512);
        juce::AudioBuffer<float> stereoBlock(2, 512);
        environment.captureTelemetry = false;
        for (int block = 0; block < 30; ++block)
        {
            monoBlock.clear();
            stereoBlock.clear();
            mono.process(monoBlock, wornControls, environment);
            stereo.process(stereoBlock, wornControls, environment);
            for (int sample = 0; sample < 512; ++sample)
            {
                const auto foldDown = 0.5f
                    * (stereoBlock.getSample(0, sample)
                       + stereoBlock.getSample(1, sample));
                expectWithinAbsoluteError(
                    foldDown, monoBlock.getSample(0, sample), 0.00001f);
            }
        }
    }
};

static VinylSimulatorTests vinylSimulatorTests;
} // namespace
