#include "modules/ModuleRegistry.h"
#include "modules/ProgramEqualizer.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <limits>

namespace
{
constexpr double sampleRate = 48000.0;

float linearNormalized(float low, float high, float value)
{
    return (value - low) / (high - low);
}

using megadsp::ControlValues;
using megadsp::ProgramEqualizerModule;

// Mirrors the already-approved defaultsFor(ModuleType::programEqualizer)
// entry in ModuleRegistry.cpp.
ControlValues programEqualizerDefaults()
{
    ControlValues controls {};
    controls[ProgramEqualizerModule::lowFrequencyControl] =
        megadsp::discreteValue(1, ProgramEqualizerModule::lowFrequencyCount);
    controls[ProgramEqualizerModule::lowBoostControl] =
        linearNormalized(0.0f, 14.0f, 0.0f);
    controls[ProgramEqualizerModule::lowAttenuationControl] =
        linearNormalized(0.0f, 18.0f, 0.0f);
    controls[ProgramEqualizerModule::highBoostFrequencyControl] =
        megadsp::discreteValue(3, ProgramEqualizerModule::highBoostFrequencyCount);
    controls[ProgramEqualizerModule::highBoostControl] =
        linearNormalized(0.0f, 18.0f, 0.0f);
    controls[ProgramEqualizerModule::bandwidthControl] = 0.45f;
    controls[ProgramEqualizerModule::highAttenuationFrequencyControl] =
        megadsp::discreteValue(
            1, ProgramEqualizerModule::highAttenuationFrequencyCount);
    controls[ProgramEqualizerModule::highAttenuationControl] =
        linearNormalized(0.0f, 18.0f, 0.0f);
    controls[ProgramEqualizerModule::driveControl] =
        linearNormalized(-12.0f, 18.0f, 0.0f);
    controls[ProgramEqualizerModule::outputControl] =
        linearNormalized(-18.0f, 12.0f, 0.0f);
    return controls;
}

juce::AudioBuffer<float> makeSine(
    int channels, int samples, float frequency, float amplitude,
    float phaseOffset = 0.0f)
{
    juce::AudioBuffer<float> buffer(channels, samples);
    for (int sample = 0; sample < samples; ++sample)
    {
        const auto value = amplitude * std::sin(
            phaseOffset + juce::MathConstants<float>::twoPi * frequency
            * static_cast<float>(sample) / static_cast<float>(sampleRate));
        for (int channel = 0; channel < channels; ++channel)
            buffer.setSample(channel, sample, value);
    }
    return buffer;
}

void settle(
    ProgramEqualizerModule& module, const ControlValues& controls,
    juce::AudioBuffer<float>& buffer, int blocks)
{
    const megadsp::ProcessEnvironment environment;
    for (int block = 0; block < blocks; ++block)
        module.process(buffer, controls, environment);
}

// Runs a fresh module to a settled steady state at the given controls and
// returns the RMS of the final settling block at the probe frequency.
float settledRms(
    const ControlValues& controls, float probeFrequency, int blocks = 60)
{
    ProgramEqualizerModule module;
    module.prepare({ sampleRate, 512, 2 });
    auto buffer = makeSine(2, 512, probeFrequency, 0.5f);
    settle(module, controls, buffer, blocks);
    double sumSquares = 0.0;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        sumSquares += static_cast<double>(buffer.getSample(0, sample))
            * static_cast<double>(buffer.getSample(0, sample));
    return static_cast<float>(
        std::sqrt(sumSquares / static_cast<double>(buffer.getNumSamples())));
}

class ProgramEqualizerTests final : public juce::UnitTest
{
public:
    ProgramEqualizerTests() : UnitTest("Program Equalizer") {}

    void runTest() override
    {
        using namespace megadsp;
        const juce::dsp::ProcessSpec stereoSpec { sampleRate, 512, 2 };
        const juce::dsp::ProcessSpec monoSpec { sampleRate, 257, 1 };
        const auto defaults = programEqualizerDefaults();

        beginTest(
            "Registry contract matches the Program Equalizer control layout");
        {
            expectEquals(static_cast<int>(ModuleType::programEqualizer), 52);
            expectEquals(static_cast<int>(ModuleType::studioPlate), 53);
            expectEquals(moduleTypeCount, 62);
            const auto& definition =
                moduleDefinition(ModuleType::programEqualizer);
            expect(definition.category == ModuleCategory::eqAndFilters);
            expect(definition.capabilities
                   == ModuleCapability::continuousTelemetry);
            for (int control = 0; control < 10; ++control)
                expect(definition.controls[static_cast<size_t>(control)]
                           .isActive());
            for (int control = 10; control < controlsPerSlot; ++control)
                expect(!definition.controls[static_cast<size_t>(control)]
                            .isActive());

            expect(formatControlValue(
                       ModuleType::programEqualizer,
                       ProgramEqualizerModule::lowBoostControl,
                       defaults[ProgramEqualizerModule::lowBoostControl])
                       .endsWith("dB"));
            expect(formatControlValue(
                       ModuleType::programEqualizer,
                       ProgramEqualizerModule::bandwidthControl,
                       defaults[ProgramEqualizerModule::bandwidthControl])
                       .endsWith("sharp"));
            const auto parsedDrive = parseControlValue(
                ModuleType::programEqualizer,
                ProgramEqualizerModule::driveControl, "6 dB");
            expect(parsedDrive.has_value());
            expectWithinAbsoluteError(
                parsedDrive.value_or(0.0f),
                linearNormalized(-12.0f, 18.0f, 6.0f), 0.0001f);
        }

        beginTest(
            "Low boost and attenuation create a resonant interaction "
            "instead of cancelling");
        {
            auto flatControls = defaults;
            auto bothControls = defaults;
            bothControls[ProgramEqualizerModule::lowBoostControl] = 1.0f;
            bothControls[ProgramEqualizerModule::lowAttenuationControl] = 1.0f;
            auto boostOnlyControls = defaults;
            boostOnlyControls[ProgramEqualizerModule::lowBoostControl] = 1.0f;
            auto attenuationOnlyControls = defaults;
            attenuationOnlyControls[
                ProgramEqualizerModule::lowAttenuationControl] = 1.0f;

            // 30 Hz is the default Low Frequency selection.
            const auto flatRms = settledRms(flatControls, 30.0f);
            const auto bothRms = settledRms(bothControls, 30.0f);
            const auto boostOnlyRms = settledRms(boostOnlyControls, 30.0f);
            const auto attenuationOnlyRms =
                settledRms(attenuationOnlyControls, 30.0f);

            // Simultaneous boost and attenuation must not cancel back to the
            // flat response...
            expect(std::abs(bothRms - flatRms) > 0.01f);
            // ...and must differ from either network acting alone, proving a
            // genuine interaction rather than one network dominating.
            expect(std::abs(bothRms - boostOnlyRms) > 0.01f);
            expect(std::abs(bothRms - attenuationOnlyRms) > 0.01f);
        }

        beginTest("Continuous parameter changes remain click-safe");
        {
            ProgramEqualizerModule module;
            module.prepare(stereoSpec);
            auto controls = defaults;
            auto buffer = makeSine(2, 512, 300.0f, 0.4f);
            settle(module, controls, buffer, 20);

            controls[ProgramEqualizerModule::highBoostControl] = 1.0f;
            controls[ProgramEqualizerModule::driveControl] = 1.0f;
            auto maxDelta = 0.0f;
            for (int block = 0; block < 6; ++block)
            {
                auto stepBuffer = makeSine(2, 512, 300.0f, 0.4f);
                module.process(stepBuffer, controls, {});
                auto previous = stepBuffer.getSample(0, 0);
                for (int sample = 1; sample < stepBuffer.getNumSamples();
                     ++sample)
                {
                    const auto current = stepBuffer.getSample(0, sample);
                    maxDelta = juce::jmax(
                        maxDelta, std::abs(current - previous));
                    previous = current;
                }
            }
            expect(maxDelta < 0.2f);
        }

        beginTest("Output stays finite for mono and stereo under extreme settings");
        {
            ControlValues extreme {};
            extreme[ProgramEqualizerModule::lowFrequencyControl] = 1.0f;
            extreme[ProgramEqualizerModule::lowBoostControl] = 1.0f;
            extreme[ProgramEqualizerModule::lowAttenuationControl] = 1.0f;
            extreme[ProgramEqualizerModule::highBoostFrequencyControl] = 1.0f;
            extreme[ProgramEqualizerModule::highBoostControl] = 1.0f;
            extreme[ProgramEqualizerModule::bandwidthControl] = 1.0f;
            extreme[ProgramEqualizerModule::highAttenuationFrequencyControl] =
                1.0f;
            extreme[ProgramEqualizerModule::highAttenuationControl] = 1.0f;
            extreme[ProgramEqualizerModule::driveControl] = 1.0f;
            extreme[ProgramEqualizerModule::outputControl] = 1.0f;

            ProgramEqualizerModule mono;
            mono.prepare(monoSpec);
            ProgramEqualizerModule stereo;
            stereo.prepare(stereoSpec);
            juce::Random random(1234);
            for (int block = 0; block < 40; ++block)
            {
                juce::AudioBuffer<float> monoBuffer(1, 257);
                juce::AudioBuffer<float> stereoBuffer(2, 512);
                for (int sample = 0; sample < monoBuffer.getNumSamples();
                     ++sample)
                    monoBuffer.setSample(
                        0, sample, random.nextFloat() * 2.0f - 1.0f);
                for (int sample = 0; sample < stereoBuffer.getNumSamples();
                     ++sample)
                {
                    const auto value = random.nextFloat() * 2.0f - 1.0f;
                    stereoBuffer.setSample(0, sample, value);
                    stereoBuffer.setSample(1, sample, -value);
                }

                mono.process(monoBuffer, extreme, {});
                stereo.process(stereoBuffer, extreme, {});
                for (int sample = 0; sample < monoBuffer.getNumSamples();
                     ++sample)
                    expect(std::isfinite(monoBuffer.getSample(0, sample)));
                for (int channel = 0; channel < 2; ++channel)
                    for (int sample = 0; sample < stereoBuffer.getNumSamples();
                         ++sample)
                        expect(std::isfinite(
                            stereoBuffer.getSample(channel, sample)));
            }
        }

        beginTest("High bands remain stable below 44.1 kHz");
        {
            auto controls = defaults;
            controls[ProgramEqualizerModule::highBoostFrequencyControl] = 1.0f;
            controls[ProgramEqualizerModule::highBoostControl] = 1.0f;
            controls[ProgramEqualizerModule::bandwidthControl] = 1.0f;
            controls[
                ProgramEqualizerModule::highAttenuationFrequencyControl] = 1.0f;
            controls[ProgramEqualizerModule::highAttenuationControl] = 1.0f;
            controls[ProgramEqualizerModule::driveControl] = 1.0f;
            for (const auto lowRate
                 : { 16000.0, 22050.0, 32000.0, 40000.0 })
            {
                ProgramEqualizerModule module;
                module.prepare({ lowRate, 257, 2 });
                juce::Random lowRateRandom(static_cast<int>(lowRate));
                for (int pass = 0; pass < 100; ++pass)
                {
                    juce::AudioBuffer<float> buffer(2, 257);
                    for (int sample = 0; sample < buffer.getNumSamples();
                         ++sample)
                    {
                        const auto input =
                            lowRateRandom.nextFloat() * 0.5f - 0.25f;
                        buffer.setSample(0, sample, input);
                        buffer.setSample(1, sample, -input);
                    }
                    module.process(buffer, controls, {});
                    for (int channel = 0; channel < 2; ++channel)
                        for (int sample = 0;
                             sample < buffer.getNumSamples(); ++sample)
                        {
                            const auto output =
                                buffer.getSample(channel, sample);
                            expect(std::isfinite(output));
                            expect(std::abs(output) < 8.0f);
                        }
                }
            }
        }

        beginTest("Reports one fixed oversampled latency regardless of parameters");
        {
            ProgramEqualizerModule module;
            module.prepare(stereoSpec);
            const auto fixedLatency = module.latencySamples();
            expectGreaterThan(fixedLatency, 0);
            auto controls = defaults;
            for (int pass = 0; pass < 8; ++pass)
            {
                controls[ProgramEqualizerModule::driveControl] =
                    (pass % 2 == 0) ? 0.0f : 1.0f;
                controls[ProgramEqualizerModule::highBoostControl] =
                    (pass % 3 == 0) ? 1.0f : 0.2f;
                auto buffer = makeSine(2, 512, 220.0f, 0.3f);
                module.process(buffer, controls, {});
                expectEquals(module.latencySamples(), fixedLatency);
            }
        }

        beginTest(
            "Controls beyond Output do not exist for this module; Mix is "
            "not part of Program Equalizer");
        {
            ProgramEqualizerModule reference;
            reference.prepare(stereoSpec);
            auto referenceBuffer = makeSine(2, 512, 440.0f, 0.5f);
            auto referenceControls = defaults;
            settle(reference, referenceControls, referenceBuffer, 10);

            ProgramEqualizerModule tampered;
            tampered.prepare(stereoSpec);
            auto tamperedBuffer = makeSine(2, 512, 440.0f, 0.5f);
            auto tamperedControls = defaults;
            tamperedControls[10] = 0.83f;
            tamperedControls[11] = 0.27f;
            settle(tampered, tamperedControls, tamperedBuffer, 10);

            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < referenceBuffer.getNumSamples();
                     ++sample)
                    expectWithinAbsoluteError(
                        tamperedBuffer.getSample(channel, sample),
                        referenceBuffer.getSample(channel, sample), 0.00001f);
        }

        beginTest("Continuous telemetry is capture-gated with matching lane counts");
        {
            ProgramEqualizerModule module;
            module.prepare(stereoSpec);
            auto controls = defaults;
            controls[ProgramEqualizerModule::driveControl] = 0.8f;
            controls[ProgramEqualizerModule::highBoostControl] = 0.6f;

            auto untrackedBuffer = makeSine(2, 512, 200.0f, 0.6f);
            module.process(untrackedBuffer, controls, { nullptr, 120.0, false });
            ContinuousTelemetrySnapshot untracked;
            const auto hasUntracked =
                module.readContinuousTelemetry(untracked);
            expect(!hasUntracked || untracked.sequence == 0);

            auto trackedBuffer = makeSine(2, 512, 200.0f, 0.6f);
            module.process(trackedBuffer, controls, { nullptr, 120.0, true });
            ContinuousTelemetrySnapshot tracked;
            expect(module.readContinuousTelemetry(tracked));
            expectEquals(
                static_cast<int>(tracked.valueCount),
                static_cast<int>(ProgramEqualizerModule::telemetryValueCount));
            expectEquals(
                static_cast<int>(tracked.historyValueCount),
                static_cast<int>(
                    ProgramEqualizerModule::telemetryHistoryCount));
            expectGreaterThan(static_cast<int>(tracked.sequence), 0);
            expect(tracked.values[ProgramEqualizerModule::driveActivity] >= 0.0f);
            expect(std::isfinite(
                tracked.values[ProgramEqualizerModule::makeupGainDb]));

            auto secondBuffer = makeSine(2, 512, 200.0f, 0.6f);
            module.process(secondBuffer, controls, { nullptr, 120.0, true });
            ContinuousTelemetrySnapshot secondTracked;
            expect(module.readContinuousTelemetry(secondTracked));
            expectGreaterThan(
                static_cast<int>(secondTracked.sequence),
                static_cast<int>(tracked.sequence));

            // Engaging the low network should register a nonzero interaction
            // reading distinct from the untouched high network.
            auto interactionControls = defaults;
            interactionControls[ProgramEqualizerModule::lowBoostControl] = 1.0f;
            interactionControls[
                ProgramEqualizerModule::lowAttenuationControl] = 1.0f;
            ProgramEqualizerModule interactionModule;
            interactionModule.prepare(stereoSpec);
            auto interactionBuffer = makeSine(2, 512, 30.0f, 0.5f);
            settle(interactionModule, interactionControls, interactionBuffer, 40);
            interactionModule.process(
                interactionBuffer, interactionControls,
                { nullptr, 120.0, true });
            ContinuousTelemetrySnapshot interactionTelemetry;
            expect(interactionModule.readContinuousTelemetry(
                interactionTelemetry));
            expectGreaterThan(
                interactionTelemetry.values[
                    ProgramEqualizerModule::lowInteractionActivity],
                0.0f);
        }
    }
};

ProgramEqualizerTests programEqualizerTests;
} // namespace
