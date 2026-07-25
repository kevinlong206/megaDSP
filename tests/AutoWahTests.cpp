#include "DspModules.h"
#include "modules/ModuleRegistry.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace
{
class AutoWahTests final : public juce::UnitTest
{
public:
    AutoWahTests() : UnitTest("Auto Wah") {}

    void runTest() override
    {
        using namespace megadsp;
        constexpr double rate = 48000.0;
        const juce::dsp::ProcessSpec stereoSpec { rate, 256, 2 };
        auto controls = moduleDefaults(ModuleType::autoWah);

        beginTest("Mix zero is exact immediate dry");
        AutoWahModule dryModule;
        dryModule.prepare(stereoSpec);
        auto dryControls = controls;
        dryControls[AutoWahModule::mixControl] = 0.0f;
        auto dry = makePickSignal(2, 24000, rate);
        const auto reference = dry;
        render(dryModule, dry, dryControls);
        expect(maxDifference(dry, reference) < 0.000001f);
        expectEquals(dryModule.latencySamples(), 0);

        beginTest("Pick attacks drive a visible resonant sweep");
        AutoWahModule responsive;
        responsive.prepare(stereoSpec);
        auto signal = makePickSignal(2, 48000, rate);
        const auto unprocessed = signal;
        render(responsive, signal, controls, true);
        expectFinite(signal);
        expect(maxDifference(signal, unprocessed) > 0.02f);
        expect(signal.getMagnitude(0, 0, signal.getNumSamples()) < 2.0f);
        ContinuousTelemetrySnapshot telemetry;
        expect(responsive.readContinuousTelemetry(telemetry));
        expect(telemetry.sequence > 0);
        expect(telemetry.values[AutoWahModule::inputLevelDb] > -60.0f);
        expect(telemetry.values[AutoWahModule::currentFrequencyHz] > 100.0f);
        expect(telemetry.values[AutoWahModule::wetEnergy] > 0.001f);

        beginTest("Sensitivity controls quiet-pick response");
        auto sensitiveControls = controls;
        sensitiveControls[AutoWahModule::sensitivityControl] = 1.0f;
        auto insensitiveControls = controls;
        insensitiveControls[AutoWahModule::sensitivityControl] = 0.0f;
        AutoWahModule sensitive;
        AutoWahModule insensitive;
        sensitive.prepare(stereoSpec);
        insensitive.prepare(stereoSpec);
        auto quietSensitive = makePickSignal(2, 24000, rate, 0.035f);
        auto quietInsensitive = quietSensitive;
        render(sensitive, quietSensitive, sensitiveControls, true);
        render(insensitive, quietInsensitive, insensitiveControls, true);
        ContinuousTelemetrySnapshot sensitiveTelemetry;
        ContinuousTelemetrySnapshot insensitiveTelemetry;
        expect(sensitive.readContinuousTelemetry(sensitiveTelemetry));
        expect(insensitive.readContinuousTelemetry(insensitiveTelemetry));
        expect(
            sensitiveTelemetry.values[AutoWahModule::envelopeAmount]
            > insensitiveTelemetry.values[AutoWahModule::envelopeAmount]
                + 0.05f);

        beginTest("Direction reverses the attack sweep");
        auto upControls = controls;
        upControls[AutoWahModule::directionControl] = 0.25f;
        auto downControls = controls;
        downControls[AutoWahModule::directionControl] = 0.75f;
        AutoWahModule up;
        AutoWahModule down;
        up.prepare(stereoSpec);
        down.prepare(stereoSpec);
        auto upSignal = makePickSignal(2, 4096, rate);
        auto downSignal = upSignal;
        render(up, upSignal, upControls, true);
        render(down, downSignal, downControls, true);
        ContinuousTelemetrySnapshot upTelemetry;
        ContinuousTelemetrySnapshot downTelemetry;
        expect(up.readContinuousTelemetry(upTelemetry));
        expect(down.readContinuousTelemetry(downTelemetry));
        expect(
            upTelemetry.values[AutoWahModule::currentFrequencyHz]
            > downTelemetry.values[AutoWahModule::currentFrequencyHz]);

        beginTest("Filter types provide distinct bounded voices");
        std::array<juce::AudioBuffer<float>, 4> filterOutputs {
            makePickSignal(2, 24000, rate),
            makePickSignal(2, 24000, rate),
            makePickSignal(2, 24000, rate),
            makePickSignal(2, 24000, rate)
        };
        for (int type = 0; type < 4; ++type)
        {
            AutoWahModule filter;
            filter.prepare(stereoSpec);
            auto filterControls = controls;
            filterControls[AutoWahModule::filterTypeControl] =
                megadsp::discreteValue(type, 4);
            render(filter, filterOutputs[static_cast<size_t>(type)],
                   filterControls);
            expectFinite(filterOutputs[static_cast<size_t>(type)]);
            expect(
                filterOutputs[static_cast<size_t>(type)].getMagnitude(
                    0, 0, 24000) < 2.0f);
        }
        expect(maxDifference(filterOutputs[0], filterOutputs[1]) > 0.01f);
        expect(maxDifference(filterOutputs[1], filterOutputs[2]) > 0.01f);
        expect(maxDifference(filterOutputs[2], filterOutputs[3]) > 0.01f);

        beginTest("Maximum resonance and drive are assertive but bounded");
        AutoWahModule mildCharacter;
        AutoWahModule strongCharacter;
        mildCharacter.prepare(stereoSpec);
        strongCharacter.prepare(stereoSpec);
        auto mildControls = controls;
        mildControls[AutoWahModule::resonanceControl] = 0.0f;
        mildControls[AutoWahModule::driveControl] = 0.0f;
        auto strongControls = controls;
        strongControls[AutoWahModule::resonanceControl] = 1.0f;
        strongControls[AutoWahModule::driveControl] = 1.0f;
        auto mildSignal = makePickSignal(2, 24000, rate);
        auto strongSignal = mildSignal;
        render(mildCharacter, mildSignal, mildControls);
        render(strongCharacter, strongSignal, strongControls);
        expect(maxDifference(mildSignal, strongSignal) > 0.10f);
        expect(strongSignal.getMagnitude(0, 0, 24000) < 2.0f);

        beginTest("Stereo and mono fallback remain finite");
        AutoWahModule stereo;
        stereo.prepare(stereoSpec);
        auto asymmetric = makePickSignal(2, 48000, rate);
        asymmetric.applyGain(1, 0, asymmetric.getNumSamples(), 0.08f);
        render(stereo, asymmetric, controls);
        expectFinite(asymmetric);
        AutoWahModule leftOnly;
        leftOnly.prepare(stereoSpec);
        auto unlinkedControls = controls;
        unlinkedControls[AutoWahModule::filterTypeControl] =
            megadsp::discreteValue(3, 4);
        auto leftOnlySignal = makePickSignal(2, 12000, rate);
        leftOnlySignal.clear(1, 0, leftOnlySignal.getNumSamples());
        render(leftOnly, leftOnlySignal, unlinkedControls, true);
        ContinuousTelemetrySnapshot leftTelemetry;
        expect(leftOnly.readContinuousTelemetry(leftTelemetry));
        expect(
            leftTelemetry.values[AutoWahModule::envelopeAmount] > 0.05f);
        AutoWahModule mono;
        mono.prepare({ rate, 256, 1 });
        auto monoSignal = makePickSignal(1, 48000, rate);
        render(mono, monoSignal, controls);
        expectFinite(monoSignal);

        beginTest("Extreme automation stays finite and bounded");
        AutoWahModule automated;
        automated.prepare(stereoSpec);
        auto automatedSignal = makePickSignal(2, 96000, rate);
        for (int offset = 0; offset < automatedSignal.getNumSamples();
             offset += 256)
        {
            const auto count =
                juce::jmin(256, automatedSignal.getNumSamples() - offset);
            auto changing = controls;
            const auto phase =
                static_cast<float>((offset / 256) % 9) / 8.0f;
            changing[AutoWahModule::sensitivityControl] = phase;
            changing[AutoWahModule::centerFrequencyControl] = 1.0f - phase;
            changing[AutoWahModule::sweepRangeControl] = phase;
            changing[AutoWahModule::attackControl] = 1.0f - phase;
            changing[AutoWahModule::releaseControl] = phase;
            changing[AutoWahModule::resonanceControl] = 1.0f;
            changing[AutoWahModule::directionControl] =
                (offset / 256) % 2 == 0 ? 0.0f : 1.0f;
            changing[AutoWahModule::driveControl] = 1.0f;
            changing[AutoWahModule::filterTypeControl] =
                megadsp::discreteValue((offset / 256) % 4, 4);
            changing[AutoWahModule::mixControl] = 1.0f;
            juce::AudioBuffer<float> block(
                automatedSignal.getArrayOfWritePointers(), 2, offset, count);
            automated.process(block, changing, {});
        }
        expectFinite(automatedSignal);
        expect(
            automatedSignal.getMagnitude(
                0, 0, automatedSignal.getNumSamples()) < 3.0f);
        expect(
            automatedSignal.getMagnitude(
                1, 0, automatedSignal.getNumSamples()) < 3.0f);
    }

private:
    static juce::AudioBuffer<float> makePickSignal(
        int channels, int samples, double rate, float gain = 0.25f)
    {
        juce::AudioBuffer<float> result(channels, samples);
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto time = static_cast<float>(sample / rate);
            const auto withinPick = std::fmod(time, 0.22f);
            const auto envelope = std::exp(-withinPick / 0.055f);
            const auto value = gain * envelope * (
                std::sin(juce::MathConstants<float>::twoPi * 164.8f * time)
                + 0.45f * std::sin(
                    juce::MathConstants<float>::twoPi * 494.4f * time));
            result.setSample(0, sample, value);
            if (channels > 1)
                result.setSample(1, sample, value * 0.92f);
        }
        return result;
    }

    static void render(
        megadsp::AutoWahModule& module, juce::AudioBuffer<float>& buffer,
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

AutoWahTests autoWahTests;
} // namespace
