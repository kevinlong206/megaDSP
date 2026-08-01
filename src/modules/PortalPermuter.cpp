#include "PortalPermuter.h"
#include "DspHelpers.h"
#include "DspSafety.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace megadsp
{
using detail::convolutionMessageQueue;
using detail::lerp;

namespace
{
constexpr std::array<double, 8> sliceDivisions {
    1.0, 0.75, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125
};

float normalized(float value, float fallback) noexcept
{
    return detail::normalizedControl(value, fallback);
}

int choice(float value, int choices) noexcept
{
    return juce::jlimit(
        0, choices - 1,
        static_cast<int>(
            normalized(value, 0.0f) * static_cast<float>(choices)));
}

double validBpm(double bpm) noexcept
{
    return std::isfinite(bpm) && bpm >= 30.0 && bpm <= 400.0 ? bpm : 120.0;
}
} // namespace

struct PortalPermuterModule::Engine
{
    Engine()
        : processor(
              juce::dsp::Convolution::NonUniform { 256 },
              convolutionMessageQueue())
    {
    }

    juce::dsp::Convolution processor;
};

struct PortalPermuterModule::PreparedLoad final
    : public PreparedImpulseResponse
{
    std::uint64_t generation = 0;
    std::unique_ptr<Engine> engine;
    juce::String name;
    ImpulseResponsePreview preview {};
    double lengthSeconds = 0.0;
};

PortalPermuterModule::PortalPermuterModule()
    : convolution(std::make_unique<Engine>())
{
}

PortalPermuterModule::~PortalPermuterModule() = default;

void PortalPermuterModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    preparedSpec = spec;
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    convolution->processor.prepare(spec);
    const auto channels = juce::jmax(1, static_cast<int>(spec.numChannels));
    const auto blockSize =
        juce::jmax(1, static_cast<int>(spec.maximumBlockSize));
    dryBuffer.setSize(channels, blockSize, false, true, true);
    excitationBuffer.setSize(channels, blockSize, false, true, true);

    const auto capacity = juce::jmax(
        16, juce::roundToInt(sampleRate * 60.0 / 30.0
                             * maximumHistorySlices)
                + 8);
    for (auto& channel : history)
        channel.assign(static_cast<size_t>(capacity), 0.0f);
    for (auto& channel : feedbackDelay)
        channel.assign(static_cast<size_t>(blockSize), 0.0f);

    feedbackSmoothed.reset(sampleRate, 0.04);
    mixSmoothed.reset(sampleRate, 0.02);
    outputSmoothed.reset(sampleRate, 0.02);
    telemetryInterval = juce::jmax(
        1, juce::roundToInt(sampleRate / 30.0));
    reset();
}

void PortalPermuterModule::reset()
{
    convolution->processor.reset();
    dryBuffer.clear();
    excitationBuffer.clear();
    for (auto& channel : history)
        std::fill(channel.begin(), channel.end(), 0.0f);
    for (auto& channel : feedbackDelay)
        std::fill(channel.begin(), channel.end(), 0.0f);
    feedbackFilter.fill(0.0f);
    transitionFrom.fill(0.0f);
    lastExcitation.fill(0.0f);
    sourceOffsets.fill(0);
    reverseSteps.fill(false);
    currentSliceSamples = juce::jmax(8.0, sampleRate * 0.25);
    slicePosition = 0.0;
    playbackLengthSamples = currentSliceSamples;
    pitchRatio = 1.0;
    writtenSamples = 0;
    sourceBoundary = 0;
    randomState = 0x51f15e5du;
    historyWritePosition = 0;
    validHistorySamples = 0;
    feedbackPosition = 0;
    eventStep = 0;
    eventLength = 0;
    transitionPosition = 0;
    transitionSamples = 1;
    eventActive = false;
    transportInitialized = false;
    parametersInitialized = false;
    telemetryState = {};
    telemetry.clear();
    telemetryCountdown = telemetryInterval;
    feedbackSmoothed.setCurrentAndTargetValue(0.0f);
    mixSmoothed.setCurrentAndTargetValue(0.0f);
    outputSmoothed.setCurrentAndTargetValue(1.0f);
}

std::uint32_t PortalPermuterModule::nextRandom() noexcept
{
    auto value = randomState;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    randomState = value != 0 ? value : 0x51f15e5du;
    return randomState;
}

float PortalPermuterModule::randomUnit() noexcept
{
    return static_cast<float>(nextRandom() >> 8)
           * (1.0f / 16777216.0f);
}

int PortalPermuterModule::randomInteger(int low, int high) noexcept
{
    if (high <= low)
        return low;
    return low + static_cast<int>(
                     nextRandom()
                     % static_cast<std::uint32_t>(high - low + 1));
}

double PortalPermuterModule::sliceSamples(
    float control, double bpm) const noexcept
{
    return juce::jmax(
        8.0, sampleRate * 60.0 / validBpm(bpm)
                 * sliceDivisions[static_cast<size_t>(choice(control, 8))]);
}

void PortalPermuterModule::beginSlice(
    const ControlValues& controls, double bpm)
{
    transitionFrom = lastExcitation;
    transitionSamples = juce::jlimit(
        8, 128, juce::roundToInt(currentSliceSamples * 0.04));
    transitionPosition = 0;
    slicePosition = 0.0;

    if (eventActive && ++eventStep < eventLength)
        return;

    eventActive = false;
    eventStep = 0;
    eventLength = juce::jlimit(
        1, maximumEventSteps,
        1 + juce::roundToInt(normalized(controls[4], 0.0f) * 7.0f));
    currentSliceSamples = sliceSamples(controls[0], bpm);
    playbackLengthSamples = juce::jmax(
        8.0, currentSliceSamples
                 * lerp(0.125f, 1.0f, normalized(controls[5], 1.0f)));
    pitchRatio = std::pow(
        2.0, static_cast<double>(
                 lerp(-12.0f, 12.0f, normalized(controls[6], 0.5f)))
                 / 12.0);

    const auto completeSlices = static_cast<int>(std::floor(
        (static_cast<double>(validHistorySamples) - 4.0)
        / currentSliceSamples));
    const auto requestedHistory = juce::jlimit(
        1, maximumHistorySlices,
        1 + juce::roundToInt(normalized(controls[3], 0.0f) * 7.0f));
    const auto availableHistory =
        juce::jmin(requestedHistory, completeSlices);
    if (availableHistory <= 0
        || randomUnit() > normalized(controls[1], 0.35f))
        return;

    eventActive = true;
    sourceBoundary = writtenSamples;
    const auto pattern = choice(controls[2], 4);
    const auto base = randomInteger(0, availableHistory - 1);
    for (int step = 0; step < maximumEventSteps; ++step)
    {
        switch (pattern)
        {
            case 0:
                sourceOffsets[static_cast<size_t>(step)] = base;
                reverseSteps[static_cast<size_t>(step)] = false;
                break;
            case 1:
                sourceOffsets[static_cast<size_t>(step)] =
                    (availableHistory - 1 - step % availableHistory);
                reverseSteps[static_cast<size_t>(step)] = true;
                break;
            case 2:
                sourceOffsets[static_cast<size_t>(step)] =
                    (base + step) % availableHistory;
                reverseSteps[static_cast<size_t>(step)] = false;
                break;
            case 3:
            default:
                sourceOffsets[static_cast<size_t>(step)] =
                    randomInteger(0, availableHistory - 1);
                reverseSteps[static_cast<size_t>(step)] =
                    (nextRandom() & 1u) != 0;
                break;
        }
    }
}

float PortalPermuterModule::historySample(
    int channel, std::int64_t absoluteSample) const noexcept
{
    const auto& samples =
        history[static_cast<size_t>(juce::jlimit(0, 1, channel))];
    if (samples.empty() || absoluteSample < 0
        || static_cast<std::uint64_t>(absoluteSample) >= writtenSamples)
        return 0.0f;
    const auto oldest = static_cast<std::int64_t>(writtenSamples)
                        - static_cast<std::int64_t>(validHistorySamples);
    if (absoluteSample < oldest)
        return 0.0f;
    auto index =
        absoluteSample % static_cast<std::int64_t>(samples.size());
    if (index < 0)
        index += static_cast<std::int64_t>(samples.size());
    return samples[static_cast<size_t>(index)];
}

float PortalPermuterModule::readHistory(
    int channel, double absoluteSample) const noexcept
{
    const auto base = static_cast<std::int64_t>(
        std::floor(absoluteSample));
    const auto fraction =
        static_cast<float>(absoluteSample - static_cast<double>(base));
    const auto y0 = historySample(channel, base - 1);
    const auto y1 = historySample(channel, base);
    const auto y2 = historySample(channel, base + 1);
    const auto y3 = historySample(channel, base + 2);
    const auto a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    const auto a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const auto a2 = -0.5f * y0 + 0.5f * y2;
    return ((a0 * fraction + a1) * fraction + a2) * fraction + y1;
}

std::array<float, 2>
PortalPermuterModule::renderExcitation() const noexcept
{
    if (!eventActive || slicePosition >= playbackLengthSamples)
        return {};

    const auto step =
        juce::jlimit(0, eventLength - 1, eventStep);
    const auto sourceStart =
        static_cast<double>(sourceBoundary)
        - static_cast<double>(
              sourceOffsets[static_cast<size_t>(step)] + 1)
              * currentSliceSamples;
    const auto phase = slicePosition / playbackLengthSamples;
    const auto pitchedPhase = phase * pitchRatio;
    const auto reverse = reverseSteps[static_cast<size_t>(step)];
    const auto read = [this, sourceStart, reverse](int channel,
                                                   double readPhase)
    {
        readPhase -= std::floor(readPhase);
        auto offset = readPhase * currentSliceSamples;
        if (reverse)
            offset = juce::jmax(
                0.0, currentSliceSamples - 1.0 - offset);
        return readHistory(channel, sourceStart + offset);
    };

    auto phaseA = pitchedPhase - std::floor(pitchedPhase);
    auto phaseB = phaseA + 0.5;
    if (phaseB >= 1.0)
        phaseB -= 1.0;
    const auto sine =
        std::sin(juce::MathConstants<double>::pi * phaseA);
    const auto weightA = static_cast<float>(sine * sine);
    const auto edge = juce::jmin(
        1.0, juce::jmin(slicePosition,
                        playbackLengthSamples - slicePosition)
                 / juce::jmax(8.0, playbackLengthSamples * 0.04));
    std::array<float, 2> result {};
    for (int channel = 0; channel < 2; ++channel)
        result[static_cast<size_t>(channel)] =
            (read(channel, phaseA) * weightA
             + read(channel, phaseB) * (1.0f - weightA))
            * static_cast<float>(edge);
    return result;
}

void PortalPermuterModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    juce::ScopedNoDenormals noDenormals;
    const auto samples = buffer.getNumSamples();
    const auto channels = buffer.getNumChannels();
    if (samples <= 0 || channels <= 0 || history[0].empty())
        return;

    jassert(samples <= dryBuffer.getNumSamples());
    jassert(channels <= dryBuffer.getNumChannels());
    for (int channel = 0; channel < channels; ++channel)
        dryBuffer.copyFrom(channel, 0, buffer, channel, 0, samples);

    const auto feedbackTarget =
        normalized(controls[9], 0.0f) * 0.55f;
    const auto mixTarget = normalized(controls[10], 0.35f);
    const auto outputTarget = juce::Decibels::decibelsToGain(
        lerp(-18.0f, 12.0f, normalized(controls[11], 0.6f)));
    feedbackSmoothed.setTargetValue(feedbackTarget);
    mixSmoothed.setTargetValue(mixTarget);
    outputSmoothed.setTargetValue(outputTarget);
    if (!parametersInitialized)
    {
        feedbackSmoothed.setCurrentAndTargetValue(feedbackTarget);
        mixSmoothed.setCurrentAndTargetValue(mixTarget);
        outputSmoothed.setCurrentAndTargetValue(outputTarget);
        parametersInitialized = true;
    }
    if (!transportInitialized)
    {
        currentSliceSamples = sliceSamples(controls[0], environment.bpm);
        transportInitialized = true;
    }

    excitationBuffer.clear();
    const auto historySize = static_cast<int>(history[0].size());
    const auto feedbackSize =
        static_cast<int>(feedbackDelay[0].size());
    const auto feedbackStart = feedbackPosition;
    for (int sample = 0; sample < samples; ++sample)
    {
        if (slicePosition >= currentSliceSamples)
            beginSlice(controls, environment.bpm);

        const std::array<float, 2> dry {
            dryBuffer.getSample(0, sample),
            channels > 1 ? dryBuffer.getSample(1, sample)
                         : dryBuffer.getSample(0, sample)
        };
        auto excitation = renderExcitation();
        if (transitionPosition < transitionSamples)
        {
            const auto progress =
                static_cast<float>(transitionPosition)
                / static_cast<float>(transitionSamples);
            const auto oldGain =
                std::cos(progress * juce::MathConstants<float>::halfPi);
            const auto newGain =
                std::sin(progress * juce::MathConstants<float>::halfPi);
            for (int channel = 0; channel < 2; ++channel)
                excitation[static_cast<size_t>(channel)] =
                    transitionFrom[static_cast<size_t>(channel)] * oldGain
                    + excitation[static_cast<size_t>(channel)] * newGain;
            ++transitionPosition;
        }

        const auto feedback = feedbackSmoothed.getNextValue();
        for (int channel = 0; channel < 2; ++channel)
        {
            feedbackFilter[static_cast<size_t>(channel)] =
                0.88f * feedbackFilter[static_cast<size_t>(channel)]
                + 0.12f
                      * feedbackDelay[static_cast<size_t>(channel)]
                                     [static_cast<size_t>(feedbackPosition)];
            feedbackFilter[static_cast<size_t>(channel)] =
                std::tanh(feedbackFilter[static_cast<size_t>(channel)]);
            excitation[static_cast<size_t>(channel)] +=
                feedback * feedbackFilter[static_cast<size_t>(channel)];
            history[static_cast<size_t>(channel)]
                   [static_cast<size_t>(historyWritePosition)] =
                detail::finiteSample(dry[static_cast<size_t>(channel)]);
        }
        excitationBuffer.setSample(0, sample, excitation[0]);
        if (channels > 1)
            excitationBuffer.setSample(1, sample, excitation[1]);
        lastExcitation = excitation;

        if (++historyWritePosition >= historySize)
            historyWritePosition = 0;
        validHistorySamples =
            juce::jmin(historySize, validHistorySamples + 1);
        ++writtenSamples;
        if (++feedbackPosition >= feedbackSize)
            feedbackPosition = 0;
        slicePosition += 1.0;
    }

    const auto available =
        impulseAvailable.load(std::memory_order_acquire);
    if (available)
    {
        juce::dsp::AudioBlock<float> wetBlock(excitationBuffer);
        auto range =
            wetBlock.getSubBlock(0, static_cast<size_t>(samples));
        juce::dsp::ProcessContextReplacing<float> context(range);
        convolution->processor.process(context);
        auto position = feedbackStart;
        for (int sample = 0; sample < samples; ++sample)
        {
            for (int channel = 0; channel < 2; ++channel)
            {
                const auto sourceChannel =
                    juce::jmin(channel, channels - 1);
                feedbackDelay[static_cast<size_t>(channel)]
                             [static_cast<size_t>(position)] =
                    detail::finiteSample(
                        excitationBuffer.getSample(sourceChannel, sample));
            }
            if (++position >= feedbackSize)
                position = 0;
        }
    }
    else
    {
        for (auto& channel : feedbackDelay)
            std::fill(channel.begin(), channel.end(), 0.0f);
        feedbackFilter.fill(0.0f);
    }

    auto wetEnergy = 0.0;
    for (int sample = 0; sample < samples; ++sample)
    {
        const auto mix = mixSmoothed.getNextValue();
        const auto dryGain = available
            ? std::cos(mix * juce::MathConstants<float>::halfPi)
            : 1.0f;
        const auto wetGain = available
            ? std::sin(mix * juce::MathConstants<float>::halfPi)
            : 0.0f;
        const auto output = outputSmoothed.getNextValue();
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto wet = excitationBuffer.getSample(channel, sample);
            wetEnergy += static_cast<double>(wet) * wet;
            const auto result =
                (dryBuffer.getSample(channel, sample) * dryGain
                 + wet * wetGain)
                * output;
            buffer.setSample(
                channel, sample, detail::finiteSample(result));
        }
    }
    if (environment.captureTelemetry
        && (telemetryCountdown -= samples) <= 0)
    {
        const auto step = juce::jlimit(
            0, maximumEventSteps - 1, eventStep);
        telemetryState.sequence++;
        telemetryState.valueCount = telemetryValueCount;
        telemetryState.values[eventActiveValue] =
            eventActive ? 1.0f : 0.0f;
        telemetryState.values[sourceSliceValue] =
            static_cast<float>(
                sourceOffsets[static_cast<size_t>(step)])
            / static_cast<float>(maximumHistorySlices - 1);
        telemetryState.values[reverseValue] =
            reverseSteps[static_cast<size_t>(step)] ? 1.0f : 0.0f;
        telemetryState.values[progressValue] = static_cast<float>(
            juce::jlimit(
                0.0, 1.0, slicePosition
                              / juce::jmax(1.0, currentSliceSamples)));
        telemetryState.values[wetLevelValue] = static_cast<float>(
            std::sqrt(wetEnergy / static_cast<double>(
                juce::jmax(1, samples * channels))));
        telemetry.publish(telemetryState);
        telemetryCountdown = telemetryInterval;
    }
}

bool PortalPermuterModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}

double PortalPermuterModule::tailSeconds(
    const ControlValues& controls) const
{
    const auto ir =
        currentImpulseResponseTailSeconds();
    if (ir <= 0.0)
        return 0.0;
    const auto feedback =
        static_cast<double>(normalized(controls[9], 0.0f) * 0.55f);
    const auto repeats = feedback > 0.0001
        ? std::log(0.001) / std::log(feedback * 0.95)
        : 0.0;
    return juce::jmin(
        8.0, ir + juce::jmax(0.0, repeats)
                      * (ir
                         + static_cast<double>(
                               preparedSpec.maximumBlockSize)
                               / sampleRate));
}

std::uint64_t PortalPermuterModule::beginRequest() noexcept
{
    const auto generation =
        requestCounter.fetch_add(1, std::memory_order_relaxed) + 1;
    requestedGeneration.store(generation, std::memory_order_release);
    return generation;
}

ImpulseResponseTransform PortalPermuterModule::impulseResponseTransform(
    const float* controls, int lane) const noexcept
{
    if (controls == nullptr || lane != 0)
        return {};
    constexpr auto region = 0.35f;
    const auto start =
        normalized(controls[7], 0.5f) * (1.0f - region);
    return { start, start + region, controls[8] >= 0.5f };
}

juce::Result PortalPermuterModule::prepareImpulseResponse(
    int lane, const juce::File& file,
    const ImpulseResponseTransform& transform,
    PreparedImpulseResponsePtr& destination)
{
    destination.reset();
    if (lane != 0)
        return juce::Result::fail("Invalid Portal Permuter IR lane.");
    const auto generation = beginRequest();
    if (!file.existsAsFile())
        return juce::Result::fail("Impulse response file does not exist.");

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(
        formats.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0
        || reader->sampleRate <= 0.0
        || reader->lengthInSamples
               > static_cast<juce::int64>(std::numeric_limits<int>::max()))
        return juce::Result::fail("Unsupported or empty impulse response.");

    const auto sourceSamples =
        static_cast<int>(reader->lengthInSamples);
    const auto channels =
        juce::jlimit(1, 2, static_cast<int>(reader->numChannels));
    juce::AudioBuffer<float> source(channels, sourceSamples);
    if (!reader->read(
            &source, 0, sourceSamples, 0, true, channels > 1))
        return juce::Result::fail("Could not decode the impulse response.");

    const auto lower = juce::jlimit(
        0.0f, 0.9999f, juce::jmin(transform.start, transform.end));
    const auto upper = juce::jlimit(
        lower + 0.0001f, 1.0f,
        juce::jmax(transform.start, transform.end));
    const auto start = juce::jlimit(
        0, sourceSamples - 1,
        juce::roundToInt(lower * static_cast<float>(sourceSamples - 1)));
    const auto end = juce::jlimit(
        start + 1, sourceSamples,
        juce::roundToInt(upper * static_cast<float>(sourceSamples)));
    const auto croppedSamples = end - start;
    juce::AudioBuffer<float> decoded(channels, croppedSamples);
    for (int channel = 0; channel < channels; ++channel)
        decoded.copyFrom(
            channel, 0, source, channel, start, croppedSamples);
    if (transform.reverse)
        for (int channel = 0; channel < channels; ++channel)
            std::reverse(
                decoded.getWritePointer(channel),
                decoded.getWritePointer(channel) + croppedSamples);

    ImpulseResponsePreview preparedPreview {};
    auto previewMaximum = 0.0f;
    for (int point = 0; point < impulseResponsePreviewSize; ++point)
    {
        const auto pointStart =
            point * croppedSamples / impulseResponsePreviewSize;
        const auto pointEnd =
            (point + 1) * croppedSamples / impulseResponsePreviewSize;
        auto peak = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
            peak = juce::jmax(
                peak, decoded.getMagnitude(
                          channel,
                          juce::jmin(pointStart, croppedSamples - 1),
                          juce::jmax(1, pointEnd - pointStart)));
        preparedPreview[static_cast<size_t>(point)] = peak;
        previewMaximum = juce::jmax(previewMaximum, peak);
    }
    if (previewMaximum > 0.0f)
        for (auto& point : preparedPreview)
            point /= previewMaximum;

    auto prepared = std::make_unique<PreparedLoad>();
    prepared->generation = generation;
    prepared->engine = std::make_unique<Engine>();
    prepared->engine->processor.loadImpulseResponse(
        std::move(decoded), reader->sampleRate,
        juce::dsp::Convolution::Stereo::yes,
        juce::dsp::Convolution::Trim::no,
        juce::dsp::Convolution::Normalise::yes);
    prepared->engine->processor.prepare(preparedSpec);
    prepared->name = file.getFileName();
    prepared->preview = preparedPreview;
    prepared->lengthSeconds =
        static_cast<double>(croppedSamples) / reader->sampleRate;
    destination = std::move(prepared);
    return juce::Result::ok();
}

juce::Result PortalPermuterModule::prepareImpulseResponse(
    const juce::File& file, PreparedImpulseResponsePtr& destination)
{
    return prepareImpulseResponse(0, file, {}, destination);
}

bool PortalPermuterModule::commitPreparedImpulseResponse(
    PreparedImpulseResponsePtr& preparedBase)
{
    auto* prepared = dynamic_cast<PreparedLoad*>(preparedBase.get());
    if (prepared == nullptr || prepared->engine == nullptr
        || prepared->generation
               != requestedGeneration.load(std::memory_order_acquire))
        return false;
    convolution = std::move(prepared->engine);
    convolution->processor.reset();
    {
        const juce::ScopedLock lock(metadataLock);
        loadedImpulseName = prepared->name;
        preview = prepared->preview;
    }
    impulseLengthSeconds.store(
        prepared->lengthSeconds, std::memory_order_relaxed);
    committedGeneration.store(
        prepared->generation, std::memory_order_relaxed);
    impulseAvailable.store(true, std::memory_order_release);
    prepared->generation = 0;
    return true;
}

void PortalPermuterModule::cancelPreparedImpulseResponse(
    PreparedImpulseResponsePtr& preparedBase) noexcept
{
    if (auto* prepared = dynamic_cast<PreparedLoad*>(preparedBase.get()))
    {
        auto expected = prepared->generation;
        requestedGeneration.compare_exchange_strong(
            expected,
            committedGeneration.load(std::memory_order_acquire),
            std::memory_order_release, std::memory_order_relaxed);
    }
    preparedBase.reset();
}

juce::Result PortalPermuterModule::loadImpulseResponse(
    const juce::File& file)
{
    PreparedImpulseResponsePtr prepared;
    const auto result = prepareImpulseResponse(file, prepared);
    if (result.failed())
        return result;
    return commitPreparedImpulseResponse(prepared)
        ? juce::Result::ok()
        : juce::Result::fail("The impulse response request was superseded.");
}

void PortalPermuterModule::clearImpulseResponse()
{
    const auto generation = beginRequest();
    impulseAvailable.store(false, std::memory_order_release);
    impulseLengthSeconds.store(0.0, std::memory_order_relaxed);
    committedGeneration.store(generation, std::memory_order_release);
    const juce::ScopedLock lock(metadataLock);
    loadedImpulseName.clear();
    preview.fill(0.0f);
}

bool PortalPermuterModule::hasImpulseResponse() const noexcept
{
    return impulseAvailable.load(std::memory_order_acquire);
}

bool PortalPermuterModule::isImpulseResponseReady() const noexcept
{
    return hasImpulseResponse();
}

juce::String PortalPermuterModule::impulseResponseName() const
{
    if (!hasImpulseResponse())
        return {};
    const juce::ScopedLock lock(metadataLock);
    return loadedImpulseName;
}

ImpulseResponsePreview
PortalPermuterModule::impulseResponsePreview() const
{
    if (!hasImpulseResponse())
        return {};
    const juce::ScopedLock lock(metadataLock);
    return preview;
}

double PortalPermuterModule::currentImpulseResponseTailSeconds()
    const noexcept
{
    return hasImpulseResponse()
        ? impulseLengthSeconds.load(std::memory_order_relaxed)
        : 0.0;
}
} // namespace megadsp
