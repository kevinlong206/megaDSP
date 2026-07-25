#pragma once

#include "DspModule.h"
#include "DspSafety.h"

#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <memory>
#include <vector>

namespace megadsp
{
enum class NamModelKind
{
    unknown,
    linear,
    lstm,
    convNet,
    waveNet,
    waveNetA2Standard,
    waveNetA2Nano,
    slimmableWaveNet,
    container
};

enum class NamCpuCapability
{
    fullOnly,
    a2Fast,
    slimmable
};

struct NamModelInfo
{
    juce::String name;
    juce::String path;
    NamModelKind kind = NamModelKind::unknown;
    NamCpuCapability cpuCapability = NamCpuCapability::fullOnly;
    double expectedSampleRate = -1.0;
    bool valid = false;
};

class PreparedNamModel
{
public:
    virtual ~PreparedNamModel() = default;
};

using PreparedNamModelPtr = std::unique_ptr<PreparedNamModel>;

class NamModelCapability
{
public:
    virtual ~NamModelCapability() = default;
    virtual juce::Result prepareNamModel(
        const juce::File&, PreparedNamModelPtr&) = 0;
    virtual bool commitPreparedNamModel(PreparedNamModelPtr&) = 0;
    virtual void cancelPreparedNamModel(PreparedNamModelPtr&) noexcept = 0;
    virtual void clearNamModel() = 0;
    virtual bool hasNamModel() const noexcept = 0;
    virtual bool isNamModelReady() const noexcept = 0;
    virtual NamModelInfo namModelInfo() const = 0;
    virtual int namLatencySamples() const noexcept = 0;
};

class NeuralAmpModelerModule final : public DspModule,
                                     public NamModelCapability
{
public:
    NeuralAmpModelerModule();
    ~NeuralAmpModelerModule() override;
    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void reset() override;
    void process(juce::AudioBuffer<float>&, const ControlValues&,
                 const ProcessEnvironment&) override;
    int latencySamples() const override;

    NamModelCapability* namModelCapability() noexcept override
    {
        return this;
    }
    const NamModelCapability* namModelCapability() const noexcept override
    {
        return this;
    }

    juce::Result prepareNamModel(
        const juce::File&, PreparedNamModelPtr&) override;
    bool commitPreparedNamModel(PreparedNamModelPtr&) override;
    void cancelPreparedNamModel(PreparedNamModelPtr&) noexcept override;
    void clearNamModel() override;
    bool hasNamModel() const noexcept override;
    bool isNamModelReady() const noexcept override;
    NamModelInfo namModelInfo() const override;
    int namLatencySamples() const noexcept override;

private:
    struct ModelVariant;
    struct ChannelState;
    struct PreparedLoad;

    static constexpr int maxChannels = 2;
    static constexpr int xfadeLength = 256;

    // Per-channel: two model variants (full / slim) with independent state
    std::unique_ptr<ChannelState> channels[maxChannels];
    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> wetBuffer;
    juce::AudioBuffer<float> xfadeScratch;
    std::array<std::vector<float>, maxChannels> dryDelay;
    int dryDelayWritePosition = 0;
    std::vector<float> inputGainFrames;
    std::vector<float> lowCutFrames;
    std::vector<float> highCutFrames;
    std::vector<float> dryFrames;
    std::vector<float> wetFrames;
    std::vector<float> outputFrames;

    // Per-channel gate
    float gateEnvelope[maxChannels] {};
    float gateGain[maxChannels] {};

    // Per-channel filters
    juce::dsp::StateVariableTPTFilter<float> lowCutFilters[maxChannels];
    juce::dsp::StateVariableTPTFilter<float> highCutFilters[maxChannels];

    // Smoothing state (shared across channels, advanced once per sample)
    float inputGainTarget = 1.0f, inputGainCur = 1.0f;
    float dryTarget = 0.0f, dryCur = 0.0f;
    float wetTarget = 1.0f, wetCur = 1.0f;
    float outputTarget = 1.0f, outputCur = 1.0f;
    float lowCutTarget = 30.0f, lowCutCur = 30.0f;
    float highCutTarget = 18000.0f, highCutCur = 18000.0f;

    // CPU mode crossfade
    int activeMode = 1;   // 0 = slim, 1 = full
    int targetMode = 1;
    int xfadePos = 0;     // 0 means not crossfading

    double hostRate = 44100.0;
    int preparedBlock = 512;
    int preparedChans = 2;
    std::atomic<bool> modelReady { false };
    std::atomic<int> reportedLatency { 0 };

    mutable juce::CriticalSection infoLock;
    NamModelInfo currentInfo;

    float smoothCoeff() const noexcept;
};
} // namespace megadsp
