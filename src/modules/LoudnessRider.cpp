#include "LoudnessRider.h"
#include "DspHelpers.h"
#include "DspSafety.h"

#include <cmath>

namespace megadsp
{
namespace
{
using detail::exponential;
using detail::lerp;

float decibels(float gain, float floor = -100.0f) noexcept
{
    return juce::Decibels::gainToDecibels(
        juce::jmax(0.0f, gain), floor);
}
} // namespace

void LoudnessRiderModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    fixedLatencySamples = juce::jmax(
        1, static_cast<int>(std::ceil(sampleRate * 0.250)));
    for (auto& delay : audioDelay)
        delay.assign(static_cast<size_t>(fixedLatencySamples + 1), 0.0f);
    gainDelay.assign(static_cast<size_t>(fixedLatencySamples + 1), 1.0f);
    telemetryInterval = juce::jmax(1, juce::roundToInt(sampleRate * 0.5));
    reset();
}

void LoudnessRiderModule::reset()
{
    for (auto& delay : audioDelay)
        std::fill(delay.begin(), delay.end(), 0.0f);
    std::fill(gainDelay.begin(), gainDelay.end(), 1.0f);
    highPassState.fill(0.0f);
    previousInput.fill(0.0f);
    shelfState.fill(0.0f);
    loudnessPower = 0.0;
    fastEnvelope = 0.0f;
    rideGainDb = 0.0f;
    requestedGainDb = 0.0f;
    outputGain = 1.0f;
    currentLoudness = -100.0f;
    currentCrest = 0.0f;
    writePosition = 0;
    telemetryCountdown = telemetryInterval;
    telemetryPointDue = false;
    rideMeterDb.store(0.0f);
    loudnessMeter.store(-100.0f);
    telemetryState = {};
    telemetry.clear();
}

void LoudnessRiderModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    const auto channels = juce::jlimit(0, 2, buffer.getNumChannels());
    if (channels == 0 || audioDelay[0].empty())
        return;
    const auto normalized = [&controls](int index, float fallback)
    {
        return detail::normalizedControl(
            controls[static_cast<size_t>(index)], fallback);
    };

    const auto targetLufs = lerp(
        -36.0f, -8.0f, normalized(targetControl, 0.57f));
    const auto rangeDb = normalized(rangeControl, 0.5f) * 18.0f;
    const auto windowSeconds = exponential(
        0.4f, 5.0f, normalized(windowControl, 0.45f));
    const auto reactionSeconds = exponential(
        0.25f, 10.0f, normalized(reactionControl, 0.35f));
    const auto lookaheadMilliseconds =
        normalized(lookaheadControl, 0.4f) * 250.0f;
    const auto lookaheadSamples = juce::jlimit(
        0, fixedLatencySamples,
        juce::roundToInt(
            lookaheadMilliseconds * 0.001f * static_cast<float>(sampleRate)));
    const auto transientHold = normalized(transientHoldControl, 0.5f);
    const auto crestPreserve = normalized(crestPreserveControl, 0.5f);
    const auto gateLufs = lerp(
        -70.0f, -30.0f, normalized(gateControl, 0.35f));
    const auto outputTarget = juce::Decibels::decibelsToGain(
        lerp(-18.0f, 12.0f, normalized(outputControl, 0.6f)));

    const auto highPassPole = std::exp(
        -juce::MathConstants<float>::twoPi * 60.0f
        / static_cast<float>(sampleRate));
    const auto shelfCoefficient = 1.0f - std::exp(
        -juce::MathConstants<float>::twoPi * 1500.0f
        / static_cast<float>(sampleRate));
    const auto windowCoefficient = std::exp(
        -1.0 / (sampleRate * static_cast<double>(windowSeconds)));
    const auto fastCoefficient = std::exp(
        -1.0f / (0.010f * static_cast<float>(sampleRate)));
    const auto outputCoefficient = std::exp(
        -1.0f / (0.025f * static_cast<float>(sampleRate)));
    const auto downwardCoefficient = std::exp(
        -1.0f / (static_cast<float>(sampleRate)
                 * juce::jmax(0.05f, reactionSeconds * 0.55f)));
    const auto upwardCoefficient = std::exp(
        -1.0f / (static_cast<float>(sampleRate)
                 * juce::jmax(0.05f, reactionSeconds)));
    bool gated = currentLoudness < gateLufs;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        float weightedPower = 0.0f;
        float inputPeakValue = 0.0f;
        std::array<float, 2> input {};
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            input[index] = detail::finiteSample(
                buffer.getSample(channel, sample));
            inputPeakValue = juce::jmax(
                inputPeakValue, std::abs(input[index]));
            const auto highPassed = input[index] - previousInput[index]
                                    + highPassPole * highPassState[index];
            previousInput[index] = input[index];
            highPassState[index] = highPassed;
            shelfState[index] += shelfCoefficient
                                 * (highPassed - shelfState[index]);
            const auto weighted =
                highPassed + 0.58f * (highPassed - shelfState[index]);
            weightedPower += weighted * weighted;
        }
        weightedPower /= static_cast<float>(channels);
        loudnessPower = windowCoefficient * loudnessPower
            + (1.0 - windowCoefficient) * static_cast<double>(weightedPower);
        fastEnvelope = fastCoefficient * fastEnvelope
            + (1.0f - fastCoefficient) * inputPeakValue;
        currentLoudness = loudnessPower > 1.0e-12
            ? -0.691f + 10.0f
                * std::log10(static_cast<float>(loudnessPower))
            : -100.0f;
        const auto rms = std::sqrt(
            juce::jmax(1.0e-12f, static_cast<float>(loudnessPower)));
        currentCrest = juce::jlimit(
            0.0f, 30.0f, decibels(fastEnvelope / rms, 0.0f));
        gated = currentLoudness < gateLufs || weightedPower < 1.0e-12f;

        if (!gated)
        {
            auto desiredGain = juce::jlimit(
                -rangeDb, rangeDb, targetLufs - currentLoudness);
            const auto crestFactor = juce::jlimit(
                0.0f, 1.0f, (currentCrest - 5.0f) / 15.0f);
            desiredGain *= 1.0f - crestPreserve * crestFactor * 0.75f;
            const auto risingTransient = fastEnvelope
                > rms * juce::Decibels::decibelsToGain(7.0f);
            if (risingTransient && transientHold > 0.001f)
            {
                const auto hold = transientHold
                    * juce::jlimit(0.0f, 1.0f, (currentCrest - 6.0f) / 12.0f);
                desiredGain = lerp(desiredGain, rideGainDb, hold);
            }
            requestedGainDb = desiredGain;
        }
        else
            requestedGainDb = rideGainDb;

        const auto rideCoefficient = requestedGainDb < rideGainDb
            ? downwardCoefficient : upwardCoefficient;
        rideGainDb = rideCoefficient * rideGainDb
                     + (1.0f - rideCoefficient) * requestedGainDb;
        rideGainDb = juce::jlimit(-rangeDb, rangeDb, rideGainDb);
        const auto currentRideGain =
            juce::Decibels::decibelsToGain(rideGainDb);
        gainDelay[static_cast<size_t>(writePosition)] = currentRideGain;

        auto gainReadPosition = writePosition
            - (fixedLatencySamples - lookaheadSamples);
        while (gainReadPosition < 0)
            gainReadPosition += static_cast<int>(gainDelay.size());
        const auto delayedGain =
            gainDelay[static_cast<size_t>(gainReadPosition)];
        outputGain = outputCoefficient * outputGain
                     + (1.0f - outputCoefficient) * outputTarget;

        for (int channel = 0; channel < channels; ++channel)
        {
            auto& delay = audioDelay[static_cast<size_t>(channel)];
            auto readPosition = writePosition - fixedLatencySamples;
            while (readPosition < 0)
                readPosition += static_cast<int>(delay.size());
            const auto delayed = delay[static_cast<size_t>(readPosition)];
            delay[static_cast<size_t>(writePosition)] =
                input[static_cast<size_t>(channel)];
            buffer.setSample(
                channel, sample,
                detail::finiteSample(delayed * delayedGain * outputGain));
        }

        writePosition = (writePosition + 1)
            % static_cast<int>(gainDelay.size());
        if (--telemetryCountdown <= 0)
        {
            telemetryCountdown = telemetryInterval;
            telemetryPointDue = true;
        }
    }

    rideMeterDb.store(rideGainDb, std::memory_order_relaxed);
    loudnessMeter.store(currentLoudness, std::memory_order_relaxed);
    if (environment.captureTelemetry)
    {
        telemetryState.sequence += 1;
        telemetryState.valueCount = telemetryValueCount;
        telemetryState.values[momentaryLoudness] = currentLoudness;
        telemetryState.values[targetLoudness] = targetLufs;
        telemetryState.values[gateLoudness] = gateLufs;
        telemetryState.values[rideGainDecibels] = rideGainDb;
        telemetryState.values[requestedGainDecibels] = requestedGainDb;
        telemetryState.values[crestDecibels] = currentCrest;
        telemetryState.values[gatedState] = gated ? 1.0f : 0.0f;
        telemetryState.values[activeLookaheadMilliseconds] =
            lookaheadMilliseconds;
        if (telemetryPointDue)
        {
            appendContinuousTelemetryHistory(
                telemetryState,
                { currentLoudness, targetLufs, gateLufs, rideGainDb },
                telemetryHistoryValueCount);
            telemetryPointDue = false;
        }
        telemetry.publish(telemetryState);
    }
}

bool LoudnessRiderModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}
} // namespace megadsp
