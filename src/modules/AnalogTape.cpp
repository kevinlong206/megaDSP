#include "AnalogTape.h"
#include "DspHelpers.h"

#include <cmath>

namespace megadsp
{
using detail::coefficient;
using detail::lerp;

namespace
{
struct MachineProfile
{
    float hfCutoffHz;
    float driveHardness;
    float noiseFloorDb;
    float wowFlutterScale;
    float headBumpFreqHz;
    float headBumpQ;
    float biasSensitivity;
    float hysteresisAmount;
    float compressionAmount;
};

// Worn Cassette, Consumer Reel, Ampex-Style Deck, Studer-Style Deck.
constexpr std::array<MachineProfile, AnalogTapeModule::machineCount>
    machineProfiles {{
        { 9000.0f, 1.35f, -46.0f, 1.6f, 90.0f, 0.9f, 1.3f, 0.35f, 0.5f },
        { 14000.0f, 1.0f, -56.0f, 1.0f, 70.0f, 1.0f, 1.0f, 0.22f, 0.35f },
        { 17000.0f, 0.85f, -66.0f, 0.55f, 60.0f, 1.1f, 0.85f, 0.16f, 0.28f },
        { 19500.0f, 0.7f, -72.0f, 0.35f, 55.0f, 1.3f, 0.75f, 0.12f, 0.22f }
    }};

struct SpeedProfile
{
    float hfMultiplier;
    float noiseMultiplier;
    float wowFlutterMultiplier;
    float headBumpFreqMultiplier;
};

// 3.75 ips, 7.5 ips, 15 ips, 30 ips.
constexpr std::array<SpeedProfile, AnalogTapeModule::speedCount>
    speedProfiles {{
        { 0.55f, 1.8f, 1.6f, 0.55f },
        { 0.78f, 1.3f, 1.2f, 0.78f },
        { 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.28f, 0.65f, 0.7f, 1.5f }
    }};

constexpr float baseDelayMs = 6.0f;
constexpr float maximumWowMs = 3.0f;
constexpr float maximumFlutterMs = 1.2f;
constexpr float wowRateHz = 0.85f;
constexpr float flutterRateHz = 9.5f;
constexpr float headBumpMaximumGainDb = 12.0f;
} // namespace

float AnalogTapeModule::Biquad::process(float input)
{
    const auto output = b0 * input + z1;
    z1 = b1 * input - a1 * output + z2;
    z2 = b2 * input - a2 * output;
    return output;
}

void AnalogTapeModule::Biquad::setPeak(
    double rate, float frequency, float q, float gainDb)
{
    const auto a = std::pow(10.0f, gainDb / 40.0f);
    const auto omega = juce::MathConstants<float>::twoPi * frequency
                       / static_cast<float>(rate);
    const auto alpha = std::sin(omega) / (2.0f * q);
    const auto cosOmega = std::cos(omega);
    const auto a0 = 1.0f + alpha / a;
    b0 = (1.0f + alpha * a) / a0;
    b1 = (-2.0f * cosOmega) / a0;
    b2 = (1.0f - alpha * a) / a0;
    a1 = (-2.0f * cosOmega) / a0;
    a2 = (1.0f - alpha / a) / a0;
}

void AnalogTapeModule::Biquad::reset()
{
    z1 = 0.0f;
    z2 = 0.0f;
}

void AnalogTapeModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = juce::jmax(8000.0, spec.sampleRate);
    oversampling = std::make_unique<juce::dsp::Oversampling<float>>(
        static_cast<size_t>(spec.numChannels), 2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    oversampling->initProcessing(spec.maximumBlockSize);
    const auto oversamplingLatency = juce::jmax(
        1, juce::roundToInt(oversampling->getLatencyInSamples()));
    const auto transportLatency = juce::roundToInt(
        baseDelayMs * 0.001 * sampleRate);
    processingLatency = oversamplingLatency + transportLatency;
    dryBuffer.setSize(static_cast<int>(spec.numChannels),
                      static_cast<int>(spec.maximumBlockSize), false, true, true);
    for (auto& channel : dryLatencyBuffer)
        channel.assign(static_cast<size_t>(processingLatency), 0.0f);
    const auto wowFlutterCapacity = static_cast<size_t>(
        std::ceil(sampleRate * 0.05) + 8.0);
    for (auto& channel : wowFlutterBuffer)
        channel.assign(wowFlutterCapacity, 0.0f);

    const auto blockRampSeconds = 0.05;
    inputSmoothed.reset(sampleRate, blockRampSeconds);
    driveSmoothed.reset(sampleRate, blockRampSeconds);
    biasSmoothed.reset(sampleRate, blockRampSeconds);
    driveHardnessSmoothed.reset(sampleRate, blockRampSeconds);
    biasSensitivitySmoothed.reset(sampleRate, blockRampSeconds);
    hysteresisSmoothed.reset(sampleRate, blockRampSeconds);
    compressionSmoothed.reset(sampleRate, blockRampSeconds);
    hfCutoffSmoothed.reset(sampleRate, blockRampSeconds);
    headBumpFreqSmoothed.reset(sampleRate, blockRampSeconds);
    headBumpQSmoothed.reset(sampleRate, blockRampSeconds);
    wowFlutterScaleSmoothed.reset(sampleRate, blockRampSeconds);
    noiseFloorSmoothed.reset(sampleRate, blockRampSeconds);

    headBumpSmoothed.reset(sampleRate, 0.03);
    wowSmoothed.reset(sampleRate, 0.05);
    flutterSmoothed.reset(sampleRate, 0.05);
    wearSmoothed.reset(sampleRate, 0.05);
    noiseSmoothed.reset(sampleRate, 0.05);
    mixSmoothed.reset(sampleRate, 0.025);
    outputSmoothed.reset(sampleRate, 0.025);
    reset();
}

void AnalogTapeModule::reset()
{
    if (oversampling != nullptr)
        oversampling->reset();
    for (auto& channel : dryLatencyBuffer)
        std::fill(channel.begin(), channel.end(), 0.0f);
    for (auto& channel : wowFlutterBuffer)
        std::fill(channel.begin(), channel.end(), 0.0f);
    for (auto& filter : headBumpFilters)
        filter.reset();
    hfLossState.fill(0.0f);
    tapeMemoryState.fill(0.0f);
    compressionEnvelope.fill(0.0f);
    noiseShapeState.fill(0.0f);
    noiseEnvelope.fill(0.0f);
    wearDropoutState.fill(0.0f);
    noiseState = { 0x9e3779b9u, 0x85ebca6bu };
    dryLatencyPosition = 0;
    wowFlutterWritePosition = 0;
    wowPhase = 0.0f;
    flutterPhase = 0.0f;
    initialized = false;
}

float AnalogTapeModule::readWowFlutter(int channel, float delaySamples) const
{
    const auto& history =
        wowFlutterBuffer[static_cast<size_t>(juce::jlimit(0, 1, channel))];
    const auto size = static_cast<int>(history.size());
    auto position = static_cast<float>(wowFlutterWritePosition)
                    - juce::jlimit(2.0f, static_cast<float>(size - 3), delaySamples);
    while (position < 0.0f)
        position += static_cast<float>(size);
    const auto index = static_cast<int>(position);
    const auto fraction = position - static_cast<float>(index);
    const auto at = [&history, size](int i)
    {
        while (i < 0)
            i += size;
        return history[static_cast<size_t>(i % size)];
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

std::uint32_t AnalogTapeModule::nextRandom(int channel)
{
    auto& state = noiseState[static_cast<size_t>(juce::jlimit(0, 1, channel))];
    auto value = state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    state = value != 0 ? value : 0x9e3779b9u;
    return state;
}

float AnalogTapeModule::randomUnit(int channel)
{
    return static_cast<float>(nextRandom(channel) >> 8) * (1.0f / 16777216.0f);
}

void AnalogTapeModule::process(juce::AudioBuffer<float>& buffer,
                               const ControlValues& controls,
                               const ProcessEnvironment&)
{
    if (oversampling == nullptr || buffer.getNumChannels() <= 0
        || buffer.getNumSamples() <= 0)
        return;

    const auto machine = discreteIndex(controls[0], machineCount);
    const auto speed = discreteIndex(controls[4], speedCount);
    const auto& machineProfile =
        machineProfiles[static_cast<size_t>(machine)];
    const auto& speedProfile = speedProfiles[static_cast<size_t>(speed)];

    const auto inputDb = lerp(-18.0f, 18.0f, controls[1]);
    const auto inputTarget = std::abs(inputDb) < 0.0001f
        ? 1.0f : juce::Decibels::decibelsToGain(inputDb);
    const auto driveTarget = juce::jlimit(0.0f, 1.0f, controls[2]);
    const auto biasTarget = lerp(-1.0f, 1.0f, controls[3]);
    const auto hfCutoffTarget = machineProfile.hfCutoffHz * speedProfile.hfMultiplier;
    const auto headBumpFreqTarget =
        machineProfile.headBumpFreqHz * speedProfile.headBumpFreqMultiplier;
    const auto wowFlutterScaleTarget =
        machineProfile.wowFlutterScale * speedProfile.wowFlutterMultiplier;
    const auto noiseFloorTarget = juce::Decibels::decibelsToGain(
        machineProfile.noiseFloorDb) * speedProfile.noiseMultiplier;

    inputSmoothed.setTargetValue(inputTarget);
    driveSmoothed.setTargetValue(driveTarget);
    biasSmoothed.setTargetValue(biasTarget);
    driveHardnessSmoothed.setTargetValue(machineProfile.driveHardness);
    biasSensitivitySmoothed.setTargetValue(machineProfile.biasSensitivity);
    hysteresisSmoothed.setTargetValue(machineProfile.hysteresisAmount);
    compressionSmoothed.setTargetValue(machineProfile.compressionAmount);
    hfCutoffSmoothed.setTargetValue(hfCutoffTarget);
    headBumpFreqSmoothed.setTargetValue(headBumpFreqTarget);
    headBumpQSmoothed.setTargetValue(machineProfile.headBumpQ);
    wowFlutterScaleSmoothed.setTargetValue(wowFlutterScaleTarget);
    noiseFloorSmoothed.setTargetValue(noiseFloorTarget);

    const auto headBumpTarget = juce::jlimit(0.0f, 1.0f, controls[5]);
    const auto wowTarget = juce::jlimit(0.0f, 1.0f, controls[6]);
    const auto flutterTarget = juce::jlimit(0.0f, 1.0f, controls[7]);
    const auto wearTarget = juce::jlimit(0.0f, 1.0f, controls[8]);
    const auto noiseTarget = juce::jlimit(0.0f, 1.0f, controls[9]);
    const auto mixTarget = juce::jlimit(0.0f, 1.0f, controls[10]);
    const auto outputDb = lerp(-18.0f, 12.0f, controls[11]);
    const auto outputTarget = std::abs(outputDb) < 0.0001f
        ? 1.0f : juce::Decibels::decibelsToGain(outputDb);
    headBumpSmoothed.setTargetValue(headBumpTarget);
    wowSmoothed.setTargetValue(wowTarget);
    flutterSmoothed.setTargetValue(flutterTarget);
    wearSmoothed.setTargetValue(wearTarget);
    noiseSmoothed.setTargetValue(noiseTarget);
    mixSmoothed.setTargetValue(mixTarget);
    outputSmoothed.setTargetValue(outputTarget);

    if (!initialized)
    {
        inputSmoothed.setCurrentAndTargetValue(inputTarget);
        driveSmoothed.setCurrentAndTargetValue(driveTarget);
        biasSmoothed.setCurrentAndTargetValue(biasTarget);
        driveHardnessSmoothed.setCurrentAndTargetValue(machineProfile.driveHardness);
        biasSensitivitySmoothed.setCurrentAndTargetValue(machineProfile.biasSensitivity);
        hysteresisSmoothed.setCurrentAndTargetValue(machineProfile.hysteresisAmount);
        compressionSmoothed.setCurrentAndTargetValue(machineProfile.compressionAmount);
        hfCutoffSmoothed.setCurrentAndTargetValue(hfCutoffTarget);
        headBumpFreqSmoothed.setCurrentAndTargetValue(headBumpFreqTarget);
        headBumpQSmoothed.setCurrentAndTargetValue(machineProfile.headBumpQ);
        wowFlutterScaleSmoothed.setCurrentAndTargetValue(wowFlutterScaleTarget);
        noiseFloorSmoothed.setCurrentAndTargetValue(noiseFloorTarget);
        headBumpSmoothed.setCurrentAndTargetValue(headBumpTarget);
        wowSmoothed.setCurrentAndTargetValue(wowTarget);
        flutterSmoothed.setCurrentAndTargetValue(flutterTarget);
        wearSmoothed.setCurrentAndTargetValue(wearTarget);
        noiseSmoothed.setCurrentAndTargetValue(noiseTarget);
        mixSmoothed.setCurrentAndTargetValue(mixTarget);
        outputSmoothed.setCurrentAndTargetValue(outputTarget);
        initialized = true;
    }

    const auto numSamples = buffer.getNumSamples();
    const auto inputGainBlock = inputSmoothed.skip(numSamples);
    const auto driveBlock = driveSmoothed.skip(numSamples);
    const auto biasBlock = biasSmoothed.skip(numSamples);
    const auto driveHardnessBlock = driveHardnessSmoothed.skip(numSamples);
    const auto biasSensitivityBlock = biasSensitivitySmoothed.skip(numSamples);
    const auto hysteresisBlock = hysteresisSmoothed.skip(numSamples);
    const auto compressionBlock = compressionSmoothed.skip(numSamples);
    const auto hfCutoffBlock = hfCutoffSmoothed.skip(numSamples);
    const auto headBumpFreqBlock = headBumpFreqSmoothed.skip(numSamples);
    const auto headBumpQBlock = headBumpQSmoothed.skip(numSamples);
    const auto wowFlutterScaleBlock = wowFlutterScaleSmoothed.skip(numSamples);
    const auto noiseFloorBlock = noiseFloorSmoothed.skip(numSamples);
    const auto headBumpBlock = headBumpSmoothed.skip(numSamples);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        dryBuffer.copyFrom(channel, 0, buffer, channel, 0, numSamples);
    buffer.applyGain(inputGainBlock);

    juce::dsp::AudioBlock<float> inputBlock(buffer);
    auto upsampled = oversampling->processSamplesUp(inputBlock);
    const auto processingRate = sampleRate
        * static_cast<double>(upsampled.getNumSamples())
        / juce::jmax(1, numSamples);
    const auto hfCoefficient = std::exp(
        -juce::MathConstants<float>::twoPi
        * juce::jlimit(1000.0f, static_cast<float>(processingRate * 0.45), hfCutoffBlock)
        / static_cast<float>(processingRate));
    const auto attackCoefficient = coefficient(processingRate, 5.0f);
    const auto releaseCoefficient = coefficient(processingRate, 120.0f);
    const auto memoryCoefficient = 1.0f - std::exp(
        -juce::MathConstants<float>::twoPi * 3000.0f
        / static_cast<float>(processingRate));
    const auto driveGainTotal =
        1.0f + driveBlock * 3.0f * driveHardnessBlock;
    const auto biasOffset = biasBlock * biasSensitivityBlock * 0.6f;
    const auto biasNull = std::tanh(biasOffset);
    const auto hysteresisTotal =
        hysteresisBlock * (1.0f + wearSmoothed.getCurrentValue() * 0.6f);

    for (size_t channel = 0; channel < upsampled.getNumChannels(); ++channel)
    {
        auto* samples = upsampled.getChannelPointer(channel);
        const auto index = juce::jmin(static_cast<int>(channel), 1);
        auto& envelope = compressionEnvelope[static_cast<size_t>(index)];
        auto& memory = tapeMemoryState[static_cast<size_t>(index)];
        auto& hfState = hfLossState[static_cast<size_t>(index)];
        for (size_t sample = 0; sample < upsampled.getNumSamples(); ++sample)
        {
            const auto x = samples[sample];
            const auto absoluteX = std::abs(x);
            const auto envelopeCoefficient =
                absoluteX > envelope ? attackCoefficient : releaseCoefficient;
            envelope = envelopeCoefficient * envelope
                       + (1.0f - envelopeCoefficient) * absoluteX;
            const auto knee = envelope / (envelope + 0.35f);
            const auto compressionGain =
                1.0f - compressionBlock * driveBlock * knee * 0.5f;
            const auto driven = x * compressionGain * driveGainTotal;
            const auto hyst = driven + hysteresisTotal * (driven - memory);
            memory += (driven - memory) * memoryCoefficient;
            const auto nonlinear = std::tanh(hyst + biasOffset) - biasNull;
            hfState = hfCoefficient * hfState + (1.0f - hfCoefficient) * nonlinear;
            samples[sample] = hfState;
        }
    }
    oversampling->processSamplesDown(inputBlock);

    const auto headBumpFreqCurrent = juce::jlimit(
        20.0f, static_cast<float>(sampleRate * 0.45), headBumpFreqBlock);
    const auto headBumpQCurrent = juce::jmax(0.2f, headBumpQBlock);
    const auto wowFlutterScaleCurrent = juce::jmax(0.0f, wowFlutterScaleBlock);
    const auto noiseFloorCurrent = juce::jmax(0.0f, noiseFloorBlock);
    const auto baseDelaySamples =
        static_cast<float>(baseDelayMs * 0.001 * sampleRate);
    const auto wowFlutterCapacity = static_cast<int>(wowFlutterBuffer[0].size());
    const auto noiseEnvelopeCoefficient = coefficient(sampleRate, 60.0f);
    const auto noiseShapeCoefficient = coefficient(sampleRate, 0.04f);
    const auto dropoutCoefficient = coefficient(sampleRate, 350.0f);
    const auto headBumpGainDb = headBumpBlock * headBumpMaximumGainDb;
    for (auto& filter : headBumpFilters)
        filter.setPeak(sampleRate, headBumpFreqCurrent, headBumpQCurrent,
                       headBumpGainDb);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto wow = wowSmoothed.getNextValue();
        const auto flutter = flutterSmoothed.getNextValue();
        const auto wear = wearSmoothed.getNextValue();
        const auto noiseAmount = noiseSmoothed.getNextValue();
        const auto mix = mixSmoothed.getNextValue();
        const auto output = outputSmoothed.getNextValue();

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto& filter = headBumpFilters[static_cast<size_t>(
                juce::jmin(channel, 1))];
            const auto processed = buffer.getSample(channel, sample);
            const auto bumped = filter.process(processed);
            wowFlutterBuffer[static_cast<size_t>(juce::jmin(channel, 1))]
                            [static_cast<size_t>(wowFlutterWritePosition)] = bumped;
        }

        wowPhase += wowRateHz * juce::MathConstants<float>::twoPi
                   / static_cast<float>(sampleRate);
        if (wowPhase > juce::MathConstants<float>::twoPi)
            wowPhase -= juce::MathConstants<float>::twoPi;
        flutterPhase += flutterRateHz * juce::MathConstants<float>::twoPi
                       / static_cast<float>(sampleRate);
        if (flutterPhase > juce::MathConstants<float>::twoPi)
            flutterPhase -= juce::MathConstants<float>::twoPi;
        const auto wowMs = wow * wowFlutterScaleCurrent * maximumWowMs;
        const auto flutterMs = flutter * wowFlutterScaleCurrent * maximumFlutterMs;
        const auto modulationSamples =
            (wowMs * std::sin(wowPhase) + flutterMs * std::sin(flutterPhase))
            * 0.001f * static_cast<float>(sampleRate);
        const auto totalDelay = juce::jlimit(
            2.0f, static_cast<float>(wowFlutterCapacity - 4),
            baseDelaySamples + modulationSamples);

        const auto dryGain = mix <= 0.0f ? 1.0f
            : std::cos(mix * juce::MathConstants<float>::halfPi);
        const auto wetGain = mix <= 0.0f ? 0.0f
            : std::sin(mix * juce::MathConstants<float>::halfPi);

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto index = juce::jmin(channel, 1);
            const auto wowFlutterOut = readWowFlutter(index, totalDelay);

            const auto rawNoise = randomUnit(index) * 2.0f - 1.0f;
            auto& noiseShape = noiseShapeState[static_cast<size_t>(index)];
            noiseShape = noiseShapeCoefficient * noiseShape
                       + (1.0f - noiseShapeCoefficient) * rawNoise;
            auto& gate = noiseEnvelope[static_cast<size_t>(index)];
            gate = noiseEnvelopeCoefficient * gate
                 + (1.0f - noiseEnvelopeCoefficient) * std::abs(wowFlutterOut);
            const auto noiseAmplitude = noiseFloorCurrent * noiseAmount
                * juce::jlimit(0.0f, 1.0f, gate);
            const auto noiseSample = noiseShape * noiseAmplitude;

            const auto dropoutRaw = randomUnit(index) * 2.0f - 1.0f;
            auto& dropoutState = wearDropoutState[static_cast<size_t>(index)];
            dropoutState = dropoutCoefficient * dropoutState
                         + (1.0f - dropoutCoefficient) * dropoutRaw;
            const auto dropoutGain =
                1.0f - wear * 0.12f * std::abs(dropoutState);

            const auto wetSample = (wowFlutterOut + noiseSample) * dropoutGain;

            auto& delayedDry = dryLatencyBuffer[static_cast<size_t>(index)]
                                               [static_cast<size_t>(dryLatencyPosition)];
            const auto dryAligned = delayedDry;
            delayedDry = dryBuffer.getSample(channel, sample);
            const auto dryFinal = dryAligned * inputGainBlock;

            const auto blended = dryGain * dryFinal + wetGain * wetSample;
            const auto result = blended * output;
            buffer.setSample(channel, sample,
                             std::isfinite(result) ? result : 0.0f);
        }

        if (++wowFlutterWritePosition >= wowFlutterCapacity)
            wowFlutterWritePosition = 0;
        if (++dryLatencyPosition
            >= juce::jmax(1, static_cast<int>(dryLatencyBuffer[0].size())))
            dryLatencyPosition = 0;
    }
}
} // namespace megadsp
