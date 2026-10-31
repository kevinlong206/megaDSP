// ReEQ2 reference mathematics: (c) 2021-2026 Justin Johnson and
// (c) 2025-2026 Yuriy Ivantsov; MIT, src/third_party/reeq2/LICENSE.
#include "modules/ReEqProcessor.h"

#include <cmath>
#include <complex>

namespace megadsp
{
namespace
{
class ReEqDspTests final : public juce::UnitTest
{
public:
    ReEqDspTests() : juce::UnitTest("Native ReEQ2 DSP", "megaDSP") {}

    void runTest() override
    {
        ProcessEnvironment environment;
        ReEqSettings settings;
        settings.native = true;
        auto& band = settings.bands[0];
        band.enabled = 2;
        band.frequency = 2600;
        band.q = 1.3f;
        band.gainDb = 9;
        band.slope = 3;

        beginTest("All fourteen native shapes match independent upstream reference responses");
        // ReJJ 281bf08: evaluate the published state-space coefficients with
        // H(z)=(D-EQ/(z+1)+CP/(z-1))/(1-Q/(z+1)+P/(z-1)).
        // These are reference numbers, not results from bandMagnitude().
        constexpr std::array<double, 14> reference {
            1.0961590450731, 1.17022247164408, 0.999920672100273,
            0.91979597658885, 3.06413922543638, 0.0147408405805186,
            0.0125956145756515, 0.960356351879189, 0.278775317066083,
            3.33132488445973, 1.06069928972571, 1.0,
            0.985942568107365, 2.74053424740027
        };
        for (size_t shape = 0; shape < reference.size(); ++shape)
        {
            band.shape = static_cast<ReEqShape>(shape);
            expectWithinAbsoluteError(ReEqProcessor::bandMagnitude(band, 48000, 7777),
                                      reference[shape], 2.0e-6);
            ReEqProcessor processor;
            processor.prepare({ 48000, 256, 2 });
            const auto measured = toneGain(processor, settings, environment, 48000, 7777);
            expectWithinAbsoluteError(measured, reference[shape], 0.002,
                                      "shape " + juce::String(static_cast<int>(shape)));
        }

        beginTest("Impulse response and displayed digital response agree near Nyquist");
        band.shape = ReEqShape::bell; band.frequency = 18000; band.gainDb = 12; band.q = 2;
        ReEqProcessor impulseProcessor;
        impulseProcessor.prepare({ 44100, 256, 2 });
        juce::AudioBuffer<float> block(2, 256);
        for (int i = 0; i < 32; ++i)
        {
            block.clear(); impulseProcessor.process(block, settings, environment);
        }
        std::complex<double> response {};
        int time = 0;
        for (int chunk = 0; chunk < 64; ++chunk)
        {
            block.clear();
            if (chunk == 0) { block.setSample(0, 0, 1); block.setSample(1, 0, 1); }
            impulseProcessor.process(block, settings, environment);
            for (int i = 0; i < block.getNumSamples(); ++i, ++time)
                response += static_cast<double>(block.getSample(0, i))
                            * std::polar(1.0, -2 * juce::MathConstants<double>::pi
                                               * 19000 * time / 44100);
        }
        expectWithinAbsoluteError(std::abs(response),
                                  ReEqProcessor::bandMagnitude(band, 44100, 19000), 0.0001);
        band.frequency = 30000;
        expectWithinAbsoluteError(ReEqProcessor::bandMagnitude(band, 22050, 30000),
                                  ReEqProcessor::bandMagnitude(band, 22050, 11025), 1.0e-12);
        expect(std::isfinite(ReEqProcessor::bandMagnitude(band, 8000, 30000)));

        beginTest("Odd and even Butterworth slopes reproduce their independent DC/asymptotic behavior");
        band.shape = ReEqShape::lowCutButterworth; band.frequency = 1000; band.gainDb = 0;
        for (int slope = 0; slope < 20; ++slope)
        {
            band.slope = slope;
            const auto below = ReEqProcessor::bandMagnitude(band, 192000, 100);
            const auto octave = ReEqProcessor::bandMagnitude(band, 192000, 200);
            expect(below > 0 && std::isfinite(below));
            const auto dbPerOctave = 20 * std::log10(octave / below);
            expectWithinAbsoluteError(dbPerOctave, 6.020599913 * (slope + 1), 0.15);
        }

        beginTest("Side routing preserves mono fold-down and left routing preserves right input");
        settings = {};
        settings.bands[0].enabled = 2;
        settings.bands[0].frequency = 1000;
        settings.bands[0].gainDb = 12;
        settings.bands[0].group = ReEqGroup::side;
        ReEqProcessor routedProcessor;
        routedProcessor.prepare({ 48000, 256, 2 });
        const auto sideMono = toneGain(routedProcessor, settings, environment, 48000, 1000);
        expectWithinAbsoluteError(sideMono, 1.0, 0.0001);
        settings.bands[0].group = ReEqGroup::left;
        routedProcessor.reset();
        const auto rightGain = toneGain(routedProcessor, settings, environment, 48000, 1000, 1);
        expectWithinAbsoluteError(rightGain, 1.0, 0.0001);
        const auto leftGain = toneGain(routedProcessor, settings, environment, 48000, 1000);
        expect(leftGain > 3.8);

        beginTest("Mono processing matches duplicated-stereo fold-down for every group");
        for (int group = 0; group < 5; ++group)
        {
            settings.bands[0].group = static_cast<ReEqGroup>(group);
            ReEqProcessor mono, stereo;
            mono.prepare({ 48000, 256, 1 }); stereo.prepare({ 48000, 256, 2 });
            juce::AudioBuffer<float> one(1, 256), two(2, 256);
            double error = 0;
            for (int chunk = 0; chunk < 24; ++chunk)
            {
                for (int i = 0; i < 256; ++i)
                {
                    const auto x = static_cast<float>(0.1 * std::sin((chunk * 256 + i) * 0.13));
                    one.setSample(0, i, x); two.setSample(0, i, x); two.setSample(1, i, x);
                }
                mono.process(one, settings, environment); stereo.process(two, settings, environment);
                for (int i = 0; i < 256; ++i)
                    error = std::max(error, std::abs(static_cast<double>(one.getSample(0, i))
                        - 0.5 * (two.getSample(0, i) + two.getSample(1, i))));
            }
            expect(error < 1.0e-6);
        }

        beginTest("Lookahead Off is immediate and selected lookahead matches reported latency");
        for (const auto rate : { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            ReEqProcessor processor;
            processor.prepare({ rate, 256, 2 });
            expectEquals(processor.latencySamples(), 0);
            for (const auto lookahead : { 0.0f, 1.0f, 5.0f, 10.0f })
            {
                processor.setLookahead(lookahead);
                processor.reset();
                settings = {}; settings.lookaheadMs = lookahead;
                const auto expected = static_cast<int>(std::lround(lookahead * 0.001 * rate));
                expectEquals(processor.latencySamples(), expected);
                int position = 0, peak = -1;
                for (int chunk = 0; chunk < 12; ++chunk)
                {
                    block.clear();
                    if (chunk == 0) { block.setSample(0, 0, 1); block.setSample(1, 0, 1); }
                    processor.process(block, settings, environment);
                    for (int i = 0; i < 256; ++i, ++position)
                        if (std::abs(block.getSample(0, i)) > 0.99f) peak = position;
                }
                expectEquals(peak, expected);
                expectEquals(processor.latencySamples(), expected);
            }
        }

        beginTest("Lookahead changes crossfade prepared audio taps without resetting the signal");
        {
            ReEqProcessor processor;
            processor.prepare({ 48000, 256, 2 });
            settings = {};
            constexpr std::array<float, 5> lookahead { 0, 1, 5, 10, 0 };
            float last = 0.0f;
            double maximumStep = 0.0;
            for (int chunk = 0; chunk < 100; ++chunk)
            {
                settings.lookaheadMs = lookahead[size_t(chunk / 20)];
                for (int i = 0; i < 256; ++i)
                {
                    block.setSample(0, i, float(0.1 * std::sin((chunk * 256 + i) * 0.05)));
                    block.setSample(1, i, 0.0f);
                }
                processor.process(block, settings, environment);
                for (int i = 0; i < 256; ++i)
                {
                    const auto sample = block.getSample(0, i);
                    expect(std::isfinite(sample));
                    maximumStep = std::max(maximumStep, std::abs(double(sample - last)));
                    last = sample;
                }
                expectWithinAbsoluteError(block.getMagnitude(1, 0, 256), 0.0f, 0.000001f);
            }
            expect(maximumStep < 0.006);
        }

        beginTest("Dynamics uses source soft knee, linked louder-channel RMS, and signed range");
        settings = {};
        auto& dyn = settings.bands[0];
        dyn.enabled = 2; dyn.dynamic = true; dyn.frequency = 1000;
        dyn.thresholdDb = -24; dyn.ratio = 4; dyn.rangeDb = -6;
        dyn.attackMs = 0.05f; dyn.releaseMs = 20;
        ReEqProcessor dynamics;
        dynamics.prepare({ 48000, 256, 2 });
        const auto compressed = toneGain(dynamics, settings, environment, 48000, 1000);
        expect(compressed < 0.56 && compressed > 0.48);
        expectWithinAbsoluteError(dynamics.dynamicGainDb(0), -6.0f, 0.05f);
        expectWithinAbsoluteError(dynamics.detectorLevelDb(0), -12.0412f, 0.6f);
        dynamics.reset();
        toneGain(dynamics, settings, environment, 48000, 1000, 1, true);
        expectWithinAbsoluteError(dynamics.detectorLevelDb(0), -12.0412f, 0.6f);
        dyn.rangeDb = 6;
        dynamics.reset();
        const auto expanded = toneGain(dynamics, settings, environment, 48000, 1000);
        expect(expanded > 1.8);
        dyn.ratio = 1;
        dynamics.reset();
        expectWithinAbsoluteError(toneGain(dynamics, settings, environment, 48000, 1000), 1.0, 0.001);

        beginTest("External sidechain fallback, one-channel sidechain, and unlinked detector are functional");
        dyn.ratio = 4; dyn.rangeDb = -6; dyn.externalSidechain = true;
        dynamics.reset();
        expect(toneGain(dynamics, settings, environment, 48000, 1000) < 0.56);
        juce::AudioBuffer<float> sidechain(1, 256);
        sidechain.clear();
        environment.sidechain = &sidechain;
        dynamics.reset();
        expectWithinAbsoluteError(toneGain(dynamics, settings, environment, 48000, 1000), 1.0, 0.002);
        for (int i = 0; i < 256; ++i)
            sidechain.setSample(0, i, static_cast<float>(0.5 * std::sin(2 * juce::MathConstants<double>::pi * i / 48)));
        dynamics.reset();
        expect(toneGain(dynamics, settings, environment, 48000, 1000) < 0.6);
        environment.sidechain = nullptr;
        dyn.externalSidechain = false; dyn.detectorLinked = false;
        dyn.detectorFrequency = 12000; dyn.detectorWidth = 0.25f;
        dynamics.reset();
        expectWithinAbsoluteError(toneGain(dynamics, settings, environment, 48000, 1000), 1.0, 0.01);
        dyn.detectorFrequency = 1000;
        dynamics.reset();
        expect(toneGain(dynamics, settings, environment, 48000, 1000) < 0.6);

        beginTest("Lookahead anticipates attacks at the corresponding delayed onset");
        ReEqSettings noLook, ahead;
        auto& transient = noLook.bands[0];
        transient.enabled = 2; transient.dynamic = true; transient.frequency = 1000;
        transient.thresholdDb = -30; transient.ratio = 20; transient.rangeDb = -18;
        transient.attackMs = 3; transient.releaseMs = 80;
        ahead = noLook; ahead.lookaheadMs = 10;
        ReEqProcessor delayedDetector, earlyDetector;
        delayedDetector.prepare({ 48000, 256, 2 });
        earlyDetector.prepare({ 48000, 256, 2 });
        juce::AudioBuffer<float> earlyBlock(2, 256);
        double offEnergy = 0, aheadEnergy = 0;
        for (int chunk = 0; chunk < 20; ++chunk)
        {
            for (int i = 0; i < 256; ++i)
            {
                const auto t = chunk * 256 + i;
                const auto x = t < 2048 ? 0.0f : static_cast<float>(
                    0.7 * std::sin(2 * juce::MathConstants<double>::pi * (t - 2048) / 48));
                for (int ch = 0; ch < 2; ++ch)
                {
                    block.setSample(ch, i, x); earlyBlock.setSample(ch, i, x);
                }
            }
            delayedDetector.process(block, noLook, environment);
            earlyDetector.process(earlyBlock, ahead, environment);
            for (int i = 0; i < 256; ++i)
            {
                const auto t = chunk * 256 + i;
                if (t >= 2048 && t < 2240)
                    offEnergy += std::pow(block.getSample(0, i), 2);
                if (t >= 2528 && t < 2720)
                    aheadEnergy += std::pow(earlyBlock.getSample(0, i), 2);
                if (t < 2048)
                    expect(std::abs(block.getSample(0, i)) < 1.0e-8f);
                if (t < 2528)
                    expect(std::abs(earlyBlock.getSample(0, i)) < 1.0e-8f);
            }
        }
        expect(aheadEnergy < offEnergy * 0.8);

        beginTest("Auto dual-envelope timing releases after sustained input");
        dyn.autoTiming = true; dyn.detectorLinked = true;
        dynamics.reset();
        toneGain(dynamics, settings, environment, 48000, 1000);
        const auto heldReduction = dynamics.dynamicGainDb(0);
        expect(heldReduction < -5.8f);
        for (int i = 0; i < 20; ++i)
        {
            block.clear(); dynamics.process(block, settings, environment);
        }
        const auto earlyRelease = dynamics.dynamicGainDb(0);
        expect(earlyRelease > heldReduction && earlyRelease < -1);
        for (int i = 0; i < 400; ++i)
        {
            block.clear(); dynamics.process(block, settings, environment);
        }
        expect(std::abs(dynamics.dynamicGainDb(0)) < 0.1f);

        beginTest("Output trims, domain polarity, source limiter, and curve-derived auto gain work");
        settings = {};
        settings.inputDb = -6; settings.outputDb = 3; settings.firstGainDb = 3;
        ReEqProcessor gains;
        gains.prepare({ 48000, 256, 2 });
        expectWithinAbsoluteError(toneGain(gains, settings, environment, 48000, 1000), 1.0, 0.001);
        expectWithinAbsoluteError(toneGain(gains, settings, environment, 48000, 1000, 1),
                                  std::pow(10.0, -3.0 / 20), 0.001);
        settings = {};
        settings.midSide = true; settings.secondPolarity = true;
        gains.reset();
        // Duplicated mono contains no side: reversing Side must not alter it.
        expectWithinAbsoluteError(toneGain(gains, settings, environment, 48000, 1000), 1.0, 0.001);
        settings = {};
        settings.bands[0].enabled = 2; settings.bands[0].shape = ReEqShape::highShelf;
        settings.bands[0].frequency = 200; settings.bands[0].gainDb = 12;
        gains.reset();
        const auto boosted = toneGain(gains, settings, environment, 48000, 5000);
        settings.autoGain = true;
        const auto compensated = toneGain(gains, settings, environment, 48000, 5000);
        expect(compensated < boosted * 0.4 && compensated > boosted * 0.2);
        settings = {}; settings.outputDb = 30; settings.limitOutput = true;
        gains.reset();
        for (int i = 0; i < 8; ++i)
        {
            for (int j = 0; j < 256; ++j)
            {
                block.setSample(0, j, 0.25f); block.setSample(1, j, -0.25f);
            }
            gains.process(block, settings, environment);
        }
        expectWithinAbsoluteError(block.getSample(0, 255), 1.0f, 0.0001f);
        expectWithinAbsoluteError(block.getSample(1, 255), -1.0f, 0.0001f);

        beginTest("Band listen isolates the focused region and detector listen ignores output trim");
        settings = {};
        settings.bands[0].enabled = 2; settings.bands[0].frequency = 1000;
        settings.listenBand = 0;
        gains.reset();
        const auto focused = toneGain(gains, settings, environment, 48000, 1000);
        gains.reset();
        const auto rejected = toneGain(gains, settings, environment, 48000, 12000);
        expect(focused > 0.8 && rejected < 0.01);
        settings.bands[0].dynamic = true; settings.bands[0].ratio = 1;
        settings.listenDetector = true; settings.outputDb = -30;
        gains.reset();
        expectWithinAbsoluteError(toneGain(gains, settings, environment, 48000, 1000), 1.0, 0.003);

        beginTest("Freeform audition owns its passband and trim even when all sixteen bands are occupied");
        settings = {};
        for (auto& b : settings.bands)
        {
            b.enabled = 2; b.shape = ReEqShape::lowShelf;
            b.frequency = 3000; b.gainDb = -3;
        }
        settings.listenBand = -2; settings.listenFrequency = 1000;
        settings.listenWidthOctaves = 2; settings.listenGainDb = -6;
        gains.reset();
        const auto freeformCenter = toneGain(gains, settings, environment, 48000, 1000);
        expect(freeformCenter > 0.48 && freeformCenter < 0.51);
        expect(toneGain(gains, settings, environment, 48000, 12000) < 0.01);
        settings.listenGroup = ReEqGroup::right;
        expect(toneGain(gains, settings, environment, 48000, 1000) < 0.001);
        expectWithinAbsoluteError(toneGain(gains, settings, environment, 48000, 1000, 1),
                                  freeformCenter, 0.001);
        settings.listenFrequency = 12000;
        settings.listenWidthOctaves = 0.5f;
        settings.listenGainDb = 0;
        expect(toneGain(gains, settings, environment, 48000, 1000, 1) < 0.01);
        expect(toneGain(gains, settings, environment, 48000, 12000, 1) > 0.5);

        beginTest("Topology, group, domain and polarity transitions stay continuous");
        settings = {};
        settings.bands[0].enabled = 2; settings.bands[0].gainDb = 9;
        settings.bands[0].frequency = 900; settings.bands[0].q = 0.7071f;
        ReEqProcessor changing;
        changing.prepare({ 48000, 256, 2 });
        float last = 0; double maximumStep = 0;
        for (int chunk = 0; chunk < 100; ++chunk)
        {
            if (chunk == 25)
            {
                settings.bands[0].shape = ReEqShape::highCutButterworth;
                settings.bands[0].slope = 19; settings.bands[0].group = ReEqGroup::side;
            }
            if (chunk == 50) { settings.midSide = true; settings.firstPolarity = true; }
            if (chunk == 75) settings.bands[0].enabled = 0;
            for (int i = 0; i < 256; ++i)
            {
                const auto x = static_cast<float>(0.1 * std::sin((chunk * 256 + i) * 0.05));
                block.setSample(0, i, x); block.setSample(1, i, x * 0.5f);
            }
            changing.process(block, settings, environment);
            for (int i = 0; i < 256; ++i)
            {
                const auto x = block.getSample(0, i);
                expect(std::isfinite(x));
                maximumStep = std::max(maximumStep, std::abs(static_cast<double>(x - last)));
                last = x;
            }
        }
        expect(maximumStep < 0.04);

        beginTest("Prepared sixteen-band extremes remain finite at supported sample rates");
        for (const auto rate : { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            ReEqProcessor processor;
            processor.prepare({ rate, 256, 2 });
            settings = {};
            for (size_t i = 0; i < settings.bands.size(); ++i)
            {
                auto& b = settings.bands[i];
                b.enabled = 2; b.shape = static_cast<ReEqShape>(i % 14);
                b.frequency = i % 2 == 0 ? 10.0f : 30000.0f;
                b.q = i % 2 == 0 ? 0.1f : 40.0f;
                b.slope = 19; b.gainDb = i % 2 == 0 ? -18.0f : 18.0f;
            }
            for (int chunk = 0; chunk < 24; ++chunk)
            {
                block.clear(); block.setSample(0, 0, 0.0001f);
                processor.process(block, settings, environment);
                bool finite = true;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 256; ++i)
                        finite = finite && std::isfinite(block.getSample(ch, i));
                expect(finite);
            }
        }
    }

private:
    static double toneGain(ReEqProcessor& processor, const ReEqSettings& settings,
                           const ProcessEnvironment& environment, double rate,
                           double frequency, int channel = 0, bool rightOnly = false)
    {
        juce::AudioBuffer<float> block(2, 256);
        std::complex<double> sum {};
        int samples = 0;
        for (int chunk = 0; chunk < 192; ++chunk)
        {
            for (int i = 0; i < 256; ++i)
            {
                const auto phase = 2 * juce::MathConstants<double>::pi
                                   * frequency * (chunk * 256 + i) / rate;
                const auto x = static_cast<float>(0.25 * std::sin(phase));
                block.setSample(0, i, rightOnly ? 0.0f : x); block.setSample(1, i, x);
            }
            processor.process(block, settings, environment);
            if (chunk < 96) continue;
            for (int i = 0; i < 256; ++i)
            {
                const auto phase = 2 * juce::MathConstants<double>::pi
                                   * frequency * (chunk * 256 + i) / rate;
                sum += static_cast<double>(block.getSample(channel, i)) * std::polar(1.0, -phase);
                ++samples;
            }
        }
        return std::abs(sum) * 2 / (samples * 0.25);
    }
};

static ReEqDspTests reEqDspTests;
} // namespace
} // namespace megadsp
