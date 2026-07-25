#include "DspModules.h"
#include "modules/ModuleRegistry.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <limits>

namespace
{
class SpectralNebulaTests final : public juce::UnitTest
{
public:
    SpectralNebulaTests() : UnitTest("Spectral Nebula") {}

    void runTest() override
    {
        using namespace megadsp;
        constexpr double rate = 48000.0;
        const ProcessEnvironment capture { nullptr, 120.0, true };
        auto controls = moduleDefaults(ModuleType::spectralNebula);

        beginTest("Cloud zero is exact immediate dry");
        SpectralNebulaModule dry;
        dry.prepare({ rate, 4096, 1 });
        auto dryControls = controls;
        dryControls[SpectralNebulaModule::cloudControl] = 0.0f;
        juce::AudioBuffer<float> impulse(1, 4096);
        impulse.clear();
        impulse.setSample(0, 0, 0.5f);
        dry.process(impulse, dryControls, {});
        expectWithinAbsoluteError(
            impulse.getSample(0, 0), 0.5f, 0.000001f);
        for (int sample = 1; sample < impulse.getNumSamples(); ++sample)
            expectWithinAbsoluteError(
                impulse.getSample(0, sample), 0.0f, 0.000001f);

        beginTest("A short input produces a long evolving cloud");
        auto cloudControls = controls;
        cloudControls[SpectralNebulaModule::dryControl] = 0.0f;
        cloudControls[SpectralNebulaModule::cloudControl] = 1.0f;
        cloudControls[SpectralNebulaModule::decayControl] = 0.75f;
        SpectralNebulaModule cloud;
        cloud.prepare({ rate, 144000, 2 });
        auto burst = makeBurst(2, 144000, rate);
        cloud.process(burst, cloudControls, capture);
        expectFinite(burst);
        expect(rms(burst, 80000, 120000) > 0.0002f);
        ContinuousTelemetrySnapshot state;
        expect(cloud.readContinuousTelemetry(state));
        expect(state.values[SpectralNebulaModule::cloudEnergy] > 0.0f);

        beginTest("Freeze retains cloud energy without new input");
        SpectralNebulaModule frozen;
        frozen.prepare({ rate, 48000, 2 });
        auto seed = makeBurst(2, 48000, rate);
        frozen.process(seed, cloudControls, {});
        auto freezeControls = cloudControls;
        freezeControls[SpectralNebulaModule::freezeControl] = 1.0f;
        juce::AudioBuffer<float> held(2, 96000);
        held.clear();
        frozen.process(held, freezeControls, capture);
        const auto earlyHeld = rms(held, 24000, 40000);
        const auto lateHeld = rms(held, 72000, 92000);
        expect(earlyHeld > 0.0002f);
        expect(lateHeld > earlyHeld * 0.55f);

        beginTest("Duplicated mono remains centred at maximum width");
        auto wideControls = cloudControls;
        wideControls[SpectralNebulaModule::widthControl] = 1.0f;
        SpectralNebulaModule wide;
        wide.prepare({ rate, 48000, 2 });
        auto centred = makeBurst(2, 48000, rate);
        wide.process(centred, wideControls, {});
        auto difference = 0.0f;
        for (int sample = 0; sample < centred.getNumSamples(); ++sample)
            difference = juce::jmax(
                difference,
                std::abs(centred.getSample(0, sample)
                         - centred.getSample(1, sample)));
        expect(difference < 0.000001f);
        expectFinite(centred);

        beginTest("Width scales stereo cloud side energy");
        auto narrowControls = cloudControls;
        narrowControls[SpectralNebulaModule::widthControl] = 0.0f;
        auto expandedControls = cloudControls;
        expandedControls[SpectralNebulaModule::widthControl] = 1.0f;
        SpectralNebulaModule narrow;
        SpectralNebulaModule expanded;
        narrow.prepare({ rate, 48000, 2 });
        expanded.prepare({ rate, 48000, 2 });
        auto narrowStereo = makeAsymmetricBurst(48000, rate);
        auto expandedStereo = makeAsymmetricBurst(48000, rate);
        narrow.process(narrowStereo, narrowControls, {});
        expanded.process(expandedStereo, expandedControls, {});
        expect(sideRms(narrowStereo, 12000, 46000) < 0.000001f);
        expect(sideRms(expandedStereo, 12000, 46000)
               > sideRms(narrowStereo, 12000, 46000) + 0.0001f);
        expectFinite(expandedStereo);

        beginTest("Off-bin notes resynthesize without frame-rate jackhammer");
        auto smoothControls = controls;
        smoothControls[SpectralNebulaModule::dryControl] = 0.0f;
        smoothControls[SpectralNebulaModule::cloudControl] = 1.0f;
        smoothControls[SpectralNebulaModule::driftControl] = 0.0f;
        smoothControls[SpectralNebulaModule::shimmerControl] = 0.0f;
        SpectralNebulaModule smooth;
        smooth.prepare({ rate, 48000, 1 });
        auto sustained = makeSustainedTone(48000, rate, 223.7f);
        smooth.process(sustained, smoothControls, {});
        const auto ripple = envelopeRipple(sustained, 12000, 44000, 256);
        expect(ripple < 0.45f,
               "Sustained cloud amplitude was modulated by STFT frames");
        expectFinite(sustained);

        beginTest("New notes replace unsupported old-note energy");
        SpectralNebulaModule replacing;
        replacing.prepare({ rate, 96000, 1 });
        auto changing = makeNoteChange(
            96000, rate, 223.7f, 337.1f, 24000);
        replacing.process(changing, smoothControls, {});
        const auto oldNote = toneAmplitude(
            changing, 60000, 84000, rate, 223.7f);
        const auto newNote = toneAmplitude(
            changing, 60000, 84000, rate, 337.1f);
        expect(newNote > oldNote * 2.5f,
               "Old notes accumulated instead of morphing into the new note");

        beginTest("Shimmer adds a restrained tonal octave");
        auto noShimmerControls = smoothControls;
        noShimmerControls[SpectralNebulaModule::shimmerControl] = 0.0f;
        auto shimmerControls = smoothControls;
        shimmerControls[SpectralNebulaModule::shimmerControl] = 1.0f;
        SpectralNebulaModule noShimmer;
        SpectralNebulaModule withShimmer;
        noShimmer.prepare({ rate, 48000, 1 });
        withShimmer.prepare({ rate, 48000, 1 });
        auto plainTone = makeSustainedTone(48000, rate, 223.7f);
        auto brightTone = makeSustainedTone(48000, rate, 223.7f);
        noShimmer.process(plainTone, noShimmerControls, {});
        withShimmer.process(brightTone, shimmerControls, {});
        const auto addedOctave =
            toneAmplitude(brightTone, 16000, 44000, rate, 447.4f)
            - toneAmplitude(plainTone, 16000, 44000, rate, 447.4f);
        const auto addedUnrelated =
            toneAmplitude(brightTone, 16000, 44000, rate, 335.55f)
            - toneAmplitude(plainTone, 16000, 44000, rate, 335.55f);
        expect(addedOctave > 0.0005f);
        expect(addedOctave > juce::jmax(0.0f, addedUnrelated) * 2.0f);
        expectFinite(brightTone);

        beginTest("Reset restores deterministic drift and cloud state");
        SpectralNebulaModule deterministic;
        deterministic.prepare({ rate, 48000, 2 });
        auto first = makeBurst(2, 48000, rate);
        deterministic.process(first, controls, {});
        deterministic.reset();
        auto second = makeBurst(2, 48000, rate);
        deterministic.process(second, controls, {});
        expect(maxDifference(first, second) < 0.000001f);
    }

private:
    static juce::AudioBuffer<float> makeBurst(
        int channels, int samples, double rate)
    {
        juce::AudioBuffer<float> result(channels, samples);
        result.clear();
        const auto sounding = juce::jmin(
            samples, juce::roundToInt(rate * 0.28));
        for (int sample = 0; sample < sounding; ++sample)
        {
            const auto time = static_cast<float>(sample / rate);
            const auto envelope =
                juce::jmin(1.0f, time / 0.004f) * std::exp(-time / 0.22f);
            const auto value = envelope * 0.13f
                * (std::sin(
                       juce::MathConstants<float>::twoPi * 196.0f * time)
                   + 0.7f * std::sin(
                       juce::MathConstants<float>::twoPi * 246.94f * time)
                   + 0.5f * std::sin(
                       juce::MathConstants<float>::twoPi * 293.66f * time));
            for (int channel = 0; channel < channels; ++channel)
                result.setSample(channel, sample, value);
        }
        return result;
    }

    static juce::AudioBuffer<float> makeAsymmetricBurst(
        int samples, double rate)
    {
        auto result = makeBurst(2, samples, rate);
        for (int sample = 0; sample < samples; ++sample)
            result.setSample(
                1, sample,
                result.getSample(1, sample) * 0.35f
                    + 0.04f * std::sin(
                        juce::MathConstants<float>::twoPi * 391.0f
                        * static_cast<float>(sample / rate)));
        return result;
    }

    static juce::AudioBuffer<float> makeSustainedTone(
        int samples, double rate, float frequency)
    {
        juce::AudioBuffer<float> result(1, samples);
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto time = static_cast<float>(sample / rate);
            const auto fade = juce::jmin(1.0f, time / 0.01f);
            result.setSample(
                0, sample,
                0.2f * fade * std::sin(
                    juce::MathConstants<float>::twoPi * frequency * time));
        }
        return result;
    }

    static juce::AudioBuffer<float> makeNoteChange(
        int samples, double rate, float firstFrequency,
        float secondFrequency, int changeSample)
    {
        juce::AudioBuffer<float> result(1, samples);
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto frequency =
                sample < changeSample ? firstFrequency : secondFrequency;
            const auto localSample =
                sample < changeSample ? sample : sample - changeSample;
            const auto fade = juce::jmin(
                1.0f, static_cast<float>(localSample / rate) / 0.01f);
            result.setSample(
                0, sample,
                0.2f * fade * std::sin(
                    juce::MathConstants<float>::twoPi * frequency
                    * static_cast<float>(sample / rate)));
        }
        return result;
    }

    static float toneAmplitude(
        const juce::AudioBuffer<float>& buffer, int start, int end,
        double rate, float frequency)
    {
        double real = 0.0;
        double imaginary = 0.0;
        for (int sample = start; sample < end; ++sample)
        {
            const auto phase = juce::MathConstants<double>::twoPi
                * static_cast<double>(frequency)
                * static_cast<double>(sample) / rate;
            const auto value = static_cast<double>(
                buffer.getSample(0, sample));
            real += value * std::cos(phase);
            imaginary -= value * std::sin(phase);
        }
        return static_cast<float>(
            2.0 * std::sqrt(real * real + imaginary * imaginary)
            / static_cast<double>(juce::jmax(1, end - start)));
    }

    static float envelopeRipple(
        const juce::AudioBuffer<float>& buffer, int start, int end,
        int window)
    {
        auto minimum = std::numeric_limits<float>::max();
        auto maximum = 0.0f;
        for (int position = start; position + window <= end;
             position += window)
        {
            const auto level = rms(buffer, position, position + window);
            minimum = juce::jmin(minimum, level);
            maximum = juce::jmax(maximum, level);
        }
        return maximum > 1.0e-6f ? (maximum - minimum) / maximum : 1.0f;
    }

    static float sideRms(
        const juce::AudioBuffer<float>& buffer, int start, int end)
    {
        double energy = 0.0;
        for (int sample = start; sample < end; ++sample)
        {
            const auto side = 0.5f
                * (buffer.getSample(0, sample)
                   - buffer.getSample(1, sample));
            energy += static_cast<double>(side) * side;
        }
        return static_cast<float>(
            std::sqrt(energy / static_cast<double>(juce::jmax(1, end - start))));
    }

    static float rms(
        const juce::AudioBuffer<float>& buffer, int start, int end)
    {
        double energy = 0.0;
        int count = 0;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = start; sample < end; ++sample)
            {
                const auto value = buffer.getSample(channel, sample);
                energy += static_cast<double>(value) * value;
                ++count;
            }
        return static_cast<float>(
            std::sqrt(energy / static_cast<double>(juce::jmax(1, count))));
    }

    static float maxDifference(
        const juce::AudioBuffer<float>& first,
        const juce::AudioBuffer<float>& second)
    {
        auto maximum = 0.0f;
        for (int channel = 0; channel < first.getNumChannels(); ++channel)
            for (int sample = 0; sample < first.getNumSamples(); ++sample)
                maximum = juce::jmax(
                    maximum,
                    std::abs(first.getSample(channel, sample)
                             - second.getSample(channel, sample)));
        return maximum;
    }

    void expectFinite(const juce::AudioBuffer<float>& buffer)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                expect(std::isfinite(buffer.getSample(channel, sample)));
    }
};

SpectralNebulaTests spectralNebulaTests;
} // namespace
