#include "modules/Bitcrusher.h"
#include "modules/ModuleRegistry.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <set>

namespace
{
class BitcrusherTests final : public juce::UnitTest
{
public:
    BitcrusherTests() : juce::UnitTest("Bitcrusher", "megaDSP") {}

    void runTest() override
    {
        using namespace megadsp;

        beginTest("Bitcrusher appends the stable module contract");
        expectEquals(static_cast<int>(ModuleType::wormholeConvolution), 55);
        expectEquals(static_cast<int>(ModuleType::bitcrusher), 56);
        expectEquals(moduleTypeCount, 59);
        expect(moduleDefinition(ModuleType::bitcrusher).category
               == ModuleCategory::saturationAndColor);
        expect(moduleDefinition(ModuleType::guitarCabinet).category
               == ModuleCategory::guitar);

        beginTest("Mix zero is exact immediate dry");
        BitcrusherModule dryModule;
        dryModule.prepare({ 48000.0, 512, 2 });
        auto dryControls = moduleDefaults(ModuleType::bitcrusher);
        dryControls[10] = 0.0f;
        juce::AudioBuffer<float> dry(2, 512);
        for (int sample = 0; sample < dry.getNumSamples(); ++sample)
        {
            const auto left = 0.65f * std::sin(
                juce::MathConstants<float>::twoPi * 997.0f
                * static_cast<float>(sample) / 48000.0f);
            dry.setSample(0, sample, left);
            dry.setSample(1, sample, left * -0.37f);
        }
        const auto reference = dry;
        dryModule.process(dry, dryControls, {});
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < dry.getNumSamples(); ++sample)
                expectWithinAbsoluteError(
                    dry.getSample(channel, sample),
                    reference.getSample(channel, sample), 1.0e-7f);

        beginTest("Two-bit balanced mode uses three bounded levels");
        BitcrusherModule lowBitsModule;
        lowBitsModule.prepare({ 48000.0, 512, 2 });
        auto lowBits = moduleDefaults(ModuleType::bitcrusher);
        lowBits[0] = 0.0f;
        lowBits[1] = 1.0f;
        lowBits[2] = discreteValue(0, 4);
        lowBits[3] = 0.0f;
        lowBits[4] = 0.0f;
        lowBits[5] = 0.0f;
        lowBits[6] = 0.0f;
        lowBits[7] = 0.0f;
        lowBits[8] = 0.5f;
        lowBits[9] = 0.0f;
        lowBits[10] = 1.0f;
        juce::AudioBuffer<float> quantized(2, 512);
        for (int sample = 0; sample < quantized.getNumSamples(); ++sample)
        {
            const auto value = -1.0f
                + 2.0f * static_cast<float>(sample)
                           / static_cast<float>(quantized.getNumSamples() - 1);
            quantized.setSample(0, sample, value);
            quantized.setSample(1, sample, -value);
        }
        lowBitsModule.process(quantized, lowBits, {});
        std::set<int> levels;
        for (int sample = 0; sample < quantized.getNumSamples(); ++sample)
        {
            const auto value = quantized.getSample(0, sample);
            expect(std::isfinite(value));
            expect(std::abs(value) <= 1.0001f);
            levels.insert(juce::roundToInt(value * 1000.0f));
        }
        juce::String levelSummary;
        for (const auto level : levels)
            levelSummary += juce::String(level) + " ";
        expect(levels.size() <= 3, "Observed levels: " + levelSummary);

        beginTest("Low sample rate produces a true held clock");
        BitcrusherModule heldModule;
        heldModule.prepare({ 48000.0, 2048, 1 });
        auto heldControls = lowBits;
        heldControls[0] = 1.0f;
        heldControls[1] = 0.0f;
        juce::AudioBuffer<float> held(1, 2048);
        for (int sample = 0; sample < held.getNumSamples(); ++sample)
            held.setSample(
                0, sample, sample % 2 == 0 ? -0.8f : 0.8f);
        heldModule.process(held, heldControls, {});
        auto transitions = 0;
        for (int sample = 1; sample < held.getNumSamples(); ++sample)
            if (std::abs(held.getSample(0, sample)
                         - held.getSample(0, sample - 1)) > 1.0e-7f)
                ++transitions;
        expect(transitions < 20);

        beginTest("Maximum sample rate follows high-rate hosts");
        BitcrusherModule hostRateModule;
        hostRateModule.prepare({ 192000.0, 512, 1 });
        auto hostRateControls = lowBits;
        hostRateControls[0] = 1.0f;
        hostRateControls[1] = 1.0f;
        juce::AudioBuffer<float> hostRate(1, 512);
        for (int sample = 0; sample < hostRate.getNumSamples(); ++sample)
            hostRate.setSample(
                0, sample, sample % 2 == 0 ? -0.8f : 0.8f);
        hostRateModule.process(hostRate, hostRateControls, {});
        transitions = 0;
        for (int sample = 1; sample < hostRate.getNumSamples(); ++sample)
            if (std::abs(hostRate.getSample(0, sample)
                         - hostRate.getSample(0, sample - 1)) > 1.0e-7f)
                ++transitions;
        expect(transitions > 500);

        beginTest("Dither and jitter reset deterministically");
        auto texturedControls = moduleDefaults(ModuleType::bitcrusher);
        texturedControls[0] = 0.12f;
        texturedControls[1] = 0.20f;
        texturedControls[3] = 0.9f;
        texturedControls[4] = 0.7f;
        texturedControls[6] = 0.8f;
        juce::AudioBuffer<float> input(2, 1024);
        for (int sample = 0; sample < input.getNumSamples(); ++sample)
        {
            input.setSample(0, sample, 0.23f);
            input.setSample(1, sample, -0.17f);
        }
        BitcrusherModule deterministic;
        deterministic.prepare({ 48000.0, 1024, 2 });
        auto first = input;
        deterministic.process(first, texturedControls, {});
        deterministic.reset();
        auto second = input;
        deterministic.process(second, texturedControls, {});
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < input.getNumSamples(); ++sample)
                expectEquals(first.getSample(channel, sample),
                             second.getSample(channel, sample));

        beginTest("Hot asymmetric stereo remains finite and distinct");
        BitcrusherModule hotModule;
        hotModule.prepare({ 192000.0, 1024, 2 });
        auto hotControls = texturedControls;
        hotControls[2] = discreteValue(3, 4);
        hotControls[8] = 1.0f;
        hotControls[9] = 1.0f;
        juce::AudioBuffer<float> hot(2, 1024);
        hot.getWritePointer(0)[0] = 1000.0f;
        hot.getWritePointer(1)[0] = -700.0f;
        for (int sample = 1; sample < hot.getNumSamples(); ++sample)
        {
            hot.setSample(0, sample, 2.0f);
            hot.setSample(1, sample, -0.35f);
        }
        hotModule.process(hot, hotControls, {});
        auto difference = 0.0f;
        for (int sample = 0; sample < hot.getNumSamples(); ++sample)
        {
            expect(std::isfinite(hot.getSample(0, sample)));
            expect(std::isfinite(hot.getSample(1, sample)));
            difference = juce::jmax(
                difference,
                std::abs(hot.getSample(0, sample)
                         - hot.getSample(1, sample)));
        }
        expect(difference > 0.01f);
    }
};

BitcrusherTests bitcrusherTests;
} // namespace
