#include "modules/FetLimiter.h"
#include "modules/ModuleRegistry.h"

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

float exponentialNormalized(float low, float high, float value)
{
    return std::log(value / low) / std::log(high / low);
}

using megadsp::ControlValues;
using megadsp::FetLimiterModule;

// Mirrors the already-approved defaultsFor(ModuleType::fetLimiter) entry in
// ModuleRegistry.cpp so these tests stay independent of the separate
// registry/UI-factory integration step that still needs to wire this module
// into the shared registry, format/parse, and view-factory tables.
ControlValues fetLimiterDefaults()
{
    ControlValues controls {};
    controls[FetLimiterModule::inputControl] = linearNormalized(-24.0f, 24.0f, 0.0f);
    controls[FetLimiterModule::outputControl] = linearNormalized(-24.0f, 24.0f, 0.0f);
    controls[FetLimiterModule::attackControl] =
        exponentialNormalized(20.0f, 800.0f, 200.0f);
    controls[FetLimiterModule::releaseControl] =
        exponentialNormalized(50.0f, 1100.0f, 300.0f);
    controls[FetLimiterModule::ratioControl] = megadsp::discreteValue(0, FetLimiterModule::ratioCount);
    controls[FetLimiterModule::detectorLowCutControl] =
        exponentialNormalized(20.0f, 300.0f, 60.0f);
    controls[FetLimiterModule::stereoLinkControl] = 1.0f;
    controls[FetLimiterModule::mixControl] = 1.0f;
    controls[FetLimiterModule::outputTrimControl] =
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
    FetLimiterModule& module, const ControlValues& controls,
    juce::AudioBuffer<float>& buffer, int blocks)
{
    const megadsp::ProcessEnvironment environment;
    for (int block = 0; block < blocks; ++block)
        module.process(buffer, controls, environment);
}

// Feeds a continuously regenerated (not self-cascading) steady tone through
// the module for several blocks so its gain-reduction envelope settles
// toward the true steady state for a sustained signal, leaving `buffer`
// holding the final processed block for measurement.
void settleSteady(
    FetLimiterModule& module, const ControlValues& controls,
    juce::AudioBuffer<float>& buffer, int blocks, int channels,
    float frequency, float amplitude)
{
    const megadsp::ProcessEnvironment environment;
    for (int block = 0; block < blocks; ++block)
    {
        buffer = makeSine(channels, buffer.getNumSamples(), frequency, amplitude);
        module.process(buffer, controls, environment);
    }
}

class FetLimiterTests final : public juce::UnitTest
{
public:
    FetLimiterTests() : UnitTest("FET Limiter") {}

    void runTest() override
    {
        using namespace megadsp;
        const juce::dsp::ProcessSpec stereoSpec { sampleRate, 512, 2 };
        const juce::dsp::ProcessSpec monoSpec { sampleRate, 257, 1 };
        const auto defaults = fetLimiterDefaults();

        beginTest("Reports one fixed oversampled latency");
        {
            FetLimiterModule module;
            module.prepare(stereoSpec);
            const auto fixedLatency = module.latencySamples();
            expectGreaterThan(fixedLatency, 0);
            auto buffer = makeSine(2, 512, 220.0f, 0.3f);
            for (int pass = 0; pass < 8; ++pass)
            {
                module.process(buffer, defaults, {});
                expectEquals(module.latencySamples(), fixedLatency);
            }
        }

        beginTest("Mix zero is exact latency-aligned dry audio");
        {
            FetLimiterModule module;
            module.prepare(stereoSpec);
            auto controls = defaults;
            controls[FetLimiterModule::mixControl] = 0.0f;
            const auto latency = module.latencySamples();
            juce::AudioBuffer<float> buffer(2, 512);
            buffer.clear();
            const auto impulseIndex = 200;
            const auto impulseValue = 0.83f;
            buffer.setSample(0, impulseIndex, impulseValue);
            buffer.setSample(1, impulseIndex, impulseValue);
            module.process(buffer, controls, {});
            for (int channel = 0; channel < 2; ++channel)
                expectWithinAbsoluteError(
                    buffer.getSample(channel, impulseIndex + latency),
                    impulseValue, 0.0005f);
            // Every other sample in the aligned window stays at silence.
            expectWithinAbsoluteError(buffer.getSample(0, 10), 0.0f, 0.0005f);
        }

        beginTest("Gain reduction increases with the stepped ratio");
        {
            std::array<float, FetLimiterModule::ratioCount - 1> reductions {};
            for (int ratioIndex = 0; ratioIndex < FetLimiterModule::ratioCount - 1;
                 ++ratioIndex)
            {
                FetLimiterModule module;
                module.prepare(stereoSpec);
                auto controls = defaults;
                controls[FetLimiterModule::inputControl] =
                    linearNormalized(-24.0f, 24.0f, 18.0f);
                controls[FetLimiterModule::ratioControl] =
                    megadsp::discreteValue(ratioIndex, FetLimiterModule::ratioCount);
                auto buffer = makeSine(2, 512, 200.0f, 0.9f);
                settleSteady(module, controls, buffer, 40, 2, 200.0f, 0.9f);
                reductions[static_cast<size_t>(ratioIndex)] = module.meterValue();
            }
            for (size_t index = 1; index < reductions.size(); ++index)
                expectGreaterThan(reductions[index], reductions[index - 1]);
        }

        beginTest("ALL mode forces a near-instant attack independent of Attack");
        {
            FetLimiterModule slowAttackFour;
            slowAttackFour.prepare(stereoSpec);
            auto fourControls = defaults;
            fourControls[FetLimiterModule::attackControl] = 1.0f;
            fourControls[FetLimiterModule::inputControl] =
                linearNormalized(-24.0f, 24.0f, 18.0f);
            fourControls[FetLimiterModule::ratioControl] =
                megadsp::discreteValue(0, FetLimiterModule::ratioCount);

            FetLimiterModule slowAttackAll;
            slowAttackAll.prepare(stereoSpec);
            auto allControls = fourControls;
            allControls[FetLimiterModule::ratioControl] = megadsp::discreteValue(
                FetLimiterModule::allButtonsInIndex, FetLimiterModule::ratioCount);

            // Warm up with silence so the transient below is a fresh onset.
            juce::AudioBuffer<float> silence(2, 64);
            silence.clear();
            settle(slowAttackFour, fourControls, silence, 4);
            settle(slowAttackAll, allControls, silence, 4);

            auto transientFour = makeSine(2, 32, 200.0f, 0.9f);
            auto transientAll = transientFour;
            slowAttackFour.process(transientFour, fourControls, {});
            slowAttackAll.process(transientAll, allControls, {});
            expectGreaterThan(
                slowAttackAll.meterValue(), slowAttackFour.meterValue());
        }

        beginTest("ALL mode reduces more than the top 20:1 ratio at steady state");
        {
            FetLimiterModule ratioTwenty;
            ratioTwenty.prepare(stereoSpec);
            auto twentyControls = defaults;
            twentyControls[FetLimiterModule::inputControl] =
                linearNormalized(-24.0f, 24.0f, 18.0f);
            twentyControls[FetLimiterModule::ratioControl] = megadsp::discreteValue(
                FetLimiterModule::allButtonsInIndex - 1, FetLimiterModule::ratioCount);
            auto twentyBuffer = makeSine(2, 512, 200.0f, 0.9f);
            settleSteady(ratioTwenty, twentyControls, twentyBuffer, 40, 2, 200.0f, 0.9f);

            FetLimiterModule ratioAll;
            ratioAll.prepare(stereoSpec);
            auto allControls = twentyControls;
            allControls[FetLimiterModule::ratioControl] = megadsp::discreteValue(
                FetLimiterModule::allButtonsInIndex, FetLimiterModule::ratioCount);
            auto allBuffer = makeSine(2, 512, 200.0f, 0.9f);
            settleSteady(ratioAll, allControls, allBuffer, 40, 2, 200.0f, 0.9f);

            expectGreaterThan(ratioAll.meterValue(), ratioTwenty.meterValue());
        }

        beginTest("Attack and release spans the documented timing ranges");
        {
            FetLimiterModule fastAttack;
            fastAttack.prepare(stereoSpec);
            auto fastControls = defaults;
            fastControls[FetLimiterModule::attackControl] = 0.0f;
            fastControls[FetLimiterModule::inputControl] =
                linearNormalized(-24.0f, 24.0f, 18.0f);

            FetLimiterModule slowAttack;
            slowAttack.prepare(stereoSpec);
            auto slowControls = fastControls;
            slowControls[FetLimiterModule::attackControl] = 1.0f;

            juce::AudioBuffer<float> silenceShort(2, 16);
            silenceShort.clear();
            settle(fastAttack, fastControls, silenceShort, 4);
            settle(slowAttack, slowControls, silenceShort, 4);

            auto fastTransient = makeSine(2, 8, 200.0f, 0.9f);
            auto slowTransient = fastTransient;
            fastAttack.process(fastTransient, fastControls, {});
            slowAttack.process(slowTransient, slowControls, {});
            expectGreaterThan(fastAttack.meterValue(), slowAttack.meterValue());

            FetLimiterModule fastRelease;
            fastRelease.prepare(stereoSpec);
            auto fastReleaseControls = defaults;
            fastReleaseControls[FetLimiterModule::releaseControl] = 0.0f;
            fastReleaseControls[FetLimiterModule::inputControl] =
                linearNormalized(-24.0f, 24.0f, 18.0f);

            FetLimiterModule slowRelease;
            slowRelease.prepare(stereoSpec);
            auto slowReleaseControls = fastReleaseControls;
            slowReleaseControls[FetLimiterModule::releaseControl] = 1.0f;

            auto loudFast = makeSine(2, 512, 200.0f, 0.9f);
            auto loudSlow = loudFast;
            settleSteady(fastRelease, fastReleaseControls, loudFast, 40, 2, 200.0f, 0.9f);
            settleSteady(slowRelease, slowReleaseControls, loudSlow, 40, 2, 200.0f, 0.9f);

            juce::AudioBuffer<float> quiet(2, 128);
            quiet.clear();
            fastRelease.process(quiet, fastReleaseControls, {});
            auto quietSlow = quiet;
            slowRelease.process(quietSlow, slowReleaseControls, {});
            expectGreaterThan(
                slowRelease.meterValue(), fastRelease.meterValue());
        }

        beginTest("Detector low cut reduces sensitivity to sub-cutoff energy");
        {
            FetLimiterModule lowCutHigh;
            lowCutHigh.prepare(stereoSpec);
            auto highCutControls = defaults;
            highCutControls[FetLimiterModule::detectorLowCutControl] = 1.0f;
            juce::AudioBuffer<float> lowToneHigh(2, 512);
            settleSteady(lowCutHigh, highCutControls, lowToneHigh, 20, 2, 40.0f, 0.9f);

            FetLimiterModule lowCutLow;
            lowCutLow.prepare(stereoSpec);
            auto lowCutControls = highCutControls;
            lowCutControls[FetLimiterModule::detectorLowCutControl] = 0.0f;
            juce::AudioBuffer<float> lowToneLow(2, 512);
            settleSteady(lowCutLow, lowCutControls, lowToneLow, 20, 2, 40.0f, 0.9f);

            expectGreaterThan(
                lowCutLow.detectorValue(), lowCutHigh.detectorValue());
        }

        beginTest("Stereo Link pulls a quiet channel toward the loud channel's reduction");
        {
            auto measureQuietChannelGain =
                [&](float stereoLink) -> float
            {
                FetLimiterModule module;
                module.prepare(stereoSpec);
                auto controls = defaults;
                controls[FetLimiterModule::inputControl] =
                    linearNormalized(-24.0f, 24.0f, 18.0f);
                controls[FetLimiterModule::stereoLinkControl] = stereoLink;
                const megadsp::ProcessEnvironment environment;
                juce::AudioBuffer<float> buffer(2, 512);
                auto fillAsymmetric = [&]()
                {
                    for (int sample = 0; sample < 512; ++sample)
                    {
                        const auto phase = juce::MathConstants<float>::twoPi
                            * 200.0f * static_cast<float>(sample)
                            / static_cast<float>(sampleRate);
                        buffer.setSample(0, sample, 0.9f * std::sin(phase));
                        buffer.setSample(1, sample, 0.05f * std::sin(phase));
                    }
                };
                fillAsymmetric();
                const auto inputRight = buffer.getRMSLevel(1, 0, 512);
                for (int block = 0; block < 30; ++block)
                {
                    fillAsymmetric();
                    module.process(buffer, controls, environment);
                }
                const auto outputRight = buffer.getRMSLevel(1, 384, 128);
                return outputRight / juce::jmax(1.0e-6f, inputRight);
            };

            const auto linkedGain = measureQuietChannelGain(1.0f);
            const auto independentGain = measureQuietChannelGain(0.0f);
            expectGreaterThan(independentGain, linkedGain);
        }

        beginTest("Duplicated-mono input keeps channels matched (mono fold-down safe)");
        {
            FetLimiterModule module;
            module.prepare(stereoSpec);
            auto controls = defaults;
            controls[FetLimiterModule::inputControl] =
                linearNormalized(-24.0f, 24.0f, 12.0f);
            auto buffer = makeSine(2, 512, 330.0f, 0.7f);
            settleSteady(module, controls, buffer, 10, 2, 330.0f, 0.7f);
            auto maximumDifference = 0.0f;
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                maximumDifference = juce::jmax(
                    maximumDifference,
                    std::abs(
                        buffer.getSample(0, sample)
                        - buffer.getSample(1, sample)));
            expectLessThan(maximumDifference, 0.001f);
        }

        beginTest("Hard-panned silent channel stays exactly silent");
        {
            FetLimiterModule module;
            module.prepare(stereoSpec);
            auto controls = defaults;
            juce::AudioBuffer<float> buffer(2, 512);
            const megadsp::ProcessEnvironment environment;
            for (int block = 0; block < 10; ++block)
            {
                buffer.clear();
                for (int sample = 0; sample < 512; ++sample)
                    buffer.setSample(0, sample, 0.8f * std::sin(
                        juce::MathConstants<float>::twoPi * 200.0f
                        * static_cast<float>(sample) / static_cast<float>(sampleRate)));
                module.process(buffer, controls, environment);
            }
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                expectEquals(buffer.getSample(1, sample), 0.0f);
        }

        beginTest("One-channel fallback processes finite mono audio");
        {
            FetLimiterModule module;
            module.prepare(monoSpec);
            auto controls = defaults;
            auto buffer = makeSine(1, 257, 311.0f, 0.8f);
            module.process(buffer, controls, {});
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                expect(std::isfinite(buffer.getSample(0, sample)));
            expectGreaterThan(module.latencySamples(), 0);
        }

        beginTest("Deterministic and finite for injected NaN, mono and stereo");
        {
            FetLimiterModule mono;
            mono.prepare(monoSpec);
            auto controls = defaults;
            auto monoSignal = makeSine(1, 257, 311.0f, 0.6f);
            monoSignal.setSample(0, 12, std::numeric_limits<float>::quiet_NaN());
            mono.process(monoSignal, controls, { nullptr, 120.0, true });
            for (int sample = 0; sample < monoSignal.getNumSamples(); ++sample)
                expect(std::isfinite(monoSignal.getSample(0, sample)));

            FetLimiterModule stereo;
            stereo.prepare(stereoSpec);
            auto stereoSignal = makeSine(2, 512, 220.0f, 0.6f);
            stereoSignal.setSample(0, 40, std::numeric_limits<float>::infinity());
            stereoSignal.setSample(1, 41, -std::numeric_limits<float>::infinity());
            stereo.process(stereoSignal, controls, { nullptr, 120.0, true });
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < stereoSignal.getNumSamples(); ++sample)
                    expect(std::isfinite(stereoSignal.getSample(channel, sample)));

            // Same input, same controls: fully deterministic output.
            FetLimiterModule repeat;
            repeat.prepare(stereoSpec);
            auto first = makeSine(2, 512, 220.0f, 0.6f);
            auto second = first;
            stereo.reset();
            stereo.process(first, controls, {});
            repeat.process(second, controls, {});
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < first.getNumSamples(); ++sample)
                    expectWithinAbsoluteError(
                        first.getSample(channel, sample),
                        second.getSample(channel, sample), 1.0e-6f);
        }

        beginTest("Driven signal remains bounded and audibly asymmetric");
        {
            FetLimiterModule module;
            module.prepare(stereoSpec);
            auto controls = defaults;
            controls[FetLimiterModule::inputControl] =
                linearNormalized(-24.0f, 24.0f, 24.0f);
            controls[FetLimiterModule::outputControl] =
                linearNormalized(-24.0f, 24.0f, 24.0f);
            auto buffer = makeSine(2, 512, 150.0f, 0.95f);
            settleSteady(module, controls, buffer, 16, 2, 150.0f, 0.95f);
            auto maximumSample = -std::numeric_limits<float>::infinity();
            auto minimumSample = std::numeric_limits<float>::infinity();
            for (int sample = buffer.getNumSamples() / 2;
                 sample < buffer.getNumSamples(); ++sample)
            {
                const auto value = buffer.getSample(0, sample);
                expect(std::isfinite(value));
                expectLessThan(std::abs(value), 6.0f);
                maximumSample = juce::jmax(maximumSample, value);
                minimumSample = juce::jmin(minimumSample, value);
            }
            expectGreaterThan(
                std::abs(maximumSample + minimumSample), 0.0005f);
        }

        beginTest("Automation steps stay finite across ratio and mix jumps");
        {
            FetLimiterModule module;
            module.prepare(stereoSpec);
            auto controls = defaults;
            for (int step = 0; step < 20; ++step)
            {
                controls[FetLimiterModule::ratioControl] = megadsp::discreteValue(
                    step % FetLimiterModule::ratioCount, FetLimiterModule::ratioCount);
                controls[FetLimiterModule::mixControl] =
                    (step % 2 == 0) ? 0.0f : 1.0f;
                controls[FetLimiterModule::inputControl] =
                    linearNormalized(-24.0f, 24.0f, (step % 5) * 6.0f - 6.0f);
                auto buffer = makeSine(2, 128, 250.0f, 0.85f);
                module.process(buffer, controls, {});
                for (int channel = 0; channel < 2; ++channel)
                    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                        expect(std::isfinite(buffer.getSample(channel, sample)));
            }
        }

        beginTest("Supported sample rates report positive finite-safe latency");
        {
            for (const auto rate : { 44100.0, 48000.0, 96000.0 })
            {
                FetLimiterModule module;
                module.prepare({ rate, 256, 2 });
                expectGreaterThan(module.latencySamples(), 0);
                auto buffer = makeSine(2, 256, 220.0f, 0.7f);
                module.process(buffer, defaults, {});
                for (int channel = 0; channel < 2; ++channel)
                    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                        expect(std::isfinite(buffer.getSample(channel, sample)));
            }
        }

        beginTest("Continuous telemetry is capture-gated with matching lane counts");
        {
            FetLimiterModule module;
            module.prepare(stereoSpec);
            auto controls = defaults;
            auto buffer = makeSine(2, 512, 200.0f, 0.6f);
            module.process(buffer, controls, { nullptr, 120.0, false });
            megadsp::ContinuousTelemetrySnapshot untracked;
            const auto hasUntracked = module.readContinuousTelemetry(untracked);
            expect(!hasUntracked || untracked.sequence == 0);

            auto trackedBuffer = makeSine(2, 512, 200.0f, 0.6f);
            module.process(trackedBuffer, controls, { nullptr, 120.0, true });
            megadsp::ContinuousTelemetrySnapshot tracked;
            expect(module.readContinuousTelemetry(tracked));
            expectEquals(
                static_cast<int>(tracked.valueCount),
                static_cast<int>(FetLimiterModule::telemetryValueCount));
            expectEquals(
                static_cast<int>(tracked.historyValueCount),
                static_cast<int>(FetLimiterModule::telemetryHistoryCount));
            expectGreaterThan(static_cast<int>(tracked.sequence), 0);

            auto secondBuffer = makeSine(2, 512, 200.0f, 0.6f);
            module.process(secondBuffer, controls, { nullptr, 120.0, true });
            megadsp::ContinuousTelemetrySnapshot secondTracked;
            expect(module.readContinuousTelemetry(secondTracked));
            expectGreaterThan(
                static_cast<int>(secondTracked.sequence),
                static_cast<int>(tracked.sequence));
        }
    }
};

FetLimiterTests fetLimiterTests;
} // namespace
