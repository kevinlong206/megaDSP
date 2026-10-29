/*
 * Native port of ReEQ2, copyright (c) 2021-2026 Justin Johnson, and Ivantsov
 * decramped filters, copyright (c) 2025-2026 Yuriy Ivantsov.
 * MIT licensed; the complete notices are in ../third_party/reeq2/LICENSE.
 * Upstream: ReJJ 281bf08f9d0b5e25a0c7db05d37b7a2f3afa6b41.
 */
#include "ReEqProcessor.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace megadsp
{
namespace
{
constexpr double pi = juce::MathConstants<double>::pi;
constexpr int dynamicStep = 16;
constexpr int gainPoints = 61;
double square(double x) noexcept { return x * x; }
double amplitude(double db) noexcept { return std::pow(10.0, db / 20.0); }
double bounded(double x, double lo, double hi, double fallback) noexcept
{
    return std::isfinite(x) ? std::clamp(x, lo, hi) : fallback;
}
float finiteFloat(double x) noexcept
{
    return std::isfinite(x) && std::abs(x) <= std::numeric_limits<float>::max()
        ? static_cast<float>(x) : 0.0f;
}
bool dynamicShape(ReEqShape s) noexcept
{
    return s == ReEqShape::bell || s == ReEqShape::lowShelf
           || s == ReEqShape::highShelf;
}

struct Coeff
{
    double p = 0.5, q = 0.5, d = 1, e = 1, c = 1;
};

// Ivantsov's sigma-corrected analogue-to-state-space transformation, without
// Nyquist clamping: above Nyquist the plain bilinear continuation is intentional.
Coeff section(int type, double fc, double zeta, double gn, double rate) noexcept
{
    const auto w = rate / (pi * fc);
    auto sigma = 2.0 / pi;
    if (fc < rate * 0.5)
    {
        const auto x = pi * fc / rate;
        const auto r = std::cos(x) / std::sin(x);
        sigma = std::sqrt(std::max(w * w - r * r, 0.0));
    }
    const auto ss = sigma * sigma;
    const auto zp = type == 7 ? zeta / gn : zeta;
    auto tt = type == 4 ? w * w * gn : type == 3 ? w * w / gn : w * w;
    auto k = tt * (2 * zp * zp - 1);
    const auto v = std::sqrt(std::max(0.0, tt * tt + ss * (2 * k + ss)));
    k += ss;
    const auto root = std::sqrt(std::max(0.0, 2 * (k + v)));
    const auto b0 = 2 / (1 + root + v);
    const auto b1 = std::sqrt(v);
    double b2, b3;
    if (type == 3 || type == 4 || type == 7)
    {
        const auto zz = type == 7 ? zeta * gn : zp;
        tt = type == 4 ? w * w / gn : type == 3 ? w * w * gn : w * w;
        auto ka = tt * (2 * zz * zz - 1);
        const auto va = std::sqrt(std::max(0.0, tt * tt + ss * (2 * ka + ss)));
        ka += ss;
        b2 = va / b1;
        b3 = (1 + std::sqrt(std::max(0.0, 2 * (ka + va))) + va) * 0.5;
    }
    else if (type == 0) { b2 = w * w / b1; b3 = w * w * 0.5; }
    else if (type == 1) { b2 = ss / b1; b3 = square(1 + sigma) * 0.5; }
    else if (type == 2) { b2 = b1; b3 = (1 - root + v) * 0.5; }
    else if (type == 5)
    {
        b2 = 2 * w * zeta * sigma / b1;
        b3 = w * zeta * (1 + sigma);
    }
    else { b2 = (w * w - ss) / b1; b3 = (1 + w * w - ss) * 0.5; }
    const auto out = type == 4 ? gn * gn : 1.0;
    return { b0, b0 * b1 * b1, b0 * b3 * out, b2 * out / b1,
             type == 0 || type == 5 ? 0.0 : out };
}

Coeff firstOrder(int type, double fc, double rate) noexcept
{
    const auto w = rate / (pi * fc);
    auto sigma = 2.0 / pi;
    if (fc < rate * 0.5)
    {
        const auto x = pi * fc / rate;
        sigma = std::sqrt(std::max(0.0, w * w - square(std::cos(x) / std::sin(x))));
    }
    const auto p = 2 / (1 + std::sqrt(w * w + sigma * sigma));
    return { p, 0, p * (type == 1 ? (1 + sigma) * 0.5 : w * 0.5),
             0, type == 1 ? 1.0 : 0.0 };
}

struct Design
{
    std::array<Coeff, 10> sections {};
    Coeff op {};
    int count = 0;
    bool first = false;
    void put(int type, double f, double z, double g, double rate) noexcept
    {
        sections[static_cast<size_t>(count++)] = section(type, f, z, g, rate);
    }
    void butterworth(int type, double f, int order, double rate) noexcept
    {
        const auto odd = order & 1;
        for (int j = 0; j < (order >> 1); ++j)
            put(type, f, std::cos(pi * (2 * (j + odd) + 1 - odd)
                                 / (2 * order)), 1, rate);
    }
    double magnitude(double f, double rate) const noexcept
    {
        const auto x = pi * std::clamp(f, 0.0, rate * 0.5) / rate;
        const auto sn = square(std::sin(x)), cs = square(std::cos(x));
        auto power = 1.0;
        for (int i = 0; i < count; ++i)
        {
            const auto& a = sections[static_cast<size_t>(i)];
            const auto num = cs * square(a.c * a.p - 2 * a.d * sn)
                             + sn * square(2 * a.d * cs - a.e * a.q);
            const auto den = cs * square(a.p - 2 * sn)
                             + sn * square(2 * cs - a.q);
            power *= num / std::max(den, 1.0e-300);
        }
        if (first)
            power *= (square(op.c * op.p - 2 * op.d * sn)
                      + 4 * op.d * op.d * sn * cs)
                     / std::max(square(op.p - 2 * sn) + 4 * sn * cs, 1.0e-300);
        return std::sqrt(std::max(0.0, power));
    }
};

Design design(const ReEqBand& b, double rate, double scale, bool force = false) noexcept
{
    Design result;
    if (b.enabled != 2 && !force)
        return result;
    const auto f = bounded(b.frequency, 10, 30000, 1000);
    const auto q = bounded(b.q, 0.1, 40, 0.70710678);
    const auto db = bounded(b.gainDb, -18, 18, 0) * scale;
    const auto gn = std::pow(10.0, db / 40);
    const auto slope = std::clamp(b.slope, 0, 19);
    const auto shape = b.shape;
    if (shape == ReEqShape::lowCut || shape == ReEqShape::highCut
        || shape == ReEqShape::lowCutButterworth || shape == ReEqShape::highCutButterworth)
    {
        const auto type = shape == ReEqShape::lowCut
                          || shape == ReEqShape::lowCutButterworth ? 0 : 1;
        if (shape == ReEqShape::lowCutButterworth || shape == ReEqShape::highCutButterworth)
            result.butterworth(type, f, slope + 1, rate);
        else
            for (int i = 0; i < ((slope + 1) >> 1); ++i)
                result.put(type, f, 0.5 / q, 1, rate);
        result.first = (slope & 1) == 0;
        if (result.first) result.op = firstOrder(type, f, rate);
    }
    else if (shape == ReEqShape::notch || shape == ReEqShape::bandPass
             || shape == ReEqShape::allPass)
        result.put(shape == ReEqShape::notch ? 6 : shape == ReEqShape::bandPass ? 5 : 2,
                   f, 0.5 / q, 1, rate);
    else if (shape == ReEqShape::vintageLow)
    {
        result.put(4, f * 1.25, 0.5 / 0.701, gn, rate);
        result.first = true;
        result.op = firstOrder(0, f, rate);
    }
    else if (shape == ReEqShape::vintageHigh)
    {
        const auto t = 1 + std::max(db, -12.0) / 12;
        const auto cutoff = std::min(db > 0 ? 22000.0 : f + 3.25 * f * t, 22040.0);
        result.put(3, f, 0.5 / 0.701, gn, rate);
        result.put(1, cutoff, 0.5 / 0.701, 1, rate);
    }
    else if (!juce::exactlyEqual(db, 0.0))
    {
        if (shape == ReEqShape::tiltShelf)
        {
            result.put(3, f, 0.5 / q, gn, rate);
            result.put(4, f, 0.5 / q, 1 / gn, rate);
        }
        else if (shape == ReEqShape::pultecLow)
        {
            const auto cut = q * 0.25 * std::clamp(db, 0.0, 10.0) / 10;
            result.put(4, f * 1.5, 0.5 / 0.701, gn, rate);
            result.put(7, f * 1.5, 0.5 / 0.701, std::pow(10.0, -cut / 40), rate);
        }
        else
            result.put(shape == ReEqShape::lowShelf ? 4 : shape == ReEqShape::highShelf ? 3 : 7,
                       f, 0.5 / q, gn, rate);
    }
    return result;
}

Coeff interpolate(const Coeff& a, const Coeff& b, double t) noexcept
{
    return { a.p + (b.p - a.p) * t, a.q + (b.q - a.q) * t,
             a.d + (b.d - a.d) * t, a.e + (b.e - a.e) * t,
             a.c + (b.c - a.c) * t };
}

struct Filter
{
    Design current, start, target;
    std::array<std::array<double, 4>, 10> z0 {}, z1 {};
    std::array<double, 4> opState {};
    double position = 1, step = 1;
    void clear() noexcept { z0 = {}; z1 = {}; opState = {}; }
    void set(const Design& d, int samples, bool immediate = false) noexcept
    {
        target = d;
        const auto n = std::max(current.count, target.count);
        for (int i = 0; i < n; ++i)
        {
            const auto k = static_cast<size_t>(i);
            if (i >= current.count)
            {
                current.sections[k] = target.sections[k];
                current.sections[k].d = current.sections[k].e = current.sections[k].c = 1;
                z0[k] = {}; z1[k] = {};
            }
            if (i >= target.count)
            {
                target.sections[k] = current.sections[k];
                target.sections[k].d = target.sections[k].e = target.sections[k].c = 1;
            }
        }
        if (!current.first && target.first)
        {
            current.op = target.op;
            current.op.d = current.op.c = 1;
            opState = {};
        }
        if (!target.first)
        {
            target.op = current.op;
            target.op.d = target.op.c = 1;
        }
        start = current;
        current.count = n;
        current.first = current.first || target.first;
        position = 0;
        step = 1.0 / std::max(1, samples);
        if (immediate) { current = target; position = 1; }
    }
    void tick() noexcept
    {
        if (position >= 1) return;
        position = std::min(1.0, position + step);
        for (int i = 0; i < current.count; ++i)
        {
            const auto k = static_cast<size_t>(i);
            current.sections[k] = interpolate(start.sections[k], target.sections[k], position);
        }
        if (current.first) current.op = interpolate(start.op, target.op, position);
        if (position >= 1) current = target;
    }
    double process(double x, size_t channel) noexcept
    {
        for (int i = 0; i < current.count; ++i)
        {
            const auto k = static_cast<size_t>(i);
            const auto& a = current.sections[k];
            const auto u = x - z0[k][channel] - z1[k][channel];
            x = u * a.d + z1[k][channel] * a.e + z0[k][channel] * a.c;
            z0[k][channel] += u * a.p;
            z1[k][channel] = -z1[k][channel] - u * a.q;
        }
        if (current.first)
        {
            const auto u = x - opState[channel];
            x = u * current.op.d + opState[channel] * current.op.c;
            opState[channel] += u * current.op.p;
        }
        return x;
    }
};

struct Ramp
{
    double value = 0, target = 0, delta = 0;
    int remaining = 0;
    void set(double v, int length) noexcept
    {
        if (juce::exactlyEqual(v, target)) return;
        target = v; remaining = std::max(1, length);
        delta = (target - value) / remaining;
    }
    double next() noexcept
    {
        if (remaining > 0 && --remaining == 0) value = target;
        else if (remaining > 0) value += delta;
        return value;
    }
    void snap(double v) noexcept { value = target = v; remaining = 0; delta = 0; }
};

std::array<double, 2> routed(Filter& filter, double l, double r,
                             const std::array<double, 5>& weights, bool solo = false) noexcept
{
    const auto fl = filter.process(l, 0), fr = filter.process(r, 1);
    const auto m = (l + r) * 0.5, s = (l - r) * 0.5;
    const auto fm = filter.process(m, 2), fs = filter.process(s, 3);
    if (solo)
        return { weights[0] * fl + weights[1] * fm + weights[2] * fs + weights[3] * fl,
                 weights[0] * fr + weights[1] * fm - weights[2] * fs + weights[4] * fr };
    return { l + (weights[0] + weights[3]) * (fl - l)
                   + weights[1] * (fm - m) + weights[2] * (fs - s),
             r + (weights[0] + weights[4]) * (fr - r)
                   + weights[1] * (fm - m) - weights[2] * (fs - s) };
}

Design listenDesign(const ReEqBand& band, double rate) noexcept
{
    auto b = band;
    b.enabled = 2;
    const auto q = bounded(b.q, 0.1, 40, 0.7071);
    auto width = 2 / std::log(2.0) * std::asinh(0.5 / q);
    if (b.shape == ReEqShape::bell || b.shape == ReEqShape::notch) width *= 2;
    const auto left = std::clamp(b.frequency / std::pow(2.0, width * 0.5), 10.0, 30000.0);
    const auto right = std::clamp(b.frequency * std::pow(2.0, width * 0.5), 10.0, 30000.0);
    Design d;
    switch (b.shape)
    {
        case ReEqShape::bell:
        case ReEqShape::bandPass:
            d.put(b.shape == ReEqShape::bell ? 7 : 6, b.frequency, 0.5 / q,
                  b.shape == ReEqShape::bell && band.enabled == 2
                      ? std::pow(10.0, band.gainDb / 40.0) : 1, rate);
            d.butterworth(0, left, 4, rate);
            d.butterworth(1, right, 4, rate);
            return d;
        case ReEqShape::notch: b.shape = ReEqShape::bandPass; break;
        case ReEqShape::lowCut: b.shape = ReEqShape::highCut; break;
        case ReEqShape::lowCutButterworth: b.shape = ReEqShape::highCutButterworth; break;
        case ReEqShape::highCut: b.shape = ReEqShape::lowCut; break;
        case ReEqShape::highCutButterworth: b.shape = ReEqShape::lowCutButterworth; break;
        case ReEqShape::lowShelf:
        case ReEqShape::pultecLow:
            b.shape = ReEqShape::highCutButterworth; b.frequency = static_cast<float>(right); b.slope = 3; break;
        case ReEqShape::highShelf:
            b.shape = ReEqShape::lowCutButterworth; b.frequency = static_cast<float>(left); b.slope = 3; break;
        case ReEqShape::vintageLow: b.shape = ReEqShape::highCut; b.q = 0.701f; b.slope = 3; break;
        case ReEqShape::vintageHigh: b.shape = ReEqShape::lowCut; b.q = 0.701f; b.slope = 3; break;
        case ReEqShape::tiltShelf:
        case ReEqShape::allPass: return d;
    }
    return design(b, rate, 1);
}

Design freeformListenDesign(const ReEqSettings& settings, double rate) noexcept
{
    const auto frequency = bounded(settings.listenFrequency, 10, 30000, 1000);
    const auto width = bounded(settings.listenWidthOctaves, 0.231015, 6.93045, 2.31015);
    const auto left = std::max(frequency * std::pow(2.0, -0.5 * width), 10.0);
    const auto right = std::min(frequency * std::pow(2.0, 0.5 * width), 30000.0);
    Design d;
    d.put(7, frequency, 0.5 / 0.7071, 1, rate);
    d.butterworth(0, left, 4, rate);
    d.butterworth(1, right, 4, rate);
    return d;
}

double biquadPower(double b0, double b1, double b2, double a1, double a2, double w) noexcept
{
    const auto c1 = std::cos(w), s1 = std::sin(w), c2 = std::cos(2 * w), s2 = std::sin(2 * w);
    return (square(b0 + b1 * c1 + b2 * c2) + square(b1 * s1 + b2 * s2))
           / (square(1 + a1 * c1 + a2 * c2) + square(a1 * s1 + a2 * s2));
}
} // namespace

struct ReEqProcessor::Impl
{
    struct Band
    {
        Filter filter, detector;
        std::array<Ramp, 5> groups;
        std::array<double, gainPoints> power {};
        std::array<double, 2> rms {};
        double rmsCoefficient = 0, normalization = 1;
        double gain = 0, fast = 0, slow = 0, applied = 0;
        double attack = 0, release = 0, attack2 = 0, release2 = 0;
        double frequency = 1000, q = 0.7071, staticGain = 0, scale = 1;
        int countdown = 0;
        bool dynamic = false, entering = false, pending = false;
    };
    std::array<Band, reEqBandCount> bands;
    std::array<std::atomic<float>, reEqBandCount> gainMeters {}, levelMeters {};
    std::array<double, gainPoints> frequencies {}, weights {};
    std::vector<std::array<double, 6>> delay;
    ReEqSettings previous;
    Filter listenFilter;
    std::array<Ramp, 5> listenGroups;
    Ramp input, output, first, second, domain, listenMix, listenGain, detectorMix, limitMix, delayMix;
    double rate = 48000, gainCorrection = 1, gainTarget = 1, gainSmoothing = 0;
    int glide = 1440, write = 0, delayLength = 0, previousDelay = 0;
    std::atomic<int> latency { 0 };
    bool initialized = false, prepared = false;

    void detectorDesign(Band& state, const ReEqBand& b, bool immediate) noexcept
    {
        Design d;
        auto low = static_cast<double>(b.frequency);
        state.normalization = 1;
        if (b.detectorLinked)
            d.put(b.shape == ReEqShape::lowShelf ? 1 : b.shape == ReEqShape::highShelf ? 0 : 5,
                  b.frequency, b.shape == ReEqShape::bell ? 0.5 / b.q : 0.7071, 1, rate);
        else
        {
            const auto f = bounded(b.detectorFrequency, 20, std::min(20000.0, 0.45 * rate), 1000);
            const auto width = bounded(b.detectorWidth, 0.25, 8, 2);
            auto fl = std::max(f * std::pow(2.0, -0.5 * width), 10.0);
            const auto fh = std::min(f * std::pow(2.0, 0.5 * width), 0.45 * rate);
            if (fh < fl * 1.05) fl = fh / 1.05;
            d.butterworth(0, fl, 4, rate); d.butterworth(1, fh, 4, rate);
            state.normalization = 1 / std::max(d.magnitude(std::sqrt(fl * fh), rate), 0.05);
            low = std::max(f * std::pow(2.0, -0.5 * width), 10.0);
        }
        state.detector.set(d, glide, immediate);
        state.rmsCoefficient = 1 - std::exp(-1 / (std::max(1 / low, 0.001) * rate));
    }

    void timing(Band& state, const ReEqBand& b) noexcept
    {
        const auto k = dynamicStep / (0.001 * rate);
        auto a = std::max(0.05, static_cast<double>(b.attackMs));
        auto r = std::max(1.0, static_cast<double>(b.releaseMs));
        if (b.autoTiming)
        {
            const auto f = b.detectorLinked ? b.frequency
                : bounded(b.detectorFrequency, 20, std::min(20000.0, rate * 0.45), 1000);
            const auto q = b.detectorLinked ? b.q : 1.41 / bounded(b.detectorWidth, 0.25, 8, 2);
            a = std::clamp(1000 * (1.5 / f + q / (pi * f)) + 0.5, 0.5, 30.0);
            r = std::clamp(8 * a, 25.0, 300.0);
            state.attack2 = 1 - std::exp(-k / std::max(10 * a, 50.0));
            state.release2 = 1 - std::exp(-k / std::clamp(40 * a, 150.0, 1500.0));
        }
        state.attack = 1 - std::exp(-k / a);
        state.release = 1 - std::exp(-k / r);
    }

    void update(const ReEqSettings& settings, float hostOutput) noexcept
    {
        // Upstream retained Eco/HQ for old projects, but explicitly stopped
        // using it when Ivantsov's native-rate decramped filters replaced OS.
        bool autoChanged = !initialized || !juce::exactlyEqual(previous.scale, settings.scale);
        for (size_t i = 0; i < bands.size(); ++i)
        {
            auto& state = bands[i];
            const auto& b = settings.bands[i];
            const auto& old = previous.bands[i];
            const auto changed = !initialized || b != old
                                 || !juce::exactlyEqual(settings.scale, previous.scale);
            if (changed)
            {
                const auto dyn = b.enabled == 2 && b.dynamic && dynamicShape(b.shape);
                const auto entering = dyn && (!state.dynamic || b.shape != old.shape);
                if (!dyn)
                {
                    state.gain = state.fast = state.slow = state.applied = 0;
                    state.rms = {};
                    state.entering = state.pending = false;
                    state.filter.set(design(b, rate, settings.scale), glide);
                }
                else
                {
                    if (entering)
                    {
                        state.frequency = b.frequency; state.q = b.q;
                        state.staticGain = b.gainDb; state.scale = settings.scale;
                        Design d;
                        d.put(b.shape == ReEqShape::lowShelf ? 4 : b.shape == ReEqShape::highShelf ? 3 : 7,
                              state.frequency, 0.5 / state.q,
                              std::pow(10.0, (state.staticGain + state.applied) * state.scale / 40), rate);
                        state.filter.set(d, glide);
                        state.entering = true;
                        state.detector.clear();
                    }
                    detectorDesign(state, b, entering || !initialized);
                }
                state.dynamic = dyn;
                timing(state, b);
                for (size_t group = 0; group < state.groups.size(); ++group)
                    state.groups[group].set(group == static_cast<size_t>(b.group) ? 1 : 0, glide);
                const auto d = design(b, rate, settings.scale);
                for (size_t p = 0; p < frequencies.size(); ++p)
                    state.power[p] = square(d.magnitude(frequencies[p], rate));
                autoChanged = true;
            }
            if (!state.dynamic)
            {
                gainMeters[i].store(0, std::memory_order_relaxed);
                levelMeters[i].store(-150, std::memory_order_relaxed);
            }
        }
        if (autoChanged)
        {
            double sum = 0, weightSum = 0;
            for (size_t p = 0; p < frequencies.size(); ++p)
            {
                double mid = 1, side = 1;
                for (size_t i = 0; i < bands.size(); ++i)
                {
                    auto power = bands[i].power[p];
                    const auto group = settings.bands[i].group;
                    if (group == ReEqGroup::left || group == ReEqGroup::right)
                        power = (power + 1) * 0.5;
                    if (group != ReEqGroup::side) mid *= power;
                    if (group != ReEqGroup::mid) side *= power;
                }
                sum += weights[p] * (0.8 * mid + 0.2 * side);
                weightSum += weights[p];
            }
            gainTarget = sum > 0 && weightSum > 0 ? std::sqrt(weightSum / sum) : 1;
        }
        input.set(amplitude(settings.inputDb), glide);
        output.set(amplitude(settings.outputDb + hostOutput), glide);
        first.set(amplitude(settings.firstGainDb) * (settings.firstPolarity ? -1 : 1), glide);
        second.set(amplitude(settings.secondGainDb) * (settings.secondPolarity ? -1 : 1), glide);
        domain.set(settings.midSide ? 1 : 0, glide);
        limitMix.set(settings.limitOutput ? 1 : 0, glide);
        const auto bandListening = settings.listenBand >= 0 && settings.listenBand < reEqBandCount;
        const auto freeformListening = settings.listenBand == -2;
        const auto listening = bandListening || freeformListening;
        const auto detectorListening = bandListening && settings.listenDetector
            && bands[static_cast<size_t>(settings.listenBand)].dynamic;
        listenMix.set(listening && !detectorListening ? 1 : 0, glide);
        listenGain.set(amplitude(freeformListening ? settings.listenGainDb : 0.0f), glide);
        detectorMix.set(detectorListening ? 1 : 0, glide);
        const auto listenChanged = !initialized || previous.listenBand != settings.listenBand
            || (freeformListening
                && (!juce::exactlyEqual(previous.listenFrequency, settings.listenFrequency)
                    || !juce::exactlyEqual(previous.listenWidthOctaves, settings.listenWidthOctaves)
                    || previous.listenGroup != settings.listenGroup))
            || (bandListening
                && settings.bands[static_cast<size_t>(settings.listenBand)]
                       != previous.bands[static_cast<size_t>(settings.listenBand)]);
        if (listening && listenChanged)
        {
            const auto group = freeformListening ? settings.listenGroup
                : settings.bands[static_cast<size_t>(settings.listenBand)].group;
            listenFilter.set(freeformListening ? freeformListenDesign(settings, rate)
                : listenDesign(settings.bands[static_cast<size_t>(settings.listenBand)], rate), glide);
            for (size_t i = 0; i < listenGroups.size(); ++i)
                listenGroups[i].set(i == static_cast<size_t>(group) ? 1 : 0, glide);
        }
        const auto wanted = std::clamp(static_cast<int>(std::lround(
            bounded(settings.lookaheadMs, 0, 10, 0) * 0.001 * rate)), 0,
            static_cast<int>(delay.size()) - 1);
        if (wanted != delayLength)
        {
            previousDelay = delayLength;
            delayLength = wanted;
            delayMix.snap(0); delayMix.set(1, glide);
        }
        if (!initialized)
        {
            input.snap(input.target); output.snap(output.target);
            first.snap(first.target); second.snap(second.target); domain.snap(domain.target);
            limitMix.snap(limitMix.target); delayMix.snap(1);
            listenGain.snap(listenGain.target);
            for (auto& b : bands)
                for (auto& g : b.groups) g.snap(g.target);
            for (auto& g : listenGroups) g.snap(g.target);
        }
        previous = settings;
        initialized = true;
    }

    std::array<double, 2> dynamicTick(size_t i, const ReEqBand& b, double l, double r) noexcept
    {
        auto& state = bands[i];
        state.detector.tick();
        if (b.group != ReEqGroup::stereo)
        {
            l = b.group == ReEqGroup::mid ? (l + r) * 0.5
                : b.group == ReEqGroup::side ? (l - r) * 0.5
                : b.group == ReEqGroup::right ? r : l;
            r = l;
        }
        l = state.detector.process(l, 0) * state.normalization;
        r = state.detector.process(r, 1) * state.normalization;
        state.rms[0] += state.rmsCoefficient * (l * l - state.rms[0]) + 1.0e-30;
        state.rms[1] += state.rmsCoefficient * (r * r - state.rms[1]) + 1.0e-30;
        if (--state.countdown <= 0)
        {
            state.countdown = dynamicStep;
            const auto level = 10 * std::log10(std::max({state.rms[0], state.rms[1], 1.0e-20})) + 3.0103;
            levelMeters[i].store(static_cast<float>(level), std::memory_order_relaxed);
            const auto over = level - b.thresholdDb;
            auto y = over <= -3 ? 0.0 : over >= 3 ? over : square(over + 3) / 12;
            y *= 1 - 1 / std::max(1.0f, b.ratio);
            const auto want = b.rangeDb < 0 ? -std::min(y, -static_cast<double>(b.rangeDb))
                                            : std::min(y, static_cast<double>(b.rangeDb));
            if (b.autoTiming)
            {
                state.fast += (std::abs(want) > std::abs(state.fast) ? state.attack : state.release)
                              * (want - state.fast);
                state.slow += (std::abs(want) > std::abs(state.slow) ? state.attack2 : state.release2)
                              * (want - state.slow);
                state.gain = std::abs(state.fast) > std::abs(state.slow) ? state.fast : state.slow;
            }
            else
            {
                state.gain += (std::abs(want) > std::abs(state.gain) ? state.attack : state.release)
                              * (want - state.gain);
                state.fast = state.slow = state.gain;
            }
            gainMeters[i].store(static_cast<float>(state.gain), std::memory_order_relaxed);
            auto moving = std::abs(state.gain - state.applied) > 0.01;
            if (!juce::exactlyEqual(state.frequency, static_cast<double>(b.frequency))
                || !juce::exactlyEqual(state.q, static_cast<double>(b.q))
                || !juce::exactlyEqual(state.staticGain, static_cast<double>(b.gainDb))
                || !juce::exactlyEqual(state.scale, static_cast<double>(previous.scale)))
            {
                const auto smoothing = 1 - std::exp(-dynamicStep / (0.01 * rate));
                state.frequency *= std::pow(b.frequency / state.frequency, smoothing);
                state.q *= std::pow(b.q / state.q, smoothing);
                state.staticGain += (b.gainDb - state.staticGain) * smoothing;
                state.scale += (previous.scale - state.scale) * smoothing;
                if (std::abs(b.frequency / state.frequency - 1) < 0.0005
                    && std::abs(b.q / state.q - 1) < 0.0005
                    && std::abs(b.gainDb - state.staticGain) < 0.005
                    && std::abs(previous.scale - state.scale) < 0.0005)
                {
                    state.frequency = b.frequency; state.q = b.q;
                    state.staticGain = b.gainDb; state.scale = previous.scale;
                }
                moving = true;
            }
            if (state.entering && state.filter.position >= 1) state.entering = false;
            if (state.entering) { state.pending = state.pending || moving; moving = false; }
            else if (state.pending) { moving = true; state.pending = false; }
            if (moving)
            {
                state.applied = state.gain;
                Design d;
                d.put(b.shape == ReEqShape::lowShelf ? 4 : b.shape == ReEqShape::highShelf ? 3 : 7,
                      state.frequency, 0.5 / state.q,
                      std::pow(10.0, (state.staticGain + state.applied) * state.scale / 40), rate);
                state.filter.set(d, dynamicStep);
            }
        }
        return { l, r };
    }
};

ReEqProcessor::ReEqProcessor() : impl(std::make_unique<Impl>()) {}
ReEqProcessor::~ReEqProcessor() = default;

void ReEqProcessor::prepare(const juce::dsp::ProcessSpec& spec)
{
    impl->rate = bounded(spec.sampleRate, 8000, 768000, 48000);
    impl->glide = std::max(1, static_cast<int>(std::lround(0.03 * impl->rate)));
    impl->delay.resize(static_cast<size_t>(std::ceil(0.01 * impl->rate)) + 1);
    impl->gainSmoothing = 1 - std::exp(-1 / (0.03 * impl->rate));
    for (size_t i = 0; i < impl->frequencies.size(); ++i)
    {
        const auto f = 20 * std::pow(1000.0, static_cast<double>(i) / (gainPoints - 1));
        impl->frequencies[i] = f;
        const auto w = 2 * pi * f / 48000;
        impl->weights[i] = f >= impl->rate * 0.5 ? 0
            : biquadPower(1.53512485958697, -2.69169618940638, 1.19839281085285,
                          -1.69065929318241, 0.73248077421585, w)
              * biquadPower(1, -2, 1, -1.99004745483398, 0.99007225036621, w);
    }
    impl->prepared = true;
    reset();
}

void ReEqProcessor::reset()
{
    impl->bands = {};
    impl->listenFilter = {};
    impl->listenGroups = {};
    std::fill(impl->delay.begin(), impl->delay.end(), std::array<double, 6> {});
    impl->input.snap(1); impl->output.snap(1); impl->first.snap(1); impl->second.snap(1);
    impl->domain.snap(0); impl->listenMix.snap(0); impl->detectorMix.snap(0);
    impl->listenGain.snap(1);
    impl->limitMix.snap(0); impl->delayMix.snap(1);
    impl->write = impl->delayLength = impl->previousDelay = 0;
    impl->latency.store(impl->prepared ? static_cast<int>(std::lround(0.01 * impl->rate)) : 0,
                        std::memory_order_relaxed);
    impl->gainCorrection = impl->gainTarget = 1;
    impl->initialized = false;
    for (size_t i = 0; i < impl->bands.size(); ++i)
    {
        impl->gainMeters[i].store(0, std::memory_order_relaxed);
        impl->levelMeters[i].store(-150, std::memory_order_relaxed);
    }
}

int ReEqProcessor::latencySamples() const noexcept
{
    return impl->latency.load(std::memory_order_relaxed);
}

double ReEqProcessor::bandMagnitude(const ReEqBand& band, double rate, double frequency, float scale)
{
    rate = bounded(rate, 8000, 768000, 48000);
    return design(band, rate, bounded(scale, 0, 2, 1)).magnitude(
        bounded(frequency, 0, 768000, 1000), rate);
}

float ReEqProcessor::dynamicGainDb(int band) const noexcept
{
    return band >= 0 && band < reEqBandCount
        ? impl->gainMeters[static_cast<size_t>(band)].load(std::memory_order_relaxed) : 0;
}

float ReEqProcessor::detectorLevelDb(int band) const noexcept
{
    return band >= 0 && band < reEqBandCount
        ? impl->levelMeters[static_cast<size_t>(band)].load(std::memory_order_relaxed) : -150;
}

void ReEqProcessor::process(juce::AudioBuffer<float>& buffer, const ReEqSettings& settings,
                            const ProcessEnvironment& environment, float hostOutputDb)
{
    if (!impl->prepared || buffer.getNumChannels() == 0) return;
    juce::ScopedNoDenormals noDenormals;
    auto& p = *impl;
    p.update(settings, hostOutputDb);
    const auto stereo = buffer.getNumChannels() > 1;
    auto* left = buffer.getWritePointer(0);
    auto* right = stereo ? buffer.getWritePointer(1) : nullptr;
    const auto* sidechain = environment.sidechain;
    const auto hasSidechain = sidechain != nullptr && sidechain->getNumChannels() > 0;
    const auto count = buffer.getNumSamples();
    const auto delaySize = static_cast<int>(p.delay.size());
    for (int sample = 0; sample < count; ++sample)
    {
        const auto inGain = p.input.next();
        auto l = (std::isfinite(left[sample]) ? left[sample] : 0.0f) * inGain;
        auto r = stereo ? (std::isfinite(right[sample]) ? right[sample] : 0.0f) * inGain : l;
        auto externalL = l, externalR = r;
        if (hasSidechain && sample < sidechain->getNumSamples())
        {
            externalL = sidechain->getSample(0, sample);
            externalR = sidechain->getSample(sidechain->getNumChannels() > 1 ? 1 : 0, sample);
            if (!std::isfinite(externalL)) externalL = 0;
            if (!std::isfinite(externalR)) externalR = 0;
        }
        auto& incoming = p.delay[static_cast<size_t>(p.write)];
        incoming = { l, r, externalL, externalR, 0, 0 };
        const auto fixedDelay = p.latency.load(std::memory_order_relaxed);
        const auto blend = p.delayMix.next();
        const auto& detectorOld = p.delay[static_cast<size_t>(
            (p.write - (fixedDelay - p.previousDelay) + delaySize) % delaySize)];
        const auto& detectorNew = p.delay[static_cast<size_t>(
            (p.write - (fixedDelay - p.delayLength) + delaySize) % delaySize)];
        std::array<double, 2> detectorSignal {};
        for (size_t i = 0; i < p.bands.size(); ++i)
        {
            if (!p.bands[i].dynamic) continue;
            const auto& b = settings.bands[i];
            const auto offset = b.externalSidechain && hasSidechain ? 2u : 0u;
            const auto dl = detectorOld[offset]
                            + (detectorNew[offset] - detectorOld[offset]) * blend;
            const auto dr = detectorOld[offset + 1]
                            + (detectorNew[offset + 1] - detectorOld[offset + 1]) * blend;
            const auto detected = p.dynamicTick(i, b, dl, dr);
            if (settings.listenBand == static_cast<int>(i)) detectorSignal = detected;
        }
        incoming[4] = detectorSignal[0]; incoming[5] = detectorSignal[1];
        const auto& main = p.delay[static_cast<size_t>(
            (p.write - fixedDelay + delaySize) % delaySize)];
        l = main[0]; r = main[1];
        const auto& a = p.delay[static_cast<size_t>((p.write - p.previousDelay + delaySize) % delaySize)];
        const auto& b = p.delay[static_cast<size_t>((p.write - p.delayLength + delaySize) % delaySize)];
        detectorSignal = { a[4] + (b[4] - a[4]) * blend,
                           a[5] + (b[5] - a[5]) * blend };
        p.write = (p.write + 1) % delaySize;
        const auto dryL = l, dryR = r;
        for (auto& state : p.bands)
        {
            state.filter.tick();
            if (state.filter.current.count == 0 && !state.filter.current.first)
            {
                for (auto& g : state.groups) g.next();
                continue;
            }
            std::array<double, 5> groups;
            for (size_t i = 0; i < groups.size(); ++i) groups[i] = state.groups[i].next();
            const auto wet = routed(state.filter, l, r, groups);
            l = wet[0]; r = wet[1];
        }
        p.listenFilter.tick();
        std::array<double, 5> groups;
        for (size_t i = 0; i < groups.size(); ++i) groups[i] = p.listenGroups[i].next();
        const auto listen = routed(p.listenFilter, dryL, dryR, groups, true);
        const auto listenAmount = p.listenMix.next();
        const auto listenTrim = p.listenGain.next();
        l += listenAmount * (listen[0] * listenTrim - l);
        r += listenAmount * (listen[1] * listenTrim - r);
        const auto firstGain = p.first.next(), secondGain = p.second.next(), ms = p.domain.next();
        const auto mid = (l + r) * 0.5 * firstGain;
        const auto side = (l - r) * 0.5 * secondGain;
        const auto outGain = p.output.next();
        const auto outL = ((1 - ms) * l * firstGain + ms * (mid + side)) * outGain;
        const auto outR = ((1 - ms) * r * secondGain + ms * (mid - side)) * outGain;
        const auto correction = settings.autoGain ? p.gainTarget : 1;
        p.gainCorrection += (correction - p.gainCorrection) * p.gainSmoothing;
        l = outL * p.gainCorrection; r = outR * p.gainCorrection;
        const auto limiting = p.limitMix.next();
        l += limiting * (std::clamp(l, -1.0, 1.0) - l);
        r += limiting * (std::clamp(r, -1.0, 1.0) - r);
        const auto detectorAmount = p.detectorMix.next();
        l += detectorAmount * (detectorSignal[0] - l);
        r += detectorAmount * (detectorSignal[1] - r);
        // A mono host represents duplicated mono, not an artificial right-only
        // signal; fold the two virtual channels back to preserve side cancellation.
        const auto result = stereo ? l : (l + r) * 0.5;
        left[sample] = finiteFloat(result);
        if (stereo) right[sample] = finiteFloat(r);
    }
}
} // namespace megadsp
