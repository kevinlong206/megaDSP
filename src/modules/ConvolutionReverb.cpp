#include "ConvolutionReverb.h"
#include "DspHelpers.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <limits>

namespace megadsp
{
using detail::convolutionMessageQueue;
using detail::coefficient;
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
    bool isMono = false;
};

struct ConvolutionReverbModule::Bank
{
    std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    juce::String name;
    ImpulseResponsePreview preview {};
    std::atomic<double> lengthSeconds { 0.0 };
    std::atomic<bool> isMono { false };
    std::atomic<bool> available { false };
};

ConvolutionReverbModule::ConvolutionReverbModule()
{
    for (auto& bank : banks)
        bank = std::make_unique<Bank>();
}

ConvolutionReverbModule::~ConvolutionReverbModule() = default;

void ConvolutionReverbModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    preparedSpec = spec;
    sampleRate = spec.sampleRate;
    for (auto& bank : banks)
        bank->engine->processor.prepare(spec);
    lowCut.prepare(spec);
    highCut.prepare(spec);
    lowCut.setType(juce::dsp::StateVariableTPTFilterType::highpass);
    highCut.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    wetBuffer.setSize(static_cast<int>(spec.numChannels),
                      static_cast<int>(spec.maximumBlockSize), false, true, true);
    wetSmoothed.reset(sampleRate, 0.02);
    drySmoothed.reset(sampleRate, 0.02);
    duckAmountSmoothed.reset(sampleRate, 0.03);
    outputSmoothed.reset(sampleRate, 0.02);
    wetSmoothed.setCurrentAndTargetValue(0.10f);
    drySmoothed.setCurrentAndTargetValue(1.0f);
    duckAmountSmoothed.setCurrentAndTargetValue(0.0f);
    outputSmoothed.setCurrentAndTargetValue(1.0f);
    telemetryInterval = juce::jmax(1, juce::roundToInt(sampleRate / 30.0));
    for (size_t cutoff = 0;
         cutoff < telemetrySpectrumCoefficients.size(); ++cutoff)
    {
        const auto proportion = static_cast<float>(cutoff)
            / static_cast<float>(
                telemetrySpectrumCoefficients.size() - 1);
        const auto frequency = 80.0f
            * std::pow(12000.0f / 80.0f, proportion);
        telemetrySpectrumCoefficients[cutoff] = 1.0f - std::exp(
            -juce::MathConstants<float>::twoPi
            * juce::jmin(
                  frequency, static_cast<float>(sampleRate * 0.45))
            / static_cast<float>(sampleRate));
    }
    reset();
}

void ConvolutionReverbModule::reset()
{
    for (auto& bank : banks)
        bank->engine->processor.reset();
    lowCut.reset();
    highCut.reset();
    wetBuffer.clear();
    duckEnvelope = 0.0f;
    duckGainDb = 0.0f;
    telemetryState = {};
    telemetry.clear();
    telemetryEnergy = {};
    telemetrySpectrumEnergy = {};
    telemetrySpectrumLowpass = {};
    telemetrySampleCount = 0;
    telemetryCountdown = telemetryInterval;
    telemetryWasCapturing = false;
}

bool ConvolutionReverbModule::readContinuousTelemetry(
    ContinuousTelemetrySnapshot& snapshot) const noexcept
{
    return telemetry.read(snapshot);
}

void ConvolutionReverbModule::process(
    juce::AudioBuffer<float>& buffer, const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    if (buffer.getNumSamples() <= 0)
        return;

    if (environment.captureTelemetry && !telemetryWasCapturing)
    {
        telemetryState = {};
        telemetryEnergy = {};
        telemetrySpectrumEnergy = {};
        telemetrySpectrumLowpass = {};
        telemetrySampleCount = 0;
        telemetryCountdown = telemetryInterval;
    }
    telemetryWasCapturing = environment.captureTelemetry;

    jassert(buffer.getNumChannels() <= wetBuffer.getNumChannels());
    jassert(buffer.getNumSamples() <= wetBuffer.getNumSamples());
    const auto impulseAvailable = hasCurrentImpulseResponse();
    auto wetAvailable = false;
    if (impulseAvailable)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            wetBuffer.copyFrom(channel, 0, buffer, channel, 0,
                               buffer.getNumSamples());
        if (buffer.getNumChannels() == 2
            && banks[static_cast<size_t>(
                activeBank.load(std::memory_order_relaxed))]
                   ->isMono.load(std::memory_order_relaxed))
        {
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                const auto mono = 0.5f
                    * (wetBuffer.getSample(0, sample)
                       + wetBuffer.getSample(1, sample));
                wetBuffer.setSample(0, sample, mono);
                wetBuffer.setSample(1, sample, mono);
            }
        }

        juce::dsp::AudioBlock<float> preparedWetBlock(wetBuffer);
        auto wetBlock = preparedWetBlock.getSubBlock(
            0, static_cast<size_t>(buffer.getNumSamples()));
        juce::dsp::ProcessContextReplacing<float> wetContext(wetBlock);
        banks[static_cast<size_t>(
            activeBank.load(std::memory_order_acquire))]
            ->engine->processor.process(wetContext);
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
    duckAmountSmoothed.setTargetValue(
        lerp(0.0f, 18.0f, controls[5]));
    outputSmoothed.setTargetValue(juce::Decibels::decibelsToGain(
        lerp(-18.0f, 18.0f, controls[3])));
    const auto detectorAttack = coefficient(sampleRate, 5.0f);
    const auto detectorRelease = coefficient(sampleRate, 180.0f);
    const auto gainAttack = coefficient(sampleRate, 8.0f);
    const auto gainRelease = coefficient(sampleRate, 350.0f);
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto wetGain = wetSmoothed.getNextValue();
        const auto dryGain = drySmoothed.getNextValue();
        const auto duckAmountDb = duckAmountSmoothed.getNextValue();
        const auto output = outputSmoothed.getNextValue();
        auto linkedInput = std::abs(buffer.getSample(0, sample));
        if (buffer.getNumChannels() > 1)
            linkedInput = juce::jmax(
                linkedInput, std::abs(buffer.getSample(1, sample)));
        const auto detectorCoefficient =
            linkedInput > duckEnvelope ? detectorAttack : detectorRelease;
        duckEnvelope = detectorCoefficient * duckEnvelope
                       + (1.0f - detectorCoefficient) * linkedInput;
        auto activity = juce::jlimit(
            0.0f, 1.0f,
            (juce::Decibels::gainToDecibels(
                 duckEnvelope, -100.0f)
             + 48.0f)
                / 36.0f);
        activity = activity * activity * (3.0f - 2.0f * activity);
        const auto targetDuckGainDb = -duckAmountDb * activity;
        const auto duckCoefficient =
            targetDuckGainDb < duckGainDb ? gainAttack : gainRelease;
        duckGainDb = duckCoefficient * duckGainDb
                     + (1.0f - duckCoefficient) * targetDuckGainDb;
        const auto duckGain =
            juce::Decibels::decibelsToGain(duckGainDb);
        std::array<float, 2> telemetryWet {};
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto wet = wetAvailable
                ? wetBuffer.getSample(channel, sample)
                      * duckGain
                : 0.0f;
            telemetryWet[static_cast<size_t>(juce::jmin(channel, 1))] = wet;
            buffer.setSample(
                channel, sample,
                (buffer.getSample(channel, sample) * dryGain
                 + wet * wetGain)
                    * output);
        }

        if (environment.captureTelemetry)
        {
            if (buffer.getNumChannels() == 1)
                telemetryWet[1] = telemetryWet[0];
            for (size_t channel = 0; channel < telemetryEnergy.size(); ++channel)
            {
                telemetryEnergy[channel] +=
                    static_cast<double>(telemetryWet[channel])
                    * telemetryWet[channel];
                auto previousLowpass = 0.0f;
                for (size_t cutoff = 0;
                     cutoff < telemetrySpectrumCoefficients.size(); ++cutoff)
                {
                    auto& lowpass =
                        telemetrySpectrumLowpass[channel][cutoff];
                    lowpass += telemetrySpectrumCoefficients[cutoff]
                        * (telemetryWet[channel] - lowpass);
                    const auto band = lowpass - previousLowpass;
                    telemetrySpectrumEnergy[channel][cutoff] +=
                        static_cast<double>(band) * band;
                    previousLowpass = lowpass;
                }
                const auto highBand =
                    telemetryWet[channel] - previousLowpass;
                telemetrySpectrumEnergy[channel].back() +=
                    static_cast<double>(highBand) * highBand;
            }
            ++telemetrySampleCount;
            if (--telemetryCountdown <= 0)
            {
                const auto divisor =
                    static_cast<double>(juce::jmax(1, telemetrySampleCount));
                std::array<float, continuousTelemetryHistoryValueCapacity>
                    levels {};
                for (size_t channel = 0; channel < telemetryEnergy.size();
                     ++channel)
                {
                    levels[channel] = static_cast<float>(
                        std::sqrt(telemetryEnergy[channel] / divisor));
                }
                std::array<double, 2> totalSpectrumEnergy {};
                std::array<double, 2> highSpectrumEnergy {};
                for (size_t band = 0; band < spectrumBandCount; ++band)
                {
                    auto combinedEnergy = 0.0;
                    for (size_t channel = 0;
                         channel < telemetrySpectrumEnergy.size(); ++channel)
                    {
                        combinedEnergy +=
                            telemetrySpectrumEnergy[channel][band];
                        totalSpectrumEnergy[channel] +=
                            telemetrySpectrumEnergy[channel][band];
                        if (band >= spectrumBandCount * 2 / 3)
                            highSpectrumEnergy[channel] +=
                                telemetrySpectrumEnergy[channel][band];
                    }
                    const auto bandRms = static_cast<float>(std::sqrt(
                        combinedEnergy
                        / (divisor * static_cast<double>(
                              telemetryEnergy.size()))));
                    telemetryState.values[
                        spectrumBandStart + band] = bandRms;
                }
                for (size_t channel = 0;
                     channel < totalSpectrumEnergy.size(); ++channel)
                    levels[channel + 2] =
                        totalSpectrumEnergy[channel] > 1.0e-12
                            ? juce::jlimit(
                                  0.0f, 1.0f,
                                  static_cast<float>(std::sqrt(
                                      highSpectrumEnergy[channel]
                                      / totalSpectrumEnergy[channel])))
                            : 0.0f;
                appendContinuousTelemetryHistory(
                    telemetryState, levels, telemetryHistoryCount);
                telemetryState.values[leftWetLevel] = levels[0];
                telemetryState.values[rightWetLevel] = levels[1];
                telemetryEnergy = {};
                telemetrySpectrumEnergy = {};
                telemetrySampleCount = 0;
                telemetryCountdown = telemetryInterval;
            }
        }
    }
    if (environment.captureTelemetry)
    {
        telemetryState.sequence += 1;
        telemetryState.valueCount = telemetryValueCount;
        telemetryState.values[outputChannelCount] =
            static_cast<float>(buffer.getNumChannels());
        telemetry.publish(telemetryState);
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
    const auto bank = activeBank.load(std::memory_order_acquire);
    return juce::isPositiveAndBelow(bank, impulseBankCount)
        && banks[static_cast<size_t>(bank)]->available.load(
            std::memory_order_acquire)
        && !activeReplacementPending.load(std::memory_order_acquire);
}

double ConvolutionReverbModule::currentImpulseResponseTailSeconds()
    const noexcept
{
    const auto bank = activeBank.load(std::memory_order_acquire);
    return hasCurrentImpulseResponse()
        ? banks[static_cast<size_t>(bank)]->lengthSeconds.load(
              std::memory_order_relaxed)
        : 0.0;
}

juce::Result ConvolutionReverbModule::prepareImpulseResponse(
    const juce::File& file, PreparedImpulseResponsePtr& destination)
{
    return prepareImpulseResponseInternal(file, destination, true);
}

juce::Result ConvolutionReverbModule::prepareImpulseResponseForBank(
    const juce::File& file, PreparedImpulseResponsePtr& destination)
{
    return prepareImpulseResponseInternal(file, destination, false);
}

juce::Result ConvolutionReverbModule::prepareImpulseResponseInternal(
    const juce::File& file, PreparedImpulseResponsePtr& destination,
    bool markActivePending)
{
    destination.reset();
    const auto generation = beginImpulseResponseRequest();
    activeReplacementPending.store(
        markActivePending, std::memory_order_release);
    const auto fail = [this](const juce::String& message)
    {
        activeReplacementPending.store(false, std::memory_order_release);
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
    prepared->isMono = channelCount == 1;
    destination = std::move(prepared);
    return juce::Result::ok();
}

bool ConvolutionReverbModule::commitPreparedImpulseResponse(
    PreparedImpulseResponsePtr& preparedBase)
{
    return commitPreparedImpulseResponseToBank(
        activeBank.load(std::memory_order_acquire), preparedBase);
}

bool ConvolutionReverbModule::commitPreparedImpulseResponseToBank(
    int bankIndex, PreparedImpulseResponsePtr& preparedBase)
{
    auto* prepared = dynamic_cast<PreparedLoad*>(preparedBase.get());
    if (!juce::isPositiveAndBelow(bankIndex, impulseBankCount)
        || prepared == nullptr || prepared->engine == nullptr
        || prepared->generation
               != requestedGeneration.load(std::memory_order_acquire))
        return false;

    auto& bank = *banks[static_cast<size_t>(bankIndex)];
    bank.engine = std::move(prepared->engine);
    bank.engine->processor.reset();
    {
        const juce::ScopedLock lock(metadataLock);
        bank.name = prepared->name;
        bank.preview = prepared->preview;
        bank.lengthSeconds.store(
            prepared->lengthSeconds, std::memory_order_relaxed);
        bank.isMono.store(prepared->isMono, std::memory_order_relaxed);
        bank.available.store(true, std::memory_order_release);
    }
    if (bankIndex == activeBank.load(std::memory_order_acquire))
    {
        lowCut.reset();
        highCut.reset();
        wetBuffer.clear();
    }
    activeReplacementPending.store(false, std::memory_order_release);
    prepared->generation = 0;
    preparedBase.reset();
    return true;
}

bool ConvolutionReverbModule::selectImpulseResponseBank(int bank)
{
    if (!juce::isPositiveAndBelow(bank, impulseBankCount))
        return false;
    activeBank.store(bank, std::memory_order_release);
    banks[static_cast<size_t>(bank)]->engine->processor.reset();
    lowCut.reset();
    highCut.reset();
    wetBuffer.clear();
    return true;
}

void ConvolutionReverbModule::clearAllImpulseResponseBanks()
{
    beginImpulseResponseRequest();
    activeReplacementPending.store(false, std::memory_order_release);
    const juce::ScopedLock lock(metadataLock);
    for (auto& bank : banks)
    {
        bank->engine->processor.reset();
        bank->name.clear();
        bank->preview.fill(0.0f);
        bank->lengthSeconds.store(0.0, std::memory_order_relaxed);
        bank->isMono.store(false, std::memory_order_relaxed);
        bank->available.store(false, std::memory_order_release);
    }
    activeBank.store(0, std::memory_order_release);
    lowCut.reset();
    highCut.reset();
    wetBuffer.clear();
}

void ConvolutionReverbModule::cancelPreparedImpulseResponse(
    PreparedImpulseResponsePtr& preparedBase) noexcept
{
    if (auto* prepared = dynamic_cast<PreparedLoad*>(preparedBase.get()))
    {
        auto expected = prepared->generation;
        requestedGeneration.compare_exchange_strong(
            expected,
            0,
            std::memory_order_release, std::memory_order_relaxed);
    }
    activeReplacementPending.store(false, std::memory_order_release);
    preparedBase.reset();
}

void ConvolutionReverbModule::clearImpulseResponse()
{
    beginImpulseResponseRequest();
    activeReplacementPending.store(false, std::memory_order_release);
    auto& bank = *banks[static_cast<size_t>(
        activeBank.load(std::memory_order_acquire))];
    {
        const juce::ScopedLock lock(metadataLock);
        bank.name.clear();
        bank.preview.fill(0.0f);
        bank.lengthSeconds.store(0.0, std::memory_order_relaxed);
        bank.isMono.store(false, std::memory_order_relaxed);
        bank.available.store(false, std::memory_order_release);
    }
}

juce::String ConvolutionReverbModule::impulseResponseName() const
{
    if (!hasCurrentImpulseResponse())
        return {};
    const juce::ScopedLock lock(metadataLock);
    return banks[static_cast<size_t>(
        activeBank.load(std::memory_order_acquire))]->name;
}

ImpulseResponsePreview
ConvolutionReverbModule::impulseResponsePreview() const
{
    if (!hasCurrentImpulseResponse())
        return {};
    const juce::ScopedLock lock(metadataLock);
    return banks[static_cast<size_t>(
        activeBank.load(std::memory_order_acquire))]->preview;
}

juce::String ConvolutionReverbModule::impulseResponseBankName(
    int bank) const
{
    if (!juce::isPositiveAndBelow(bank, impulseBankCount))
        return {};
    const juce::ScopedLock lock(metadataLock);
    return banks[static_cast<size_t>(bank)]->name;
}
} // namespace megadsp
