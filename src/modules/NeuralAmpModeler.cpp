#include "NeuralAmpModeler.h"

#include <NAM/get_dsp.h>
#include <NAM/container.h>
#include <NAM/convnet.h>
#include <NAM/dsp.h>
#include <NAM/linear.h>
#include <NAM/lstm.h>
#include <NAM/model_config.h>
#include <NAM/slimmable.h>
#include <NAM/wavenet/a2_fast.h>
#include <NAM/wavenet/model.h>

// Provide missing iplug compatibility for AudioDSPTools LanczosResampler
namespace iplug { constexpr double PI = 3.14159265358979323846; }
#include <ResamplingContainer/ResamplingContainer.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <mutex>

namespace megadsp
{
namespace
{
constexpr float gainLow = -24.0f;
constexpr float gainHigh = 24.0f;
constexpr float toneLow = -12.0f;
constexpr float toneHigh = 12.0f;
constexpr float maximumSafeOutput = 8.0f;
constexpr double defaultModelRate = 48000.0;

float linMap(float lo, float hi, float n)
{
    return lo + (hi - lo) * juce::jlimit(0.0f, 1.0f, n);
}

juce::String jsonString(
    const nlohmann::json& object, const char* key)
{
    const auto found = object.find(key);
    return found != object.end() && found->is_string()
        ? juce::String(found->get<std::string>())
        : juce::String();
}

double jsonNumber(
    const nlohmann::json& object, const char* key) noexcept
{
    const auto found = object.find(key);
    return found != object.end() && found->is_number()
        ? found->get<double>()
        : std::numeric_limits<double>::quiet_NaN();
}

const nlohmann::json* highestQualityModel(
    const nam::dspData& config, int& submodelCount)
{
    if (config.architecture != "SlimmableContainer"
        && config.architecture != "Container")
        return nullptr;

    const auto found = config.config.find("submodels");
    if (found == config.config.end() || !found->is_array()
        || found->empty())
        return nullptr;

    submodelCount = static_cast<int>(found->size());
    const nlohmann::json* selected = nullptr;
    double highest = -std::numeric_limits<double>::infinity();
    for (const auto& entry : *found)
    {
        const auto maxValue = entry.find("max_value");
        const auto model = entry.find("model");
        if (maxValue == entry.end() || !maxValue->is_number()
            || model == entry.end() || !model->is_object())
            continue;
        const auto value = maxValue->get<double>();
        if (value > highest)
        {
            highest = value;
            selected = &*model;
        }
    }
    return selected;
}

bool isA2Config(const nam::dspData& config)
{
#if defined(NAM_ENABLE_A2_FAST)
    if (config.architecture == "WaveNet")
    {
        int channels = 0;
        return nam::wavenet::a2_fast::is_a2_shape(
            config.config, &channels);
    }

    int ignored = 1;
    if (const auto* model = highestQualityModel(config, ignored))
    {
        if (model->value("architecture", std::string()) != "WaveNet")
            return false;
        const auto modelConfig = model->find("config");
        if (modelConfig == model->end() || !modelConfig->is_object())
            return false;
        int channels = 0;
        return nam::wavenet::a2_fast::is_a2_shape(
            *modelConfig, &channels);
    }
#else
    juce::ignoreUnused(config);
#endif
    return false;
}

juce::String captureDescription(const nlohmann::json& metadata)
{
    juce::StringArray parts;
    auto gear = jsonString(metadata, "gear_make");
    const auto model = jsonString(metadata, "gear_model");
    if (model.isNotEmpty())
        gear += (gear.isNotEmpty() ? " " : "") + model;
    if (gear.isEmpty())
        gear = jsonString(metadata, "gear_type");
    if (gear.isNotEmpty())
        parts.add(gear);

    auto tone = jsonString(metadata, "tone_type");
    if (tone.isNotEmpty())
        parts.add(tone.substring(0, 1).toUpperCase() + tone.substring(1));

    const auto withCab = metadata.find("with_cab");
    if (withCab != metadata.end() && withCab->is_boolean())
        parts.add(withCab->get<bool>() ? "Cab included" : "No cab");

    const auto modeler = jsonString(metadata, "modeled_by");
    if (modeler.isNotEmpty())
        parts.add("by " + modeler);
    return parts.joinIntoString(" | ");
}

void ensureNamConfigParsersRegistered()
{
    static std::once_flag flag;
    std::call_once(flag, []
    {
        auto& registry = nam::ConfigParserRegistry::instance();
        const auto registerIfMissing =
            [&registry](const char* name, nam::ConfigParserFunction parser)
        {
            if (!registry.has(name))
                registry.registerParser(name, std::move(parser));
        };

        registerIfMissing("Linear", nam::linear::create_config);
        registerIfMissing("LSTM", nam::lstm::create_config);
        registerIfMissing("ConvNet", nam::convnet::create_config);
        registerIfMissing("WaveNet", nam::wavenet::create_config);
        nam::container::ensure_config_parser_registered();
    });
}
} // namespace

// One full-quality model with its own resampler and buffers.
struct NeuralAmpModelerModule::ModelVariant
{
    std::unique_ptr<nam::DSP> model;
    std::unique_ptr<dsp::ResamplingContainer<float, 1, 12>> resampler;
    bool needsResample = false;
    int latency = 0;
    std::vector<float> inBuf;
    std::vector<float> outBuf;
    int maxBlock = 0;

    void allocate(int block)
    {
        maxBlock = juce::jmax(1, block);
        const auto sz = static_cast<size_t>(maxBlock * 4);
        inBuf.assign(sz, 0.0f);
        outBuf.assign(sz, 0.0f);
    }
    void clearBufs()
    {
        std::fill(inBuf.begin(), inBuf.end(), 0.0f);
        std::fill(outBuf.begin(), outBuf.end(), 0.0f);
    }
    void process(float* data, int n)
    {
        if (!model || n <= 0) return;
        auto runModel = [&](float** in, float** out, int frames)
        { model->process(in, out, frames); };

        std::copy_n(data, n, inBuf.data());
        float* ip = inBuf.data();
        float* op = outBuf.data();
        float* ips[1] = { ip };
        float* ops[1] = { op };
        if (needsResample && resampler)
            resampler->ProcessBlock(ips, ops, n, runModel);
        else
            model->process(ips, ops, n);
        std::copy_n(outBuf.data(), n, data);
    }
};

// Per-channel state owns only the full-quality variant.
struct NeuralAmpModelerModule::ChannelState
{
    std::unique_ptr<ModelVariant> full;

    ChannelState()
    {
        full = std::make_unique<ModelVariant>();
    }
    void allocate(int block)
    {
        full->allocate(block);
    }
    void clearBufs()
    {
        full->clearBufs();
    }
};

struct NeuralAmpModelerModule::PreparedLoad : public PreparedNamModel
{
    std::unique_ptr<ChannelState> ch[maxChannels];
    NamModelInfo info;
    int latency = 0;
};

NeuralAmpModelerModule::NeuralAmpModelerModule()
{
    for (auto& c : channels) c = std::make_unique<ChannelState>();
}

NeuralAmpModelerModule::~NeuralAmpModelerModule() = default;

void NeuralAmpModelerModule::updateToneStack(
    float bassDb, float midDb, float trebleDb) noexcept
{
    const auto setPeak = [this](Biquad& filter, float frequency,
                                float q, float gainDb)
    {
        const auto a = std::pow(10.0f, gainDb / 40.0f);
        const auto omega = juce::MathConstants<float>::twoPi * frequency
            / static_cast<float>(hostRate);
        const auto alpha = std::sin(omega) / (2.0f * q);
        const auto cosine = std::cos(omega);
        const auto a0 = 1.0f + alpha / a;
        filter.b0 = (1.0f + alpha * a) / a0;
        filter.b1 = (-2.0f * cosine) / a0;
        filter.b2 = (1.0f - alpha * a) / a0;
        filter.a1 = (-2.0f * cosine) / a0;
        filter.a2 = (1.0f - alpha / a) / a0;
    };
    const auto setShelf = [this](Biquad& filter, float frequency,
                                 float gainDb, bool high)
    {
        const auto a = std::pow(10.0f, gainDb / 40.0f);
        const auto omega = juce::MathConstants<float>::twoPi * frequency
            / static_cast<float>(hostRate);
        const auto cosine = std::cos(omega);
        const auto sine = std::sin(omega);
        const auto alpha = sine * std::sqrt(2.0f) * 0.5f;
        const auto beta = 2.0f * std::sqrt(a) * alpha;
        const auto sign = high ? -1.0f : 1.0f;
        const auto a0 = (a + 1.0f) + sign * (a - 1.0f) * cosine + beta;
        filter.b0 = a * ((a + 1.0f) - sign * (a - 1.0f) * cosine + beta) / a0;
        filter.b1 = 2.0f * a
            * (sign * (a - 1.0f) - (a + 1.0f) * cosine) / a0;
        filter.b2 = a * ((a + 1.0f) - sign * (a - 1.0f) * cosine - beta) / a0;
        filter.a1 = -2.0f
            * (sign * (a - 1.0f) + (a + 1.0f) * cosine) / a0;
        filter.a2 = ((a + 1.0f) + sign * (a - 1.0f) * cosine - beta) / a0;
    };
    for (auto& channel : toneFilters)
    {
        setShelf(channel[0], 120.0f, bassDb, false);
        setPeak(channel[1], 750.0f, 0.75f, midDb);
        setShelf(channel[2], 3200.0f, trebleDb, true);
    }
}

void NeuralAmpModelerModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    hostRate = detail::safeSampleRate(spec.sampleRate);
    preparedBlock = juce::jmax(1, static_cast<int>(spec.maximumBlockSize));
    preparedChans = juce::jmax(1, static_cast<int>(spec.numChannels));

    for (int ch = 0; ch < maxChannels; ++ch)
        channels[ch]->allocate(preparedBlock);
    wetBuffer.setSize(preparedChans, preparedBlock);
    for (auto& smoother : controlSmoothers)
        smoother.reset(hostRate, 0.025);
    for (auto& frames : controlFrames)
        frames.resize(static_cast<size_t>(preparedBlock));
    reset();
}

void NeuralAmpModelerModule::reset()
{
    for (int ch = 0; ch < maxChannels; ++ch)
    {
        for (auto& filter : toneFilters[static_cast<size_t>(ch)])
            filter.reset();
        channels[ch]->clearBufs();
    }
    wetBuffer.clear();
    const std::array<float, 5> defaults { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f };
    for (size_t index = 0; index < controlSmoothers.size(); ++index)
        controlSmoothers[index].setCurrentAndTargetValue(defaults[index]);
}

void NeuralAmpModelerModule::process(
    juce::AudioBuffer<float>& buffer,
    const ControlValues& controls,
    const ProcessEnvironment& environment)
{
    juce::ScopedNoDenormals noDenormals;
    const int ns = buffer.getNumSamples();
    const int nc = juce::jmin(buffer.getNumChannels(), maxChannels);
    if (ns <= 0 || nc <= 0) return;

    if (ns > preparedBlock)
    {
        for (int offset = 0; offset < ns; offset += preparedBlock)
        {
            const auto chunkSamples = juce::jmin(preparedBlock, ns - offset);
            std::array<float*, maxChannels> pointers {};
            for (int ch = 0; ch < nc; ++ch)
                pointers[static_cast<size_t>(ch)] =
                    buffer.getWritePointer(ch, offset);
            juce::AudioBuffer<float> chunk(
                pointers.data(), nc, chunkSamples);
            process(chunk, controls, environment);
        }
        return;
    }

    const std::array<float, 5> targets {
        juce::Decibels::decibelsToGain(linMap(gainLow, gainHigh, controls[0])),
        linMap(toneLow, toneHigh, controls[1]),
        linMap(toneLow, toneHigh, controls[2]),
        linMap(toneLow, toneHigh, controls[3]),
        juce::Decibels::decibelsToGain(linMap(gainLow, gainHigh, controls[4]))
    };
    for (size_t index = 0; index < controlSmoothers.size(); ++index)
    {
        controlSmoothers[index].setTargetValue(targets[index]);
        auto& frames = controlFrames[index];
        for (int sample = 0; sample < ns; ++sample)
            frames[static_cast<size_t>(sample)] =
                controlSmoothers[index].getNextValue();
    }
    const bool hasModel = modelReady.load(std::memory_order_acquire);

    for (int ch = 0; ch < nc; ++ch)
        wetBuffer.copyFrom(ch, 0, buffer, ch, 0, ns);

    if (hasModel && channels[0]->full->model)
    {
        for (int ch = 0; ch < nc; ++ch)
        {
            const int ci = juce::jmin(ch, maxChannels - 1);
            float* wet = wetBuffer.getWritePointer(ch);

            for (int i = 0; i < ns; ++i)
                wet[i] = detail::finiteSample(wet[i])
                    * controlFrames[0][static_cast<size_t>(i)];
            channels[ci]->full->process(wet, ns);
            for (int i = 0; i < ns; ++i)
            {
                const auto bass = controlFrames[1][static_cast<size_t>(i)];
                const auto mid = controlFrames[2][static_cast<size_t>(i)];
                const auto treble = controlFrames[3][static_cast<size_t>(i)];
                updateToneStack(bass, mid, treble);
                auto sample = wet[i];
                for (auto& filter : toneFilters[static_cast<size_t>(ci)])
                    sample = filter.process(sample);
                wet[i] = sample * controlFrames[4][static_cast<size_t>(i)];
            }
        }
    }
    else
    {
        wetBuffer.clear(0, ns);
    }

    auto outputIsSafe = true;
    for (int ch = 0; ch < nc && outputIsSafe; ++ch)
    {
        const auto* wet = wetBuffer.getReadPointer(ch);
        for (int sample = 0; sample < ns; ++sample)
            if (!std::isfinite(wet[sample])
                || std::abs(wet[sample]) > maximumSafeOutput)
            {
                outputIsSafe = false;
                break;
            }
    }
    if (!outputIsSafe)
    {
        wetBuffer.clear(0, ns);
        for (int ch = 0; ch < nc; ++ch)
            for (auto& filter : toneFilters[static_cast<size_t>(ch)])
                filter.reset();
    }

    for (int ch = 0; ch < nc; ++ch)
        buffer.copyFrom(ch, 0, wetBuffer, ch, 0, ns);
}

int NeuralAmpModelerModule::latencySamples() const
{
    return reportedLatency.load(std::memory_order_relaxed);
}

// --- prepare / commit / cancel / clear (all non-RT) ---

juce::Result NeuralAmpModelerModule::prepareNamModel(
    const juce::File& file, PreparedNamModelPtr& prepared)
{
    if (!file.existsAsFile())
        return juce::Result::fail(
            "NAM file not found: " + file.getFullPathName());
    if (file.getFileExtension().toLowerCase() != ".nam")
        return juce::Result::fail("Expected a .nam file.");

    auto load = std::make_unique<PreparedLoad>();
    try
    {
        ensureNamConfigParsersRegistered();
        nam::dspData config;
        auto mdl = nam::get_dsp(
            std::filesystem::path(file.getFullPathName().toStdString()),
            config, nam::DspLoadOptions { false });
        if (!mdl)
            return juce::Result::fail("Failed to parse NAM model.");

        load->info.name = file.getFileNameWithoutExtension();
        load->info.path = file.getFullPathName();
        load->info.formatVersion =
            juce::String(config.version);
        load->info.expectedSampleRate = mdl->GetExpectedSampleRate();
        load->info.valid = true;
        load->info.generation = isA2Config(config)
            ? NamArchitectureGeneration::a2
            : NamArchitectureGeneration::a1;
        load->info.submodelCount = 1;
        highestQualityModel(config, load->info.submodelCount);
        load->info.fullQuality = true;

        if (config.metadata.is_object())
        {
            const auto displayName =
                jsonString(config.metadata, "display_name");
            const auto metadataName =
                jsonString(config.metadata, "name");
            if (displayName.isNotEmpty())
                load->info.name = displayName;
            else if (metadataName.isNotEmpty())
                load->info.name = metadataName;
            load->info.captureDescription =
                captureDescription(config.metadata);
            load->info.inputLevelDbU =
                jsonNumber(config.metadata, "input_level_dbu");
            load->info.validationEsr =
                jsonNumber(config.metadata, "validation_esr");
            const auto training = config.metadata.find("training");
            if (!std::isfinite(load->info.validationEsr)
                && training != config.metadata.end()
                && training->is_object())
                load->info.validationEsr =
                    jsonNumber(*training, "validation_esr");
        }

        NamModelKind kind = NamModelKind::unknown;
        if (config.architecture == "Linear") kind = NamModelKind::linear;
        else if (config.architecture == "LSTM") kind = NamModelKind::lstm;
        else if (config.architecture == "ConvNet") kind = NamModelKind::convNet;
        else if (config.architecture == "Container"
                 || config.architecture == "SlimmableContainer")
            kind = NamModelKind::container;
        else if (config.architecture == "WaveNet")
        {
            if (dynamic_cast<nam::SlimmableModel*>(mdl.get()))
                kind = NamModelKind::slimmableWaveNet;
            else
            {
#if defined(NAM_ENABLE_A2_FAST)
                int a2ch = 0;
                if (nam::wavenet::a2_fast::is_a2_shape(config.config, &a2ch))
                    kind = a2ch == 8 ? NamModelKind::waveNetA2Standard
                                    : NamModelKind::waveNetA2Nano;
                else
#endif
                    kind = NamModelKind::waveNet;
            }
        }
        load->info.kind = kind;
        const bool isSlim =
            dynamic_cast<nam::SlimmableModel*>(mdl.get()) != nullptr;
        load->info.cpuCapability =
            (kind == NamModelKind::waveNetA2Standard
             || kind == NamModelKind::waveNetA2Nano)
                ? NamCpuCapability::a2Fast
            : isSlim ? NamCpuCapability::slimmable
                     : NamCpuCapability::fullOnly;

        const double modelRate = load->info.expectedSampleRate > 0
                                     ? load->info.expectedSampleRate
                                     : defaultModelRate;
        const bool resample = std::abs(modelRate - hostRate) > 1.0;
        const int blk = juce::jmax(1, preparedBlock);

        auto makeVariant = [&](std::unique_ptr<nam::DSP> m)
            -> std::unique_ptr<ModelVariant>
        {
            auto v = std::make_unique<ModelVariant>();
            v->allocate(blk);
            if (auto* slimmable =
                    dynamic_cast<nam::SlimmableModel*>(m.get()))
                slimmable->SetSlimmableSize(1.0);
            if (resample)
            {
                v->needsResample = true;
                v->resampler = std::make_unique<
                    dsp::ResamplingContainer<float, 1, 12>>(modelRate);
                v->resampler->Reset(hostRate, blk);
                v->latency = v->resampler->GetLatency();
                m->SetPrewarmOnReset(false);
                const auto modelBlock = juce::jmax(
                    1, static_cast<int>(
                           std::ceil(blk * modelRate / hostRate)) + 32);
                m->Reset(modelRate, modelBlock);
                m->prewarm();
            }
            else
            {
                v->latency = 0;
                m->SetPrewarmOnReset(false);
                m->Reset(hostRate, blk);
                m->prewarm();
            }
            v->model = std::move(m);
            if (auto* si =
                    dynamic_cast<nam::SlimmableModel*>(v->model.get()))
                si->FinalizeSlimmableSizeForRealtime();
            return v;
        };

        for (int ch = 0; ch < maxChannels; ++ch)
        {
            auto cs = std::make_unique<ChannelState>();
            auto fm = ch == 0
                ? std::move(mdl)
                : nam::get_dsp(config, nam::DspLoadOptions { false });
            if (!fm) return juce::Result::fail("Stereo model failed.");
            cs->full = makeVariant(std::move(fm));
            cs->allocate(blk);
            load->ch[ch] = std::move(cs);
        }
        load->latency = load->ch[0]->full->latency;
    }
    catch (const std::filesystem::filesystem_error& e)
    {
        return juce::Result::fail(juce::String("NAM filesystem error: ") + e.what());
    }
    catch (const nlohmann::json::exception& e)
    {
        return juce::Result::fail(juce::String("NAM JSON error: ") + e.what());
    }
    catch (const std::invalid_argument& e)
    {
        return juce::Result::fail(juce::String("NAM argument error: ") + e.what());
    }
    catch (const std::out_of_range& e)
    {
        return juce::Result::fail(juce::String("NAM range error: ") + e.what());
    }
    catch (const std::runtime_error& e)
    {
        return juce::Result::fail(juce::String("NAM runtime error: ") + e.what());
    }

    prepared = std::move(load);
    return juce::Result::ok();
}

bool NeuralAmpModelerModule::commitPreparedNamModel(PreparedNamModelPtr& p)
{
    auto* ld = dynamic_cast<PreparedLoad*>(p.get());
    if (!ld) return false;
    auto retired = std::make_unique<PreparedLoad>();
    for (int ch = 0; ch < maxChannels; ++ch)
    {
        retired->ch[ch] = std::move(channels[ch]);
        channels[ch] = std::move(ld->ch[ch]);
    }
    reportedLatency.store(ld->latency, std::memory_order_relaxed);
    { const juce::ScopedLock sl(infoLock); currentInfo = ld->info; }
    modelReady.store(true, std::memory_order_release);
    // Return the displaced graphs to the caller so asynchronous hosts can
    // destroy their large weight buffers away from the message thread.
    p = std::move(retired);
    return true;
}

void NeuralAmpModelerModule::cancelPreparedNamModel(
    PreparedNamModelPtr& p) noexcept
{ p.reset(); }

void NeuralAmpModelerModule::clearNamModel()
{
    modelReady.store(false, std::memory_order_release);
    for (auto& c : channels)
    {
        c = std::make_unique<ChannelState>();
        c->allocate(juce::jmax(1, preparedBlock));
    }
    reportedLatency.store(0, std::memory_order_relaxed);
    { const juce::ScopedLock sl(infoLock); currentInfo = {}; }
}

bool NeuralAmpModelerModule::hasNamModel() const noexcept
{ return modelReady.load(std::memory_order_acquire); }

bool NeuralAmpModelerModule::isNamModelReady() const noexcept
{ return modelReady.load(std::memory_order_acquire); }

NamModelInfo NeuralAmpModelerModule::namModelInfo() const
{ const juce::ScopedLock sl(infoLock); return currentInfo; }

int NeuralAmpModelerModule::namLatencySamples() const noexcept
{ return reportedLatency.load(std::memory_order_relaxed); }
} // namespace megadsp
