#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <atomic>

namespace megadsp
{
inline constexpr int reEqBandCount = 16;

enum class ReEqShape
{
    bell, lowCut, lowCutButterworth, lowShelf, highShelf, highCut,
    highCutButterworth, notch, bandPass, tiltShelf, pultecLow,
    allPass, vintageLow, vintageHigh
};

enum class ReEqGroup { stereo, mid, side, left, right };

struct ReEqBand
{
    int enabled = 0;
    ReEqShape shape = ReEqShape::bell;
    ReEqGroup group = ReEqGroup::stereo;
    float frequency = 1000.0f;
    float gainDb = 0.0f;
    float q = 0.70710678f;
    int slope = 0;
    bool dynamic = false;
    float thresholdDb = -24.0f;
    float ratio = 4.0f;
    float rangeDb = -6.0f;
    float attackMs = 5.0f;
    float releaseMs = 80.0f;
    bool autoTiming = false;
    bool externalSidechain = false;
    bool detectorLinked = true;
    float detectorFrequency = 1000.0f;
    float detectorWidth = 2.0f;

    bool operator==(const ReEqBand&) const noexcept;
};

struct ReEqSettings
{
    bool native = false;
    std::array<ReEqBand, reEqBandCount> bands {};
    float inputDb = 0.0f;
    float outputDb = 0.0f;
    float firstGainDb = 0.0f;
    float secondGainDb = 0.0f;
    float scale = 1.0f;
    bool midSide = false;
    bool firstPolarity = false;
    bool secondPolarity = false;
    bool autoGain = false;
    bool limitOutput = false;
    bool highQuality = true;
    float lookaheadMs = 0.0f;
    int listenBand = -1;
    bool listenDetector = false;
    float listenFrequency = 1000.0f;
    float listenWidthOctaves = 2.31015f;
    float listenGainDb = 0.0f;
    ReEqGroup listenGroup = ReEqGroup::stereo;
    int spectrumMode = 0;
    int displayMode = 0;
    int fftOrder = 13;
    int windowType = 1;
    int smoothing = 1;
    float tilt = 4.5f;
    int ceilingDb = 0;
    int floorDb = -90;
    int rangeDb = 18;
    bool showPreEq = false;
    bool showPeaks = false;
    bool showPiano = false;

    void sanitize() noexcept;
    void clearAudition() noexcept;
    juce::ValueTree toValueTree() const;
    static ReEqSettings fromValueTree(const juce::ValueTree&);
    bool operator==(const ReEqSettings&) const noexcept;
};

class AtomicReEqSettings
{
public:
    static_assert(std::atomic<float>::is_always_lock_free);
    AtomicReEqSettings() noexcept;
    ReEqSettings load() const noexcept;
    void store(const ReEqSettings&) noexcept;
    void copyFrom(const AtomicReEqSettings& other) noexcept
    {
        store(other.load());
    }

private:
    static constexpr int bandFields = 18;
    static constexpr int globalFields = 31;
    std::array<std::atomic<float>,
               globalFields + reEqBandCount * bandFields> values {};
};

const char* reEqShapeName(ReEqShape) noexcept;
const char* reEqGroupName(ReEqGroup) noexcept;
int reEqSlopeDb(int slope) noexcept;
} // namespace megadsp
