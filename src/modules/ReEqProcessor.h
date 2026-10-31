#pragma once

#include "DspModule.h"
#include "ReEqSettings.h"

#include <memory>

namespace megadsp
{
// ReEQ2 (c) 2021-2026 Justin Johnson / (c) 2025-2026 Yuriy Ivantsov.
// Native MIT-licensed port; see src/third_party/reeq2/LICENSE.
class ReEqProcessor
{
public:
    ReEqProcessor();
    ~ReEqProcessor();
    ReEqProcessor(const ReEqProcessor&) = delete;
    ReEqProcessor& operator=(const ReEqProcessor&) = delete;

    void prepare(const juce::dsp::ProcessSpec&);
    void reset();
    void process(juce::AudioBuffer<float>&, const ReEqSettings&,
                 const ProcessEnvironment&, float hostOutputDb = 0.0f);
    void setLookahead(float milliseconds) noexcept;
    int latencySamples() const noexcept;
    int maximumLatencySamples() const noexcept;
    static double bandMagnitude(const ReEqBand&, double sampleRate,
                                double frequency, float scale = 1.0f);
    float dynamicGainDb(int band) const noexcept;
    float detectorLevelDb(int band) const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace megadsp
