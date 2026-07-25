#include "GuitarCabinet.h"
#include "DspHelpers.h"
#include "DspSafety.h"

#include <cmath>

namespace megadsp
{
using detail::exponential;
using detail::lerp;

namespace
{
struct SpeakerProfile
{
    float hardness;
    float asymmetry;
    float powerCompression;
    float excursionCharacter;
    float excursionHz;
    float breakupHz;
    float breakupQ;
    float breakupGain;
    float level;
};

constexpr std::array<SpeakerProfile, GuitarCabinetModule::speakerModelCount>
    speakerProfiles {{
        { 0.72f,  0.10f, 0.42f, 0.38f,  82.0f, 2250.0f, 1.35f, 0.18f, 1.02f },
        { 0.96f,  0.05f, 0.31f, 0.29f,  96.0f, 3150.0f, 1.75f, 0.23f, 0.98f },
        { 0.84f, -0.04f, 0.35f, 0.33f,  72.0f, 3850.0f, 1.25f, 0.16f, 1.00f },
        { 1.18f,  0.01f, 0.20f, 0.19f, 108.0f, 4650.0f, 2.00f, 0.20f, 0.94f }
    }};

struct CabinetProfile
{
    float lowCutHz;
    float highCutHz;
    std::array<float, 2> modeHz;
    std::array<float, 2> modeGain;
    float looseQ;
    float level;
};

constexpr std::array<CabinetProfile, GuitarCabinetModule::cabinetModelCount>
    cabinetProfiles {{
        { 82.0f, 7600.0f, { 112.0f, 345.0f }, { 0.28f,  0.12f }, 2.1f, 1.04f },
        { 70.0f, 6800.0f, {  94.0f, 248.0f }, { 0.32f, -0.10f }, 2.5f, 1.01f },
        { 62.0f, 6100.0f, {  78.0f, 188.0f }, { 0.38f,  0.14f }, 3.0f, 0.96f },
        { 52.0f, 6500.0f, {  66.0f, 152.0f }, { 0.42f, -0.12f }, 3.4f, 0.94f }
    }};

struct MicrophoneProfile
{
    float lowCutHz;
    float highCutHz;
    float presenceHz;
    float presenceGain;
    float proximity;
    float level;
};

constexpr std::array<MicrophoneProfile,
                     GuitarCabinetModule::microphoneModelCount>
    microphoneProfiles {{
        { 72.0f, 11800.0f, 4550.0f,  0.22f, 0.28f, 1.03f },
        { 48.0f, 18000.0f, 6800.0f,  0.16f, 0.20f, 0.98f },
        { 38.0f, 10500.0f, 2900.0f, -0.12f, 0.38f, 1.06f }
    }};

float onePoleCoefficient(double rate, float frequency) noexcept
{
    const auto limitedFrequency = juce::jlimit(
        5.0f, static_cast<float>(rate * 0.45), frequency);
    return 1.0f - std::exp(
        -juce::MathConstants<float>::twoPi * limitedFrequency
        / static_cast<float>(rate));
}

float stateVariableG(double rate, float frequency) noexcept
{
    const auto limitedFrequency = juce::jlimit(
        5.0f, static_cast<float>(rate * 0.42), frequency);
    return std::tan(
        juce::MathConstants<float>::pi * limitedFrequency
        / static_cast<float>(rate));
}

template <size_t Size>
void normalizeWeights(std::array<float, Size>& weights) noexcept
{
    float sum = 0.0f;
    for (const auto weight : weights)
        sum += weight;
    if (sum <= 1.0e-6f)
        return;
    for (auto& weight : weights)
        weight /= sum;
}
} // namespace

GuitarCabinetModule::StateVariableFilter::Output
GuitarCabinetModule::StateVariableFilter::process(
    float input, float g, float inverseQ) noexcept
{
    g = juce::jlimit(0.00001f, 7.0f, g);
    inverseQ = juce::jlimit(0.18f, 2.5f, inverseQ);
    const auto a1 = 1.0f / (1.0f + g * (g + inverseQ));
    const auto a2 = g * a1;
    const auto a3 = g * a2;
    const auto v3 = input - state2;
    const auto band = a1 * state1 + a2 * v3;
    const auto low = state2 + a2 * state1 + a3 * v3;
    state1 = 2.0f * band - state1;
    state2 = 2.0f * low - state2;
    const auto high = input - inverseQ * band - low;
    if (std::isfinite(low) && std::isfinite(band)
        && std::isfinite(high) && std::isfinite(state1)
        && std::isfinite(state2))
        return { low, band, high };
    reset();
    return {};
}

void GuitarCabinetModule::StateVariableFilter::reset() noexcept
{
    state1 = 0.0f;
    state2 = 0.0f;
}

void GuitarCabinetModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    const auto channelCount = juce::jlimit(
        1, 2, static_cast<int>(spec.numChannels));
    oversampling = std::make_unique<juce::dsp::Oversampling<float>>(
        static_cast<size_t>(channelCount), 2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    oversampling->initProcessing(spec.maximumBlockSize);
    processingLatency = juce::jmax(
        1, juce::roundToInt(oversampling->getLatencyInSamples()));
    parameterFrames.resize(
        static_cast<size_t>(juce::jmax(1u, spec.maximumBlockSize)));

    const auto roomCapacity = juce::jmax(
        64, juce::roundToInt(sampleRate * 0.055));
    roomHistory.assign(static_cast<size_t>(roomCapacity), 0.0f);

    const auto oversampledRate =
        sampleRate * static_cast<double>(oversamplingFactor);
    oversampledAttackCoefficient = detail::coefficient(oversampledRate, 65.0f);
    oversampledReleaseCoefficient = detail::coefficient(oversampledRate, 700.0f);
    dcRetention = std::exp(
        -juce::MathConstants<float>::twoPi * 12.0f
        / static_cast<float>(oversampledRate));

    for (int index = 0; index < speakerModelCount; ++index)
    {
        const auto& profile = speakerProfiles[static_cast<size_t>(index)];
        excursionCoefficients[static_cast<size_t>(index)] =
            onePoleCoefficient(oversampledRate, profile.excursionHz);
        breakupFilterG[static_cast<size_t>(index)] =
            stateVariableG(oversampledRate, profile.breakupHz);
        speakerMix[static_cast<size_t>(index)].reset(sampleRate, 0.045);
    }
    for (int index = 0; index < cabinetModelCount; ++index)
    {
        const auto& profile = cabinetProfiles[static_cast<size_t>(index)];
        cabinetLowCutCoefficients[static_cast<size_t>(index)] =
            onePoleCoefficient(sampleRate, profile.lowCutHz);
        cabinetHighCutCoefficients[static_cast<size_t>(index)] =
            onePoleCoefficient(sampleRate, profile.highCutHz);
        for (int mode = 0; mode < 2; ++mode)
            cabinetModeG[static_cast<size_t>(index)][static_cast<size_t>(mode)] =
                stateVariableG(
                    sampleRate, profile.modeHz[static_cast<size_t>(mode)]);
        cabinetMix[static_cast<size_t>(index)].reset(sampleRate, 0.045);
    }
    for (int index = 0; index < microphoneModelCount; ++index)
    {
        microphonePresenceG[static_cast<size_t>(index)] =
            stateVariableG(
                sampleRate,
                microphoneProfiles[static_cast<size_t>(index)].presenceHz);
        microphoneMix[static_cast<size_t>(index)].reset(sampleRate, 0.045);
    }

    driveSmoothed.reset(sampleRate, 0.035);
    breakupSmoothed.reset(sampleRate, 0.035);
    resonanceSmoothed.reset(sampleRate, 0.035);
    dampingSmoothed.reset(sampleRate, 0.035);
    positionSmoothed.reset(sampleRate, 0.035);
    distanceSmoothed.reset(sampleRate, 0.05);
    roomSmoothed.reset(sampleRate, 0.05);
    widthSmoothed.reset(sampleRate, 0.05);
    outputSmoothed.reset(sampleRate, 0.03);
    reset();
}

void GuitarCabinetModule::reset()
{
    if (oversampling != nullptr)
        oversampling->reset();
    for (auto& model : speakerStates)
    {
        model.powerEnvelope = 0.0f;
        for (auto& channel : model.channels)
        {
            channel.breakupFilter.reset();
            channel.excursion = 0.0f;
            channel.dcInput = 0.0f;
            channel.dcOutput = 0.0f;
        }
    }
    for (auto& model : cabinetStates)
        for (auto& channel : model.channels)
        {
            for (auto& mode : channel.modes)
                mode.reset();
            channel.lowCutState = 0.0f;
            channel.highCutState = 0.0f;
        }
    for (auto& model : microphoneStates)
        for (auto& channel : model.channels)
        {
            channel.presenceFilter.reset();
            channel.lowCutState = 0.0f;
            channel.proximityState = 0.0f;
            channel.highCutState = 0.0f;
        }
    std::fill(roomHistory.begin(), roomHistory.end(), 0.0f);
    roomWritePosition = 0;
    roomDampingState = 0.0f;
    telemetryState = {};
    telemetry.clear();
    initialized = false;
}

float GuitarCabinetModule::renderSpeaker(
    int model, int channel, float input, const ParameterFrame& frame,
    float compressionGain, float& excursionActivity,
    float& breakupActivity) noexcept
{
    const auto& profile = speakerProfiles[static_cast<size_t>(model)];
    auto& state = speakerStates[static_cast<size_t>(model)]
                              .channels[static_cast<size_t>(channel)];
    const auto driven = input * frame.driveGain * compressionGain;
    state.excursion += excursionCoefficients[static_cast<size_t>(model)]
        * (driven - state.excursion);
    const auto excursionScale = 0.2f + 0.8f * frame.driveAmount;
    const auto excursion = state.excursion * profile.excursionCharacter
        * excursionScale;
    const auto shaped = driven
        + excursion * (0.45f + 0.16f * std::abs(driven))
        + profile.asymmetry * driven * driven
            / (1.0f + std::abs(driven));
    const auto nonlinear = std::tanh(profile.hardness * shaped)
        / profile.hardness;
    auto speaker = nonlinear * profile.level;

    const auto dcBlocked =
        speaker - state.dcInput + dcRetention * state.dcOutput;
    state.dcInput = speaker;
    state.dcOutput = std::isfinite(dcBlocked) ? dcBlocked : 0.0f;
    speaker = state.dcOutput;

    const auto breakup = state.breakupFilter.process(
        speaker, breakupFilterG[static_cast<size_t>(model)],
        1.0f / profile.breakupQ).band;
    const auto breakupContribution = frame.breakup * profile.breakupGain
        * std::tanh(1.7f * breakup);
    excursionActivity = juce::jmax(
        excursionActivity,
        juce::jlimit(0.0f, 1.0f, std::abs(excursion) * 2.5f));
    breakupActivity = juce::jmax(
        breakupActivity,
        juce::jlimit(0.0f, 1.0f, std::abs(breakupContribution) * 4.0f));
    const auto result = speaker + breakupContribution;
    return std::isfinite(result) ? result : 0.0f;
}

float GuitarCabinetModule::renderCabinet(
    int model, int channel, float input, const ParameterFrame& frame,
    float& cabinetActivity) noexcept
{
    const auto& profile = cabinetProfiles[static_cast<size_t>(model)];
    auto& state = cabinetStates[static_cast<size_t>(model)]
                              .channels[static_cast<size_t>(channel)];
    state.lowCutState += cabinetLowCutCoefficients[static_cast<size_t>(model)]
        * (input - state.lowCutState);
    const auto highPassed = input - state.lowCutState;
    const auto dampingHighCut = lerp(
        1.0f, 0.68f, frame.damping);
    const auto highCutCoefficient = juce::jlimit(
        0.0001f, 1.0f,
        cabinetHighCutCoefficients[static_cast<size_t>(model)]
            * dampingHighCut);
    state.highCutState += highCutCoefficient
        * (highPassed - state.highCutState);
    const auto body = state.highCutState;

    const auto modeQ = lerp(profile.looseQ, 0.72f, frame.damping);
    const auto modeAmount = 0.035f + 0.48f * frame.cabinetResonance;
    float modes = 0.0f;
    for (int mode = 0; mode < 2; ++mode)
    {
        const auto band = state.modes[static_cast<size_t>(mode)].process(
            body,
            cabinetModeG[static_cast<size_t>(model)]
                         [static_cast<size_t>(mode)],
            1.0f / modeQ).band;
        modes += band * profile.modeGain[static_cast<size_t>(mode)];
    }
    const auto resonant = modes * modeAmount;
    cabinetActivity = juce::jmax(
        cabinetActivity,
        juce::jlimit(0.0f, 1.0f, std::abs(resonant) * 3.0f));
    const auto result = (body + resonant) * profile.level;
    if (std::isfinite(result))
        return result;
    state.lowCutState = 0.0f;
    state.highCutState = 0.0f;
    for (auto& mode : state.modes)
        mode.reset();
    return 0.0f;
}

float GuitarCabinetModule::renderMicrophone(
    int model, int channel, float input,
    const ParameterFrame& frame) noexcept
{
    const auto& profile = microphoneProfiles[static_cast<size_t>(model)];
    auto& state = microphoneStates[static_cast<size_t>(model)]
                                 .channels[static_cast<size_t>(channel)];
    const auto distance = juce::jlimit(1.0f, 100.0f, frame.micDistanceCm);
    const auto lowCut = profile.lowCutHz
        * lerp(0.78f, 1.24f, frame.micPosition);
    state.lowCutState += onePoleCoefficient(sampleRate, lowCut)
        * (input - state.lowCutState);
    const auto highPassed = input - state.lowCutState;

    state.proximityState += onePoleCoefficient(sampleRate, 185.0f)
        * (highPassed - state.proximityState);
    const auto proximity = profile.proximity
        * std::exp(-distance / 17.0f)
        * lerp(1.0f, 0.78f, frame.micPosition);
    auto microphone = highPassed + state.proximityState * proximity;

    const auto presence = state.presenceFilter.process(
        microphone, microphonePresenceG[static_cast<size_t>(model)],
        1.1f).band;
    microphone += presence * profile.presenceGain
        * lerp(1.0f, 0.22f, frame.micPosition);

    const auto edgeDarkening = lerp(1.0f, 0.48f, frame.micPosition);
    const auto airAbsorption = std::exp(-0.0038f * distance);
    const auto highCut = profile.highCutHz
        * edgeDarkening * airAbsorption;
    state.highCutState += onePoleCoefficient(sampleRate, highCut)
        * (microphone - state.highCutState);
    const auto distanceGain = 1.0f
        / std::sqrt(1.0f + distance * 0.018f);
    const auto result = state.highCutState * distanceGain * profile.level;
    if (std::isfinite(result))
        return result;
    state = {};
    return 0.0f;
}

void GuitarCabinetModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    juce::ScopedNoDenormals noDenormals;
    if (oversampling == nullptr || buffer.getNumChannels() <= 0
        || buffer.getNumSamples() <= 0)
        return;

    const auto sampleCount = buffer.getNumSamples();
    const auto channelCount = juce::jmin(2, buffer.getNumChannels());
    if (sampleCount > static_cast<int>(parameterFrames.size()))
    {
        jassertfalse;
        return;
    }

    const auto normalized = [&controls](int index, float fallback)
    {
        return detail::normalizedControl(
            controls[static_cast<size_t>(index)], fallback);
    };
    const auto speaker = discreteIndex(
        normalized(0, 0.125f), speakerModelCount);
    const auto cabinet = discreteIndex(
        normalized(1, 0.375f), cabinetModelCount);
    const auto microphone = discreteIndex(
        normalized(2, 0.125f), microphoneModelCount);
    const auto positionTarget = normalized(3, 0.35f);
    const auto distanceTarget = exponential(
        1.0f, 100.0f, normalized(4, 0.35f));
    const auto driveDb = lerp(0.0f, 24.0f, normalized(5, 0.25f));
    const auto driveTarget = juce::Decibels::decibelsToGain(driveDb);
    const auto breakupTarget = normalized(6, 0.30f);
    const auto resonanceTarget = normalized(7, 0.45f);
    const auto dampingTarget = normalized(8, 0.55f);
    const auto roomTarget = normalized(9, 0.10f);
    const auto widthTarget = normalized(10, 0.55f);
    const auto outputTarget = juce::Decibels::decibelsToGain(
        lerp(-18.0f, 12.0f, normalized(11, 0.60f)));

    for (int index = 0; index < speakerModelCount; ++index)
        speakerMix[static_cast<size_t>(index)].setTargetValue(
            index == speaker ? 1.0f : 0.0f);
    for (int index = 0; index < cabinetModelCount; ++index)
        cabinetMix[static_cast<size_t>(index)].setTargetValue(
            index == cabinet ? 1.0f : 0.0f);
    for (int index = 0; index < microphoneModelCount; ++index)
        microphoneMix[static_cast<size_t>(index)].setTargetValue(
            index == microphone ? 1.0f : 0.0f);
    driveSmoothed.setTargetValue(driveTarget);
    breakupSmoothed.setTargetValue(breakupTarget);
    resonanceSmoothed.setTargetValue(resonanceTarget);
    dampingSmoothed.setTargetValue(dampingTarget);
    positionSmoothed.setTargetValue(positionTarget);
    distanceSmoothed.setTargetValue(distanceTarget);
    roomSmoothed.setTargetValue(roomTarget);
    widthSmoothed.setTargetValue(widthTarget);
    outputSmoothed.setTargetValue(outputTarget);

    if (!initialized)
    {
        for (int index = 0; index < speakerModelCount; ++index)
            speakerMix[static_cast<size_t>(index)].setCurrentAndTargetValue(
                index == speaker ? 1.0f : 0.0f);
        for (int index = 0; index < cabinetModelCount; ++index)
            cabinetMix[static_cast<size_t>(index)].setCurrentAndTargetValue(
                index == cabinet ? 1.0f : 0.0f);
        for (int index = 0; index < microphoneModelCount; ++index)
            microphoneMix[static_cast<size_t>(index)].setCurrentAndTargetValue(
                index == microphone ? 1.0f : 0.0f);
        driveSmoothed.setCurrentAndTargetValue(driveTarget);
        breakupSmoothed.setCurrentAndTargetValue(breakupTarget);
        resonanceSmoothed.setCurrentAndTargetValue(resonanceTarget);
        dampingSmoothed.setCurrentAndTargetValue(dampingTarget);
        positionSmoothed.setCurrentAndTargetValue(positionTarget);
        distanceSmoothed.setCurrentAndTargetValue(distanceTarget);
        roomSmoothed.setCurrentAndTargetValue(roomTarget);
        widthSmoothed.setCurrentAndTargetValue(widthTarget);
        outputSmoothed.setCurrentAndTargetValue(outputTarget);
        initialized = true;
    }

    for (int sample = 0; sample < sampleCount; ++sample)
    {
        auto& frame = parameterFrames[static_cast<size_t>(sample)];
        for (int index = 0; index < speakerModelCount; ++index)
            frame.speakerWeights[static_cast<size_t>(index)] =
                speakerMix[static_cast<size_t>(index)].getNextValue();
        for (int index = 0; index < cabinetModelCount; ++index)
            frame.cabinetWeights[static_cast<size_t>(index)] =
                cabinetMix[static_cast<size_t>(index)].getNextValue();
        for (int index = 0; index < microphoneModelCount; ++index)
            frame.microphoneWeights[static_cast<size_t>(index)] =
                microphoneMix[static_cast<size_t>(index)].getNextValue();
        normalizeWeights(frame.speakerWeights);
        normalizeWeights(frame.cabinetWeights);
        normalizeWeights(frame.microphoneWeights);
        frame.driveGain = driveSmoothed.getNextValue();
        frame.driveAmount = juce::jlimit(
            0.0f, 1.0f,
            juce::Decibels::gainToDecibels(frame.driveGain) / 24.0f);
        frame.breakup = breakupSmoothed.getNextValue();
        frame.cabinetResonance = resonanceSmoothed.getNextValue();
        frame.damping = dampingSmoothed.getNextValue();
        frame.micPosition = positionSmoothed.getNextValue();
        frame.micDistanceCm = distanceSmoothed.getNextValue();
        frame.room = roomSmoothed.getNextValue();
        frame.roomWidth = widthSmoothed.getNextValue();
        frame.output = outputSmoothed.getNextValue();
    }

    for (int channel = 0; channel < channelCount; ++channel)
        for (int sample = 0; sample < sampleCount; ++sample)
        {
            const auto finite =
                detail::finiteSample(buffer.getSample(channel, sample));
            buffer.setSample(
                channel, sample, juce::jlimit(-32.0f, 32.0f, finite));
        }

    float blockExcursion = 0.0f;
    float blockCompression = 0.0f;
    float blockBreakup = 0.0f;
    float blockCabinet = 0.0f;
    float blockRoom = 0.0f;

    juce::dsp::AudioBlock<float> inputBlock(buffer);
    auto upsampled = oversampling->processSamplesUp(inputBlock);
    const auto upsampledSamples = static_cast<int>(upsampled.getNumSamples());
    for (int osSample = 0; osSample < upsampledSamples; ++osSample)
    {
        const auto frameIndex = juce::jmin(
            sampleCount - 1, osSample / oversamplingFactor);
        const auto& frame = parameterFrames[static_cast<size_t>(frameIndex)];
        float detector = 0.0f;
        for (int channel = 0; channel < channelCount; ++channel)
            detector = juce::jmax(
                detector,
                std::abs(upsampled.getSample(
                    channel, osSample))
                    * frame.driveGain);

        std::array<float, speakerModelCount> compressionGains {};
        for (int model = 0; model < speakerModelCount; ++model)
        {
            const auto& profile = speakerProfiles[static_cast<size_t>(model)];
            auto& envelope =
                speakerStates[static_cast<size_t>(model)].powerEnvelope;
            const auto detectorPower = detector * detector;
            const auto coefficient = detectorPower > envelope
                ? oversampledAttackCoefficient
                : oversampledReleaseCoefficient;
            envelope = coefficient * envelope
                + (1.0f - coefficient) * detectorPower;
            const auto thermalLevel = std::sqrt(juce::jmax(0.0f, envelope));
            const auto power = thermalLevel / (0.32f + thermalLevel);
            const auto gain = 1.0f
                / (1.0f + profile.powerCompression
                    * (0.35f + 0.65f * frame.driveAmount) * power);
            compressionGains[static_cast<size_t>(model)] = gain;
            blockCompression = juce::jmax(
                blockCompression,
                frame.speakerWeights[static_cast<size_t>(model)]
                    * (1.0f - gain));
        }

        for (int channel = 0; channel < channelCount; ++channel)
        {
            const auto input = upsampled.getSample(
                channel, osSample);
            float output = 0.0f;
            for (int model = 0; model < speakerModelCount; ++model)
            {
                const auto weight =
                    frame.speakerWeights[static_cast<size_t>(model)];
                float modelExcursion = 0.0f;
                float modelBreakup = 0.0f;
                output += weight * renderSpeaker(
                    model, channel, input, frame,
                    compressionGains[static_cast<size_t>(model)],
                    modelExcursion, modelBreakup);
                blockExcursion = juce::jmax(
                    blockExcursion, weight * modelExcursion);
                blockBreakup = juce::jmax(
                    blockBreakup, weight * modelBreakup);
            }
            upsampled.setSample(
                channel, osSample,
                std::isfinite(output) ? output : 0.0f);
        }
    }
    oversampling->processSamplesDown(inputBlock);

    const auto roomSize = static_cast<int>(roomHistory.size());
    const auto tap = [this, roomSize](float milliseconds)
    {
        const auto delay = juce::jlimit(
            1, roomSize - 1,
            juce::roundToInt(
                milliseconds * 0.001 * sampleRate));
        auto index = roomWritePosition - delay;
        if (index < 0)
            index += roomSize;
        return roomHistory[static_cast<size_t>(index)];
    };

    for (int sample = 0; sample < sampleCount; ++sample)
    {
        const auto& frame = parameterFrames[static_cast<size_t>(sample)];
        std::array<float, 2> close {};
        for (int channel = 0; channel < channelCount; ++channel)
        {
            const auto speakerOutput = buffer.getSample(channel, sample);
            float cabinetOutput = 0.0f;
            for (int model = 0; model < cabinetModelCount; ++model)
            {
                const auto weight =
                    frame.cabinetWeights[static_cast<size_t>(model)];
                float modelActivity = 0.0f;
                cabinetOutput += weight * renderCabinet(
                    model, channel, speakerOutput, frame, modelActivity);
                blockCabinet = juce::jmax(
                    blockCabinet, weight * modelActivity);
            }

            float microphoneOutput = 0.0f;
            for (int model = 0; model < microphoneModelCount; ++model)
                microphoneOutput +=
                    frame.microphoneWeights[static_cast<size_t>(model)]
                    * renderMicrophone(
                        model, channel, cabinetOutput, frame);
            close[static_cast<size_t>(channel)] = microphoneOutput;
        }

        const auto roomInput = channelCount == 1
            ? close[0] : 0.5f * (close[0] + close[1]);
        const auto reflectionA = tap(6.7f);
        const auto reflectionB = tap(11.3f);
        const auto reflectionC = tap(18.1f);
        const auto reflectionD = tap(29.7f);
        const auto roomMid = 0.25f * reflectionA
            + 0.19f * reflectionB + 0.14f * reflectionC
            + 0.09f * reflectionD;
        const auto roomSide = 0.22f * (reflectionA - reflectionB)
            + 0.13f * (reflectionC - reflectionD);
        roomDampingState += 0.16f * (roomMid - roomDampingState);
        roomHistory[static_cast<size_t>(roomWritePosition)] =
            roomInput + 0.24f * roomDampingState;
        if (++roomWritePosition >= roomSize)
            roomWritePosition = 0;

        const auto roomMidContribution = frame.room * roomMid;
        const auto sideContribution = channelCount > 1
            ? frame.room * frame.roomWidth * 1.35f * roomSide
            : 0.0f;
        blockRoom = juce::jmax(
            blockRoom,
            juce::jlimit(
                0.0f, 1.0f,
                (std::abs(roomMidContribution)
                 + std::abs(sideContribution)) * 3.0f));
        const auto left =
            (close[0] + roomMidContribution + sideContribution)
            * frame.output;
        buffer.setSample(
            0, sample, std::isfinite(left) ? left : 0.0f);
        if (channelCount > 1)
        {
            const auto right =
                (close[1] + roomMidContribution - sideContribution)
                * frame.output;
            buffer.setSample(
                1, sample, std::isfinite(right) ? right : 0.0f);
        }
    }

    if (environment.captureTelemetry)
    {
        telemetryState.sequence += 1;
        telemetryState.valueCount = telemetryValueCount;
        const std::array<float, telemetryValueCount> blockValues {
            blockExcursion, blockCompression, blockBreakup,
            blockCabinet, blockRoom
        };
        for (size_t index = 0; index < blockValues.size(); ++index)
            telemetryState.values[index] =
                0.72f * telemetryState.values[index]
                + 0.28f * blockValues[index];
        telemetry.publish(telemetryState);
    }
}

bool GuitarCabinetModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}

double GuitarCabinetModule::tailSeconds(
    const ControlValues& controls) const
{
    const auto room = detail::normalizedControl(controls[9], 0.10f);
    return room <= 1.0e-5f
        ? 0.12
        : 0.12 + 0.28 * std::sqrt(static_cast<double>(room));
}
} // namespace megadsp
