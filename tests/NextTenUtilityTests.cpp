#include "DspModules.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 4096;
const megadsp::ProcessEnvironment capture { nullptr, 120.0, true };

void fillTestSignal(juce::AudioBuffer<float>& buffer)
{
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto time = static_cast<float>(sample / sampleRate);
        const auto impulse = sample % 997 == 0 ? 0.35f : 0.0f;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.setSample(
                channel, sample,
                impulse
                    + (channel == 0 ? 0.22f : 0.15f)
                        * std::sin(
                            juce::MathConstants<float>::twoPi
                            * (channel == 0 ? 317.0f : 463.0f) * time));
    }
}

bool isFinite(const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            if (!std::isfinite(buffer.getSample(channel, sample)))
                return false;
    return true;
}

class NextTenUtilityTests final : public juce::UnitTest
{
public:
    NextTenUtilityTests()
        : juce::UnitTest("Next ten utility modules", "megaDSP")
    {
    }

    void runTest() override
    {
        using namespace megadsp;

        beginTest("All six modules are finite, deterministic, and silence safe");
        exerciseContract<PhaseCoherenceModule>(
            ModuleType::phaseCoherence,
            PhaseCoherenceModule::telemetryValueCount);
        exerciseContract<LoudnessRiderModule>(
            ModuleType::loudnessRider,
            LoudnessRiderModule::telemetryValueCount);
        exerciseContract<AdaptiveClipperModule>(
            ModuleType::adaptiveClipper,
            AdaptiveClipperModule::telemetryValueCount);
        exerciseContract<FormantForgeModule>(
            ModuleType::formantForge,
            FormantForgeModule::telemetryValueCount);
        exerciseContract<HarmonicMirageModule>(
            ModuleType::harmonicMirage,
            HarmonicMirageModule::telemetryValueCount);
        exerciseContract<ChaosFieldModule>(
            ModuleType::chaosField,
            ChaosFieldModule::telemetryValueCount);

        beginTest("Advertised fixed-latency dry paths are sample aligned");
        {
            PhaseCoherenceModule module;
            auto controls = moduleDefaults(ModuleType::phaseCoherence);
            controls[PhaseCoherenceModule::correctionControl] = 0.0f;
            controls[PhaseCoherenceModule::monoBelowControl] = 0.0f;
            expectAlignedDry(module, controls);
        }
        {
            LoudnessRiderModule module;
            auto controls = moduleDefaults(ModuleType::loudnessRider);
            controls[LoudnessRiderModule::rangeControl] = 0.0f;
            expectAlignedDry(module, controls);
        }
        {
            AdaptiveClipperModule module;
            auto controls = moduleDefaults(ModuleType::adaptiveClipper);
            controls[AdaptiveClipperModule::mixControl] = 0.0f;
            expectAlignedDry(module, controls);
        }
        {
            HarmonicMirageModule module;
            auto controls = moduleDefaults(ModuleType::harmonicMirage);
            controls[HarmonicMirageModule::mixControl] = 0.0f;
            expectAlignedDry(module, controls);
            expectEquals(
                module.latencySamples(),
                FixedLatencyStft::reportedLatencySamples);
        }

        beginTest("Phase Coherence finds and confidently repairs a known lag");
        PhaseCoherenceModule phase;
        constexpr int phaseSamples = 16384;
        phase.prepare({ sampleRate, phaseSamples, 2 });
        auto phaseControls = moduleDefaults(ModuleType::phaseCoherence);
        phaseControls[PhaseCoherenceModule::correctionControl] = 1.0f;
        phaseControls[PhaseCoherenceModule::maxAlignmentControl] = 1.0f;
        phaseControls[PhaseCoherenceModule::phaseRotationControl] = 1.0f;
        phaseControls[PhaseCoherenceModule::stereoPreserveControl] = 1.0f;
        phaseControls[PhaseCoherenceModule::monoBelowControl] = 0.0f;
        juce::AudioBuffer<float> lagged(2, phaseSamples);
        std::vector<float> source(static_cast<size_t>(phaseSamples));
        std::uint32_t randomState = 0x12345678u;
        for (auto& value : source)
        {
            randomState ^= randomState << 13;
            randomState ^= randomState >> 17;
            randomState ^= randomState << 5;
            value = 0.25f
                * (static_cast<float>(randomState >> 8)
                       * (1.0f / 8388608.0f)
                   - 1.0f);
        }
        constexpr int knownLag = 24;
        for (int sample = 0; sample < phaseSamples; ++sample)
        {
            lagged.setSample(0, sample, source[static_cast<size_t>(sample)]);
            lagged.setSample(
                1, sample,
                sample >= knownLag
                    ? source[static_cast<size_t>(sample - knownLag)] : 0.0f);
        }
        phase.process(lagged, phaseControls, capture);
        ContinuousTelemetrySnapshot phaseTelemetry;
        expect(phase.readContinuousTelemetry(phaseTelemetry));
        expect(phaseTelemetry.values[PhaseCoherenceModule::analysisConfidence]
               > 0.7f);
        expectWithinAbsoluteError(
            phaseTelemetry.values[
                PhaseCoherenceModule::estimatedDelayMilliseconds],
            0.5f, 0.08f);

        beginTest("Loudness Rider movement is gated and range bounded");
        LoudnessRiderModule rider;
        rider.prepare({ sampleRate, blockSize, 2 });
        auto riderControls = moduleDefaults(ModuleType::loudnessRider);
        riderControls[LoudnessRiderModule::targetControl] = 0.0f;
        riderControls[LoudnessRiderModule::rangeControl] = 0.25f;
        riderControls[LoudnessRiderModule::windowControl] = 0.0f;
        riderControls[LoudnessRiderModule::reactionControl] = 0.0f;
        riderControls[LoudnessRiderModule::transientHoldControl] = 0.0f;
        riderControls[LoudnessRiderModule::crestPreserveControl] = 0.0f;
        riderControls[LoudnessRiderModule::gateControl] = 0.0f;
        juce::AudioBuffer<float> riderTone(2, blockSize);
        for (int block = 0; block < 20; ++block)
        {
            for (int sample = 0; sample < blockSize; ++sample)
            {
                const auto time = static_cast<float>(
                    block * blockSize + sample) / static_cast<float>(sampleRate);
                const auto value = 0.5f * std::sin(
                    juce::MathConstants<float>::twoPi * 997.0f * time);
                riderTone.setSample(0, sample, value);
                riderTone.setSample(1, sample, value);
            }
            rider.process(riderTone, riderControls, capture);
        }
        ContinuousTelemetrySnapshot riderTelemetry;
        expect(rider.readContinuousTelemetry(riderTelemetry));
        const auto ride =
            riderTelemetry.values[LoudnessRiderModule::rideGainDecibels];
        expect(ride < -0.5f);
        expect(ride >= -4.5001f && ride <= 4.5001f);
        expect(riderTelemetry.values[
                   LoudnessRiderModule::requestedGainDecibels]
               >= -4.5001f);
        rider.reset();
        riderTone.clear();
        rider.process(riderTone, riderControls, capture);
        expect(rider.readContinuousTelemetry(riderTelemetry));
        expectEquals(
            riderTelemetry.values[LoudnessRiderModule::gatedState], 1.0f);
        expectEquals(
            riderTelemetry.values[LoudnessRiderModule::rideGainDecibels],
            0.0f);

        beginTest("Adaptive Clipper honors ceiling and selected oversampling");
        AdaptiveClipperModule clipper;
        clipper.prepare({ sampleRate, blockSize, 2 });
        auto clipControls = moduleDefaults(ModuleType::adaptiveClipper);
        clipControls[AdaptiveClipperModule::driveControl] = 1.0f;
        clipControls[AdaptiveClipperModule::ceilingControl] = 0.5f;
        clipControls[AdaptiveClipperModule::oversamplingControl] = 1.0f;
        clipControls[AdaptiveClipperModule::autoTrimControl] = 0.0f;
        clipControls[AdaptiveClipperModule::mixControl] = 1.0f;
        juce::AudioBuffer<float> hot(2, blockSize);
        for (int sample = 0; sample < blockSize; ++sample)
        {
            const auto value = 1.2f * std::sin(
                juce::MathConstants<float>::twoPi * 1103.0f
                * static_cast<float>(sample) / static_cast<float>(sampleRate));
            hot.setSample(0, sample, value);
            hot.setSample(1, sample, -0.8f * value);
        }
        clipper.process(hot, clipControls, capture);
        const auto ceiling = juce::Decibels::decibelsToGain(-6.0f) * 0.94f;
        expect(hot.getMagnitude(0, 0, blockSize) <= ceiling + 1.0e-5f);
        expect(hot.getMagnitude(1, 0, blockSize) <= ceiling + 1.0e-5f);
        ContinuousTelemetrySnapshot clipTelemetry;
        expect(clipper.readContinuousTelemetry(clipTelemetry));
        expectEquals(
            clipTelemetry.values[AdaptiveClipperModule::activeOversampling],
            8.0f);
        expect(clipTelemetry.values[AdaptiveClipperModule::clippingDecibels]
               > 1.0f);
        expect(clipTelemetry.values[AdaptiveClipperModule::clippedEnergy]
               > 0.0f);

        beginTest("Formant Forge reports predictable model and octave shift");
        const auto lowFormants = renderFormants(0.5f, 0.0f);
        const auto highFormants = renderFormants(0.75f, 0.0f);
        const auto metalFormants = renderFormants(0.5f, 1.0f);
        expectWithinAbsoluteError(
            highFormants.values[FormantForgeModule::actualFormant1Hz]
                / lowFormants.values[FormantForgeModule::actualFormant1Hz],
            2.0f, 0.02f);
        expect(
            metalFormants.values[FormantForgeModule::actualFormant4Hz]
            > lowFormants.values[FormantForgeModule::actualFormant4Hz]);
        expect(lowFormants.values[FormantForgeModule::actualOutputRms]
               > 1.0e-5f);

        beginTest("Harmonic Mirage tracks a tone and activates partials");
        HarmonicMirageModule mirage;
        mirage.prepare({ sampleRate, blockSize, 2 });
        auto mirageControls = moduleDefaults(ModuleType::harmonicMirage);
        mirageControls[HarmonicMirageModule::trackingControl] = 0.0f;
        mirageControls[HarmonicMirageModule::partialsControl] = 0.35f;
        mirageControls[HarmonicMirageModule::fineDriftControl] = 0.0f;
        mirageControls[HarmonicMirageModule::responseControl] = 0.0f;
        mirageControls[HarmonicMirageModule::transientPreserveControl] = 0.0f;
        mirageControls[HarmonicMirageModule::mixControl] = 1.0f;
        juce::AudioBuffer<float> harmonicTone(2, blockSize);
        constexpr float trackedTone = 468.75f;
        for (int block = 0; block < 8; ++block)
        {
            for (int sample = 0; sample < blockSize; ++sample)
            {
                const auto time = static_cast<float>(
                    block * blockSize + sample) / static_cast<float>(sampleRate);
                const auto value = 0.3f * std::sin(
                    juce::MathConstants<float>::twoPi * trackedTone * time);
                harmonicTone.setSample(0, sample, value);
                harmonicTone.setSample(1, sample, value);
            }
            mirage.process(harmonicTone, mirageControls, capture);
        }
        ContinuousTelemetrySnapshot mirageTelemetry;
        expect(mirage.readContinuousTelemetry(mirageTelemetry));
        expectWithinAbsoluteError(
            mirageTelemetry.values[HarmonicMirageModule::trackedFrequencyHz],
            trackedTone, 15.0f);
        expect(mirageTelemetry.values[
                   HarmonicMirageModule::trackingConfidence]
               > 0.2f);
        expect(mirageTelemetry.values[
                   HarmonicMirageModule::activePartialCount]
               >= 2.0f);
        expect(mirageTelemetry.values[HarmonicMirageModule::generatedRms]
               > 1.0e-5f);

        beginTest("Chaos Field has a deterministic bounded active tail");
        ChaosFieldModule chaos;
        auto chaosControls = moduleDefaults(ModuleType::chaosField);
        chaosControls[ChaosFieldModule::depthControl] = 0.55f;
        chaosControls[ChaosFieldModule::delayCenterControl] = 0.15f;
        chaosControls[ChaosFieldModule::feedbackControl] = 0.80f;
        chaosControls[ChaosFieldModule::mixControl] = 1.0f;
        const auto tail = chaos.tailSeconds(chaosControls);
        expect(std::isfinite(tail) && tail >= 0.002 && tail <= 30.0);
        chaos.prepare({ sampleRate, blockSize, 2 });
        juce::AudioBuffer<float> impulse(2, blockSize);
        impulse.clear();
        impulse.setSample(0, 0, 0.7f);
        impulse.setSample(1, 0, 0.7f);
        double tailEnergy = 0.0;
        float peak = 0.0f;
        for (int block = 0; block < 12; ++block)
        {
            if (block != 0)
                impulse.clear();
            chaos.process(impulse, chaosControls, capture);
            peak = std::max(peak, impulse.getMagnitude(0, 0, blockSize));
            for (int sample = 0; sample < blockSize; ++sample)
                tailEnergy += std::abs(impulse.getSample(0, sample));
        }
        expect(tailEnergy > 0.01);
        expect(std::isfinite(peak) && peak < 4.0f);
        ContinuousTelemetrySnapshot chaosTelemetry;
        expect(chaos.readContinuousTelemetry(chaosTelemetry));
        expect(std::abs(chaosTelemetry.values[ChaosFieldModule::actualX])
               <= 1.0f);
        expect(std::abs(chaosTelemetry.values[ChaosFieldModule::actualY])
               <= 1.0f);
        expect(std::abs(chaosTelemetry.values[ChaosFieldModule::actualZ])
               <= 1.0f);
    }

private:
    template <typename Module>
    void exerciseContract(megadsp::ModuleType type, int expectedValueCount)
    {
        Module module;
        const auto controls = megadsp::moduleDefaults(type);
        module.prepare({ sampleRate, blockSize, 2 });
        juce::AudioBuffer<float> input(2, blockSize);
        fillTestSignal(input);
        auto first = input;
        module.process(first, controls, capture);
        expect(isFinite(first));

        megadsp::ContinuousTelemetrySnapshot telemetry;
        expect(module.readContinuousTelemetry(telemetry));
        expectEquals(static_cast<int>(telemetry.sequence), 1);
        expectEquals(
            static_cast<int>(telemetry.valueCount), expectedValueCount);
        for (std::uint32_t index = 0; index < telemetry.valueCount; ++index)
            expect(std::isfinite(telemetry.values[index]));

        module.reset();
        auto second = input;
        module.process(second, controls, {});
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < blockSize; ++sample)
                expectWithinAbsoluteError(
                    second.getSample(channel, sample),
                    first.getSample(channel, sample), 1.0e-6f);

        module.prepare({ sampleRate, blockSize, 1 });
        juce::AudioBuffer<float> monoSilence(1, blockSize);
        monoSilence.clear();
        module.process(monoSilence, controls, capture);
        expect(isFinite(monoSilence));
        expect(monoSilence.getMagnitude(0, 0, blockSize) < 1.0e-7f);
    }

    void expectAlignedDry(
        megadsp::DspModule& module,
        const megadsp::ControlValues& controls)
    {
        module.prepare({ sampleRate, 16384, 2 });
        const auto latency = module.latencySamples();
        expect(latency > 0);
        const auto samples = latency + 2048;
        juce::AudioBuffer<float> buffer(2, samples);
        fillTestSignal(buffer);
        const auto reference = buffer;
        module.process(buffer, controls, {});
        for (int channel = 0; channel < 2; ++channel)
        {
            expect(buffer.getMagnitude(channel, 0, latency) < 1.0e-7f);
            for (int sample = latency; sample < samples; ++sample)
                expectWithinAbsoluteError(
                    buffer.getSample(channel, sample),
                    reference.getSample(channel, sample - latency), 2.0e-6f);
        }
    }

    megadsp::ContinuousTelemetrySnapshot renderFormants(
        float shift, float model)
    {
        megadsp::FormantForgeModule module;
        module.prepare({ sampleRate, blockSize, 2 });
        auto controls =
            megadsp::moduleDefaults(megadsp::ModuleType::formantForge);
        controls[megadsp::FormantForgeModule::modelControl] = model;
        controls[megadsp::FormantForgeModule::formantShiftControl] = shift;
        controls[megadsp::FormantForgeModule::breathControl] = 0.0f;
        controls[megadsp::FormantForgeModule::motionDepthControl] = 0.0f;
        controls[megadsp::FormantForgeModule::mixControl] = 1.0f;
        juce::AudioBuffer<float> excitation(2, blockSize);
        fillTestSignal(excitation);
        module.process(excitation, controls, capture);
        megadsp::ContinuousTelemetrySnapshot telemetry;
        expect(module.readContinuousTelemetry(telemetry));
        return telemetry;
    }
};

NextTenUtilityTests nextTenUtilityTests;
} // namespace
