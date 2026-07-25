#include "MidnightCurrent.h"

#include "DspHelpers.h"
#include "DspSafety.h"

#include <cmath>

namespace megadsp
{
namespace
{
constexpr float twoPi = juce::MathConstants<float>::twoPi;

float normalizedDrive(float input, float drive) noexcept
{
    const auto denominator = std::tanh(drive);
    return denominator > 0.0001f
        ? std::tanh(input * drive) / denominator : input;
}
} // namespace

void MidnightCurrentModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    centerSmoothed.reset(sampleRate, 0.05);
    rateSmoothed.reset(sampleRate, 0.08);
    outputSmoothed.reset(sampleRate, 0.03);
    for (auto* smoother : {
             &sweepSmoothed, &phaseDepthSmoothed, &regenerationSmoothed,
             &resonanceSmoothed, &warmthSmoothed, &softnessSmoothed,
             &driftSmoothed, &spreadSmoothed, &mixSmoothed })
        smoother->reset(sampleRate, 0.04);
    reset();
}

void MidnightCurrentModule::reset()
{
    channels = {};
    motionPhase = 0.0;
    driftPhase = 0.0;
    meterEnvelope = 0.0f;
    initialized = false;
    telemetrySequence = 0;
    telemetry.clear();
    meter.store(0.0f, std::memory_order_relaxed);
}

float MidnightCurrentModule::renderChannel(
    int channel, float input, float phase, float cutoffHz, float filterSweep,
    float phaseDepth, float regeneration, float resonance, float warmth,
    float softness, float drift) noexcept
{
    auto& state = channels[static_cast<size_t>(channel)];
    const auto softenerCutoff = detail::lerp(18000.0f, 3200.0f, softness);
    const auto softenerCoefficient = 1.0f - std::exp(
        -twoPi * softenerCutoff / static_cast<float>(sampleRate));
    state.softener += softenerCoefficient * (input - state.softener);

    const auto drive = 1.0f + warmth * 5.0f;
    const auto preamp = normalizedDrive(state.softener, drive);
    const auto primaryLfo = std::sin(twoPi * phase);
    const auto driftLfo = std::sin(
        twoPi * static_cast<float>(driftPhase + channel * 0.173));
    const auto modulation = primaryLfo + drift * 0.22f * driftLfo;
    const auto movingCutoff = juce::jlimit(
        45.0f, static_cast<float>(sampleRate * 0.42),
        cutoffHz * std::exp2(filterSweep * 1.75f * modulation));

    const auto g = std::tan(
        juce::MathConstants<float>::pi * movingCutoff
        / static_cast<float>(sampleRate));
    const auto damping = detail::lerp(1.75f, 0.18f, resonance);
    const auto denominator = 1.0f + g * (g + damping);
    const auto band = (
        state.filterIntegrator1
        + g * (preamp - state.filterIntegrator2)) / denominator;
    const auto low = state.filterIntegrator2 + g * band;
    state.filterIntegrator1 = detail::finiteSample(
        2.0f * band - state.filterIntegrator1);
    state.filterIntegrator2 = detail::finiteSample(
        2.0f * low - state.filterIntegrator2);
    const auto filtered = detail::finiteSample(
        low + band * (0.10f + 0.14f * resonance));

    auto phased = filtered + state.feedback * regeneration * 0.72f;
    for (int stage = 0; stage < stageCount; ++stage)
    {
        const auto position =
            static_cast<float>(stage) / static_cast<float>(stageCount - 1);
        const auto stageOffset = (position - 0.5f) * 2.2f;
        const auto stageMotion =
            phaseDepth * (0.45f + 0.55f * position) * modulation;
        const auto frequency = juce::jlimit(
            35.0f, static_cast<float>(sampleRate * 0.42),
            cutoffHz * std::exp2(stageOffset + stageMotion * 2.1f));
        const auto tangent = std::tan(
            juce::MathConstants<float>::pi * frequency
            / static_cast<float>(sampleRate));
        const auto coefficient = (tangent - 1.0f) / (tangent + 1.0f);
        auto& memory = state.allpass[static_cast<size_t>(stage)];
        const auto output = coefficient * phased + memory;
        memory = detail::finiteSample(phased - coefficient * output);
        phased = detail::finiteSample(output);
    }
    state.feedback += 0.12f * (phased - state.feedback);
    state.feedback = detail::finiteSample(state.feedback);

    const auto body = 0.58f * filtered + 0.50f * phased;
    return detail::finiteSample(normalizedDrive(body, 1.0f + warmth * 1.5f));
}

void MidnightCurrentModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() <= 0 || buffer.getNumSamples() <= 0)
        return;

    const auto centerTarget = detail::exponential(
        120.0f, 3000.0f,
        detail::normalizedControl(controls[filterCenterControl], 0.456f));
    const auto sweepTarget =
        detail::normalizedControl(controls[filterSweepControl], 0.70f);
    const auto rateTarget = detail::exponential(
        0.02f, 1.2f,
        detail::normalizedControl(controls[motionRateControl], 0.417f));
    const auto phaseDepthTarget =
        detail::normalizedControl(controls[phaseDepthControl], 0.78f);
    const auto regenerationTarget = detail::lerp(
        -0.80f, 0.80f,
        detail::normalizedControl(controls[regenerationControl], 0.738f));
    const auto resonanceTarget =
        detail::normalizedControl(controls[resonanceControl], 0.55f);
    const auto warmthTarget =
        detail::normalizedControl(controls[warmthControl], 0.45f);
    const auto softnessTarget =
        detail::normalizedControl(controls[softnessControl], 0.55f);
    const auto driftTarget =
        detail::normalizedControl(controls[driftControl], 0.15f);
    const auto spreadTarget =
        detail::normalizedControl(controls[stereoSpreadControl], 0.55f);
    const auto mixTarget =
        detail::normalizedControl(controls[mixControl], 0.70f);
    const auto outputTarget = juce::Decibels::decibelsToGain(detail::lerp(
        -18.0f, 12.0f,
        detail::normalizedControl(controls[outputControl], 0.60f)));

    auto setTarget = [this](auto& smoother, float target)
    {
        if (initialized)
            smoother.setTargetValue(target);
        else
            smoother.setCurrentAndTargetValue(target);
    };
    setTarget(centerSmoothed, centerTarget);
    setTarget(sweepSmoothed, sweepTarget);
    setTarget(rateSmoothed, rateTarget);
    setTarget(phaseDepthSmoothed, phaseDepthTarget);
    setTarget(regenerationSmoothed, regenerationTarget);
    setTarget(resonanceSmoothed, resonanceTarget);
    setTarget(warmthSmoothed, warmthTarget);
    setTarget(softnessSmoothed, softnessTarget);
    setTarget(driftSmoothed, driftTarget);
    setTarget(spreadSmoothed, spreadTarget);
    setTarget(mixSmoothed, mixTarget);
    setTarget(outputSmoothed, outputTarget);
    initialized = true;

    const auto activeChannels = juce::jmin(2, buffer.getNumChannels());
    float latestCutoff = centerTarget;
    float latestDepth = phaseDepthTarget;
    float latestDrive = 1.0f + warmthTarget * 5.0f;
    double wetEnergyAccumulator = 0.0;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const std::array<float, 2> dry {
            detail::finiteSample(buffer.getSample(0, sample)),
            activeChannels > 1
                ? detail::finiteSample(buffer.getSample(1, sample))
                : detail::finiteSample(buffer.getSample(0, sample))
        };
        const auto center = centerSmoothed.getNextValue();
        const auto sweep = sweepSmoothed.getNextValue();
        const auto rate = rateSmoothed.getNextValue();
        const auto phaseDepth = phaseDepthSmoothed.getNextValue();
        const auto regeneration = regenerationSmoothed.getNextValue();
        const auto resonance = resonanceSmoothed.getNextValue();
        const auto warmth = warmthSmoothed.getNextValue();
        const auto softness = softnessSmoothed.getNextValue();
        const auto drift = driftSmoothed.getNextValue();
        const auto spread = spreadSmoothed.getNextValue();
        const auto mix = mixSmoothed.getNextValue();
        const auto output = outputSmoothed.getNextValue();
        latestDepth = phaseDepth;
        latestDrive = 1.0f + warmth * 5.0f;

        const auto leftPhase = static_cast<float>(motionPhase);
        auto rightPhase = leftPhase + 0.37f;
        rightPhase -= std::floor(rightPhase);
        const std::array<float, 2> rendered {
            renderChannel(
                0, dry[0], leftPhase, center, sweep, phaseDepth,
                regeneration, resonance, warmth, softness, drift),
            renderChannel(
                1, dry[1], rightPhase, center, sweep, phaseDepth,
                regeneration, resonance, warmth, softness, drift)
        };
        const auto wetMid = 0.5f * (rendered[0] + rendered[1]);
        const auto wetSide = 0.5f * (rendered[0] - rendered[1]) * spread;
        const std::array<float, 2> wet { wetMid + wetSide, wetMid - wetSide };
        wetEnergyAccumulator += 0.5 * (
            static_cast<double>(wet[0]) * wet[0]
            + static_cast<double>(wet[1]) * wet[1]);

        const auto dryGain =
            std::cos(mix * juce::MathConstants<float>::halfPi);
        const auto wetGain =
            std::sin(mix * juce::MathConstants<float>::halfPi);
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto index = static_cast<size_t>(juce::jmin(channel, 1));
            const auto wetSample = activeChannels == 1 ? wetMid : wet[index];
            buffer.setSample(
                channel, sample,
                detail::finiteSample(
                    output * (dry[index] * dryGain + wetSample * wetGain)));
        }

        const auto lfo = std::sin(twoPi * leftPhase);
        latestCutoff = juce::jlimit(
            45.0f, static_cast<float>(sampleRate * 0.42),
            center * std::exp2(sweep * 1.75f * lfo));
        motionPhase += static_cast<double>(rate) / sampleRate;
        motionPhase -= std::floor(motionPhase);
        driftPhase += static_cast<double>(rate) * 0.173 / sampleRate;
        driftPhase -= std::floor(driftPhase);
    }

    const auto wetRms = static_cast<float>(std::sqrt(
        wetEnergyAccumulator / juce::jmax(1, buffer.getNumSamples())));
    meterEnvelope += 0.18f * (wetRms - meterEnvelope);
    meter.store(
        juce::jlimit(0.0f, 1.0f, meterEnvelope),
        std::memory_order_relaxed);

    if (environment.captureTelemetry)
    {
        ContinuousTelemetrySnapshot snapshot;
        snapshot.sequence = ++telemetrySequence;
        snapshot.valueCount = telemetryValueCount;
        snapshot.values[leftPhase] = static_cast<float>(motionPhase);
        snapshot.values[rightPhase] = static_cast<float>(
            std::fmod(motionPhase + 0.37, 1.0));
        snapshot.values[actualCutoffHz] = latestCutoff;
        snapshot.values[phaseIntensity] = latestDepth;
        snapshot.values[wetEnergy] = wetRms;
        snapshot.values[driveAmount] = latestDrive;
        telemetry.publish(snapshot);
    }
}

double MidnightCurrentModule::tailSeconds(
    const ControlValues& controls) const
{
    const auto regeneration = std::abs(detail::lerp(
        -0.80f, 0.80f,
        detail::normalizedControl(controls[regenerationControl], 0.738f)))
        * 0.72f;
    return regeneration <= 0.001f
        ? 0.05
        : juce::jlimit(
              0.05, 2.0,
              0.012 * std::log(0.001) / std::log(regeneration));
}

bool MidnightCurrentModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}
} // namespace megadsp
