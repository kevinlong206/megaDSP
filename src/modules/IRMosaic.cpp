#include "IRMosaic.h"

#include "DspHelpers.h"
#include "DspSafety.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace megadsp
{
using detail::convolutionMessageQueue;
using detail::exponential;
using detail::finiteSample;
using detail::lerp;

namespace
{
constexpr int tileCount = 8;
constexpr std::array<std::array<int, tileCount>,
                     IRMosaicModule::patternCount> tileOrders {{
    {{ 0, 3, 6, 1, 7, 4, 2, 5 }},
    {{ 5, 1, 7, 3, 0, 6, 4, 2 }},
    {{ 2, 7, 4, 0, 6, 1, 5, 3 }},
    {{ 6, 2, 0, 5, 3, 7, 1, 4 }}
}};

double reshuffleRate(
    const ControlValues& controls, double bpm) noexcept
{
    if (controls[2] >= 0.5f)
    {
        constexpr std::array<double, 8> beats {
            4.0, 2.0, 1.0, 0.5, 0.25, 0.75, 1.5, 3.0
        };
        const auto index = discreteIndex(controls[4], beats.size());
        return juce::jmax(20.0, bpm) / 60.0
               / beats[static_cast<size_t>(index)];
    }
    return exponential(0.03f, 4.0f, controls[1]);
}
} // namespace

struct IRMosaicModule::Engine
{
    Engine()
        : processor(
              juce::dsp::Convolution::NonUniform { 256 },
              convolutionMessageQueue())
    {
    }

    juce::dsp::Convolution processor;
};

struct IRMosaicModule::PreparedLoad final : public PreparedImpulseResponse
{
    std::uint64_t generation = 0;
    std::array<std::unique_ptr<Engine>, patternCount> engines;
    juce::String name;
    ImpulseResponsePreview preview {};
    double lengthSeconds = 0.0;
};

IRMosaicModule::IRMosaicModule()
{
    for (auto& engine : engines)
        engine = std::make_unique<Engine>();
}

IRMosaicModule::~IRMosaicModule() = default;

void IRMosaicModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    preparedSpec = spec;
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    for (size_t pattern = 0; pattern < engines.size(); ++pattern)
    {
        engines[pattern]->processor.prepare(spec);
        wetBuffers[pattern].setSize(
            static_cast<int>(spec.numChannels),
            static_cast<int>(spec.maximumBlockSize), false, true, true);
    }
    dryBuffer.setSize(
        static_cast<int>(spec.numChannels),
        static_cast<int>(spec.maximumBlockSize), false, true, true);
    excitationBuffer.setSize(
        static_cast<int>(spec.numChannels),
        static_cast<int>(spec.maximumBlockSize), false, true, true);
    lowCut.prepare(spec);
    highCut.prepare(spec);
    lowCut.setType(juce::dsp::StateVariableTPTFilterType::highpass);
    highCut.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    for (auto* smoother : {
             &wetLevelSmoothed, &widthSmoothed, &feedbackSmoothed,
             &mixSmoothed })
        smoother->reset(sampleRate, 0.025);
    outputSmoothed.reset(sampleRate, 0.025);
    wetLevelSmoothed.setCurrentAndTargetValue(1.0f);
    widthSmoothed.setCurrentAndTargetValue(0.5f);
    feedbackSmoothed.setCurrentAndTargetValue(0.0f);
    mixSmoothed.setCurrentAndTargetValue(0.35f);
    outputSmoothed.setCurrentAndTargetValue(1.0f);
    telemetryInterval = juce::jmax(
        1, juce::roundToInt(sampleRate / 30.0));
    reset();
}

void IRMosaicModule::reset()
{
    for (size_t pattern = 0; pattern < engines.size(); ++pattern)
    {
        engines[pattern]->processor.reset();
        wetBuffers[pattern].clear();
    }
    dryBuffer.clear();
    excitationBuffer.clear();
    lowCut.reset();
    highCut.reset();
    feedbackState = {};
    reshufflePhase = 0.0;
    randomState = 0x9e3779b9u;
    currentPattern = 0;
    nextPattern = 1;
    pingPongDirection = 1;
    transitionPosition = 0;
    transitionSamples = 1;
    transitioning = false;
    telemetryState = {};
    telemetry.clear();
    telemetryCountdown = telemetryInterval;
    parametersInitialized = false;
}

bool IRMosaicModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}

std::uint32_t IRMosaicModule::nextRandom() noexcept
{
    auto value = randomState;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    randomState = value != 0 ? value : 0x9e3779b9u;
    return randomState;
}

void IRMosaicModule::beginReshuffle(
    int style, double rate, float crossfade)
{
    switch (style)
    {
        case 0:
            nextPattern = (currentPattern + 1) % patternCount;
            break;
        case 1:
            nextPattern = static_cast<int>(
                nextRandom() % static_cast<std::uint32_t>(patternCount - 1));
            if (nextPattern >= currentPattern)
                ++nextPattern;
            break;
        case 2:
            if (currentPattern == 0)
                pingPongDirection = 1;
            else if (currentPattern == patternCount - 1)
                pingPongDirection = -1;
            nextPattern = currentPattern + pingPongDirection;
            break;
        case 3:
        default:
            nextPattern = (currentPattern
                           + 2
                           + static_cast<int>(nextRandom() & 1u))
                          % patternCount;
            break;
    }
    engines[static_cast<size_t>(nextPattern)]->processor.reset();
    const auto requested = exponential(0.05f, 0.5f, crossfade);
    const auto bounded = juce::jmin(
        static_cast<double>(requested), 0.45 / juce::jmax(0.03, rate));
    transitionSamples = juce::jmax(
        1, juce::roundToInt(sampleRate * juce::jmax(0.03, bounded)));
    transitionPosition = 0;
    transitioning = true;
}

void IRMosaicModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    const auto samples = buffer.getNumSamples();
    if (samples <= 0)
        return;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        dryBuffer.copyFrom(channel, 0, buffer, channel, 0, samples);

    wetLevelSmoothed.setTargetValue(controls[5]);
    widthSmoothed.setTargetValue(controls[6]);
    feedbackSmoothed.setTargetValue(controls[7] * 0.55f);
    mixSmoothed.setTargetValue(controls[10]);
    outputSmoothed.setTargetValue(juce::Decibels::decibelsToGain(
        lerp(-18.0f, 12.0f, controls[11])));
    if (!parametersInitialized)
    {
        wetLevelSmoothed.setCurrentAndTargetValue(controls[5]);
        widthSmoothed.setCurrentAndTargetValue(controls[6]);
        feedbackSmoothed.setCurrentAndTargetValue(controls[7] * 0.55f);
        mixSmoothed.setCurrentAndTargetValue(controls[10]);
        outputSmoothed.setCurrentAndTargetValue(
            juce::Decibels::decibelsToGain(
                lerp(-18.0f, 12.0f, controls[11])));
        parametersInitialized = true;
    }
    const auto rate = reshuffleRate(controls, environment.bpm);
    if (!transitioning)
    {
        reshufflePhase += rate * static_cast<double>(samples) / sampleRate;
        if (reshufflePhase >= 1.0)
        {
            reshufflePhase -= std::floor(reshufflePhase);
            beginReshuffle(
                discreteIndex(controls[0], patternCount),
                rate, controls[3]);
        }
    }

    for (int sample = 0; sample < samples; ++sample)
    {
        const auto feedback = feedbackSmoothed.getNextValue();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            excitationBuffer.setSample(
                channel, sample,
                finiteSample(
                    dryBuffer.getSample(channel, sample)
                    + feedbackState[static_cast<size_t>(
                        juce::jmin(channel, 1))] * feedback));
    }

    const auto impulseAvailable = hasImpulseResponse();
    if (!impulseAvailable)
        feedbackState = {};
    for (size_t pattern = 0; pattern < engines.size(); ++pattern)
    {
        const auto active =
            static_cast<int>(pattern) == currentPattern
            || (transitioning
                && static_cast<int>(pattern) == nextPattern);
        if (!active)
        {
            wetBuffers[pattern].clear();
            continue;
        }
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            wetBuffers[pattern].copyFrom(
                channel, 0, excitationBuffer, channel, 0, samples);
        if (!impulseAvailable)
        {
            wetBuffers[pattern].clear();
            continue;
        }
        juce::dsp::AudioBlock<float> block(wetBuffers[pattern]);
        auto range = block.getSubBlock(0, static_cast<size_t>(samples));
        juce::dsp::ProcessContextReplacing<float> context(range);
        engines[pattern]->processor.process(context);
    }

    lowCut.setCutoffFrequency(exponential(20.0f, 1000.0f, controls[8]));
    highCut.setCutoffFrequency(juce::jmin(
        static_cast<float>(sampleRate * 0.45),
        exponential(2000.0f, 20000.0f, controls[9])));
    auto wetEnergy = 0.0;
    auto lastBlend = 0.0f;
    auto lastActivity = 0.0f;
    for (int sample = 0; sample < samples; ++sample)
    {
        auto blend = 0.0f;
        if (transitioning)
            blend = juce::jlimit(
                0.0f, 1.0f,
                static_cast<float>(transitionPosition)
                    / static_cast<float>(transitionSamples));
        const auto oldGain =
            std::cos(blend * juce::MathConstants<float>::halfPi);
        const auto newGain =
            std::sin(blend * juce::MathConstants<float>::halfPi);
        const auto wetLevel = wetLevelSmoothed.getNextValue();
        lastBlend = blend;
        lastActivity = transitioning ? 1.0f : 0.0f;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto current = wetBuffers[
                static_cast<size_t>(currentPattern)].getSample(
                    channel, sample);
            const auto incoming = transitioning
                ? wetBuffers[static_cast<size_t>(nextPattern)].getSample(
                      channel, sample)
                : 0.0f;
            wetBuffers[0].setSample(
                channel, sample,
                (current * oldGain + incoming * newGain) * wetLevel);
        }
        if (transitioning && ++transitionPosition >= transitionSamples)
        {
            currentPattern = nextPattern;
            transitioning = false;
            transitionPosition = 0;
            reshufflePhase = 0.0;
        }
    }
    juce::dsp::AudioBlock<float> wetBlock(wetBuffers[0]);
    auto wetRange = wetBlock.getSubBlock(0, static_cast<size_t>(samples));
    juce::dsp::ProcessContextReplacing<float> wetContext(wetRange);
    lowCut.process(wetContext);
    highCut.process(wetContext);

    for (int sample = 0; sample < samples; ++sample)
    {
        const auto width = widthSmoothed.getNextValue() * 1.5f;
        std::array<float, 2> wet {
            wetBuffers[0].getSample(0, sample),
            wetBuffers[0].getSample(
                juce::jmin(1, buffer.getNumChannels() - 1), sample)
        };
        if (buffer.getNumChannels() > 1)
        {
            const auto mid = 0.5f * (wet[0] + wet[1]);
            const auto side = 0.5f * (wet[0] - wet[1]) * width;
            wet[0] = mid + side;
            wet[1] = mid - side;
        }
        const auto mix = mixSmoothed.getNextValue();
        const auto dryGain = impulseAvailable
            ? std::cos(mix * juce::MathConstants<float>::halfPi) : 1.0f;
        const auto wetGain = impulseAvailable
            ? std::sin(mix * juce::MathConstants<float>::halfPi) : 0.0f;
        const auto output = outputSmoothed.getNextValue();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto wetSample =
                wet[static_cast<size_t>(juce::jmin(channel, 1))];
            auto& feedbackSample =
                feedbackState[static_cast<size_t>(juce::jmin(channel, 1))];
            feedbackSample += 0.08f * (wetSample - feedbackSample);
            feedbackSample = std::tanh(finiteSample(feedbackSample));
            wetEnergy += static_cast<double>(wetSample) * wetSample;
            buffer.setSample(
                channel, sample,
                finiteSample(
                    (dryBuffer.getSample(channel, sample) * dryGain
                     + wetSample * wetGain) * output));
        }
    }

    if (environment.captureTelemetry
        && (telemetryCountdown -= samples) <= 0)
    {
        telemetryState.sequence++;
        telemetryState.valueCount = telemetryValueCount;
        telemetryState.values[primaryPattern] =
            static_cast<float>(currentPattern);
        telemetryState.values[secondaryPattern] =
            static_cast<float>(nextPattern);
        telemetryState.values[patternBlend] = lastBlend;
        telemetryState.values[reshuffleActivity] = lastActivity;
        telemetryState.values[wetLevel] = static_cast<float>(std::sqrt(
            wetEnergy / static_cast<double>(
                juce::jmax(1, samples * buffer.getNumChannels()))));
        telemetry.publish(telemetryState);
        telemetryCountdown = telemetryInterval;
    }
}

double IRMosaicModule::tailSeconds(const ControlValues& controls) const
{
    const auto base = currentImpulseResponseTailSeconds();
    const auto feedback = static_cast<double>(controls[7] * 0.55f);
    return juce::jlimit(0.0, 8.0, base / juce::jmax(0.15, 1.0 - feedback));
}

std::uint64_t IRMosaicModule::beginRequest() noexcept
{
    const auto generation =
        requestCounter.fetch_add(1, std::memory_order_relaxed) + 1;
    requestedGeneration.store(generation, std::memory_order_release);
    return generation;
}

juce::Result IRMosaicModule::prepareImpulseResponse(
    const juce::File& file, PreparedImpulseResponsePtr& destination)
{
    destination.reset();
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

    const auto sourceSamples = static_cast<int>(reader->lengthInSamples);
    const auto channels =
        juce::jlimit(1, 2, static_cast<int>(reader->numChannels));
    juce::AudioBuffer<float> source(channels, sourceSamples);
    if (!reader->read(
            &source, 0, sourceSamples, 0, true, channels > 1))
        return juce::Result::fail("Could not decode the impulse response.");

    auto prepared = std::make_unique<PreparedLoad>();
    prepared->generation = generation;
    for (int pattern = 0; pattern < patternCount; ++pattern)
    {
        juce::AudioBuffer<float> mosaic(channels, sourceSamples);
        for (int destinationTile = 0; destinationTile < tileCount;
             ++destinationTile)
        {
            const auto sourceTile =
                tileOrders[static_cast<size_t>(pattern)]
                          [static_cast<size_t>(destinationTile)];
            const auto destinationStart =
                destinationTile * sourceSamples / tileCount;
            const auto destinationEnd =
                (destinationTile + 1) * sourceSamples / tileCount;
            const auto sourceStart =
                sourceTile * sourceSamples / tileCount;
            const auto sourceEnd =
                (sourceTile + 1) * sourceSamples / tileCount;
            const auto copySamples = juce::jmin(
                destinationEnd - destinationStart,
                sourceEnd - sourceStart);
            const auto fadeSamples = juce::jmin(
                juce::roundToInt(reader->sampleRate * 0.005),
                copySamples / 4);
            for (int channel = 0; channel < channels; ++channel)
                for (int sample = 0; sample < copySamples; ++sample)
                {
                    auto gain = 1.0f;
                    if (sample < fadeSamples)
                        gain *= static_cast<float>(sample)
                                / static_cast<float>(
                                    juce::jmax(1, fadeSamples));
                    if (copySamples - sample <= fadeSamples)
                        gain *= static_cast<float>(copySamples - sample)
                                / static_cast<float>(
                                    juce::jmax(1, fadeSamples));
                    mosaic.setSample(
                        channel, destinationStart + sample,
                        source.getSample(
                            channel, sourceStart + sample) * gain);
                }
        }
        prepared->engines[static_cast<size_t>(pattern)] =
            std::make_unique<Engine>();
        prepared->engines[static_cast<size_t>(pattern)]
            ->processor.loadImpulseResponse(
                std::move(mosaic), reader->sampleRate,
                juce::dsp::Convolution::Stereo::yes,
                juce::dsp::Convolution::Trim::no,
                juce::dsp::Convolution::Normalise::yes);
        prepared->engines[static_cast<size_t>(pattern)]
            ->processor.prepare(preparedSpec);
    }

    auto maximum = 0.0f;
    for (int point = 0; point < impulseResponsePreviewSize; ++point)
    {
        const auto start =
            point * sourceSamples / impulseResponsePreviewSize;
        const auto end =
            (point + 1) * sourceSamples / impulseResponsePreviewSize;
        auto peak = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
            peak = juce::jmax(
                peak, source.getMagnitude(
                          channel, juce::jmin(start, sourceSamples - 1),
                          juce::jmax(1, end - start)));
        prepared->preview[static_cast<size_t>(point)] = peak;
        maximum = juce::jmax(maximum, peak);
    }
    if (maximum > 0.0f)
        for (auto& point : prepared->preview)
            point /= maximum;
    prepared->name = file.getFileName();
    prepared->lengthSeconds =
        static_cast<double>(sourceSamples) / reader->sampleRate;
    destination = std::move(prepared);
    return juce::Result::ok();
}

bool IRMosaicModule::commitPreparedImpulseResponse(
    PreparedImpulseResponsePtr& preparedBase)
{
    auto* prepared = dynamic_cast<PreparedLoad*>(preparedBase.get());
    if (prepared == nullptr
        || prepared->generation
               != requestedGeneration.load(std::memory_order_acquire))
        return false;
    for (size_t pattern = 0; pattern < engines.size(); ++pattern)
    {
        if (prepared->engines[pattern] == nullptr)
            return false;
        engines[pattern] = std::move(prepared->engines[pattern]);
        engines[pattern]->processor.reset();
    }
    {
        const juce::ScopedLock lock(metadataLock);
        loadedName = prepared->name;
        preview = prepared->preview;
    }
    lengthSeconds.store(
        prepared->lengthSeconds, std::memory_order_relaxed);
    committedGeneration.store(
        prepared->generation, std::memory_order_relaxed);
    available.store(true, std::memory_order_release);
    return true;
}

void IRMosaicModule::cancelPreparedImpulseResponse(
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

void IRMosaicModule::clearImpulseResponse()
{
    const auto generation = beginRequest();
    available.store(false, std::memory_order_release);
    committedGeneration.store(generation, std::memory_order_release);
    lengthSeconds.store(0.0, std::memory_order_relaxed);
    const juce::ScopedLock lock(metadataLock);
    loadedName.clear();
    preview.fill(0.0f);
}

bool IRMosaicModule::hasImpulseResponse() const noexcept
{
    return available.load(std::memory_order_acquire);
}

juce::String IRMosaicModule::impulseResponseName() const
{
    if (!hasImpulseResponse())
        return {};
    const juce::ScopedLock lock(metadataLock);
    return loadedName;
}

ImpulseResponsePreview IRMosaicModule::impulseResponsePreview() const
{
    if (!hasImpulseResponse())
        return {};
    const juce::ScopedLock lock(metadataLock);
    return preview;
}

double IRMosaicModule::currentImpulseResponseTailSeconds() const noexcept
{
    return hasImpulseResponse()
        ? lengthSeconds.load(std::memory_order_relaxed) : 0.0;
}

juce::Result IRMosaicModule::loadImpulseResponse(
    const juce::File& file)
{
    PreparedImpulseResponsePtr prepared;
    auto result = prepareImpulseResponse(file, prepared);
    if (result.failed())
        return result;
    return commitPreparedImpulseResponse(prepared)
        ? juce::Result::ok()
        : juce::Result::fail("The impulse response request was superseded.");
}
} // namespace megadsp
