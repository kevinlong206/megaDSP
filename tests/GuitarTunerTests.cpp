#include "modules/GuitarTuner.h"
#include "modules/ModuleRegistry.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace
{
class GuitarTunerTests final : public juce::UnitTest
{
public:
    GuitarTunerTests() : juce::UnitTest("Guitar Tuner", "megaDSP") {}

    void runTest() override
    {
        using namespace megadsp;

        beginTest("Registry appends the stable tuner contract");
        expectEquals(static_cast<int>(ModuleType::irMosaic), 58);
        expectEquals(static_cast<int>(ModuleType::guitarTuner), 59);
        expectEquals(moduleTypeCount, 62);
        const auto& definition = moduleDefinition(ModuleType::guitarTuner);
        expect(definition.category == ModuleCategory::guitar);
        expect(definition.capabilities
               == ModuleCapability::continuousTelemetry);
        expectEquals(
            juce::String(controlMetadata(
                ModuleType::guitarTuner,
                GuitarTunerModule::referenceControl).label),
            juce::String("Reference Pitch"));
        expectEquals(
            controlOptions(
                ModuleType::guitarTuner,
                GuitarTunerModule::viewControl).size(),
            GuitarTunerModule::viewCount);
        expectEquals(
            controlOptions(
                ModuleType::guitarTuner,
                GuitarTunerModule::tuningControl).size(),
            GuitarTunerModule::tuningCount);

        beginTest("Steady guitar and bass tones resolve within one tenth cent");
        for (const auto frequency : { 41.203445f, 82.40689f,
                                      110.0f, 329.62756f })
        {
            GuitarTunerModule module;
            module.prepare({ 48000.0, 512, 2 });
            auto controls = moduleDefaults(ModuleType::guitarTuner);
            controls[GuitarTunerModule::tuningControl] =
                discreteValue(0, GuitarTunerModule::tuningCount);
            const auto snapshot =
                renderTone(module, controls, frequency, 0.75f, 0.55);
            expect(snapshot.values[
                       GuitarTunerModule::trackingConfidence] > 0.45f,
                   "Low confidence at "
                       + juce::String(frequency, 3) + " Hz");
            expectWithinAbsoluteError(
                snapshot.values[
                    GuitarTunerModule::detectedFrequencyHz],
                frequency,
                frequency
                    * (std::exp2(0.1f / 1200.0f) - 1.0f));
            expectWithinAbsoluteError(
                snapshot.values[GuitarTunerModule::centsError],
                0.0f, 0.1f);
        }

        beginTest("Sharp and flat offsets retain cent sign and precision");
        for (const auto cents : { -17.35f, 23.60f })
        {
            GuitarTunerModule module;
            module.prepare({ 48000.0, 512, 1 });
            auto controls = moduleDefaults(ModuleType::guitarTuner);
            controls[GuitarTunerModule::tuningControl] =
                discreteValue(0, GuitarTunerModule::tuningCount);
            const auto frequency = 110.0f * std::exp2(cents / 1200.0f);
            const auto snapshot =
                renderTone(module, controls, frequency, 0.75f, 0.55);
            expectWithinAbsoluteError(
                snapshot.values[GuitarTunerModule::centsError],
                cents, 0.12f);
        }

        beginTest("Strong guitar harmonics and anti-phase stereo avoid octave errors");
        {
            GuitarTunerModule module;
            module.prepare({ 48000.0, 512, 2 });
            auto controls = moduleDefaults(ModuleType::guitarTuner);
            controls[GuitarTunerModule::tuningControl] =
                discreteValue(0, GuitarTunerModule::tuningCount);
            const auto snapshot = renderTone(
                module, controls, 82.40689f, 0.62f, 0.65,
                48000.0, true, true);
            expect(snapshot.values[
                       GuitarTunerModule::trackingConfidence] > 0.4f);
            expectWithinAbsoluteError(
                snapshot.values[GuitarTunerModule::centsError],
                0.0f, 0.5f);
            expectEquals(
                juce::roundToInt(snapshot.values[
                    GuitarTunerModule::detectedMidiNote]),
                40);
        }

        beginTest("Supported high sample rates retain cent accuracy");
        for (const auto rate : { 44100.0, 96000.0, 192000.0 })
        {
            GuitarTunerModule module;
            module.prepare({ rate, 512, 1 });
            auto controls = moduleDefaults(ModuleType::guitarTuner);
            controls[GuitarTunerModule::tuningControl] =
                discreteValue(0, GuitarTunerModule::tuningCount);
            const auto snapshot =
                renderTone(
                    module, controls, 196.0f, 0.7f, 0.45, rate);
            expectWithinAbsoluteError(
                snapshot.values[GuitarTunerModule::centsError],
                0.0f, 0.15f,
                "Rate " + juce::String(rate, 0));
        }

        beginTest("Reference pitch and chromatic targets are exact");
        {
            GuitarTunerModule module;
            module.prepare({ 48000.0, 512, 1 });
            auto controls = moduleDefaults(ModuleType::guitarTuner);
            controls[GuitarTunerModule::referenceControl] =
                (442.0f - 430.0f) / 20.0f;
            controls[GuitarTunerModule::tuningControl] =
                discreteValue(0, GuitarTunerModule::tuningCount);
            auto snapshot =
                renderTone(module, controls, 442.0f, 0.7f, 0.55);
            expectWithinAbsoluteError(
                snapshot.values[GuitarTunerModule::targetFrequencyHz],
                442.0f, 0.01f);
            expectWithinAbsoluteError(
                snapshot.values[GuitarTunerModule::centsError],
                0.0f, 0.1f);

            module.reset();
            controls = moduleDefaults(ModuleType::guitarTuner);
            controls[GuitarTunerModule::transposeControl] =
                (1.0f + 12.0f) / 24.0f;
            const auto sharpA =
                110.0f * std::exp2(1.0f / 12.0f);
            snapshot =
                renderTone(module, controls, sharpA, 0.7f, 0.55);
            expectEquals(
                juce::roundToInt(snapshot.values[
                    GuitarTunerModule::targetMidiNote]),
                46);
            expectWithinAbsoluteError(
                snapshot.values[GuitarTunerModule::centsError],
                0.0f, 0.1f);
        }

        beginTest("Detector controls do not color passthrough audio");
        {
            GuitarTunerModule module;
            module.prepare({ 48000.0, 512, 2 });
            auto controls = moduleDefaults(ModuleType::guitarTuner);
            controls[GuitarTunerModule::inputGainControl] = 1.0f;
            juce::AudioBuffer<float> buffer(2, 512);
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                buffer.setSample(
                    0, sample,
                    0.45f * std::sin(
                        juce::MathConstants<float>::twoPi * 997.0f
                        * static_cast<float>(sample) / 48000.0f));
                buffer.setSample(
                    1, sample,
                    -0.31f * std::cos(
                        juce::MathConstants<float>::twoPi * 613.0f
                        * static_cast<float>(sample) / 48000.0f));
            }
            const auto reference = buffer;
            module.process(buffer, controls, {});
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                    expectWithinAbsoluteError(
                        buffer.getSample(channel, sample),
                        reference.getSample(channel, sample), 1.0e-7f);
        }

        beginTest("Legacy output controls cannot alter passthrough");
        {
            GuitarTunerModule module;
            module.prepare({ 48000.0, 512, 1 });
            auto controls = moduleDefaults(ModuleType::guitarTuner);
            controls[GuitarTunerModule::tuningControl] =
                discreteValue(0, GuitarTunerModule::tuningCount);
            auto snapshot =
                renderTone(module, controls, 110.0f, 0.7f, 0.45);
            expect(snapshot.values[
                       GuitarTunerModule::trackingConfidence] > 0.4f);
            controls[GuitarTunerModule::muteControl] = 1.0f;
            juce::AudioBuffer<float> block(1, 512);
            ProcessEnvironment environment;
            environment.captureTelemetry = true;
            for (int iteration = 0; iteration < 12; ++iteration)
            {
                block.clear();
                for (int sample = 0; sample < block.getNumSamples(); ++sample)
                    block.setSample(0, sample, 0.5f);
                module.process(block, controls, environment);
            }
            expectWithinAbsoluteError(
                block.getMagnitude(0, 0, block.getNumSamples()),
                0.5f, 1.0e-7f);

            controls[GuitarTunerModule::muteControl] = 0.0f;
            controls[GuitarTunerModule::holdControl] = 0.0f;
            for (int iteration = 0; iteration < 80; ++iteration)
            {
                block.clear();
                module.process(block, controls, environment);
            }
            const auto timeout =
                juce::Time::getMillisecondCounterHiRes() + 2000.0;
            do
            {
                juce::Thread::sleep(10);
                expect(module.readContinuousTelemetry(snapshot));
            }
            while (snapshot.values[
                       GuitarTunerModule::detectedFrequencyHz] > 0.0f
                   && juce::Time::getMillisecondCounterHiRes() < timeout);
            expect(snapshot.values[
                       GuitarTunerModule::trackingConfidence] < 0.12f);
            expectEquals(
                snapshot.values[GuitarTunerModule::detectedFrequencyHz],
                0.0f);
        }

        beginTest("Telemetry stays Precision and Chromatic");
        {
            GuitarTunerModule module;
            module.prepare({ 44100.0, 256, 1 });
            auto controls = moduleDefaults(ModuleType::guitarTuner);
            controls[GuitarTunerModule::viewControl] =
                discreteValue(2, GuitarTunerModule::viewCount);
            controls[GuitarTunerModule::rangeControl] =
                discreteValue(0, GuitarTunerModule::rangeCount);
            const auto snapshot =
                renderTone(module, controls, 220.0f, 0.6f, 0.5, 44100.0);
            expectEquals(
                juce::roundToInt(snapshot.values[
                    GuitarTunerModule::activeView]),
                1);
            expectEquals(
                juce::roundToInt(snapshot.values[
                    GuitarTunerModule::activeTuning]),
                0);
            expectEquals(
                snapshot.values[
                    GuitarTunerModule::displayRangeCents],
                2.0f);
            expectEquals(
                static_cast<int>(snapshot.historyValueCount),
                static_cast<int>(
                    GuitarTunerModule::telemetryHistoryValueCount));
        }
    }

private:
    static megadsp::ContinuousTelemetrySnapshot renderTone(
        megadsp::GuitarTunerModule& module,
        const megadsp::ControlValues& controls,
        float frequency, float amplitude, double seconds,
        double sampleRate = 48000.0,
        bool guitarLike = false, bool antiPhase = false)
    {
        constexpr int blockSize = 512;
        juce::AudioBuffer<float> block(2, blockSize);
        megadsp::ProcessEnvironment environment;
        environment.captureTelemetry = true;
        auto phase = 0.0;
        const auto phaseStep =
            juce::MathConstants<double>::twoPi
            * static_cast<double>(frequency) / sampleRate;
        const auto totalSamples =
            juce::roundToInt(seconds * sampleRate);
        for (int rendered = 0; rendered < totalSamples;
             rendered += blockSize)
        {
            for (int sample = 0; sample < blockSize; ++sample)
            {
                const auto waveform = guitarLike
                    ? 0.34 * std::sin(phase)
                        + 0.82 * std::sin(phase * 2.0)
                        + 0.43 * std::sin(phase * 3.0)
                        + 0.18 * std::sin(phase * 4.0)
                    : std::sin(phase);
                const auto value =
                    amplitude * static_cast<float>(waveform);
                phase += phaseStep;
                if (phase >= juce::MathConstants<double>::twoPi)
                    phase -= juce::MathConstants<double>::twoPi;
                block.setSample(0, sample, value);
                block.setSample(
                    1, sample, antiPhase ? -value : value);
            }
            module.process(block, controls, environment);
        }
        megadsp::ContinuousTelemetrySnapshot snapshot;
        std::uint64_t previousSequence = 0;
        auto stableReads = 0;
        const auto timeout =
            juce::Time::getMillisecondCounterHiRes() + 2500.0;
        do
        {
            juce::Thread::sleep(20);
            if (!module.readContinuousTelemetry(snapshot)
                || snapshot.sequence == 0)
                continue;
            if (snapshot.sequence == previousSequence)
                ++stableReads;
            else
                stableReads = 0;
            previousSequence = snapshot.sequence;
        }
        while (stableReads < 5
               && juce::Time::getMillisecondCounterHiRes() < timeout);
        return snapshot;
    }
};

GuitarTunerTests guitarTunerTests;
} // namespace
