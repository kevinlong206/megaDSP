#include "NeuralAmpModeler.h"

#include <NAM/get_dsp.h>
#include <NAM/dsp.h>
#include <NAM/slimmable.h>
#include <NAM/wavenet/a2_fast.h>

// Provide missing iplug compatibility for AudioDSPTools LanczosResampler
namespace iplug { constexpr double PI = 3.14159265358979323846; }
#include <ResamplingContainer/ResamplingContainer.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace megadsp
{
namespace
{
constexpr float inputTrimLow = -24.0f;
constexpr float inputTrimHigh = 24.0f;
constexpr float gateThreshLow = -90.0f;
constexpr float gateThreshHigh = -20.0f;
constexpr float gateRelLow = 10.0f;
constexpr float gateRelHigh = 500.0f;
constexpr float lcLow = 20.0f;
constexpr float lcHigh = 250.0f;
constexpr float hcLow = 4000.0f;
constexpr float hcHigh = 20000.0f;
constexpr float outLow = -24.0f;
constexpr float outHigh = 24.0f;
constexpr double defaultModelRate = 48000.0;

float expMap(float lo, float hi, float n)
{
    return lo * std::pow(hi / lo, juce::jlimit(0.0f, 1.0f, n));
}
float linMap(float lo, float hi, float n)
{
    return lo + (hi - lo) * juce::jlimit(0.0f, 1.0f, n);
}
} // namespace

// One model variant (full or slim) with its own resampler and buffers
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

// Per-channel state: owns full and slim variants
struct NeuralAmpModelerModule::ChannelState
{
    std::unique_ptr<ModelVariant> full;
    std::unique_ptr<ModelVariant> slim;
    bool slimmable = false;

    ChannelState()
    {
        full = std::make_unique<ModelVariant>();
        slim = std::make_unique<ModelVariant>();
    }
    void allocate(int block)
    {
        full->allocate(block);
        slim->allocate(block);
    }
    void clearBufs()
    {
        full->clearBufs();
        slim->clearBufs();
    }
    ModelVariant* variant(int mode)
    {
        return (mode == 0 && slimmable && slim->model) ? slim.get() : full.get();
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

float NeuralAmpModelerModule::smoothCoeff() const noexcept
{
    return 1.0f - std::exp(-1.0f / static_cast<float>(
        detail::safeSampleRate(hostRate) * 0.005));
}

void NeuralAmpModelerModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    hostRate = detail::safeSampleRate(spec.sampleRate);
    preparedBlock = juce::jmax(1, static_cast<int>(spec.maximumBlockSize));
    preparedChans = juce::jmax(1, static_cast<int>(spec.numChannels));

    const auto monoSpec = juce::dsp::ProcessSpec {
        spec.sampleRate, spec.maximumBlockSize, 1 };
    for (int ch = 0; ch < maxChannels; ++ch)
    {
        lowCutFilters[ch].prepare(monoSpec);
        lowCutFilters[ch].setType(juce::dsp::StateVariableTPTFilterType::highpass);
        highCutFilters[ch].prepare(monoSpec);
        highCutFilters[ch].setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        channels[ch]->allocate(preparedBlock);
    }
    dryBuffer.setSize(preparedChans, preparedBlock);
    wetBuffer.setSize(preparedChans, preparedBlock);
    xfadeScratch.setSize(1, preparedBlock);
    inputGainFrames.resize(static_cast<size_t>(preparedBlock));
    lowCutFrames.resize(static_cast<size_t>(preparedBlock));
    highCutFrames.resize(static_cast<size_t>(preparedBlock));
    dryFrames.resize(static_cast<size_t>(preparedBlock));
    wetFrames.resize(static_cast<size_t>(preparedBlock));
    outputFrames.resize(static_cast<size_t>(preparedBlock));
}

void NeuralAmpModelerModule::reset()
{
    for (int ch = 0; ch < maxChannels; ++ch)
    {
        lowCutFilters[ch].reset();
        highCutFilters[ch].reset();
        gateEnvelope[ch] = 0.0f;
        gateGain[ch] = 1.0f;
        channels[ch]->clearBufs();
    }
    dryBuffer.clear();
    wetBuffer.clear();
    xfadeScratch.clear();
    for (auto& channel : dryDelay)
        std::fill(channel.begin(), channel.end(), 0.0f);
    dryDelayWritePosition = 0;
    inputGainCur = inputGainTarget;
    dryCur = dryTarget;
    wetCur = wetTarget;
    outputCur = outputTarget;
    lowCutCur = lowCutTarget;
    highCutCur = highCutTarget;
    xfadePos = 0;
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

    // --- read controls ---
    inputGainTarget = juce::Decibels::decibelsToGain(
        linMap(inputTrimLow, inputTrimHigh, controls[0]));
    const bool gateOn = controls[1] >= 0.5f;
    const float gateThreshLin = juce::Decibels::decibelsToGain(
        linMap(gateThreshLow, gateThreshHigh, controls[2]));
    const float gateRelMs = expMap(gateRelLow, gateRelHigh, controls[3]);
    lowCutTarget = expMap(lcLow, lcHigh, controls[4]);
    highCutTarget = expMap(hcLow, hcHigh, controls[5]);
    const int wantMode = controls[6] < 0.5f ? 0 : 1;
    dryTarget = controls[7];
    wetTarget = controls[8];
    outputTarget = juce::Decibels::decibelsToGain(
        linMap(outLow, outHigh, controls[9]));

    const float gateRet = 1.0f - std::exp(
        -1.0f / static_cast<float>(hostRate * gateRelMs * 0.001));
    const float coeff = smoothCoeff();
    const bool hasModel = modelReady.load(std::memory_order_acquire);

    // --- precompute per-sample smooth arrays ---
    // We compute them once and apply identically to all channels.
    auto* igArr = inputGainFrames.data();
    auto* lcArr = lowCutFrames.data();
    auto* hcArr = highCutFrames.data();
    auto* dArr = dryFrames.data();
    auto* wArr = wetFrames.data();
    auto* oArr = outputFrames.data();
    {
        float ig = inputGainCur, lc = lowCutCur, hc = highCutCur;
        float d = dryCur, w = wetCur, o = outputCur;
        for (int i = 0; i < ns; ++i)
        {
            ig += (inputGainTarget - ig) * coeff;
            lc += (lowCutTarget - lc) * coeff;
            hc += (highCutTarget - hc) * coeff;
            d += (dryTarget - d) * coeff;
            w += (wetTarget - w) * coeff;
            o += (outputTarget - o) * coeff;
            igArr[i] = ig; lcArr[i] = lc; hcArr[i] = hc;
            dArr[i] = d; wArr[i] = w; oArr[i] = o;
        }
        inputGainCur = ig; lowCutCur = lc; highCutCur = hc;
        dryCur = d; wetCur = w; outputCur = o;
    }

    // --- CPU mode crossfade management ---
    const auto canSlim = hasModel && channels[0]->slimmable;
    const auto effectiveMode = canSlim ? wantMode : 1;
    if (effectiveMode != targetMode)
    {
        targetMode = effectiveMode;
        xfadePos = targetMode == activeMode ? 0 : xfadeLength;
    }
    const auto transitionSamples = juce::jmin(xfadePos, ns);

    // --- save dry, build wet ---
    for (int ch = 0; ch < nc; ++ch)
    {
        dryBuffer.copyFrom(ch, 0, buffer, ch, 0, ns);
        wetBuffer.copyFrom(ch, 0, buffer, ch, 0, ns);
    }

    if (hasModel && channels[0]->full->model)
    {
        for (int ch = 0; ch < nc; ++ch)
        {
            const int ci = juce::jmin(ch, maxChannels - 1);
            float* wet = wetBuffer.getWritePointer(ch);

            // input gain
            for (int i = 0; i < ns; ++i)
                wet[i] = detail::finiteSample(wet[i]) * igArr[i];

            // per-channel gate
            if (gateOn)
            {
                for (int i = 0; i < ns; ++i)
                {
                    const float lv = std::abs(wet[i]);
                    if (lv > gateEnvelope[ci])
                        gateEnvelope[ci] = lv;
                    else
                        gateEnvelope[ci] *= (1.0f - gateRet);
                    const float tgt = gateEnvelope[ci] >= gateThreshLin
                                          ? 1.0f : 0.0f;
                    gateGain[ci] += (tgt - gateGain[ci]) * gateRet;
                    wet[i] *= gateGain[ci];
                }
            }
            else
            {
                // Click-safe fade gate gain toward 1
                for (int i = 0; i < ns; ++i)
                {
                    gateGain[ci] += (1.0f - gateGain[ci]) * coeff;
                    wet[i] *= gateGain[ci];
                }
            }

            auto* cs = channels[ci].get();
            if (transitionSamples > 0 && cs->slimmable)
            {
                xfadeScratch.copyFrom(
                    0, 0, wetBuffer, ch, 0, transitionSamples);
                cs->variant(activeMode)->process(wet, transitionSamples);
                auto* alt = xfadeScratch.getWritePointer(0);
                cs->variant(targetMode)->process(alt, transitionSamples);
                for (int i = 0; i < transitionSamples; ++i)
                {
                    const auto oldGain = static_cast<float>(xfadePos - i)
                        / static_cast<float>(xfadeLength);
                    wet[i] = wet[i] * oldGain
                        + alt[i] * (1.0f - oldGain);
                }
                if (transitionSamples < ns)
                    cs->variant(targetMode)->process(
                        wet + transitionSamples, ns - transitionSamples);
            }
            else
            {
                cs->variant(activeMode)->process(wet, ns);
            }

            // per-channel filters with per-sample cutoff
            for (int i = 0; i < ns; ++i)
            {
                lowCutFilters[ci].setCutoffFrequency(lcArr[i]);
                highCutFilters[ci].setCutoffFrequency(hcArr[i]);
                float s = wet[i];
                s = lowCutFilters[ci].processSample(0, s);
                s = highCutFilters[ci].processSample(0, s);
                wet[i] = s;
            }
        }
    }
    else
    {
        wetBuffer.clear(0, ns);
    }

    if (transitionSamples > 0)
    {
        xfadePos -= transitionSamples;
        if (xfadePos == 0)
            activeMode = targetMode;
    }

    const auto dryLatency = reportedLatency.load(std::memory_order_relaxed);
    if (dryLatency > 0)
    {
        for (int i = 0; i < ns; ++i)
        {
            for (int ch = 0; ch < nc; ++ch)
            {
                auto& delayed = dryDelay[static_cast<size_t>(ch)][
                    static_cast<size_t>(dryDelayWritePosition)];
                const auto input = dryBuffer.getSample(ch, i);
                dryBuffer.setSample(ch, i, delayed);
                delayed = input;
            }
            if (++dryDelayWritePosition >= dryLatency)
                dryDelayWritePosition = 0;
        }
    }

    // --- blend dry/wet -> output ---
    for (int ch = 0; ch < nc; ++ch)
    {
        float* out = buffer.getWritePointer(ch);
        const float* dry = dryBuffer.getReadPointer(ch);
        const float* wet = wetBuffer.getReadPointer(ch);
        for (int i = 0; i < ns; ++i)
        {
            float v = detail::finiteSample(dry[i]) * dArr[i]
                    + detail::finiteSample(wet[i]) * wArr[i];
            out[i] = detail::finiteSample(v * oArr[i]);
        }
    }
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
        nam::dspData config;
        auto mdl = nam::get_dsp(
            std::filesystem::path(file.getFullPathName().toStdString()),
            config, nam::DspLoadOptions { false });
        if (!mdl)
            return juce::Result::fail("Failed to parse NAM model.");

        load->info.name = file.getFileNameWithoutExtension();
        load->info.path = file.getFullPathName();
        load->info.expectedSampleRate = mdl->GetExpectedSampleRate();
        load->info.valid = true;

        // --- detect kind (item 3: authoritative A2) ---
        NamModelKind kind = NamModelKind::unknown;
        if (config.architecture == "Linear") kind = NamModelKind::linear;
        else if (config.architecture == "LSTM") kind = NamModelKind::lstm;
        else if (config.architecture == "ConvNet") kind = NamModelKind::convNet;
        else if (config.architecture == "Container") kind = NamModelKind::container;
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

        auto makeVariant = [&](std::unique_ptr<nam::DSP> m, bool doSlim)
            -> std::unique_ptr<ModelVariant>
        {
            auto v = std::make_unique<ModelVariant>();
            v->allocate(blk);
            if (doSlim)
            {
                if (auto* si = dynamic_cast<nam::SlimmableModel*>(m.get()))
                    si->SetSlimmableSize(0.0);
            }
            else if (isSlim)
            {
                if (auto* si = dynamic_cast<nam::SlimmableModel*>(m.get()))
                    si->SetSlimmableSize(1.0);
            }
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

        auto loadModel = [&]() { return nam::get_dsp(
            std::filesystem::path(file.getFullPathName().toStdString()),
            nam::DspLoadOptions { false }); };

        for (int ch = 0; ch < maxChannels; ++ch)
        {
            auto cs = std::make_unique<ChannelState>();
            cs->slimmable = isSlim;
            // full variant
            auto fm = ch == 0 ? std::move(mdl) : loadModel();
            if (!fm) return juce::Result::fail("Stereo model failed.");
            cs->full = makeVariant(std::move(fm), false);
            // slim variant (distinct instance)
            if (isSlim)
            {
                auto sm = loadModel();
                if (!sm)
                    return juce::Result::fail(
                        "Slim NAM model variant failed.");
                cs->slim = makeVariant(std::move(sm), true);
            }
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
    for (int ch = 0; ch < maxChannels; ++ch)
        channels[ch] = std::move(ld->ch[ch]);
    reportedLatency.store(ld->latency, std::memory_order_relaxed);
    const auto delaySize = static_cast<size_t>(juce::jmax(0, ld->latency));
    for (auto& channel : dryDelay)
        channel.assign(delaySize, 0.0f);
    dryDelayWritePosition = 0;
    { const juce::ScopedLock sl(infoLock); currentInfo = ld->info; }
    activeMode = targetMode = 1;
    xfadePos = 0;
    modelReady.store(true, std::memory_order_release);
    p.reset();
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
    for (auto& channel : dryDelay)
        channel.clear();
    dryDelayWritePosition = 0;
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
