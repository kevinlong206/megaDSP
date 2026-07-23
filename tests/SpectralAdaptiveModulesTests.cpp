#include "modules/ResonanceTamer.h"
#include "modules/SpectralBalance.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace
{
constexpr double sampleRate = 48000.0;

megadsp::ControlValues unityOutputControls()
{
    megadsp::ControlValues controls {};
    controls[8] = 0.6f;
    return controls;
}

class SpectralAdaptiveModulesTests final : public juce::UnitTest
{
public:
    SpectralAdaptiveModulesTests()
        : juce::UnitTest("Spectral adaptive modules", "megaDSP")
    {
    }

    void runTest() override
    {
        const juce::dsp::ProcessSpec spec { sampleRate, 512, 2 };

        beginTest("Fixed spectral history is bounded and chronological");
        megadsp::FixedSpectralHistory history;
        history.prepare(2, 3);
        const float first[] { 1.0f, 2.0f, 3.0f };
        const float second[] { 4.0f, 5.0f, 6.0f };
        const float third[] { 7.0f, 8.0f, 9.0f };
        history.push(first, 3);
        history.push(second, 3);
        history.push(third, 3);
        expectEquals(static_cast<int>(history.size()), 2);
        expectEquals(history.frame(0)[0], 7.0f);
        expectEquals(history.frame(1)[0], 4.0f);
        expect(history.frame(2) == nullptr);

        beginTest("Resonance Tamer dry path has exact fixed alignment");
        megadsp::ResonanceTamerModule tamer;
        tamer.prepare(spec);
        auto tamerControls = unityOutputControls();
        tamerControls[7] = 0.0f;
        expectEquals(tamer.latencySamples(),
                     megadsp::FixedLatencyStft::reportedLatencySamples);
        expectAlignedDry(tamer, tamerControls);

        beginTest("Spectral Balance Amount zero is transparent and aligned");
        megadsp::SpectralBalanceModule balance;
        balance.prepare(spec);
        auto balanceControls = unityOutputControls();
        balanceControls[1] = 0.0f;
        expectEquals(balance.latencySamples(), tamer.latencySamples());
        expectAlignedDry(balance, balanceControls);

        beginTest("Narrow stable energy produces bounded linked reduction");
        tamer.reset();
        tamerControls.fill(0.0f);
        tamerControls[0] = 1.0f;
        tamerControls[1] = 1.0f;
        tamerControls[2] = 1.0f;
        tamerControls[3] = 0.5f;
        tamerControls[4] = 0.0f;
        tamerControls[5] = 1.0f;
        tamerControls[6] = 0.0f;
        tamerControls[7] = 1.0f;
        tamerControls[8] = 0.6f;
        juce::AudioBuffer<float> tone(2, 48000);
        for (int sample = 0; sample < tone.getNumSamples(); ++sample)
        {
            const auto value = 0.2f * std::sin(
                juce::MathConstants<float>::twoPi * 2000.0f
                * static_cast<float>(sample)
                / static_cast<float>(sampleRate));
            tone.setSample(0, sample, value);
            tone.setSample(1, sample, 0.5f * value);
        }
        tamer.process(tone, tamerControls, { nullptr, 120.0, true });
        megadsp::ContinuousTelemetrySnapshot tamerTelemetry;
        expect(tamer.readContinuousTelemetry(tamerTelemetry));
        const auto reduction = tamerTelemetry.values[
            megadsp::ResonanceTamerModule::actualReductionDb];
        expect(reduction > 0.1f);
        expect(reduction <= 18.0f);
        expectWithinAbsoluteError(
            tone.getRMSLevel(1, 24000, 24000)
                / tone.getRMSLevel(0, 24000, 24000),
            0.5f, 0.01f);

        beginTest("Spectral Balance correction stays finite and bounded");
        balance.reset();
        balanceControls = unityOutputControls();
        balanceControls[0] = 1.0f;
        balanceControls[1] = 1.0f;
        balanceControls[2] = 0.5f;
        balanceControls[3] = 0.5f;
        balanceControls[4] = 0.5f;
        balanceControls[5] = 0.0f;
        balanceControls[6] = 0.5f;
        balanceControls[7] = 0.0f;
        juce::AudioBuffer<float> tilted(1, 48000);
        for (int sample = 0; sample < tilted.getNumSamples(); ++sample)
        {
            const auto time = static_cast<float>(sample / sampleRate);
            tilted.setSample(
                0, sample,
                0.18f * std::sin(
                            juce::MathConstants<float>::twoPi * 180.0f * time)
                    + 0.025f * std::sin(
                                  juce::MathConstants<float>::twoPi
                                  * 6000.0f * time));
        }
        balance.process(
            tilted, balanceControls, { nullptr, 120.0, true });
        megadsp::ContinuousTelemetrySnapshot balanceTelemetry;
        expect(balance.readContinuousTelemetry(balanceTelemetry));
        expect(balanceTelemetry.values[
                   megadsp::SpectralBalanceModule::maximumCorrectionDb]
               <= 9.0001f);
        for (int sample = 0; sample < tilted.getNumSamples(); ++sample)
            expect(std::isfinite(tilted.getSample(0, sample)));
    }

private:
    void expectAlignedDry(
        megadsp::DspModule& module,
        const megadsp::ControlValues& controls)
    {
        constexpr int samples = 8192;
        juce::AudioBuffer<float> buffer(2, samples);
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto value = 0.3f * std::sin(
                juce::MathConstants<float>::twoPi * 731.0f
                * static_cast<float>(sample)
                / static_cast<float>(sampleRate));
            buffer.setSample(0, sample, value);
            buffer.setSample(1, sample, -0.4f * value);
        }
        auto reference = buffer;
        module.process(buffer, controls, {});
        const auto latency = module.latencySamples();
        expect(buffer.getMagnitude(0, 0, latency) < 1.0e-7f);
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = latency; sample < samples; ++sample)
                expectWithinAbsoluteError(
                    buffer.getSample(channel, sample),
                    reference.getSample(channel, sample - latency),
                    1.0e-6f);
    }
};

SpectralAdaptiveModulesTests spectralAdaptiveModulesTests;
} // namespace
