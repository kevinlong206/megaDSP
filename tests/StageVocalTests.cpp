#include "modules/StageVocal.h"
#include "modules/ModuleRegistry.h"
#include "Parameters.h"

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

juce::AudioBuffer<float> makeSine(
    int channels, int samples, float frequency, float amplitude)
{
    juce::AudioBuffer<float> buffer(channels, samples);
    for (int sample = 0; sample < samples; ++sample)
    {
        const auto value = amplitude * std::sin(
            juce::MathConstants<float>::twoPi * frequency
            * static_cast<float>(sample) / static_cast<float>(sampleRate));
        for (int channel = 0; channel < channels; ++channel)
            buffer.setSample(channel, sample, value);
    }
    return buffer;
}

float lateRms(const juce::AudioBuffer<float>& buffer)
{
    const auto start = buffer.getNumSamples() * 3 / 4;
    return buffer.getRMSLevel(0, start, buffer.getNumSamples() - start);
}

megadsp::ControlValues neutralControls()
{
    auto controls =
        megadsp::moduleDefaults(megadsp::ModuleType::stageVocal);
    controls[megadsp::StageVocalModule::lowCutControl] = 0.0f;
    controls[megadsp::StageVocalModule::fastAmountControl] = 0.0f;
    controls[megadsp::StageVocalModule::slowAmountControl] = 0.0f;
    controls[megadsp::StageVocalModule::deEssAmountControl] = 0.0f;
    controls[megadsp::StageVocalModule::presenceControl] = 0.5f;
    controls[megadsp::StageVocalModule::airControl] = 0.5f;
    controls[megadsp::StageVocalModule::outputControl] =
        linearNormalized(-18.0f, 12.0f, 0.0f);
    return controls;
}

class StageVocalTests final : public juce::UnitTest
{
public:
    StageVocalTests() : UnitTest("Stage Vocal") {}

    void runTest() override
    {
        using namespace megadsp;
        const juce::dsp::ProcessSpec spec { sampleRate, 512, 2 };

        beginTest("Registry exposes serial fast and slow compressors");
        const auto& definition = moduleDefinition(ModuleType::stageVocal);
        expectEquals(static_cast<int>(ModuleType::stageVocal), 48);
        expect(definition.category == ModuleCategory::dynamics);
        expect(definition.capabilities
               == ModuleCapability::continuousTelemetry);
        for (int control = 0; control < controlsPerSlot; ++control)
            expect(definition.controls[static_cast<size_t>(control)].isActive());
        const auto defaults = moduleDefaults(ModuleType::stageVocal);
        expectEquals(
            formatControlValue(
                ModuleType::stageVocal,
                StageVocalModule::lowCutControl,
                defaults[StageVocalModule::lowCutControl]),
            juce::String("80 Hz"));
        expectEquals(
            formatControlValue(
                ModuleType::stageVocal,
                StageVocalModule::fastAttackControl,
                defaults[StageVocalModule::fastAttackControl]),
            juce::String("0.15 ms"));
        const auto parsed = parseControlValue(
            ModuleType::stageVocal,
            StageVocalModule::deEssFrequencyControl,
            "7.2 kHz");
        expect(parsed.has_value());
        if (parsed.has_value())
            expectWithinAbsoluteError(
                *parsed,
                exponentialNormalized(3000.0f, 10000.0f, 7200.0f),
                0.0001f);

        beginTest("Schema-12 controls preserve compatible vocal settings");
        std::array<float, controlsPerSlot> legacy {};
        for (int control = 0; control < controlsPerSlot; ++control)
            legacy[static_cast<size_t>(control)] =
                0.03f + static_cast<float>(control) * 0.07f;
        const auto migrated =
            migrateStageVocalControlsFromSchema12(legacy);
        expectEquals(migrated[0], legacy[0]);
        expectEquals(migrated[7], legacy[3]);
        expectEquals(migrated[8], legacy[4]);
        expectEquals(migrated[9], legacy[9]);
        expectEquals(migrated[10], legacy[10]);
        expectEquals(migrated[11], legacy[11]);
        expectEquals(
            migrated[1],
            defaults[StageVocalModule::fastAmountControl]);

        beginTest("Low cut rejects rumble while retaining vocal fundamentals");
        auto cleanupControls = neutralControls();
        cleanupControls[StageVocalModule::lowCutControl] =
            exponentialNormalized(40.0f, 220.0f, 180.0f);
        StageVocalModule lowCutLow;
        StageVocalModule lowCutHigh;
        lowCutLow.prepare(spec);
        lowCutHigh.prepare(spec);
        auto rumble = makeSine(2, 48000, 50.0f, 0.2f);
        auto vocal = makeSine(2, 48000, 500.0f, 0.2f);
        lowCutLow.process(rumble, cleanupControls, {});
        lowCutHigh.process(vocal, cleanupControls, {});
        expect(lateRms(rumble) < lateRms(vocal) * 0.25f);

        beginTest("Fast FET stage catches peaks with sub-millisecond attack");
        auto fastControls = neutralControls();
        fastControls[StageVocalModule::fastAmountControl] = 1.0f;
        fastControls[StageVocalModule::fastAttackControl] = 0.0f;
        fastControls[StageVocalModule::fastReleaseControl] = 0.0f;
        StageVocalModule fast;
        fast.prepare(spec);
        auto fastSignal = makeSine(2, 48000, 700.0f, 0.8f);
        const auto fastInput = lateRms(fastSignal);
        fast.process(fastSignal, fastControls, {});
        expect(lateRms(fastSignal) < fastInput * 0.35f);

        beginTest("Fast feedback curve approaches an 8:1 steady ratio");
        StageVocalModule lowerFast;
        StageVocalModule upperFast;
        lowerFast.prepare(spec);
        upperFast.prepare(spec);
        auto lowerLevel = makeSine(2, 96000, 700.0f, 0.2f);
        auto upperLevel = makeSine(2, 96000, 700.0f, 0.4f);
        lowerFast.process(lowerLevel, fastControls, {});
        upperFast.process(upperLevel, fastControls, {});
        const auto outputDeltaDb = juce::Decibels::gainToDecibels(
            lateRms(upperLevel) / lateRms(lowerLevel));
        expect(outputDeltaDb > 0.2f,
               "Output delta was " + juce::String(outputDeltaDb, 3)
                   + " dB");
        expect(outputDeltaDb < 1.8f,
               "Output delta was " + juce::String(outputDeltaDb, 3)
                   + " dB");
        StageVocalModule stabilityFast;
        stabilityFast.prepare(spec);
        juce::AudioBuffer<float> stabilitySignal(2, 48000);
        for (int sample = 0;
             sample < stabilitySignal.getNumSamples(); ++sample)
        {
            const auto input = sample % 2 == 0 ? -0.4f : 0.4f;
            stabilitySignal.setSample(0, sample, input);
            stabilitySignal.setSample(1, sample, input);
        }
        stabilityFast.process(stabilitySignal, fastControls, {});
        auto minimumSteadyGain = 100.0f;
        auto maximumSteadyGain = 0.0f;
        for (int sample = stabilitySignal.getNumSamples() - 4800;
             sample < stabilitySignal.getNumSamples(); ++sample)
        {
            const auto gain =
                std::abs(stabilitySignal.getSample(0, sample) / 0.4f);
            minimumSteadyGain = juce::jmin(minimumSteadyGain, gain);
            maximumSteadyGain = juce::jmax(maximumSteadyGain, gain);
        }
        expect(maximumSteadyGain
                   < minimumSteadyGain * 1.25f,
               "Steady gain ranged "
                   + juce::String(minimumSteadyGain, 4) + " to "
                   + juce::String(maximumSteadyGain, 4));

        beginTest("Slow optical stage levels sustained phrases");
        auto slowControls = neutralControls();
        slowControls[StageVocalModule::slowAmountControl] = 1.0f;
        slowControls[StageVocalModule::slowAttackControl] =
            exponentialNormalized(2.0f, 40.0f, 10.0f);
        slowControls[StageVocalModule::slowReleaseControl] =
            exponentialNormalized(200.0f, 3000.0f, 1200.0f);
        StageVocalModule slow;
        slow.prepare(spec);
        auto slowSignal = makeSine(2, 96000, 500.0f, 0.65f);
        const auto slowInput = lateRms(slowSignal);
        slow.process(slowSignal, slowControls, {});
        expect(lateRms(slowSignal) < slowInput * 0.45f);

        beginTest("Optical recovery persists longer than fast FET recovery");
        StageVocalModule fastRecovery;
        StageVocalModule slowRecovery;
        fastRecovery.prepare(spec);
        slowRecovery.prepare(spec);
        auto driveFast = makeSine(2, 48000, 500.0f, 0.8f);
        auto driveSlow = driveFast;
        fastRecovery.process(driveFast, fastControls, {});
        slowRecovery.process(driveSlow, slowControls, {});
        auto fastProbe = makeSine(2, 14400, 500.0f, 0.06f);
        auto slowProbe = fastProbe;
        fastRecovery.process(fastProbe, fastControls, {});
        slowRecovery.process(slowProbe, slowControls, {});
        expect(lateRms(slowProbe) < lateRms(fastProbe) * 0.75f);

        beginTest("Serial FET and optical stages add their gain reduction");
        auto serialControls = fastControls;
        serialControls[StageVocalModule::slowAmountControl] = 1.0f;
        serialControls[StageVocalModule::slowAttackControl] =
            slowControls[StageVocalModule::slowAttackControl];
        serialControls[StageVocalModule::slowReleaseControl] =
            slowControls[StageVocalModule::slowReleaseControl];
        StageVocalModule serial;
        serial.prepare(spec);
        auto serialSignal = makeSine(2, 96000, 500.0f, 0.8f);
        serial.process(serialSignal, serialControls, {});
        expect(lateRms(serialSignal) < lateRms(slowSignal) * 0.8f);

        beginTest("De-esser remains frequency selective");
        auto deEssControls = neutralControls();
        deEssControls[StageVocalModule::deEssFrequencyControl] =
            exponentialNormalized(3000.0f, 10000.0f, 5000.0f);
        deEssControls[StageVocalModule::deEssAmountControl] = 1.0f;
        StageVocalModule lowVoice;
        StageVocalModule sibilance;
        lowVoice.prepare(spec);
        sibilance.prepare(spec);
        auto lowBand = makeSine(2, 48000, 1000.0f, 0.3f);
        auto highBand = makeSine(2, 48000, 8000.0f, 0.3f);
        const auto lowInput = lateRms(lowBand);
        const auto highInput = lateRms(highBand);
        lowVoice.process(lowBand, deEssControls, {});
        sibilance.process(highBand, deEssControls, {});
        expect(lateRms(highBand) / highInput
               < lateRms(lowBand) / lowInput * 0.7f);

        beginTest("Tone shaping does not alter either dynamics detector");
        auto flatToneControls = defaults;
        flatToneControls[StageVocalModule::lowCutControl] = 0.0f;
        auto brightToneControls = flatToneControls;
        brightToneControls[StageVocalModule::presenceControl] = 1.0f;
        brightToneControls[StageVocalModule::airControl] = 1.0f;
        StageVocalModule flatTone;
        StageVocalModule brightTone;
        flatTone.prepare(spec);
        brightTone.prepare(spec);
        auto flatSignal = makeSine(2, 48000, 7000.0f, 0.4f);
        auto brightSignal = flatSignal;
        flatTone.process(
            flatSignal, flatToneControls, { nullptr, 120.0, true });
        brightTone.process(
            brightSignal, brightToneControls, { nullptr, 120.0, true });
        ContinuousTelemetrySnapshot flatTelemetry;
        ContinuousTelemetrySnapshot brightTelemetry;
        expect(flatTone.readContinuousTelemetry(flatTelemetry));
        expect(brightTone.readContinuousTelemetry(brightTelemetry));
        for (const auto reduction : {
                 StageVocalModule::fastReductionDb,
                 StageVocalModule::deEssReductionDb,
                 StageVocalModule::slowReductionDb })
            expectWithinAbsoluteError(
                flatTelemetry.values[reduction],
                brightTelemetry.values[reduction], 0.0001f);

        beginTest("Mono and telemetry stay finite and zero latency");
        StageVocalModule mono;
        mono.prepare({ sampleRate, 257, 1 });
        auto monoSignal = makeSine(1, 257, 311.0f, 0.2f);
        monoSignal.setSample(
            0, 12, std::numeric_limits<float>::quiet_NaN());
        mono.process(
            monoSignal, defaults, { nullptr, 120.0, true });
        for (int sample = 0; sample < monoSignal.getNumSamples(); ++sample)
            expect(std::isfinite(monoSignal.getSample(0, sample)));
        ContinuousTelemetrySnapshot telemetry;
        expect(mono.readContinuousTelemetry(telemetry));
        expectEquals(
            static_cast<int>(telemetry.valueCount),
            static_cast<int>(StageVocalModule::telemetryValueCount));
        expect(mono.latencySamples() == 0);
        expect(mono.tailSeconds(defaults) == 0.0);
    }
};

StageVocalTests stageVocalTests;
} // namespace
