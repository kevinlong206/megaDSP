#include "DspModules.h"
#include "modules/ModuleRegistry.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace
{
class ChordLoomTests final : public juce::UnitTest
{
public:
    ChordLoomTests() : UnitTest("Chord Loom") {}

    void runTest() override
    {
        using namespace megadsp;
        constexpr double rate = 48000.0;
        const ProcessEnvironment capture { nullptr, 120.0, true };

        beginTest("A strummed chord becomes a long non-looping sustain");
        auto controls = moduleDefaults(ModuleType::chordLoom);
        ChordLoomModule module;
        module.prepare({ rate, 96000, 2 });
        expectEquals(module.tailSeconds(controls), 120.0);
        expectEquals(module.snapshotSpillSeconds(controls), 1.5);
        auto first = makeChord(2, 96000, rate, { 220.0f, 277.18f, 329.63f });
        module.process(first, controls, capture);
        expectFinite(first);
        expect(rms(first, 72000, 96000) > 0.0001f);
        ContinuousTelemetrySnapshot firstState;
        expect(module.readContinuousTelemetry(firstState));
        expect(firstState.sequence == 1);
        const auto firstLayer = juce::roundToInt(
            firstState.values[ChordLoomModule::activeLayer]);
        expect(firstLayer >= 0 && firstLayer < ChordLoomModule::layerCount);

        const auto comparison = segmentDifference(
            first, 56000, 80000, 4096);
        expect(comparison > 0.000001f,
               "Held texture repeated an identical captured segment");

        beginTest("Sensitivity exposes and applies the input threshold");
        auto quietChord = makeChord(
            2, 48000, rate, { 220.0f, 277.18f, 329.63f });
        quietChord.applyGain(0.08f);
        auto insensitiveControls = controls;
        insensitiveControls[0] = 0.0f;
        ChordLoomModule insensitive;
        insensitive.prepare({ rate, 48000, 2 });
        insensitive.process(quietChord, insensitiveControls, capture);
        ContinuousTelemetrySnapshot insensitiveState;
        expect(insensitive.readContinuousTelemetry(insensitiveState));
        expectWithinAbsoluteError(
            insensitiveState.values[ChordLoomModule::inputThreshold],
            juce::Decibels::decibelsToGain(-24.0f), 0.000001f);
        expect(insensitiveState.values[ChordLoomModule::activeLayer] < 0.0f);

        auto sensitiveChord = makeChord(
            2, 48000, rate, { 220.0f, 277.18f, 329.63f });
        sensitiveChord.applyGain(0.08f);
        auto sensitiveControls = controls;
        sensitiveControls[0] = 1.0f;
        ChordLoomModule sensitive;
        sensitive.prepare({ rate, 48000, 2 });
        sensitive.process(sensitiveChord, sensitiveControls, capture);
        ContinuousTelemetrySnapshot sensitiveState;
        expect(sensitive.readContinuousTelemetry(sensitiveState));
        expectWithinAbsoluteError(
            sensitiveState.values[ChordLoomModule::inputThreshold],
            juce::Decibels::decibelsToGain(-60.0f), 0.000001f);
        expect(sensitiveState.values[ChordLoomModule::captureTriggered] > 0.5f);
        expect(sensitiveState.values[ChordLoomModule::activeLayer] >= 0.0f);

        beginTest("A new chord smoothly takes ownership of another layer");
        auto second = makeChord(
            2, 96000, rate, { 293.66f, 369.99f, 440.0f });
        module.process(second, controls, capture);
        expectFinite(second);
        expect(rms(second, 72000, 96000) > 0.0001f);
        ContinuousTelemetrySnapshot secondState;
        expect(module.readContinuousTelemetry(secondState));
        const auto secondLayer = juce::roundToInt(
            secondState.values[ChordLoomModule::activeLayer]);
        expect(secondLayer >= 0 && secondLayer < ChordLoomModule::layerCount);
        expect(secondLayer != firstLayer);
        auto gainSum = 0.0f;
        for (int layer = 0; layer < ChordLoomModule::layerCount; ++layer)
            gainSum += secondState.values[static_cast<size_t>(
                ChordLoomModule::layerGain0 + layer)];
        expect(gainSum > 0.95f && gainSum < 1.05f);

        beginTest("Three successive chords rotate through all hold layers");
        auto third = makeChord(
            2, 96000, rate, { 329.63f, 415.30f, 493.88f });
        module.process(third, controls, capture);
        expectFinite(third);
        ContinuousTelemetrySnapshot thirdState;
        expect(module.readContinuousTelemetry(thirdState));
        const auto thirdLayer = juce::roundToInt(
            thirdState.values[ChordLoomModule::activeLayer]);
        expect(thirdLayer >= 0 && thirdLayer < ChordLoomModule::layerCount);
        expect(thirdLayer != firstLayer);
        expect(thirdLayer != secondLayer);

        beginTest("Close chord changes preserve in-progress layer gains");
        auto fastControls = controls;
        fastControls[1] = 0.0f;
        fastControls[2] = 1.0f;
        ChordLoomModule fastChanges;
        fastChanges.prepare({ rate, 64, 2 });
        auto previousGainSum = 0.0f;
        auto maximumGainJump = 0.0f;
        for (int blockStart = 0; blockStart < 72000; blockStart += 64)
        {
            juce::AudioBuffer<float> block(2, 64);
            for (int sample = 0; sample < block.getNumSamples(); ++sample)
            {
                const auto absoluteSample = blockStart + sample;
                const auto strum = absoluteSample / 24000;
                const auto strumSample = absoluteSample % 24000;
                auto value = 0.0f;
                if (strum < 3 && strumSample < 5760)
                {
                    const auto time =
                        static_cast<float>(strumSample / rate);
                    const auto envelope =
                        juce::jmin(1.0f, time / 0.004f)
                        * std::exp(-time / 0.08f);
                    const auto root =
                        196.0f + 49.0f * static_cast<float>(strum);
                    value = 0.12f * envelope
                        * (std::sin(
                               juce::MathConstants<float>::twoPi
                               * root * time)
                           + std::sin(
                               juce::MathConstants<float>::twoPi
                               * root * 1.25f * time));
                }
                block.setSample(0, sample, value);
                block.setSample(1, sample, value);
            }
            fastChanges.process(block, fastControls, capture);
            ContinuousTelemetrySnapshot state;
            expect(fastChanges.readContinuousTelemetry(state));
            auto currentGainSum = 0.0f;
            for (int layer = 0; layer < ChordLoomModule::layerCount; ++layer)
                currentGainSum += state.values[static_cast<size_t>(
                    ChordLoomModule::layerGain0 + layer)];
            maximumGainJump = juce::jmax(
                maximumGainJump,
                std::abs(currentGainSum - previousGainSum));
            previousGainSum = currentGainSum;
        }
        expect(maximumGainJump < 0.02f);

        beginTest("Dry-only is exact and mono processing stays finite");
        auto dryControls = controls;
        dryControls[10] = 0.0f;
        ChordLoomModule dryModule;
        dryModule.prepare({ rate, 4096, 1 });
        juce::AudioBuffer<float> dry(1, 4096);
        for (int sample = 0; sample < dry.getNumSamples(); ++sample)
            dry.setSample(
                0, sample,
                0.2f * std::sin(
                    juce::MathConstants<float>::twoPi * 311.0f
                    * static_cast<float>(sample / rate)));
        const auto reference = dry;
        dryModule.process(dry, dryControls, {});
        for (int sample = 0; sample < dry.getNumSamples(); ++sample)
            expectWithinAbsoluteError(
                dry.getSample(0, sample), reference.getSample(0, sample),
                0.000001f);
        expectFinite(dry);

        beginTest("Reset makes capture and grain motion deterministic");
        ChordLoomModule deterministic;
        deterministic.prepare({ rate, 48000, 2 });
        auto deterministicFirst = makeChord(
            2, 48000, rate, { 196.0f, 246.94f, 293.66f });
        deterministic.process(deterministicFirst, controls, {});
        deterministic.reset();
        auto deterministicSecond = makeChord(
            2, 48000, rate, { 196.0f, 246.94f, 293.66f });
        deterministic.process(deterministicSecond, controls, {});
        expect(maxDifference(deterministicFirst, deterministicSecond)
               < 0.000001f);
    }

private:
    static juce::AudioBuffer<float> makeChord(
        int channels, int samples, double rate,
        std::array<float, 3> frequencies)
    {
        juce::AudioBuffer<float> result(channels, samples);
        result.clear();
        const auto soundingSamples =
            juce::jmin(samples, juce::roundToInt(rate * 1.05));
        for (int sample = 0; sample < soundingSamples; ++sample)
        {
            const auto time = static_cast<float>(sample / rate);
            const auto attack = juce::jmin(1.0f, time / 0.006f);
            const auto decay = std::exp(-time / 1.8f);
            auto value = 0.0f;
            for (const auto frequency : frequencies)
                value += std::sin(
                    juce::MathConstants<float>::twoPi * frequency * time);
            value *= attack * decay * 0.10f;
            for (int channel = 0; channel < channels; ++channel)
                result.setSample(channel, sample, value);
        }
        return result;
    }

    static float rms(
        const juce::AudioBuffer<float>& buffer, int start, int end)
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

    static float segmentDifference(
        const juce::AudioBuffer<float>& buffer, int first, int second,
        int length)
    {
        auto maximum = 0.0f;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = 0; sample < length; ++sample)
                maximum = juce::jmax(
                    maximum,
                    std::abs(buffer.getSample(channel, first + sample)
                             - buffer.getSample(channel, second + sample)));
        return maximum;
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
                    std::abs(first.getSample(channel, sample)
                             - second.getSample(channel, sample)));
        return maximum;
    }

    void expectFinite(const juce::AudioBuffer<float>& buffer)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                expect(std::isfinite(buffer.getSample(channel, sample)));
    }
};

ChordLoomTests chordLoomTests;
} // namespace
