#include "WormholeConvolution.h"
#include "DspHelpers.h"
#include "DspSafety.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace megadsp
{
using detail::convolutionMessageQueue;
using detail::exponential;
using detail::lerp;

struct WormholeConvolutionModule::Engine
{
    Engine()
        : processor(
              juce::dsp::Convolution::NonUniform { 256 },
              convolutionMessageQueue())
    {
    }

    juce::dsp::Convolution processor;
};

struct WormholeConvolutionModule::PreparedLoad final
    : public PreparedImpulseResponse
{
    int lane = 0;
    std::uint64_t generation = 0;
    std::unique_ptr<Engine> engine;
    juce::String name;
    ImpulseResponsePreview preview {};
    double lengthSeconds = 0.0;
};

struct WormholeConvolutionModule::Lane
{
    std::unique_ptr<Engine> active = std::make_unique<Engine>();
    std::unique_ptr<Engine> retiring;
    juce::AudioBuffer<float> wet;
    juce::AudioBuffer<float> retiringWet;
    std::atomic<std::uint64_t> requestCounter { 0 };
    std::atomic<std::uint64_t> requestedGeneration { 0 };
    std::atomic<std::uint64_t> committedGeneration { 0 };
    std::atomic<bool> available { false };
    std::atomic<double> lengthSeconds { 0.0 };
    mutable juce::CriticalSection metadataLock;
    juce::String name;
    ImpulseResponsePreview preview {};
    int fadeSamplesRemaining = 0;
    int fadeSamplesTotal = 1;
    bool retiringAvailable = false;
};

WormholeConvolutionModule::WormholeConvolutionModule()
{
    for (auto& lane : lanes)
        lane = std::make_unique<Lane>();
}

WormholeConvolutionModule::~WormholeConvolutionModule() = default;

void WormholeConvolutionModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    preparedSpec = spec;
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    for (auto& lane : lanes)
    {
        lane->active->processor.prepare(spec);
        lane->wet.setSize(
            static_cast<int>(spec.numChannels),
            static_cast<int>(spec.maximumBlockSize), false, true, true);
        lane->retiringWet.setSize(
            static_cast<int>(spec.numChannels),
            static_cast<int>(spec.maximumBlockSize), false, true, true);
        lane->fadeSamplesTotal =
            juce::jmax(1, juce::roundToInt(sampleRate * 0.02));
    }
    dryBuffer.setSize(
        static_cast<int>(spec.numChannels),
        static_cast<int>(spec.maximumBlockSize), false, true, true);
    lowCut.prepare(spec);
    highCut.prepare(spec);
    lowCut.setType(juce::dsp::StateVariableTPTFilterType::highpass);
    highCut.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    for (auto& gain : laneGain)
    {
        gain.reset(sampleRate, 0.025);
        gain.setCurrentAndTargetValue(0.35f);
    }
    dryGain.reset(sampleRate, 0.025);
    outputGain.reset(sampleRate, 0.025);
    dryGain.setCurrentAndTargetValue(1.0f);
    outputGain.setCurrentAndTargetValue(1.0f);
    reset();
}

void WormholeConvolutionModule::reset()
{
    for (auto& lane : lanes)
    {
        lane->active->processor.reset();
        if (lane->retiring != nullptr)
            lane->retiring->processor.reset();
        lane->wet.clear();
        lane->retiringWet.clear();
        lane->fadeSamplesRemaining = 0;
    }
    dryBuffer.clear();
    lowCut.reset();
    highCut.reset();
}

void WormholeConvolutionModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment&)
{
    const auto samples = buffer.getNumSamples();
    if (samples <= 0)
        return;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        dryBuffer.copyFrom(channel, 0, buffer, channel, 0, samples);

    laneGain[0].setTargetValue(controls[3]);
    laneGain[1].setTargetValue(controls[7]);
    dryGain.setTargetValue(controls[10]);
    outputGain.setTargetValue(juce::Decibels::decibelsToGain(
        lerp(-18.0f, 12.0f, controls[11])));

    for (int laneIndex = 0; laneIndex < laneCount; ++laneIndex)
    {
        auto& lane = *lanes[static_cast<size_t>(laneIndex)];
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            lane.wet.copyFrom(channel, 0, dryBuffer, channel, 0, samples);
            lane.retiringWet.copyFrom(
                channel, 0, dryBuffer, channel, 0, samples);
        }
        if (laneAvailable(laneIndex))
        {
            juce::dsp::AudioBlock<float> block(lane.wet);
            auto range = block.getSubBlock(0, static_cast<size_t>(samples));
            juce::dsp::ProcessContextReplacing<float> context(range);
            lane.active->processor.process(context);
        }
        else
            lane.wet.clear();

        if (lane.fadeSamplesRemaining > 0
            && lane.retiring != nullptr && lane.retiringAvailable)
        {
            juce::dsp::AudioBlock<float> oldBlock(lane.retiringWet);
            auto oldRange =
                oldBlock.getSubBlock(0, static_cast<size_t>(samples));
            juce::dsp::ProcessContextReplacing<float> oldContext(oldRange);
            lane.retiring->processor.process(oldContext);
            for (int sample = 0; sample < samples; ++sample)
            {
                const auto progress = 1.0f
                    - static_cast<float>(lane.fadeSamplesRemaining)
                        / static_cast<float>(lane.fadeSamplesTotal);
                const auto smooth =
                    progress * progress * (3.0f - 2.0f * progress);
                for (int channel = 0; channel < buffer.getNumChannels();
                     ++channel)
                    lane.wet.setSample(
                        channel, sample,
                        lerp(lane.retiringWet.getSample(channel, sample),
                             lane.wet.getSample(channel, sample), smooth));
                lane.fadeSamplesRemaining =
                    juce::jmax(0, lane.fadeSamplesRemaining - 1);
            }
        }
    }

    lowCut.setCutoffFrequency(exponential(20.0f, 1000.0f, controls[8]));
    highCut.setCutoffFrequency(juce::jmin(
        static_cast<float>(sampleRate * 0.45),
        exponential(2000.0f, 20000.0f, controls[9])));
    for (int sample = 0; sample < samples; ++sample)
    {
        const std::array<float, 2> wet {
            laneGain[0].getNextValue(), laneGain[1].getNextValue()
        };
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.setSample(
                channel, sample,
                lanes[0]->wet.getSample(channel, sample) * wet[0]
                + lanes[1]->wet.getSample(channel, sample) * wet[1]);
    }
    juce::dsp::AudioBlock<float> outputBlock(buffer);
    auto outputRange =
        outputBlock.getSubBlock(0, static_cast<size_t>(samples));
    juce::dsp::ProcessContextReplacing<float> outputContext(outputRange);
    lowCut.process(outputContext);
    highCut.process(outputContext);
    for (int sample = 0; sample < samples; ++sample)
    {
        const auto dry = dryGain.getNextValue();
        const auto output = outputGain.getNextValue();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.setSample(
                channel, sample,
                (buffer.getSample(channel, sample)
                 + dryBuffer.getSample(channel, sample) * dry)
                    * output);
    }
}

double WormholeConvolutionModule::tailSeconds(
    const ControlValues& controls) const
{
    auto tail = 0.0;
    for (int lane = 0; lane < laneCount; ++lane)
        if (controls[static_cast<size_t>(lane == 0 ? 3 : 7)] > 0.0001f)
            tail = juce::jmax(
                tail, lanes[static_cast<size_t>(lane)]->lengthSeconds.load(
                          std::memory_order_relaxed));
    return tail;
}

std::uint64_t WormholeConvolutionModule::beginRequest(int lane) noexcept
{
    auto& state = *lanes[static_cast<size_t>(lane)];
    const auto generation =
        state.requestCounter.fetch_add(1, std::memory_order_relaxed) + 1;
    state.requestedGeneration.store(generation, std::memory_order_release);
    return generation;
}

ImpulseResponseTransform WormholeConvolutionModule::impulseResponseTransform(
    const float* controls, int lane) const noexcept
{
    if (controls == nullptr || !juce::isPositiveAndBelow(lane, laneCount))
        return {};
    const auto offset = lane == 0 ? 0 : 4;
    return {
        controls[offset],
        controls[offset + 1],
        controls[offset + 2] >= 0.5f
    };
}

juce::Result WormholeConvolutionModule::prepareImpulseResponse(
    int lane, const juce::File& file,
    const ImpulseResponseTransform& transform,
    PreparedImpulseResponsePtr& destination)
{
    destination.reset();
    if (!juce::isPositiveAndBelow(lane, laneCount))
        return juce::Result::fail("Invalid Wormhole IR lane.");
    const auto generation = beginRequest(lane);
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

    const auto lower = juce::jlimit(
        0.0f, 0.9999f, juce::jmin(transform.start, transform.end));
    const auto upper = juce::jlimit(
        lower + 0.0001f, 1.0f,
        juce::jmax(transform.start, transform.end));
    const auto start =
        juce::jlimit(0, sourceSamples - 1,
                     juce::roundToInt(
                         lower * static_cast<float>(sourceSamples - 1)));
    const auto end =
        juce::jlimit(start + 1, sourceSamples,
                     juce::roundToInt(
                         upper * static_cast<float>(sourceSamples)));
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

    ImpulseResponsePreview preview {};
    auto maximum = 0.0f;
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
                          channel, juce::jmin(pointStart, croppedSamples - 1),
                          juce::jmax(1, pointEnd - pointStart)));
        preview[static_cast<size_t>(point)] = peak;
        maximum = juce::jmax(maximum, peak);
    }
    if (maximum > 0.0f)
        for (auto& point : preview)
            point /= maximum;

    auto prepared = std::make_unique<PreparedLoad>();
    prepared->lane = lane;
    prepared->generation = generation;
    prepared->engine = std::make_unique<Engine>();
    prepared->engine->processor.loadImpulseResponse(
        std::move(decoded), reader->sampleRate,
        juce::dsp::Convolution::Stereo::yes,
        juce::dsp::Convolution::Trim::no,
        juce::dsp::Convolution::Normalise::yes);
    prepared->engine->processor.prepare(preparedSpec);
    prepared->name = file.getFileName();
    prepared->preview = preview;
    prepared->lengthSeconds =
        static_cast<double>(croppedSamples) / reader->sampleRate;
    destination = std::move(prepared);
    return juce::Result::ok();
}

bool WormholeConvolutionModule::commitPreparedImpulseResponse(
    int laneIndex, PreparedImpulseResponsePtr& preparedBase)
{
    auto* prepared = dynamic_cast<PreparedLoad*>(preparedBase.get());
    if (prepared == nullptr || prepared->engine == nullptr
        || prepared->lane != laneIndex
        || !juce::isPositiveAndBelow(laneIndex, laneCount))
        return false;
    auto& lane = *lanes[static_cast<size_t>(laneIndex)];
    if (prepared->generation
        != lane.requestedGeneration.load(std::memory_order_acquire))
        return false;
    lane.retiring = std::move(lane.active);
    lane.retiringAvailable =
        lane.available.load(std::memory_order_acquire);
    lane.active = std::move(prepared->engine);
    lane.active->processor.reset();
    lane.fadeSamplesRemaining =
        lane.retiringAvailable ? lane.fadeSamplesTotal : 0;
    {
        const juce::ScopedLock lock(lane.metadataLock);
        lane.name = prepared->name;
        lane.preview = prepared->preview;
    }
    lane.lengthSeconds.store(
        prepared->lengthSeconds, std::memory_order_relaxed);
    lane.committedGeneration.store(
        prepared->generation, std::memory_order_relaxed);
    lane.available.store(true, std::memory_order_release);
    prepared->generation = 0;
    return true;
}

void WormholeConvolutionModule::cancelPreparedImpulseResponse(
    int laneIndex, PreparedImpulseResponsePtr& preparedBase) noexcept
{
    if (auto* prepared = dynamic_cast<PreparedLoad*>(preparedBase.get());
        prepared != nullptr && juce::isPositiveAndBelow(laneIndex, laneCount))
    {
        auto& lane = *lanes[static_cast<size_t>(laneIndex)];
        auto expected = prepared->generation;
        lane.requestedGeneration.compare_exchange_strong(
            expected,
            lane.committedGeneration.load(std::memory_order_acquire),
            std::memory_order_release, std::memory_order_relaxed);
    }
    preparedBase.reset();
}

void WormholeConvolutionModule::clearImpulseResponse(int laneIndex)
{
    if (!juce::isPositiveAndBelow(laneIndex, laneCount))
        return;
    auto& lane = *lanes[static_cast<size_t>(laneIndex)];
    const auto generation = beginRequest(laneIndex);
    lane.available.store(false, std::memory_order_release);
    lane.committedGeneration.store(generation, std::memory_order_release);
    lane.lengthSeconds.store(0.0, std::memory_order_relaxed);
    const juce::ScopedLock lock(lane.metadataLock);
    lane.name.clear();
    lane.preview.fill(0.0f);
}

bool WormholeConvolutionModule::laneAvailable(int laneIndex) const noexcept
{
    const auto& lane = *lanes[static_cast<size_t>(laneIndex)];
    return lane.available.load(std::memory_order_acquire);
}

juce::String WormholeConvolutionModule::impulseResponseName(
    int laneIndex) const
{
    if (!juce::isPositiveAndBelow(laneIndex, laneCount)
        || !laneAvailable(laneIndex))
        return {};
    const auto& lane = *lanes[static_cast<size_t>(laneIndex)];
    const juce::ScopedLock lock(lane.metadataLock);
    return lane.name;
}

ImpulseResponsePreview WormholeConvolutionModule::impulseResponsePreview(
    int laneIndex) const
{
    if (!juce::isPositiveAndBelow(laneIndex, laneCount)
        || !laneAvailable(laneIndex))
        return {};
    const auto& lane = *lanes[static_cast<size_t>(laneIndex)];
    const juce::ScopedLock lock(lane.metadataLock);
    return lane.preview;
}

juce::Result WormholeConvolutionModule::loadImpulseResponse(
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

juce::Result WormholeConvolutionModule::prepareImpulseResponse(
    const juce::File& file, PreparedImpulseResponsePtr& prepared)
{
    return prepareImpulseResponse(0, file, {}, prepared);
}

bool WormholeConvolutionModule::commitPreparedImpulseResponse(
    PreparedImpulseResponsePtr& prepared)
{
    return commitPreparedImpulseResponse(0, prepared);
}

void WormholeConvolutionModule::cancelPreparedImpulseResponse(
    PreparedImpulseResponsePtr& prepared) noexcept
{
    cancelPreparedImpulseResponse(0, prepared);
}

void WormholeConvolutionModule::clearImpulseResponse()
{
    clearImpulseResponse(0);
}

bool WormholeConvolutionModule::hasImpulseResponse() const noexcept
{
    return laneAvailable(0) || laneAvailable(1);
}

bool WormholeConvolutionModule::isImpulseResponseReady() const noexcept
{
    return hasImpulseResponse();
}

juce::String WormholeConvolutionModule::impulseResponseName() const
{
    return impulseResponseName(0);
}

ImpulseResponsePreview
WormholeConvolutionModule::impulseResponsePreview() const
{
    return impulseResponsePreview(0);
}

double WormholeConvolutionModule::currentImpulseResponseTailSeconds()
    const noexcept
{
    return juce::jmax(
        lanes[0]->lengthSeconds.load(std::memory_order_relaxed),
        lanes[1]->lengthSeconds.load(std::memory_order_relaxed));
}
} // namespace megadsp
