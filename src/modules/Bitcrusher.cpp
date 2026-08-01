#include "Bitcrusher.h"

#include "DspHelpers.h"
#include "DspSafety.h"

#include <cmath>

namespace megadsp
{
namespace
{
using detail::exponential;
using detail::lerp;
}

void BitcrusherModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    bitsSmoothed.reset(sampleRate, 0.025);
    rateSmoothed.reset(sampleRate, 0.025);
    for (auto& smoother : linearSmoothed)
        smoother.reset(sampleRate, 0.025);
    outputSmoothed.reset(sampleRate, 0.025);
    reset();
}

void BitcrusherModule::reset()
{
    antiAliasState = {};
    heldSample.fill(0.0f);
    slewState.fill(0.0f);
    errorState.fill(0.0f);
    randomState = { 0x9e3779b9u, 0x243f6a88u };
    samplesUntilUpdate = 0.0f;
    initialized = false;
}

float BitcrusherModule::randomUnit(int channel) noexcept
{
    auto& state = randomState[static_cast<size_t>(channel)];
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(state & 0x00ffffffu)
           / static_cast<float>(0x01000000u);
}

float BitcrusherModule::quantize(
    float input, float step, float mode) noexcept
{
    const auto scaled = input / step;
    const std::array<float, 4> values {
        std::round(scaled) * step,
        std::trunc(scaled) * step,
        std::floor(scaled) * step,
        [&]
        {
            constexpr auto mu = 31.0f;
            const auto compressed = std::copysign(
                std::log1p(mu * std::abs(input)) / std::log1p(mu), input);
            const auto quantized = std::round(compressed / step) * step;
            return std::copysign(
                std::expm1(std::abs(quantized) * std::log1p(mu)) / mu,
                quantized);
        }()
    };
    const auto lower = juce::jlimit(0, 3, static_cast<int>(mode));
    const auto upper = juce::jmin(3, lower + 1);
    return lerp(values[static_cast<size_t>(lower)],
                values[static_cast<size_t>(upper)],
                mode - static_cast<float>(lower));
}

void BitcrusherModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment&)
{
    const auto channels = juce::jlimit(0, 2, buffer.getNumChannels());
    if (channels == 0)
        return;

    const auto bits = lerp(
        2.0f, 24.0f, detail::normalizedControl(controls[0], 0.45f));
    const auto normalizedRate =
        detail::normalizedControl(controls[1], 0.482f);
    const auto rate = juce::jmin(
        static_cast<float>(sampleRate),
        exponential(250.0f, 768000.0f, normalizedRate));
    const std::array<float, 9> linearTargets {
        static_cast<float>(discreteIndex(
            detail::normalizedControl(controls[2], 0.125f), 4)),
        detail::normalizedControl(controls[3], 0.2f),
        detail::normalizedControl(controls[4], 0.0f),
        detail::normalizedControl(controls[5], 0.65f),
        detail::normalizedControl(controls[6], 0.0f),
        detail::normalizedControl(controls[7], 0.0f),
        detail::normalizedControl(controls[8], 0.5f) * 2.0f - 1.0f,
        detail::normalizedControl(controls[9], 0.0f),
        detail::normalizedControl(controls[10], 1.0f)
    };
    const auto output = juce::Decibels::decibelsToGain(
        lerp(-18.0f, 12.0f,
             detail::normalizedControl(controls[11], 0.6f)));

    if (!initialized)
    {
        bitsSmoothed.setCurrentAndTargetValue(bits);
        rateSmoothed.setCurrentAndTargetValue(rate);
        for (size_t index = 0; index < linearSmoothed.size(); ++index)
            linearSmoothed[index].setCurrentAndTargetValue(
                linearTargets[index]);
        outputSmoothed.setCurrentAndTargetValue(output);
        initialized = true;
    }
    bitsSmoothed.setTargetValue(bits);
    rateSmoothed.setTargetValue(rate);
    for (size_t index = 0; index < linearSmoothed.size(); ++index)
        linearSmoothed[index].setTargetValue(linearTargets[index]);
    outputSmoothed.setTargetValue(output);

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto currentBits = bitsSmoothed.getNextValue();
        const auto currentRate = rateSmoothed.getNextValue();
        const auto mode = linearSmoothed[0].getNextValue();
        const auto dither = linearSmoothed[1].getNextValue();
        const auto noiseShape = linearSmoothed[2].getNextValue();
        const auto antiAlias = linearSmoothed[3].getNextValue();
        const auto jitter = linearSmoothed[4].getNextValue();
        const auto slew = linearSmoothed[5].getNextValue();
        const auto bias = linearSmoothed[6].getNextValue();
        const auto drive = linearSmoothed[7].getNextValue();
        const auto mix = linearSmoothed[8].getNextValue();
        const auto outputGain = outputSmoothed.getNextValue();
        const auto driveGain =
            juce::Decibels::decibelsToGain(drive * 24.0f);
        const auto driveDenominator = std::tanh(driveGain);
        const auto levels = juce::jmax(1.0f, std::exp2(currentBits - 1.0f) - 1.0f);
        const auto step = 1.0f / levels;
        const auto cutoff = juce::jlimit(
            20.0f, static_cast<float>(sampleRate * 0.45),
            currentRate * 0.45f);
        const auto filterCoefficient = 1.0f - std::exp(
            -juce::MathConstants<float>::twoPi * cutoff
            / static_cast<float>(sampleRate));

        std::array<float, 2> dry {};
        std::array<float, 2> source {};
        for (int channel = 0; channel < channels; ++channel)
        {
            dry[static_cast<size_t>(channel)] =
                detail::finiteSample(buffer.getSample(channel, sample));
            const auto saturated = std::tanh(
                dry[static_cast<size_t>(channel)] * driveGain)
                / driveDenominator;
            const auto driven = lerp(
                dry[static_cast<size_t>(channel)], saturated, drive);
            auto& filter = antiAliasState[static_cast<size_t>(channel)];
            filter[0] += filterCoefficient * (driven - filter[0]);
            filter[1] += filterCoefficient * (filter[0] - filter[1]);
            source[static_cast<size_t>(channel)] =
                lerp(driven, filter[1], antiAlias);
        }

        samplesUntilUpdate -= 1.0f;
        if (samplesUntilUpdate <= 0.0f)
        {
            const auto interval =
                static_cast<float>(sampleRate) / currentRate;
            const auto randomClock = randomUnit(0) * 2.0f - 1.0f;
            samplesUntilUpdate += juce::jmax(
                1.0f, interval * (1.0f + randomClock * jitter * 0.45f));
            for (int channel = 0; channel < channels; ++channel)
            {
                const auto tpdf = randomUnit(channel) - randomUnit(channel);
                const auto shaped = source[static_cast<size_t>(channel)]
                    + errorState[static_cast<size_t>(channel)]
                          * noiseShape * 0.85f
                    + tpdf * dither * step * 0.5f
                    + bias * step;
                const auto quantized = quantize(
                    juce::jlimit(-1.0f, 1.0f, shaped), step, mode)
                    - bias * step;
                heldSample[static_cast<size_t>(channel)] =
                    juce::jlimit(-1.25f, 1.25f, quantized);
                errorState[static_cast<size_t>(channel)] = juce::jlimit(
                    -2.0f * step, 2.0f * step,
                    source[static_cast<size_t>(channel)]
                        - heldSample[static_cast<size_t>(channel)]);
            }
        }

        const auto holdSamples =
            static_cast<float>(sampleRate) / currentRate;
        const auto slewCoefficient = slew <= 0.0001f
            ? 1.0f
            : 1.0f - std::exp(
                -1.0f / juce::jmax(1.0f, holdSamples * slew * slew * 2.0f));
        for (int channel = 0; channel < channels; ++channel)
        {
            auto& smoothed = slewState[static_cast<size_t>(channel)];
            smoothed += slewCoefficient
                * (heldSample[static_cast<size_t>(channel)] - smoothed);
            const auto result = (lerp(
                dry[static_cast<size_t>(channel)], smoothed, mix))
                * outputGain;
            buffer.setSample(
                channel, sample, std::isfinite(result) ? result : 0.0f);
        }
    }
}
} // namespace megadsp
