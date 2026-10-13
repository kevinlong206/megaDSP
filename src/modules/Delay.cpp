#include "Delay.h"
#include "DspHelpers.h"
#include "DspSafety.h"

#include <cmath>

namespace megadsp
{
namespace
{
constexpr std::array<float, 12> beatDivisions {
    1.0f / 12.0f, 0.125f, 1.0f / 6.0f, 0.25f, 1.0f / 3.0f,
    0.375f, 0.5f, 2.0f / 3.0f, 0.75f, 1.0f, 1.5f, 2.0f
};
constexpr float maximumDelaySeconds = 2.0f;
constexpr float maximumMovementSeconds = 0.016f;
constexpr float delayLineLimit = 4.0f;

float equalPowerIn(float proportion) noexcept
{
    return std::sin(
        juce::jlimit(0.0f, 1.0f, proportion)
        * juce::MathConstants<float>::halfPi);
}

float equalPowerOut(float proportion) noexcept
{
    return std::cos(
        juce::jlimit(0.0f, 1.0f, proportion)
        * juce::MathConstants<float>::halfPi);
}

float boundedSample(float sample) noexcept
{
    if (!std::isfinite(sample))
        return 0.0f;
    return delayLineLimit * std::tanh(sample / delayLineLimit);
}
} // namespace

void DelayModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    const auto capacity = static_cast<size_t>(std::ceil(
        sampleRate
        * (maximumDelaySeconds + maximumMovementSeconds + 0.05f)));
    for (auto& channel : delayBuffer)
        channel.assign(juce::jmax(size_t { 16 }, capacity), 0.0f);

    feedbackSmoothed.reset(sampleRate, 0.035);
    characterSmoothed.reset(sampleRate, 0.04);
    spreadSmoothed.reset(sampleRate, 0.04);
    movementSmoothed.reset(sampleRate, 0.06);
    toneSmoothed.reset(sampleRate, 0.04);
    duckingSmoothed.reset(sampleRate, 0.04);
    mixSmoothed.reset(sampleRate, 0.025);
    reset();
}

void DelayModule::reset()
{
    for (auto& channel : delayBuffer)
        std::fill(channel.begin(), channel.end(), 0.0f);
    colourStates = {};
    randomState = { 0x9e3779b9u, 0x243f6a88u };
    driftState.fill(0.0f);
    currentDelaySamples.fill(static_cast<float>(sampleRate * 0.25));
    oldDelaySamples = currentDelaySamples;
    targetDelaySamples = currentDelaySamples;
    lastRequestedDelaySamples = currentDelaySamples;
    writePosition = 0;
    wowPhase = 0.0;
    flutterPhase = 0.0;
    timeTransition = 1.0f;
    styleTransition = 1.0f;
    stereoTransition = 1.0f;
    inputEnvelope = 0.0f;
    lastDuckGain = 1.0f;
    activeStyleIndex = 0;
    previousStyleIndex = 0;
    activeStereoMode = 0;
    previousStereoMode = 0;
    parametersInitialised = false;
    telemetrySequence = 0;
    telemetry.clear();
    feedbackSmoothed.setCurrentAndTargetValue(0.0f);
    characterSmoothed.setCurrentAndTargetValue(0.25f);
    spreadSmoothed.setCurrentAndTargetValue(0.5f);
    movementSmoothed.setCurrentAndTargetValue(0.0f);
    toneSmoothed.setCurrentAndTargetValue(0.65f);
    duckingSmoothed.setCurrentAndTargetValue(0.0f);
    mixSmoothed.setCurrentAndTargetValue(0.2f);
}

bool DelayModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}

float DelayModule::readDelay(
    int channel, float delaySamples) const noexcept
{
    const auto& source =
        delayBuffer[static_cast<size_t>(juce::jlimit(0, 1, channel))];
    const auto capacity = static_cast<int>(source.size());
    const auto safeDelay = juce::jlimit(
        3.0f, static_cast<float>(capacity - 3), delaySamples);
    auto position = static_cast<float>(writePosition) - safeDelay;
    while (position < 0.0f)
        position += static_cast<float>(capacity);

    const auto index1 = static_cast<int>(position);
    const auto index0 = index1 > 0 ? index1 - 1 : capacity - 1;
    const auto index2 = index1 + 1 < capacity ? index1 + 1 : 0;
    const auto index3 = index2 + 1 < capacity ? index2 + 1 : 0;
    const auto fraction = position - static_cast<float>(index1);
    const auto y0 = source[static_cast<size_t>(index0)];
    const auto y1 = source[static_cast<size_t>(index1)];
    const auto y2 = source[static_cast<size_t>(index2)];
    const auto y3 = source[static_cast<size_t>(index3)];
    const auto a0 = y3 - y2 - y0 + y1;
    const auto a1 = y0 - y1 - a0;
    const auto a2 = y2 - y0;
    return ((a0 * fraction + a1) * fraction + a2) * fraction + y1;
}

float DelayModule::randomBipolar(int channel) noexcept
{
    auto& state =
        randomState[static_cast<size_t>(juce::jlimit(0, 1, channel))];
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(state & 0x00ffffffu)
            / static_cast<float>(0x00800000u)
        - 1.0f;
}

float DelayModule::movementFor(
    int style, int channel, float amount) noexcept
{
    if (amount <= 0.000001f)
        return 0.0f;

    const auto side = channel == 0 ? 1.0 : -1.0;
    const auto wow = static_cast<float>(
        std::sin(wowPhase + side * 0.47));
    const auto flutter = static_cast<float>(
        std::sin(flutterPhase + side * 1.11));
    auto& drift = driftState[static_cast<size_t>(channel)];
    const auto driftPole = static_cast<float>(
        std::exp(-1.0 / (sampleRate * 0.18)));
    drift = driftPole * drift
        + (1.0f - driftPole) * randomBipolar(channel);

    float milliseconds = 0.0f;
    switch (style)
    {
        case 0:
            milliseconds = amount * (1.8f * wow + 0.35f * drift);
            break;
        case 1:
            milliseconds = amount
                * (4.5f * wow + 1.35f * flutter + 2.0f * drift);
            break;
        case 2:
            milliseconds = amount
                * (2.8f * wow + 0.55f * flutter + 1.2f * drift);
            break;
        default:
            milliseconds = amount
                * (0.45f * wow + 0.20f * flutter + 0.35f * drift);
            break;
    }
    return milliseconds * 0.001f * static_cast<float>(sampleRate);
}

float DelayModule::processColour(
    int style, int channel, float input, float tone,
    float character) noexcept
{
    auto& state = colourStates[static_cast<size_t>(style)]
        [static_cast<size_t>(channel)];
    const auto nyquist = static_cast<float>(sampleRate * 0.475);
    float lowCut = 20.0f;
    float highCut = nyquist;
    float drive = 1.0f;

    switch (style)
    {
        case 0:
            lowCut = 24.0f + 90.0f * (1.0f - tone);
            highCut = detail::exponential(
                4200.0f, nyquist, tone);
            drive = 1.0f + character * 0.45f;
            break;
        case 1:
            lowCut = 45.0f + 180.0f * character;
            highCut = detail::exponential(
                2400.0f, juce::jmin(15000.0f, nyquist),
                tone * (0.85f - 0.25f * character));
            drive = 1.0f + character * 3.2f;
            break;
        case 2:
            lowCut = 70.0f + 260.0f * character;
            highCut = detail::exponential(
                1200.0f, juce::jmin(9500.0f, nyquist),
                tone * (0.72f - 0.22f * character));
            drive = 1.0f + character * 2.1f;
            break;
        default:
            lowCut = 32.0f + 110.0f * character;
            highCut = detail::exponential(
                3000.0f, juce::jmin(17000.0f, nyquist),
                tone * (0.92f - 0.18f * character));
            drive = 1.0f + character * 0.75f;
            break;
    }

    const auto lowpassPole = std::exp(
        -juce::MathConstants<float>::twoPi * highCut
        / static_cast<float>(sampleRate));
    state.lowpass = lowpassPole * state.lowpass
        + (1.0f - lowpassPole) * input;
    const auto highpassPole = std::exp(
        -juce::MathConstants<float>::twoPi * lowCut
        / static_cast<float>(sampleRate));
    state.highpass = highpassPole
            * (state.highpass + state.lowpass - state.highpassInput);
    state.highpassInput = state.lowpass;
    auto coloured = state.highpass;

    if (style == 1)
    {
        const auto asymmetric = coloured * drive + 0.08f * character;
        coloured = std::tanh(asymmetric)
            - std::tanh(0.08f * character);
        state.memory += 0.035f * (coloured - state.memory);
        coloured = coloured * (1.0f - 0.16f * character)
            + state.memory * (0.16f * character);
    }
    else if (style == 2)
    {
        const auto compressed = std::tanh(coloured * drive);
        state.memory += 0.12f * (compressed - state.memory);
        coloured = compressed * 0.82f + state.memory * 0.18f;
    }
    else if (style == 3)
    {
        const auto steps = 32768.0f
            / std::pow(8.0f, character);
        const auto dither = randomBipolar(channel) / steps;
        coloured = std::round((coloured + dither) * steps) / steps;
    }
    else
        coloured = std::tanh(coloured * drive) / std::tanh(drive);

    return detail::finiteSample(coloured);
}

void DelayModule::startTimeTransition(
    const std::array<float, 2>& targets) noexcept
{
    oldDelaySamples = currentDelaySamples;
    targetDelaySamples = targets;
    timeTransition = 0.0f;
}

void DelayModule::publishTelemetry(
    bool capture, int sampleCount, float interval,
    const std::array<double, 2>& wetEnergy,
    float duckGain, float movement, int style) noexcept
{
    if (!capture || sampleCount <= 0)
        return;

    ContinuousTelemetrySnapshot snapshot;
    snapshot.sequence = ++telemetrySequence;
    snapshot.valueCount = telemetryValueCount;
    snapshot.values[intervalSeconds] = interval;
    snapshot.values[leftWetEnergy] = static_cast<float>(
        std::sqrt(wetEnergy[0] / static_cast<double>(sampleCount)));
    snapshot.values[rightWetEnergy] = static_cast<float>(
        std::sqrt(wetEnergy[1] / static_cast<double>(sampleCount)));
    snapshot.values[duckingGain] = duckGain;
    snapshot.values[movementSamples] = movement;
    snapshot.values[activeStyle] = static_cast<float>(style);
    appendContinuousTelemetryHistory(
        snapshot,
        { snapshot.values[leftWetEnergy],
          snapshot.values[rightWetEnergy],
          snapshot.values[duckingGain],
          snapshot.values[movementSamples] },
        4);
    telemetry.publish(snapshot);
}

void DelayModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    if (delayBuffer[0].empty() || buffer.getNumChannels() <= 0)
        return;

    const auto bpm = std::isfinite(environment.bpm)
            && environment.bpm >= 30.0 && environment.bpm <= 400.0
        ? environment.bpm : 120.0;
    const auto freeSeconds = detail::exponential(
        0.001f, maximumDelaySeconds,
        detail::normalizedControl(controls[0], 0.727f));
    const auto division = discreteIndex(
        detail::normalizedControl(controls[1], 0.64f),
        static_cast<int>(beatDivisions.size()));
    const auto syncedSeconds =
        beatDivisions[static_cast<size_t>(division)]
        * 60.0 / bpm;
    const auto synced =
        detail::normalizedControl(controls[2], 1.0f) >= 0.5f;
    const auto delaySeconds = juce::jlimit(
        0.001f, maximumDelaySeconds,
        static_cast<float>(synced ? syncedSeconds : freeSeconds));
    const auto feedbackTarget =
        detail::normalizedControl(controls[3], 0.33f) * 0.94f;
    const auto style = discreteIndex(
        detail::normalizedControl(controls[4], 0.0f), styleCount);
    const auto characterTarget =
        detail::normalizedControl(controls[5], 0.25f);
    const auto stereoMode = discreteIndex(
        detail::normalizedControl(controls[6], 0.0f), stereoModeCount);
    const auto spreadTarget =
        detail::normalizedControl(controls[7], 0.5f);
    const auto movementTarget =
        detail::normalizedControl(controls[8], 0.0f);
    const auto toneTarget =
        detail::normalizedControl(controls[9], 0.65f);
    const auto duckingTarget =
        detail::normalizedControl(controls[10], 0.0f);
    const auto mixTarget =
        detail::normalizedControl(controls[11], 0.2f);

    feedbackSmoothed.setTargetValue(feedbackTarget);
    characterSmoothed.setTargetValue(characterTarget);
    spreadSmoothed.setTargetValue(spreadTarget);
    movementSmoothed.setTargetValue(movementTarget);
    toneSmoothed.setTargetValue(toneTarget);
    duckingSmoothed.setTargetValue(duckingTarget);
    mixSmoothed.setTargetValue(mixTarget);

    if (style != activeStyleIndex)
    {
        previousStyleIndex = activeStyleIndex;
        activeStyleIndex = style;
        styleTransition = 0.0f;
    }
    if (stereoMode != activeStereoMode)
    {
        previousStereoMode = activeStereoMode;
        activeStereoMode = stereoMode;
        stereoTransition = 0.0f;
    }

    const auto channels = juce::jmin(2, buffer.getNumChannels());
    const auto effectiveStereoMode =
        channels > 1 ? activeStereoMode : 0;
    const auto baseSamples =
        delaySeconds * static_cast<float>(sampleRate);
    const auto spread = spreadTarget;
    std::array<float, 2> requested {
        baseSamples, baseSamples
    };
    if (effectiveStereoMode == 1 || effectiveStereoMode == 3)
    {
        requested[0] *= 1.0f - spread * 0.22f;
        requested[1] *= 1.0f + spread * 0.22f;
    }
    const auto maximumRead =
        static_cast<float>(delayBuffer[0].size() - 3);
    for (auto& delay : requested)
        delay = juce::jlimit(3.0f, maximumRead, delay);

    if (!parametersInitialised)
    {
        feedbackSmoothed.setCurrentAndTargetValue(feedbackTarget);
        characterSmoothed.setCurrentAndTargetValue(characterTarget);
        spreadSmoothed.setCurrentAndTargetValue(spreadTarget);
        movementSmoothed.setCurrentAndTargetValue(movementTarget);
        toneSmoothed.setCurrentAndTargetValue(toneTarget);
        duckingSmoothed.setCurrentAndTargetValue(duckingTarget);
        mixSmoothed.setCurrentAndTargetValue(mixTarget);
        currentDelaySamples = requested;
        oldDelaySamples = requested;
        targetDelaySamples = requested;
        lastRequestedDelaySamples = requested;
        activeStyleIndex = style;
        previousStyleIndex = style;
        activeStereoMode = stereoMode;
        previousStereoMode = stereoMode;
        timeTransition = 1.0f;
        styleTransition = 1.0f;
        stereoTransition = 1.0f;
        parametersInitialised = true;
    }
    else if (std::abs(requested[0] - lastRequestedDelaySamples[0]) > 0.25f
             || std::abs(requested[1]
                         - lastRequestedDelaySamples[1]) > 0.25f)
    {
        if (style == 0 || style == 3)
            startTimeTransition(requested);
        else
            targetDelaySamples = requested;
        lastRequestedDelaySamples = requested;
    }

    const auto capacity = static_cast<int>(delayBuffer[0].size());
    std::array<double, 2> wetEnergy {};
    float maximumMovement = 0.0f;
    const auto timeTransitionStep =
        1.0f / juce::jmax(1.0f, static_cast<float>(sampleRate * 0.028));
    const auto styleTransitionStep =
        1.0f / juce::jmax(1.0f, static_cast<float>(sampleRate * 0.045));
    const auto stereoTransitionStep =
        1.0f / juce::jmax(1.0f, static_cast<float>(sampleRate * 0.035));
    const auto delayGlideCoefficient = 1.0f
        - static_cast<float>(std::exp(
            -1.0 / (sampleRate * (style == 1 ? 0.08 : 0.045))));
    const auto envelopeAttack = 1.0f
        - static_cast<float>(std::exp(-1.0 / (sampleRate * 0.008)));
    const auto envelopeRelease = 1.0f
        - static_cast<float>(std::exp(-1.0 / (sampleRate * 0.28)));
    const auto duckRelease = 1.0f
        - static_cast<float>(std::exp(-1.0 / (sampleRate * 0.18)));

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        wowPhase += juce::MathConstants<double>::twoPi
            * (0.18 + 0.55 * characterTarget) / sampleRate;
        flutterPhase += juce::MathConstants<double>::twoPi
            * (4.7 + 2.8 * characterTarget) / sampleRate;
        if (wowPhase >= juce::MathConstants<double>::twoPi)
            wowPhase -= juce::MathConstants<double>::twoPi;
        if (flutterPhase >= juce::MathConstants<double>::twoPi)
            flutterPhase -= juce::MathConstants<double>::twoPi;

        std::array<float, 2> dry {
            detail::finiteSample(buffer.getSample(0, sample)),
            channels > 1
                ? detail::finiteSample(buffer.getSample(1, sample))
                : detail::finiteSample(buffer.getSample(0, sample))
        };
        const auto detector = juce::jmax(
            std::abs(dry[0]), std::abs(dry[1]));
        inputEnvelope += (detector > inputEnvelope
                ? envelopeAttack : envelopeRelease)
            * (detector - inputEnvelope);

        const auto feedback = feedbackSmoothed.getNextValue();
        const auto character = characterSmoothed.getNextValue();
        const auto currentSpread = spreadSmoothed.getNextValue();
        const auto movementAmount = movementSmoothed.getNextValue();
        const auto tone = toneSmoothed.getNextValue();
        const auto ducking = duckingSmoothed.getNextValue();
        const auto mix = mixSmoothed.getNextValue();
        juce::ignoreUnused(currentSpread);

        std::array<float, 2> modulation {
            movementFor(style, 0, movementAmount),
            movementFor(style, 1, movementAmount)
        };
        if (effectiveStereoMode == 0)
            modulation[1] = modulation[0];
        std::array<float, 2> delayed {};
        for (int channel = 0; channel < 2; ++channel)
        {
            if (style == 1 || style == 2)
                currentDelaySamples[static_cast<size_t>(channel)]
                    += delayGlideCoefficient
                        * (targetDelaySamples[static_cast<size_t>(channel)]
                           - currentDelaySamples[
                               static_cast<size_t>(channel)]);

            const auto movementSamples =
                modulation[static_cast<size_t>(channel)];
            maximumMovement = juce::jmax(
                maximumMovement, std::abs(movementSamples));
            if ((style == 0 || style == 3)
                && timeTransition < 1.0f)
            {
                const auto oldRead = readDelay(
                    channel,
                    oldDelaySamples[static_cast<size_t>(channel)]
                        + movementSamples);
                const auto newRead = readDelay(
                    channel,
                    targetDelaySamples[static_cast<size_t>(channel)]
                        + movementSamples);
                delayed[static_cast<size_t>(channel)] =
                    oldRead * equalPowerOut(timeTransition)
                    + newRead * equalPowerIn(timeTransition);
            }
            else
            {
                currentDelaySamples[static_cast<size_t>(channel)] =
                    style == 0 || style == 3
                    ? targetDelaySamples[static_cast<size_t>(channel)]
                    : currentDelaySamples[static_cast<size_t>(channel)];
                delayed[static_cast<size_t>(channel)] = readDelay(
                    channel,
                    currentDelaySamples[static_cast<size_t>(channel)]
                        + movementSamples);
            }
        }
        if (timeTransition < 1.0f)
            timeTransition = juce::jmin(
                1.0f, timeTransition + timeTransitionStep);

        const auto injectionFor =
            [&](int mode)
        {
            if (channels <= 1 || mode == 0)
            {
                const auto mono = 0.5f * (dry[0] + dry[1]);
                return std::array<float, 2> { mono, mono };
            }
            if (mode == 2)
            {
                const auto mono = 0.5f * (dry[0] + dry[1]);
                return std::array<float, 2> { mono, 0.0f };
            }
            return dry;
        };
        const auto feedbackFor =
            [&](int mode)
        {
            if (channels <= 1 || mode == 0 || mode == 1)
                return delayed;
            if (mode == 2)
                return std::array<float, 2> { delayed[1], delayed[0] };
            const auto cross = 0.2f + 0.65f * currentSpread;
            return std::array<float, 2> {
                delayed[0] * (1.0f - cross) + delayed[1] * cross,
                delayed[1] * (1.0f - cross) + delayed[0] * cross
            };
        };
        auto injection = injectionFor(effectiveStereoMode);
        auto feedbackSource = feedbackFor(effectiveStereoMode);
        if (channels > 1 && stereoTransition < 1.0f)
        {
            const auto oldInjection =
                injectionFor(previousStereoMode);
            const auto oldFeedback =
                feedbackFor(previousStereoMode);
            for (size_t channel = 0; channel < 2; ++channel)
            {
                injection[channel] = detail::lerp(
                    oldInjection[channel], injection[channel],
                    stereoTransition);
                feedbackSource[channel] = detail::lerp(
                    oldFeedback[channel], feedbackSource[channel],
                    stereoTransition);
            }
        }

        for (int channel = 0; channel < 2; ++channel)
        {
            auto coloured = processColour(
                activeStyleIndex, channel,
                feedbackSource[static_cast<size_t>(channel)],
                tone, character);
            if (styleTransition < 1.0f
                && previousStyleIndex != activeStyleIndex)
            {
                const auto oldColour = processColour(
                    previousStyleIndex, channel,
                    feedbackSource[static_cast<size_t>(channel)],
                    tone, character);
                coloured = oldColour * equalPowerOut(styleTransition)
                    + coloured * equalPowerIn(styleTransition);
            }
            delayBuffer[static_cast<size_t>(channel)]
                [static_cast<size_t>(writePosition)] =
                    boundedSample(
                        injection[static_cast<size_t>(channel)]
                        + coloured * feedback);
        }
        if (styleTransition < 1.0f)
            styleTransition = juce::jmin(
                1.0f, styleTransition + styleTransitionStep);
        if (stereoTransition < 1.0f)
            stereoTransition = juce::jmin(
                1.0f, stereoTransition + stereoTransitionStep);

        const auto duckTarget = juce::Decibels::decibelsToGain(
            -18.0f * ducking
            * juce::jlimit(0.0f, 1.0f, inputEnvelope * 2.5f));
        const auto duckCoefficient =
            duckTarget < lastDuckGain ? envelopeAttack : duckRelease;
        lastDuckGain += duckCoefficient
            * (duckTarget - lastDuckGain);

        for (int channel = 0; channel < channels; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            const auto wet = delayed[index] * lastDuckGain;
            wetEnergy[index] += static_cast<double>(wet) * wet;
            buffer.setSample(
                channel, sample,
                detail::finiteSample(
                    dry[index] + (wet - dry[index]) * mix));
        }
        if (channels == 1)
            wetEnergy[1] = wetEnergy[0];

        if (++writePosition >= capacity)
            writePosition = 0;
    }

    publishTelemetry(
        environment.captureTelemetry, buffer.getNumSamples(),
        delaySeconds, wetEnergy, lastDuckGain, maximumMovement,
        activeStyleIndex);
}

double DelayModule::tailSeconds(const ControlValues& controls) const
{
    const auto feedback =
        detail::normalizedControl(controls[3], 0.33f) * 0.94f;
    if (feedback < 0.001f)
        return maximumDelaySeconds;
    const auto repeats = std::log(0.001) / std::log(
        static_cast<double>(feedback));
    return juce::jlimit(
        0.0, 8.0, maximumDelaySeconds * juce::jmax(1.0, repeats));
}
} // namespace megadsp
