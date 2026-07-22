#include "SpatialOrbit.h"
#include "DspHelpers.h"
#include "DspSafety.h"

#include <cmath>

namespace megadsp
{
namespace
{
using detail::exponential;
using detail::lerp;

constexpr std::array<float, 8> divisionBeats {
    16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.75f, 0.25f
};
} // namespace

void SpatialOrbitModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    fixedLatencySamples = juce::jmax(8, juce::roundToInt(sampleRate * 0.035));
    const auto capacity = static_cast<size_t>(fixedLatencySamples + 16);
    for (auto& history : motionHistory)
        history.assign(capacity, 0.0f);
    for (auto& history : dryHistory)
        history.assign(static_cast<size_t>(fixedLatencySamples), 0.0f);
    for (auto* smoother : { &spanSmoothed, &widthSmoothed, &distanceSmoothed,
                            &dopplerSmoothed, &dampingSmoothed, &mixSmoothed })
        smoother->reset(sampleRate, 0.04);
    for (auto* smoother : { &rateSmoothed, &monoBelowSmoothed, &outputSmoothed })
        smoother->reset(sampleRate, 0.04);
    reset();
}

void SpatialOrbitModule::reset()
{
    for (auto& history : motionHistory)
        std::fill(history.begin(), history.end(), 0.0f);
    for (auto& history : dryHistory)
        std::fill(history.begin(), history.end(), 0.0f);
    dampingState.fill(0.0f);
    sideFoundationState = 0.0f;
    randomState = 0x4f1bbcddu;
    motionPhase = 0.0;
    previousTrajectory = 0.0f;
    wanderStart = 0.0f;
    wanderTarget = 0.0f;
    wanderProgress = 1.0f;
    writePosition = 0;
    dryPosition = 0;
    initialized = false;
    telemetryState = {};
    telemetry.clear();
}

float SpatialOrbitModule::readDelay(int channel, float delaySamples) const noexcept
{
    const auto& history = motionHistory[static_cast<size_t>(channel)];
    const auto size = static_cast<int>(history.size());
    auto position = static_cast<float>(writePosition)
                    - juce::jlimit(2.0f, static_cast<float>(size - 3),
                                   delaySamples);
    while (position < 0.0f)
        position += static_cast<float>(size);
    const auto index = static_cast<int>(position);
    const auto fraction = position - static_cast<float>(index);
    const auto at = [&history, size](int positionToRead)
    {
        while (positionToRead < 0)
            positionToRead += size;
        return history[static_cast<size_t>(positionToRead % size)];
    };
    const auto y0 = at(index - 1);
    const auto y1 = at(index);
    const auto y2 = at(index + 1);
    const auto y3 = at(index + 2);
    const auto a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    const auto a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const auto a2 = -0.5f * y0 + 0.5f * y2;
    return ((a0 * fraction + a1) * fraction + a2) * fraction + y1;
}

float SpatialOrbitModule::wanderPosition() noexcept
{
    if (wanderProgress >= 1.0f)
    {
        wanderStart = wanderTarget;
        randomState ^= randomState << 13;
        randomState ^= randomState >> 17;
        randomState ^= randomState << 5;
        wanderTarget = static_cast<float>(randomState & 0x00ffffffu)
                           / static_cast<float>(0x00800000u) - 1.0f;
        wanderProgress = 0.0f;
    }
    const auto shaped = wanderProgress * wanderProgress
                        * (3.0f - 2.0f * wanderProgress);
    return wanderStart + (wanderTarget - wanderStart) * shaped;
}

void SpatialOrbitModule::process(juce::AudioBuffer<float>& buffer,
                                 const ControlValues& controls,
                                 const ProcessEnvironment& environment)
{
    const auto channels = juce::jlimit(0, 2, buffer.getNumChannels());
    if (channels == 0 || motionHistory[0].empty())
        return;

    const auto path = discreteIndex(
        detail::normalizedControl(controls[0], 0.0f), 4);
    auto rate = exponential(
        0.02f, 5.0f, detail::normalizedControl(controls[1], 0.27f));
    if (detail::normalizedControl(controls[2], 0.0f) >= 0.5f)
    {
        const auto division = discreteIndex(
            detail::normalizedControl(controls[3], 0.67f),
            static_cast<int>(divisionBeats.size()));
        const auto bpm = std::isfinite(environment.bpm) && environment.bpm > 1.0
            ? environment.bpm : 120.0;
        rate = static_cast<float>(
            bpm / (60.0 * divisionBeats[static_cast<size_t>(division)]));
    }
    const auto span = detail::normalizedControl(controls[4], 0.5f);
    const auto width = detail::normalizedControl(controls[5], 0.5f) * 2.0f;
    const auto distance = exponential(0.5f, 10.0f,
        detail::normalizedControl(controls[6], 0.46f));
    const auto doppler = detail::normalizedControl(controls[7], 0.2f);
    const auto damping = detail::normalizedControl(controls[8], 0.4f);
    const auto monoBelow = exponential(20.0f, 500.0f,
        detail::normalizedControl(controls[9], 0.56f));
    const auto mix = detail::normalizedControl(controls[10], 1.0f);
    const auto output = juce::Decibels::decibelsToGain(
        lerp(-18.0f, 12.0f,
             detail::normalizedControl(controls[11], 0.6f)));

    if (!initialized)
    {
        rateSmoothed.setCurrentAndTargetValue(rate);
        spanSmoothed.setCurrentAndTargetValue(span);
        widthSmoothed.setCurrentAndTargetValue(width);
        distanceSmoothed.setCurrentAndTargetValue(distance);
        dopplerSmoothed.setCurrentAndTargetValue(doppler);
        dampingSmoothed.setCurrentAndTargetValue(damping);
        monoBelowSmoothed.setCurrentAndTargetValue(monoBelow);
        mixSmoothed.setCurrentAndTargetValue(mix);
        outputSmoothed.setCurrentAndTargetValue(output);
        initialized = true;
    }
    rateSmoothed.setTargetValue(rate);
    spanSmoothed.setTargetValue(span);
    widthSmoothed.setTargetValue(width);
    distanceSmoothed.setTargetValue(distance);
    dopplerSmoothed.setTargetValue(doppler);
    dampingSmoothed.setTargetValue(damping);
    monoBelowSmoothed.setTargetValue(monoBelow);
    mixSmoothed.setTargetValue(mix);
    outputSmoothed.setTargetValue(output);

    float telemetryX = 0.0f;
    float telemetryY = 0.0f;
    float telemetryDistance = distance;
    float telemetryTrajectory = 0.0f;
    float telemetryRadial = 0.0f;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto dryLeft =
            detail::finiteSample(buffer.getSample(0, sample));
        const auto dryRight = channels > 1
            ? detail::finiteSample(buffer.getSample(1, sample)) : dryLeft;
        const auto inputMid = 0.5f * (dryLeft + dryRight);
        const auto inputSide = channels > 1 ? 0.5f * (dryLeft - dryRight) : 0.0f;
        motionHistory[0][static_cast<size_t>(writePosition)] = inputMid;
        motionHistory[1][static_cast<size_t>(writePosition)] = inputSide;

        const auto currentRate = rateSmoothed.getNextValue();
        motionPhase += static_cast<double>(currentRate) / sampleRate;
        motionPhase -= std::floor(motionPhase);
        const auto phase = static_cast<float>(
            juce::MathConstants<double>::twoPi * motionPhase);
        float trajectory = 0.0f;
        float radial = 0.0f;
        if (path == 0)
        {
            trajectory = std::sin(phase);
            radial = 0.5f - 0.5f * std::cos(phase);
        }
        else if (path == 1)
        {
            trajectory = std::sin(phase * 2.0f);
            radial = 0.5f + 0.5f * std::cos(phase);
        }
        else if (path == 2)
        {
            trajectory = std::sin(phase);
            radial = 0.5f + 0.25f * std::cos(phase);
        }
        else
        {
            wanderProgress += currentRate
                              / static_cast<float>(sampleRate * 0.65);
            trajectory = wanderPosition();
            radial = 0.5f + 0.25f * std::sin(phase * 0.73f);
        }

        const auto currentSpan = spanSmoothed.getNextValue();
        const auto azimuth = trajectory * currentSpan
                             * juce::MathConstants<float>::pi;
        const auto pan = std::sin(azimuth);
        const auto currentDistance = distanceSmoothed.getNextValue()
            * (0.88f + radial * 0.24f);
        telemetryX = pan;
        telemetryY = 1.0f - 2.0f * radial;
        telemetryDistance = currentDistance;
        telemetryTrajectory = trajectory;
        telemetryRadial = radial;
        const auto velocity = juce::jlimit(
            -0.08f, 0.08f, trajectory - previousTrajectory);
        previousTrajectory = trajectory;
        const auto delayExcursion = velocity
            * dopplerSmoothed.getNextValue()
            * static_cast<float>(sampleRate) * 0.020f;
        const auto delay = juce::jlimit(
            3.0f, static_cast<float>(fixedLatencySamples + 12),
            static_cast<float>(fixedLatencySamples) + delayExcursion);
        auto wetMid = readDelay(0, delay);
        auto wetSide = readDelay(1, delay);

        const auto currentDamping = dampingSmoothed.getNextValue();
        const auto airCutoff = juce::jlimit(
            800.0f, static_cast<float>(sampleRate * 0.45),
            20000.0f / (1.0f + currentDamping * currentDistance * 0.42f));
        const auto airCoefficient = 1.0f - std::exp(
            -juce::MathConstants<float>::twoPi * airCutoff
            / static_cast<float>(sampleRate));
        dampingState[0] += airCoefficient * (wetMid - dampingState[0]);
        dampingState[1] += airCoefficient * (wetSide - dampingState[1]);
        wetMid = dampingState[0];
        wetSide = dampingState[1];

        const auto foundationCoefficient = 1.0f - std::exp(
            -juce::MathConstants<float>::twoPi
            * monoBelowSmoothed.getNextValue()
            / static_cast<float>(sampleRate));
        const auto leftTrajectoryGain = std::sqrt(
            0.5f * juce::jmax(0.0f, 1.0f - pan));
        const auto rightTrajectoryGain = std::sqrt(
            0.5f * juce::jmax(0.0f, 1.0f + pan));
        const auto trajectoryMidGain =
            0.5f * (leftTrajectoryGain + rightTrajectoryGain);
        const auto trajectorySideGain =
            0.5f * (leftTrajectoryGain - rightTrajectoryGain);
        const auto combinedSide = wetSide * widthSmoothed.getNextValue()
                                  + wetMid * trajectorySideGain;
        sideFoundationState += foundationCoefficient
                               * (combinedSide - sideFoundationState);
        const auto focusedSide = combinedSide - sideFoundationState;
        const auto distanceGain = juce::jlimit(
            0.18f, 1.35f, std::sqrt(2.0f / currentDistance));
        wetMid *= distanceGain * trajectoryMidGain;
        const auto finalSide = focusedSide * distanceGain;
        const auto wetLeft = wetMid + finalSide;
        const auto wetRight = wetMid - finalSide;

        const auto delayedLeft =
            dryHistory[0][static_cast<size_t>(dryPosition)];
        const auto delayedRight =
            dryHistory[1][static_cast<size_t>(dryPosition)];
        dryHistory[0][static_cast<size_t>(dryPosition)] = dryLeft;
        dryHistory[1][static_cast<size_t>(dryPosition)] = dryRight;
        const auto currentMix = mixSmoothed.getNextValue();
        const auto currentOutput = outputSmoothed.getNextValue();
        buffer.setSample(
            0, sample,
            detail::finiteSample(
                (delayedLeft + (wetLeft - delayedLeft) * currentMix)
                * currentOutput));
        if (channels > 1)
            buffer.setSample(
                1, sample,
                detail::finiteSample(
                    (delayedRight + (wetRight - delayedRight) * currentMix)
                    * currentOutput));

        writePosition = (writePosition + 1)
                        % static_cast<int>(motionHistory[0].size());
        dryPosition = (dryPosition + 1) % fixedLatencySamples;
    }

    if (environment.captureTelemetry)
    {
        ++telemetryState.sequence;
        telemetryState.valueCount = telemetryValueCount;
        telemetryState.values[xPosition] = telemetryX;
        telemetryState.values[yPosition] = telemetryY;
        telemetryState.values[distanceMetres] = telemetryDistance;
        telemetryState.values[activePath] = static_cast<float>(path);
        telemetryState.values[pathPhase] = static_cast<float>(motionPhase);
        telemetryState.values[trajectoryPosition] = telemetryTrajectory;
        telemetryState.values[radialPosition] = telemetryRadial;
        telemetryState.historyValueCount = telemetryHistoryValueCount;
        const auto position = telemetryState.historyWritePosition;
        telemetryState.history[xHistory][position] = telemetryX;
        telemetryState.history[yHistory][position] = telemetryY;
        telemetryState.history[distanceHistory][position] = telemetryDistance;
        telemetryState.historyCount = juce::jmin<std::uint32_t>(
            static_cast<std::uint32_t>(
                continuousTelemetryHistoryCapacity),
            telemetryState.historyCount + 1);
        telemetryState.historyWritePosition =
            (position + 1)
            % static_cast<std::uint32_t>(
                continuousTelemetryHistoryCapacity);
        telemetry.publish(telemetryState);
    }
}

bool SpatialOrbitModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}

double SpatialOrbitModule::tailSeconds(const ControlValues&) const
{
    return static_cast<double>(fixedLatencySamples) / sampleRate;
}
} // namespace megadsp
