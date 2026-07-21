#include "ConvolutionReverb.h"
#include "DspHelpers.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <limits>

namespace megadsp
{
using detail::convolutionMessageQueue;
using detail::exponential;
using detail::lerp;

struct ConvolutionReverbModule::Engine
{
    Engine()
        : processor(
              juce::dsp::Convolution::NonUniform { 256 },
              convolutionMessageQueue())
    {
    }

    juce::dsp::Convolution processor;
};

struct ConvolutionReverbModule::PreparedLoad final
    : public PreparedImpulseResponse
{
    std::uint64_t generation = 0;
    std::unique_ptr<Engine> engine;
    juce::String name;
    ImpulseResponsePreview preview {};
    double lengthSeconds = 0.0;
};

ConvolutionReverbModule::ConvolutionReverbModule()
    : convolution(std::make_unique<Engine>())
{
}

ConvolutionReverbModule::~ConvolutionReverbModule() = default;

void ConvolutionReverbModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    preparedSpec = spec;
    sampleRate = spec.sampleRate;
    convolution->processor.prepare(spec);
    lowCut.prepare(spec);
    highCut.prepare(spec);
    lowCut.setType(juce::dsp::StateVariableTPTFilterType::highpass);
    highCut.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    wetBuffer.setSize(static_cast<int>(spec.numChannels),
                      static_cast<int>(spec.maximumBlockSize), false, true, true);
    wetSmoothed.reset(sampleRate, 0.02);
    drySmoothed.reset(sampleRate, 0.02);
    outputSmoothed.reset(sampleRate, 0.02);
    wetSmoothed.setCurrentAndTargetValue(0.10f);
    drySmoothed.setCurrentAndTargetValue(1.0f);
    outputSmoothed.setCurrentAndTargetValue(1.0f);
    reset();
}

void ConvolutionReverbModule::reset()
{
    convolution->processor.reset();
    lowCut.reset();
    highCut.reset();
    wetBuffer.clear();
}

void ConvolutionReverbModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment&)
{
    if (buffer.getNumSamples() <= 0)
        return;

    jassert(buffer.getNumChannels() <= wetBuffer.getNumChannels());
    jassert(buffer.getNumSamples() <= wetBuffer.getNumSamples());
    const auto impulseAvailable = hasCurrentImpulseResponse();
    auto wetAvailable = false;
    if (impulseAvailable)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            wetBuffer.copyFrom(channel, 0, buffer, channel, 0,
                               buffer.getNumSamples());

        juce::dsp::AudioBlock<float> preparedWetBlock(wetBuffer);
        auto wetBlock = preparedWetBlock.getSubBlock(
            0, static_cast<size_t>(buffer.getNumSamples()));
        juce::dsp::ProcessContextReplacing<float> wetContext(wetBlock);
        convolution->processor.process(wetContext);
        wetAvailable = true;
        lowCut.setCutoffFrequency(
            exponential(20.0f, 1000.0f, controls[0]));
        highCut.setCutoffFrequency(juce::jmin(
            static_cast<float>(sampleRate * 0.45),
            exponential(2000.0f, 20000.0f, controls[1])));
        lowCut.process(wetContext);
        highCut.process(wetContext);
    }

    wetSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, controls[2]));
    drySmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, controls[4]));
    outputSmoothed.setTargetValue(juce::Decibels::decibelsToGain(
        lerp(-18.0f, 18.0f, controls[3])));
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto wetGain = wetSmoothed.getNextValue();
        const auto dryGain = drySmoothed.getNextValue();
        const auto output = outputSmoothed.getNextValue();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.setSample(
                channel, sample,
                (buffer.getSample(channel, sample) * dryGain
                 + (wetAvailable
                        ? wetBuffer.getSample(channel, sample) * wetGain
                        : 0.0f))
                    * output);
    }
}

juce::Result ConvolutionReverbModule::loadImpulseResponse(
    const juce::File& file)
{
    PreparedImpulseResponsePtr prepared;
    auto result = prepareImpulseResponse(file, prepared);
    if (result.failed())
        return result;
    if (!commitPreparedImpulseResponse(prepared))
    {
        cancelPreparedImpulseResponse(prepared);
        return juce::Result::fail(
            "The impulse response request was superseded.");
    }
    return juce::Result::ok();
}

std::uint64_t
ConvolutionReverbModule::beginImpulseResponseRequest() noexcept
{
    const auto generation =
        requestCounter.fetch_add(1, std::memory_order_relaxed) + 1;
    requestedGeneration.store(generation, std::memory_order_release);
    return generation;
}

bool ConvolutionReverbModule::hasCurrentImpulseResponse() const noexcept
{
    const auto requested =
        requestedGeneration.load(std::memory_order_acquire);
    return committedImpulseAvailable.load(std::memory_order_acquire)
           && committedGeneration.load(std::memory_order_acquire)
                  == requested;
}

juce::Result ConvolutionReverbModule::prepareImpulseResponse(
    const juce::File& file, PreparedImpulseResponsePtr& destination)
{
    destination.reset();
    const auto generation = beginImpulseResponseRequest();
    const auto fail = [this, generation](const juce::String& message)
    {
        auto expected = generation;
        requestedGeneration.compare_exchange_strong(
            expected,
            committedGeneration.load(std::memory_order_acquire),
            std::memory_order_release, std::memory_order_relaxed);
        return juce::Result::fail(message);
    };

    if (!file.existsAsFile())
        return fail("Impulse response file does not exist.");

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(
        formats.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0
        || reader->sampleRate <= 0.0)
        return fail(
            "Unsupported or empty impulse response file.");
    if (reader->lengthInSamples
        > static_cast<juce::int64>(std::numeric_limits<int>::max()))
        return fail("Impulse response is too long to load safely.");

    const auto sampleCount = static_cast<int>(reader->lengthInSamples);
    const auto channelCount = juce::jlimit(
        1, 2, static_cast<int>(reader->numChannels));
    juce::AudioBuffer<float> decoded(channelCount, sampleCount);
    if (!reader->read(
            &decoded, 0, sampleCount, 0, true, channelCount > 1))
        return fail("Could not decode the impulse response.");

    ImpulseResponsePreview newPreview{};
    float maximum = 0.0f;
    for (int point = 0;
         point < static_cast<int>(newPreview.size()); ++point)
    {
        const auto start = static_cast<int>(
            static_cast<juce::int64>(point) * sampleCount
            / static_cast<int>(newPreview.size()));
        const auto end = static_cast<int>(
            static_cast<juce::int64>(point + 1) * sampleCount
            / static_cast<int>(newPreview.size()));
        const auto count = juce::jmax(1, end - start);
        float peak = 0.0f;
        for (int channel = 0; channel < decoded.getNumChannels(); ++channel)
            peak = juce::jmax(
                peak, decoded.getMagnitude(
                          channel, juce::jmin(start, sampleCount - 1),
                          juce::jmin(count, sampleCount - start)));
        newPreview[static_cast<size_t>(point)] = peak;
        maximum = juce::jmax(maximum, peak);
    }
    if (maximum > 0.0f)
        for (auto& point : newPreview)
            point /= maximum;

    auto prepared = std::make_unique<PreparedLoad>();
    prepared->generation = generation;
    prepared->engine = std::make_unique<Engine>();
    // Preparing an isolated engine after loading installs that IR before handoff.
    prepared->engine->processor.loadImpulseResponse(
        std::move(decoded), reader->sampleRate,
        juce::dsp::Convolution::Stereo::yes,
        juce::dsp::Convolution::Trim::yes,
        juce::dsp::Convolution::Normalise::yes);
    prepared->engine->processor.prepare(preparedSpec);
    prepared->name = file.getFileName();
    prepared->preview = newPreview;
    prepared->lengthSeconds =
        static_cast<double>(sampleCount) / reader->sampleRate;
    destination = std::move(prepared);
    return juce::Result::ok();
}

bool ConvolutionReverbModule::commitPreparedImpulseResponse(
    PreparedImpulseResponsePtr& preparedBase)
{
    auto* prepared = dynamic_cast<PreparedLoad*>(preparedBase.get());
    if (prepared == nullptr || prepared->engine == nullptr
        || prepared->generation
               != requestedGeneration.load(std::memory_order_acquire))
        return false;

    std::swap(convolution, prepared->engine);
    convolution->processor.reset();
    lowCut.reset();
    highCut.reset();
    wetBuffer.clear();
    {
        const juce::ScopedLock lock(metadataLock);
        loadedImpulseName = prepared->name;
        preview = prepared->preview;
    }
    impulseLengthSeconds.store(
        prepared->lengthSeconds, std::memory_order_relaxed);
    committedGeneration.store(
        prepared->generation, std::memory_order_relaxed);
    committedImpulseAvailable.store(true, std::memory_order_release);
    prepared->generation = 0;
    return true;
}

void ConvolutionReverbModule::cancelPreparedImpulseResponse(
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

void ConvolutionReverbModule::clearImpulseResponse()
{
    const auto generation = beginImpulseResponseRequest();
    committedImpulseAvailable.store(false, std::memory_order_release);
    committedGeneration.store(generation, std::memory_order_release);
    impulseLengthSeconds.store(0.0, std::memory_order_relaxed);
    {
        const juce::ScopedLock lock(metadataLock);
        loadedImpulseName.clear();
        preview.fill(0.0f);
    }
}

juce::String ConvolutionReverbModule::impulseResponseName() const
{
    if (!hasCurrentImpulseResponse())
        return {};
    const juce::ScopedLock lock(metadataLock);
    return loadedImpulseName;
}

ImpulseResponsePreview
ConvolutionReverbModule::impulseResponsePreview() const
{
    if (!hasCurrentImpulseResponse())
        return {};
    const juce::ScopedLock lock(metadataLock);
    return preview;
}
} // namespace megadsp
