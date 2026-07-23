#include "ChaosField.h"
#include "DspHelpers.h"
#include "DspSafety.h"

#include <cmath>

namespace megadsp
{
namespace
{
constexpr std::array<float, 8> divisionBeats {
    4.0f, 2.0f, 1.5f, 1.0f, 0.75f, 0.5f, 0.375f, 0.25f
};

float outputGain(float normalized) noexcept
{
    const auto gainDb = detail::lerp(-18.0f, 12.0f, normalized);
    return std::abs(gainDb) < 0.0001f
        ? 1.0f : juce::Decibels::decibelsToGain(gainDb);
}

double wrapAngle(double angle) noexcept
{
    return std::remainder(angle, juce::MathConstants<double>::twoPi);
}
} // namespace

float ChaosFieldModule::StateVariableFilter::process(
    float input, float cutoff, double rate) noexcept
{
    const auto safeCutoff = juce::jlimit(
        25.0f, static_cast<float>(rate * 0.42), cutoff);
    const auto g = std::tan(
        juce::MathConstants<float>::pi * safeCutoff
        / static_cast<float>(rate));
    constexpr float damping = 1.15f;
    const auto inverse = 1.0f / (1.0f + g * (g + damping));
    const auto a2 = g * inverse;
    const auto a3 = g * a2;
    const auto v3 = input - integrator2;
    const auto v1 = inverse * integrator1 + a2 * v3;
    const auto v2 = integrator2 + a2 * integrator1 + a3 * v3;
    integrator1 = 2.0f * v1 - integrator1;
    integrator2 = 2.0f * v2 - integrator2;
    if (std::isfinite(v2) && std::isfinite(integrator1)
        && std::isfinite(integrator2))
        return v2;
    reset();
    return 0.0f;
}

void ChaosFieldModule::StateVariableFilter::reset() noexcept
{
    integrator1 = 0.0f;
    integrator2 = 0.0f;
}

void ChaosFieldModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    const auto capacity = static_cast<size_t>(
        juce::jmax(16, juce::roundToInt(sampleRate * 2.55)));
    for (auto& channel : delayBuffer)
        channel.assign(capacity, 0.0f);
    for (auto& smoother : attractorMix)
        smoother.reset(sampleRate, 0.055);
    rateSmoothed.reset(sampleRate, 0.06);
    depthSmoothed.reset(sampleRate, 0.04);
    filterCenterSmoothed.reset(sampleRate, 0.05);
    delayCenterSmoothed.reset(sampleRate, 0.06);
    feedbackSmoothed.reset(sampleRate, 0.05);
    panOrbitSmoothed.reset(sampleRate, 0.04);
    stereoSpreadSmoothed.reset(sampleRate, 0.04);
    mixSmoothed.reset(sampleRate, 0.03);
    outputSmoothed.reset(sampleRate, 0.03);
    reset();
}

void ChaosFieldModule::reset()
{
    for (auto& channel : delayBuffer)
        std::fill(channel.begin(), channel.end(), 0.0f);
    for (auto& filter : inputFilter)
        filter.reset();
    feedbackFilterState.fill(0.0f);
    attractors[0] = { 0.71, 1.13, 18.7, 0.0 };
    attractors[1] = { 0.17, -0.31, 0.23, 0.0 };
    attractors[2] = { 1.83, 0.0, -0.74, 0.0 };
    writePosition = 0;
    activeAttractor = 0;
    initialized = false;
    outputMeter.store(0.0f, std::memory_order_relaxed);
    detectorMeter.store(-100.0f, std::memory_order_relaxed);
    telemetryState = {};
    telemetry.clear();
}

void ChaosFieldModule::validateAttractor(int attractor) noexcept
{
    auto& state = attractors[static_cast<size_t>(attractor)];
    if (std::isfinite(state.x) && std::isfinite(state.y)
        && std::isfinite(state.z) && std::isfinite(state.w))
        return;
    if (attractor == 0)
        state = { 0.71, 1.13, 18.7, 0.0 };
    else if (attractor == 1)
        state = { 0.17, -0.31, 0.23, 0.0 };
    else
        state = { 1.83, 0.0, -0.74, 0.0 };
}

void ChaosFieldModule::stepAttractors(float rate) noexcept
{
    const auto boundedRate = juce::jlimit(0.005f, 12.0f, rate);

    {
        auto& state = attractors[0];
        const auto dt = static_cast<double>(boundedRate) * 4.5 / sampleRate;
        const auto derivative = [](const AttractorState& value)
        {
            return AttractorState {
                10.0 * (value.y - value.x),
                value.x * (28.0 - value.z) - value.y,
                value.x * value.y - (8.0 / 3.0) * value.z,
                0.0
            };
        };
        const auto first = derivative(state);
        const AttractorState midpoint {
            state.x + first.x * dt * 0.5,
            state.y + first.y * dt * 0.5,
            state.z + first.z * dt * 0.5,
            0.0
        };
        const auto second = derivative(midpoint);
        state.x += second.x * dt;
        state.y += second.y * dt;
        state.z += second.z * dt;
        state.x = juce::jlimit(-60.0, 60.0, state.x);
        state.y = juce::jlimit(-70.0, 70.0, state.y);
        state.z = juce::jlimit(0.0, 90.0, state.z);
    }

    {
        auto& state = attractors[1];
        const auto dt = static_cast<double>(boundedRate) * 5.2 / sampleRate;
        const auto derivative = [](const AttractorState& value)
        {
            return AttractorState {
                -value.y - value.z,
                value.x + 0.2 * value.y,
                0.2 + value.z * (value.x - 5.7),
                0.0
            };
        };
        const auto first = derivative(state);
        const AttractorState midpoint {
            state.x + first.x * dt * 0.5,
            state.y + first.y * dt * 0.5,
            state.z + first.z * dt * 0.5,
            0.0
        };
        const auto second = derivative(midpoint);
        state.x += second.x * dt;
        state.y += second.y * dt;
        state.z += second.z * dt;
        state.x = juce::jlimit(-35.0, 35.0, state.x);
        state.y = juce::jlimit(-35.0, 35.0, state.y);
        state.z = juce::jlimit(0.0, 45.0, state.z);
    }

    {
        auto& state = attractors[2];
        const auto dt = static_cast<double>(boundedRate) * 2.6 / sampleRate;
        const auto derivative = [](const AttractorState& value)
        {
            constexpr double gravity = 9.81;
            const auto delta = value.z - value.x;
            const auto cosine = std::cos(delta);
            const auto sine = std::sin(delta);
            const auto denominator1 = 2.0 - cosine * cosine;
            const auto acceleration1 =
                (gravity * std::sin(value.z) * cosine
                 - sine * (value.w * value.w
                           + gravity * 2.0 * std::cos(value.x)))
                / denominator1;
            const auto acceleration2 =
                (2.0 * sine
                 * (value.y * value.y
                    + gravity * std::cos(value.x)
                    + value.w * value.w * cosine * 0.5))
                / denominator1;
            return AttractorState {
                value.y,
                acceleration1 - value.y * 0.0008,
                value.w,
                acceleration2 - value.w * 0.0008
            };
        };
        const auto first = derivative(state);
        const AttractorState midpoint {
            state.x + first.x * dt * 0.5,
            state.y + first.y * dt * 0.5,
            state.z + first.z * dt * 0.5,
            state.w + first.w * dt * 0.5
        };
        const auto second = derivative(midpoint);
        state.x = wrapAngle(state.x + second.x * dt);
        state.y = juce::jlimit(-14.0, 14.0, state.y + second.y * dt);
        state.z = wrapAngle(state.z + second.z * dt);
        state.w = juce::jlimit(-14.0, 14.0, state.w + second.w * dt);
    }

    for (int attractor = 0; attractor < attractorCount; ++attractor)
        validateAttractor(attractor);
}

std::array<float, 3> ChaosFieldModule::normalizedAxes(
    int attractor) const noexcept
{
    const auto index = juce::jlimit(0, attractorCount - 1, attractor);
    const auto& state = attractors[static_cast<size_t>(index)];
    if (index == 0)
        return {
            static_cast<float>(std::tanh(state.x / 18.0)),
            static_cast<float>(std::tanh(state.y / 23.0)),
            static_cast<float>(std::tanh((state.z - 24.0) / 18.0))
        };
    if (index == 1)
        return {
            static_cast<float>(std::tanh(state.x / 7.0)),
            static_cast<float>(std::tanh(state.y / 7.0)),
            static_cast<float>(std::tanh((state.z - 3.0) / 5.0))
        };
    return {
        static_cast<float>(std::sin(state.x)),
        static_cast<float>(std::sin(state.z)),
        static_cast<float>(std::tanh((state.y - state.w) / 4.0))
    };
}

float ChaosFieldModule::readDelay(
    int channel, float delaySamples) const noexcept
{
    const auto& history =
        delayBuffer[static_cast<size_t>(juce::jlimit(0, 1, channel))];
    const auto size = static_cast<int>(history.size());
    auto position = static_cast<float>(writePosition)
        - juce::jlimit(2.0f, static_cast<float>(size - 3), delaySamples);
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
    const auto a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    const auto a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const auto a2 = -0.5f * y0 + 0.5f * y2;
    return ((a0 * fraction + a1) * fraction + a2) * fraction + y1;
}

void ChaosFieldModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    const auto channels = juce::jlimit(0, 2, buffer.getNumChannels());
    if (channels == 0 || buffer.getNumSamples() <= 0 || delayBuffer[0].empty())
        return;

    const auto normalized = [&controls](int index, float fallback)
    {
        return detail::normalizedControl(
            controls[static_cast<size_t>(index)], fallback);
    };
    const auto attractor = discreteIndex(
        normalized(attractorControl, 0.0f), attractorCount);
    auto rate = detail::exponential(
        0.015f, 7.0f, normalized(rateControl, 0.36f));
    if (normalized(syncControl, 0.0f) >= 0.5f)
    {
        const auto division =
            discreteIndex(normalized(divisionControl, 0.57f), 8);
        const auto bpm =
            std::isfinite(environment.bpm)
                && environment.bpm >= 20.0 && environment.bpm <= 400.0
            ? environment.bpm : 120.0;
        rate = static_cast<float>(
            bpm / (60.0 * divisionBeats[static_cast<size_t>(division)]));
    }
    const auto depth = normalized(depthControl, 0.50f);
    const auto filterCenter = detail::exponential(
        80.0f, 10000.0f, normalized(filterCenterControl, 0.58f));
    const auto delayCenterMilliseconds = detail::exponential(
        2.0f, 600.0f, normalized(delayCenterControl, 0.53f));
    const auto feedback = detail::lerp(
        -0.88f, 0.88f, normalized(feedbackControl, 0.50f));
    const auto panOrbit = normalized(panOrbitControl, 0.70f);
    const auto stereoSpread =
        normalized(stereoSpreadControl, 0.60f) * 1.75f;
    const auto mix = normalized(mixControl, 0.45f);
    const auto output = outputGain(normalized(outputControl, 0.60f));

    if (!initialized)
    {
        activeAttractor = attractor;
        for (int index = 0; index < attractorCount; ++index)
            attractorMix[static_cast<size_t>(index)].setCurrentAndTargetValue(
                index == attractor ? 1.0f : 0.0f);
        rateSmoothed.setCurrentAndTargetValue(rate);
        depthSmoothed.setCurrentAndTargetValue(depth);
        filterCenterSmoothed.setCurrentAndTargetValue(filterCenter);
        delayCenterSmoothed.setCurrentAndTargetValue(delayCenterMilliseconds);
        feedbackSmoothed.setCurrentAndTargetValue(feedback);
        panOrbitSmoothed.setCurrentAndTargetValue(panOrbit);
        stereoSpreadSmoothed.setCurrentAndTargetValue(stereoSpread);
        mixSmoothed.setCurrentAndTargetValue(mix);
        outputSmoothed.setCurrentAndTargetValue(output);
        initialized = true;
    }
    if (attractor != activeAttractor)
    {
        activeAttractor = attractor;
        for (int index = 0; index < attractorCount; ++index)
            attractorMix[static_cast<size_t>(index)].setTargetValue(
                index == attractor ? 1.0f : 0.0f);
    }
    rateSmoothed.setTargetValue(rate);
    depthSmoothed.setTargetValue(depth);
    filterCenterSmoothed.setTargetValue(filterCenter);
    delayCenterSmoothed.setTargetValue(delayCenterMilliseconds);
    feedbackSmoothed.setTargetValue(feedback);
    panOrbitSmoothed.setTargetValue(panOrbit);
    stereoSpreadSmoothed.setTargetValue(stereoSpread);
    mixSmoothed.setTargetValue(mix);
    outputSmoothed.setTargetValue(output);

    const auto feedbackFilterCoefficient = 1.0f - std::exp(
        -juce::MathConstants<float>::twoPi
        * juce::jmin(12000.0f, static_cast<float>(sampleRate * 0.40))
        / static_cast<float>(sampleRate));
    double feedbackEnergy = 0.0;
    double wetEnergy = 0.0;
    double outputEnergy = 0.0;
    std::array<float, 3> telemetryAxes {};
    float telemetryFilter = filterCenter;
    float telemetryDelay = delayCenterMilliseconds;
    float telemetryPan = 0.0f;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const std::array<float, 2> dry {
            detail::finiteSample(buffer.getSample(0, sample)),
            channels > 1
                ? detail::finiteSample(buffer.getSample(1, sample))
                : detail::finiteSample(buffer.getSample(0, sample))
        };
        const auto inputMid = 0.5f * (dry[0] + dry[1]);
        const auto inputSide =
            channels > 1 ? 0.5f * (dry[0] - dry[1]) : 0.0f;

        const auto currentRate = rateSmoothed.getNextValue();
        stepAttractors(currentRate);
        std::array<float, attractorCount> weights {};
        float weightSum = 0.0f;
        for (int index = 0; index < attractorCount; ++index)
        {
            weights[static_cast<size_t>(index)] =
                attractorMix[static_cast<size_t>(index)].getNextValue();
            weightSum += weights[static_cast<size_t>(index)];
        }
        const auto inverseWeight =
            weightSum > 0.000001f ? 1.0f / weightSum : 1.0f;
        std::array<float, 3> axes {};
        for (int index = 0; index < attractorCount; ++index)
        {
            const auto modelAxes = normalizedAxes(index);
            const auto weight =
                weights[static_cast<size_t>(index)] * inverseWeight;
            for (size_t axis = 0; axis < axes.size(); ++axis)
                axes[axis] += modelAxes[axis] * weight;
        }

        const auto currentDepth = depthSmoothed.getNextValue();
        const auto cutoff = juce::jlimit(
            25.0f, static_cast<float>(sampleRate * 0.42),
            filterCenterSmoothed.getNextValue()
                * std::exp2(axes[0] * currentDepth * 3.0f));
        const auto delayMilliseconds =
            delayCenterSmoothed.getNextValue()
            * std::exp2(axes[1] * currentDepth * 2.0f);
        const auto delaySamples = juce::jlimit(
            2.0f, static_cast<float>(delayBuffer[0].size() - 3),
            delayMilliseconds * 0.001f * static_cast<float>(sampleRate));
        const auto currentFeedback = feedbackSmoothed.getNextValue();

        const std::array<float, 2> filteredInput {
            inputFilter[0].process(inputMid, cutoff, sampleRate),
            inputFilter[1].process(inputSide, cutoff, sampleRate)
        };
        std::array<float, 2> delayed {
            detail::finiteSample(readDelay(0, delaySamples)),
            detail::finiteSample(readDelay(1, delaySamples))
        };
        for (int channel = 0; channel < 2; ++channel)
        {
            auto& feedbackState =
                feedbackFilterState[static_cast<size_t>(channel)];
            feedbackState += feedbackFilterCoefficient
                * (delayed[static_cast<size_t>(channel)] - feedbackState);
            if (!std::isfinite(feedbackState))
                feedbackState = 0.0f;
            const auto feedbackSignal = feedbackState * currentFeedback;
            feedbackEnergy +=
                static_cast<double>(feedbackSignal) * feedbackSignal;
            const auto writeValue =
                filteredInput[static_cast<size_t>(channel)] + feedbackSignal;
            delayBuffer[static_cast<size_t>(channel)]
                       [static_cast<size_t>(writePosition)] =
                detail::finiteSample(writeValue);
        }
        if (++writePosition >= static_cast<int>(delayBuffer[0].size()))
            writePosition = 0;

        const auto pan =
            juce::jlimit(-1.0f, 1.0f, axes[2])
            * currentDepth * panOrbitSmoothed.getNextValue();
        const auto spread = stereoSpreadSmoothed.getNextValue();
        const auto wetMid = delayed[0];
        const auto wetSide = (delayed[1] + delayed[0] * pan) * spread;
        const std::array<float, 2> wet {
            wetMid + wetSide,
            wetMid - wetSide
        };
        wetEnergy += static_cast<double>(wetMid) * wetMid
                     + static_cast<double>(wetSide) * wetSide;

        const auto currentMix = mixSmoothed.getNextValue();
        const auto currentOutput = outputSmoothed.getNextValue();
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto wetSample =
                channels > 1 ? wet[static_cast<size_t>(channel)] : wetMid;
            const auto result = detail::finiteSample(
                (dry[static_cast<size_t>(channel)]
                 + (wetSample - dry[static_cast<size_t>(channel)])
                       * currentMix)
                * currentOutput);
            buffer.setSample(channel, sample, result);
            outputEnergy += static_cast<double>(result) * result;
        }

        telemetryAxes = axes;
        telemetryFilter = cutoff;
        telemetryDelay =
            delaySamples * 1000.0f / static_cast<float>(sampleRate);
        telemetryPan = pan;
    }

    const auto sampleCount = static_cast<double>(
        juce::jmax(1, buffer.getNumSamples()));
    const auto currentFeedbackRms = static_cast<float>(
        std::sqrt(feedbackEnergy / (sampleCount * 2.0)));
    const auto currentWetRms =
        static_cast<float>(std::sqrt(wetEnergy / sampleCount));
    const auto currentOutputRms = static_cast<float>(std::sqrt(
        outputEnergy / (sampleCount * static_cast<double>(channels))));
    outputMeter.store(currentOutputRms, std::memory_order_relaxed);
    detectorMeter.store(
        juce::Decibels::gainToDecibels(currentWetRms, -100.0f),
        std::memory_order_relaxed);

    if (environment.captureTelemetry)
    {
        ++telemetryState.sequence;
        telemetryState.valueCount = telemetryValueCount;
        telemetryState.values[actualX] = telemetryAxes[0];
        telemetryState.values[actualY] = telemetryAxes[1];
        telemetryState.values[actualZ] = telemetryAxes[2];
        telemetryState.values[actualFilterHz] = telemetryFilter;
        telemetryState.values[actualDelayMilliseconds] = telemetryDelay;
        telemetryState.values[actualPan] = telemetryPan;
        telemetryState.values[feedbackRms] = currentFeedbackRms;
        telemetryState.values[wetRms] = currentWetRms;
        appendContinuousTelemetryHistory(
            telemetryState,
            { telemetryAxes[0], telemetryAxes[1],
              telemetryAxes[2], currentWetRms },
            telemetryHistoryValueCount);
        telemetry.publish(telemetryState);
    }
}

double ChaosFieldModule::tailSeconds(const ControlValues& controls) const
{
    const auto depth =
        detail::normalizedControl(controls[depthControl], 0.50f);
    const auto centerMilliseconds = detail::exponential(
        2.0f, 600.0f,
        detail::normalizedControl(controls[delayCenterControl], 0.53f));
    const auto maximumDelay =
        centerMilliseconds * std::exp2(depth * 2.0f) * 0.001;
    const auto feedback = std::abs(detail::lerp(
        -0.88f, 0.88f,
        detail::normalizedControl(controls[feedbackControl], 0.50f)));
    if (feedback < 0.0001f)
        return juce::jlimit(0.002, 2.5, maximumDelay);
    const auto decayRepeats = std::log(0.001) / std::log(feedback);
    return juce::jlimit(
        0.002, 30.0, maximumDelay * (1.0 + decayRepeats));
}

float ChaosFieldModule::meterValue() const
{
    return outputMeter.load(std::memory_order_relaxed);
}

float ChaosFieldModule::detectorValue() const
{
    return detectorMeter.load(std::memory_order_relaxed);
}

bool ChaosFieldModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}
} // namespace megadsp
