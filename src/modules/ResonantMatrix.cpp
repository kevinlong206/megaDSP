#include "ResonantMatrix.h"
#include "DspHelpers.h"

#include <algorithm>
#include <cmath>

namespace megadsp
{
using detail::exponential;
using detail::lerp;

namespace
{
constexpr float minimumDelaySamples = 4.0f;
constexpr float outputScale = 0.17677669f;
constexpr float decorScale = 0.17f;
constexpr float inputScale = 0.24f;
constexpr float householderScale = 0.25f;
constexpr float sideFocusFrequencyHz = 220.0f;
constexpr float activityCoefficient = 0.9965f;
constexpr float activityScale = 1.35f;
constexpr float inverseSqrtTwo = 0.70710678f;

constexpr std::array<float, ResonantMatrixModule::resonatorCount> detunePattern {
    0.0f, -1.0f, 0.65f, -0.65f, 0.35f, -0.35f, 0.85f, -0.85f
};

constexpr std::array<float, ResonantMatrixModule::resonatorCount> inputMidSigns {
    1.0f, -0.75f, 0.58f, 0.92f, -0.64f, 0.47f, -0.83f, 0.61f
};

constexpr std::array<float, ResonantMatrixModule::resonatorCount> inputSideSigns {
    -0.78f, 0.93f, -0.42f, 0.58f, -0.95f, 0.36f, -0.69f, 0.84f
};

constexpr std::array<std::array<float, ResonantMatrixModule::resonatorCount>,
                     ResonantMatrixModule::topologyCount> decorWeights {{
    { -0.96f, -0.61f, -0.26f, -0.05f, 0.05f, 0.26f, 0.61f, 0.96f },
    { -0.88f, -0.20f, 0.54f, -0.63f, 0.63f, -0.54f, 0.20f, 0.88f },
    { -0.74f, -0.93f, -0.11f, 0.48f, -0.48f, 0.11f, 0.93f, 0.74f },
    { -0.91f, 0.35f, -0.67f, 0.14f, -0.14f, 0.67f, -0.35f, 0.91f }
}};

constexpr std::array<std::array<float, ResonantMatrixModule::resonatorCount>,
                     ResonantMatrixModule::topologyCount> motionRateMultipliers {{
    { 0.71f, 0.83f, 0.97f, 1.11f, 1.27f, 1.43f, 1.61f, 1.79f },
    { 0.64f, 0.88f, 1.02f, 1.19f, 1.31f, 1.47f, 1.68f, 1.86f },
    { 0.58f, 0.79f, 0.94f, 1.18f, 1.39f, 1.57f, 1.73f, 1.92f },
    { 0.67f, 0.91f, 1.08f, 1.26f, 1.34f, 1.52f, 1.74f, 1.97f }
}};

constexpr std::array<std::array<std::array<int, ResonantMatrixModule::resonatorCount>,
                                4>,
                     ResonantMatrixModule::scaleCount> intervalTables {{
    {{
        { 0, 2, 4, 5, 7, 9, 11, 12 },
        { 0, 4, 7, 11, 14, 17, 21, 24 },
        { 0, 5, 11, 16, 21, 26, 31, 36 },
        { 0, 7, 14, 21, 28, 35, 41, 48 },
    }},
    {{
        { 0, 2, 3, 5, 7, 8, 10, 12 },
        { 0, 3, 7, 10, 14, 17, 20, 24 },
        { 0, 5, 10, 15, 20, 26, 31, 36 },
        { 0, 7, 14, 20, 27, 34, 41, 48 },
    }},
    {{
        { 0, 2, 3, 5, 7, 9, 10, 12 },
        { 0, 3, 7, 10, 14, 17, 21, 24 },
        { 0, 5, 10, 15, 21, 26, 31, 36 },
        { 0, 7, 14, 21, 27, 34, 41, 48 },
    }},
    {{
        { 0, 3, 3, 5, 7, 10, 10, 12 },
        { 0, 3, 7, 10, 15, 17, 22, 24 },
        { 0, 5, 10, 15, 22, 27, 31, 36 },
        { 0, 7, 15, 22, 27, 34, 41, 48 },
    }},
    {{
        { 0, 2, 4, 6, 6, 8, 10, 12 },
        { 0, 4, 6, 10, 14, 18, 20, 24 },
        { 0, 6, 10, 16, 20, 26, 30, 36 },
        { 0, 6, 14, 20, 28, 34, 42, 48 },
    }},
    {{
        { 0, 0, 0, 0, 12, 12, 12, 12 },
        { 0, 0, 12, 12, 12, 12, 24, 24 },
        { 0, 0, 12, 12, 24, 24, 36, 36 },
        { 0, 12, 12, 24, 24, 36, 36, 48 },
    }},
}};

float safe(float value, float fallback)
{
    return std::isfinite(value) ? juce::jlimit(0.0f, 1.0f, value) : fallback;
}

float centsToDelayRatio(float cents)
{
    return std::pow(2.0f, -cents / 1200.0f);
}

float sanitizeSample(float value)
{
    if (!std::isfinite(value))
        return 0.0f;
    return juce::jlimit(-8.0f, 8.0f, value);
}
} // namespace

void ResonantMatrixModule::FractionalDelay::prepare(int capacity)
{
    buffer.assign(static_cast<size_t>(juce::jmax(8, capacity)), 0.0f);
    writePosition = 0;
}

void ResonantMatrixModule::FractionalDelay::reset()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writePosition = 0;
}

float ResonantMatrixModule::FractionalDelay::read(float delaySamples) const
{
    if (buffer.empty())
        return 0.0f;

    const auto size = static_cast<int>(buffer.size());
    auto position = static_cast<float>(writePosition)
                    - juce::jlimit(2.0f, static_cast<float>(size - 3),
                                   delaySamples);
    while (position < 0.0f)
        position += static_cast<float>(size);

    const auto index = static_cast<int>(std::floor(position));
    const auto fraction = position - static_cast<float>(index);
    const auto at = [this, size](int sampleIndex)
    {
        while (sampleIndex < 0)
            sampleIndex += size;
        return buffer[static_cast<size_t>(sampleIndex % size)];
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

void ResonantMatrixModule::FractionalDelay::write(float value)
{
    if (buffer.empty())
        return;

    buffer[static_cast<size_t>(writePosition)] = sanitizeSample(value);
    if (++writePosition >= static_cast<int>(buffer.size()))
        writePosition = 0;
}

void ResonantMatrixModule::NetworkState::prepare(
    double newSampleRate, int topologyIndex)
{
    sampleRate = juce::jmax(8000.0, newSampleRate);
    topology = juce::jlimit(0, topologyCount - 1, topologyIndex);
    maximumDelaySamples = static_cast<float>(
        std::ceil(sampleRate / 20.0) + 16.0);
    for (auto& line : lines)
        for (auto& delay : line.delay)
            delay.prepare(static_cast<int>(maximumDelaySamples));
    reset();
}

void ResonantMatrixModule::NetworkState::reset()
{
    for (auto& line : lines)
    {
        for (auto& delay : line.delay)
            delay.reset();
        line.dampingState = {};
    }

    for (int line = 0; line < resonatorCount; ++line)
    {
        phases[static_cast<size_t>(line)] =
            std::fmod(0.137f * static_cast<float>(topology)
                          + 0.173f * static_cast<float>(line),
                      1.0f);
    }
}

void ResonantMatrixModule::NetworkState::applyTopology(
    std::array<float, resonatorCount>& values) const
{
    if (topology == 0)
    {
        constexpr std::array<int, resonatorCount> permutation {
            1, 3, 5, 7, 0, 2, 4, 6
        };
        constexpr std::array<float, resonatorCount> signs {
            1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f, -1.0f
        };
        auto rotated = values;
        for (int line = 0; line < resonatorCount; ++line)
            values[static_cast<size_t>(line)] =
                rotated[static_cast<size_t>(permutation[static_cast<size_t>(line)])]
                * signs[static_cast<size_t>(line)];
        return;
    }

    if (topology == 1)
    {
        auto source = values;
        values[0] = (source[0] + source[1]) * inverseSqrtTwo;
        values[1] = (source[0] - source[1]) * inverseSqrtTwo;
        values[2] = (source[2] - source[3]) * inverseSqrtTwo;
        values[3] = (source[2] + source[3]) * inverseSqrtTwo;
        values[4] = (source[4] + source[5]) * inverseSqrtTwo;
        values[5] = (source[4] - source[5]) * inverseSqrtTwo;
        values[6] = (source[6] - source[7]) * inverseSqrtTwo;
        values[7] = (source[6] + source[7]) * inverseSqrtTwo;
        return;
    }

    if (topology == 2)
    {
        auto state = std::array<float, resonatorCount> {
            values[0], values[2], values[4], values[6],
            -values[1], values[3], -values[5], values[7]
        };
        auto rotate = [&state](int first, int second, float sign)
        {
            const auto a = state[static_cast<size_t>(first)];
            const auto b = state[static_cast<size_t>(second)] * sign;
            state[static_cast<size_t>(first)] = (a + b) * inverseSqrtTwo;
            state[static_cast<size_t>(second)] = (b - a) * inverseSqrtTwo;
        };
        rotate(0, 1, 1.0f);
        rotate(2, 3, -1.0f);
        rotate(4, 5, 1.0f);
        rotate(6, 7, -1.0f);
        rotate(1, 2, 1.0f);
        rotate(3, 4, 1.0f);
        rotate(5, 6, 1.0f);
        rotate(0, 7, -1.0f);
        values = state;
        return;
    }

    constexpr std::array<float, resonatorCount> signs {
        1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f
    };
    auto dot = 0.0f;
    for (int line = 0; line < resonatorCount; ++line)
        dot += signs[static_cast<size_t>(line)]
               * values[static_cast<size_t>(line)];
    for (int line = 0; line < resonatorCount; ++line)
        values[static_cast<size_t>(line)] -= householderScale
            * signs[static_cast<size_t>(line)] * dot;
}

ResonantMatrixModule::NetworkOutput
ResonantMatrixModule::NetworkState::processSample(
    float leftInput, float rightInput, float dampingCoefficient,
    float motionRateHz, float motionDepthCents,
    const std::array<float, resonatorCount>& baseDelaySamples,
    const std::array<float, resonatorCount>& feedbackGains)
{
    std::array<float, resonatorCount> filteredLeft {};
    std::array<float, resonatorCount> filteredRight {};
    NetworkOutput result;
    const auto midInput = 0.5f * (leftInput + rightInput);
    const auto sideInput = 0.5f * (leftInput - rightInput);

    for (int line = 0; line < resonatorCount; ++line)
    {
        const auto lineIndex = static_cast<size_t>(line);
        const auto motion = motionDepthCents > 0.0f
            ? std::sin(juce::MathConstants<float>::twoPi * phases[lineIndex])
            : 0.0f;
        const auto delaySamples = juce::jlimit(
            minimumDelaySamples, maximumDelaySamples,
            baseDelaySamples[lineIndex]
                * centsToDelayRatio(motionDepthCents * motion));

        auto left = lines[lineIndex].delay[0].read(delaySamples);
        auto right = lines[lineIndex].delay[1].read(delaySamples);
        lines[lineIndex].dampingState[0] += dampingCoefficient
            * (left - lines[lineIndex].dampingState[0]);
        lines[lineIndex].dampingState[1] += dampingCoefficient
            * (right - lines[lineIndex].dampingState[1]);
        filteredLeft[lineIndex] = lines[lineIndex].dampingState[0];
        filteredRight[lineIndex] = lines[lineIndex].dampingState[1];

        phases[lineIndex] += motionRateHz
            * motionRateMultipliers[static_cast<size_t>(topology)][lineIndex]
            / static_cast<float>(sampleRate);
        phases[lineIndex] -= std::floor(phases[lineIndex]);
    }

    auto matrixLeft = filteredLeft;
    auto matrixRight = filteredRight;
    applyTopology(matrixLeft);
    applyTopology(matrixRight);

    for (int line = 0; line < resonatorCount; ++line)
    {
        const auto lineIndex = static_cast<size_t>(line);
        const auto main = 0.5f
            * (filteredLeft[lineIndex] + filteredRight[lineIndex]);
        const auto side = 0.5f
            * (filteredLeft[lineIndex] - filteredRight[lineIndex]);
        result.main += main * outputScale;
        result.trueSide += side * outputScale;
        result.decorSide += main
            * decorWeights[static_cast<size_t>(topology)][lineIndex]
            * decorScale;
        result.energy += (main * main + side * side)
            * (1.0f / static_cast<float>(resonatorCount));

        const auto commonDrive = inputScale * inputMidSigns[lineIndex] * midInput;
        const auto stereoDrive = inputScale * inputSideSigns[lineIndex] * sideInput;
        lines[lineIndex].delay[0].write(
            matrixLeft[lineIndex] * feedbackGains[lineIndex]
            + commonDrive + stereoDrive);
        lines[lineIndex].delay[1].write(
            matrixRight[lineIndex] * feedbackGains[lineIndex]
            + commonDrive - stereoDrive);
    }

    return result;
}

void ResonantMatrixModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = juce::jmax(8000.0, spec.sampleRate);
    maximumDelaySamples = static_cast<float>(
        std::ceil(sampleRate / 20.0) + 16.0);
    for (int topology = 0; topology < topologyCount; ++topology)
        networks[static_cast<size_t>(topology)].prepare(sampleRate, topology);

    for (auto& smoother : topologyMix)
        smoother.reset(sampleRate, 0.05);
    for (auto& smoother : pitchDelaySmoothed)
        smoother.reset(sampleRate, 0.045);
    decaySmoothed.reset(sampleRate, 0.06);
    dampingCoefficientSmoothed.reset(sampleRate, 0.04);
    detuneSmoothed.reset(sampleRate, 0.05);
    motionRateSmoothed.reset(sampleRate, 0.08);
    motionDepthSmoothed.reset(sampleRate, 0.05);
    widthSmoothed.reset(sampleRate, 0.04);
    mixSmoothed.reset(sampleRate, 0.025);
    outputSmoothed.reset(sampleRate, 0.025);
    reset();
}

void ResonantMatrixModule::reset()
{
    for (auto& network : networks)
        network.reset();
    for (auto& smoother : topologyMix)
        smoother.setCurrentAndTargetValue(0.0f);
    for (auto& smoother : pitchDelaySmoothed)
        smoother.setCurrentAndTargetValue(maximumDelaySamples * 0.5f);
    decaySmoothed.setCurrentAndTargetValue(1.0f);
    dampingCoefficientSmoothed.setCurrentAndTargetValue(0.5f);
    detuneSmoothed.setCurrentAndTargetValue(0.0f);
    motionRateSmoothed.setCurrentAndTargetValue(0.1f);
    motionDepthSmoothed.setCurrentAndTargetValue(0.0f);
    widthSmoothed.setCurrentAndTargetValue(1.0f);
    mixSmoothed.setCurrentAndTargetValue(0.0f);
    outputSmoothed.setCurrentAndTargetValue(1.0f);
    sideLowState = 0.0f;
    activityEnvelope = 0.0f;
    activityMeter.store(0.0f, std::memory_order_relaxed);
    initialized = false;
}

void ResonantMatrixModule::process(juce::AudioBuffer<float>& buffer,
                                   const ControlValues& controls,
                                   const ProcessEnvironment&)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() <= 0 || buffer.getNumSamples() <= 0)
        return;

    const auto tuneHz = exponential(27.5f, 440.0f, safe(controls[0], 0.5f));
    const auto scale = discreteIndex(safe(controls[1], 0.0f), scaleCount);
    const auto span = juce::jlimit(
        1, 4, juce::roundToInt(lerp(1.0f, 4.0f, safe(controls[2], 0.333f))));
    const auto topology = discreteIndex(safe(controls[3], 0.0f), topologyCount);
    const auto decaySeconds = exponential(0.1f, 12.0f, safe(controls[4], 0.55f));
    const auto dampingHz = exponential(500.0f, 20000.0f, safe(controls[5], 0.78f));
    const auto detuneCents = safe(controls[6], 0.2f) * 30.0f;
    const auto motionRateHz = exponential(0.02f, 2.0f, safe(controls[7], 0.38f));
    const auto motionDepthCents = safe(controls[8], 0.12f) * 50.0f;
    const auto width = safe(controls[9], 2.0f / 3.0f) * 1.5f;
    const auto mix = safe(controls[10], 0.35f);
    const auto outputDb = lerp(-18.0f, 12.0f, safe(controls[11], 0.6f));
    const auto outputGain = std::abs(outputDb) < 0.0001f
        ? 1.0f : juce::Decibels::decibelsToGain(outputDb);
    const auto dampingCoefficient = 1.0f - std::exp(
        -juce::MathConstants<float>::twoPi
        * juce::jlimit(200.0f, static_cast<float>(sampleRate * 0.45), dampingHz)
        / static_cast<float>(sampleRate));

    const auto& intervals =
        intervalTables[static_cast<size_t>(scale)][static_cast<size_t>(span - 1)];
    std::array<float, resonatorCount> targetPitchDelaySamples {};
    for (int line = 0; line < resonatorCount; ++line)
    {
        const auto semitones = static_cast<float>(intervals[static_cast<size_t>(line)]);
        const auto frequency = tuneHz * std::pow(2.0f, semitones / 12.0f);
        targetPitchDelaySamples[static_cast<size_t>(line)] = juce::jlimit(
            minimumDelaySamples, maximumDelaySamples,
            static_cast<float>(sampleRate) / juce::jmax(1.0f, frequency));
    }

    if (!initialized)
    {
        for (int index = 0; index < topologyCount; ++index)
            topologyMix[static_cast<size_t>(index)].setCurrentAndTargetValue(
                index == topology ? 1.0f : 0.0f);
        for (int line = 0; line < resonatorCount; ++line)
            pitchDelaySmoothed[static_cast<size_t>(line)]
                .setCurrentAndTargetValue(
                    targetPitchDelaySamples[static_cast<size_t>(line)]);
        decaySmoothed.setCurrentAndTargetValue(decaySeconds);
        dampingCoefficientSmoothed.setCurrentAndTargetValue(dampingCoefficient);
        detuneSmoothed.setCurrentAndTargetValue(detuneCents);
        motionRateSmoothed.setCurrentAndTargetValue(motionRateHz);
        motionDepthSmoothed.setCurrentAndTargetValue(motionDepthCents);
        widthSmoothed.setCurrentAndTargetValue(width);
        mixSmoothed.setCurrentAndTargetValue(mix);
        outputSmoothed.setCurrentAndTargetValue(outputGain);
        initialized = true;
    }

    for (int index = 0; index < topologyCount; ++index)
        topologyMix[static_cast<size_t>(index)].setTargetValue(
            index == topology ? 1.0f : 0.0f);
    for (int line = 0; line < resonatorCount; ++line)
        pitchDelaySmoothed[static_cast<size_t>(line)].setTargetValue(
            targetPitchDelaySamples[static_cast<size_t>(line)]);
    decaySmoothed.setTargetValue(decaySeconds);
    dampingCoefficientSmoothed.setTargetValue(dampingCoefficient);
    detuneSmoothed.setTargetValue(detuneCents);
    motionRateSmoothed.setTargetValue(motionRateHz);
    motionDepthSmoothed.setTargetValue(motionDepthCents);
    widthSmoothed.setTargetValue(width);
    mixSmoothed.setTargetValue(mix);
    outputSmoothed.setTargetValue(outputGain);

    const auto stereo = buffer.getNumChannels() > 1;
    const auto sideFocusCoefficient = std::exp(
        -juce::MathConstants<float>::twoPi * sideFocusFrequencyHz
        / static_cast<float>(sampleRate));
    std::array<float, resonatorCount> baseDelaySamples {};
    std::array<float, resonatorCount> feedbackGains {};

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto dryLeft = buffer.getSample(0, sample);
        const auto dryRight = stereo ? buffer.getSample(1, sample) : dryLeft;
        const auto currentDecay = juce::jmax(0.1f, decaySmoothed.getNextValue());
        const auto currentDamping = dampingCoefficientSmoothed.getNextValue();
        const auto currentDetune = detuneSmoothed.getNextValue();
        const auto currentMotionRate = motionRateSmoothed.getNextValue();
        const auto currentMotionDepth = motionDepthSmoothed.getNextValue();
        const auto currentWidth = widthSmoothed.getNextValue();
        const auto currentMix = mixSmoothed.getNextValue();
        const auto currentOutput = outputSmoothed.getNextValue();

        for (int line = 0; line < resonatorCount; ++line)
        {
            const auto lineIndex = static_cast<size_t>(line);
            const auto pitchDelay = pitchDelaySmoothed[lineIndex].getNextValue();
            const auto staticCents = detunePattern[lineIndex] * currentDetune;
            baseDelaySamples[lineIndex] = juce::jlimit(
                minimumDelaySamples, maximumDelaySamples,
                pitchDelay * centsToDelayRatio(staticCents));
            const auto worstCaseDelay = juce::jmin(
                maximumDelaySamples,
                pitchDelay * std::pow(
                                 2.0f,
                                 (std::abs(detunePattern[lineIndex]) * currentDetune
                                  + currentMotionDepth) / 1200.0f));
            feedbackGains[lineIndex] = juce::jlimit(
                0.0f, 0.9998f,
                std::pow(0.001f,
                         (worstCaseDelay / static_cast<float>(sampleRate))
                             / currentDecay));
        }

        NetworkOutput summed;
        for (int index = 0; index < topologyCount; ++index)
        {
            const auto topologyWeight =
                topologyMix[static_cast<size_t>(index)].getNextValue();
            const auto rendered =
                networks[static_cast<size_t>(index)].processSample(
                    dryLeft, dryRight, currentDamping, currentMotionRate,
                    currentMotionDepth, baseDelaySamples, feedbackGains);
            summed.main += rendered.main * topologyWeight;
            summed.trueSide += rendered.trueSide * topologyWeight;
            summed.decorSide += rendered.decorSide * topologyWeight;
            summed.energy += rendered.energy * topologyWeight;
        }

        const auto rawSide =
            (summed.trueSide + summed.decorSide) * currentWidth;
        sideLowState = sideFocusCoefficient * sideLowState
                       + (1.0f - sideFocusCoefficient) * rawSide;
        const auto focusedSide = (rawSide - sideLowState)
            + sideLowState * (0.12f + 0.18f * juce::jmin(1.0f, currentWidth));
        const auto widthCompensation =
            1.0f / std::sqrt(1.0f + 0.35f * currentWidth * currentWidth);
        const auto wetMid = summed.main;
        const auto wetSide = focusedSide * widthCompensation;
        const auto wetLeft = wetMid + wetSide;
        const auto wetRight = wetMid - wetSide;

        auto dryGain = 1.0f;
        auto wetGain = 0.0f;
        if (currentMix > 0.0f)
        {
            const auto angle =
                juce::MathConstants<float>::halfPi * currentMix;
            dryGain = std::cos(angle);
            wetGain = std::sin(angle);
        }

        if (stereo)
        {
            buffer.setSample(0, sample, sanitizeSample(
                (dryLeft * dryGain + wetLeft * wetGain) * currentOutput));
            buffer.setSample(1, sample, sanitizeSample(
                (dryRight * dryGain + wetRight * wetGain) * currentOutput));
        }
        else
        {
            buffer.setSample(0, sample, sanitizeSample(
                (dryLeft * dryGain + wetMid * wetGain) * currentOutput));
        }

        activityEnvelope = activityCoefficient * activityEnvelope
                           + (1.0f - activityCoefficient) * summed.energy;
    }

    activityMeter.store(juce::jlimit(
                            0.0f, 1.0f,
                            std::sqrt(juce::jmax(0.0f, activityEnvelope))
                                * activityScale),
                        std::memory_order_relaxed);
}

double ResonantMatrixModule::tailSeconds(const ControlValues& controls) const
{
    const auto span = juce::jlimit(
        1, 4, juce::roundToInt(lerp(1.0f, 4.0f, safe(controls[2], 0.333f))));
    return exponential(0.1f, 12.0f, safe(controls[4], 0.55f))
           + 0.08 * static_cast<double>(span);
}
} // namespace megadsp
