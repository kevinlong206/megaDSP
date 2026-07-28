#include "DspModules.h"
#include "modules/ModuleRegistry.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <limits>

namespace
{
class StudioPlateTests final : public juce::UnitTest
{
public:
    StudioPlateTests() : UnitTest("Studio Plate") {}

    void runTest() override
    {
        using namespace megadsp;
        constexpr double rate = 48000.0;
        const juce::dsp::ProcessSpec spec { rate, 512, 2 };
        auto controls = moduleDefaults(ModuleType::studioPlate);

        beginTest("Wet zero is exact immediate dry with zero host PDC");
        StudioPlateModule dry;
        dry.prepare(spec);
        auto dryControls = controls;
        dryControls[StudioPlateModule::wetControl] = 0.0f;
        auto drySignal = makeSignal(2, 8192, rate);
        const auto reference = drySignal;
        render(dry, drySignal, dryControls);
        expect(maxDifference(drySignal, reference) < 0.000001f);
        expectEquals(dry.latencySamples(), 0);

        beginTest("An impulse produces a bounded, decaying tail");
        StudioPlateModule impulseModule;
        impulseModule.prepare(spec);
        auto impulse = makeImpulse(2, static_cast<int>(rate * 3.5), rate);
        render(impulseModule, impulse, controls);
        expectFinite(impulse);
        const auto earlyRms =
            blockRms(impulse, 0, static_cast<int>(rate * 0.1));
        const auto midRms = blockRms(
            impulse, static_cast<int>(rate * 0.5),
            static_cast<int>(rate * 0.1));
        const auto lateRms = blockRms(
            impulse, static_cast<int>(rate * 3.0),
            static_cast<int>(rate * 0.1));
        expect(earlyRms > 0.0001f);
        expect(midRms < earlyRms);
        expect(lateRms < midRms);
        expect(lateRms < earlyRms * 0.05f);
        expect(impulseModule.tailSeconds(controls) >= 0.2);
        expect(impulseModule.tailSeconds(controls) <= 8.0);

        beginTest("Higher Decay lengthens the measured tail");
        auto shortDecayControls = controls;
        shortDecayControls[StudioPlateModule::decayControl] = 0.0f;
        auto longDecayControls = controls;
        longDecayControls[StudioPlateModule::decayControl] = 1.0f;
        StudioPlateModule shortDecay;
        StudioPlateModule longDecay;
        shortDecay.prepare(spec);
        longDecay.prepare(spec);
        auto shortSignal = makeImpulse(2, static_cast<int>(rate * 4.0), rate);
        auto longSignal = shortSignal;
        render(shortDecay, shortSignal, shortDecayControls);
        render(longDecay, longSignal, longDecayControls);
        const auto shortLateRms = blockRms(
            shortSignal, static_cast<int>(rate * 1.5),
            static_cast<int>(rate * 0.1));
        const auto longLateRms = blockRms(
            longSignal, static_cast<int>(rate * 1.5),
            static_cast<int>(rate * 0.1));
        expect(longLateRms > shortLateRms);
        expect(shortDecay.tailSeconds(shortDecayControls)
               < longDecay.tailSeconds(longDecayControls));

        beginTest("Higher Damping shortens high-band decay relative to low");
        auto lowDampingControls = controls;
        lowDampingControls[StudioPlateModule::dampingControl] = 0.0f;
        auto highDampingControls = controls;
        highDampingControls[StudioPlateModule::dampingControl] = 1.0f;
        StudioPlateModule lowDamping;
        StudioPlateModule highDamping;
        lowDamping.prepare(spec);
        highDamping.prepare(spec);
        auto lowDampingSignal =
            makeImpulse(2, static_cast<int>(rate * 3.0), rate);
        auto highDampingSignal = lowDampingSignal;
        render(lowDamping, lowDampingSignal, lowDampingControls);
        render(highDamping, highDampingSignal, highDampingControls);
        const auto lowDampingHighBand =
            highBandEnergy(lowDampingSignal, static_cast<int>(rate * 1.0));
        const auto highDampingHighBand =
            highBandEnergy(highDampingSignal, static_cast<int>(rate * 1.0));
        expect(highDampingHighBand < lowDampingHighBand);

        beginTest("Stereo Width and Pickup Spread preserve mono fold-down");
        auto narrowControls = controls;
        narrowControls[StudioPlateModule::widthControl] = 0.0f;
        narrowControls[StudioPlateModule::pickupSpreadControl] = 0.0f;
        auto wideControls = controls;
        wideControls[StudioPlateModule::widthControl] = 1.0f;
        wideControls[StudioPlateModule::pickupSpreadControl] = 1.0f;
        StudioPlateModule narrow;
        StudioPlateModule wide;
        narrow.prepare(spec);
        wide.prepare(spec);
        auto narrowSignal = makeSignal(2, 48000, rate);
        auto wideSignal = narrowSignal;
        render(narrow, narrowSignal, narrowControls);
        render(wide, wideSignal, wideControls);
        auto foldDifference = 0.0f;
        auto wideSideEnergy = 0.0;
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
            wideSideEnergy += static_cast<double>(side) * side;
        }
        expect(foldDifference < 0.000002f);
        expect(wideSideEnergy > 0.0001);

        beginTest("Mono input is independent of Width and Pickup Spread");
        StudioPlateModule monoNarrow;
        StudioPlateModule monoWide;
        monoNarrow.prepare({ rate, 512, 1 });
        monoWide.prepare({ rate, 512, 1 });
        auto firstMono = makeSignal(1, 24000, rate);
        auto secondMono = firstMono;
        render(monoNarrow, firstMono, narrowControls);
        render(monoWide, secondMono, wideControls);
        expect(maxDifference(firstMono, secondMono) < 0.000002f);

        beginTest(
            "Duplicated-mono stereo input remains stable and mono-safe");
        StudioPlateModule duplicated;
        duplicated.prepare(spec);
        auto duplicatedSignal = makeSignal(1, 24000, rate);
        juce::AudioBuffer<float> stereoFromMono(2, duplicatedSignal.getNumSamples());
        stereoFromMono.copyFrom(0, 0, duplicatedSignal, 0, 0, duplicatedSignal.getNumSamples());
        stereoFromMono.copyFrom(1, 0, duplicatedSignal, 0, 0, duplicatedSignal.getNumSamples());
        render(duplicated, stereoFromMono, wideControls);
        expectFinite(stereoFromMono);

        beginTest("Asymmetric and hard-panned stereo input stays finite");
        StudioPlateModule asymmetric;
        asymmetric.prepare(spec);
        juce::AudioBuffer<float> panned(2, 24000);
        for (int sample = 0; sample < panned.getNumSamples(); ++sample)
        {
            const auto time = static_cast<float>(sample / rate);
            panned.setSample(
                0, sample,
                0.4f * std::sin(
                    juce::MathConstants<float>::twoPi * 220.0f * time));
            panned.setSample(1, sample, 0.0f);
        }
        render(asymmetric, panned, wideControls);
        expectFinite(panned);

        beginTest("Plate Size change is click-safe and keeps the tail alive");
        StudioPlateModule sizeChange;
        sizeChange.prepare(spec);
        auto sizeSignal =
            makeImpulse(2, static_cast<int>(rate * 2.0), rate);
        auto compactControls = controls;
        compactControls[StudioPlateModule::plateSizeControl] =
            discreteValue(0, StudioPlateModule::plateSizeCount);
        // Let the tail build up under the Compact size for a while.
        {
            juce::AudioBuffer<float> block(
                sizeSignal.getArrayOfWritePointers(), 2, 0,
                static_cast<int>(rate * 0.6));
            for (int offset = 0; offset < block.getNumSamples(); offset += 512)
            {
                const auto count =
                    juce::jmin(512, block.getNumSamples() - offset);
                juce::AudioBuffer<float> chunk(
                    block.getArrayOfWritePointers(), 2, offset, count);
                sizeChange.process(chunk, compactControls, {});
            }
        }
        const auto beforeChangeTailRms = blockRms(
            sizeSignal, static_cast<int>(rate * 0.5),
            static_cast<int>(rate * 0.05));
        auto largeControls = controls;
        largeControls[StudioPlateModule::plateSizeControl] =
            discreteValue(2, StudioPlateModule::plateSizeCount);
        auto maxStepDifference = 0.0f;
        auto previousSample = sizeSignal.getSample(0, static_cast<int>(rate * 0.6) - 1);
        for (int offset = static_cast<int>(rate * 0.6);
             offset < sizeSignal.getNumSamples(); offset += 512)
        {
            const auto count =
                juce::jmin(512, sizeSignal.getNumSamples() - offset);
            juce::AudioBuffer<float> chunk(
                sizeSignal.getArrayOfWritePointers(), 2, offset, count);
            sizeChange.process(chunk, largeControls, {});
            for (int sample = 0; sample < count; ++sample)
            {
                const auto current = sizeSignal.getSample(0, offset + sample);
                maxStepDifference = juce::jmax(
                    maxStepDifference, std::abs(current - previousSample));
                previousSample = current;
            }
        }
        expectFinite(sizeSignal);
        // No sample-to-sample discontinuity from the structural change:
        // bounded by what a smoothly decaying, band-limited tail can move
        // in a single sample at this sample rate.
        expect(maxStepDifference < 0.35f);
        const auto afterChangeTailRms = blockRms(
            sizeSignal, static_cast<int>(rate * 0.65),
            static_cast<int>(rate * 0.05));
        // The tail is not cleared by the structural change: energy shortly
        // after the transition remains a substantial fraction of the tail
        // energy captured just before it, rather than dropping to silence.
        expect(afterChangeTailRms > beforeChangeTailRms * 0.1f);
        expect(afterChangeTailRms > 0.00002f);

        beginTest("Low Cut and High Cut filter the excitation");
        auto openControls = controls;
        openControls[StudioPlateModule::lowCutControl] = 0.0f;
        openControls[StudioPlateModule::highCutControl] = 1.0f;
        auto narrowBandControls = controls;
        narrowBandControls[StudioPlateModule::lowCutControl] = 1.0f;
        narrowBandControls[StudioPlateModule::highCutControl] = 0.0f;
        StudioPlateModule openExcitation;
        StudioPlateModule narrowExcitation;
        openExcitation.prepare(spec);
        narrowExcitation.prepare(spec);
        auto lowToneSignal = makeTone(2, 24000, rate, 60.0f);
        auto lowToneSignalNarrow = lowToneSignal;
        megadsp::ProcessEnvironment captureEnvironment;
        captureEnvironment.captureTelemetry = true;
        render(openExcitation, lowToneSignal, openControls, captureEnvironment);
        render(
            narrowExcitation, lowToneSignalNarrow, narrowBandControls,
            captureEnvironment);
        expectFinite(lowToneSignal);
        expectFinite(lowToneSignalNarrow);
        ContinuousTelemetrySnapshot openTelemetry;
        ContinuousTelemetrySnapshot narrowTelemetry;
        expect(openExcitation.readContinuousTelemetry(openTelemetry));
        expect(narrowExcitation.readContinuousTelemetry(narrowTelemetry));
        // A 60 Hz tone excites the plate freely when Low Cut is at its
        // lowest setting, but is almost entirely removed before it can
        // excite the plate when Low Cut is at its highest setting (with
        // High Cut also fully closed down), so the narrow-band excitation
        // telemetry should read substantially lower.
        expect(
            narrowTelemetry.values[StudioPlateModule::excitationLevel]
            < openTelemetry.values[StudioPlateModule::excitationLevel] * 0.5f);

        beginTest("Ducking attenuates only the completed wet path");
        auto duckedControls = controls;
        duckedControls[StudioPlateModule::dryControl] = 0.0f;
        duckedControls[StudioPlateModule::wetControl] = 1.0f;
        duckedControls[StudioPlateModule::duckingControl] = 1.0f;
        auto unduckedControls = duckedControls;
        unduckedControls[StudioPlateModule::duckingControl] = 0.0f;
        StudioPlateModule ducked;
        StudioPlateModule unducked;
        ducked.prepare(spec);
        unducked.prepare(spec);
        auto duckSignal = makeLoudBurstThenSilence(2, static_cast<int>(rate * 2.0), rate);
        auto unduckSignal = duckSignal;
        render(ducked, duckSignal, duckedControls);
        render(unducked, unduckSignal, unduckedControls);
        const auto duckedSustain = blockRms(
            duckSignal, static_cast<int>(rate * 0.3),
            static_cast<int>(rate * 0.1));
        const auto unduckedSustain = blockRms(
            unduckSignal, static_cast<int>(rate * 0.3),
            static_cast<int>(rate * 0.1));
        expect(duckedSustain < unduckedSustain);
        // Ducking never affects the (already zero here) dry path directly;
        // both renders remain finite and bounded.
        expectFinite(duckSignal);
        expectFinite(unduckSignal);

        beginTest("Telemetry is only meaningful when capture-gated");
        StudioPlateModule telemetryModule;
        telemetryModule.prepare(spec);
        auto telemetrySignal = makeSignal(2, 4096, rate);
        {
            juce::AudioBuffer<float> block(
                telemetrySignal.getArrayOfWritePointers(), 2, 0,
                telemetrySignal.getNumSamples());
            telemetryModule.process(block, controls, {});
        }
        ContinuousTelemetrySnapshot uncaptured;
        const auto hadUncaptured =
            telemetryModule.readContinuousTelemetry(uncaptured);
        expect(!hadUncaptured || uncaptured.sequence == 0);
        {
            juce::AudioBuffer<float> block(
                telemetrySignal.getArrayOfWritePointers(), 2, 0,
                telemetrySignal.getNumSamples());
            megadsp::ProcessEnvironment environment;
            environment.captureTelemetry = true;
            telemetryModule.process(block, controls, environment);
        }
        ContinuousTelemetrySnapshot captured;
        expect(telemetryModule.readContinuousTelemetry(captured));
        expect(captured.sequence > 0);
        expect(captured.valueCount >= StudioPlateModule::telemetryValueCount);
        expect(captured.values[StudioPlateModule::excitationLevel] >= 0.0f);
        expect(captured.values[StudioPlateModule::leftPickupLevel] >= 0.0f);
        expect(captured.values[StudioPlateModule::rightPickupLevel] >= 0.0f);
        expect(captured.values[StudioPlateModule::lowBandLevel] >= 0.0f);
        expect(captured.values[StudioPlateModule::midBandLevel] >= 0.0f);
        expect(captured.values[StudioPlateModule::highBandLevel] >= 0.0f);
        expect(captured.values[StudioPlateModule::modalActivityLevel] >= 0.0f);

        beginTest("Feedback extremes and automation stay finite and bounded");
        StudioPlateModule stressed;
        stressed.prepare(spec);
        juce::AudioBuffer<float> automated(2, 96000);
        for (int offset = 0; offset < automated.getNumSamples(); offset += 512)
        {
            const auto count =
                juce::jmin(512, automated.getNumSamples() - offset);
            for (int sample = 0; sample < count; ++sample)
            {
                const auto absolute = offset + sample;
                const auto time = static_cast<float>(absolute / rate);
                automated.setSample(
                    0, absolute,
                    0.3f * std::sin(
                        juce::MathConstants<float>::twoPi * 233.0f * time));
                automated.setSample(
                    1, absolute,
                    0.22f * std::sin(
                        juce::MathConstants<float>::twoPi * 349.0f * time));
            }
            auto changing = controls;
            const auto phase = static_cast<float>(offset / 512 % 5) / 4.0f;
            changing[StudioPlateModule::plateSizeControl] =
                discreteValue(offset / 512 % 3, StudioPlateModule::plateSizeCount);
            changing[StudioPlateModule::decayControl] = phase;
            changing[StudioPlateModule::dampingControl] = 1.0f - phase;
            changing[StudioPlateModule::preDelayControl] = phase;
            changing[StudioPlateModule::lowCutControl] = phase;
            changing[StudioPlateModule::highCutControl] = 1.0f - phase;
            changing[StudioPlateModule::pickupSpreadControl] = 1.0f;
            changing[StudioPlateModule::widthControl] = 1.0f;
            changing[StudioPlateModule::duckingControl] = 1.0f;
            changing[StudioPlateModule::wetControl] = 1.0f;
            juce::AudioBuffer<float> block(
                automated.getArrayOfWritePointers(), 2, offset, count);
            stressed.process(block, changing, {});
        }
        expectFinite(automated);
        expect(automated.getMagnitude(0, 0, automated.getNumSamples()) < 6.0f);
        expect(automated.getMagnitude(1, 0, automated.getNumSamples()) < 6.0f);
        expect(stressed.tailSeconds(controls) >= 0.2);
        expect(stressed.tailSeconds(controls) <= 8.0);

        beginTest("Reset restores deterministic behaviour");
        StudioPlateModule deterministic;
        deterministic.prepare(spec);
        auto first = makeSignal(2, 24000, rate);
        render(deterministic, first, controls);
        deterministic.reset();
        auto second = makeSignal(2, 24000, rate);
        render(deterministic, second, controls);
        expect(maxDifference(first, second) < 0.000001f);
    }

private:
    static juce::AudioBuffer<float> makeSignal(
        int channels, int samples, double rate)
    {
        juce::AudioBuffer<float> result(channels, samples);
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto time = static_cast<float>(sample / rate);
            const auto envelope =
                0.35f + 0.65f * std::exp(-std::fmod(time, 0.7f) / 0.16f);
            const auto left = envelope * (
                0.15f * std::sin(
                    juce::MathConstants<float>::twoPi * 196.0f * time)
                + 0.08f * std::sin(
                    juce::MathConstants<float>::twoPi * 784.0f * time)
                + 0.035f * std::sin(
                    juce::MathConstants<float>::twoPi * 3136.0f * time));
            result.setSample(0, sample, left);
            if (channels > 1)
                result.setSample(
                    1, sample,
                    left * 0.82f
                        + 0.025f * std::sin(
                            juce::MathConstants<float>::twoPi * 293.0f * time));
        }
        return result;
    }

    static juce::AudioBuffer<float> makeTone(
        int channels, int samples, double rate, float frequencyHz)
    {
        juce::AudioBuffer<float> result(channels, samples);
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto time = static_cast<float>(sample / rate);
            const auto value = 0.3f * std::sin(
                juce::MathConstants<float>::twoPi * frequencyHz * time);
            for (int channel = 0; channel < channels; ++channel)
                result.setSample(channel, sample, value);
        }
        return result;
    }

    static juce::AudioBuffer<float> makeImpulse(
        int channels, int samples, double rate)
    {
        juce::ignoreUnused(rate);
        juce::AudioBuffer<float> result(channels, samples);
        result.clear();
        for (int channel = 0; channel < channels; ++channel)
            result.setSample(channel, 0, 0.9f);
        return result;
    }

    static juce::AudioBuffer<float> makeLoudBurstThenSilence(
        int channels, int samples, double rate)
    {
        juce::AudioBuffer<float> result(channels, samples);
        const auto burstSamples = static_cast<int>(rate * 0.2);
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto time = static_cast<float>(sample / rate);
            const auto value = sample < burstSamples
                ? 0.85f * std::sin(
                      juce::MathConstants<float>::twoPi * 400.0f * time)
                : 0.0f;
            for (int channel = 0; channel < channels; ++channel)
                result.setSample(channel, sample, value);
        }
        return result;
    }

    static void render(
        megadsp::StudioPlateModule& module,
        juce::AudioBuffer<float>& buffer,
        const megadsp::ControlValues& controls,
        const megadsp::ProcessEnvironment& environment = {})
    {
        for (int offset = 0; offset < buffer.getNumSamples(); offset += 512)
        {
            const auto count =
                juce::jmin(512, buffer.getNumSamples() - offset);
            juce::AudioBuffer<float> block(
                buffer.getArrayOfWritePointers(), buffer.getNumChannels(),
                offset, count);
            module.process(block, controls, environment);
        }
    }

    static float blockRms(
        const juce::AudioBuffer<float>& buffer, int start, int length)
    {
        const auto safeStart = juce::jlimit(0, buffer.getNumSamples() - 1, start);
        const auto safeLength =
            juce::jmax(1, juce::jmin(length, buffer.getNumSamples() - safeStart));
        double energy = 0.0;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = safeStart; sample < safeStart + safeLength;
                 ++sample)
            {
                const auto value = buffer.getSample(channel, sample);
                energy += static_cast<double>(value) * value;
            }
        return static_cast<float>(std::sqrt(
            energy
            / static_cast<double>(safeLength * buffer.getNumChannels())));
    }

    static float highBandEnergy(
        const juce::AudioBuffer<float>& buffer, int start)
    {
        double energy = 0.0;
        const auto end = buffer.getNumSamples();
        for (int sample = juce::jmax(1, start); sample < end; ++sample)
        {
            const auto difference =
                buffer.getSample(0, sample) - buffer.getSample(0, sample - 1);
            energy += static_cast<double>(difference) * difference;
        }
        return static_cast<float>(energy);
    }

    static void expectFiniteBuffer(
        juce::UnitTest& test, const juce::AudioBuffer<float>& buffer)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                test.expect(std::isfinite(buffer.getSample(channel, sample)));
    }

    void expectFinite(const juce::AudioBuffer<float>& buffer)
    {
        expectFiniteBuffer(*this, buffer);
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

StudioPlateTests studioPlateTests;
} // namespace
