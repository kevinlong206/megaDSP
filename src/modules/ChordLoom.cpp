#include "ChordLoom.h"
#include "DspHelpers.h"
#include "DspSafety.h"

#include <cmath>
#include <limits>

namespace megadsp
{
namespace
{
using detail::exponential;
using detail::lerp;

float onePoleCoefficient(double rate, float frequency)
{
    return 1.0f - std::exp(
        -juce::MathConstants<float>::twoPi * frequency
        / static_cast<float>(rate));
}
} // namespace

float ChordLoomModule::envelopeCoefficient(
    double rate, float milliseconds) noexcept
{
    return std::exp(
        -1.0f / juce::jmax(
            1.0f, milliseconds * 0.001f * static_cast<float>(rate)));
}

std::uint32_t ChordLoomModule::nextRandom(Layer& layer) noexcept
{
    auto value = layer.randomState;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    layer.randomState = value != 0 ? value : 0x9e3779b9u;
    return layer.randomState;
}

float ChordLoomModule::randomUnit(Layer& layer) noexcept
{
    return static_cast<float>(nextRandom(layer) >> 8)
           * (1.0f / 16777216.0f);
}

float ChordLoomModule::readLinear(
    const Layer& layer, int channel, float position) noexcept
{
    if (layer.capturedSamples < 2)
        return 0.0f;
    const auto maximum = static_cast<float>(layer.capturedSamples - 2);
    const auto safePosition = juce::jlimit(0.0f, maximum, position);
    const auto index = static_cast<int>(safePosition);
    const auto fraction = safePosition - static_cast<float>(index);
    const auto& source =
        layer.audio[static_cast<size_t>(juce::jlimit(0, 1, channel))];
    return source[static_cast<size_t>(index)]
        + fraction * (source[static_cast<size_t>(index + 1)]
                      - source[static_cast<size_t>(index)]);
}

void ChordLoomModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    const auto capacity = static_cast<size_t>(
        std::ceil(sampleRate * maximumCaptureSeconds) + 4.0);
    for (auto& layer : layers)
    {
        for (auto& channel : layer.audio)
            channel.assign(capacity, 0.0f);
        layer.gain.reset(sampleRate, 0.35);
    }
    for (auto* smoother : { &textureSmoothed, &motionSmoothed,
                            &bloomSmoothed, &spreadSmoothed, &drySmoothed,
                            &sustainSmoothed })
        smoother->reset(sampleRate, 0.04);
    for (auto* smoother : {
             &lowCutSmoothed, &highCutSmoothed, &outputSmoothed })
        smoother->reset(sampleRate, 0.04);
    fastAttackCoefficient = envelopeCoefficient(sampleRate, 2.0f);
    fastReleaseCoefficient = envelopeCoefficient(sampleRate, 70.0f);
    slowCoefficient = envelopeCoefficient(sampleRate, 450.0f);
    reset();
}

void ChordLoomModule::reset()
{
    for (int index = 0; index < layerCount; ++index)
    {
        auto& layer = layers[static_cast<size_t>(index)];
        layer.gain.setCurrentAndTargetValue(0.0f);
        layer.grains = {};
        layer.randomState =
            0x9e3779b9u ^ (0x85ebca6bu * static_cast<std::uint32_t>(index + 1));
        layer.capturedSamples = 0;
        layer.captureLimit = 0;
        layer.capturing = false;
        layer.ready = false;
    }
    lowCutState.fill(0.0f);
    highCutState.fill(0.0f);
    fastEnvelope = 0.0f;
    slowEnvelope = 0.0f;
    samplesSinceCapture = juce::roundToInt(sampleRate);
    nextCaptureLayer = 0;
    onsetArmed = true;
    initialized = false;
    telemetrySequence = 0;
    telemetry.clear();
}

void ChordLoomModule::beginCapture(int layerIndex, int captureSamples)
{
    auto& layer = layers[static_cast<size_t>(
        juce::jlimit(0, layerCount - 1, layerIndex))];
    layer.capturedSamples = 0;
    layer.captureLimit = juce::jlimit(
        2, static_cast<int>(layer.audio[0].size()), captureSamples);
    layer.capturing = true;
    layer.ready = false;
    layer.grains = {};
    layer.randomState ^= 0x27d4eb2du;
    layer.gain.setTargetValue(0.0f);
}

void ChordLoomModule::startGrain(
    Layer& layer, Grain& grain, int voice, float texture,
    float motion)
{
    const auto baseSeconds = lerp(0.55f, 0.11f, texture);
    const auto durationVariation = lerp(
        0.82f, 1.18f, randomUnit(layer));
    const auto maximumDuration = juce::jmax(
        64, static_cast<int>(
                static_cast<float>(layer.capturedSamples)
                / 2.2f));
    grain.duration = juce::jlimit(
        64, maximumDuration,
        juce::roundToInt(baseSeconds * durationVariation * sampleRate));
    const auto cents = (randomUnit(layer) * 2.0f - 1.0f) * motion;
    grain.increment = std::pow(2.0f, cents / 1200.0f);
    const auto maximumTravel = static_cast<float>(grain.duration)
        * juce::jmax(grain.increment, grain.increment * 2.0f);
    const auto latestStart = juce::jmax(
        0.0f, static_cast<float>(layer.capturedSamples - 2) - maximumTravel);
    grain.position = randomUnit(layer) * latestStart;
    grain.octavePosition = grain.position;
    const auto voicePan = static_cast<float>(voice * 2)
                              / static_cast<float>(grainsPerLayer - 1)
                          - 1.0f;
    grain.pan = juce::jlimit(
        -1.0f, 1.0f,
        voicePan * 0.82f
            + (randomUnit(layer) * 2.0f - 1.0f) * texture * 0.12f);
    grain.age = 0;
    grain.active = true;
}

std::array<float, 2> ChordLoomModule::renderLayer(
    Layer& layer, int channels, float texture, float motion,
    float bloom, float spread)
{
    std::array<float, 2> output {};
    if (!layer.ready)
        return output;
    for (int voice = 0; voice < grainsPerLayer; ++voice)
    {
        auto& grain = layer.grains[static_cast<size_t>(voice)];
        if (!grain.active || grain.age >= grain.duration)
            startGrain(layer, grain, voice, texture, motion);
        const auto phase = (static_cast<float>(grain.age) + 0.5f)
                           / static_cast<float>(grain.duration);
        const auto window =
            std::sin(juce::MathConstants<float>::pi * phase);
        const auto windowSquared = window * window;
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto normal =
                readLinear(layer, channel, grain.position);
            const auto octave =
                readLinear(layer, channel, grain.octavePosition);
            const auto panGain = channels > 1
                ? std::sqrt(juce::jmax(
                      0.0f, 1.0f
                          + (channel == 0 ? -grain.pan : grain.pan) * spread))
                : 1.0f;
            output[static_cast<size_t>(channel)] +=
                lerp(normal, octave, bloom) * windowSquared * panGain;
        }
        grain.position += grain.increment;
        grain.octavePosition += grain.increment * 2.0f;
        ++grain.age;
    }
    constexpr auto normalization = 2.0f / static_cast<float>(grainsPerLayer);
    output[0] *= normalization;
    output[1] *= normalization;
    return output;
}

void ChordLoomModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    const auto channels = juce::jlimit(0, 2, buffer.getNumChannels());
    if (channels == 0 || buffer.getNumSamples() <= 0)
        return;

    const auto sensitivity =
        detail::normalizedControl(controls[0], 0.62f);
    const auto captureSeconds = exponential(
        0.25f, maximumCaptureSeconds,
        detail::normalizedControl(controls[1], 0.62f));
    const auto transitionSeconds = exponential(
        0.10f, 2.0f, detail::normalizedControl(controls[2], 0.54f));
    const auto texture = detail::normalizedControl(controls[3], 0.42f);
    const auto motion =
        detail::normalizedControl(controls[4], 0.25f) * 20.0f;
    const auto bloom =
        detail::normalizedControl(controls[5], 0.12f) * 0.5f;
    const auto spread = detail::normalizedControl(controls[6], 0.65f);
    const auto lowCut = exponential(
        20.0f, 1000.0f, detail::normalizedControl(controls[7], 0.35f));
    const auto highCut = exponential(
        2000.0f, 20000.0f, detail::normalizedControl(controls[8], 0.70f));
    const auto dry = detail::normalizedControl(controls[9], 1.0f);
    const auto sustain = detail::normalizedControl(controls[10], 0.55f);
    const auto output = juce::Decibels::decibelsToGain(
        lerp(-18.0f, 12.0f, detail::normalizedControl(controls[11], 0.6f)));
    textureSmoothed.setTargetValue(texture);
    motionSmoothed.setTargetValue(motion);
    bloomSmoothed.setTargetValue(bloom);
    spreadSmoothed.setTargetValue(spread);
    lowCutSmoothed.setTargetValue(lowCut);
    highCutSmoothed.setTargetValue(highCut);
    drySmoothed.setTargetValue(dry);
    sustainSmoothed.setTargetValue(sustain);
    outputSmoothed.setTargetValue(output);
    if (!initialized)
    {
        textureSmoothed.setCurrentAndTargetValue(texture);
        motionSmoothed.setCurrentAndTargetValue(motion);
        bloomSmoothed.setCurrentAndTargetValue(bloom);
        spreadSmoothed.setCurrentAndTargetValue(spread);
        lowCutSmoothed.setCurrentAndTargetValue(lowCut);
        highCutSmoothed.setCurrentAndTargetValue(highCut);
        drySmoothed.setCurrentAndTargetValue(dry);
        sustainSmoothed.setCurrentAndTargetValue(sustain);
        outputSmoothed.setCurrentAndTargetValue(output);
        initialized = true;
    }

    const auto captureSamples =
        juce::roundToInt(captureSeconds * static_cast<float>(sampleRate));
    const auto readySamples = juce::jmin(
        captureSamples,
        juce::jmax(2048, juce::roundToInt(0.18 * sampleRate)));
    const auto requiredInput = juce::Decibels::decibelsToGain(
        lerp(-24.0f, -60.0f, sensitivity));
    const auto requiredOnset = lerp(7.0f, 2.0f, sensitivity);
    const auto refractorySamples = juce::roundToInt(0.28 * sampleRate);
    float latestOnset = 0.0f;
    float latestCaptureProgress = 0.0f;
    float maximumWet = 0.0f;
    bool capturedThisBlock = false;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        std::array<float, 2> input {
            detail::finiteSample(buffer.getSample(0, sample)),
            channels > 1
                ? detail::finiteSample(buffer.getSample(1, sample))
                : detail::finiteSample(buffer.getSample(0, sample))
        };
        const auto inputLevel =
            juce::jmax(std::abs(input[0]), std::abs(input[1]));
        const auto fastCoefficient =
            inputLevel > fastEnvelope
                ? fastAttackCoefficient : fastReleaseCoefficient;
        fastEnvelope = fastCoefficient * fastEnvelope
                       + (1.0f - fastCoefficient) * inputLevel;
        slowEnvelope = slowCoefficient * slowEnvelope
                       + (1.0f - slowCoefficient) * inputLevel;
        const auto onset = fastEnvelope / (slowEnvelope + 0.004f);
        latestOnset = onset;
        ++samplesSinceCapture;

        if (!onsetArmed && samplesSinceCapture >= refractorySamples
            && onset < 1.35f)
            onsetArmed = true;

        if (onsetArmed && inputLevel >= requiredInput
            && onset >= requiredOnset
            && samplesSinceCapture >= refractorySamples)
        {
            auto destination = nextCaptureLayer;
            auto quietestScore = std::numeric_limits<float>::max();
            for (int offset = 0; offset < layerCount; ++offset)
            {
                const auto layer = (nextCaptureLayer + offset) % layerCount;
                const auto& candidate = layers[static_cast<size_t>(layer)];
                const auto score = candidate.gain.getCurrentValue()
                    + (candidate.capturing ? 2.0f : 0.0f);
                if (score < quietestScore)
                {
                    destination = layer;
                    quietestScore = score;
                }
            }
            beginCapture(destination, captureSamples);
            nextCaptureLayer = (destination + 1) % layerCount;
            samplesSinceCapture = 0;
            onsetArmed = false;
            capturedThisBlock = true;
        }

        for (int layerIndex = 0; layerIndex < layerCount; ++layerIndex)
        {
            auto& layer = layers[static_cast<size_t>(layerIndex)];
            if (!layer.capturing)
                continue;
            for (int channel = 0; channel < 2; ++channel)
                layer.audio[static_cast<size_t>(channel)]
                           [static_cast<size_t>(layer.capturedSamples)] =
                    input[static_cast<size_t>(channel)];
            ++layer.capturedSamples;
            latestCaptureProgress = juce::jmax(
                latestCaptureProgress,
                static_cast<float>(layer.capturedSamples)
                    / static_cast<float>(juce::jmax(1, layer.captureLimit)));
            if (!layer.ready && layer.capturedSamples >= readySamples)
            {
                layer.ready = true;
                for (auto& other : layers)
                {
                    const auto currentGain = other.gain.getCurrentValue();
                    other.gain.reset(sampleRate, transitionSeconds);
                    other.gain.setCurrentAndTargetValue(currentGain);
                    other.gain.setTargetValue(&other == &layer ? 1.0f : 0.0f);
                }
            }
            if (layer.capturedSamples >= layer.captureLimit)
                layer.capturing = false;
        }

        const auto currentTexture = textureSmoothed.getNextValue();
        const auto currentMotion = motionSmoothed.getNextValue();
        const auto currentBloom = bloomSmoothed.getNextValue();
        const auto currentSpread = spreadSmoothed.getNextValue();
        std::array<float, 2> wet {};
        for (int layerIndex = 0; layerIndex < layerCount; ++layerIndex)
        {
            auto& layer = layers[static_cast<size_t>(layerIndex)];
            const auto gain = layer.gain.getNextValue();
            if (gain <= 0.00001f)
                continue;
            const auto layerOutput = renderLayer(
                layer, channels, currentTexture, currentMotion,
                currentBloom, currentSpread);
            wet[0] += layerOutput[0] * gain;
            wet[1] += layerOutput[1] * gain;
        }

        const auto lowCoefficient = onePoleCoefficient(
            sampleRate, lowCutSmoothed.getNextValue());
        const auto highCoefficient = onePoleCoefficient(
            sampleRate, juce::jmin(
                highCutSmoothed.getNextValue(),
                static_cast<float>(sampleRate * 0.45)));
        const auto currentDry = drySmoothed.getNextValue();
        const auto currentSustain = sustainSmoothed.getNextValue();
        const auto currentOutput = outputSmoothed.getNextValue();
        for (int channel = 0; channel < channels; ++channel)
        {
            auto& lowState = lowCutState[static_cast<size_t>(channel)];
            lowState += lowCoefficient
                        * (wet[static_cast<size_t>(channel)] - lowState);
            const auto highPassed =
                wet[static_cast<size_t>(channel)] - lowState;
            auto& highState = highCutState[static_cast<size_t>(channel)];
            highState += highCoefficient * (highPassed - highState);
            const auto held = std::tanh(highState * 1.2f) / 1.2f;
            maximumWet = juce::jmax(maximumWet, std::abs(held));
            const auto result =
                (input[static_cast<size_t>(channel)] * currentDry
                 + held * currentSustain) * currentOutput;
            buffer.setSample(
                channel, sample, detail::finiteSample(result));
        }
    }

    if (environment.captureTelemetry)
    {
        auto loudestLayer = -1;
        auto loudestGain = 0.0f;
        for (int layer = 0; layer < layerCount; ++layer)
        {
            const auto gain =
                layers[static_cast<size_t>(layer)].gain.getCurrentValue();
            if (gain > loudestGain)
            {
                loudestGain = gain;
                loudestLayer = layer;
            }
        }
        ContinuousTelemetrySnapshot snapshot;
        snapshot.sequence = ++telemetrySequence;
        snapshot.valueCount = telemetryValueCount;
        snapshot.values[inputEnvelope] = fastEnvelope;
        snapshot.values[inputThreshold] = requiredInput;
        snapshot.values[onsetStrength] = latestOnset;
        snapshot.values[onsetThreshold] = requiredOnset;
        snapshot.values[captureTriggered] = capturedThisBlock ? 1.0f : 0.0f;
        snapshot.values[captureProgress] =
            juce::jlimit(0.0f, 1.0f, latestCaptureProgress);
        snapshot.values[activeLayer] = static_cast<float>(loudestLayer);
        snapshot.values[layerGain0] = layers[0].gain.getCurrentValue();
        snapshot.values[layerGain1] = layers[1].gain.getCurrentValue();
        snapshot.values[layerGain2] = layers[2].gain.getCurrentValue();
        snapshot.values[wetEnvelope] = maximumWet;
        telemetry.publish(snapshot);
    }
}

double ChordLoomModule::tailSeconds(const ControlValues& controls) const
{
    return detail::normalizedControl(controls[10], 0.55f) > 0.0001f
        ? 120.0 : 0.0;
}

bool ChordLoomModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}
} // namespace megadsp
