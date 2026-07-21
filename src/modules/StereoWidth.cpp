#include "StereoWidth.h"
#include "DspHelpers.h"

#include <cmath>

namespace megadsp
{
using detail::exponential;
using detail::lerp;

void StereoWidthModule::AllpassStage::prepare(int delaySamples,
                                               float newCoefficient)
{
    buffer.assign(static_cast<size_t>(juce::jmax(1, delaySamples)), 0.0f);
    coefficient = newCoefficient;
    position = 0;
}

void StereoWidthModule::AllpassStage::reset()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    position = 0;
}

float StereoWidthModule::AllpassStage::process(float input)
{
    if (buffer.empty())
        return input;
    const auto delayed = buffer[static_cast<size_t>(position)];
    const auto output = delayed - coefficient * input;
    buffer[static_cast<size_t>(position)] = input + coefficient * output;
    if (++position >= static_cast<int>(buffer.size()))
        position = 0;
    return output;
}

void StereoWidthModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    constexpr std::array<float, 4> delayMilliseconds {
        1.37f, 2.81f, 4.93f, 7.91f
    };
    constexpr std::array<float, 4> coefficients {
        0.58f, -0.53f, 0.47f, -0.41f
    };
    for (int stage = 0; stage < static_cast<int>(decorrelator.size()); ++stage)
        decorrelator[static_cast<size_t>(stage)].prepare(
            juce::roundToInt(delayMilliseconds[static_cast<size_t>(stage)]
                             * 0.001 * sampleRate),
            coefficients[static_cast<size_t>(stage)]);
    for (auto* smoother : {
             &widthSmoothed, &dimensionSmoothed, &monoCoefficientSmoothed,
             &focusCoefficientSmoothed, &balanceSmoothed, &mixSmoothed,
             &outputSmoothed })
        smoother->reset(sampleRate, 0.05);
    reset();
}

void StereoWidthModule::reset()
{
    for (auto& stage : decorrelator)
        stage.reset();
    sideLowState = 0.0f;
    dimensionLowState = 0.0f;
    midEnergy = 0.0f;
    sideEnergy = 0.0f;
    safeGain = 1.0f;
    widthSmoothed.setCurrentAndTargetValue(1.0f);
    dimensionSmoothed.setCurrentAndTargetValue(0.0f);
    monoCoefficientSmoothed.setCurrentAndTargetValue(0.0f);
    focusCoefficientSmoothed.setCurrentAndTargetValue(0.0f);
    balanceSmoothed.setCurrentAndTargetValue(0.5f);
    mixSmoothed.setCurrentAndTargetValue(1.0f);
    outputSmoothed.setCurrentAndTargetValue(1.0f);
}

void StereoWidthModule::process(juce::AudioBuffer<float>& buffer,
                                const ControlValues& controls,
                                const ProcessEnvironment&)
{
    const auto monoFrequency = exponential(20.0f, 500.0f, controls[2]);
    const auto focusFrequency = exponential(500.0f, 8000.0f, controls[3]);
    widthSmoothed.setTargetValue(controls[0] * 2.0f);
    dimensionSmoothed.setTargetValue(controls[1]);
    monoCoefficientSmoothed.setTargetValue(std::exp(
        -juce::MathConstants<float>::twoPi * monoFrequency
        / static_cast<float>(sampleRate)));
    focusCoefficientSmoothed.setTargetValue(std::exp(
        -juce::MathConstants<float>::twoPi * focusFrequency
        / static_cast<float>(sampleRate)));
    balanceSmoothed.setTargetValue(controls[4]);
    mixSmoothed.setTargetValue(controls[5]);
    outputSmoothed.setTargetValue(juce::Decibels::decibelsToGain(
        lerp(-12.0f, 12.0f, controls[6])));
    const auto monoSafe = controls[7] >= 0.5f;
    const auto energyCoefficient = std::exp(
        -1.0f / static_cast<float>(sampleRate * 0.08));

    if (buffer.getNumChannels() < 2)
    {
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            widthSmoothed.getNextValue();
            dimensionSmoothed.getNextValue();
            monoCoefficientSmoothed.getNextValue();
            focusCoefficientSmoothed.getNextValue();
            balanceSmoothed.getNextValue();
            mixSmoothed.getNextValue();
            buffer.setSample(0, sample, buffer.getSample(0, sample)
                                         * outputSmoothed.getNextValue());
        }
        return;
    }

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto dryLeft = buffer.getSample(0, sample);
        const auto dryRight = buffer.getSample(1, sample);
        const auto balance = balanceSmoothed.getNextValue();
        const auto balanceAngle =
            balance * juce::MathConstants<float>::halfPi;
        const auto balancedLeft = dryLeft * std::cos(balanceAngle)
                                  * juce::MathConstants<float>::sqrt2;
        const auto balancedRight = dryRight * std::sin(balanceAngle)
                                   * juce::MathConstants<float>::sqrt2;
        const auto mid = (balancedLeft + balancedRight) * 0.5f;
        const auto side = (balancedLeft - balancedRight) * 0.5f;
        const auto width = widthSmoothed.getNextValue();
        const auto dimension = dimensionSmoothed.getNextValue();
        const auto monoCoefficient = monoCoefficientSmoothed.getNextValue();
        const auto focusCoefficient = focusCoefficientSmoothed.getNextValue();

        sideLowState = monoCoefficient * sideLowState
                       + (1.0f - monoCoefficient) * side;
        const auto sideHigh = side - sideLowState;
        auto widenedSide = sideHigh * width
                           + sideLowState * width * 0.10f;

        dimensionLowState = focusCoefficient * dimensionLowState
                            + (1.0f - focusCoefficient) * mid;
        auto decorrelated = mid - dimensionLowState;
        for (auto& stage : decorrelator)
            decorrelated = stage.process(decorrelated);
        widenedSide += decorrelated * dimension * 0.45f;

        midEnergy = energyCoefficient * midEnergy
                    + (1.0f - energyCoefficient) * mid * mid;
        sideEnergy = energyCoefficient * sideEnergy
                     + (1.0f - energyCoefficient) * widenedSide * widenedSide;
        auto targetSafeGain = 1.0f;
        if (monoSafe && (width > 1.0f || dimension > 0.001f)
            && sideEnergy > midEnergy * 0.96f * 0.96f)
            targetSafeGain = std::sqrt(
                (midEnergy * 0.96f * 0.96f + 1.0e-12f)
                / (sideEnergy + 1.0e-12f));
        const auto safetyRate = targetSafeGain < safeGain ? 0.02f : 0.001f;
        safeGain += (targetSafeGain - safeGain) * safetyRate;
        widenedSide *= safeGain;

        const auto wetLeft = mid + widenedSide;
        const auto wetRight = mid - widenedSide;
        const auto mix = mixSmoothed.getNextValue();
        const auto output = outputSmoothed.getNextValue();
        buffer.setSample(0, sample,
                         (balancedLeft + (wetLeft - balancedLeft) * mix)
                             * output);
        buffer.setSample(1, sample,
                         (balancedRight + (wetRight - balancedRight) * mix)
                             * output);
    }
}
} // namespace megadsp
