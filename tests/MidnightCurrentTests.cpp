#include "DspModules.h"
#include "modules/ModuleRegistry.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <limits>

namespace
{
class MidnightCurrentTests final : public juce::UnitTest
{
public:
    MidnightCurrentTests() : UnitTest("Midnight Current") {}

    void runTest() override
    {
        using namespace megadsp;
        constexpr double rate = 48000.0;
        const juce::dsp::ProcessSpec spec { rate, 512, 2 };
        auto controls = moduleDefaults(ModuleType::midnightCurrent);

        beginTest("Mix zero is exact immediate dry");
        MidnightCurrentModule dry;
        dry.prepare(spec);
        auto dryControls = controls;
        dryControls[MidnightCurrentModule::mixControl] = 0.0f;
        auto drySignal = makeSignal(2, 8192, rate);
        const auto reference = drySignal;
        render(dry, drySignal, dryControls);
        expect(maxDifference(drySignal, reference) < 0.000001f);
        expectEquals(dry.latencySamples(), 0);

        beginTest("Default treatment creates a dark moving phase field");
        MidnightCurrentModule treated;
        treated.prepare(spec);
        auto source = makeSignal(2, 96000, rate);
        const auto untreated = source;
        render(treated, source, controls, { nullptr, 120.0, true });
        expectFinite(source);
        expect(maxDifference(source, untreated) > 0.02f);
        expect(highBandDifference(source, untreated) > 0.0001f);
        expect(source.getRMSLevel(0, 24000, 48000) > 0.005f);
        expect(source.getMagnitude(0, 0, source.getNumSamples()) < 2.5f);
        ContinuousTelemetrySnapshot telemetry;
        expect(treated.readContinuousTelemetry(telemetry));
        expect(telemetry.sequence > 0);
        expect(telemetry.values[MidnightCurrentModule::actualCutoffHz] > 40.0f);
        expect(telemetry.values[MidnightCurrentModule::wetEnergy] > 0.001f);

        beginTest("Stereo spread preserves mono fold-down");
        auto narrowControls = controls;
        narrowControls[MidnightCurrentModule::stereoSpreadControl] = 0.0f;
        auto wideControls = controls;
        wideControls[MidnightCurrentModule::stereoSpreadControl] = 1.0f;
        MidnightCurrentModule narrow;
        MidnightCurrentModule wide;
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
        expect(wideSideEnergy > 0.01);

        beginTest("Mono fallback is independent of stereo spread");
        MidnightCurrentModule monoNarrow;
        MidnightCurrentModule monoWide;
        monoNarrow.prepare({ rate, 512, 1 });
        monoWide.prepare({ rate, 512, 1 });
        auto firstMono = makeSignal(1, 24000, rate);
        auto secondMono = firstMono;
        render(monoNarrow, firstMono, narrowControls);
        render(monoWide, secondMono, wideControls);
        expect(maxDifference(firstMono, secondMono) < 0.000002f);

        beginTest("Feedback extremes and automation stay finite and bounded");
        MidnightCurrentModule stressed;
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
                    0.25f * std::sin(
                        juce::MathConstants<float>::twoPi * 211.0f * time));
                automated.setSample(
                    1, absolute,
                    0.17f * std::sin(
                        juce::MathConstants<float>::twoPi * 337.0f * time));
            }
            auto changing = controls;
            const auto phase = static_cast<float>(offset / 512 % 7) / 6.0f;
            changing[MidnightCurrentModule::filterCenterControl] = phase;
            changing[MidnightCurrentModule::filterSweepControl] = 1.0f - phase;
            changing[MidnightCurrentModule::regenerationControl] =
                offset / 512 % 2 == 0 ? 0.0f : 1.0f;
            changing[MidnightCurrentModule::resonanceControl] = 1.0f;
            changing[MidnightCurrentModule::warmthControl] = 1.0f;
            changing[MidnightCurrentModule::mixControl] = 1.0f;
            juce::AudioBuffer<float> block(
                automated.getArrayOfWritePointers(), 2, offset, count);
            stressed.process(block, changing, {});
        }
        expectFinite(automated);
        expect(automated.getMagnitude(0, 0, automated.getNumSamples()) < 4.0f);
        expect(automated.getMagnitude(1, 0, automated.getNumSamples()) < 4.0f);
        expect(stressed.tailSeconds(controls) >= 0.05);
        expect(stressed.tailSeconds(controls) <= 2.0);

        beginTest("Reset restores deterministic motion");
        MidnightCurrentModule deterministic;
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

    static void render(
        megadsp::MidnightCurrentModule& module,
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

    static float highBandDifference(
        const juce::AudioBuffer<float>& processed,
        const juce::AudioBuffer<float>& dry)
    {
        double energy = 0.0;
        for (int sample = 1; sample < processed.getNumSamples(); ++sample)
        {
            const auto processedHigh =
                processed.getSample(0, sample)
                - processed.getSample(0, sample - 1);
            const auto dryHigh =
                dry.getSample(0, sample) - dry.getSample(0, sample - 1);
            const auto difference = processedHigh - dryHigh;
            energy += static_cast<double>(difference) * difference;
        }
        return static_cast<float>(std::sqrt(
            energy / static_cast<double>(processed.getNumSamples() - 1)));
    }
};

MidnightCurrentTests midnightCurrentTests;
} // namespace
