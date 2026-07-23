#include "modules/SpectralDelayCanvas.h"
#include "modules/TimeMosaic.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <limits>

namespace
{
constexpr double sampleRate = 48000.0;

template <typename Module>
void renderInBlocks(
    Module& module, juce::AudioBuffer<float>& buffer,
    const megadsp::ControlValues& controls,
    const megadsp::ProcessEnvironment& environment = {})
{
    constexpr int blockSize = 256;
    for (int start = 0; start < buffer.getNumSamples(); start += blockSize)
    {
        const auto count = juce::jmin(
            blockSize, buffer.getNumSamples() - start);
        juce::AudioBuffer<float> block(
            buffer.getArrayOfWritePointers(), buffer.getNumChannels(),
            start, count);
        module.process(block, controls, environment);
    }
}

class SpectralHistoryPairTests final : public juce::UnitTest
{
public:
    SpectralHistoryPairTests()
        : juce::UnitTest("Spectral history pair", "megaDSP")
    {
    }

    void runTest() override
    {
        const juce::dsp::ProcessSpec spec { sampleRate, 256, 2 };

        beginTest("Both processors report exact latency and aligned dry");
        megadsp::SpectralDelayCanvasModule delay;
        megadsp::TimeMosaicModule mosaic;
        delay.prepare(spec);
        mosaic.prepare(spec);
        expectEquals(
            delay.latencySamples(),
            megadsp::FixedLatencyStft::reportedLatencySamples);
        expectEquals(mosaic.latencySamples(), delay.latencySamples());
        expectAlignedDry(delay, 10, 11);
        expectAlignedDry(mosaic, 9, 10);

        beginTest("Reset makes deterministic motion repeat exactly");
        megadsp::ControlValues mosaicControls {};
        mosaicControls[0] = 0.35f;
        mosaicControls[1] = 0.4f;
        mosaicControls[2] = 0.1f;
        mosaicControls[3] = 0.4f;
        mosaicControls[4] = 0.9f;
        mosaicControls[5] = 0.55f;
        mosaicControls[6] = 0.65f;
        mosaicControls[8] = 0.7f;
        mosaicControls[9] = 1.0f;
        mosaicControls[10] = 0.6f;
        auto first = makeSignal(2, 48000);
        auto second = first;
        mosaic.reset();
        renderInBlocks(mosaic, first, mosaicControls);
        mosaic.reset();
        renderInBlocks(mosaic, second, mosaicControls);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < first.getNumSamples(); ++sample)
                expectEquals(
                    first.getSample(channel, sample),
                    second.getSample(channel, sample));

        beginTest("Freeze retains a bounded audible spectral reservoir");
        megadsp::ControlValues delayControls {};
        delayControls[1] = 0.4f;
        delayControls[3] = 0.0f;
        delayControls[4] = 0.0f;
        delayControls[5] = 0.0f;
        delayControls[6] = 0.7f;
        delayControls[7] = 0.4f;
        delayControls[10] = 1.0f;
        delayControls[11] = 0.6f;
        delay.reset();
        auto source = makeSignal(1, 24000);
        renderInBlocks(delay, source, delayControls);
        delayControls[9] = 1.0f;
        juce::AudioBuffer<float> frozen(1, 12000);
        frozen.clear();
        renderInBlocks(delay, frozen, delayControls);
        expect(frozen.getRMSLevel(0, 4096, 7904) > 1.0e-5f);
        expect(delay.tailSeconds(delayControls) <= 30.0);

        beginTest("Telemetry publication is capture gated");
        megadsp::ContinuousTelemetrySnapshot continuous;
        auto telemetryInput = makeSignal(1, 4096);
        delay.reset();
        renderInBlocks(delay, telemetryInput, delayControls);
        expect(delay.readContinuousTelemetry(continuous));
        expectEquals(static_cast<int>(continuous.sequence), 0);
        telemetryInput = makeSignal(1, 4096);
        renderInBlocks(
            delay, telemetryInput, delayControls, { nullptr, 120.0, true });
        expect(delay.readContinuousTelemetry(continuous));
        expect(continuous.sequence > 0);
        const auto capturedSequence = continuous.sequence;
        telemetryInput = makeSignal(1, 4096);
        renderInBlocks(delay, telemetryInput, delayControls);
        expect(delay.readContinuousTelemetry(continuous));
        expect(continuous.sequence == capturedSequence);
        megadsp::EventTelemetrySnapshot events;
        telemetryInput = makeSignal(1, 4096);
        mosaic.reset();
        renderInBlocks(
            mosaic, telemetryInput, mosaicControls,
            { nullptr, 120.0, true });
        expect(mosaic.readEventTelemetry(events));
        expect(events.sequence > 0);

        beginTest("Non-finite input cannot escape either processor");
        juce::AudioBuffer<float> invalid(2, 8192);
        invalid.clear();
        invalid.setSample(
            0, 0, std::numeric_limits<float>::quiet_NaN());
        invalid.setSample(
            1, 1, std::numeric_limits<float>::infinity());
        mosaic.reset();
        renderInBlocks(mosaic, invalid, mosaicControls);
        for (int channel = 0; channel < invalid.getNumChannels(); ++channel)
            for (int sample = 0; sample < invalid.getNumSamples(); ++sample)
                expect(std::isfinite(invalid.getSample(channel, sample)));
    }

private:
    static juce::AudioBuffer<float> makeSignal(int channels, int samples)
    {
        juce::AudioBuffer<float> result(channels, samples);
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto time = static_cast<float>(sample / sampleRate);
            const auto value =
                0.17f * std::sin(
                    juce::MathConstants<float>::twoPi * 173.0f * time)
                + 0.09f * std::sin(
                    juce::MathConstants<float>::twoPi * 1601.0f * time)
                + 0.04f * std::sin(
                    juce::MathConstants<float>::twoPi * 7103.0f * time);
            for (int channel = 0; channel < channels; ++channel)
                result.setSample(
                    channel, sample,
                    value * (channel == 0 ? 1.0f : -0.55f));
        }
        return result;
    }

    template <typename Module>
    void expectAlignedDry(Module& module, int mixIndex, int outputIndex)
    {
        auto buffer = makeSignal(2, 8192);
        auto reference = buffer;
        megadsp::ControlValues controls {};
        controls[static_cast<std::size_t>(mixIndex)] = 0.0f;
        controls[static_cast<std::size_t>(outputIndex)] = 0.6f;
        module.reset();
        renderInBlocks(module, buffer, controls);
        const auto latency = module.latencySamples();
        expect(buffer.getMagnitude(0, 0, latency) < 1.0e-7f);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = latency; sample < buffer.getNumSamples(); ++sample)
                expectWithinAbsoluteError(
                    buffer.getSample(channel, sample),
                    reference.getSample(channel, sample - latency),
                    1.0e-6f);
    }
};

SpectralHistoryPairTests spectralHistoryPairTests;
} // namespace
