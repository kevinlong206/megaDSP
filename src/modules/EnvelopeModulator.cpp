#include "EnvelopeModulator.h"

#include "DspHelpers.h"
#include "DspSafety.h"

#include <cmath>

namespace megadsp
{
std::array<float, 2> EnvelopeModulatorModule::orderedThresholds(
    float quietDb, float loudDb) noexcept
{
    const auto low = juce::jmin(
        juce::jmin(quietDb, loudDb), maximumLevelDb - minimumLevelSpanDb);
    return { low, juce::jmax(
                      juce::jmax(quietDb, loudDb),
                      low + minimumLevelSpanDb) };
}

void EnvelopeModulatorModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    const auto capacity =
        static_cast<size_t>(std::ceil(sampleRate * 0.25)) + 8;
    for (auto& channel : delayBuffer)
        channel.assign(capacity, 0.0f);
    for (auto& value : smoothed)
        value.reset(sampleRate, 0.03);
    for (auto& weight : modeWeights)
        weight.reset(sampleRate, 0.03);
    for (auto& weight : waveformWeights)
        weight.reset(sampleRate, 0.03);
    vibratoBlend.reset(sampleRate, 0.03);
    crossoverCoefficient = 1.0f - std::exp(
        -juce::MathConstants<float>::twoPi
        * juce::jmin(700.0f, static_cast<float>(sampleRate) * 0.4f)
        / static_cast<float>(sampleRate));
    historyInterval = juce::jmax(1, juce::roundToInt(sampleRate * 0.02));
    reset();
}

void EnvelopeModulatorModule::reset()
{
    for (auto& channel : delayBuffer)
        std::fill(channel.begin(), channel.end(), 0.0f);
    lowState.fill(0.0f);
    envelope = 0.0f;
    phase = 0.0;
    vibratoDelay = 3.0f;
    writePosition = 0;
    historySamples = 0;
    initialized = false;
    wasCapturing = false;
    telemetrySequence = 0;
    liveTelemetry = {};
    telemetry.clear();
    meter.store(0.0f, std::memory_order_relaxed);
}

float EnvelopeModulatorModule::readDelay(
    int channel, float delaySamples) const noexcept
{
    const auto& history = delayBuffer[static_cast<size_t>(channel)];
    const auto size = static_cast<int>(history.size());
    auto position = static_cast<double>(writePosition)
        - static_cast<double>(delaySamples);
    if (position < 0.0)
        position += static_cast<double>(size);
    const auto integerPosition = static_cast<int>(position);
    const auto index1 = integerPosition % size;
    const auto fraction = static_cast<float>(
        position - static_cast<double>(integerPosition));
    const auto y0 = history[static_cast<size_t>((index1 + size - 1) % size)];
    const auto y1 = history[static_cast<size_t>(index1)];
    const auto y2 = history[static_cast<size_t>((index1 + 1) % size)];
    const auto y3 = history[static_cast<size_t>((index1 + 2) % size)];
    const auto a = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    const auto b = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const auto c = 0.5f * (y2 - y0);
    return ((a * fraction + b) * fraction + c) * fraction + y1;
}

void EnvelopeModulatorModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    juce::ScopedNoDenormals noDenormals;
    const auto channels = juce::jmin(2, buffer.getNumChannels());
    const auto samples = buffer.getNumSamples();
    if (channels <= 0 || samples <= 0)
        return;

    ControlValues targets {};
    for (size_t control = 0; control < targets.size(); ++control)
        targets[control] = detail::normalizedControl(controls[control], 0.5f);
    const auto mode = discreteIndex(targets[modeControl], 3);
    const auto waveform = discreteIndex(targets[waveformControl], 3);
    const auto pitchActive = targets[pitchDepthControl] > 0.0f ? 1.0f : 0.0f;
    const auto minimumLogRate = std::log(minimumRateHz);
    const auto logRateRange = std::log(maximumRateHz / minimumRateHz);
    targets[quietSpeedControl] =
        minimumLogRate + targets[quietSpeedControl] * logRateRange;
    targets[loudSpeedControl] =
        minimumLogRate + targets[loudSpeedControl] * logRateRange;
    for (const auto control : { quietLevelControl, loudLevelControl })
        targets[static_cast<size_t>(control)] = detail::lerp(
            minimumLevelDb, maximumLevelDb,
            targets[static_cast<size_t>(control)]);
    targets[attackControl] = detail::coefficient(
        sampleRate, detail::exponential(1.0f, 200.0f, targets[attackControl]));
    targets[releaseControl] = detail::coefficient(
        sampleRate, detail::exponential(30.0f, 3000.0f, targets[releaseControl]));
    targets[pitchDepthControl] *= 100.0f;
    targets[outputControl] = juce::Decibels::decibelsToGain(
        detail::lerp(-12.0f, 12.0f, targets[outputControl]));

    for (size_t control = 0; control < targets.size(); ++control)
    {
        if (initialized)
            smoothed[control].setTargetValue(targets[control]);
        else
            smoothed[control].setCurrentAndTargetValue(targets[control]);
    }
    for (int index = 0; index < 3; ++index)
    {
        const auto setWeight = [this](auto& weight, float target)
        {
            if (initialized)
                weight.setTargetValue(target);
            else
                weight.setCurrentAndTargetValue(target);
        };
        setWeight(modeWeights[static_cast<size_t>(index)],
                  index == mode ? 1.0f : 0.0f);
        setWeight(waveformWeights[static_cast<size_t>(index)],
                  index == waveform ? 1.0f : 0.0f);
    }
    if (initialized)
        vibratoBlend.setTargetValue(pitchActive);
    else
        vibratoBlend.setCurrentAndTargetValue(pitchActive);
    initialized = true;
    if (environment.captureTelemetry && !wasCapturing)
    {
        liveTelemetry = {};
        historySamples = 0;
    }
    wasCapturing = environment.captureTelemetry;
    double wetPower = 0.0;

    for (int sample = 0; sample < samples; ++sample)
    {
        ControlValues current {};
        for (size_t control = 0; control < current.size(); ++control)
            current[control] = smoothed[control].getNextValue();
        std::array<float, 2> dry {};
        float peak = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            dry[index] = detail::finiteSample(buffer.getSample(channel, sample));
            peak = juce::jmax(peak, std::abs(dry[index]));
            delayBuffer[index][static_cast<size_t>(writePosition)] = dry[index];
        }
        const auto envelopeCoefficient = peak > envelope
            ? current[attackControl] : current[releaseControl];
        envelope += (peak - envelope) * (1.0f - envelopeCoefficient);
        const auto level = juce::Decibels::gainToDecibels(envelope, -100.0f);
        const auto thresholds = orderedThresholds(
            current[quietLevelControl], current[loudLevelControl]);
        const auto amount = juce::jlimit(
            0.0f, 1.0f,
            (level - thresholds[0]) / (thresholds[1] - thresholds[0]));
        const auto rate = std::exp(detail::lerp(
            current[quietSpeedControl], current[loudSpeedControl], amount));
        const auto p = static_cast<float>(phase);
        const auto angle = juce::MathConstants<float>::twoPi * p;
        const auto sine = std::sin(angle);
        const std::array<float, 3> waves {
            sine,
            p < 0.25f ? 4.0f * p : p < 0.75f ? 2.0f - 4.0f * p
                                              : 4.0f * p - 4.0f,
            sine * (1.5f - 0.5f * sine * sine)
        };
        // Zero-based integrals keep vibrato excursion independent of waveform.
        const std::array<float, 3> integrals {
            (1.0f - std::cos(angle)) / juce::MathConstants<float>::twoPi,
            p < 0.25f ? 2.0f * p * p
                : p < 0.75f ? 2.0f * p - 2.0f * p * p - 0.25f
                            : 2.0f * p * p - 4.0f * p + 2.0f,
            (1.125f * (1.0f - std::cos(angle))
             + (1.0f - std::cos(3.0f * angle)) / 24.0f)
                / juce::MathConstants<float>::twoPi
        };
        float lfo = 0.0f;
        float integral = 0.0f;
        std::array<float, 3> modes {};
        for (size_t index = 0; index < waves.size(); ++index)
        {
            const auto weight = waveformWeights[index].getNextValue();
            lfo += waves[index] * weight;
            integral += integrals[index] * weight;
            modes[index] = modeWeights[index].getNextValue();
        }
        const auto depth = current[tremoloDepthControl];
        const auto gain = 1.0f - depth * (0.5f + 0.5f * lfo);
        const auto highGain = 1.0f - depth * (0.5f - 0.5f * lfo);
        const auto pitchUp = std::exp2(current[pitchDepthControl] / 1200.0f);
        const auto pitchDown = 1.0f / pitchUp;
        const auto deviation = 0.5f * (pitchUp - pitchDown);
        const auto targetDelay = 3.0f
            + deviation * static_cast<float>(sampleRate) / rate * integral;
        // Bound read-head velocity so rate automation cannot create pitch spikes.
        const auto delayStep = juce::jlimit(
            1.0f - pitchUp, 1.0f - pitchDown, targetDelay - vibratoDelay);
        vibratoDelay += delayStep;
        const auto pitchMix = vibratoBlend.getNextValue();
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            lowState[index] += crossoverCoefficient * (dry[index] - lowState[index]);
            const auto harmonic = lowState[index] * gain
                + (dry[index] - lowState[index]) * highGain;
            const auto vibrato = dry[index]
                + (readDelay(channel, vibratoDelay) - dry[index]) * pitchMix;
            const auto wet = dry[index] * gain * modes[0]
                + harmonic * modes[1] + vibrato * modes[2];
            wetPower += static_cast<double>(wet) * wet;
            buffer.setSample(
                channel, sample,
                detail::finiteSample(
                    (dry[index] + (wet - dry[index]) * current[mixControl])
                    * current[outputControl]));
        }
        writePosition = (writePosition + 1)
            % static_cast<int>(delayBuffer[0].size());
        phase += static_cast<double>(rate) / sampleRate;
        phase -= std::floor(phase);

        if (environment.captureTelemetry)
        {
            liveTelemetry.values[inputLevelDb] = level;
            liveTelemetry.values[quietThresholdDb] = thresholds[0];
            liveTelemetry.values[loudThresholdDb] = thresholds[1];
            liveTelemetry.values[currentRateHz] = rate;
            liveTelemetry.values[currentLfo] = lfo;
            liveTelemetry.values[oscillatorPhase] = p;
            liveTelemetry.values[amplitudeGain] = gain;
            liveTelemetry.values[pitchShiftCents] =
                1200.0f * std::log2(1.0f - delayStep);
            liveTelemetry.values[ratePosition] = amount;
            if (++historySamples >= historyInterval)
            {
                historySamples = 0;
                appendContinuousTelemetryHistory(
                    liveTelemetry, { level, rate, lfo, 0.0f }, 3);
            }
        }
    }
    const auto wetRms = static_cast<float>(std::sqrt(
        wetPower / static_cast<double>(samples * channels)));
    meter.store(juce::jlimit(0.0f, 1.0f, wetRms), std::memory_order_relaxed);
    if (environment.captureTelemetry)
    {
        liveTelemetry.sequence = ++telemetrySequence;
        liveTelemetry.valueCount = telemetryValueCount;
        liveTelemetry.values[wetEnergy] = wetRms;
        telemetry.publish(liveTelemetry);
    }
}

bool EnvelopeModulatorModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}
} // namespace megadsp
