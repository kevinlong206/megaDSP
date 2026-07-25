#include "DspModules.h"
#include "modules/ModuleRegistry.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace
{
class VolumeSwellTests final : public juce::UnitTest
{
public:
    VolumeSwellTests() : UnitTest("Volume Swell") {}

    void runTest() override
    {
        using namespace megadsp;
        constexpr double rate = 48000.0;
        const ProcessEnvironment telemetryEnvironment { nullptr, 120.0, true };
        auto controls = moduleDefaults(ModuleType::volumeSwell);

        beginTest("Mix zero is exact latency-aligned dry");
        VolumeSwellModule dry;
        dry.prepare({ rate, 2048, 1 });
        auto dryControls = controls;
        dryControls[4] = 0.0f;
        juce::AudioBuffer<float> impulse(1, 2048);
        impulse.clear();
        impulse.setSample(0, 0, 0.5f);
        dry.process(impulse, dryControls, {});
        expectWithinAbsoluteError(
            impulse.getSample(0, dry.latencySamples()), 0.5f, 0.000001f);
        for (int sample = 0; sample < dry.latencySamples(); ++sample)
            expectWithinAbsoluteError(
                impulse.getSample(0, sample), 0.0f, 0.000001f);

        beginTest("A chord attack ducks then swells to full level");
        VolumeSwellModule swell;
        swell.prepare({ rate, 96000, 2 });
        auto chord = makeChord(2, 96000, rate, 0.0);
        swell.process(chord, controls, telemetryEnvironment);
        expectFinite(chord);
        expect(rms(chord, 1000, 5000) < 0.02f);
        expect(rms(chord, 70000, 90000) > 0.05f);
        ContinuousTelemetrySnapshot state;
        expect(swell.readContinuousTelemetry(state));
        expect(state.values[VolumeSwellModule::swellGain] > 0.95f);

        beginTest("Swell Time changes the rise duration");
        auto fastControls = controls;
        fastControls[1] = 0.0f;
        VolumeSwellModule fast;
        fast.prepare({ rate, 24000, 1 });
        auto fastChord = makeChord(1, 24000, rate, 0.0);
        fast.process(fastChord, fastControls, {});
        const auto fastLevel = rms(fastChord, 10000, 18000);

        auto slowControls = controls;
        slowControls[1] = 1.0f;
        VolumeSwellModule slow;
        slow.prepare({ rate, 24000, 1 });
        auto slowChord = makeChord(1, 24000, rate, 0.0);
        slow.process(slowChord, slowControls, {});
        const auto slowLevel = rms(slowChord, 10000, 18000);
        expect(fastLevel > slowLevel * 3.0f);

        beginTest("Maximum sensitivity does not retrigger a sustained chord");
        auto maximumSensitivity = controls;
        maximumSensitivity[0] = 1.0f;
        VolumeSwellModule stableDetector;
        stableDetector.prepare({ rate, 64, 1 });
        auto triggerBlocks = 0;
        for (int blockStart = 0; blockStart < 144000; blockStart += 64)
        {
            juce::AudioBuffer<float> block(1, 64);
            for (int sample = 0; sample < block.getNumSamples(); ++sample)
            {
                const auto time = static_cast<float>(
                    (blockStart + sample) / rate);
                block.setSample(
                    0, sample,
                    0.11f
                        * (std::sin(
                               juce::MathConstants<float>::twoPi
                               * 220.0f * time)
                           + 0.7f * std::sin(
                               juce::MathConstants<float>::twoPi
                               * 275.0f * time)));
            }
            stableDetector.process(
                block, maximumSensitivity, telemetryEnvironment);
            ContinuousTelemetrySnapshot detectorState;
            expect(stableDetector.readContinuousTelemetry(detectorState));
            if (detectorState.values[VolumeSwellModule::triggered] > 0.5f)
                ++triggerBlocks;
        }
        expectEquals(triggerBlocks, 1);

        beginTest("A later chord retriggers the swell");
        VolumeSwellModule retrigger;
        retrigger.prepare({ rate, 120000, 2 });
        auto twoChords = makeChord(2, 120000, rate, 0.0);
        auto second = makeChord(2, 72000, rate, 330.0);
        for (int channel = 0; channel < 2; ++channel)
            twoChords.addFrom(channel, 48000, second, channel, 0, 72000);
        retrigger.process(twoChords, fastControls, {});
        const auto beforeSecond = rms(twoChords, 45000, 47500);
        const auto afterSecond = rms(twoChords, 49440, 49920);
        const auto recovered = rms(twoChords, 54000, 60000);
        expect(
            afterSecond < beforeSecond * 0.35f,
            "before=" + juce::String(beforeSecond)
                + " after=" + juce::String(afterSecond));
        expect(
            recovered > afterSecond * 2.0f,
            "after=" + juce::String(afterSecond)
                + " recovered=" + juce::String(recovered));
        expectFinite(twoChords);
    }

private:
    static juce::AudioBuffer<float> makeChord(
        int channels, int samples, double rate, double alternateRoot)
    {
        juce::AudioBuffer<float> result(channels, samples);
        result.clear();
        const auto root = alternateRoot > 0.0 ? alternateRoot : 220.0;
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto time = static_cast<float>(sample / rate);
            const auto attack = juce::jmin(1.0f, time / 0.004f);
            const auto value = 0.11f * attack
                * (std::sin(
                       juce::MathConstants<float>::twoPi
                       * static_cast<float>(root) * time)
                   + 0.7f * std::sin(
                       juce::MathConstants<float>::twoPi
                       * static_cast<float>(root * 1.25) * time));
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
            for (int sample = start; sample < end; ++sample)
            {
                const auto value = buffer.getSample(channel, sample);
                energy += static_cast<double>(value) * value;
                ++count;
            }
        return static_cast<float>(
            std::sqrt(energy / static_cast<double>(juce::jmax(1, count))));
    }

    void expectFinite(const juce::AudioBuffer<float>& buffer)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                expect(std::isfinite(buffer.getSample(channel, sample)));
    }
};

VolumeSwellTests volumeSwellTests;
} // namespace
