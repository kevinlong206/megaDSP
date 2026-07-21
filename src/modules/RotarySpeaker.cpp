#include "RotarySpeaker.h"
#include "DspHelpers.h"

#include <cmath>

namespace megadsp
{
using detail::exponential;
using detail::lerp;

void RotarySpeakerModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    const auto historyCapacity = static_cast<size_t>(
        juce::jmax(64, static_cast<int>(std::ceil(sampleRate * 0.025))));
    hornHistory.assign(historyCapacity, 0.0f);
    drumHistory.assign(historyCapacity, 0.0f);
    const auto roomCapacity = static_cast<size_t>(
        juce::jmax(64, static_cast<int>(std::ceil(sampleRate * 0.04))));
    for (auto& channel : roomHistory)
        channel.assign(roomCapacity, 0.0f);
    for (auto* smoother : {
             &driveSmoothed, &balanceSmoothed, &crossoverSmoothed,
             &dopplerSmoothed, &distanceSmoothed, &spreadSmoothed,
             &cabinetSmoothed, &roomSmoothed, &mixSmoothed })
        smoother->reset(sampleRate, 0.025);
    outputSmoothed.reset(sampleRate, 0.025);
    reset();
}

void RotarySpeakerModule::reset()
{
    std::fill(hornHistory.begin(), hornHistory.end(), 0.0f);
    std::fill(drumHistory.begin(), drumHistory.end(), 0.0f);
    for (auto& channel : roomHistory)
        std::fill(channel.begin(), channel.end(), 0.0f);
    cabinetState.fill(0.0f);
    crossoverState = 0.0f;
    previousDrivenInput = 0.0f;
    historyWritePosition = 0;
    roomWritePosition = 0;
    hornPhase = 0.0f;
    drumPhase = juce::MathConstants<float>::pi;
    hornRpm = 0.0f;
    drumRpm = 0.0f;
    driveSmoothed.setCurrentAndTargetValue(6.0f);
    balanceSmoothed.setCurrentAndTargetValue(0.5f);
    crossoverSmoothed.setCurrentAndTargetValue(800.0f);
    dopplerSmoothed.setCurrentAndTargetValue(0.75f);
    distanceSmoothed.setCurrentAndTargetValue(0.7f);
    spreadSmoothed.setCurrentAndTargetValue(
        juce::degreesToRadians(110.0f));
    cabinetSmoothed.setCurrentAndTargetValue(0.65f);
    roomSmoothed.setCurrentAndTargetValue(0.18f);
    mixSmoothed.setCurrentAndTargetValue(1.0f);
    outputSmoothed.setCurrentAndTargetValue(1.0f);
}

float RotarySpeakerModule::readHistory(const std::vector<float>& history,
                                       float delaySamples) const
{
    const auto size = static_cast<int>(history.size());
    auto position = static_cast<float>(historyWritePosition) - delaySamples;
    while (position < 0.0f)
        position += static_cast<float>(size);
    const auto index1 = static_cast<int>(position) % size;
    const auto fraction = position - std::floor(position);
    const auto index0 = (index1 - 1 + size) % size;
    const auto index2 = (index1 + 1) % size;
    const auto index3 = (index1 + 2) % size;
    const auto y0 = history[static_cast<size_t>(index0)];
    const auto y1 = history[static_cast<size_t>(index1)];
    const auto y2 = history[static_cast<size_t>(index2)];
    const auto y3 = history[static_cast<size_t>(index3)];
    const auto a0 = y3 - y2 - y0 + y1;
    const auto a1 = y0 - y1 - a0;
    const auto a2 = y2 - y0;
    return ((a0 * fraction + a1) * fraction + a2) * fraction + y1;
}

float RotarySpeakerModule::rotorSample(
    const std::vector<float>& history, float rotorPhaseValue,
    float microphoneAngle, float radiusMetres,
    float microphoneDistanceMetres, float doppler, bool horn) const
{
    const auto relativeAngle = rotorPhaseValue - microphoneAngle;
    const auto physicalDistance = std::sqrt(
        microphoneDistanceMetres * microphoneDistanceMetres
        + radiusMetres * radiusMetres
        - 2.0f * microphoneDistanceMetres * radiusMetres
              * std::cos(relativeAngle));
    const auto effectiveDistance = microphoneDistanceMetres
        + (physicalDistance - microphoneDistanceMetres) * doppler;
    const auto delaySamples = effectiveDistance / 343.0f
                              * static_cast<float>(sampleRate) + 3.0f;
    const auto directivity = horn
        ? 0.28f + 0.72f * std::pow(0.5f + 0.5f * std::cos(relativeAngle), 1.5f)
        : 0.72f + 0.28f * std::cos(relativeAngle);
    return readHistory(history, delaySamples) * directivity;
}

void RotarySpeakerModule::process(juce::AudioBuffer<float>& buffer,
                                  const ControlValues& controls,
                                  const ProcessEnvironment&)
{
    const auto speed = discreteIndex(controls[0], 3);
    const auto targetHornRpm = speed == 0 ? 0.0f : speed == 1 ? 48.0f : 400.0f;
    const auto targetDrumRpm = speed == 0 ? 0.0f : speed == 1 ? 40.0f : 340.0f;
    driveSmoothed.setTargetValue(lerp(0.0f, 24.0f, controls[1]));
    balanceSmoothed.setTargetValue(controls[2]);
    crossoverSmoothed.setTargetValue(exponential(500.0f, 1400.0f, controls[3]));
    dopplerSmoothed.setTargetValue(controls[4]);
    distanceSmoothed.setTargetValue(lerp(0.2f, 2.0f, controls[5]));
    spreadSmoothed.setTargetValue(juce::degreesToRadians(
        lerp(0.0f, 180.0f, controls[6])));
    cabinetSmoothed.setTargetValue(controls[8]);
    roomSmoothed.setTargetValue(controls[9]);
    mixSmoothed.setTargetValue(controls[10]);
    outputSmoothed.setTargetValue(juce::Decibels::decibelsToGain(
        lerp(-12.0f, 12.0f, controls[11])));
    const auto inertiaScale = lerp(0.4f, 2.5f, controls[7]);

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto hornTime = (targetHornRpm > hornRpm ? 0.75f : 1.15f)
                              * inertiaScale;
        const auto drumTime = (targetDrumRpm > drumRpm ? 2.4f : 3.6f)
                              * inertiaScale;
        hornRpm += (targetHornRpm - hornRpm)
                   * (1.0f - std::exp(-1.0f / static_cast<float>(
                       sampleRate * hornTime)));
        drumRpm += (targetDrumRpm - drumRpm)
                   * (1.0f - std::exp(-1.0f / static_cast<float>(
                       sampleRate * drumTime)));

        const auto dryLeft = buffer.getSample(0, sample);
        const auto dryRight = buffer.getNumChannels() > 1
                                  ? buffer.getSample(1, sample) : dryLeft;
        const auto mid = 0.5f * (dryLeft + dryRight);
        const auto side = 0.5f * (dryLeft - dryRight);
        const auto driveDb = driveSmoothed.getNextValue();
        const auto driveGain = juce::Decibels::decibelsToGain(driveDb);
        const auto driveAmount = driveDb / 24.0f;
        const auto normalization = 1.0f / juce::jmax(0.01f, std::tanh(driveGain));
        const auto midpoint = 0.5f * (previousDrivenInput + mid);
        const auto saturated = 0.5f * (
            std::tanh(midpoint * driveGain)
            + std::tanh(mid * driveGain)) * normalization;
        const auto driven = mid + (saturated - mid) * driveAmount;
        previousDrivenInput = mid;

        const auto crossover = crossoverSmoothed.getNextValue();
        const auto crossoverCoefficient = 1.0f - std::exp(
            -juce::MathConstants<float>::twoPi * crossover
            / static_cast<float>(sampleRate));
        crossoverState += crossoverCoefficient * (driven - crossoverState);
        drumHistory[static_cast<size_t>(historyWritePosition)] = crossoverState;
        hornHistory[static_cast<size_t>(historyWritePosition)] =
            driven - crossoverState;

        const auto balance = balanceSmoothed.getNextValue();
        const auto balanceAngle =
            balance * juce::MathConstants<float>::halfPi;
        const auto drumGain = std::cos(balanceAngle) * 1.2f;
        const auto hornGain = std::sin(balanceAngle) * 1.2f;
        const auto doppler = dopplerSmoothed.getNextValue();
        const auto distance = distanceSmoothed.getNextValue();
        const auto spread = spreadSmoothed.getNextValue();
        const auto cabinet = cabinetSmoothed.getNextValue();
        const auto room = roomSmoothed.getNextValue();
        const auto mix = mixSmoothed.getNextValue();
        const auto output = outputSmoothed.getNextValue();

        std::array<float, 2> wet {};
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto microphoneAngle =
                (channel == 0 ? -0.5f : 0.5f) * spread;
            const auto horn = rotorSample(
                hornHistory, hornPhase, microphoneAngle, 0.17f,
                distance, doppler, true) * hornGain;
            const auto drum = rotorSample(
                drumHistory, drumPhase, microphoneAngle, 0.22f,
                distance, doppler, false) * drumGain;
            auto cabinetWet = horn + drum;
            auto& cabinetLow = cabinetState[static_cast<size_t>(channel)];
            const auto cabinetCoefficient = 1.0f - std::exp(
                -juce::MathConstants<float>::twoPi * 420.0f
                / static_cast<float>(sampleRate));
            cabinetLow += cabinetCoefficient * (cabinetWet - cabinetLow);
            cabinetWet = cabinetWet * (1.0f - 0.22f * cabinet)
                         + cabinetLow * 0.38f * cabinet;
            const auto preservedSide =
                (channel == 0 ? side : -side) * (1.0f - 0.28f * cabinet);
            wet[static_cast<size_t>(channel)] = cabinetWet + preservedSide;
        }

        const auto roomSize = static_cast<int>(roomHistory[0].size());
        const auto tapA = juce::jlimit(
            1, roomSize - 1, static_cast<int>(sampleRate * 0.0073));
        const auto tapB = juce::jlimit(
            1, roomSize - 1, static_cast<int>(sampleRate * 0.0131));
        for (int channel = 0; channel < 2; ++channel)
        {
            auto& history = roomHistory[static_cast<size_t>(channel)];
            const auto reflection =
                history[static_cast<size_t>(
                    (roomWritePosition - tapA + roomSize) % roomSize)] * 0.34f
                + roomHistory[static_cast<size_t>(1 - channel)]
                    [static_cast<size_t>(
                        (roomWritePosition - tapB + roomSize) % roomSize)] * 0.21f;
            history[static_cast<size_t>(roomWritePosition)] =
                wet[static_cast<size_t>(channel)];
            wet[static_cast<size_t>(channel)] += reflection * room;
        }

        buffer.setSample(0, sample,
                         (dryLeft + (wet[0] - dryLeft) * mix) * output);
        if (buffer.getNumChannels() > 1)
            buffer.setSample(1, sample,
                             (dryRight + (wet[1] - dryRight) * mix) * output);

        historyWritePosition = (historyWritePosition + 1)
                               % static_cast<int>(hornHistory.size());
        roomWritePosition = (roomWritePosition + 1)
                            % static_cast<int>(roomHistory[0].size());
        hornPhase += juce::MathConstants<float>::twoPi * hornRpm
                     / (60.0f * static_cast<float>(sampleRate));
        drumPhase += juce::MathConstants<float>::twoPi * drumRpm
                     / (60.0f * static_cast<float>(sampleRate));
        if (hornPhase >= juce::MathConstants<float>::twoPi)
            hornPhase -= juce::MathConstants<float>::twoPi;
        if (drumPhase >= juce::MathConstants<float>::twoPi)
            drumPhase -= juce::MathConstants<float>::twoPi;
    }
}
} // namespace megadsp
