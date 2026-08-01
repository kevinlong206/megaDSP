#include "VelvetDrive.h"

#include "DspHelpers.h"
#include "DspSafety.h"

#include <algorithm>
#include <cmath>

namespace megadsp
{
using detail::exponential;
using detail::lerp;

namespace
{
// Per-voice character constants blended by the smoothed voice weights. Classic
// is a smooth, thick, clear overdrive; Modern is a mid-forward, tighter voice
// that leans on post-clip shaping; High Headroom stays cleaner and stiffer
// with a later knee and less compression.
struct VoiceProfile
{
    float stageGainScale; // overall preamp gain trim
    float knee;           // soft-clip knee width (larger = later break-up)
    float asymmetry;      // even-harmonic bias asymmetry
    float compression;    // supply/power compression amount
    float preToneWeight;  // pre-clip tone stack contribution
    float postToneWeight; // post-clip tone stack contribution
};

constexpr std::array<VoiceProfile, VelvetDriveModule::voiceCount> voiceProfiles
{{
    // Classic
    { 1.05f, 1.00f, 0.26f, 0.55f, 1.00f, 0.00f },
    // Modern
    { 1.12f, 0.86f, 0.18f, 0.62f, 0.35f, 0.75f },
    // High Headroom
    { 0.75f, 1.65f, 0.10f, 0.25f, 1.00f, 0.00f }
}};

// Fixed preamp stage voicing. Coupling frequencies set the interstage RC
// blocking corner; miller frequencies set the treble roll-off; base gains set
// the raw stage gain before drive, voice and ratio scaling; grid thresholds
// set where soft grid conduction and bias shift begin.
constexpr std::array<float, VelvetDriveModule::preampStageCount>
    stageCouplingHz { 6.0f, 9.0f, 16.0f, 26.0f };
constexpr std::array<float, VelvetDriveModule::preampStageCount>
    stageMillerHz { 12500.0f, 9500.0f, 7200.0f, 5600.0f };
constexpr std::array<float, VelvetDriveModule::preampStageCount>
    stageBaseGain { 1.35f, 1.20f, 1.30f, 1.50f };
constexpr std::array<float, VelvetDriveModule::preampStageCount>
    stageBiasOffset { 0.05f, 0.07f, 0.09f, 0.11f };
constexpr std::array<float, VelvetDriveModule::preampStageCount>
    stageGridThreshold { 0.95f, 0.85f, 0.75f, 0.65f };

// Analytic speaker voicings. All shaping is filter based, with no cabinet
// room or reverb: users can follow Velvet Drive with the Guitar Cabinet.
struct SpeakerProfile
{
    float highPassHz;
    float lowPassHz;
    float resonanceAHz;
    float resonanceAGain;
    float resonanceAQ;
    float resonanceBHz;
    float resonanceBGain;
    float resonanceBQ;
    float sparkleHz;
    float sparkleGain;
    float compression;
    float level;
};

constexpr std::array<SpeakerProfile, VelvetDriveModule::speakerModelCount>
    speakerProfiles
{{
    // Direct: minimal transformer/load shaping, a gentle low-mid support.
    { 55.0f, 9000.0f, 180.0f, 0.14f, 0.9f, 900.0f, 0.05f, 0.8f,
      6500.0f, 0.06f, 0.02f, 1.00f },
    // Flat 12: high-headroom band-limited voicing.
    { 85.0f, 6300.0f, 2400.0f, 0.10f, 0.8f, 400.0f, -0.05f, 0.9f,
      4800.0f, 0.05f, 0.05f, 1.00f },
    // Scooped 12: low resonance, mid scoop, upper sparkle.
    { 92.0f, 5200.0f, 105.0f, 0.30f, 1.2f, 520.0f, -0.30f, 1.1f,
      4200.0f, 0.16f, 0.05f, 0.98f },
    // Chime 12: earlier compression and an upper-mid chime.
    { 96.0f, 5600.0f, 3050.0f, 0.24f, 1.3f, 720.0f, -0.10f, 1.0f,
      5200.0f, 0.10f, 0.16f, 0.98f }
}};

float onePoleCoefficient(double rate, float frequency) noexcept
{
    const auto limited = juce::jlimit(
        3.0f, static_cast<float>(rate * 0.45), frequency);
    return 1.0f - std::exp(
        -juce::MathConstants<float>::twoPi * limited
        / static_cast<float>(rate));
}

float stateVariableG(double rate, float frequency) noexcept
{
    const auto limited = juce::jlimit(
        5.0f, static_cast<float>(rate * 0.42), frequency);
    return std::tan(
        juce::MathConstants<float>::pi * limited
        / static_cast<float>(rate));
}

// Asymmetric soft transfer shared by the triode and power stages. The upper
// (grid conduction) side clamps sooner than the lower (cutoff) side, which
// creates the even/odd harmonic mix of a real tube stage. The knee widens the
// linear region so higher-headroom voices break up later.
float asymmetricShape(float x, float knee, float asymmetry) noexcept
{
    const auto safeKnee = juce::jmax(0.35f, knee);
    const auto upper = safeKnee;
    const auto lower = safeKnee * (1.0f + juce::jlimit(0.0f, 1.0f, asymmetry));
    const auto shaped = x >= 0.0f
        ? upper * std::tanh(x / upper)
        : lower * std::tanh(x / lower);
    return std::isfinite(shaped) ? shaped : 0.0f;
}
} // namespace

float VelvetDriveModule::StateVariableFilter::processBand(
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
    if (std::isfinite(band) && std::isfinite(state1)
        && std::isfinite(state2))
        return band;
    reset();
    return 0.0f;
}


void VelvetDriveModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    const auto channelCount = juce::jlimit(
        1, 2, static_cast<int>(spec.numChannels));
    oversampling = std::make_unique<juce::dsp::Oversampling<float>>(
        static_cast<size_t>(channelCount), 2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true, true);
    oversampling->initProcessing(spec.maximumBlockSize);
    processingLatency = juce::jmax(
        1, juce::roundToInt(oversampling->getLatencyInSamples()));
    parameterFrames.resize(
        static_cast<size_t>(juce::jmax(1u, spec.maximumBlockSize)));

    oversampledRate = sampleRate * static_cast<double>(oversamplingFactor);

    for (int stage = 0; stage < preampStageCount; ++stage)
    {
        stageCouplingCoefficients[static_cast<size_t>(stage)] =
            onePoleCoefficient(
                oversampledRate, stageCouplingHz[static_cast<size_t>(stage)]);
        stageMillerCoefficients[static_cast<size_t>(stage)] =
            onePoleCoefficient(
                oversampledRate, stageMillerHz[static_cast<size_t>(stage)]);
    }

    biasAttackCoefficient = detail::coefficient(oversampledRate, 2.5f);
    biasReleaseCoefficient = detail::coefficient(oversampledRate, 65.0f);
    toneLowCoefficient = onePoleCoefficient(oversampledRate, 220.0f);
    toneHighCoefficient = onePoleCoefficient(oversampledRate, 1800.0f);
    postToneLowCoefficient = onePoleCoefficient(oversampledRate, 260.0f);
    postToneHighCoefficient = onePoleCoefficient(oversampledRate, 1500.0f);
    presenceFeedbackCoefficient = onePoleCoefficient(oversampledRate, 1200.0f);
    inputBrightCoefficient = onePoleCoefficient(oversampledRate, 720.0f);
    sagAttackCoefficient = detail::coefficient(oversampledRate, 9.0f);
    oversampledDcCoefficient = onePoleCoefficient(oversampledRate, 12.0f);
    baseDcCoefficient = onePoleCoefficient(sampleRate, 14.0f);

    for (int model = 0; model < speakerModelCount; ++model)
    {
        const auto& profile = speakerProfiles[static_cast<size_t>(model)];
        speakerHighPassCoefficients[static_cast<size_t>(model)] =
            onePoleCoefficient(sampleRate, profile.highPassHz);
        speakerLowPassCoefficients[static_cast<size_t>(model)] =
            onePoleCoefficient(sampleRate, profile.lowPassHz);
        speakerSparkleCoefficients[static_cast<size_t>(model)] =
            onePoleCoefficient(sampleRate, profile.sparkleHz);
        speakerResonanceAG[static_cast<size_t>(model)] =
            stateVariableG(sampleRate, profile.resonanceAHz);
        speakerResonanceBG[static_cast<size_t>(model)] =
            stateVariableG(sampleRate, profile.resonanceBHz);
        speakerMix[static_cast<size_t>(model)].reset(sampleRate, 0.05);
    }

    inputFetMix.reset(sampleRate, 0.05);
    for (auto& mix : voiceMix)
        mix.reset(sampleRate, 0.05);
    driveSmoothed.reset(sampleRate, 0.035);
    ratioSmoothed.reset(sampleRate, 0.035);
    levelSmoothed.reset(sampleRate, 0.03);
    bassSmoothed.reset(sampleRate, 0.04);
    midSmoothed.reset(sampleRate, 0.04);
    trebleSmoothed.reset(sampleRate, 0.04);
    presenceSmoothed.reset(sampleRate, 0.04);
    feelSmoothed.reset(sampleRate, 0.05);
    masterSmoothed.reset(sampleRate, 0.03);

    reset();
}

void VelvetDriveModule::reset()
{
    if (oversampling != nullptr)
        oversampling->reset();
    for (auto& channel : channels)
    {
        channel.input.blocking.reset();
        channel.input.brightState.reset();
        for (auto& stage : channel.stages)
        {
            stage.coupling.reset();
            stage.miller.reset();
            stage.biasState = 0.0f;
        }
        channel.preTone.low.reset();
        channel.preTone.high.reset();
        channel.postTone.low.reset();
        channel.postTone.high.reset();
        channel.power.feedbackLow.reset();
        channel.power.feedbackShaped = 0.0f;
        channel.power.dcInput = 0.0f;
        channel.power.dcOutput = 0.0f;
        for (auto& speaker : channel.speakers)
        {
            speaker.highPass.reset();
            speaker.resonanceA.reset();
            speaker.resonanceB.reset();
            speaker.sparkle.reset();
            speaker.lowPass.reset();
            speaker.dcInput = 0.0f;
            speaker.dcOutput = 0.0f;
        }
        channel.outputDcLow.reset();
    }
    supplyEnvelope = 0.0f;
    telemetryState = {};
    telemetry.clear();
    initialized = false;
}

float VelvetDriveModule::toneStack(
    ToneState& state, float input, const ParameterFrame& frame) noexcept
{
    // A passive interactive Bass/Mid/Treble approximation: the signal is split
    // into low, mid and high bands whose recombination weights interact
    // through a shared passive-network normalisation, so raising one control
    // audibly loads the others and a low mid setting scoops the midrange.
    const auto low = state.low.processLowpass(input, toneLowCoefficient);
    const auto high = input - state.high.processLowpass(
        input, toneHighCoefficient);
    const auto mid = input - low - high;

    const auto bassWeight = 0.25f + 1.30f * frame.bass;
    const auto midWeight = 0.10f + 1.10f * frame.mid;
    const auto trebleWeight = 0.25f + 1.30f * frame.treble;
    const auto shaped = low * bassWeight + mid * midWeight
        + high * trebleWeight;
    const auto loading = 0.55f
        + 0.45f * (bassWeight + midWeight + trebleWeight) / 3.0f;
    const auto result = shaped / juce::jmax(0.35f, loading);
    return std::isfinite(result) ? result : 0.0f;
}

float VelvetDriveModule::triodeStage(
    int stage, ChannelState& channel, float input,
    const ParameterFrame& frame, float gain) noexcept
{
    auto& state = channel.stages[static_cast<size_t>(stage)];
    // Interstage RC coupling: block DC and the slow bias drift of the prior
    // stage before applying gain.
    const auto blocked = input - state.coupling.processLowpass(
        input, stageCouplingCoefficients[static_cast<size_t>(stage)]);
    const auto biasOffset = stageBiasOffset[static_cast<size_t>(stage)];
    const auto driven = blocked * gain + biasOffset - state.biasState;

    const auto shaped = asymmetricShape(driven, frame.knee, frame.asymmetry);

    // Soft grid conduction charges the coupling cap and shifts the bias for a
    // slower, bounded compression/bloom without ever running away.
    const auto gridThreshold = stageGridThreshold[static_cast<size_t>(stage)];
    const auto grid = juce::jmax(0.0f, driven - gridThreshold);
    const auto biasTarget = juce::jlimit(
        0.0f, 1.2f, grid * (0.35f + 0.65f * frame.driveAmount));
    const auto biasCoefficient = biasTarget > state.biasState
        ? biasAttackCoefficient : biasReleaseCoefficient;
    state.biasState = biasCoefficient * state.biasState
        + (1.0f - biasCoefficient) * biasTarget;
    if (!std::isfinite(state.biasState))
        state.biasState = 0.0f;

    // Miller capacitance treble roll-off.
    const auto filtered = state.miller.processLowpass(
        shaped, stageMillerCoefficients[static_cast<size_t>(stage)]);
    return std::isfinite(filtered) ? filtered : 0.0f;
}

float VelvetDriveModule::runPreamp(
    ChannelState& channel, float input, const ParameterFrame& frame,
    float& activity) noexcept
{
    // Pre-clip tone shaping (Classic / High Headroom) crossfaded with the raw
    // input; Modern leans on post-clip shaping instead.
    const auto preToned = toneStack(channel.preTone, input, frame);
    auto node = lerp(input, preToned, frame.preToneWeight);

    // Ratio scales the later stages to intensify the cascade and blends the
    // clean and overdriven taps of the cascade.
    const auto lateScale = 0.55f + 0.95f * frame.ratio;
    float cleanTap = node;
    float value = node;
    for (int stage = 0; stage < preampStageCount; ++stage)
    {
        const auto stageScale = stage >= 2 ? lateScale : 1.0f;
        const auto gain = stageBaseGain[static_cast<size_t>(stage)]
            * frame.stageGainScale * frame.driveScale * stageScale;
        value = triodeStage(stage, channel, value, frame, gain);
        if (stage == 1)
            cleanTap = value;
    }
    activity = juce::jmax(activity, juce::jlimit(0.0f, 1.0f, std::abs(value)));

    // Post-clip tone stack for the Modern voice.
    const auto postToned = toneStack(channel.postTone, value, frame);
    value = lerp(value, postToned, frame.postToneWeight);

    // Ratio balances the cleaner early tap and the fully driven output; Level
    // trims the overdrive channel only.
    const auto blend = juce::jlimit(0.0f, 1.0f, 0.15f + 0.8f * frame.ratio);
    const auto result = lerp(cleanTap, value * frame.odLevel, blend);
    return std::isfinite(result) ? result : 0.0f;
}

float VelvetDriveModule::runPowerStage(
    ChannelState& channel, float input, const ParameterFrame& frame,
    float sagGain) noexcept
{
    auto& power = channel.power;
    // Frequency-dependent presence feedback: the low band of the previous
    // output is fed back at sub-unity gain, and Presence reduces the amount of
    // high-frequency feedback so upper harmonics rise. Kept stable.
    const auto feedbackGain = 0.32f;
    const auto driven = input * sagGain - feedbackGain * power.feedbackShaped;

    // Phase inverter and push-pull Class-AB pair with a soft crossover and a
    // small bounded asymmetry between the two halves.
    const auto powerDrive = 1.15f + 0.6f * frame.driveAmount;
    const auto upper = asymmetricShape(driven * powerDrive, 1.0f, 0.08f);
    const auto lower = asymmetricShape(-driven * powerDrive, 1.0f, 0.02f);
    const auto crossover = 0.04f * (1.0f - frame.driveAmount);
    auto pushPull = 0.5f * (upper - lower);
    pushPull -= crossover * std::tanh(pushPull * 6.0f);

    // Update the presence feedback path from the new output.
    const auto feedbackLow = power.feedbackLow.processLowpass(
        pushPull, presenceFeedbackCoefficient);
    const auto feedbackHigh = pushPull - feedbackLow;
    power.feedbackShaped = feedbackLow
        + (1.0f - frame.presence) * feedbackHigh;
    if (!std::isfinite(power.feedbackShaped))
        power.feedbackShaped = 0.0f;

    // DC block after the nonlinear power stage.
    const auto dcBlocked = pushPull - power.dcInput
        + (1.0f - oversampledDcCoefficient) * power.dcOutput;
    power.dcInput = pushPull;
    power.dcOutput = std::isfinite(dcBlocked) ? dcBlocked : 0.0f;
    return power.dcOutput;
}

float VelvetDriveModule::renderSpeaker(
    int model, ChannelState& channel, float input) noexcept
{
    const auto& profile = speakerProfiles[static_cast<size_t>(model)];
    auto& state = channel.speakers[static_cast<size_t>(model)];

    const auto lowState = state.highPass.processLowpass(
        input, speakerHighPassCoefficients[static_cast<size_t>(model)]);
    auto value = input - lowState;

    value += profile.resonanceAGain * state.resonanceA.processBand(
        value, speakerResonanceAG[static_cast<size_t>(model)],
        1.0f / juce::jmax(0.2f, profile.resonanceAQ));
    value += profile.resonanceBGain * state.resonanceB.processBand(
        value, speakerResonanceBG[static_cast<size_t>(model)],
        1.0f / juce::jmax(0.2f, profile.resonanceBQ));

    const auto sparkleLow = state.sparkle.processLowpass(
        value, speakerSparkleCoefficients[static_cast<size_t>(model)]);
    value += profile.sparkleGain * (value - sparkleLow);

    if (profile.compression > 0.0f)
    {
        const auto amount = 1.0f + profile.compression * 2.0f;
        value = value - profile.compression
            * (value - std::tanh(value * amount) / amount);
    }

    value = state.lowPass.processLowpass(
        value, speakerLowPassCoefficients[static_cast<size_t>(model)]);
    const auto result = value * profile.level;
    return std::isfinite(result) ? result : 0.0f;
}

void VelvetDriveModule::process(
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

    const auto inputMode = discreteIndex(
        normalized(inputControl, 0.25f), inputModeCount);
    const auto voice = discreteIndex(
        normalized(voiceControl, 0.166f), voiceCount);
    const auto driveNorm = normalized(driveControl, 0.38f);
    const auto ratioNorm = normalized(ratioControl, 0.46f);
    const auto levelDb = lerp(-18.0f, 12.0f, normalized(levelControl, 0.6f));
    const auto bassNorm = normalized(bassControl, 0.46f);
    const auto midNorm = normalized(midControl, 0.58f);
    const auto trebleNorm = normalized(trebleControl, 0.54f);
    const auto presenceNorm = normalized(presenceControl, 0.45f);
    const auto feelNorm = normalized(feelControl, 0.42f);
    const auto speaker = discreteIndex(
        normalized(speakerControl, 0.375f), speakerModelCount);
    const auto masterDb = lerp(-24.0f, 12.0f, normalized(masterControl, 0.666f));

    const auto driveScaleTarget = exponential(0.5f, 4.0f, driveNorm);
    const auto levelTarget = juce::Decibels::decibelsToGain(levelDb);
    const auto masterTarget = juce::Decibels::decibelsToGain(masterDb);

    inputFetMix.setTargetValue(inputMode == 1 ? 1.0f : 0.0f);
    for (int index = 0; index < voiceCount; ++index)
        voiceMix[static_cast<size_t>(index)].setTargetValue(
            index == voice ? 1.0f : 0.0f);
    for (int index = 0; index < speakerModelCount; ++index)
        speakerMix[static_cast<size_t>(index)].setTargetValue(
            index == speaker ? 1.0f : 0.0f);
    driveSmoothed.setTargetValue(driveScaleTarget);
    ratioSmoothed.setTargetValue(ratioNorm);
    levelSmoothed.setTargetValue(levelTarget);
    bassSmoothed.setTargetValue(bassNorm);
    midSmoothed.setTargetValue(midNorm);
    trebleSmoothed.setTargetValue(trebleNorm);
    presenceSmoothed.setTargetValue(presenceNorm);
    feelSmoothed.setTargetValue(feelNorm);
    masterSmoothed.setTargetValue(masterTarget);

    if (!initialized)
    {
        inputFetMix.setCurrentAndTargetValue(inputMode == 1 ? 1.0f : 0.0f);
        for (int index = 0; index < voiceCount; ++index)
            voiceMix[static_cast<size_t>(index)].setCurrentAndTargetValue(
                index == voice ? 1.0f : 0.0f);
        for (int index = 0; index < speakerModelCount; ++index)
            speakerMix[static_cast<size_t>(index)].setCurrentAndTargetValue(
                index == speaker ? 1.0f : 0.0f);
        driveSmoothed.setCurrentAndTargetValue(driveScaleTarget);
        ratioSmoothed.setCurrentAndTargetValue(ratioNorm);
        levelSmoothed.setCurrentAndTargetValue(levelTarget);
        bassSmoothed.setCurrentAndTargetValue(bassNorm);
        midSmoothed.setCurrentAndTargetValue(midNorm);
        trebleSmoothed.setCurrentAndTargetValue(trebleNorm);
        presenceSmoothed.setCurrentAndTargetValue(presenceNorm);
        feelSmoothed.setCurrentAndTargetValue(feelNorm);
        masterSmoothed.setCurrentAndTargetValue(masterTarget);
        initialized = true;
    }

    for (int sample = 0; sample < sampleCount; ++sample)
    {
        auto& frame = parameterFrames[static_cast<size_t>(sample)];
        frame.inputFetWeight = inputFetMix.getNextValue();
        float voiceSum = 0.0f;
        for (int index = 0; index < voiceCount; ++index)
        {
            frame.voiceWeight[static_cast<size_t>(index)] =
                voiceMix[static_cast<size_t>(index)].getNextValue();
            voiceSum += frame.voiceWeight[static_cast<size_t>(index)];
        }
        if (voiceSum > 1.0e-6f)
            for (auto& weight : frame.voiceWeight)
                weight /= voiceSum;

        float speakerSum = 0.0f;
        for (int index = 0; index < speakerModelCount; ++index)
        {
            frame.speakerWeight[static_cast<size_t>(index)] =
                speakerMix[static_cast<size_t>(index)].getNextValue();
            speakerSum += frame.speakerWeight[static_cast<size_t>(index)];
        }
        if (speakerSum > 1.0e-6f)
            for (auto& weight : frame.speakerWeight)
                weight /= speakerSum;

        frame.driveScale = driveSmoothed.getNextValue();
        frame.driveAmount = juce::jlimit(0.0f, 1.0f, driveNorm);
        frame.ratio = ratioSmoothed.getNextValue();
        frame.odLevel = levelSmoothed.getNextValue();
        frame.bass = bassSmoothed.getNextValue();
        frame.mid = midSmoothed.getNextValue();
        frame.treble = trebleSmoothed.getNextValue();
        frame.presence = presenceSmoothed.getNextValue();
        frame.feelDepth = feelSmoothed.getNextValue();
        frame.master = masterSmoothed.getNextValue();

        // Blend the voice character scalars from the smoothed voice weights.
        frame.stageGainScale = 0.0f;
        frame.knee = 0.0f;
        frame.asymmetry = 0.0f;
        frame.compression = 0.0f;
        frame.preToneWeight = 0.0f;
        frame.postToneWeight = 0.0f;
        for (int index = 0; index < voiceCount; ++index)
        {
            const auto weight = frame.voiceWeight[static_cast<size_t>(index)];
            const auto& profile = voiceProfiles[static_cast<size_t>(index)];
            frame.stageGainScale += weight * profile.stageGainScale;
            frame.knee += weight * profile.knee;
            frame.asymmetry += weight * profile.asymmetry;
            frame.compression += weight * profile.compression;
            frame.preToneWeight += weight * profile.preToneWeight;
            frame.postToneWeight += weight * profile.postToneWeight;
        }

        // Feel sets both supply-sag depth and recovery speed; a slower
        // recovery gives more note bloom.
        frame.sagCoefficient = detail::coefficient(
            oversampledRate, lerp(90.0f, 420.0f, frame.feelDepth));
    }

    // Bound and clean the incoming signal before oversampling.
    for (int channel = 0; channel < channelCount; ++channel)
        for (int sample = 0; sample < sampleCount; ++sample)
        {
            const auto finite =
                detail::finiteSample(buffer.getSample(channel, sample));
            buffer.setSample(
                channel, sample, juce::jlimit(-16.0f, 16.0f, finite));
        }

    float blockInputEnvelope = 0.0f;
    float blockPreampActivity = 0.0f;
    float blockSagReduction = 0.0f;
    float blockPowerActivity = 0.0f;
    float blockOutputEnvelope = 0.0f;

    juce::dsp::AudioBlock<float> inputBlock(buffer);
    auto upsampled = oversampling->processSamplesUp(inputBlock);
    const auto upsampledSamples = static_cast<int>(upsampled.getNumSamples());

    std::array<float, 2> preampNodes {};
    for (int osSample = 0; osSample < upsampledSamples; ++osSample)
    {
        const auto frameIndex = juce::jmin(
            sampleCount - 1, osSample / oversamplingFactor);
        const auto& frame = parameterFrames[static_cast<size_t>(frameIndex)];

        // Input voicing + preamp for each channel first, so the supply detector
        // links both channels and duplicated mono stays matched.
        float detector = 0.0f;
        for (int channel = 0; channel < channelCount; ++channel)
        {
            auto& state = channels[static_cast<size_t>(channel)];
            const auto raw = upsampled.getSample(channel, osSample);
            blockInputEnvelope = juce::jmax(
                blockInputEnvelope, std::abs(raw));

            // Normal / FET input crossfade. FET adds pre-gain, a bright lift
            // and an asymmetric JFET-like soft curvature.
            const auto inputDc = state.input.blocking.processLowpass(
                raw, oversampledDcCoefficient);
            const auto acInput = raw - inputDc;
            const auto brightLow = state.input.brightState.processLowpass(
                acInput, inputBrightCoefficient);
            const auto bright = acInput + 0.5f * (acInput - brightLow);
            const auto fetDriven = bright * 1.6f;
            const auto fet = asymmetricShape(fetDriven, 0.9f, 0.55f);
            const auto inputSignal = lerp(acInput, fet, frame.inputFetWeight);

            float activity = 0.0f;
            auto node = runPreamp(state, inputSignal, frame, activity);
            preampNodes[static_cast<size_t>(channel)] =
                node * frame.master;
            blockPreampActivity = juce::jmax(blockPreampActivity, activity);
            detector = juce::jmax(
                detector,
                std::abs(preampNodes[static_cast<size_t>(channel)]));
        }

        // Stereo-linked dynamic supply sag: fast droop, Feel-controlled slower
        // recovery. It only reduces headroom when signal is present, so it
        // never pumps silence.
        const auto attack = sagAttackCoefficient;
        const auto coefficient = detector > supplyEnvelope
            ? attack : frame.sagCoefficient;
        supplyEnvelope = coefficient * supplyEnvelope
            + (1.0f - coefficient) * detector;
        if (!std::isfinite(supplyEnvelope))
            supplyEnvelope = 0.0f;
        const auto sagAmount = frame.compression
            * (0.4f + 0.6f * frame.feelDepth);
        const auto sagGain = 1.0f
            / (1.0f + sagAmount * supplyEnvelope);
        blockSagReduction = juce::jmax(
            blockSagReduction, juce::jlimit(0.0f, 1.0f, 1.0f - sagGain));

        for (int channel = 0; channel < channelCount; ++channel)
        {
            auto& state = channels[static_cast<size_t>(channel)];
            const auto powerOut = runPowerStage(
                state, preampNodes[static_cast<size_t>(channel)], frame,
                sagGain);
            blockPowerActivity = juce::jmax(
                blockPowerActivity, std::abs(powerOut));
            upsampled.setSample(
                channel, osSample,
                std::isfinite(powerOut) ? powerOut : 0.0f);
        }
    }
    oversampling->processSamplesDown(inputBlock);

    // Analytic speaker voicings, output DC block and calibrated master at the
    // base rate.
    constexpr float outputCalibration = 0.6f;
    for (int channel = 0; channel < channelCount; ++channel)
    {
        auto& state = channels[static_cast<size_t>(channel)];
        for (int sample = 0; sample < sampleCount; ++sample)
        {
            const auto& frame = parameterFrames[static_cast<size_t>(sample)];
            const auto powerOut = buffer.getSample(channel, sample);
            float speakerOut = 0.0f;
            for (int model = 0; model < speakerModelCount; ++model)
            {
                const auto weight =
                    frame.speakerWeight[static_cast<size_t>(model)];
                if (weight <= 0.0f)
                {
                    // Keep inactive speaker filter state running for a
                    // click-free crossfade, but discard its output.
                    renderSpeaker(model, state, powerOut);
                    continue;
                }
                speakerOut += weight
                    * renderSpeaker(model, state, powerOut);
            }

            const auto dcLow = state.outputDcLow.processLowpass(
                speakerOut, baseDcCoefficient);
            const auto dcBlocked = speakerOut - dcLow;
            auto output = dcBlocked * outputCalibration;
            output = juce::jlimit(-8.0f, 8.0f,
                detail::finiteSample(output));
            buffer.setSample(channel, sample, output);
            blockOutputEnvelope = juce::jmax(
                blockOutputEnvelope, std::abs(output));
        }
    }

    if (environment.captureTelemetry)
    {
        telemetryState.sequence += 1;
        telemetryState.valueCount = telemetryValueCount;
        const std::array<float, telemetryValueCount> blockValues {
            juce::jlimit(0.0f, 1.0f, blockInputEnvelope),
            juce::jlimit(0.0f, 1.0f, blockPreampActivity),
            juce::jlimit(0.0f, 1.0f, blockSagReduction),
            juce::jlimit(0.0f, 1.0f, blockPowerActivity),
            juce::jlimit(0.0f, 1.0f, blockOutputEnvelope)
        };
        for (size_t index = 0; index < blockValues.size(); ++index)
            telemetryState.values[index] =
                0.72f * telemetryState.values[index]
                + 0.28f * blockValues[index];
        telemetry.publish(telemetryState);
    }
}

bool VelvetDriveModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}
} // namespace megadsp
