#include "PitchBloom.h"
#include "DspHelpers.h"
#include "DspSafety.h"

#include <cmath>

namespace megadsp
{
namespace
{
constexpr std::array<float, 5> intervalSemitones {
    0.0f, 7.0f, 12.0f, 19.0f, 24.0f
};
constexpr std::array<float, PitchBloomModule::diffuserStageCount>
    bloomMilliseconds { 7.1f, 13.7f, 23.9f };

float outputGain(float normalized)
{
    return juce::Decibels::decibelsToGain(
        detail::lerp(-18.0f, 12.0f, normalized));
}
} // namespace

void PitchBloomModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    const auto repeatCapacity =
        juce::jmax(4, juce::roundToInt(sampleRate * 0.55));
    for (auto& channel : repeatDelay)
        channel.assign(static_cast<size_t>(repeatCapacity), 0.0f);
    for (auto& channel : pitchBuffer)
        channel.assign(static_cast<size_t>(
            pitchMinimumDelaySamples + pitchSpanSamples + 4), 0.0f);
    for (size_t stage = 0; stage < diffuser.size(); ++stage)
    {
        const auto capacity = juce::jmax(
            4, juce::roundToInt(
                sampleRate * bloomMilliseconds[stage] * 1.2f * 0.001));
        for (auto& channel : diffuser[stage].buffer)
            channel.assign(static_cast<size_t>(capacity), 0.0f);
    }

    ratioSmoothed.reset(sampleRate, 0.08);
    intervalTransition.reset(sampleRate, 0.05);
    delaySamplesSmoothed.reset(sampleRate, 0.04);
    feedbackSmoothed.reset(sampleRate, 0.05);
    bloomSmoothed.reset(sampleRate, 0.05);
    spreadSmoothed.reset(sampleRate, 0.05);
    lowCutSmoothed.reset(sampleRate, 0.04);
    highCutSmoothed.reset(sampleRate, 0.04);
    duckingSmoothed.reset(sampleRate, 0.04);
    mixSmoothed.reset(sampleRate, 0.03);
    outputSmoothed.reset(sampleRate, 0.03);
    reset();
}

void PitchBloomModule::reset()
{
    for (auto& channel : repeatDelay)
        std::fill(channel.begin(), channel.end(), 0.0f);
    for (auto& channel : pitchBuffer)
        std::fill(channel.begin(), channel.end(), 0.0f);
    for (auto& channel : dryDelay)
        channel.fill(0.0f);
    for (auto& stage : diffuser)
    {
        for (auto& channel : stage.buffer)
            std::fill(channel.begin(), channel.end(), 0.0f);
        stage.writePosition.fill(0);
    }
    lowpassState.fill(0.0f);
    highpassState.fill(0.0f);
    highpassInput.fill(0.0f);
    feedbackState.fill(0.0f);
    pitchInputEnvelope.fill(0.0f);
    transientResetCooldown.fill(0);
    pitchPhase.fill(0.5f);
    previousPitchPhase.fill(0.5f);
    repeatWritePosition = 0;
    pitchWritePosition = 0;
    dryWritePosition = 0;
    inputEnvelope = 0.0f;
    currentRatio = 1.0f;
    previousRatio = 1.0f;
    currentInterval = -1;
    parametersInitialised = false;
    ratioSmoothed.setCurrentAndTargetValue(2.0f);
    intervalTransition.setCurrentAndTargetValue(1.0f);
    delaySamplesSmoothed.setCurrentAndTargetValue(
        static_cast<float>(sampleRate * 0.12));
    feedbackSmoothed.setCurrentAndTargetValue(0.0f);
    bloomSmoothed.setCurrentAndTargetValue(0.0f);
    spreadSmoothed.setCurrentAndTargetValue(0.75f);
    lowCutSmoothed.setCurrentAndTargetValue(180.0f);
    highCutSmoothed.setCurrentAndTargetValue(10000.0f);
    duckingSmoothed.setCurrentAndTargetValue(0.0f);
    mixSmoothed.setCurrentAndTargetValue(0.0f);
    outputSmoothed.setCurrentAndTargetValue(1.0f);
    telemetryWorking = {};
    telemetry.clear();
    telemetryEnergy.fill(0.0);
    telemetryDifferenceEnergy = 0.0;
    telemetryPhaseSamples = 0.0f;
    telemetrySampleCount = 0;
    telemetryEventSequence = 0;
    telemetryPublicationSequence = 0;
    telemetryWasCapturing = false;
}

bool PitchBloomModule::readEventTelemetry(
    EventTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}

void PitchBloomModule::beginTelemetryBlock(bool capture) noexcept
{
    if (!capture)
    {
        telemetryWasCapturing = false;
        return;
    }
    if (!telemetryWasCapturing)
    {
        telemetryWorking = {};
        telemetryEnergy.fill(0.0);
        telemetryDifferenceEnergy = 0.0;
        telemetryPhaseSamples = 0.0f;
        telemetrySampleCount = 0;
    }
    telemetryWasCapturing = true;
}

void PitchBloomModule::addTelemetryEvent(
    float intervalSemitones, float intervalSeconds, float energy,
    float pan, float spread, bool stereo) noexcept
{
    std::uint32_t retained = 0;
    for (std::uint32_t index = 0;
         index < telemetryWorking.eventCount; ++index)
    {
        auto event = telemetryWorking.events[index];
        const auto duration = event.values[static_cast<size_t>(
            PitchBloomTelemetryValue::intervalSeconds)];
        if (duration > 0.0f)
            event.progress +=
                1.0f / static_cast<float>(sampleRate * duration);
        if (event.progress < 1.0f)
            telemetryWorking.events[retained++] = event;
    }
    telemetryWorking.eventCount = retained;

    if (!(energy > 0.00001f) || !std::isfinite(energy))
        return;

    if (telemetryWorking.eventCount >= eventTelemetryCapacity)
    {
        for (size_t index = 1; index < eventTelemetryCapacity; ++index)
            telemetryWorking.events[index - 1] =
                telemetryWorking.events[index];
        telemetryWorking.eventCount =
            static_cast<std::uint32_t>(eventTelemetryCapacity - 1);
    }

    auto& event =
        telemetryWorking.events[telemetryWorking.eventCount++];
    event = {};
    event.sequence = ++telemetryEventSequence;
    event.kind =
        static_cast<std::uint32_t>(
            PitchBloomTelemetryEventKind::shiftedRepeat);
    event.flags = stereo ? 1u : 0u;
    event.position[0] = juce::jlimit(-1.0f, 1.0f, pan);
    event.position[1] = juce::jlimit(0.0f, 1.0f, spread);
    event.values[
        static_cast<size_t>(
            PitchBloomTelemetryValue::intervalSemitones)] =
        intervalSemitones;
    event.values[
        static_cast<size_t>(PitchBloomTelemetryValue::intervalSeconds)] =
        intervalSeconds;
    event.values[
        static_cast<size_t>(PitchBloomTelemetryValue::energy)] = energy;
    event.values[
        static_cast<size_t>(PitchBloomTelemetryValue::stereoSpread)] =
        juce::jlimit(0.0f, 1.0f, spread);
}

void PitchBloomModule::captureTelemetrySample(
    const std::array<float, 2>& shifted, float delaySamples,
    float ratio, bool stereo) noexcept
{
    for (size_t channel = 0; channel < 2; ++channel)
        telemetryEnergy[channel] +=
            static_cast<double>(shifted[channel]) * shifted[channel];
    const auto difference = shifted[0] - shifted[1];
    telemetryDifferenceEnergy +=
        static_cast<double>(difference) * difference;
    ++telemetrySampleCount;
    telemetryPhaseSamples += 1.0f;
    if (telemetryPhaseSamples < delaySamples || telemetrySampleCount == 0)
        return;

    const auto samples = static_cast<double>(telemetrySampleCount);
    const std::array<float, 2> rms {
        static_cast<float>(std::sqrt(telemetryEnergy[0] / samples)),
        static_cast<float>(std::sqrt(telemetryEnergy[1] / samples))
    };
    const auto energy = std::sqrt(
        0.5f * (rms[0] * rms[0] + rms[1] * rms[1]));
    const auto sum = rms[0] + rms[1];
    const auto pan = sum > 0.0000001f
        ? (rms[1] - rms[0]) / sum : 0.0f;
    const auto differenceRms = static_cast<float>(
        std::sqrt(telemetryDifferenceEnergy / (2.0 * samples)));
    const auto spread = energy > 0.0000001f
        ? juce::jlimit(0.0f, 1.0f, differenceRms / energy) : 0.0f;
    const auto semitones = 12.0f * std::log2(
        juce::jmax(0.000001f, ratio));
    addTelemetryEvent(
        semitones, delaySamples / static_cast<float>(sampleRate),
        energy, pan, spread, stereo);

    telemetryPhaseSamples = std::fmod(telemetryPhaseSamples, delaySamples);
    telemetrySampleCount = 0;
    telemetryEnergy.fill(0.0);
    telemetryDifferenceEnergy = 0.0;
}

float PitchBloomModule::readCircular(
    const std::vector<float>& source, int writePosition,
    float delaySamples) const
{
    const auto capacity = static_cast<int>(source.size());
    auto read = static_cast<float>(writePosition) - delaySamples;
    while (read < 0.0f)
        read += static_cast<float>(capacity);
    const auto first = static_cast<int>(read);
    const auto second = first + 1 < capacity ? first + 1 : 0;
    const auto fraction = read - static_cast<float>(first);
    return source[static_cast<size_t>(first)]
        + (source[static_cast<size_t>(second)]
           - source[static_cast<size_t>(first)]) * fraction;
}

float PitchBloomModule::processPitch(
    int channel, float input, float ratio, float oldRatio, float transition)
{
    const auto index = static_cast<size_t>(channel);
    auto& envelope = pitchInputEnvelope[index];
    const auto magnitude = std::abs(input);
    const auto release = 1.0f - std::exp(
        -1.0f / static_cast<float>(sampleRate * 0.025));
    const auto transient = magnitude > 0.015f
        && magnitude > envelope * 3.5f
        && transientResetCooldown[index] <= 0;
    envelope += (magnitude - envelope)
        * (magnitude > envelope ? 0.35f : release);
    if (transient)
    {
        pitchPhase[index] = 0.5f;
        previousPitchPhase[index] = 0.5f;
        transientResetCooldown[index] =
            juce::roundToInt(sampleRate * 0.012);
    }
    else if (transientResetCooldown[index] > 0)
        --transientResetCooldown[index];

    auto& line = pitchBuffer[index];
    line[static_cast<size_t>(pitchWritePosition)] = input;
    auto render = [&](float& phase, float renderRatio)
    {
        const auto secondPhase = std::fmod(phase + 0.5f, 1.0f);
        const auto firstWindow = std::sin(
            juce::MathConstants<float>::pi * phase);
        const auto secondWindow = std::sin(
            juce::MathConstants<float>::pi * secondPhase);
        const auto firstDelay = static_cast<float>(pitchMinimumDelaySamples)
            + (1.0f - phase) * static_cast<float>(pitchSpanSamples);
        const auto secondDelay = static_cast<float>(pitchMinimumDelaySamples)
            + (1.0f - secondPhase) * static_cast<float>(pitchSpanSamples);
        const auto first = readCircular(line, pitchWritePosition, firstDelay);
        const auto second = readCircular(line, pitchWritePosition, secondDelay);
        const auto result = first * firstWindow * firstWindow
            + second * secondWindow * secondWindow;
        phase += (renderRatio - 1.0f)
            / static_cast<float>(pitchSpanSamples);
        phase -= std::floor(phase);
        return result;
    };

    const auto shifted = render(pitchPhase[index], ratio);
    if (transition >= 1.0f)
        return shifted;
    const auto previous = render(previousPitchPhase[index], oldRatio);
    return previous + (shifted - previous) * transition;
}

float PitchBloomModule::processBloom(
    int channel, float input, float amount)
{
    auto value = input;
    for (size_t stageIndex = 0; stageIndex < diffuser.size(); ++stageIndex)
    {
        auto& stage = diffuser[stageIndex];
        auto& line = stage.buffer[static_cast<size_t>(channel)];
        auto& write = stage.writePosition[static_cast<size_t>(channel)];
        const auto capacity = static_cast<int>(line.size());
        const auto channelScale = channel == 0 ? 1.0f : 1.13f;
        const auto delay = juce::jlimit(
            1, capacity - 1,
            juce::roundToInt(bloomMilliseconds[stageIndex] * channelScale
                             * 0.001 * sampleRate));
        auto read = write - delay;
        if (read < 0)
            read += capacity;
        const auto delayed = line[static_cast<size_t>(read)];
        const auto coefficient = 0.22f + amount * 0.43f;
        const auto allpass = delayed - coefficient * value;
        line[static_cast<size_t>(write)] = value + coefficient * allpass;
        if (++write >= capacity)
            write = 0;
        value = allpass;
    }
    return input + (value - input) * amount;
}

void PitchBloomModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    if (repeatDelay[0].empty() || buffer.getNumChannels() <= 0)
        return;

    beginTelemetryBlock(environment.captureTelemetry);

    const auto interval = discreteIndex(
        detail::normalizedControl(controls[0], 0.5f), 5);
    const auto fineCents =
        detail::lerp(-50.0f, 50.0f,
                     detail::normalizedControl(controls[1], 0.5f));
    const auto semitones =
        intervalSemitones[static_cast<size_t>(interval)] + fineCents * 0.01f;
    const auto ratioTarget = std::pow(2.0f, semitones / 12.0f);
    const auto delayTarget = detail::exponential(
        0.020f, 0.500f, detail::normalizedControl(controls[2], 0.557f))
        * static_cast<float>(sampleRate);
    const auto feedbackTarget =
        detail::normalizedControl(controls[3], 0.353f) * 0.85f;
    const auto bloomTarget = detail::normalizedControl(controls[4], 0.35f);
    const auto spreadTarget = detail::normalizedControl(controls[5], 0.75f);
    const auto lowCutTarget = detail::exponential(
        20.0f, 2000.0f, detail::normalizedControl(controls[6], 0.477f));
    const auto highCutTarget = detail::exponential(
        1000.0f, 20000.0f, detail::normalizedControl(controls[7], 0.768f));
    const auto duckingTarget = detail::normalizedControl(controls[8], 0.20f);
    const auto mixTarget = detail::normalizedControl(controls[9], 0.15f);
    const auto outputTarget = outputGain(
        detail::normalizedControl(controls[10], 0.60f));

    if (currentInterval >= 0 && interval != currentInterval)
    {
        previousPitchPhase = pitchPhase;
        previousRatio = currentRatio;
        intervalTransition.setCurrentAndTargetValue(0.0f);
        intervalTransition.setTargetValue(1.0f);
    }
    currentInterval = interval;
    ratioSmoothed.setTargetValue(ratioTarget);
    delaySamplesSmoothed.setTargetValue(delayTarget);
    feedbackSmoothed.setTargetValue(feedbackTarget);
    bloomSmoothed.setTargetValue(bloomTarget);
    spreadSmoothed.setTargetValue(spreadTarget);
    lowCutSmoothed.setTargetValue(lowCutTarget);
    highCutSmoothed.setTargetValue(highCutTarget);
    duckingSmoothed.setTargetValue(duckingTarget);
    mixSmoothed.setTargetValue(mixTarget);
    outputSmoothed.setTargetValue(outputTarget);
    if (!parametersInitialised)
    {
        ratioSmoothed.setCurrentAndTargetValue(ratioTarget);
        intervalTransition.setCurrentAndTargetValue(1.0f);
        currentRatio = ratioTarget;
        previousRatio = ratioTarget;
        delaySamplesSmoothed.setCurrentAndTargetValue(delayTarget);
        feedbackSmoothed.setCurrentAndTargetValue(feedbackTarget);
        bloomSmoothed.setCurrentAndTargetValue(bloomTarget);
        spreadSmoothed.setCurrentAndTargetValue(spreadTarget);
        lowCutSmoothed.setCurrentAndTargetValue(lowCutTarget);
        highCutSmoothed.setCurrentAndTargetValue(highCutTarget);
        duckingSmoothed.setCurrentAndTargetValue(duckingTarget);
        mixSmoothed.setCurrentAndTargetValue(mixTarget);
        outputSmoothed.setCurrentAndTargetValue(outputTarget);
        parametersInitialised = true;
    }

    const auto channels = juce::jmin(2, buffer.getNumChannels());
    const auto repeatCapacity = static_cast<int>(repeatDelay[0].size());
    const auto dryCapacity = static_cast<int>(dryDelay[0].size());
    const auto pitchCapacity = static_cast<int>(pitchBuffer[0].size());
    const auto envelopeAttack =
        1.0f - std::exp(-1.0f / static_cast<float>(sampleRate * 0.005));
    const auto envelopeRelease =
        1.0f - std::exp(-1.0f / static_cast<float>(sampleRate * 0.20));
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        std::array<float, 2> dry {
            detail::finiteSample(buffer.getSample(0, sample)),
            channels > 1
                ? detail::finiteSample(buffer.getSample(1, sample))
                : detail::finiteSample(buffer.getSample(0, sample))
        };
        const auto level = juce::jmax(std::abs(dry[0]), std::abs(dry[1]));
        inputEnvelope += (level - inputEnvelope)
            * (level > inputEnvelope ? envelopeAttack : envelopeRelease);

        const auto ratio = ratioSmoothed.getNextValue();
        currentRatio = ratio;
        const auto pitchTransition = intervalTransition.getNextValue();
        const auto delay = juce::jlimit(
            1.0f, static_cast<float>(repeatCapacity - 2),
            delaySamplesSmoothed.getNextValue());
        const auto feedback = feedbackSmoothed.getNextValue();
        const auto bloom = bloomSmoothed.getNextValue();
        const auto spread = spreadSmoothed.getNextValue();
        const auto lowCut = lowCutSmoothed.getNextValue();
        const auto highCut = highCutSmoothed.getNextValue();
        const auto ducking = duckingSmoothed.getNextValue();
        const auto mix = mixSmoothed.getNextValue();
        const auto output = outputSmoothed.getNextValue();
        const auto lowpassCoefficient = 1.0f - std::exp(
            -juce::MathConstants<float>::twoPi * highCut
            / static_cast<float>(sampleRate));
        const auto highpassPole = std::exp(
            -juce::MathConstants<float>::twoPi * lowCut
            / static_cast<float>(sampleRate));

        std::array<float, 2> alignedDry {};
        std::array<float, 2> wet {};
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            const auto dryRead = (dryWritePosition + 1) % dryCapacity;
            alignedDry[index] = dryDelay[index][static_cast<size_t>(dryRead)];
            dryDelay[index][static_cast<size_t>(dryWritePosition)] = dry[index];

            const auto delayed = readCircular(
                repeatDelay[index], repeatWritePosition, delay);
            const auto repeatInput = dry[index] + feedbackState[index] * feedback;
            repeatDelay[index][static_cast<size_t>(repeatWritePosition)] =
                std::isfinite(repeatInput) ? repeatInput : 0.0f;
            const auto shifted = processPitch(
                channel, delayed, ratio, previousRatio, pitchTransition);

            auto& lowState = lowpassState[index];
            lowState += lowpassCoefficient * (shifted - lowState);
            auto& highState = highpassState[index];
            auto& previousInput = highpassInput[index];
            highState = highpassPole
                * (highState + lowState - previousInput);
            previousInput = lowState;
            wet[index] = processBloom(
                channel, std::isfinite(highState) ? highState : 0.0f, bloom);
        }
        if (++repeatWritePosition >= repeatCapacity)
            repeatWritePosition = 0;
        if (++pitchWritePosition >= pitchCapacity)
            pitchWritePosition = 0;
        if (++dryWritePosition >= dryCapacity)
            dryWritePosition = 0;

        const auto feedbackWet = wet;
        if (channels > 1)
        {
            const auto mid = (wet[0] + wet[1]) * 0.5f;
            const auto side = (wet[0] - wet[1]) * 0.5f * spread * 1.5f;
            wet = { mid + side, mid - side };
        }
        else
            wet[1] = wet[0];

        if (environment.captureTelemetry)
            captureTelemetrySample(
                wet, delay, ratio, channels > 1);
        for (int channel = 0; channel < 2; ++channel)
            feedbackState[static_cast<size_t>(channel)] =
                detail::finiteSample(
                    feedbackWet[static_cast<size_t>(channel)]);

        const auto wetDuck =
            1.0f / (1.0f + ducking * inputEnvelope * 10.0f);
        const auto dryGain = std::cos(
            mix * juce::MathConstants<float>::halfPi);
        const auto wetGain = std::sin(
            mix * juce::MathConstants<float>::halfPi) * wetDuck;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto index = static_cast<size_t>(juce::jmin(channel, 1));
            const auto result =
                (alignedDry[index] * dryGain + wet[index] * wetGain) * output;
            buffer.setSample(channel, sample,
                             std::isfinite(result) ? result : 0.0f);
        }
    }
    if (environment.captureTelemetry)
    {
        telemetryWorking.sequence = ++telemetryPublicationSequence;
        telemetry.publish(telemetryWorking);
    }
}

double PitchBloomModule::tailSeconds(const ControlValues& controls) const
{
    const auto feedback =
        detail::normalizedControl(controls[3], 0.353f) * 0.85f;
    const auto delay = detail::exponential(
        0.020f, 0.500f, detail::normalizedControl(controls[2], 0.557f));
    const auto maximumPitchDelay =
        static_cast<double>(pitchMinimumDelaySamples + pitchSpanSamples)
        / juce::jmax(8000.0, sampleRate);
    const auto amount = detail::normalizedControl(controls[4], 0.35f);
    const auto coefficient = 0.22f + amount * 0.43f;
    const auto bloomTail = amount <= 0.001f ? 0.0
        : 0.051
            + 0.028 * juce::jmax(
                0.0, std::log(0.001 / static_cast<double>(amount))
                         / std::log(static_cast<double>(coefficient)));
    const auto cycle = delay + maximumPitchDelay + amount * 0.051;
    if (feedback <= 0.0001f)
        return cycle + bloomTail;
    const auto repeats = std::log(0.001) / std::log(
        static_cast<double>(feedback));
    return juce::jmin(120.0, cycle * repeats + bloomTail);
}
} // namespace megadsp
