#include "DiffusionDelay.h"
#include "DspHelpers.h"
#include "DspSafety.h"

#include <cmath>

namespace megadsp
{
namespace
{
constexpr std::array<float, DiffusionDelayModule::diffuserStageCount>
    diffuserMilliseconds { 11.3f, 19.7f, 31.1f, 47.9f };
constexpr std::array<float, 8> beatDivisions {
    0.125f, 0.25f, 0.375f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f
};

float outputGain(float normalized)
{
    return juce::Decibels::decibelsToGain(
        detail::lerp(-18.0f, 12.0f, normalized));
}
} // namespace

void DiffusionDelayModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    const auto primaryCapacity =
        juce::jmax(4, juce::roundToInt(sampleRate * 4.05));
    for (auto& channel : primaryDelay)
        channel.assign(static_cast<size_t>(primaryCapacity), 0.0f);

    for (size_t stage = 0; stage < diffuser.size(); ++stage)
    {
        const auto capacity = juce::jmax(
            4, juce::roundToInt(sampleRate
                * (diffuserMilliseconds[stage] + 4.0f) * 0.001));
        for (auto& channel : diffuser[stage].buffer)
            channel.assign(static_cast<size_t>(capacity), 0.0f);
    }

    delaySamplesSmoothed.reset(sampleRate, 0.04);
    feedbackSmoothed.reset(sampleRate, 0.04);
    diffusionSmoothed.reset(sampleRate, 0.04);
    movementSmoothed.reset(sampleRate, 0.06);
    lowCutSmoothed.reset(sampleRate, 0.04);
    highCutSmoothed.reset(sampleRate, 0.04);
    widthSmoothed.reset(sampleRate, 0.04);
    duckingSmoothed.reset(sampleRate, 0.04);
    mixSmoothed.reset(sampleRate, 0.03);
    outputSmoothed.reset(sampleRate, 0.03);
    reset();
}

void DiffusionDelayModule::reset()
{
    for (auto& channel : primaryDelay)
        std::fill(channel.begin(), channel.end(), 0.0f);
    for (auto& stage : diffuser)
    {
        for (auto& channel : stage.buffer)
            std::fill(channel.begin(), channel.end(), 0.0f);
        stage.writePosition.fill(0);
        stage.phase = 0.0f;
    }
    lowpassState.fill(0.0f);
    highpassState.fill(0.0f);
    highpassInput.fill(0.0f);
    sideLowpassState = 0.0f;
    primaryWritePosition = 0;
    inputEnvelope = 0.0f;
    parametersInitialised = false;
    delaySamplesSmoothed.setCurrentAndTargetValue(
        static_cast<float>(sampleRate * 0.5));
    feedbackSmoothed.setCurrentAndTargetValue(0.0f);
    diffusionSmoothed.setCurrentAndTargetValue(0.0f);
    movementSmoothed.setCurrentAndTargetValue(0.0f);
    lowCutSmoothed.setCurrentAndTargetValue(120.0f);
    highCutSmoothed.setCurrentAndTargetValue(10000.0f);
    widthSmoothed.setCurrentAndTargetValue(1.0f);
    duckingSmoothed.setCurrentAndTargetValue(0.0f);
    mixSmoothed.setCurrentAndTargetValue(0.0f);
    outputSmoothed.setCurrentAndTargetValue(1.0f);
    telemetryWorking = {};
    telemetry.clear();
    telemetryPrimaryEnergy.fill(0.0);
    telemetryCloudEnergy.fill(0.0);
    telemetryPrimaryDifferenceEnergy = 0.0;
    telemetryCloudDifferenceEnergy = 0.0;
    telemetryPhaseSamples = 0.0f;
    telemetrySampleCount = 0;
    telemetryEventSequence = 0;
    telemetryPublicationSequence = 0;
    telemetryWasCapturing = false;
}

bool DiffusionDelayModule::readEventTelemetry(
    EventTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}

void DiffusionDelayModule::beginTelemetryBlock(bool capture) noexcept
{
    if (!capture)
    {
        telemetryWasCapturing = false;
        return;
    }
    if (!telemetryWasCapturing)
    {
        telemetryWorking = {};
        telemetryPrimaryEnergy.fill(0.0);
        telemetryCloudEnergy.fill(0.0);
        telemetryPrimaryDifferenceEnergy = 0.0;
        telemetryCloudDifferenceEnergy = 0.0;
        telemetryPhaseSamples = 0.0f;
        telemetrySampleCount = 0;
    }
    telemetryWasCapturing = true;
}

void DiffusionDelayModule::addTelemetryEvent(
    DiffusionDelayTelemetryEventKind kind, float energy,
    float intervalSeconds, float diffusion, float pan, float spread,
    bool stereo) noexcept
{
    std::uint32_t retained = 0;
    for (std::uint32_t index = 0;
         index < telemetryWorking.eventCount; ++index)
    {
        auto event = telemetryWorking.events[index];
        const auto duration = event.values[static_cast<size_t>(
            DiffusionDelayTelemetryValue::intervalSeconds)];
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
    event.kind = static_cast<std::uint32_t>(kind);
    event.flags = stereo ? 1u : 0u;
    event.position[0] = juce::jlimit(-1.0f, 1.0f, pan);
    event.position[1] = juce::jlimit(0.0f, 1.0f, spread);
    event.values[
        static_cast<size_t>(DiffusionDelayTelemetryValue::energy)] = energy;
    event.values[
        static_cast<size_t>(
            DiffusionDelayTelemetryValue::intervalSeconds)] =
        intervalSeconds;
    event.values[
        static_cast<size_t>(DiffusionDelayTelemetryValue::diffusion)] =
        juce::jlimit(0.0f, 1.0f, diffusion);
    event.values[
        static_cast<size_t>(
            DiffusionDelayTelemetryValue::stereoSpread)] =
        juce::jlimit(0.0f, 1.0f, spread);
}

void DiffusionDelayModule::captureTelemetrySample(
    const std::array<float, 2>& primary,
    const std::array<float, 2>& cloud, float delaySamples,
    float diffusion, bool stereo) noexcept
{
    for (size_t channel = 0; channel < 2; ++channel)
    {
        telemetryPrimaryEnergy[channel] +=
            static_cast<double>(primary[channel]) * primary[channel];
        telemetryCloudEnergy[channel] +=
            static_cast<double>(cloud[channel]) * cloud[channel];
    }
    const auto primaryDifference = primary[0] - primary[1];
    const auto cloudDifference = cloud[0] - cloud[1];
    telemetryPrimaryDifferenceEnergy +=
        static_cast<double>(primaryDifference) * primaryDifference;
    telemetryCloudDifferenceEnergy +=
        static_cast<double>(cloudDifference) * cloudDifference;
    ++telemetrySampleCount;
    telemetryPhaseSamples += 1.0f;
    if (telemetryPhaseSamples < delaySamples || telemetrySampleCount == 0)
        return;

    const auto samples = static_cast<double>(telemetrySampleCount);
    const auto channelEnergy = [](const std::array<double, 2>& sum,
                                  double count)
    {
        return std::array<float, 2> {
            static_cast<float>(std::sqrt(sum[0] / count)),
            static_cast<float>(std::sqrt(sum[1] / count))
        };
    };
    const auto primaryRms =
        channelEnergy(telemetryPrimaryEnergy, samples);
    const auto cloudRms = channelEnergy(telemetryCloudEnergy, samples);
    const auto summarize = [](const std::array<float, 2>& rms,
                              double differenceEnergy, double count)
    {
        const auto energy = std::sqrt(
            0.5f * (rms[0] * rms[0] + rms[1] * rms[1]));
        const auto sum = rms[0] + rms[1];
        const auto pan = sum > 0.0000001f
            ? (rms[1] - rms[0]) / sum : 0.0f;
        const auto differenceRms = static_cast<float>(
            std::sqrt(differenceEnergy / (2.0 * count)));
        const auto spread = energy > 0.0000001f
            ? juce::jlimit(0.0f, 1.0f, differenceRms / energy) : 0.0f;
        return std::array<float, 3> { energy, pan, spread };
    };
    const auto primarySummary = summarize(
        primaryRms, telemetryPrimaryDifferenceEnergy, samples);
    const auto cloudSummary = summarize(
        cloudRms, telemetryCloudDifferenceEnergy, samples);
    const auto intervalSeconds =
        delaySamples / static_cast<float>(sampleRate);
    addTelemetryEvent(
        DiffusionDelayTelemetryEventKind::primaryRepeat,
        primarySummary[0], intervalSeconds, diffusion,
        primarySummary[1], primarySummary[2], stereo);
    addTelemetryEvent(
        DiffusionDelayTelemetryEventKind::diffusionCloud,
        cloudSummary[0], intervalSeconds, diffusion,
        cloudSummary[1], cloudSummary[2], stereo);

    telemetryPhaseSamples = std::fmod(telemetryPhaseSamples, delaySamples);
    telemetrySampleCount = 0;
    telemetryPrimaryEnergy.fill(0.0);
    telemetryCloudEnergy.fill(0.0);
    telemetryPrimaryDifferenceEnergy = 0.0;
    telemetryCloudDifferenceEnergy = 0.0;
}

float DiffusionDelayModule::readPrimary(
    int channel, float delaySamples) const
{
    const auto& source = primaryDelay[static_cast<size_t>(channel)];
    const auto capacity = static_cast<int>(source.size());
    auto position = static_cast<float>(primaryWritePosition) - delaySamples;
    if (position < 0.0f)
        position += static_cast<float>(capacity);
    const auto first = static_cast<int>(position);
    const auto second = first + 1 < capacity ? first + 1 : 0;
    const auto fraction = position - static_cast<float>(first);
    return source[static_cast<size_t>(first)]
        + (source[static_cast<size_t>(second)]
           - source[static_cast<size_t>(first)]) * fraction;
}

float DiffusionDelayModule::processDiffuser(
    int channel, float input, float amount, float movement)
{
    std::array<float, 2> branch { input, input };
    for (size_t stageIndex = 0; stageIndex < diffuser.size(); ++stageIndex)
    {
        const auto branchIndex = stageIndex / 2;
        auto& value = branch[branchIndex];
        auto& stage = diffuser[stageIndex];
        auto& line = stage.buffer[static_cast<size_t>(channel)];
        auto& write = stage.writePosition[static_cast<size_t>(channel)];
        const auto capacity = static_cast<int>(line.size());
        const auto phaseOffset = channel == 0 ? 0.0f : 1.73f;
        const auto modulation = std::sin(
            stage.phase + phaseOffset
            + static_cast<float>(stageIndex) * 0.91f);
        const auto delay = juce::jlimit(
            1.0f, static_cast<float>(capacity - 2),
            diffuserMilliseconds[stageIndex] * 0.001f
                * static_cast<float>(sampleRate)
            + modulation * movement * 0.0015f
                * static_cast<float>(sampleRate));
        auto read = static_cast<float>(write) - delay;
        if (read < 0.0f)
            read += static_cast<float>(capacity);
        const auto first = static_cast<int>(read);
        const auto second = first + 1 < capacity ? first + 1 : 0;
        const auto fraction = read - static_cast<float>(first);
        const auto delayed = line[static_cast<size_t>(first)]
            + (line[static_cast<size_t>(second)]
               - line[static_cast<size_t>(first)]) * fraction;
        const auto coefficient = 0.18f + amount * 0.52f;
        const auto allpass = delayed - coefficient * value;
        line[static_cast<size_t>(write)] = value + coefficient * allpass;
        if (++write >= capacity)
            write = 0;
        value = allpass;
    }
    const auto parallelOutput = (branch[0] + branch[1]) * 0.5f;
    return input + (parallelOutput - input) * amount;
}

void DiffusionDelayModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    if (primaryDelay[0].empty() || buffer.getNumChannels() <= 0)
        return;

    beginTelemetryBlock(environment.captureTelemetry);

    const auto bpm = std::isfinite(environment.bpm)
                         && environment.bpm >= 30.0
                         && environment.bpm <= 400.0
                     ? environment.bpm : 120.0;
    const auto division = discreteIndex(
        detail::normalizedControl(controls[2], 0.69f), 8);
    const auto freeSeconds = detail::exponential(
        0.010f, 2.0f, detail::normalizedControl(controls[0], 0.74f));
    const auto syncSeconds =
        beatDivisions[static_cast<size_t>(division)] * 60.0 / bpm;
    const auto delaySeconds =
        detail::normalizedControl(controls[1], 1.0f) >= 0.5f
            ? syncSeconds : freeSeconds;
    const auto delayTarget = juce::jlimit(
        1.0f, static_cast<float>(primaryDelay[0].size() - 2),
        static_cast<float>(delaySeconds * sampleRate));
    const auto feedbackTarget =
        detail::normalizedControl(controls[3], 0.389f) * 0.90f;
    const auto diffusionTarget = detail::normalizedControl(controls[4], 0.30f);
    const auto movementTarget = detail::normalizedControl(controls[5], 0.15f);
    const auto lowCutTarget = detail::exponential(
        20.0f, 2000.0f, detail::normalizedControl(controls[6], 0.389f));
    const auto highCutTarget = detail::exponential(
        1000.0f, 20000.0f, detail::normalizedControl(controls[7], 0.768f));
    const auto widthTarget =
        detail::normalizedControl(controls[8], 2.0f / 3.0f) * 1.5f;
    const auto duckingTarget = detail::normalizedControl(controls[9], 0.20f);
    const auto mixTarget = detail::normalizedControl(controls[10], 0.20f);
    const auto outputTarget = outputGain(
        detail::normalizedControl(controls[11], 0.60f));

    delaySamplesSmoothed.setTargetValue(delayTarget);
    feedbackSmoothed.setTargetValue(feedbackTarget);
    diffusionSmoothed.setTargetValue(diffusionTarget);
    movementSmoothed.setTargetValue(movementTarget);
    lowCutSmoothed.setTargetValue(lowCutTarget);
    highCutSmoothed.setTargetValue(highCutTarget);
    widthSmoothed.setTargetValue(widthTarget);
    duckingSmoothed.setTargetValue(duckingTarget);
    mixSmoothed.setTargetValue(mixTarget);
    outputSmoothed.setTargetValue(outputTarget);
    if (!parametersInitialised)
    {
        delaySamplesSmoothed.setCurrentAndTargetValue(delayTarget);
        feedbackSmoothed.setCurrentAndTargetValue(feedbackTarget);
        diffusionSmoothed.setCurrentAndTargetValue(diffusionTarget);
        movementSmoothed.setCurrentAndTargetValue(movementTarget);
        lowCutSmoothed.setCurrentAndTargetValue(lowCutTarget);
        highCutSmoothed.setCurrentAndTargetValue(highCutTarget);
        widthSmoothed.setCurrentAndTargetValue(widthTarget);
        duckingSmoothed.setCurrentAndTargetValue(duckingTarget);
        mixSmoothed.setCurrentAndTargetValue(mixTarget);
        outputSmoothed.setCurrentAndTargetValue(outputTarget);
        parametersInitialised = true;
    }

    const auto channels = juce::jmin(2, buffer.getNumChannels());
    const auto envelopeAttack =
        1.0f - std::exp(-1.0f / static_cast<float>(sampleRate * 0.005));
    const auto envelopeRelease =
        1.0f - std::exp(-1.0f / static_cast<float>(sampleRate * 0.18));
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

        const auto delay = delaySamplesSmoothed.getNextValue();
        const auto feedback = feedbackSmoothed.getNextValue();
        const auto diffusion = diffusionSmoothed.getNextValue();
        const auto movement = movementSmoothed.getNextValue();
        const auto lowCut = lowCutSmoothed.getNextValue();
        const auto highCut = highCutSmoothed.getNextValue();
        const auto width = widthSmoothed.getNextValue();
        const auto ducking = duckingSmoothed.getNextValue();
        const auto mix = mixSmoothed.getNextValue();
        const auto output = outputSmoothed.getNextValue();
        const auto lowpassCoefficient = 1.0f - std::exp(
            -juce::MathConstants<float>::twoPi * highCut
            / static_cast<float>(sampleRate));
        const auto highpassPole = std::exp(
            -juce::MathConstants<float>::twoPi * lowCut
            / static_cast<float>(sampleRate));

        std::array<float, 2> primaryActivity {};
        std::array<float, 2> cloud {};
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto delayed = readPrimary(channel, delay);
            primaryActivity[static_cast<size_t>(channel)] = delayed;
            auto& lowState = lowpassState[static_cast<size_t>(channel)];
            lowState += lowpassCoefficient * (delayed - lowState);
            auto& highState = highpassState[static_cast<size_t>(channel)];
            auto& previousInput = highpassInput[static_cast<size_t>(channel)];
            highState = highpassPole
                * (highState + lowState - previousInput);
            previousInput = lowState;
            const auto filtered = std::isfinite(highState) ? highState : 0.0f;
            cloud[static_cast<size_t>(channel)] =
                processDiffuser(channel, filtered, diffusion, movement);
        }
        for (size_t stage = 0; stage < diffuser.size(); ++stage)
        {
            diffuser[stage].phase +=
                (0.07f + static_cast<float>(stage) * 0.031f)
                * juce::MathConstants<float>::twoPi
                / static_cast<float>(sampleRate);
            if (diffuser[stage].phase
                >= juce::MathConstants<float>::twoPi)
                diffuser[stage].phase -= juce::MathConstants<float>::twoPi;
        }

        const auto feedbackCloud = cloud;
        if (channels > 1)
        {
            const auto mid = (cloud[0] + cloud[1]) * 0.5f;
            const auto rawSide = (cloud[0] - cloud[1]) * 0.5f;
            const auto sideLowCoefficient = 1.0f - std::exp(
                -juce::MathConstants<float>::twoPi * 240.0f
                / static_cast<float>(sampleRate));
            sideLowpassState += sideLowCoefficient
                * (rawSide - sideLowpassState);
            const auto lowFocus = juce::jlimit(
                0.0f, 1.0f,
                diffusion * 0.65f + juce::jmax(0.0f, width - 1.0f) * 0.35f);
            const auto side =
                (rawSide - sideLowpassState * lowFocus) * width;
            cloud = { mid + side, mid - side };
        }
        else
            cloud[1] = cloud[0];

        if (environment.captureTelemetry)
            captureTelemetrySample(
                primaryActivity, cloud, delay, diffusion, channels > 1);

        for (int channel = 0; channel < 2; ++channel)
        {
            const auto writeValue = dry[static_cast<size_t>(channel)]
                + feedbackCloud[static_cast<size_t>(channel)] * feedback;
            primaryDelay[static_cast<size_t>(channel)][
                static_cast<size_t>(primaryWritePosition)] =
                    std::isfinite(writeValue) ? writeValue : 0.0f;
        }
        if (++primaryWritePosition
            >= static_cast<int>(primaryDelay[0].size()))
            primaryWritePosition = 0;

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
                (dry[index] * dryGain + cloud[index] * wetGain) * output;
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

double DiffusionDelayModule::tailSeconds(
    const ControlValues& controls) const
{
    const auto feedback =
        detail::normalizedControl(controls[3], 0.389f) * 0.90f;
    const auto freeDelay = detail::exponential(
        0.010f, 2.0f, detail::normalizedControl(controls[0], 0.74f));
    const auto primary = detail::normalizedControl(controls[1], 1.0f) >= 0.5f
                             ? 4.0f : freeDelay;
    const auto amount = detail::normalizedControl(controls[4], 0.30f);
    const auto coefficient = 0.18f + amount * 0.52f;
    const auto diffuserTail = amount <= 0.001f ? 0.0
        : 0.080
            + 0.050 * juce::jmax(
                0.0, std::log(0.001 / static_cast<double>(amount))
                         / std::log(static_cast<double>(coefficient)));
    if (feedback <= 0.0001f)
        return primary + diffuserTail;
    const auto repeats = std::log(0.001) / std::log(
        static_cast<double>(feedback));
    return juce::jmin(
        120.0, (primary + amount * 0.080) * repeats + diffuserTail);
}
} // namespace megadsp
