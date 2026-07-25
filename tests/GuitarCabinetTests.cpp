#include "DspModules.h"

#include <juce_core/juce_core.h>

#include <cmath>

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

        beginTest("Reports one fixed oversampling latency");
        GuitarCabinetModule latencyModule;
        latencyModule.prepare(stereoSpec);
        const auto fixedLatency = latencyModule.latencySamples();
        expect(fixedLatency > 0);
        auto controls = moduleDefaults(ModuleType::guitarCabinet);
        juce::AudioBuffer<float> block(2, blockSize);
        ProcessEnvironment environment;
        for (int speaker = 0; speaker < 4; ++speaker)
            for (int cabinet = 0; cabinet < 4; ++cabinet)
                for (int microphone = 0; microphone < 3; ++microphone)
                {
                    controls[0] = discreteValue(speaker, 4);
                    controls[1] = discreteValue(cabinet, 4);
                    controls[2] = discreteValue(microphone, 3);
                    block.clear();
                    latencyModule.process(block, controls, environment);
                    expectEquals(latencyModule.latencySamples(), fixedLatency);
                }

        beginTest("Hot input remains finite and bounded");
        GuitarCabinetModule boundedModule;
        boundedModule.prepare(stereoSpec);
        controls = moduleDefaults(ModuleType::guitarCabinet);
        controls[5] = 1.0f;
        controls[6] = 1.0f;
        controls[7] = 1.0f;
        controls[9] = 1.0f;
        auto maximum = 0.0f;
        for (int pass = 0; pass < 160; ++pass)
        {
            for (int sample = 0; sample < blockSize; ++sample)
            {
                const auto phase = static_cast<float>(pass * blockSize + sample);
                const auto signal = 1.4f * std::sin(
                    juce::MathConstants<float>::twoPi * 173.0f
                    * phase / 48000.0f);
                block.setSample(0, sample, signal);
                block.setSample(1, sample, signal * 0.71f);
            }
            boundedModule.process(block, controls, environment);
            for (int channel = 0; channel < block.getNumChannels(); ++channel)
                for (int sample = 0; sample < blockSize; ++sample)
                {
                    const auto value = block.getSample(channel, sample);
                    expect(std::isfinite(value));
                    maximum = juce::jmax(maximum, std::abs(value));
                }
        }
        expect(maximum < 16.0f);

        beginTest("Generic model families produce distinct responses");
        GuitarCabinetModule firstModel;
        GuitarCabinetModule secondModel;
        firstModel.prepare(stereoSpec);
        secondModel.prepare(stereoSpec);
        auto firstControls = moduleDefaults(ModuleType::guitarCabinet);
        auto secondControls = firstControls;
        firstControls[0] = discreteValue(0, 4);
        firstControls[1] = discreteValue(0, 4);
        firstControls[2] = discreteValue(0, 3);
        secondControls[0] = discreteValue(3, 4);
        secondControls[1] = discreteValue(3, 4);
        secondControls[2] = discreteValue(2, 3);
        juce::AudioBuffer<float> first(2, blockSize);
        juce::AudioBuffer<float> second(2, blockSize);
        double difference = 0.0;
        for (int pass = 0; pass < 80; ++pass)
        {
            for (int sample = 0; sample < blockSize; ++sample)
            {
                const auto absolute = pass * blockSize + sample;
                const auto source = absolute == 0
                    ? 1.0f
                    : 0.25f * std::sin(
                          juce::MathConstants<float>::twoPi * 997.0f
                          * static_cast<float>(absolute) / 48000.0f);
                first.setSample(0, sample, source);
                first.setSample(1, sample, source);
                second.setSample(0, sample, source);
                second.setSample(1, sample, source);
            }
            firstModel.process(first, firstControls, environment);
            secondModel.process(second, secondControls, environment);
            for (int sample = 0; sample < blockSize; ++sample)
                difference += std::abs(
                    first.getSample(0, sample)
                    - second.getSample(0, sample));
        }
        expect(difference > 0.1);

        beginTest("Telemetry is capture gated and reports measured activity");
        GuitarCabinetModule telemetryModule;
        telemetryModule.prepare(stereoSpec);
        controls = moduleDefaults(ModuleType::guitarCabinet);
        ContinuousTelemetrySnapshot before;
        expect(telemetryModule.readContinuousTelemetry(before));
        block.clear();
        block.setSample(0, 0, 1.0f);
        block.setSample(1, 0, 1.0f);
        telemetryModule.process(block, controls, environment);
        ContinuousTelemetrySnapshot uncaptured;
        expect(telemetryModule.readContinuousTelemetry(uncaptured));
        expectEquals(
            static_cast<int>(uncaptured.sequence),
            static_cast<int>(before.sequence));
        environment.captureTelemetry = true;
        for (int pass = 0; pass < 8; ++pass)
        {
            for (int sample = 0; sample < blockSize; ++sample)
            {
                const auto source = 0.7f * std::sin(
                    juce::MathConstants<float>::twoPi * 220.0f
                    * static_cast<float>(pass * blockSize + sample)
                    / 48000.0f);
                block.setSample(0, sample, source);
                block.setSample(1, sample, source);
            }
            telemetryModule.process(block, controls, environment);
        }
        ContinuousTelemetrySnapshot captured;
        expect(telemetryModule.readContinuousTelemetry(captured));
        expect(
            captured.sequence > uncaptured.sequence);
        expectEquals(
            static_cast<int>(captured.valueCount),
            static_cast<int>(GuitarCabinetModule::telemetryValueCount));

        beginTest("Mono processing and room-zero tail are supported");
        GuitarCabinetModule monoModule;
        monoModule.prepare({ 44100.0, blockSize, 1 });
        auto monoControls = moduleDefaults(ModuleType::guitarCabinet);
        monoControls[9] = 0.0f;
        juce::AudioBuffer<float> mono(1, blockSize);
        mono.clear();
        mono.setSample(0, 0, 1.0f);
        monoModule.process(mono, monoControls, {});
        for (int sample = 0; sample < blockSize; ++sample)
            expect(std::isfinite(mono.getSample(0, sample)));
        const auto dryRoomTail = monoModule.tailSeconds(monoControls);
        monoControls[9] = 1.0f;
        expect(monoModule.tailSeconds(monoControls) >= dryRoomTail);

        beginTest("Room width is neutral in mono and cancels on fold-down");
        auto narrowControls = moduleDefaults(ModuleType::guitarCabinet);
        narrowControls[9] = 0.8f;
        narrowControls[10] = 0.0f;
        auto wideControls = narrowControls;
        wideControls[10] = 1.0f;
        GuitarCabinetModule narrowMono;
        GuitarCabinetModule wideMono;
        narrowMono.prepare({ 48000.0, blockSize, 1 });
        wideMono.prepare({ 48000.0, blockSize, 1 });
        GuitarCabinetModule narrowStereo;
        GuitarCabinetModule wideStereo;
        narrowStereo.prepare(stereoSpec);
        wideStereo.prepare(stereoSpec);
        juce::AudioBuffer<float> monoNarrow(1, blockSize);
        juce::AudioBuffer<float> monoWide(1, blockSize);
        juce::AudioBuffer<float> stereoNarrow(2, blockSize);
        juce::AudioBuffer<float> stereoWide(2, blockSize);
        auto monoDifference = 0.0;
        auto foldDifference = 0.0;
        for (int pass = 0; pass < 40; ++pass)
        {
            for (int sample = 0; sample < blockSize; ++sample)
            {
                const auto source = 0.45f * std::sin(
                    juce::MathConstants<float>::twoPi * 311.0f
                    * static_cast<float>(pass * blockSize + sample)
                    / 48000.0f);
                monoNarrow.setSample(0, sample, source);
                monoWide.setSample(0, sample, source);
                stereoNarrow.setSample(0, sample, source);
                stereoNarrow.setSample(1, sample, source);
                stereoWide.setSample(0, sample, source);
                stereoWide.setSample(1, sample, source);
            }
            narrowMono.process(monoNarrow, narrowControls, {});
            wideMono.process(monoWide, wideControls, {});
            narrowStereo.process(stereoNarrow, narrowControls, {});
            wideStereo.process(stereoWide, wideControls, {});
            for (int sample = 0; sample < blockSize; ++sample)
            {
                monoDifference += std::abs(
                    monoNarrow.getSample(0, sample)
                    - monoWide.getSample(0, sample));
                const auto narrowFold = 0.5f
                    * (stereoNarrow.getSample(0, sample)
                       + stereoNarrow.getSample(1, sample));
                const auto wideFold = 0.5f
                    * (stereoWide.getSample(0, sample)
                       + stereoWide.getSample(1, sample));
                foldDifference += std::abs(narrowFold - wideFold);
            }
        }
        expect(monoDifference < 0.0001);
        expect(foldDifference < 0.0001);
    }
};

GuitarCabinetTests guitarCabinetTests;
} // namespace
