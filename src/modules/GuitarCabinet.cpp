#include "GuitarCabinet.h"
#include "DspHelpers.h"
#include "DspSafety.h"

#include <BinaryData.h>

#include <cmath>
#include <limits>
#include <numeric>

namespace megadsp
{
using detail::convolutionMessageQueue;
using detail::exponential;
using detail::lerp;

namespace
{
constexpr float cabinetPinkReference = 0.25f;
constexpr float maximumCalibrationDb = 6.0f;
constexpr int maximumCalibrationFftSize = 1 << 19;
constexpr double calibrationLowFrequency = 70.0;
constexpr double calibrationHighFrequency = 9000.0;

float cabinetCalibrationGain(
    const juce::AudioBuffer<float>& impulse, double sampleRate)
{
    auto maximumChannelEnergy = 0.0;
    for (int channel = 0; channel < impulse.getNumChannels(); ++channel)
    {
        const auto* samples = impulse.getReadPointer(channel);
        auto energy = 0.0;
        for (int sample = 0; sample < impulse.getNumSamples(); ++sample)
            energy += static_cast<double>(samples[sample]) * samples[sample];
        maximumChannelEnergy = juce::jmax(maximumChannelEnergy, energy);
    }
    if (!(maximumChannelEnergy > 1.0e-12))
        return 1.0f;

    const auto energyNormalisation = static_cast<float>(
        0.125 / std::sqrt(maximumChannelEnergy));
    const auto analysisSamples = juce::jmin(
        impulse.getNumSamples(), maximumCalibrationFftSize);
    auto fftOrder = 1;
    while ((1 << fftOrder) < analysisSamples)
        ++fftOrder;
    const auto fftSize = 1 << fftOrder;
    juce::dsp::FFT fft(fftOrder);
    std::vector<float> fftData(static_cast<size_t>(fftSize * 2), 0.0f);

    auto weightedEnergy = 0.0;
    auto weightSum = 0.0;
    for (int channel = 0; channel < impulse.getNumChannels(); ++channel)
    {
        std::fill(fftData.begin(), fftData.end(), 0.0f);
        std::copy_n(
            impulse.getReadPointer(channel), analysisSamples,
            fftData.begin());
        fft.performFrequencyOnlyForwardTransform(fftData.data());
        for (int bin = 1; bin <= fftSize / 2; ++bin)
        {
            const auto frequency =
                static_cast<double>(bin) * sampleRate / fftSize;
            if (frequency < calibrationLowFrequency
                || frequency > juce::jmin(
                    calibrationHighFrequency, sampleRate * 0.45))
                continue;
            const auto weight = 1.0 / frequency;
            const auto magnitude = static_cast<double>(
                fftData[static_cast<size_t>(bin)]);
            weightedEnergy += magnitude * magnitude * weight;
            weightSum += weight;
        }
    }
    if (!(weightedEnergy > 1.0e-12) || !(weightSum > 0.0))
        return energyNormalisation;

    const auto weightedResponse =
        std::sqrt(weightedEnergy / weightSum);
    const auto pinkNormalisation = static_cast<float>(
        cabinetPinkReference / weightedResponse);
    const auto maximumCorrection =
        juce::Decibels::decibelsToGain(maximumCalibrationDb);
    return energyNormalisation * juce::jlimit(
        1.0f / maximumCorrection, maximumCorrection,
        pinkNormalisation / energyNormalisation);
}
} // namespace

struct GuitarCabinetModule::Engine
{
    Engine()
        : processor(
              juce::dsp::Convolution::NonUniform { 256 },
              convolutionMessageQueue())
    {
    }

    juce::dsp::Convolution processor;
};

struct GuitarCabinetModule::PreparedLoad final
    : public PreparedImpulseResponse
{
    std::uint64_t generation = 0;
    std::unique_ptr<Engine> engine;
    juce::String name;
    ImpulseResponsePreview preview {};
    double lengthSeconds = 0.0;
};

GuitarCabinetModule::GuitarCabinetModule()
    : convolution(std::make_unique<Engine>())
{
}

GuitarCabinetModule::~GuitarCabinetModule() = default;

void GuitarCabinetModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    preparedSpec = spec;
    sampleRate = detail::safeSampleRate(spec.sampleRate);
    convolution->processor.prepare(spec);
    lowCut.prepare(spec);
    highCut.prepare(spec);
    lowCut.setType(juce::dsp::StateVariableTPTFilterType::highpass);
    highCut.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    wetBuffer.setSize(
        static_cast<int>(spec.numChannels),
        static_cast<int>(spec.maximumBlockSize), false, true, true);
    mixSmoothed.reset(sampleRate, 0.025);
    outputSmoothed.reset(sampleRate, 0.025);
    driveSmoothed.reset(sampleRate, 0.025);
    bodySmoothed.reset(sampleRate, 0.025);
    presenceSmoothed.reset(sampleRate, 0.025);
    mixSmoothed.setCurrentAndTargetValue(1.0f);
    outputSmoothed.setCurrentAndTargetValue(1.0f);
    driveSmoothed.setCurrentAndTargetValue(1.0f);
    bodySmoothed.setCurrentAndTargetValue(0.0f);
    presenceSmoothed.setCurrentAndTargetValue(0.0f);
    reset();

    PreparedImpulseResponsePtr prepared;
    if (prepareBuiltInImpulseResponse(
            defaultBuiltInIndex, prepared).wasOk())
        commitPreparedImpulseResponse(prepared);
}

void GuitarCabinetModule::reset()
{
    convolution->processor.reset();
    lowCut.reset();
    highCut.reset();
    wetBuffer.clear();
    bodyState = {};
    presenceState = {};
    telemetryState = {};
    telemetry.clear();
}

bool GuitarCabinetModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}

void GuitarCabinetModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    const auto samples = buffer.getNumSamples();
    if (samples <= 0)
        return;

    jassert(buffer.getNumChannels() <= wetBuffer.getNumChannels());
    jassert(samples <= wetBuffer.getNumSamples());
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        wetBuffer.copyFrom(channel, 0, buffer, channel, 0, samples);

    driveSmoothed.setTargetValue(juce::Decibels::decibelsToGain(
        lerp(0.0f, 18.0f, controls[5])));
    bodySmoothed.setTargetValue(lerp(-6.0f, 6.0f, controls[3]));
    presenceSmoothed.setTargetValue(lerp(-6.0f, 6.0f, controls[4]));
    mixSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, controls[6]));
    outputSmoothed.setTargetValue(juce::Decibels::decibelsToGain(
        lerp(-18.0f, 12.0f, controls[7])));

    double inputEnergy = 0.0;
    double wetEnergy = 0.0;
    double outputEnergy = 0.0;
    double nonlinearEnergy = 0.0;
    for (int sample = 0; sample < samples; ++sample)
    {
        const auto drive = driveSmoothed.getNextValue();
        const auto driveNormalisation = 1.0f / std::tanh(drive);
        const auto driveAmount = juce::jlimit(
            0.0f, 1.0f,
            (drive - 1.0f)
                / (juce::Decibels::decibelsToGain(18.0f) - 1.0f));
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto input = wetBuffer.getSample(channel, sample);
            const auto saturated =
                std::tanh(input * drive) * driveNormalisation;
            const auto driven = lerp(input, saturated, driveAmount);
            wetBuffer.setSample(channel, sample, driven);
            inputEnergy += static_cast<double>(input) * input;
            const auto difference = driven - input;
            nonlinearEnergy += static_cast<double>(difference) * difference;
        }
    }

    if (hasImpulseResponse())
    {
        juce::dsp::AudioBlock<float> fullWetBlock(wetBuffer);
        auto wetBlock = fullWetBlock.getSubBlock(
            0, static_cast<size_t>(samples));
        juce::dsp::ProcessContextReplacing<float> wetContext(wetBlock);
        convolution->processor.process(wetContext);
        lowCut.setCutoffFrequency(exponential(30.0f, 300.0f, controls[1]));
        highCut.setCutoffFrequency(juce::jmin(
            static_cast<float>(sampleRate * 0.45),
            exponential(2500.0f, 18000.0f, controls[2])));
        lowCut.process(wetContext);
        highCut.process(wetContext);
    }
    else
        wetBuffer.clear();

    const auto bodyCoefficient = 1.0f - std::exp(
        -juce::MathConstants<float>::twoPi * 180.0f
        / static_cast<float>(sampleRate));
    const auto presenceCoefficient = 1.0f - std::exp(
        -juce::MathConstants<float>::twoPi * 2200.0f
        / static_cast<float>(sampleRate));
    for (int sample = 0; sample < samples; ++sample)
    {
        const auto bodyGain =
            juce::Decibels::decibelsToGain(bodySmoothed.getNextValue()) - 1.0f;
        const auto presenceGain =
            juce::Decibels::decibelsToGain(
                presenceSmoothed.getNextValue()) - 1.0f;
        const auto mix = mixSmoothed.getNextValue();
        const auto output = outputSmoothed.getNextValue();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto index = static_cast<size_t>(juce::jmin(channel, 1));
            auto wet = wetBuffer.getSample(channel, sample);
            bodyState[index] += bodyCoefficient * (wet - bodyState[index]);
            presenceState[index] +=
                presenceCoefficient * (wet - presenceState[index]);
            wet += bodyGain * bodyState[index]
                + presenceGain * (wet - presenceState[index]);
            wetEnergy += static_cast<double>(wet) * wet;
            const auto result =
                lerp(buffer.getSample(channel, sample), wet, mix) * output;
            buffer.setSample(channel, sample, result);
            outputEnergy += static_cast<double>(result) * result;
        }
    }

    if (environment.captureTelemetry)
    {
        const auto divisor = static_cast<double>(
            juce::jmax(1, samples * buffer.getNumChannels()));
        telemetryState.sequence += 1;
        telemetryState.valueCount = telemetryValueCount;
        telemetryState.values[inputLevel] =
            static_cast<float>(std::sqrt(inputEnergy / divisor));
        telemetryState.values[wetLevel] =
            static_cast<float>(std::sqrt(wetEnergy / divisor));
        telemetryState.values[outputLevel] =
            static_cast<float>(std::sqrt(outputEnergy / divisor));
        telemetryState.values[driveActivity] = juce::jlimit(
            0.0f, 1.0f,
            static_cast<float>(std::sqrt(nonlinearEnergy / divisor) * 4.0));
        telemetry.publish(telemetryState);
    }
}

double GuitarCabinetModule::tailSeconds(const ControlValues&) const
{
    return currentImpulseResponseTailSeconds();
}

std::uint64_t GuitarCabinetModule::beginImpulseResponseRequest() noexcept
{
    const auto generation =
        requestCounter.fetch_add(1, std::memory_order_relaxed) + 1;
    requestedGeneration.store(generation, std::memory_order_release);
    return generation;
}

juce::Result GuitarCabinetModule::prepareDecodedImpulse(
    std::unique_ptr<juce::AudioFormatReader> reader, const juce::String& name,
    PreparedImpulseResponsePtr& destination, std::uint64_t generation)
{
    destination.reset();
    if (reader == nullptr || reader->lengthInSamples <= 0
        || reader->sampleRate <= 0.0
        || reader->lengthInSamples
               > static_cast<juce::int64>(std::numeric_limits<int>::max()))
        return juce::Result::fail(
            "Unsupported, empty, or excessively long cabinet impulse.");

    const auto sampleCount = static_cast<int>(reader->lengthInSamples);
    const auto channels = juce::jlimit(
        1, 2, static_cast<int>(reader->numChannels));
    juce::AudioBuffer<float> decoded(channels, sampleCount);
    if (!reader->read(
            &decoded, 0, sampleCount, 0, true, channels > 1))
        return juce::Result::fail("Could not decode the cabinet impulse.");

    ImpulseResponsePreview newPreview {};
    float maximum = 0.0f;
    for (int point = 0; point < impulseResponsePreviewSize; ++point)
    {
        const auto start = static_cast<int>(
            static_cast<juce::int64>(point) * sampleCount
            / impulseResponsePreviewSize);
        const auto end = static_cast<int>(
            static_cast<juce::int64>(point + 1) * sampleCount
            / impulseResponsePreviewSize);
        float peak = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
            peak = juce::jmax(
                peak, decoded.getMagnitude(
                          channel, juce::jmin(start, sampleCount - 1),
                          juce::jmax(1, juce::jmin(
                              end - start, sampleCount - start))));
        newPreview[static_cast<size_t>(point)] = peak;
        maximum = juce::jmax(maximum, peak);
    }
    if (maximum > 0.0f)
        for (auto& point : newPreview)
            point /= maximum;

    decoded.applyGain(cabinetCalibrationGain(decoded, reader->sampleRate));

    auto prepared = std::make_unique<PreparedLoad>();
    prepared->generation = generation;
    prepared->engine = std::make_unique<Engine>();
    prepared->engine->processor.loadImpulseResponse(
        std::move(decoded), reader->sampleRate,
        juce::dsp::Convolution::Stereo::yes,
        juce::dsp::Convolution::Trim::yes,
        juce::dsp::Convolution::Normalise::no);
    prepared->engine->processor.prepare(preparedSpec);
    prepared->name = name;
    prepared->preview = newPreview;
    prepared->lengthSeconds =
        static_cast<double>(sampleCount) / reader->sampleRate;
    destination = std::move(prepared);
    return juce::Result::ok();
}

juce::Result GuitarCabinetModule::prepareImpulseResponse(
    const juce::File& file, PreparedImpulseResponsePtr& destination)
{
    const auto generation = beginImpulseResponseRequest();
    if (!file.existsAsFile())
        return juce::Result::fail(
            "Cabinet impulse response file does not exist.");
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    return prepareDecodedImpulse(
        std::unique_ptr<juce::AudioFormatReader>(
            formats.createReaderFor(file)),
        file.getFileName(), destination, generation);
}

juce::Result GuitarCabinetModule::prepareBuiltInImpulseResponse(
    int index, PreparedImpulseResponsePtr& destination)
{
    if (!juce::isPositiveAndBelow(index, builtInCount)
        || index >= CabinetIRData::namedResourceListSize)
        return juce::Result::fail("Invalid built-in cabinet selection.");
    const auto generation = beginImpulseResponseRequest();
    const auto* resourceName = CabinetIRData::namedResourceList[index];
    int dataSize = 0;
    const auto* data =
        CabinetIRData::getNamedResource(resourceName, dataSize);
    if (data == nullptr || dataSize <= 0)
        return juce::Result::fail(
            "The built-in cabinet data is unavailable.");

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto stream = std::make_unique<juce::MemoryInputStream>(
        data, static_cast<size_t>(dataSize), false);
    return prepareDecodedImpulse(
        std::unique_ptr<juce::AudioFormatReader>(
            formats.createReaderFor(std::move(stream))),
        builtInImpulseResponseName(index), destination, generation);
}

juce::Result GuitarCabinetModule::loadImpulseResponse(const juce::File& file)
{
    PreparedImpulseResponsePtr prepared;
    auto result = prepareImpulseResponse(file, prepared);
    if (result.failed())
        return result;
    if (!commitPreparedImpulseResponse(prepared))
        return juce::Result::fail(
            "The cabinet impulse request was superseded.");
    return juce::Result::ok();
}

bool GuitarCabinetModule::commitPreparedImpulseResponse(
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
    bodyState = {};
    presenceState = {};
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

void GuitarCabinetModule::cancelPreparedImpulseResponse(
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

void GuitarCabinetModule::clearImpulseResponse()
{
    const auto generation = beginImpulseResponseRequest();
    committedImpulseAvailable.store(false, std::memory_order_release);
    committedGeneration.store(generation, std::memory_order_release);
    impulseLengthSeconds.store(0.0, std::memory_order_relaxed);
    const juce::ScopedLock lock(metadataLock);
    loadedImpulseName.clear();
    preview.fill(0.0f);
}

bool GuitarCabinetModule::hasImpulseResponse() const noexcept
{
    return committedImpulseAvailable.load(std::memory_order_acquire)
        && committedGeneration.load(std::memory_order_acquire)
               == requestedGeneration.load(std::memory_order_acquire);
}

juce::String GuitarCabinetModule::impulseResponseName() const
{
    if (!hasImpulseResponse())
        return {};
    const juce::ScopedLock lock(metadataLock);
    return loadedImpulseName;
}

ImpulseResponsePreview GuitarCabinetModule::impulseResponsePreview() const
{
    if (!hasImpulseResponse())
        return {};
    const juce::ScopedLock lock(metadataLock);
    return preview;
}

double GuitarCabinetModule::currentImpulseResponseTailSeconds() const noexcept
{
    return hasImpulseResponse()
        ? impulseLengthSeconds.load(std::memory_order_relaxed)
        : 0.0;
}

juce::String GuitarCabinetModule::builtInImpulseResponseName(int index) const
{
    if (!juce::isPositiveAndBelow(index, builtInCount)
        || index >= CabinetIRData::namedResourceListSize)
        return {};
    return juce::File(
        CabinetIRData::getNamedResourceOriginalFilename(
            CabinetIRData::namedResourceList[index]))
        .getFileNameWithoutExtension();
}
} // namespace megadsp
