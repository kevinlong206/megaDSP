#include "DspModules.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;

juce::AudioBuffer<float> render(
    const megadsp::ControlValues& controls, float frequency,
    float amplitude, int channels = 2)
{
    constexpr int totalSamples = 48000;
    juce::AudioBuffer<float> result(channels, totalSamples);
    megadsp::VelvetDriveModule module;
    module.prepare({ sampleRate, blockSize, static_cast<juce::uint32>(channels) });
    juce::AudioBuffer<float> block(channels, blockSize);
    for (int offset = 0; offset < totalSamples; offset += blockSize)
    {
        const auto samples = juce::jmin(blockSize, totalSamples - offset);
        block.setSize(channels, samples, false, false, true);
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto absoluteSample = offset + sample;
            const auto attack = juce::jmin(
                1.0f, static_cast<float>(absoluteSample) / 480.0f);
            const auto input = amplitude * attack * std::sin(
                juce::MathConstants<float>::twoPi * frequency
                * static_cast<float>(absoluteSample)
                / static_cast<float>(sampleRate));
            for (int channel = 0; channel < channels; ++channel)
                block.setSample(channel, sample, input);
        }
        module.process(block, controls, {});
        for (int channel = 0; channel < channels; ++channel)
            result.copyFrom(channel, offset, block, channel, 0, samples);
    }
    return result;
}

float harmonicRatio(
    const juce::AudioBuffer<float>& buffer, float frequency)
{
    constexpr int analysisSamples = 16384;
    const auto start = buffer.getNumSamples() - analysisSamples;
    const auto magnitudeAt = [&](float target)
    {
        double real = 0.0;
        double imaginary = 0.0;
        for (int sample = 0; sample < analysisSamples; ++sample)
        {
            const auto phase =
                juce::MathConstants<double>::twoPi * target
                * static_cast<double>(sample) / sampleRate;
            const auto value = static_cast<double>(
                buffer.getSample(0, start + sample));
            real += value * std::cos(phase);
            imaginary -= value * std::sin(phase);
        }
        return std::sqrt(real * real + imaginary * imaginary);
    };

    const auto fundamental = juce::jmax(1.0e-9, magnitudeAt(frequency));
    double harmonicEnergy = 0.0;
    for (int harmonic = 2; harmonic <= 8; ++harmonic)
    {
        const auto magnitude =
            magnitudeAt(frequency * static_cast<float>(harmonic));
        harmonicEnergy += magnitude * magnitude;
    }
    return static_cast<float>(std::sqrt(harmonicEnergy) / fundamental);
}

class VelvetDriveTests final : public juce::UnitTest
{
public:
    VelvetDriveTests()
        : juce::UnitTest("Velvet Drive", "megaDSP")
    {
    }

    void runTest() override
    {
        using namespace megadsp;

        beginTest("Default amp is audible, finite, and bounded");
        const auto defaults = moduleDefaults(ModuleType::velvetDrive);
        const auto defaultRender = render(defaults, 375.0f, 0.12f);
        expect(defaultRender.getRMSLevel(0, 24000, 24000) > 0.01f);
        expect(defaultRender.getMagnitude(0, 48000) < 8.0f);
        for (int channel = 0; channel < defaultRender.getNumChannels();
             ++channel)
            for (int sample = 0; sample < defaultRender.getNumSamples();
                 ++sample)
                expect(std::isfinite(
                    defaultRender.getSample(channel, sample)));

        VelvetDriveModule latencyModule;
        latencyModule.prepare({ sampleRate, blockSize, 2 });
        expect(latencyModule.latencySamples() > 0);
        expectEquals(latencyModule.tailSeconds(defaults), 0.0);

        beginTest("High Headroom stays cleaner than Classic");
        auto classic = defaults;
        classic[1] = discreteValue(0, 3);
        classic[2] = 0.76f;
        classic[3] = 0.72f;
        classic[10] = discreteValue(0, 4);
        auto highHeadroom = classic;
        highHeadroom[1] = discreteValue(2, 3);
        const auto classicRender = render(classic, 375.0f, 0.12f);
        const auto headroomRender = render(highHeadroom, 375.0f, 0.12f);
        const auto classicHarmonics =
            harmonicRatio(classicRender, 375.0f);
        const auto headroomHarmonics =
            harmonicRatio(headroomRender, 375.0f);
        expect(
            classicHarmonics > 0.015f,
            "Classic harmonic ratio "
                + juce::String(classicHarmonics, 6));
        expect(
            headroomHarmonics < classicHarmonics * 0.9f,
            "Classic " + juce::String(classicHarmonics, 6)
                + ", High Headroom "
                + juce::String(headroomHarmonics, 6));

        beginTest("Master drives the modeled power stage");
        auto lowMaster = defaults;
        lowMaster[2] = 0.55f;
        lowMaster[3] = 0.55f;
        lowMaster[10] = discreteValue(0, 4);
        lowMaster[11] = 12.0f / 36.0f;
        auto highMaster = lowMaster;
        highMaster[11] = 1.0f;
        const auto lowMasterHarmonics =
            harmonicRatio(render(lowMaster, 375.0f, 0.12f), 375.0f);
        const auto highMasterHarmonics =
            harmonicRatio(render(highMaster, 375.0f, 0.12f), 375.0f);
        expect(
            highMasterHarmonics > lowMasterHarmonics * 1.08f,
            "Low Master " + juce::String(lowMasterHarmonics, 6)
                + ", High Master "
                + juce::String(highMasterHarmonics, 6));

        beginTest("Analytic speaker loads are distinct");
        auto direct = defaults;
        direct[10] = discreteValue(0, 4);
        auto scooped = defaults;
        scooped[10] = discreteValue(2, 4);
        const auto directRender = render(direct, 731.0f, 0.10f);
        const auto scoopedRender = render(scooped, 731.0f, 0.10f);
        double differenceEnergy = 0.0;
        for (int sample = 24000; sample < 48000; ++sample)
        {
            const auto difference = directRender.getSample(0, sample)
                - scoopedRender.getSample(0, sample);
            differenceEnergy +=
                static_cast<double>(difference) * difference;
        }
        expect(std::sqrt(differenceEnergy / 24000.0) > 0.002);

        beginTest("Duplicated mono remains channel matched");
        const auto stereo = render(classic, 223.0f, 0.16f);
        auto maximumDifference = 0.0f;
        for (int sample = 0; sample < stereo.getNumSamples(); ++sample)
            maximumDifference = juce::jmax(
                maximumDifference,
                std::abs(
                    stereo.getSample(0, sample)
                    - stereo.getSample(1, sample)));
        expect(maximumDifference < 0.00001f);

        beginTest("Automation steps remain finite and bounded");
        VelvetDriveModule automated;
        automated.prepare({ sampleRate, 193, 1 });
        juce::AudioBuffer<float> block(1, 193);
        auto controls = defaults;
        for (int blockIndex = 0; blockIndex < 180; ++blockIndex)
        {
            controls[0] = discreteValue(blockIndex % 2, 2);
            controls[1] = discreteValue(blockIndex % 3, 3);
            controls[2] = blockIndex % 2 == 0 ? 0.0f : 1.0f;
            controls[3] = blockIndex % 3 == 0 ? 1.0f : 0.1f;
            controls[9] = blockIndex % 2 == 0 ? 1.0f : 0.0f;
            controls[10] = discreteValue(blockIndex % 4, 4);
            for (int sample = 0; sample < block.getNumSamples(); ++sample)
                block.setSample(
                    0, sample,
                    0.18f * std::sin(
                        juce::MathConstants<float>::twoPi * 317.0f
                        * static_cast<float>(
                            blockIndex * block.getNumSamples() + sample)
                        / static_cast<float>(sampleRate)));
            automated.process(block, controls, {});
            expect(block.getMagnitude(0, block.getNumSamples()) < 8.0f);
            for (int sample = 0; sample < block.getNumSamples(); ++sample)
                expect(std::isfinite(block.getSample(0, sample)));
        }
    }
};

VelvetDriveTests velvetDriveTests;
} // namespace
