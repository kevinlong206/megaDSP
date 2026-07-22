#include "StudioPhaser.h"
#include "DspHelpers.h"
#include "DspSafety.h"

#include <cmath>

namespace megadsp
{
using detail::exponential;
using detail::lerp;

namespace
{
constexpr std::array<int, StudioPhaserModule::topologyCount> stageCounts {
    2, 4, 6, 8, 12
};
constexpr std::array<float, 8> beatsPerCycle {
    16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.75f, 0.25f
};
}

void StudioPhaserModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    for (auto& smoother : topologyMix)
        smoother.reset(sampleRate, 0.035);
    for (auto* smoother : {
             &rateSmoothed, &depthSmoothed, &feedbackSmoothed,
             &stereoPhaseSmoothed, &mixSmoothed })
        smoother->reset(sampleRate, 0.025);
    centreSmoothed.reset(sampleRate, 0.035);
    sweepSmoothed.reset(sampleRate, 0.035);
    outputSmoothed.reset(sampleRate, 0.025);
    reset();
}

void StudioPhaserModule::reset()
{
    for (auto& topology : states)
        for (auto& channel : topology)
            channel.fill(0.0f);
    for (auto& topology : feedbackState)
        topology.fill(0.0f);
    phase = 0.0f;
    activeTopology = 2;
    initialized = false;
    telemetrySequence = 0;
    telemetry.clear();
}

float StudioPhaserModule::renderTopology(
    int topology, int channel, float input, float centre,
    float sweep, float modulation)
{
    const auto stages = stageCounts[static_cast<size_t>(topology)];
    auto sample = input;
    auto& topologyStates = states[static_cast<size_t>(topology)]
                                 [static_cast<size_t>(channel)];
    for (int stage = 0; stage < stages; ++stage)
    {
        const auto position = stages > 1
            ? static_cast<float>(stage) / static_cast<float>(stages - 1)
                  - 0.5f
            : 0.0f;
        const auto stageFrequency = juce::jlimit(
            18.0f, static_cast<float>(sampleRate * 0.45),
            centre * std::pow(2.0f, sweep * (0.32f * position + modulation)));
        const auto tangent = std::tan(
            juce::MathConstants<float>::pi * stageFrequency
            / static_cast<float>(sampleRate));
        const auto coefficient = (tangent - 1.0f) / (tangent + 1.0f);
        auto& state = topologyStates[static_cast<size_t>(stage)];
        const auto output = coefficient * sample + state;
        state = sample - coefficient * output;
        sample = output;
    }
    return sample;
}

void StudioPhaserModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    if (buffer.getNumChannels() == 0)
        return;

    const auto topology = discreteIndex(
        detail::normalizedControl(controls[0], 0.0f), topologyCount);
    const auto freeRate = exponential(
        0.02f, 12.0f, detail::normalizedControl(controls[1], 0.0f));
    const auto division = beatsPerCycle[static_cast<size_t>(
        discreteIndex(detail::normalizedControl(controls[3], 0.0f),
                      static_cast<int>(beatsPerCycle.size())))];
    const auto bpm = static_cast<float>(
        juce::jlimit(20.0, 400.0,
            std::isfinite(environment.bpm) && environment.bpm > 0.0
                ? environment.bpm : 120.0));
    const auto rate = detail::normalizedControl(controls[2], 0.0f) >= 0.5f
        ? bpm / (60.0f * division) : freeRate;
    const auto centre = exponential(
        80.0f, 8000.0f, detail::normalizedControl(controls[5], 0.5f));
    const auto sweep = exponential(
        0.25f, 6.0f, detail::normalizedControl(controls[6], 0.5f));
    const auto feedback = lerp(
        -0.95f, 0.95f, detail::normalizedControl(controls[7], 0.5f));
    const auto stereoPhase = lerp(
        0.0f, 0.5f, detail::normalizedControl(controls[8], 0.0f));
    const auto output = juce::Decibels::decibelsToGain(
        lerp(-18.0f, 12.0f,
             detail::normalizedControl(controls[10], 0.6f)));

    for (int index = 0; index < topologyCount; ++index)
    {
        const auto target = index == topology ? 1.0f : 0.0f;
        if (!initialized)
            topologyMix[static_cast<size_t>(index)]
                .setCurrentAndTargetValue(target);
        else
            topologyMix[static_cast<size_t>(index)].setTargetValue(target);
    }
    auto setTarget = [this](auto& smoother, float target)
    {
        if (!initialized)
            smoother.setCurrentAndTargetValue(target);
        else
            smoother.setTargetValue(target);
    };
    setTarget(rateSmoothed, rate);
    setTarget(depthSmoothed,
              detail::normalizedControl(controls[4], 0.0f));
    setTarget(centreSmoothed, centre);
    setTarget(sweepSmoothed, sweep);
    setTarget(feedbackSmoothed, feedback);
    setTarget(stereoPhaseSmoothed, stereoPhase);
    setTarget(mixSmoothed,
              detail::normalizedControl(controls[9], 0.0f));
    setTarget(outputSmoothed, output);
    activeTopology = topology;
    initialized = true;

    const auto channelCount = juce::jmin(2, buffer.getNumChannels());
    std::array<float, topologyCount> telemetryTopologyGains {};
    for (int sampleIndex = 0; sampleIndex < buffer.getNumSamples();
         ++sampleIndex)
    {
        const auto currentRate = rateSmoothed.getNextValue();
        const auto depth = depthSmoothed.getNextValue();
        const auto currentCentre = centreSmoothed.getNextValue();
        const auto currentSweep = sweepSmoothed.getNextValue();
        const auto currentFeedback = feedbackSmoothed.getNextValue();
        const auto phaseOffset = stereoPhaseSmoothed.getNextValue();
        const auto mix = mixSmoothed.getNextValue();
        const auto currentOutput = outputSmoothed.getNextValue();
        std::array<float, topologyCount> topologyGains {};
        for (int index = 0; index < topologyCount; ++index)
            topologyGains[static_cast<size_t>(index)] =
                topologyMix[static_cast<size_t>(index)].getNextValue();
        telemetryTopologyGains = topologyGains;

        const std::array<float, 2> dry {
            detail::finiteSample(buffer.getSample(0, sampleIndex)),
            channelCount > 1
                ? detail::finiteSample(buffer.getSample(1, sampleIndex))
                : detail::finiteSample(buffer.getSample(0, sampleIndex))
        };
        std::array<float, 2> wet {};
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto channelPhase =
                phase + (channel == 1 ? phaseOffset : 0.0f);
            const auto lfo = std::sin(
                juce::MathConstants<float>::twoPi * channelPhase);
            for (int index = 0; index < topologyCount; ++index)
            {
                auto& feedbackValue =
                    feedbackState[static_cast<size_t>(index)]
                                 [static_cast<size_t>(channel)];
                const auto injected = dry[static_cast<size_t>(channel)]
                    + feedbackValue * currentFeedback * 0.92f;
                const auto rendered = renderTopology(
                    index, channel, injected, currentCentre, currentSweep,
                    0.5f * depth * lfo);
                feedbackValue = detail::finiteSample(
                    feedbackValue + 0.18f * (rendered - feedbackValue));
                wet[static_cast<size_t>(channel)] +=
                    topologyGains[static_cast<size_t>(index)] * rendered;
            }
        }

        const auto dryGain = std::cos(
            mix * juce::MathConstants<float>::halfPi);
        const auto wetGain = std::sin(
            mix * juce::MathConstants<float>::halfPi);
        for (int channel = 0; channel < channelCount; ++channel)
        {
            const auto wetSample = channelCount == 1
                ? 0.5f * (wet[0] + wet[1])
                : wet[static_cast<size_t>(channel)];
            buffer.setSample(
                channel, sampleIndex,
                detail::finiteSample(
                    currentOutput
                    * (dryGain * dry[static_cast<size_t>(channel)]
                       + wetGain * wetSample)));
        }
        phase += currentRate / static_cast<float>(sampleRate);
        phase -= std::floor(phase);
    }

    if (environment.captureTelemetry)
    {
        ContinuousTelemetrySnapshot snapshot;
        snapshot.sequence = ++telemetrySequence;
        snapshot.valueCount = telemetryValueCount;
        snapshot.values[leftPhase] = phase;
        snapshot.values[rightPhase] =
            phase + stereoPhaseSmoothed.getCurrentValue();
        snapshot.values[rightPhase] -=
            std::floor(snapshot.values[rightPhase]);
        snapshot.values[targetTopology] = static_cast<float>(activeTopology);
        for (int index = 0; index < topologyCount; ++index)
            snapshot.values[static_cast<size_t>(topologyMix0 + index)] =
                telemetryTopologyGains[static_cast<size_t>(index)];
        telemetry.publish(snapshot);
    }
}

bool StudioPhaserModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}

double StudioPhaserModule::tailSeconds(const ControlValues& controls) const
{
    const auto feedback =
        std::abs(lerp(-0.95f, 0.95f,
                      detail::normalizedControl(controls[7], 0.5f))) * 0.92f;
    return feedback < 0.001f
        ? 0.05
        : juce::jlimit(0.05, 3.0,
              0.025 * std::log(0.001) / std::log(feedback));
}
} // namespace megadsp
