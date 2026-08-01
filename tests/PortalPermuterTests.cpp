#include "../src/modules/PortalPermuter.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>

namespace
{
megadsp::ControlValues portalDefaults()
{
    megadsp::ControlValues controls {};
    controls[0] = 1.0f;
    controls[1] = 1.0f;
    controls[2] = 0.0f;
    controls[3] = 0.0f;
    controls[4] = 0.0f;
    controls[5] = 1.0f;
    controls[6] = 0.5f;
    controls[7] = 0.5f;
    controls[8] = 0.0f;
    controls[9] = 0.0f;
    controls[10] = 0.7f;
    controls[11] = 0.6f;
    return controls;
}

juce::File createPortalTestImpulse()
{
    auto directory =
        juce::File::getCurrentWorkingDirectory().getChildFile("build");
    directory.createDirectory();
    auto file = directory.getNonexistentChildFile(
        "portal-permuter-test", ".wav");
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
    juce::AudioBuffer<float> impulse(2, 4096);
    impulse.clear();
    impulse.setSample(0, 0, 1.0f);
    impulse.setSample(1, 0, 0.8f);
    impulse.setSample(0, 511, 0.25f);
    impulse.setSample(1, 997, -0.2f);
    impulse.setSample(0, 2048, 0.4f);
    impulse.setSample(1, 2301, 0.3f);
    for (int sample = 0; sample < impulse.getNumSamples(); ++sample)
    {
        const auto decay = std::exp(
            -static_cast<float>(sample) / 2200.0f);
        const auto dense = 0.16f * decay * std::sin(
            juce::MathConstants<float>::twoPi
            * static_cast<float>(sample) / 43.0f);
        impulse.addSample(0, sample, dense);
        impulse.addSample(1, sample, dense * 0.9f);
    }
    writer->writeFromAudioSampleBuffer(
        impulse, 0, impulse.getNumSamples());
    writer.reset();
    return file;
}

void fillInput(juce::AudioBuffer<float>& block, int blockIndex)
{
    for (int sample = 0; sample < block.getNumSamples(); ++sample)
    {
        const auto absolute =
            blockIndex * block.getNumSamples() + sample;
        const auto value = 0.3f * std::sin(
            juce::MathConstants<float>::twoPi * 220.0f
            * static_cast<float>(absolute) / 48000.0f);
        block.setSample(0, sample, value);
        if (block.getNumChannels() > 1)
            block.setSample(1, sample, value * -0.6f);
    }
}

class PortalPermuterTests final : public juce::UnitTest
{
public:
    PortalPermuterTests()
        : juce::UnitTest("Portal Permuter", "megaDSP")
    {
    }

    void runTest() override
    {
        using namespace megadsp;
        auto controls = portalDefaults();

        beginTest("Unavailable IR is immediate dry times Output");
        PortalPermuterModule fallback;
        fallback.prepare({ 48000.0, 256, 2 });
        juce::AudioBuffer<float> dryBlock(2, 256);
        fillInput(dryBlock, 0);
        auto expected = dryBlock;
        fallback.process(dryBlock, controls, { nullptr, 960.0, false });
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < dryBlock.getNumSamples(); ++sample)
                expectWithinAbsoluteError(
                    dryBlock.getSample(channel, sample),
                    expected.getSample(channel, sample), 0.00001f);
        expectEquals(fallback.latencySamples(), 0);
        expect(fallback.dryPathTiming() == DryPathTiming::immediate);

        const auto file = createPortalTestImpulse();
        expect(file.existsAsFile());

        beginTest("Transformed synthetic IR becomes ready and produces wet");
        PortalPermuterModule loaded;
        loaded.prepare({ 48000.0, 256, 2 });
        PreparedImpulseResponsePtr prepared;
        const auto transform =
            loaded.impulseResponseTransform(controls.data(), 0);
        expect(loaded.prepareImpulseResponse(
            0, file, transform, prepared).wasOk());
        expect(loaded.commitPreparedImpulseResponse(prepared));
        expect(loaded.isImpulseResponseReady());
        expect(loaded.impulseResponseName().isNotEmpty());
        expect(loaded.currentImpulseResponseTailSeconds() > 0.0);
        const auto preview = loaded.impulseResponsePreview();
        expect(*std::max_element(preview.begin(), preview.end()) > 0.0f);

        auto wetEnergy = 0.0;
        juce::AudioBuffer<float> wetBlock(2, 256);
        for (int pass = 0; pass < 80; ++pass)
        {
            fillInput(wetBlock, pass);
            auto dry = wetBlock;
            loaded.process(wetBlock, controls, { nullptr, 960.0, false });
            if (pass > 45)
                for (int channel = 0; channel < 2; ++channel)
                    for (int sample = 0; sample < 256; ++sample)
                        wetEnergy += std::abs(
                            wetBlock.getSample(channel, sample)
                            - dry.getSample(channel, sample));
        }
        expect(wetEnergy > 0.1);

        beginTest("Reset restores deterministic permutation");
        PortalPermuterModule deterministic;
        deterministic.prepare({ 48000.0, 256, 2 });
        expect(deterministic.loadImpulseResponse(file).wasOk());
        auto render = [&deterministic, &controls]()
        {
            deterministic.reset();
            juce::AudioBuffer<float> block(2, 256);
            juce::AudioBuffer<float> result(2, 256 * 56);
            for (int pass = 0; pass < 56; ++pass)
            {
                fillInput(block, pass);
                deterministic.process(
                    block, controls, { nullptr, 960.0, false });
                for (int channel = 0; channel < 2; ++channel)
                    result.copyFrom(
                        channel, pass * 256, block, channel, 0, 256);
            }
            return result;
        };
        const auto first = render();
        const auto second = render();
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < first.getNumSamples(); ++sample)
                expectWithinAbsoluteError(
                    first.getSample(channel, sample),
                    second.getSample(channel, sample), 0.000001f);

        beginTest("Mono remains finite and feedback cannot run away");
        PortalPermuterModule mono;
        mono.prepare({ 48000.0, 256, 1 });
        expect(mono.loadImpulseResponse(file).wasOk());
        controls[9] = 1.0f;
        controls[10] = 1.0f;
        juce::AudioBuffer<float> monoBlock(1, 256);
        auto maximum = 0.0f;
        auto lateMaximum = 0.0f;
        for (int pass = 0; pass < 3000; ++pass)
        {
            if (pass < 1800)
                fillInput(monoBlock, pass);
            else
                monoBlock.clear();
            mono.process(monoBlock, controls, { nullptr, 960.0, false });
            for (int sample = 0; sample < 256; ++sample)
            {
                const auto value = monoBlock.getSample(0, sample);
                expect(std::isfinite(value));
                maximum = juce::jmax(maximum, std::abs(value));
                if (pass >= 2800)
                    lateMaximum = juce::jmax(
                        lateMaximum, std::abs(value));
            }
        }
        expect(maximum < 4.0f);
        expect(lateMaximum < 0.25f);

        beginTest("Convolution tail survives after excitation");
        PortalPermuterModule tail;
        tail.prepare({ 48000.0, 256, 2 });
        expect(tail.loadImpulseResponse(file).wasOk());
        controls[9] = 0.0f;
        controls[10] = 1.0f;
        juce::AudioBuffer<float> tailBlock(2, 256);
        auto tailEnergy = 0.0;
        for (int pass = 0; pass < 70; ++pass)
        {
            if (pass < 55)
                fillInput(tailBlock, pass);
            else
            {
                tailBlock.clear();
                controls[1] = 0.0f;
            }
            tail.process(tailBlock, controls, { nullptr, 960.0, false });
            if (pass >= 55)
                for (int channel = 0; channel < 2; ++channel)
                    tailEnergy += tailBlock.getMagnitude(channel, 0, 256);
        }
        expect(tailEnergy > 0.0001);
        expect(tail.tailSeconds(controls) > 0.0);

        file.deleteFile();
    }
};

PortalPermuterTests portalPermuterTests;
} // namespace
