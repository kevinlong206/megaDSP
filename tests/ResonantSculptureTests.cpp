#include "DspModules.h"
#include "modules/ModuleRegistry.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace
{
class ResonantSculptureTests final : public juce::UnitTest
{
public:
    ResonantSculptureTests() : UnitTest("Resonant Sculpture") {}

    void runTest() override
    {
        using namespace megadsp;
        constexpr double rate = 48000.0;
        const juce::dsp::ProcessSpec spec { rate, 256, 2 };
        auto controls = moduleDefaults(ModuleType::resonantSculpture);

        beginTest("Mix zero is exact immediate dry");
        ResonantSculptureModule dryModule;
        dryModule.prepare(spec);
        auto dryControls = controls;
        dryControls[ResonantSculptureModule::mixControl] = 0.0f;
        auto dry = makeSignal(2, 24000, rate);
        const auto reference = dry;
        render(dryModule, dry, dryControls);
        expect(maxDifference(dry, reference) < 0.000001f);
        expectEquals(dryModule.latencySamples(), 0);

        beginTest("Default sculpture creates a sustained modal field");
        ResonantSculptureModule sculpture;
        sculpture.prepare(spec);
        auto signal = makeSignal(2, 96000, rate);
        const auto unprocessed = signal;
        render(sculpture, signal, controls, true);
        expectFinite(signal);
        expect(maxDifference(signal, unprocessed) > 0.01f);
        expect(signal.getRMSLevel(0, 48000, 48000) > 0.0005f);
        expect(signal.getMagnitude(0, 0, signal.getNumSamples()) < 2.0f);
        ContinuousTelemetrySnapshot telemetry;
        expect(sculpture.readContinuousTelemetry(telemetry));
        expect(telemetry.sequence > 0);
        expect(
            telemetry.values[ResonantSculptureModule::overallEnergy] > 0.0f);
        expect(
            telemetry.values[ResonantSculptureModule::firstModeEnergy] > 0.0f);

        beginTest("Modal root follows the incoming fundamental");
        ResonantSculptureModule lowTracker;
        ResonantSculptureModule highTracker;
        lowTracker.prepare(spec);
        highTracker.prepare(spec);
        auto lowTone = makeTone(110.0f, 48000, rate);
        auto highTone = makeTone(220.0f, 48000, rate);
        render(lowTracker, lowTone, controls, true);
        render(highTracker, highTone, controls, true);
        ContinuousTelemetrySnapshot lowTracking;
        ContinuousTelemetrySnapshot highTracking;
        expect(lowTracker.readContinuousTelemetry(lowTracking));
        expect(highTracker.readContinuousTelemetry(highTracking));
        expectWithinAbsoluteError(
            lowTracking.values[ResonantSculptureModule::trackedFrequencyHz],
            110.0f, 3.0f);
        expectWithinAbsoluteError(
            highTracking.values[ResonantSculptureModule::trackedFrequencyHz],
            220.0f, 5.0f);
        expect(
            lowTracking.values[ResonantSculptureModule::trackingConfidence]
                > 0.4f);
        expect(
            highTracking.values[ResonantSculptureModule::trackingConfidence]
                > 0.4f);

        beginTest("Materials produce distinct modal spectra");
        std::array<juce::AudioBuffer<float>, 4> materials {
            makeSignal(2, 48000, rate), makeSignal(2, 48000, rate),
            makeSignal(2, 48000, rate), makeSignal(2, 48000, rate)
        };
        for (int material = 0; material < 4; ++material)
        {
            ResonantSculptureModule module;
            module.prepare(spec);
            auto materialControls = controls;
            materialControls[ResonantSculptureModule::materialControl] =
                megadsp::discreteValue(material, 4);
            render(module, materials[static_cast<size_t>(material)],
                   materialControls);
            expectFinite(materials[static_cast<size_t>(material)]);
        }
        expect(maxDifference(materials[0], materials[1]) > 0.002f);
        expect(maxDifference(materials[1], materials[2]) > 0.002f);
        expect(maxDifference(materials[2], materials[3]) > 0.002f);

        beginTest("Bow and strike have distinct excitation roles");
        auto bowControls = controls;
        bowControls[ResonantSculptureModule::bowControl] = 1.0f;
        bowControls[ResonantSculptureModule::strikeControl] = 0.0f;
        auto strikeControls = controls;
        strikeControls[ResonantSculptureModule::bowControl] = 0.0f;
        strikeControls[ResonantSculptureModule::strikeControl] = 1.0f;
        ResonantSculptureModule bowed;
        ResonantSculptureModule struck;
        bowed.prepare(spec);
        struck.prepare(spec);
        auto bowSignal = makeSignal(2, 48000, rate);
        auto strikeSignal = bowSignal;
        render(bowed, bowSignal, bowControls);
        render(struck, strikeSignal, strikeControls);
        expect(maxDifference(bowSignal, strikeSignal) > 0.005f);

        beginTest("Stereo field preserves mono fold-down");
        auto narrowControls = controls;
        narrowControls[ResonantSculptureModule::stereoFieldControl] = 0.0f;
        auto wideControls = controls;
        wideControls[ResonantSculptureModule::stereoFieldControl] = 1.0f;
        ResonantSculptureModule narrow;
        ResonantSculptureModule wide;
        narrow.prepare(spec);
        wide.prepare(spec);
        auto narrowSignal = makeSignal(2, 48000, rate);
        auto wideSignal = narrowSignal;
        render(narrow, narrowSignal, narrowControls);
        render(wide, wideSignal, wideControls);
        auto foldDifference = 0.0f;
        double sideEnergy = 0.0;
        for (int sample = 0; sample < narrowSignal.getNumSamples(); ++sample)
        {
            const auto narrowFold =
                narrowSignal.getSample(0, sample)
                + narrowSignal.getSample(1, sample);
            const auto wideFold =
                wideSignal.getSample(0, sample)
                + wideSignal.getSample(1, sample);
            foldDifference = juce::jmax(
                foldDifference, std::abs(narrowFold - wideFold));
            const auto side =
                wideSignal.getSample(0, sample)
                - wideSignal.getSample(1, sample);
            sideEnergy += static_cast<double>(side) * side;
        }
        expect(foldDifference < 0.000002f);
        expect(sideEnergy > 0.0001);

        beginTest("Mono fallback and deterministic reset are stable");
        ResonantSculptureModule deterministic;
        deterministic.prepare({ rate, 256, 1 });
        auto first = makeSignal(1, 48000, rate);
        render(deterministic, first, controls);
        deterministic.reset();
        auto second = makeSignal(1, 48000, rate);
        render(deterministic, second, controls);
        expectFinite(first);
        expect(maxDifference(first, second) < 0.000001f);

        beginTest("Extreme automation remains finite and bounded");
        ResonantSculptureModule automated;
        automated.prepare(spec);
        auto automatedSignal = makeSignal(2, 96000, rate);
        for (int offset = 0; offset < automatedSignal.getNumSamples();
             offset += 256)
        {
            const auto count =
                juce::jmin(256, automatedSignal.getNumSamples() - offset);
            auto changing = controls;
            const auto phase =
                static_cast<float>((offset / 256) % 11) / 10.0f;
            changing[ResonantSculptureModule::materialControl] =
                megadsp::discreteValue((offset / 256) % 4, 4);
            changing[ResonantSculptureModule::shapeControl] = phase;
            changing[ResonantSculptureModule::tuningControl] = 1.0f - phase;
            changing[ResonantSculptureModule::decayControl] = 1.0f;
            changing[ResonantSculptureModule::mutationControl] = 1.0f;
            changing[ResonantSculptureModule::bowControl] = 1.0f;
            changing[ResonantSculptureModule::strikeControl] = 1.0f;
            changing[ResonantSculptureModule::chaosControl] = 1.0f;
            changing[ResonantSculptureModule::mixControl] = 1.0f;
            juce::AudioBuffer<float> block(
                automatedSignal.getArrayOfWritePointers(), 2, offset, count);
            automated.process(block, changing, {});
        }
        expectFinite(automatedSignal);
        expect(automatedSignal.getMagnitude(0, 0, 96000) < 2.0f);
        expect(automatedSignal.getMagnitude(1, 0, 96000) < 2.0f);
        expect(sculpture.tailSeconds(controls) > 0.1);
        expect(sculpture.tailSeconds(controls) <= 12.0);
    }

private:
    static juce::AudioBuffer<float> makeSignal(
        int channels, int samples, double rate)
    {
        juce::AudioBuffer<float> result(channels, samples);
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto time = static_cast<float>(sample / rate);
            const auto burstTime = std::fmod(time, 0.37f);
            const auto envelope = std::exp(-burstTime / 0.055f);
            const auto value = envelope * (
                0.22f * std::sin(
                    juce::MathConstants<float>::twoPi * 146.8f * time)
                + 0.08f * std::sin(
                    juce::MathConstants<float>::twoPi * 293.7f * time));
            result.setSample(0, sample, value);
            if (channels > 1)
                result.setSample(1, sample, value);
        }
        return result;
    }

    static juce::AudioBuffer<float> makeTone(
        float frequency, int samples, double rate)
    {
        juce::AudioBuffer<float> result(2, samples);
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto phase = juce::MathConstants<float>::twoPi
                * frequency * static_cast<float>(sample / rate);
            const auto value = 0.22f * std::sin(phase);
            result.setSample(0, sample, value);
            result.setSample(1, sample, value);
        }
        return result;
    }

    static void render(
        megadsp::ResonantSculptureModule& module,
        juce::AudioBuffer<float>& buffer,
        const megadsp::ControlValues& controls, bool capture = false)
    {
        for (int offset = 0; offset < buffer.getNumSamples(); offset += 256)
        {
            const auto count =
                juce::jmin(256, buffer.getNumSamples() - offset);
            juce::AudioBuffer<float> block(
                buffer.getArrayOfWritePointers(), buffer.getNumChannels(),
                offset, count);
            module.process(block, controls, { nullptr, 120.0, capture });
        }
    }

    void expectFinite(const juce::AudioBuffer<float>& buffer)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                expect(std::isfinite(buffer.getSample(channel, sample)));
    }

    static float maxDifference(
        const juce::AudioBuffer<float>& first,
        const juce::AudioBuffer<float>& second)
    {
        auto maximum = 0.0f;
        for (int channel = 0; channel < first.getNumChannels(); ++channel)
            for (int sample = 0; sample < first.getNumSamples(); ++sample)
                maximum = juce::jmax(
                    maximum,
                    std::abs(
                        first.getSample(channel, sample)
                        - second.getSample(channel, sample)));
        return maximum;
    }
};

ResonantSculptureTests resonantSculptureTests;
} // namespace
