#include "VinylSimulator.h"

#include "DspHelpers.h"
#include "DspSafety.h"

#include <cmath>

namespace megadsp
{
namespace
{
constexpr float minimumNoiseDb = -90.0f;
}

void VinylSimulatorModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    fixedLatencySamples =
        juce::jmax(8, juce::roundToInt(sampleRate * 0.012));
    const auto transportCapacity = static_cast<size_t>(
        fixedLatencySamples + juce::roundToInt(sampleRate * 0.006) + 16);
    for (auto& history : transportHistory)
        history.assign(transportCapacity, 0.0f);
    for (auto& history : dryHistory)
        history.assign(static_cast<size_t>(fixedLatencySamples), 0.0f);
    for (auto& smoother : smoothed)
        smoother.reset(sampleRate, 0.04);
    reset();
}

void VinylSimulatorModule::reset()
{
    for (auto& history : transportHistory)
        std::fill(history.begin(), history.end(), 0.0f);
    for (auto& history : dryHistory)
        std::fill(history.begin(), history.end(), 0.0f);
    crackleVoices = {};
    toneState = {};
    hissLowpass = {};
    rumbleFirst = {};
    rumbleSecond = {};
    dcState = {};
    dcInput = {};
    telemetryState = {};
    telemetry.clear();
    randomState = 0x6d2b79f5u;
    wowPhase = 0.0;
    flutterPhase = 0.0;
    platterPhase = 0.0;
    scratchEnvelope = 0.0f;
    scratchPhase = 0.0f;
    scratchRate = 0.0f;
    scratchSamplesRemaining = 0;
    scratchTotalSamples = 0;
    transportWritePosition = 0;
    dryWritePosition = 0;
    initialized = false;
}

std::uint32_t VinylSimulatorModule::nextRandom() noexcept
{
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return randomState;
}

float VinylSimulatorModule::randomUnit() noexcept
{
    return static_cast<float>(nextRandom() & 0x00ffffffu)
        / static_cast<float>(0x01000000u);
}

float VinylSimulatorModule::randomBipolar() noexcept
{
    return randomUnit() * 2.0f - 1.0f;
}

float VinylSimulatorModule::readTransport(
    int channel, float delaySamples) const noexcept
{
    const auto& history = transportHistory[static_cast<size_t>(channel)];
    const auto size = static_cast<int>(history.size());
    auto position = static_cast<float>(transportWritePosition)
        - juce::jlimit(
              2.0f, static_cast<float>(size - 3), delaySamples);
    while (position < 0.0f)
        position += static_cast<float>(size);
    const auto index = static_cast<int>(position);
    const auto fraction = position - static_cast<float>(index);
    const auto at = [&history, size](int sample)
    {
        while (sample < 0)
            sample += size;
        return history[static_cast<size_t>(sample % size)];
    };
    const auto y0 = at(index - 1);
    const auto y1 = at(index);
    const auto y2 = at(index + 1);
    const auto y3 = at(index + 2);
    const auto a0 =
        -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    const auto a1 =
        y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const auto a2 = -0.5f * y0 + 0.5f * y2;
    return ((a0 * fraction + a1) * fraction + a2) * fraction + y1;
}

void VinylSimulatorModule::startCrackle(
    float amplitude, float durationMilliseconds) noexcept
{
    for (auto& voice : crackleVoices)
    {
        if (voice.active)
            continue;
        const auto durationSamples = juce::jmax(
            2.0f,
            durationMilliseconds * 0.001f * static_cast<float>(sampleRate));
        voice.fast = amplitude;
        voice.slow = amplitude;
        voice.fastDecay = std::exp(-6.0f / durationSamples);
        voice.slowDecay = std::exp(-1.4f / durationSamples);
        voice.pan = randomBipolar();
        voice.active = true;
        return;
    }
}

void VinylSimulatorModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    const auto channels = juce::jlimit(0, 2, buffer.getNumChannels());
    if (channels == 0 || transportHistory[0].empty())
        return;

    std::array<float, controlsPerSlot> targets {};
    for (int control = 0; control < controlsPerSlot; ++control)
        targets[static_cast<size_t>(control)] =
            detail::normalizedControl(controls[static_cast<size_t>(control)],
                                      0.0f);
    if (!initialized)
    {
        for (size_t control = 0; control < smoothed.size(); ++control)
            smoothed[control].setCurrentAndTargetValue(targets[control]);
        initialized = true;
    }
    for (size_t control = 0; control < smoothed.size(); ++control)
        smoothed[control].setTargetValue(targets[control]);

    std::array<float, 2> telemetryCracklePeak {};
    float telemetryNoisePeak = 0.0f;
    float telemetryRumblePeak = 0.0f;
    float telemetryScratchPeak = 0.0f;
    double telemetryWetEnergy = 0.0;
    float latestModulationMilliseconds = 0.0f;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        std::array<float, controlsPerSlot> current {};
        for (size_t control = 0; control < smoothed.size(); ++control)
            current[control] = smoothed[control].getNextValue();
        const auto condition = current[conditionControl];
        const auto crackleAmount = current[crackleControl];
        const auto dustAmount = current[dustControl];
        const auto scratchAmount = current[scratchControl];
        const auto surfaceGain = juce::Decibels::decibelsToGain(
            detail::lerp(
                minimumNoiseDb, -34.0f, current[surfaceNoiseControl]));
        const auto rumbleGain = juce::Decibels::decibelsToGain(
            detail::lerp(
                minimumNoiseDb, -36.0f, current[rumbleControl]));
        const auto stereo = current[stereoControl];

        const auto dustRate = dustAmount * dustAmount * 180.0f;
        if (randomUnit() < dustRate / static_cast<float>(sampleRate))
        {
            const auto amplitude = crackleAmount
                * detail::lerp(0.008f, 0.09f, randomUnit() * randomUnit())
                * detail::lerp(0.55f, 1.35f, condition);
            startCrackle(
                amplitude, detail::lerp(0.12f, 0.9f, randomUnit()));
        }
        const auto crackleRate = crackleAmount * crackleAmount
            * detail::lerp(1.0f, 38.0f, condition);
        if (randomUnit() < crackleRate / static_cast<float>(sampleRate))
        {
            const auto amplitude = crackleAmount
                * detail::lerp(0.035f, 0.34f, randomUnit() * randomUnit())
                * detail::lerp(0.6f, 1.4f, condition);
            startCrackle(
                amplitude, detail::lerp(0.5f, 4.5f, randomUnit()));
        }

        if (scratchSamplesRemaining <= 0)
        {
            const auto scratchRatePerSecond =
                scratchAmount * scratchAmount * 0.9f;
            if (randomUnit()
                < scratchRatePerSecond / static_cast<float>(sampleRate))
            {
                scratchSamplesRemaining = juce::roundToInt(
                    sampleRate * detail::lerp(
                                     0.035f, 0.24f, randomUnit()));
                scratchTotalSamples = scratchSamplesRemaining;
                scratchRate = detail::lerp(45.0f, 190.0f, randomUnit());
                scratchEnvelope = 0.0f;
            }
        }
        auto scratch = 0.0f;
        if (scratchSamplesRemaining > 0)
        {
            const auto progress = 1.0f
                - static_cast<float>(scratchSamplesRemaining)
                      / static_cast<float>(
                          juce::jmax(1, scratchTotalSamples));
            const auto window = std::sin(
                juce::MathConstants<float>::pi
                * juce::jlimit(0.0f, 1.0f, progress));
            scratchPhase += scratchRate / static_cast<float>(sampleRate);
            if (scratchPhase >= 1.0f)
            {
                scratchPhase -= 1.0f;
                scratchEnvelope = detail::lerp(
                    0.15f, 1.0f, randomUnit() * randomUnit());
            }
            scratchEnvelope *= std::exp(
                -1.0f / static_cast<float>(sampleRate * 0.0018));
            scratch = scratchEnvelope * window * scratchAmount
                * detail::lerp(0.03f, 0.22f, condition);
            --scratchSamplesRemaining;
        }

        std::array<float, 2> crackle {};
        for (auto& voice : crackleVoices)
        {
            if (!voice.active)
                continue;
            voice.fast *= voice.fastDecay;
            voice.slow *= voice.slowDecay;
            const auto click = (voice.fast - voice.slow) * 2.4f;
            const auto leftGain = channels == 1
                ? 1.0f : 1.0f - voice.pan * stereo;
            const auto rightGain = 1.0f + voice.pan * stereo;
            crackle[0] += click * leftGain;
            crackle[1] += click * rightGain;
            if (std::abs(voice.slow) < 0.000001f)
                voice.active = false;
        }

        const auto commonNoise = randomBipolar();
        const auto sideNoise = randomBipolar();
        const auto hissCoefficient = 1.0f - std::exp(
            -juce::MathConstants<float>::twoPi * 1800.0f
            / static_cast<float>(sampleRate));
        const auto rumbleCoefficient = 1.0f - std::exp(
            -juce::MathConstants<float>::twoPi
                * detail::lerp(22.0f, 68.0f, condition)
            / static_cast<float>(sampleRate));
        const auto toneCutoff = juce::jmin(
            static_cast<float>(sampleRate * 0.45),
            detail::exponential(
                3200.0f, 19000.0f,
                current[toneControl] * detail::lerp(0.72f, 1.0f, 1.0f - condition)));
        const auto toneCoefficient = 1.0f - std::exp(
            -juce::MathConstants<float>::twoPi * toneCutoff
            / static_cast<float>(sampleRate));

        wowPhase += detail::lerp(0.18f, 0.55f, condition)
            / static_cast<float>(sampleRate);
        flutterPhase += detail::lerp(4.5, 10.5, condition) / sampleRate;
        platterPhase += 33.333333 / 60.0 / sampleRate;
        wowPhase -= std::floor(wowPhase);
        flutterPhase -= std::floor(flutterPhase);
        platterPhase -= std::floor(platterPhase);
        const auto wow = std::sin(
            juce::MathConstants<double>::twoPi * wowPhase);
        const auto flutter = std::sin(
            juce::MathConstants<double>::twoPi * flutterPhase
            + 0.34 * std::sin(
                juce::MathConstants<double>::twoPi * platterPhase));
        const auto modulationSamples =
            current[wowControl] * static_cast<float>(sampleRate * 0.0038)
                * static_cast<float>(wow)
            + current[flutterControl]
                  * static_cast<float>(sampleRate * 0.00065)
                  * static_cast<float>(flutter);
        latestModulationMilliseconds =
            modulationSamples / static_cast<float>(sampleRate) * 1000.0f;

        const std::array<float, 2> dry {
            detail::finiteSample(buffer.getSample(0, sample)),
            channels > 1
                ? detail::finiteSample(buffer.getSample(1, sample))
                : detail::finiteSample(buffer.getSample(0, sample))
        };
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto channelIndex = static_cast<size_t>(channel);
            const auto noise = channels == 1
                ? commonNoise
                : commonNoise
                      + (channel == 0 ? sideNoise : -sideNoise) * stereo;
            hissLowpass[channelIndex] +=
                hissCoefficient * (noise - hissLowpass[channelIndex]);
            const auto hiss =
                (noise - hissLowpass[channelIndex] * 0.72f) * surfaceGain;
            rumbleFirst[channelIndex] += rumbleCoefficient
                * (noise - rumbleFirst[channelIndex]);
            rumbleSecond[channelIndex] += rumbleCoefficient
                * (rumbleFirst[channelIndex] - rumbleSecond[channelIndex]);
            const auto rumble = rumbleSecond[channelIndex] * rumbleGain;

            const auto scratchGain = channels == 1
                ? 1.0f
                : (channel == 0 ? 1.0f + stereo * 0.14f
                                : 1.0f - stereo * 0.14f);
            const auto texture = crackle[channelIndex]
                + scratch * scratchGain
                + hiss + rumble;
            const auto drive = detail::lerp(0.15f, 2.6f, condition);
            const auto softened =
                std::tanh(dry[channelIndex] * drive)
                / std::tanh(drive);
            toneState[channelIndex] += toneCoefficient
                * (softened - toneState[channelIndex]);
            auto worn = toneState[channelIndex] + texture;
            const auto dcCoefficient = std::exp(
                -juce::MathConstants<float>::twoPi * 12.0f
                / static_cast<float>(sampleRate));
            dcState[channelIndex] = dcCoefficient
                    * (dcState[channelIndex] + worn - dcInput[channelIndex]);
            dcInput[channelIndex] = worn;
            worn = detail::finiteSample(dcState[channelIndex]);
            transportHistory[channelIndex][
                static_cast<size_t>(transportWritePosition)] = worn;

            const auto wet = readTransport(
                channel, static_cast<float>(fixedLatencySamples)
                             + modulationSamples);
            auto& delayedDry = dryHistory[channelIndex];
            const auto alignedDry =
                delayedDry[static_cast<size_t>(dryWritePosition)];
            delayedDry[static_cast<size_t>(dryWritePosition)] =
                dry[channelIndex];
            const auto mix = current[mixControl];
            const auto output = juce::Decibels::decibelsToGain(
                detail::lerp(-18.0f, 12.0f, current[outputControl]));
            const auto result =
                (alignedDry + (wet - alignedDry) * mix) * output;
            buffer.setSample(
                channel, sample, detail::finiteSample(result));
            telemetryCracklePeak[channelIndex] = juce::jmax(
                telemetryCracklePeak[channelIndex],
                std::abs(crackle[channelIndex]));
            telemetryNoisePeak = juce::jmax(
                telemetryNoisePeak, std::abs(hiss));
            telemetryRumblePeak = juce::jmax(
                telemetryRumblePeak, std::abs(rumble));
            telemetryScratchPeak = juce::jmax(
                telemetryScratchPeak, std::abs(scratch));
            telemetryWetEnergy += static_cast<double>(wet) * wet;
        }
        transportWritePosition = (transportWritePosition + 1)
            % static_cast<int>(transportHistory[0].size());
        dryWritePosition =
            (dryWritePosition + 1) % fixedLatencySamples;
    }

    if (environment.captureTelemetry)
    {
        telemetryState.sequence += 1;
        telemetryState.valueCount = telemetryValueCount;
        telemetryState.values[transportPhase] =
            static_cast<float>(platterPhase);
        telemetryState.values[leftCrackleActivity] =
            telemetryCracklePeak[0];
        telemetryState.values[rightCrackleActivity] =
            channels > 1 ? telemetryCracklePeak[1]
                         : telemetryCracklePeak[0];
        telemetryState.values[surfaceNoiseActivity] = telemetryNoisePeak;
        telemetryState.values[rumbleActivity] = telemetryRumblePeak;
        telemetryState.values[modulationMilliseconds] =
            latestModulationMilliseconds;
        telemetryState.values[scratchActivity] = telemetryScratchPeak;
        telemetryState.values[wetEnergy] = static_cast<float>(std::sqrt(
            telemetryWetEnergy
            / static_cast<double>(
                juce::jmax(1, channels * buffer.getNumSamples()))));
        appendContinuousTelemetryHistory(
            telemetryState,
            { telemetryCracklePeak[0],
              channels > 1 ? telemetryCracklePeak[1]
                           : telemetryCracklePeak[0],
              telemetryScratchPeak,
              telemetryState.values[wetEnergy] },
            telemetryHistoryCount);
        telemetry.publish(telemetryState);
    }
}

bool VinylSimulatorModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}

double VinylSimulatorModule::tailSeconds(const ControlValues&) const
{
    return static_cast<double>(fixedLatencySamples) / sampleRate;
}
} // namespace megadsp
