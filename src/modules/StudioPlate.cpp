#include "StudioPlate.h"

#include "DspHelpers.h"
#include "DspSafety.h"

#include <cmath>

namespace megadsp
{
namespace
{
constexpr float twoPi = juce::MathConstants<float>::twoPi;

// Inharmonic modal ratios drawn from small rectangular-plate eigenmodes
// (sqrt(m^2 + n^2) normalized to the fundamental), giving the metallic,
// non-integer-related onset character of a physical plate.
constexpr std::array<float, StudioPlateModule::modeCount> modeRatios {
    1.000f, 1.581f, 2.000f, 2.236f, 2.550f, 2.915f, 3.000f, 3.162f
};
constexpr std::array<float, StudioPlateModule::modeCount> modeGains {
    1.00f, 0.74f, 0.60f, 0.50f, 0.42f, 0.36f, 0.32f, 0.28f
};
constexpr std::array<float, StudioPlateModule::plateSizeCount>
    plateBaseFrequencyHz { 255.0f, 150.0f, 92.0f };
constexpr std::array<float, StudioPlateModule::plateSizeCount>
    plateSizeScale { 0.55f, 1.0f, 1.85f };
constexpr std::array<float, StudioPlateModule::lineCount> lineMilliseconds {
    14.7f, 18.3f, 22.9f, 27.1f, 31.7f, 36.9f, 42.3f, 47.9f
};
constexpr std::array<float, StudioPlateModule::lineCount> injectionSigns {
    1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f
};
// Orthogonal to the all-ones pickup-mid vector (sums to zero), so scaling
// it by Pickup Spread and Width can never introduce side energy that
// survives an L+R fold-down.
constexpr std::array<float, StudioPlateModule::lineCount> pickupSideVector {
    1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f
};

float follow(float current, float target,
             float attackCoefficient, float releaseCoefficient) noexcept
{
    const auto coefficient =
        target > current ? attackCoefficient : releaseCoefficient;
    return coefficient * current + (1.0f - coefficient) * target;
}

float duckingActivity(float levelDb) noexcept
{
    auto activity = juce::jlimit(0.0f, 1.0f, (levelDb + 48.0f) / 36.0f);
    return activity * activity * (3.0f - 2.0f * activity);
}

void hadamard8(std::array<float, StudioPlateModule::lineCount>& values)
{
    constexpr auto count = StudioPlateModule::lineCount;
    for (int stride = 1; stride < count; stride *= 2)
        for (int group = 0; group < count; group += stride * 2)
            for (int index = 0; index < stride; ++index)
            {
                const auto a = values[static_cast<size_t>(group + index)];
                const auto b =
                    values[static_cast<size_t>(group + index + stride)];
                values[static_cast<size_t>(group + index)] = a + b;
                values[static_cast<size_t>(group + index + stride)] = a - b;
            }
    constexpr auto normalization = 0.35355339059f; // 1 / sqrt(8)
    for (auto& value : values)
        value *= normalization;
}
} // namespace

void StudioPlateModule::FractionalDelay::prepare(int capacity)
{
    buffer.assign(static_cast<size_t>(juce::jmax(4, capacity)), 0.0f);
    writePosition = 0;
}

void StudioPlateModule::FractionalDelay::reset()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writePosition = 0;
}

float StudioPlateModule::FractionalDelay::read(float delaySamples) const
{
    if (buffer.empty())
        return 0.0f;
    const auto size = static_cast<int>(buffer.size());
    auto position = static_cast<float>(writePosition)
        - juce::jlimit(1.0f, static_cast<float>(size - 2), delaySamples);
    if (position < 0.0f)
        position += static_cast<float>(size);
    const auto centre = static_cast<int>(position);
    const auto fraction = position - static_cast<float>(centre);
    const auto previous = centre > 0 ? centre - 1 : size - 1;
    const auto next = centre + 1 < size ? centre + 1 : 0;
    const auto afterNext = centre + 2 < size ? centre + 2 : centre + 2 - size;
    const auto y0 = buffer[static_cast<size_t>(previous)];
    const auto y1 = buffer[static_cast<size_t>(centre)];
    const auto y2 = buffer[static_cast<size_t>(next)];
    const auto y3 = buffer[static_cast<size_t>(afterNext)];
    const auto a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    const auto a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const auto a2 = -0.5f * y0 + 0.5f * y2;
    return ((a0 * fraction + a1) * fraction + a2) * fraction + y1;
}

void StudioPlateModule::FractionalDelay::write(float value)
{
    if (buffer.empty())
        return;
    buffer[static_cast<size_t>(writePosition)] = value;
    if (++writePosition >= static_cast<int>(buffer.size()))
        writePosition = 0;
}

void StudioPlateModule::DecayFilter::reset()
{
    lowState = 0.0f;
    highState = 0.0f;
}

float StudioPlateModule::DecayFilter::process(
    float input, float lowCoefficient, float highCoefficient,
    float lowGain, float midGain, float highGain)
{
    lowState = lowCoefficient * lowState + (1.0f - lowCoefficient) * input;
    highState = highCoefficient * highState + (1.0f - highCoefficient) * input;
    const auto low = lowState;
    const auto mid = highState - lowState;
    const auto high = input - highState;
    return low * lowGain + mid * midGain + high * highGain;
}

float StudioPlateModule::lineDelaySamples(int line, float sizeScale) const
{
    return baseDelaySamples[static_cast<size_t>(line)] * sizeScale;
}

void StudioPlateModule::updateModalTargets(
    float decaySeconds, float damping, int plateSizeIndex)
{
    const auto safeIndex =
        juce::jlimit(0, plateSizeCount - 1, plateSizeIndex);
    const auto baseFrequency =
        plateBaseFrequencyHz[static_cast<size_t>(safeIndex)];
    const auto nyquistLimit = static_cast<float>(sampleRate * 0.45);
    const auto modeHighRatio = detail::lerp(0.95f, 0.14f, damping);
    for (int mode = 0; mode < modeCount; ++mode)
    {
        const auto index = static_cast<size_t>(mode);
        const auto frequency = juce::jlimit(
            20.0f, nyquistLimit, baseFrequency * modeRatios[index]);
        modeFrequencySmoothed[index].setTargetValue(frequency);
        const auto position =
            static_cast<float>(mode) / static_cast<float>(modeCount - 1);
        const auto ratio = detail::lerp(1.0f, modeHighRatio, position);
        modeT60Smoothed[index].setTargetValue(
            juce::jmax(0.05f, decaySeconds * ratio));
    }
}

void StudioPlateModule::updateDecayTargets(
    float decaySeconds, float damping, float sizeScale)
{
    constexpr auto lowRatio = 1.35f;
    const auto highRatio = detail::lerp(0.9f, 0.12f, damping);
    const auto midT60 = juce::jmax(0.1f, decaySeconds);
    const auto lowT60 = juce::jmax(0.15f, midT60 * lowRatio);
    const auto highT60 = juce::jmax(0.05f, midT60 * highRatio);
    for (int line = 0; line < lineCount; ++line)
    {
        const auto index = static_cast<size_t>(line);
        const auto delaySeconds =
            lineDelaySamples(line, sizeScale) / static_cast<float>(sampleRate);
        targetLowGains[index] = std::pow(0.001f, delaySeconds / lowT60);
        targetMidGains[index] = std::pow(0.001f, delaySeconds / midT60);
        targetHighGains[index] = std::pow(0.001f, delaySeconds / highT60);
    }
}

void StudioPlateModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    for (int line = 0; line < lineCount; ++line)
        baseDelaySamples[static_cast<size_t>(line)] =
            lineMilliseconds[static_cast<size_t>(line)] * 0.001f
            * static_cast<float>(sampleRate);

    const auto delayCapacity = juce::roundToInt(sampleRate * 0.22);
    for (auto& delay : delays)
        delay.prepare(delayCapacity);
    preDelayLine.prepare(juce::roundToInt(sampleRate * 0.30));

    decayLowCoefficient = std::exp(
        -twoPi * 300.0f / static_cast<float>(sampleRate));
    decayHighCoefficient = std::exp(
        -twoPi * 4200.0f / static_cast<float>(sampleRate));
    decayCoefficientSmoothing =
        1.0f - std::exp(-1.0f / static_cast<float>(sampleRate * 0.06));

    for (auto* smoother : {
             &preDelaySmoothed, &excitationLowCoefficientSmoothed,
             &excitationHighCoefficientSmoothed, &pickupSpreadSmoothed,
             &widthSmoothed, &duckAmountSmoothed, &drySmoothed, &wetSmoothed })
        smoother->reset(sampleRate, 0.06);
    outputSmoothed.reset(sampleRate, 0.03);
    for (auto& smoother : modeFrequencySmoothed)
        smoother.reset(sampleRate, 0.15);
    for (auto& smoother : modeT60Smoothed)
        smoother.reset(sampleRate, 0.15);

    reset();
}

void StudioPlateModule::reset()
{
    for (auto& delay : delays)
        delay.reset();
    preDelayLine.reset();
    for (auto& filter : decayFilters)
        filter.reset();
    modalModes.fill(ModalMode {});
    lowGains.fill(0.0f);
    midGains.fill(0.0f);
    highGains.fill(0.0f);
    targetLowGains.fill(0.0f);
    targetMidGains.fill(0.0f);
    targetHighGains.fill(0.0f);
    excitationLowState = 0.0f;
    excitationHighState = 0.0f;
    duckEnvelope = 0.0f;
    duckGainDb = 0.0f;
    activePlateSizeIndex = 1;
    currentSizeScale = oldSizeScale = targetSizeScale =
        plateSizeScale[static_cast<size_t>(activePlateSizeIndex)];
    sizeFade = 1.0f;
    initialized = false;
    telemetrySequence = 0;
    telemetry.clear();
    meter.store(0.0f, std::memory_order_relaxed);
}

void StudioPlateModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() <= 0 || buffer.getNumSamples() <= 0)
        return;

    const auto requestedSizeIndex =
        discreteIndex(controls[plateSizeControl], plateSizeCount);
    const auto decayTarget = detail::exponential(
        0.5f, 8.0f,
        detail::normalizedControl(controls[decayControl], 0.565759f));
    const auto dampingTarget =
        detail::normalizedControl(controls[dampingControl], 0.45f);
    const auto preDelayTarget = detail::exponential(
        1.0f, 200.0f,
        detail::normalizedControl(controls[preDelayControl], 0.565412f))
        * 0.001f * static_cast<float>(sampleRate);
    const auto lowCutHz = detail::exponential(
        20.0f, 800.0f,
        detail::normalizedControl(controls[lowCutControl], 0.375804f));
    const auto highCutHz = detail::exponential(
        2000.0f, 20000.0f,
        detail::normalizedControl(controls[highCutControl], 0.778151f));
    const auto excitationLowCoefficientTarget = std::exp(
        -twoPi * lowCutHz / static_cast<float>(sampleRate));
    const auto excitationHighCoefficientTarget = std::exp(
        -twoPi * highCutHz / static_cast<float>(sampleRate));
    const auto pickupSpreadTarget =
        detail::normalizedControl(controls[pickupSpreadControl], 0.65f);
    const auto widthTarget =
        detail::normalizedControl(controls[widthControl], 0.75f);
    const auto duckAmountTarget = detail::lerp(
        0.0f, 18.0f,
        detail::normalizedControl(controls[duckingControl], 0.0f));
    const auto dryTarget =
        detail::normalizedControl(controls[dryControl], 1.0f);
    const auto wetTarget =
        detail::normalizedControl(controls[wetControl], 0.18f);
    const auto outputTarget = juce::Decibels::decibelsToGain(detail::lerp(
        -18.0f, 12.0f,
        detail::normalizedControl(controls[outputControl], 0.6f)));

    auto setTarget = [this](auto& smoother, float target)
    {
        if (initialized)
            smoother.setTargetValue(target);
        else
            smoother.setCurrentAndTargetValue(target);
    };
    setTarget(preDelaySmoothed, preDelayTarget);
    setTarget(excitationLowCoefficientSmoothed, excitationLowCoefficientTarget);
    setTarget(
        excitationHighCoefficientSmoothed, excitationHighCoefficientTarget);
    setTarget(pickupSpreadSmoothed, pickupSpreadTarget);
    setTarget(widthSmoothed, widthTarget);
    setTarget(duckAmountSmoothed, duckAmountTarget);
    setTarget(drySmoothed, dryTarget);
    setTarget(wetSmoothed, wetTarget);
    setTarget(outputSmoothed, outputTarget);

    if (!initialized)
    {
        activePlateSizeIndex = requestedSizeIndex;
        currentSizeScale = oldSizeScale = targetSizeScale =
            plateSizeScale[static_cast<size_t>(activePlateSizeIndex)];
        sizeFade = 1.0f;
        updateModalTargets(decayTarget, dampingTarget, activePlateSizeIndex);
        for (auto& smoother : modeFrequencySmoothed)
            smoother.setCurrentAndTargetValue(smoother.getTargetValue());
        for (auto& smoother : modeT60Smoothed)
            smoother.setCurrentAndTargetValue(smoother.getTargetValue());
        updateDecayTargets(decayTarget, dampingTarget, targetSizeScale);
        lowGains = targetLowGains;
        midGains = targetMidGains;
        highGains = targetHighGains;
        initialized = true;
    }
    else
    {
        // Plate Size changes crossfade delay taps in place (see the
        // per-sample loop below) rather than resetting the FDN state, so
        // the existing tail keeps ringing through the transition.
        if (sizeFade >= 1.0f && requestedSizeIndex != activePlateSizeIndex)
        {
            oldSizeScale = currentSizeScale;
            targetSizeScale =
                plateSizeScale[static_cast<size_t>(requestedSizeIndex)];
            activePlateSizeIndex = requestedSizeIndex;
            sizeFade = 0.0f;
        }
        updateModalTargets(decayTarget, dampingTarget, activePlateSizeIndex);
        updateDecayTargets(decayTarget, dampingTarget, targetSizeScale);
    }

    const auto sizeFadeIncrement =
        1.0f / static_cast<float>(sampleRate * 0.18);
    const auto duckDetectorAttack = detail::coefficient(sampleRate, 5.0f);
    const auto duckDetectorRelease = detail::coefficient(sampleRate, 180.0f);
    const auto duckGainAttack = detail::coefficient(sampleRate, 8.0f);
    const auto duckGainRelease = detail::coefficient(sampleRate, 350.0f);

    double excitationEnergy = 0.0;
    double leftPickupEnergy = 0.0;
    double rightPickupEnergy = 0.0;
    double lowBandEnergy = 0.0;
    double midBandEnergy = 0.0;
    double highBandEnergy = 0.0;
    double modalEnergy = 0.0;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto preDelaySamples =
            juce::jmax(0.0f, preDelaySmoothed.getNextValue());
        const auto excitationLowCoefficient =
            excitationLowCoefficientSmoothed.getNextValue();
        const auto excitationHighCoefficient =
            excitationHighCoefficientSmoothed.getNextValue();
        const auto pickupSpread = pickupSpreadSmoothed.getNextValue();
        const auto width = widthSmoothed.getNextValue();
        const auto duckAmountDb = duckAmountSmoothed.getNextValue();
        const auto dryGain = drySmoothed.getNextValue();
        const auto wetGain = wetSmoothed.getNextValue();
        const auto outputGain = outputSmoothed.getNextValue();

        const std::array<float, 2> dry {
            detail::finiteSample(buffer.getSample(0, sample)),
            buffer.getNumChannels() > 1
                ? detail::finiteSample(buffer.getSample(1, sample))
                : detail::finiteSample(buffer.getSample(0, sample))
        };
        const auto monoIn = 0.5f * (dry[0] + dry[1]);

        excitationLowState =
            excitationLowCoefficient * excitationLowState
            + (1.0f - excitationLowCoefficient) * monoIn;
        const auto highPassed = monoIn - excitationLowState;
        excitationHighState =
            excitationHighCoefficient * excitationHighState
            + (1.0f - excitationHighCoefficient) * highPassed;
        const auto excitationRaw = detail::finiteSample(excitationHighState);

        preDelayLine.write(excitationRaw);
        const auto excitation = preDelayLine.read(preDelaySamples);
        excitationEnergy += static_cast<double>(excitation) * excitation;

        float modalSum = 0.0f;
        for (int mode = 0; mode < modeCount; ++mode)
        {
            const auto index = static_cast<size_t>(mode);
            const auto frequency = modeFrequencySmoothed[index].getNextValue();
            const auto t60 = modeT60Smoothed[index].getNextValue();
            const auto r = std::pow(
                0.001f,
                1.0f / juce::jmax(1.0f, t60 * static_cast<float>(sampleRate)));
            const auto theta = twoPi * frequency / static_cast<float>(sampleRate);
            const auto coefficient = 2.0f * r * std::cos(theta);
            auto& state = modalModes[index];
            auto y = coefficient * state.y1 - (r * r) * state.y2
                     + (1.0f - r) * excitation;
            y = detail::finiteSample(juce::jlimit(-8.0f, 8.0f, y));
            state.y2 = state.y1;
            state.y1 = y;
            modalSum += y * modeGains[index];
        }
        modalSum = detail::finiteSample(modalSum * 0.28f);
        modalEnergy += static_cast<double>(modalSum) * modalSum;

        std::array<float, lineCount> tankOutput {};
        for (int line = 0; line < lineCount; ++line)
        {
            const auto index = static_cast<size_t>(line);
            const auto newDelay = lineDelaySamples(line, targetSizeScale);
            if (sizeFade < 1.0f)
            {
                const auto oldDelay = lineDelaySamples(line, oldSizeScale);
                tankOutput[index] =
                    delays[index].read(oldDelay) * (1.0f - sizeFade)
                    + delays[index].read(newDelay) * sizeFade;
            }
            else
                tankOutput[index] = delays[index].read(newDelay);
        }
        if (sizeFade < 1.0f)
        {
            sizeFade = juce::jmin(1.0f, sizeFade + sizeFadeIncrement);
            if (sizeFade >= 1.0f)
                currentSizeScale = targetSizeScale;
        }

        auto feedback = tankOutput;
        hadamard8(feedback);

        for (int line = 0; line < lineCount; ++line)
        {
            const auto index = static_cast<size_t>(line);
            lowGains[index] +=
                (targetLowGains[index] - lowGains[index])
                * decayCoefficientSmoothing;
            midGains[index] +=
                (targetMidGains[index] - midGains[index])
                * decayCoefficientSmoothing;
            highGains[index] +=
                (targetHighGains[index] - highGains[index])
                * decayCoefficientSmoothing;

            const auto decayed = decayFilters[index].process(
                feedback[index], decayLowCoefficient, decayHighCoefficient,
                lowGains[index], midGains[index], highGains[index]);
            const auto lowComponent = decayFilters[index].lowState
                                      * lowGains[index];
            const auto midComponent =
                (decayFilters[index].highState - decayFilters[index].lowState)
                * midGains[index];
            const auto highComponent =
                (feedback[index] - decayFilters[index].highState)
                * highGains[index];
            lowBandEnergy +=
                static_cast<double>(lowComponent) * lowComponent;
            midBandEnergy +=
                static_cast<double>(midComponent) * midComponent;
            highBandEnergy +=
                static_cast<double>(highComponent) * highComponent;

            const auto inject =
                excitation * injectionSigns[index] * 0.42f
                + modalSum * injectionSigns[index] * 0.30f;
            auto next = decayed + inject;
            if (!std::isfinite(next))
                next = 0.0f;
            next = juce::jlimit(-16.0f, 16.0f, next);
            delays[index].write(next);
        }

        float mid = 0.0f;
        float sideRaw = 0.0f;
        for (int line = 0; line < lineCount; ++line)
        {
            const auto index = static_cast<size_t>(line);
            const auto value = tankOutput[index];
            mid += value;
            sideRaw += value * pickupSideVector[index];
        }
        constexpr auto pickupNormalization = 0.15f;
        mid *= pickupNormalization;
        const auto side =
            sideRaw * pickupNormalization * pickupSpread * width;

        const auto linkedInput =
            juce::jmax(std::abs(dry[0]), std::abs(dry[1]));
        duckEnvelope = follow(
            duckEnvelope, linkedInput,
            duckDetectorAttack, duckDetectorRelease);
        const auto targetDuckGainDb = -duckAmountDb * duckingActivity(
            juce::Decibels::gainToDecibels(duckEnvelope, -100.0f));
        duckGainDb = follow(
            duckGainDb, targetDuckGainDb, duckGainRelease, duckGainAttack);
        const auto duckGain = juce::Decibels::decibelsToGain(duckGainDb);

        const auto wetLeft = (mid + side) * duckGain;
        const auto wetRight = (mid - side) * duckGain;
        leftPickupEnergy += static_cast<double>(wetLeft) * wetLeft;
        rightPickupEnergy += static_cast<double>(wetRight) * wetRight;

        if (buffer.getNumChannels() == 1)
        {
            const auto outMono = detail::finiteSample(
                (dry[0] * dryGain + mid * duckGain * wetGain) * outputGain);
            buffer.setSample(0, sample, outMono);
        }
        else
        {
            for (int channel = 0; channel < buffer.getNumChannels();
                 ++channel)
            {
                const auto channelIndex =
                    static_cast<size_t>(juce::jmin(channel, 1));
                const auto wetSample =
                    channelIndex == 0 ? wetLeft : wetRight;
                buffer.setSample(
                    channel, sample,
                    detail::finiteSample(
                        (dry[channelIndex] * dryGain
                         + wetSample * wetGain)
                        * outputGain));
            }
        }
    }

    const auto sampleCount =
        static_cast<double>(juce::jmax(1, buffer.getNumSamples()));
    const auto leftPickupRms =
        static_cast<float>(std::sqrt(leftPickupEnergy / sampleCount));
    const auto rightPickupRms =
        static_cast<float>(std::sqrt(rightPickupEnergy / sampleCount));
    meter.store(
        juce::jlimit(0.0f, 1.0f, juce::jmax(leftPickupRms, rightPickupRms)),
        std::memory_order_relaxed);

    if (environment.captureTelemetry)
    {
        ContinuousTelemetrySnapshot snapshot;
        snapshot.sequence = ++telemetrySequence;
        snapshot.valueCount = telemetryValueCount;
        snapshot.values[excitationLevel] = static_cast<float>(
            std::sqrt(excitationEnergy / sampleCount));
        snapshot.values[leftPickupLevel] = leftPickupRms;
        snapshot.values[rightPickupLevel] = rightPickupRms;
        snapshot.values[lowBandLevel] = static_cast<float>(
            std::sqrt(lowBandEnergy / sampleCount));
        snapshot.values[midBandLevel] = static_cast<float>(
            std::sqrt(midBandEnergy / sampleCount));
        snapshot.values[highBandLevel] = static_cast<float>(
            std::sqrt(highBandEnergy / sampleCount));
        snapshot.values[modalActivityLevel] = static_cast<float>(
            std::sqrt(modalEnergy / sampleCount));
        telemetry.publish(snapshot);
    }
}

double StudioPlateModule::tailSeconds(const ControlValues& controls) const
{
    const auto decaySeconds = detail::exponential(
        0.5f, 8.0f,
        detail::normalizedControl(controls[decayControl], 0.565759f));
    const auto preDelaySeconds = detail::exponential(
        1.0f, 200.0f,
        detail::normalizedControl(controls[preDelayControl], 0.565412f))
        * 0.001f;
    constexpr auto lowRatio = 1.35f;
    return juce::jlimit(
        0.2, 8.0,
        static_cast<double>(decaySeconds * lowRatio + preDelaySeconds + 0.05f));
}

bool StudioPlateModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}

// Mirrors ModuleRegistry.cpp's defaultsFor(ModuleType::studioPlate); kept
// self-contained so this module, its view, and its tests stay usable even
// if the shared registry wiring changes.
ControlValues StudioPlateModule::defaultControlValues()
{
    ControlValues values {};
    values[plateSizeControl] = discreteValue(1, plateSizeCount);
    values[decayControl] = 0.565759f;
    values[dampingControl] = 0.45f;
    values[preDelayControl] = 0.565412f;
    values[lowCutControl] = 0.375804f;
    values[highCutControl] = 0.778151f;
    values[pickupSpreadControl] = 0.65f;
    values[widthControl] = 0.75f;
    values[duckingControl] = 0.0f;
    values[dryControl] = 1.0f;
    values[wetControl] = 0.18f;
    values[outputControl] = 0.6f;
    return values;
}
} // namespace megadsp
