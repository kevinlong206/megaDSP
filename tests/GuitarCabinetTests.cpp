#include "DspModules.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
class GuitarCabinetTests final : public juce::UnitTest
{
public:
    GuitarCabinetTests()
        : juce::UnitTest("Guitar Cabinet", "megaDSP")
    {
    }

    void runTest() override
    {
        using namespace megadsp;
        constexpr int blockSize = 256;
        const juce::dsp::ProcessSpec stereoSpec { 48000.0, blockSize, 2 };

        beginTest("Every bundled cabinet IR decodes and prepares");
        GuitarCabinetModule module;
        module.prepare(stereoSpec);
        expect(module.hasImpulseResponse());
        expectEquals(
            module.builtInImpulseResponseCount(),
            GuitarCabinetModule::builtInCount);
        for (int index = 0; index < GuitarCabinetModule::builtInCount; ++index)
        {
            PreparedImpulseResponsePtr prepared;
            const auto result =
                module.prepareBuiltInImpulseResponse(index, prepared);
            expect(result.wasOk(), result.getErrorMessage());
            expect(prepared != nullptr);
            expect(module.commitPreparedImpulseResponse(prepared));
            expect(module.hasImpulseResponse());
            expect(module.impulseResponseName().isNotEmpty());
            expect(module.currentImpulseResponseTailSeconds() > 0.0);
            const auto preview = module.impulseResponsePreview();
            expect(*std::max_element(preview.begin(), preview.end()) > 0.0f);
        }

        beginTest("Bundled cabinets are pink-noise level matched");
        constexpr int noiseSamples = 48000 * 3;
        std::vector<float> pinkNoise(static_cast<size_t>(noiseSamples));
        std::uint32_t random = 0x91e10da5u;
        std::array<float, 7> rows {};
        for (int sample = 0; sample < noiseSamples; ++sample)
        {
            random = random * 1664525u + 1013904223u;
            auto white = static_cast<float>(
                static_cast<std::int32_t>(random)) / 2147483648.0f;
            auto sum = 0.0f;
            for (size_t row = 0; row < rows.size(); ++row)
            {
                const auto period = std::uint32_t { 1 }
                    << static_cast<unsigned int>(row);
                if ((static_cast<std::uint32_t>(sample) & (period - 1)) == 0)
                {
                    random = random * 1664525u + 1013904223u;
                    rows[row] = static_cast<float>(
                        static_cast<std::int32_t>(random))
                        / 2147483648.0f;
                }
                sum += rows[row];
            }
            pinkNoise[static_cast<size_t>(sample)] =
                0.025f * (sum / static_cast<float>(rows.size()) + white);
        }

        auto minimumLevel = std::numeric_limits<double>::max();
        auto maximumLevel = 0.0;
        GuitarCabinetModule matched;
        matched.prepare(stereoSpec);
        auto matchedControls = moduleDefaults(ModuleType::guitarCabinet);
        juce::AudioBuffer<float> matchedBlock(2, blockSize);
        for (int index = 0; index < GuitarCabinetModule::builtInCount; ++index)
        {
            PreparedImpulseResponsePtr prepared;
            expect(matched.prepareBuiltInImpulseResponse(
                index, prepared).wasOk());
            expect(matched.commitPreparedImpulseResponse(prepared));
            matched.reset();
            auto energy = 0.0;
            auto measuredSamples = 0;
            for (int offset = 0; offset < noiseSamples; offset += blockSize)
            {
                const auto count = juce::jmin(
                    blockSize, noiseSamples - offset);
                matchedBlock.clear();
                matchedBlock.copyFrom(
                    0, 0, pinkNoise.data() + offset, count);
                matchedBlock.copyFrom(
                    1, 0, pinkNoise.data() + offset, count);
                matched.process(matchedBlock, matchedControls, {});
                if (offset < noiseSamples - 48000)
                    continue;
                for (int channel = 0; channel < 2; ++channel)
                    for (int sample = 0; sample < count; ++sample)
                    {
                        const auto output =
                            matchedBlock.getSample(channel, sample);
                        energy += static_cast<double>(output) * output;
                        ++measuredSamples;
                    }
            }
            const auto rms = std::sqrt(
                energy / static_cast<double>(juce::jmax(1, measuredSamples)));
            minimumLevel = juce::jmin(minimumLevel, rms);
            maximumLevel = juce::jmax(maximumLevel, rms);
        }
        expect(minimumLevel > 0.0);
        const auto levelSpreadDb = juce::Decibels::gainToDecibels(
            static_cast<float>(maximumLevel / minimumLevel));
        expect(
            levelSpreadDb < 3.5f,
            "Level spread was " + juce::String(levelSpreadDb, 2) + " dB");

        beginTest("Bundled cabinets produce distinct finite responses");
        GuitarCabinetModule first;
        GuitarCabinetModule second;
        first.prepare(stereoSpec);
        second.prepare(stereoSpec);
        PreparedImpulseResponsePtr firstPrepared;
        PreparedImpulseResponsePtr secondPrepared;
        expect(first.prepareBuiltInImpulseResponse(
            0, firstPrepared).wasOk());
        expect(second.prepareBuiltInImpulseResponse(
            GuitarCabinetModule::builtInCount - 1,
            secondPrepared).wasOk());
        expect(first.commitPreparedImpulseResponse(firstPrepared));
        expect(second.commitPreparedImpulseResponse(secondPrepared));
        auto controls = moduleDefaults(ModuleType::guitarCabinet);
        juce::AudioBuffer<float> firstBlock(2, blockSize);
        juce::AudioBuffer<float> secondBlock(2, blockSize);
        double difference = 0.0;
        for (int pass = 0; pass < 24; ++pass)
        {
            firstBlock.clear();
            secondBlock.clear();
            if (pass == 0)
            {
                firstBlock.setSample(0, 0, 1.0f);
                firstBlock.setSample(1, 0, 0.5f);
                secondBlock.setSample(0, 0, 1.0f);
                secondBlock.setSample(1, 0, 0.5f);
            }
            first.process(firstBlock, controls, {});
            second.process(secondBlock, controls, {});
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < blockSize; ++sample)
                {
                    expect(std::isfinite(firstBlock.getSample(channel, sample)));
                    expect(std::isfinite(secondBlock.getSample(channel, sample)));
                    difference += std::abs(
                        firstBlock.getSample(channel, sample)
                        - secondBlock.getSample(channel, sample));
                }
        }
        expect(difference > 0.1);

        beginTest("Hot input remains finite and bounded");
        GuitarCabinetModule bounded;
        bounded.prepare(stereoSpec);
        controls[5] = 1.0f;
        controls[3] = 1.0f;
        controls[4] = 1.0f;
        auto maximum = 0.0f;
        juce::AudioBuffer<float> block(2, blockSize);
        for (int pass = 0; pass < 80; ++pass)
        {
            for (int sample = 0; sample < blockSize; ++sample)
            {
                const auto phase = static_cast<float>(pass * blockSize + sample);
                const auto source = 2.0f * std::sin(
                    juce::MathConstants<float>::twoPi * 173.0f
                    * phase / 48000.0f);
                block.setSample(0, sample, source);
                block.setSample(1, sample, source * 0.61f);
            }
            bounded.process(block, controls, {});
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < blockSize; ++sample)
                {
                    const auto value = block.getSample(channel, sample);
                    expect(std::isfinite(value));
                    maximum = juce::jmax(maximum, std::abs(value));
                }
        }
        expect(maximum < 16.0f);

        beginTest("Mono processing and telemetry are supported");
        GuitarCabinetModule mono;
        mono.prepare({ 44100.0, blockSize, 1 });
        controls = moduleDefaults(ModuleType::guitarCabinet);
        juce::AudioBuffer<float> monoBlock(1, blockSize);
        ProcessEnvironment environment;
        environment.captureTelemetry = true;
        for (int pass = 0; pass < 8; ++pass)
        {
            for (int sample = 0; sample < blockSize; ++sample)
                monoBlock.setSample(
                    0, sample,
                    0.6f * std::sin(
                        juce::MathConstants<float>::twoPi * 220.0f
                        * static_cast<float>(pass * blockSize + sample)
                        / 44100.0f));
            mono.process(monoBlock, controls, environment);
            for (int sample = 0; sample < blockSize; ++sample)
                expect(std::isfinite(monoBlock.getSample(0, sample)));
        }
        ContinuousTelemetrySnapshot telemetry;
        expect(mono.readContinuousTelemetry(telemetry));
        expect(telemetry.sequence > 0);
        expectEquals(
            static_cast<int>(telemetry.valueCount),
            static_cast<int>(GuitarCabinetModule::telemetryValueCount));
    }
};

GuitarCabinetTests guitarCabinetTests;
} // namespace
