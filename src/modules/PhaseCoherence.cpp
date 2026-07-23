#include "PhaseCoherence.h"
#include "DspHelpers.h"
#include "DspSafety.h"

#include <cmath>

namespace megadsp
{
namespace
{
using detail::exponential;
using detail::lerp;

float correlation(float cross, float leftPower, float rightPower) noexcept
{
    const auto denominator = std::sqrt(
        juce::jmax(0.0f, leftPower * rightPower));
    return denominator > 1.0e-9f
        ? juce::jlimit(-1.0f, 1.0f, cross / denominator) : 0.0f;
}
} // namespace

void PhaseCoherenceModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    fixedLatencySamples = juce::jmax(
        1, static_cast<int>(std::ceil(sampleRate * 0.002)));
    analysisWindowSamples = juce::jlimit(
        256, 2048, juce::roundToInt(sampleRate * 0.024));
    const auto capacity = static_cast<size_t>(
        analysisWindowSamples + fixedLatencySamples * 2 + 8);
    for (auto& history : audioHistory)
        history.assign(capacity, 0.0f);
    for (auto& history : analysisHistory)
        history.assign(capacity, 0.0f);

    alignmentSmoothed.reset(sampleRate, 0.060);
    correctionSmoothed.reset(sampleRate, 0.040);
    preserveSmoothed.reset(sampleRate, 0.040);
    monoBelowSmoothed.reset(sampleRate, 0.050);
    outputSmoothed.reset(sampleRate, 0.025);
    reset();
}

void PhaseCoherenceModule::reset()
{
    for (auto& history : audioHistory)
        std::fill(history.begin(), history.end(), 0.0f);
    for (auto& history : analysisHistory)
        std::fill(history.begin(), history.end(), 0.0f);
    detectorLowPass.fill(0.0f);
    monoLowPass.fill(0.0f);
    previousAnalysis.fill(0.0f);
    currentAnalysis.fill(0.0f);
    estimatedDelaySamples = 0.0f;
    estimatedPhase = 0.0f;
    confidence = 0.0f;
    beforeCorrelation = 1.0f;
    afterCorrelation = 1.0f;
    writePosition = 0;
    analysisPosition = 0;
    analysisCountdown = analysisWindowSamples / 2;
    initialized = false;
    telemetryState = {};
    telemetry.clear();
}

float PhaseCoherenceModule::readDelay(
    int channel, float delaySamples) const noexcept
{
    const auto& history = audioHistory[static_cast<size_t>(
        juce::jlimit(0, 1, channel))];
    const auto size = static_cast<int>(history.size());
    auto position = static_cast<float>(writePosition) - delaySamples;
    while (position < 0.0f)
        position += static_cast<float>(size);
    while (position >= static_cast<float>(size))
        position -= static_cast<float>(size);
    const auto first = static_cast<int>(position);
    const auto fraction = position - static_cast<float>(first);
    const auto second = (first + 1) % size;
    return history[static_cast<size_t>(first)]
        + fraction * (history[static_cast<size_t>(second)]
                      - history[static_cast<size_t>(first)]);
}

void PhaseCoherenceModule::analyse(
    float maximumAlignmentSamples, float crossoverHz) noexcept
{
    const auto size = static_cast<int>(analysisHistory[0].size());
    const auto maximumLag = juce::jlimit(
        0, fixedLatencySamples,
        static_cast<int>(std::floor(maximumAlignmentSamples)));
    if (maximumLag <= 0)
    {
        estimatedDelaySamples = 0.0f;
        confidence = 0.0f;
        return;
    }

    std::array<float, 3> neighbours {};
    const auto correlationAt = [&](int lag, int stride) noexcept
    {
        double cross = 0.0;
        double leftPower = 0.0;
        double rightPower = 0.0;
        for (int offset = 0; offset < analysisWindowSamples; offset += stride)
        {
            auto leftIndex = analysisPosition - analysisWindowSamples + offset;
            auto rightIndex = leftIndex + lag;
            while (leftIndex < 0)
                leftIndex += size;
            while (rightIndex < 0)
                rightIndex += size;
            const auto left = analysisHistory[0][static_cast<size_t>(
                leftIndex % size)];
            const auto right = analysisHistory[1][static_cast<size_t>(
                rightIndex % size)];
            cross += static_cast<double>(left) * right;
            leftPower += static_cast<double>(left) * left;
            rightPower += static_cast<double>(right) * right;
        }
        return correlation(
            static_cast<float>(cross), static_cast<float>(leftPower),
            static_cast<float>(rightPower));
    };
    const auto coarseStride = juce::jmax(
        1, juce::roundToInt(static_cast<float>(sampleRate / 48000.0)));
    auto bestCorrelation = -1.0f;
    auto bestLag = 0;
    const auto zeroCorrelation = correlationAt(0, coarseStride);
    const auto coarseMaximum =
        (maximumLag / coarseStride) * coarseStride;
    for (int lag = -coarseMaximum; lag <= coarseMaximum;
         lag += coarseStride)
    {
        const auto value = correlationAt(lag, coarseStride);
        if (value > bestCorrelation)
        {
            bestCorrelation = value;
            bestLag = lag;
        }
    }
    const auto coarseBestLag = bestLag;
    bestCorrelation = -1.0f;
    for (int lag = juce::jmax(-maximumLag, coarseBestLag - coarseStride);
         lag <= juce::jmin(maximumLag, coarseBestLag + coarseStride); ++lag)
    {
        const auto value = correlationAt(lag, 1);
        if (value > bestCorrelation)
        {
            bestCorrelation = value;
            bestLag = lag;
        }
    }

    for (int neighbour = -1; neighbour <= 1; ++neighbour)
    {
        const auto lag = juce::jlimit(
            -maximumLag, maximumLag, bestLag + neighbour);
        neighbours[static_cast<size_t>(neighbour + 1)] =
            correlationAt(lag, 1);
    }

    const auto denominator =
        neighbours[0] - 2.0f * neighbours[1] + neighbours[2];
    auto fraction = std::abs(denominator) > 1.0e-5f
        ? 0.5f * (neighbours[0] - neighbours[2]) / denominator : 0.0f;
    fraction = juce::jlimit(-0.5f, 0.5f, fraction);
    if (bestLag == -maximumLag || bestLag == maximumLag)
        fraction = 0.0f;

    const auto energyConfidence = juce::jlimit(
        0.0f, 1.0f, (bestCorrelation - 0.55f) / 0.4f);
    const auto improvementConfidence = bestLag == 0
        ? energyConfidence
        : juce::jlimit(0.0f, 1.0f,
                      (bestCorrelation - zeroCorrelation - 0.025f) / 0.15f);
    confidence = bestLag == 0
        ? energyConfidence : juce::jmin(energyConfidence, improvementConfidence);
    beforeCorrelation = zeroCorrelation;
    if (confidence > 0.62f)
        estimatedDelaySamples = static_cast<float>(bestLag) + fraction;
    else
        estimatedDelaySamples = 0.0f;
    estimatedPhase = juce::jlimit(
        -180.0f, 180.0f,
        estimatedDelaySamples * 360.0f * crossoverHz
            / static_cast<float>(sampleRate));
}

void PhaseCoherenceModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    const auto channels = juce::jlimit(0, 2, buffer.getNumChannels());
    if (channels == 0 || audioHistory[0].empty())
        return;
    const auto normalized = [&controls](int index, float fallback)
    {
        return detail::normalizedControl(
            controls[static_cast<size_t>(index)], fallback);
    };

    static constexpr std::array<float, 3> rangeMultipliers {
        1.0f, 2.5f, 8.0f
    };
    const auto range = discreteIndex(normalized(rangeControl, 0.5f), 3);
    const auto crossover = exponential(
        40.0f, 800.0f, normalized(crossoverControl, 0.46f));
    const auto detectorCutoff = juce::jmin(
        static_cast<float>(sampleRate * 0.42),
        crossover * rangeMultipliers[static_cast<size_t>(range)]);
    const auto correction = normalized(correctionControl, 0.75f);
    const auto maximumAlignment = normalized(maxAlignmentControl, 1.0f)
        * 0.002f * static_cast<float>(sampleRate);
    const auto maximumRotation =
        normalized(phaseRotationControl, 0.5f) * 180.0f;
    const auto preserve = normalized(stereoPreserveControl, 0.75f);
    const auto monoBelow = exponential(
        20.0f, 500.0f, normalized(monoBelowControl, 0.0f));
    const auto output = juce::Decibels::decibelsToGain(
        lerp(-18.0f, 12.0f, normalized(outputControl, 0.6f)));

    if (!initialized)
    {
        alignmentSmoothed.setCurrentAndTargetValue(0.0f);
        correctionSmoothed.setCurrentAndTargetValue(correction);
        preserveSmoothed.setCurrentAndTargetValue(preserve);
        monoBelowSmoothed.setCurrentAndTargetValue(monoBelow);
        outputSmoothed.setCurrentAndTargetValue(output);
        initialized = true;
    }
    correctionSmoothed.setTargetValue(correction);
    preserveSmoothed.setTargetValue(preserve);
    monoBelowSmoothed.setTargetValue(monoBelow);
    outputSmoothed.setTargetValue(output);

    const auto detectorCoefficient = 1.0f - std::exp(
        -juce::MathConstants<float>::twoPi * detectorCutoff
        / static_cast<float>(sampleRate));
    double afterCross = 0.0;
    double afterLeftPower = 0.0;
    double afterRightPower = 0.0;
    float appliedDelay = 0.0f;
    float appliedRotation = 0.0f;
    float lastPreservation = 1.0f;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto left = detail::finiteSample(buffer.getSample(0, sample));
        const auto right = channels > 1
            ? detail::finiteSample(buffer.getSample(1, sample)) : left;
        audioHistory[0][static_cast<size_t>(writePosition)] = left;
        audioHistory[1][static_cast<size_t>(writePosition)] = right;

        detectorLowPass[0] += detectorCoefficient
                              * (left - detectorLowPass[0]);
        detectorLowPass[1] += detectorCoefficient
                              * (right - detectorLowPass[1]);
        analysisHistory[0][static_cast<size_t>(analysisPosition)] =
            detectorLowPass[0];
        analysisHistory[1][static_cast<size_t>(analysisPosition)] =
            detectorLowPass[1];
        analysisPosition = (analysisPosition + 1)
            % static_cast<int>(analysisHistory[0].size());

        if (channels > 1 && --analysisCountdown <= 0)
        {
            analyse(maximumAlignment, crossover);
            const auto phaseLimitedSamples = maximumRotation > 0.0f
                ? maximumRotation * static_cast<float>(sampleRate)
                    / (360.0f * crossover)
                : 0.0f;
            const auto integerPart = std::trunc(estimatedDelaySamples);
            const auto fractionalPart = juce::jlimit(
                -phaseLimitedSamples, phaseLimitedSamples,
                estimatedDelaySamples - integerPart);
            alignmentSmoothed.setTargetValue(
                juce::jlimit(-maximumAlignment, maximumAlignment,
                             integerPart + fractionalPart));
            analysisCountdown = analysisWindowSamples / 2;
        }

        const auto currentCorrection = correctionSmoothed.getNextValue();
        const auto alignment = channels > 1
            ? alignmentSmoothed.getNextValue() * currentCorrection : 0.0f;
        appliedDelay = alignment;
        appliedRotation = alignment * 360.0f * crossover
                          / static_cast<float>(sampleRate);
        const auto leftDelay = static_cast<float>(fixedLatencySamples)
            + juce::jmin(0.0f, alignment);
        const auto rightDelay = static_cast<float>(fixedLatencySamples)
            - juce::jmax(0.0f, alignment);
        auto alignedLeft = readDelay(0, leftDelay);
        auto alignedRight = channels > 1
            ? readDelay(1, rightDelay) : alignedLeft;

        if (channels > 1)
        {
            const auto mid = 0.5f * (alignedLeft + alignedRight);
            auto side = 0.5f * (alignedLeft - alignedRight);
            const auto currentMonoBelow = monoBelowSmoothed.getNextValue();
            const auto monoCoefficient = 1.0f - std::exp(
                -juce::MathConstants<float>::twoPi * currentMonoBelow
                / static_cast<float>(sampleRate));
            monoLowPass[0] += monoCoefficient * (side - monoLowPass[0]);
            const auto lowSideReduction = normalized(monoBelowControl, 0.0f)
                                          * currentCorrection;
            side -= monoLowPass[0] * lowSideReduction;
            const auto correlationRepair = confidence > 0.75f
                ? juce::jlimit(0.0f, 1.0f, (beforeCorrelation + 0.2f) / 0.55f)
                : 1.0f;
            lastPreservation = lerp(
                preserveSmoothed.getNextValue(), 1.0f,
                1.0f - currentCorrection * (1.0f - correlationRepair));
            side *= lastPreservation;
            alignedLeft = mid + side;
            alignedRight = mid - side;
        }
        else
        {
            monoBelowSmoothed.getNextValue();
            preserveSmoothed.getNextValue();
        }

        const auto currentOutput = outputSmoothed.getNextValue();
        alignedLeft = detail::finiteSample(alignedLeft * currentOutput);
        alignedRight = detail::finiteSample(alignedRight * currentOutput);
        buffer.setSample(0, sample, alignedLeft);
        if (channels > 1)
            buffer.setSample(1, sample, alignedRight);
        afterCross += static_cast<double>(alignedLeft) * alignedRight;
        afterLeftPower += static_cast<double>(alignedLeft) * alignedLeft;
        afterRightPower += static_cast<double>(alignedRight) * alignedRight;

        writePosition = (writePosition + 1)
            % static_cast<int>(audioHistory[0].size());
    }
    afterCorrelation = correlation(
        static_cast<float>(afterCross), static_cast<float>(afterLeftPower),
        static_cast<float>(afterRightPower));

    if (environment.captureTelemetry)
    {
        telemetryState.sequence += 1;
        telemetryState.valueCount = telemetryValueCount;
        telemetryState.values[correlationBefore] = beforeCorrelation;
        telemetryState.values[correlationAfter] = afterCorrelation;
        telemetryState.values[estimatedDelayMilliseconds] =
            estimatedDelaySamples * 1000.0f / static_cast<float>(sampleRate);
        telemetryState.values[appliedDelayMilliseconds] =
            appliedDelay * 1000.0f / static_cast<float>(sampleRate);
        telemetryState.values[estimatedPhaseDegrees] = estimatedPhase;
        telemetryState.values[appliedRotationDegrees] = appliedRotation;
        telemetryState.values[analysisConfidence] = confidence;
        telemetryState.values[sidePreservation] = lastPreservation;
        appendContinuousTelemetryHistory(
            telemetryState,
            { beforeCorrelation, afterCorrelation,
              telemetryState.values[appliedDelayMilliseconds],
              appliedRotation },
            telemetryHistoryValueCount);
        telemetry.publish(telemetryState);
    }
}

bool PhaseCoherenceModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}
} // namespace megadsp
