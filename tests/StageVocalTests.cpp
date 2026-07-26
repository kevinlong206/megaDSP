#include "modules/StageVocal.h"
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

class StageVocalTests final : public juce::UnitTest
{
public:
    StageVocalTests() : UnitTest("Stage Vocal") {}

    void runTest() override
    {
        using namespace megadsp;
        const juce::dsp::ProcessSpec spec { sampleRate, 512, 2 };

        beginTest("Registry exposes the complete live vocal strip");
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

        beginTest("Low cut rejects rumble while retaining vocal fundamentals");
        auto cleanupControls = defaults;
        cleanupControls[StageVocalModule::lowCutControl] =
            exponentialNormalized(40.0f, 220.0f, 180.0f);
        cleanupControls[StageVocalModule::expanderRangeControl] = 0.0f;
        cleanupControls[StageVocalModule::deEssAmountControl] = 0.0f;
        cleanupControls[StageVocalModule::compressorRatioControl] = 0.0f;
        cleanupControls[StageVocalModule::presenceControl] = 0.5f;
        cleanupControls[StageVocalModule::airControl] = 0.5f;
        StageVocalModule lowCutLow;
        StageVocalModule lowCutHigh;
        lowCutLow.prepare(spec);
        lowCutHigh.prepare(spec);
        auto rumble = makeSine(2, 48000, 50.0f, 0.2f);
        auto vocal = makeSine(2, 48000, 500.0f, 0.2f);
        lowCutLow.process(rumble, cleanupControls, {});
        lowCutHigh.process(vocal, cleanupControls, {});
        expect(lateRms(rumble) < lateRms(vocal) * 0.25f);

        beginTest("Expander attenuates quiet stage bleed without hard muting");
        auto expanderControls = cleanupControls;
        expanderControls[StageVocalModule::lowCutControl] = 0.0f;
        expanderControls[StageVocalModule::expanderThresholdControl] =
            linearNormalized(-70.0f, -20.0f, -35.0f);
        expanderControls[StageVocalModule::expanderRangeControl] = 1.0f;
        StageVocalModule quietExpander;
        quietExpander.prepare(spec);
        auto quiet = makeSine(2, 48000, 500.0f, 0.005f);
        const auto quietInput = lateRms(quiet);
        quietExpander.process(quiet, expanderControls, {});
        expect(lateRms(quiet) < quietInput * 0.12f);
        expect(lateRms(quiet) > 0.0f);

        beginTest("Expander opens quickly when the vocal returns");
        auto onset = makeSine(2, 960, 500.0f, 0.5f);
        const auto onsetInput = lateRms(onset);
        quietExpander.process(onset, expanderControls, {});
        expect(lateRms(onset) > onsetInput * 0.8f);

        beginTest("De-esser focuses attenuation above its selected frequency");
        auto deEssControls = cleanupControls;
        deEssControls[StageVocalModule::lowCutControl] = 0.0f;
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
        const auto lowRatio = lateRms(lowBand) / lowInput;
        const auto highRatio = lateRms(highBand) / highInput;
        expect(highRatio < lowRatio * 0.7f);

        beginTest("Tone shaping does not drive the dynamics detectors");
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
                 StageVocalModule::expanderReductionDb,
                 StageVocalModule::deEssReductionDb,
                 StageVocalModule::compressorReductionDb })
        {
            expectWithinAbsoluteError(
                flatTelemetry.values[reduction],
                brightTelemetry.values[reduction], 0.0001f);
        }

        beginTest("Linked compression controls loud vocal peaks");
        auto compressorControls = cleanupControls;
        compressorControls[StageVocalModule::lowCutControl] = 0.0f;
        compressorControls[StageVocalModule::compressorThresholdControl] =
            linearNormalized(-40.0f, 0.0f, -24.0f);
        compressorControls[StageVocalModule::compressorRatioControl] =
            exponentialNormalized(1.0f, 10.0f, 6.0f);
        compressorControls[StageVocalModule::attackControl] =
            exponentialNormalized(0.5f, 50.0f, 1.0f);
        StageVocalModule compressor;
        compressor.prepare(spec);
        auto loud = makeSine(2, 48000, 500.0f, 0.8f);
        const auto loudInput = lateRms(loud);
        compressor.process(loud, compressorControls, {});
        expect(lateRms(loud) < loudInput * 0.55f);

        beginTest("Mono, stereo and telemetry stay finite");
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
