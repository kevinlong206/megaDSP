#include "../src/modules/IRMosaic.h"
#include "../src/modules/ModuleRegistry.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>

namespace
{
megadsp::ControlValues mosaicDefaults()
{
    megadsp::ControlValues controls {};
    controls[0] = megadsp::discreteValue(1, 4);
    controls[1] = std::log(2.0f / 0.03f) / std::log(4.0f / 0.03f);
    controls[2] = 0.0f;
    controls[3] = std::log(0.08f / 0.05f) / std::log(0.5f / 0.05f);
    controls[4] = megadsp::discreteValue(2, 8);
    controls[5] = 1.0f;
    controls[6] = 2.0f / 3.0f;
    controls[8] = 0.0f;
    controls[9] = 1.0f;
    controls[10] = 0.6f;
    controls[11] = 0.6f;
    return controls;
}

juce::File createMosaicImpulse()
{
    auto directory =
        juce::File::getCurrentWorkingDirectory().getChildFile("build");
    directory.createDirectory();
    const auto file = directory.getNonexistentChildFile(
        "ir-mosaic-test", ".wav");
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    auto writer = format.createWriterFor(
        stream,
        juce::AudioFormatWriterOptions {}
            .withSampleRate(48000.0)
            .withNumChannels(2)
            .withBitsPerSample(24));
    if (writer == nullptr)
        return {};
    juce::AudioBuffer<float> impulse(2, 8192);
    impulse.clear();
    for (int tile = 0; tile < 8; ++tile)
    {
        const auto sample = tile * 1024 + 64;
        impulse.setSample(0, sample, 1.0f / static_cast<float>(tile + 1));
        impulse.setSample(1, sample + 31, 0.7f / static_cast<float>(tile + 1));
    }
    writer->writeFromAudioSampleBuffer(impulse, 0, impulse.getNumSamples());
    writer.reset();
    return file;
}

void fillMosaicInput(juce::AudioBuffer<float>& block, int pass)
{
    for (int sample = 0; sample < block.getNumSamples(); ++sample)
    {
        const auto position = pass * block.getNumSamples() + sample;
        const auto value = 0.25f * std::sin(
            juce::MathConstants<float>::twoPi * 173.0f
            * static_cast<float>(position) / 48000.0f);
        block.setSample(0, sample, value);
        if (block.getNumChannels() > 1)
            block.setSample(1, sample, value * 0.4f);
    }
}

class IRMosaicTests final : public juce::UnitTest
{
public:
    IRMosaicTests() : juce::UnitTest("IR Mosaic", "megaDSP") {}

    void runTest() override
    {
        using namespace megadsp;
        auto controls = mosaicDefaults();

        beginTest("Missing IR remains immediate dry");
        expect(moduleDefaults(ModuleType::irMosaic)[1] > 0.0f);
        IRMosaicModule fallback;
        fallback.prepare({ 48000.0, 256, 2 });
        juce::AudioBuffer<float> block(2, 256);
        fillMosaicInput(block, 0);
        const auto expected = block;
        fallback.process(block, controls, { nullptr, 120.0, false });
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < 256; ++sample)
                expectWithinAbsoluteError(
                    block.getSample(channel, sample),
                    expected.getSample(channel, sample), 0.00001f);
        expectEquals(fallback.latencySamples(), 0);
        expect(fallback.dryPathTiming() == DryPathTiming::immediate);

        const auto file = createMosaicImpulse();
        expect(file.existsAsFile());

        beginTest("Loaded mosaic produces bounded moving wet output");
        IRMosaicModule loaded;
        loaded.prepare({ 48000.0, 256, 2 });
        expect(loaded.loadImpulseResponse(file).wasOk());
        expect(loaded.isImpulseResponseReady());
        expect(loaded.impulseResponseName().isNotEmpty());
        auto difference = 0.0;
        auto maximum = 0.0f;
        auto maximumBlend = 0.0f;
        auto maximumDelta = 0.0f;
        auto sawNewShuffle = false;
        std::array<float, 2> previous {};
        for (int pass = 0; pass < 180; ++pass)
        {
            fillMosaicInput(block, pass);
            const auto dry = block;
            loaded.process(block, controls, { nullptr, 240.0, true });
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < 256; ++sample)
                {
                    const auto value = block.getSample(channel, sample);
                    expect(std::isfinite(value));
                    maximum = juce::jmax(maximum, std::abs(value));
                    maximumDelta = juce::jmax(
                        maximumDelta,
                        std::abs(
                            value - previous[static_cast<size_t>(channel)]));
                    previous[static_cast<size_t>(channel)] = value;
                    difference += std::abs(
                        value - dry.getSample(channel, sample));
                }
            ContinuousTelemetrySnapshot currentTelemetry {};
            if (loaded.readContinuousTelemetry(currentTelemetry)
                && currentTelemetry.valueCount
                       >= IRMosaicModule::telemetryValueCount)
                maximumBlend = juce::jmax(
                    maximumBlend,
                    currentTelemetry.values[
                        IRMosaicModule::patternBlend]);
            if (currentTelemetry.valueCount
                    >= IRMosaicModule::telemetryValueCount
                && currentTelemetry.values[
                       IRMosaicModule::primaryPattern] > 0.5f)
                sawNewShuffle = true;
        }
        expect(difference > 0.1, "Wet output did not differ from dry.");
        expect(maximum < 4.0f, "Wet output exceeded the safety bound.");
        expect(maximumBlend > 0.25f, "No re-shuffle crossfade was measured.");
        expect(sawNewShuffle, "The active shuffled kernel never changed.");
        expect(
            maximumDelta < 0.5f,
            "A re-shuffle produced an abrupt output discontinuity.");
        ContinuousTelemetrySnapshot telemetry {};
        expect(loaded.readContinuousTelemetry(telemetry));
        expectEquals(
            static_cast<int>(telemetry.valueCount),
            static_cast<int>(IRMosaicModule::telemetryValueCount));

        beginTest("Reset is deterministic and mono safe");
        IRMosaicModule mono;
        mono.prepare({ 48000.0, 256, 1 });
        expect(mono.loadImpulseResponse(file).wasOk());
        controls[7] = 1.0f;
        controls[10] = 1.0f;
        juce::AudioBuffer<float> monoBlock(1, 256);
        auto render = [&]()
        {
            mono.reset();
            juce::AudioBuffer<float> result(1, 256 * 40);
            for (int pass = 0; pass < 40; ++pass)
            {
                fillMosaicInput(monoBlock, pass);
                mono.process(
                    monoBlock, controls, { nullptr, 180.0, false });
                result.copyFrom(0, pass * 256, monoBlock, 0, 0, 256);
            }
            return result;
        };
        const auto first = render();
        const auto second = render();
        for (int sample = 0; sample < first.getNumSamples(); ++sample)
        {
            expect(std::isfinite(first.getSample(0, sample)));
            expectWithinAbsoluteError(
                first.getSample(0, sample),
                second.getSample(0, sample), 0.000001f);
        }
        expect(loaded.tailSeconds(controls) > 0.0);
        expect(loaded.tailSeconds(controls) <= 8.0);
        file.deleteFile();
    }
};

IRMosaicTests irMosaicTests;
} // namespace
