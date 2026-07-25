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
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;

        float process(float input) noexcept
        {
            const auto output = b0 * input + z1;
            z1 = b1 * input - a1 * output + z2;
            z2 = b2 * input - a2 * output;
            return detail::finiteSample(output);
        }
        void reset() noexcept { z1 = z2 = 0.0f; }
    };

    struct ModelVariant;
    struct ChannelState;
    struct PreparedLoad;

    static constexpr int maxChannels = 2;

    // Per-channel: two model variants (full / slim) with independent state
    std::unique_ptr<ChannelState> channels[maxChannels];
    juce::AudioBuffer<float> wetBuffer;
    std::array<std::array<Biquad, 3>, maxChannels> toneFilters {};
    std::array<juce::SmoothedValue<float>, 5> controlSmoothers {};
    std::array<std::vector<float>, 5> controlFrames;

    double hostRate = 44100.0;
    int preparedBlock = 512;
    int preparedChans = 2;
    std::atomic<bool> modelReady { false };
    std::atomic<int> reportedLatency { 0 };

    mutable juce::CriticalSection infoLock;
    NamModelInfo currentInfo;

    void updateToneStack(float bassDb, float midDb, float trebleDb) noexcept;
};
} // namespace megadsp
