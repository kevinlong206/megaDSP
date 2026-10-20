#include "DspModules.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>

namespace
{
juce::File createWormholeTestImpulse()
{
    auto file = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getNonexistentChildFile(
            "megadsp-wormhole", ".wav");
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream =
        file.createOutputStream();
    auto writer = format.createWriterFor(
        stream,
        juce::AudioFormatWriterOptions {}
            .withSampleRate(48000.0)
            .withNumChannels(2)
            .withBitsPerSample(24));
    if (writer == nullptr)
        return {};
    juce::AudioBuffer<float> impulse(2, 48000);
    impulse.clear();
    impulse.setSample(0, 0, 1.0f);
    impulse.setSample(1, 0, 0.8f);
    impulse.setSample(0, 12000, 0.5f);
    impulse.setSample(1, 24000, -0.4f);
    impulse.setSample(0, 47999, 0.2f);
    writer->writeFromAudioSampleBuffer(
        impulse, 0, impulse.getNumSamples());
    writer.reset();
    return file;
}

class WormholeConvolutionTests final : public juce::UnitTest
{
public:
    WormholeConvolutionTests()
        : juce::UnitTest("Wormhole Convolution", "megaDSP")
    {
    }

    void runTest() override
    {
        using namespace megadsp;
        beginTest("Registry appends Wormhole without shifting prior modules");
        const auto file = createWormholeTestImpulse();
        expect(file.existsAsFile());

        expectEquals(
            static_cast<int>(ModuleType::velvetDrive), 54);
        expectEquals(
            static_cast<int>(ModuleType::wormholeConvolution), 55);
        expectEquals(moduleTypeCount, 62);
        const auto& definition =
            moduleDefinition(ModuleType::wormholeConvolution);
        expect(
            definition.capabilities == ModuleCapability::impulseResponse);

        beginTest("Two lanes crop and reverse independently");
        WormholeConvolutionModule module;
        module.prepare({ 48000.0, 256, 2 });
        PreparedImpulseResponsePtr first;
        PreparedImpulseResponsePtr second;
        expect(module.prepareImpulseResponse(
            0, file, { 0.0f, 1.0f, false }, first).wasOk());
        expect(module.prepareImpulseResponse(
            1, file, { 0.25f, 0.75f, true }, second).wasOk());
        expect(module.commitPreparedImpulseResponse(0, first));
        expect(module.commitPreparedImpulseResponse(1, second));
        expect(module.impulseResponseName(0).isNotEmpty());
        expect(module.impulseResponseName(1).isNotEmpty());
        PreparedImpulseResponsePtr missing;
        expect(module.prepareImpulseResponse(
            0, file.getSiblingFile("missing-wormhole.wav"),
            { 0.0f, 1.0f, false }, missing).failed());
        expect(module.impulseResponseName(0).isNotEmpty());
        const auto controls = moduleDefaults(
            ModuleType::wormholeConvolution);
        expectWithinAbsoluteError(
            module.tailSeconds(controls), 1.0, 0.001);
        const auto previewA = module.impulseResponsePreview(0);
        const auto previewB = module.impulseResponsePreview(1);
        expect(*std::max_element(
            previewA.begin(), previewA.end()) > 0.0f);
        expect(*std::max_element(
            previewB.begin(), previewB.end()) > 0.0f);

        beginTest("Parallel processing is finite and dry can remain exact");
        auto dryControls = controls;
        dryControls[3] = 0.0f;
        dryControls[7] = 0.0f;
        dryControls[10] = 1.0f;
        juce::AudioBuffer<float> block(2, 256);
        for (int pass = 0; pass < 16; ++pass)
        {
            for (int sample = 0; sample < block.getNumSamples(); ++sample)
            {
                const auto absolute = pass * block.getNumSamples() + sample;
                const auto value = 0.25f * std::sin(
                    juce::MathConstants<float>::twoPi * 330.0f
                    * static_cast<float>(absolute) / 48000.0f);
                block.setSample(0, sample, value);
                block.setSample(1, sample, -value * 0.7f);
            }
            auto expected = block;
            module.process(block, dryControls, {});
            if (pass > 12)
                for (int channel = 0; channel < 2; ++channel)
                    for (int sample = 0; sample < block.getNumSamples();
                         ++sample)
                    {
                        expect(std::isfinite(
                            block.getSample(channel, sample)));
                        expectWithinAbsoluteError(
                            block.getSample(channel, sample),
                            expected.getSample(channel, sample), 0.0001f);
                    }
        }

        beginTest("Clearing one lane preserves the other");
        module.clearImpulseResponse(1);
        expect(module.impulseResponseName(1).isEmpty());
        expect(module.impulseResponseName(0).isNotEmpty());
        file.deleteFile();
    }
};

WormholeConvolutionTests wormholeConvolutionTests;
} // namespace
