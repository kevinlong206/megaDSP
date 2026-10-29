#include "ReEqSettings.h"

#include <algorithm>
#include <cmath>

namespace megadsp
{
namespace
{
constexpr std::array<const char*, 31> globalKeys {
    "native", "inputDb", "outputDb", "firstGainDb", "secondGainDb", "scale",
    "midSide", "firstPolarity", "secondPolarity", "autoGain", "limitOutput",
    "highQuality", "lookaheadMs", "listenBand", "listenDetector", "spectrumMode",
    "displayMode", "fftOrder", "windowType", "smoothing", "tilt", "ceilingDb",
    "floorDb", "rangeDb", "showPreEq", "showPeaks", "showPiano",
    "listenFrequency", "listenWidthOctaves", "listenGainDb", "listenGroup"
};
constexpr std::array<const char*, 18> bandKeys {
    "enabled", "shape", "group", "frequency", "gainDb", "q", "slope", "dynamic",
    "thresholdDb", "ratio", "rangeDb", "attackMs", "releaseMs", "autoTiming",
    "externalSidechain", "detectorLinked", "detectorFrequency", "detectorWidth"
};

std::array<float, 31> packGlobals(const ReEqSettings& s)
{
    return {
        float(s.native), s.inputDb, s.outputDb, s.firstGainDb, s.secondGainDb,
        s.scale, float(s.midSide), float(s.firstPolarity), float(s.secondPolarity),
        float(s.autoGain), float(s.limitOutput), float(s.highQuality),
        s.lookaheadMs, float(s.listenBand), float(s.listenDetector),
        float(s.spectrumMode), float(s.displayMode), float(s.fftOrder),
        float(s.windowType), float(s.smoothing), s.tilt, float(s.ceilingDb),
        float(s.floorDb), float(s.rangeDb), float(s.showPreEq),
        float(s.showPeaks), float(s.showPiano), s.listenFrequency,
        s.listenWidthOctaves, s.listenGainDb, float(s.listenGroup)
    };
}

void unpackGlobals(ReEqSettings& s, const std::array<float, 31>& v)
{
    s.native = v[0] >= 0.5f;
    s.inputDb = v[1]; s.outputDb = v[2];
    s.firstGainDb = v[3]; s.secondGainDb = v[4]; s.scale = v[5];
    s.midSide = v[6] >= 0.5f;
    s.firstPolarity = v[7] >= 0.5f; s.secondPolarity = v[8] >= 0.5f;
    s.autoGain = v[9] >= 0.5f; s.limitOutput = v[10] >= 0.5f;
    s.highQuality = v[11] >= 0.5f;
    s.lookaheadMs = v[12]; s.listenBand = int(v[13]);
    s.listenDetector = v[14] >= 0.5f;
    s.spectrumMode = int(v[15]); s.displayMode = int(v[16]);
    s.fftOrder = int(v[17]); s.windowType = int(v[18]);
    s.smoothing = int(v[19]); s.tilt = v[20];
    s.ceilingDb = int(v[21]); s.floorDb = int(v[22]); s.rangeDb = int(v[23]);
    s.showPreEq = v[24] >= 0.5f; s.showPeaks = v[25] >= 0.5f;
    s.showPiano = v[26] >= 0.5f;
    s.listenFrequency = v[27]; s.listenWidthOctaves = v[28];
    s.listenGainDb = v[29]; s.listenGroup = ReEqGroup(int(v[30]));
}

std::array<float, 18> packBand(const ReEqBand& b)
{
    return {
        float(b.enabled), float(b.shape), float(b.group), b.frequency, b.gainDb,
        b.q, float(b.slope), float(b.dynamic), b.thresholdDb, b.ratio, b.rangeDb,
        b.attackMs, b.releaseMs, float(b.autoTiming), float(b.externalSidechain),
        float(b.detectorLinked), b.detectorFrequency, b.detectorWidth
    };
}

void unpackBand(ReEqBand& b, const std::array<float, 18>& v)
{
    b.enabled = int(v[0]); b.shape = ReEqShape(int(v[1]));
    b.group = ReEqGroup(int(v[2])); b.frequency = v[3]; b.gainDb = v[4];
    b.q = v[5]; b.slope = int(v[6]); b.dynamic = v[7] >= 0.5f;
    b.thresholdDb = v[8]; b.ratio = v[9]; b.rangeDb = v[10];
    b.attackMs = v[11]; b.releaseMs = v[12];
    b.autoTiming = v[13] >= 0.5f; b.externalSidechain = v[14] >= 0.5f;
    b.detectorLinked = v[15] >= 0.5f;
    b.detectorFrequency = v[16]; b.detectorWidth = v[17];
}

float bounded(float v, float low, float high, float fallback)
{
    return std::isfinite(v) ? juce::jlimit(low, high, v) : fallback;
}
}

bool ReEqBand::operator==(const ReEqBand& other) const noexcept
{
    const auto left = packBand(*this);
    const auto right = packBand(other);
    return std::equal(left.begin(), left.end(), right.begin(),
        [](float a, float b) { return juce::exactlyEqual(a, b); });
}

bool ReEqSettings::operator==(const ReEqSettings& other) const noexcept
{
    const auto left = packGlobals(*this);
    const auto right = packGlobals(other);
    return bands == other.bands
        && std::equal(left.begin(), left.end(), right.begin(),
            [](float a, float b) { return juce::exactlyEqual(a, b); });
}

void ReEqSettings::sanitize() noexcept
{
    inputDb = bounded(inputDb, -136.0f, 30.0f, 0.0f);
    outputDb = bounded(outputDb, -136.0f, 30.0f, 0.0f);
    firstGainDb = bounded(firstGainDb, -136.0f, 30.0f, 0.0f);
    secondGainDb = bounded(secondGainDb, -136.0f, 30.0f, 0.0f);
    scale = bounded(scale, 0.0f, 2.0f, 1.0f);
    lookaheadMs = bounded(lookaheadMs, 0.0f, 10.0f, 0.0f);
    listenBand = juce::jlimit(-2, reEqBandCount - 1, listenBand);
    listenFrequency = bounded(listenFrequency, 10.0f, 30000.0f, 1000.0f);
    listenWidthOctaves = bounded(
        listenWidthOctaves, 0.231015f, 6.93045f, 2.31015f);
    listenGainDb = bounded(listenGainDb, -30.0f, 30.0f, 0.0f);
    listenGroup = ReEqGroup(juce::jlimit(0, 4, int(listenGroup)));
    spectrumMode = juce::jlimit(0, 6, spectrumMode);
    displayMode = juce::jlimit(0, 3, displayMode);
    fftOrder = juce::jlimit(11, 14, fftOrder);
    windowType = juce::jlimit(0, 3, windowType);
    smoothing = juce::jlimit(0, 4, smoothing);
    tilt = bounded(tilt, 0.0f, 6.0f, 4.5f);
    ceilingDb = juce::jlimit(0, 40, ceilingDb);
    floorDb = juce::jlimit(-200, -60, floorDb);
    rangeDb = juce::jlimit(6, 30, rangeDb);
    for (auto& b : bands)
    {
        b.enabled = juce::jlimit(0, 2, b.enabled);
        b.shape = ReEqShape(juce::jlimit(0, 13, int(b.shape)));
        b.group = ReEqGroup(juce::jlimit(0, 4, int(b.group)));
        b.frequency = bounded(b.frequency, 10.0f, 30000.0f, 1000.0f);
        b.gainDb = bounded(b.gainDb, -18.0f, 18.0f, 0.0f);
        b.q = bounded(b.q, 0.1f, 40.0f, 0.70710678f);
        b.slope = juce::jlimit(0, 19, b.slope);
        b.thresholdDb = bounded(b.thresholdDb, -60.0f, 0.0f, -24.0f);
        b.ratio = bounded(b.ratio, 1.0f, 20.0f, 4.0f);
        b.rangeDb = bounded(b.rangeDb, -24.0f, 24.0f, -6.0f);
        b.attackMs = bounded(b.attackMs, 0.1f, 200.0f, 5.0f);
        b.releaseMs = bounded(b.releaseMs, 5.0f, 2000.0f, 80.0f);
        b.detectorFrequency = bounded(
            b.detectorFrequency, 20.0f, 20000.0f, 1000.0f);
        b.detectorWidth = bounded(b.detectorWidth, 0.25f, 8.0f, 2.0f);
    }
}

void ReEqSettings::clearAudition() noexcept
{
    listenBand = -1;
    listenDetector = false;
    listenFrequency = 1000.0f;
    listenWidthOctaves = 2.31015f;
    listenGainDb = 0.0f;
    listenGroup = ReEqGroup::stereo;
}

AtomicReEqSettings::AtomicReEqSettings() noexcept
{
    store({});
}

void AtomicReEqSettings::store(const ReEqSettings& source) noexcept
{
    auto settings = source;
    settings.sanitize();
    const auto globals = packGlobals(settings);
    for (size_t i = 1; i < globals.size(); ++i)
        values[i].store(globals[i], std::memory_order_relaxed);
    for (size_t band = 0; band < settings.bands.size(); ++band)
    {
        const auto fields = packBand(settings.bands[band]);
        for (size_t i = 0; i < fields.size(); ++i)
            values[globalFields + band * bandFields + i].store(
                fields[i], std::memory_order_relaxed);
    }
    values[0].store(globals[0], std::memory_order_release);
}

ReEqSettings AtomicReEqSettings::load() const noexcept
{
    ReEqSettings settings;
    std::array<float, 31> globals {};
    globals[0] = values[0].load(std::memory_order_acquire);
    for (size_t i = 1; i < globals.size(); ++i)
        globals[i] = values[i].load(std::memory_order_relaxed);
    unpackGlobals(settings, globals);
    for (size_t band = 0; band < settings.bands.size(); ++band)
    {
        std::array<float, 18> fields {};
        for (size_t i = 0; i < fields.size(); ++i)
            fields[i] = values[globalFields + band * bandFields + i].load(
                std::memory_order_relaxed);
        unpackBand(settings.bands[band], fields);
    }
    return settings;
}

juce::ValueTree ReEqSettings::toValueTree() const
{
    juce::ValueTree tree("REEQ2");
    const auto globals = packGlobals(*this);
    for (size_t i = 0; i < globals.size(); ++i)
        if (i != 13 && i != 14 && i < 27)
            tree.setProperty(globalKeys[i], globals[i], nullptr);
    for (size_t band = 0; band < bands.size(); ++band)
    {
        juce::ValueTree child("BAND");
        child.setProperty("index", int(band), nullptr);
        const auto fields = packBand(bands[band]);
        for (size_t i = 0; i < fields.size(); ++i)
            child.setProperty(bandKeys[i], fields[i], nullptr);
        tree.addChild(child, -1, nullptr);
    }
    return tree;
}

ReEqSettings ReEqSettings::fromValueTree(const juce::ValueTree& tree)
{
    ReEqSettings settings;
    if (!tree.hasType("REEQ2"))
        return settings;
    auto globals = packGlobals(settings);
    for (size_t i = 0; i < globals.size(); ++i)
        globals[i] = bounded(
            float(tree.getProperty(globalKeys[i], globals[i])),
            -100000.0f, 100000.0f, globals[i]);
    unpackGlobals(settings, globals);
    for (const auto child : tree)
    {
        if (!child.hasType("BAND"))
            continue;
        const auto index = int(child.getProperty("index", -1));
        if (!juce::isPositiveAndBelow(index, reEqBandCount))
            continue;
        auto& band = settings.bands[size_t(index)];
        auto fields = packBand(band);
        for (size_t i = 0; i < fields.size(); ++i)
            fields[i] = bounded(
                float(child.getProperty(bandKeys[i], fields[i])),
                -100000.0f, 100000.0f, fields[i]);
        unpackBand(band, fields);
    }
    settings.sanitize();
    // Audition is transient, never reopen a project in solo/listen.
    settings.clearAudition();
    return settings;
}

const char* reEqShapeName(ReEqShape shape) noexcept
{
    static constexpr std::array<const char*, 14> names {
        "Bell", "Low Cut", "Low Cut BW", "Low Shelf", "High Shelf", "High Cut",
        "High Cut BW", "Notch", "Band Pass", "Tilt Shelf", "Pultec Low",
        "All Pass", "Vintage Lo", "Vintage Hi"
    };
    return names[size_t(juce::jlimit(0, 13, int(shape)))];
}

const char* reEqGroupName(ReEqGroup group) noexcept
{
    static constexpr std::array<const char*, 5> names {
        "Stereo", "Mid", "Side", "Left", "Right"
    };
    return names[size_t(juce::jlimit(0, 4, int(group)))];
}

int reEqSlopeDb(int slope) noexcept
{
    return 6 * (juce::jlimit(0, 19, slope) + 1);
}
} // namespace megadsp
