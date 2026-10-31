#include "PluginProcessor.h"
#include "modules/Equalizer.h"
#include "SnapshotState.h"

#include <cmath>
#include <limits>

namespace
{
class ReEqStateTests final : public juce::UnitTest
{
public:
    ReEqStateTests() : juce::UnitTest("ReEQ2 state compatibility", "megaDSP") {}

    void runTest() override
    {
        using namespace megadsp;
        juce::ScopedJuceInitialiser_GUI gui;
        juce::ScopedNoDenormals noDenormals;

        beginTest("Sixteen bands round trip and malformed values are bounded");
        ReEqSettings settings;
        settings.native = true;
        settings.inputDb = -3.0f;
        settings.outputDb = 1.5f;
        settings.midSide = true;
        settings.lookaheadMs = 5.0f;
        settings.showPiano = true;
        settings.fftOrder = 14;
        settings.scale = 1.25f;
        for (int i = 0; i < reEqBandCount; ++i)
        {
            auto& b = settings.bands[size_t(i)];
            b.enabled = i % 3;
            b.shape = ReEqShape(i % 14);
            b.group = ReEqGroup(i % 5);
            b.frequency = 20.0f * std::pow(1.5f, float(i));
            b.gainDb = float(i - 8);
            b.q = 0.3f + float(i);
            b.slope = i;
            b.dynamic = true;
            b.detectorLinked = false;
            b.detectorFrequency = 300.0f + 50.0f * float(i);
            b.detectorWidth = 0.5f + 0.1f * float(i);
            b.externalSidechain = true;
            b.autoTiming = i % 2 == 0;
        }
        expect(ReEqSettings::fromValueTree(settings.toValueTree()) == settings);
        AtomicReEqSettings atomic;
        atomic.store(settings);
        expect(atomic.load() == settings);
        auto malformed = settings.toValueTree();
        malformed.setProperty("fftOrder", 999, nullptr);
        malformed.setProperty(
            "lookaheadMs", std::numeric_limits<double>::quiet_NaN(), nullptr);
        malformed.getChild(0).setProperty("frequency", -800, nullptr);
        malformed.getChild(0).setProperty("q", 0, nullptr);
        malformed.getChild(0).setProperty("group", 300, nullptr);
        const auto repaired = ReEqSettings::fromValueTree(malformed);
        expectEquals(repaired.fftOrder, 14);
        expectEquals(repaired.lookaheadMs, 0.0f);
        expectEquals(repaired.bands[0].frequency, 10.0f);
        expect(repaired.bands[0].q > 0.0f);
        expect(int(repaired.bands[0].group) <= 4);
        settings.listenBand = 15;
        settings.listenDetector = true;
        const auto audition = ReEqSettings::fromValueTree(settings.toValueTree());
        expectEquals(audition.listenBand, -1);
        expect(!audition.listenDetector);
        settings.listenBand = -1;
        settings.listenDetector = false;

        beginTest("Project, snapshots, undo and reordering retain independent EQs");
        auto processor = std::make_unique<MegaDSPAudioProcessor>();
        processor->prepareToPlay(48000.0, 256);
        processor->addModule(ModuleType::equalizer);
        processor->addModule(ModuleType::delay);
        expectEquals(processor->getParameters().size(), 114);
        expectEquals(int(ModuleType::equalizer), 1);
        expect(processor->reEqSettings(0).native);
        processor->setReEqSettings(0, settings);
        processor->storeSnapshot(1, false);
        auto changed = settings;
        changed.bands[15].gainDb = 11.0f;
        processor->setReEqSettings(0, changed);
        expect(processor->undo());
        expect(processor->reEqSettings(0) == settings);
        expect(processor->redo());
        expect(processor->reEqSettings(0) == changed);
        processor->recallSnapshot(0);
        expect(processor->reEqSettings(0) == settings);
        processor->recallSnapshot(1);
        expect(processor->reEqSettings(0) == changed);
        const auto* prepared = processor->getRack().activeModuleInstance(0);
        processor->moveSlot(0, 1);
        expect(processor->getRack().activeModuleInstance(1) == prepared);
        expect(processor->reEqSettings(1) == changed);
        juce::MemoryBlock state;
        processor->getStateInformation(state);
        auto restored = std::make_unique<MegaDSPAudioProcessor>();
        restored->prepareToPlay(48000.0, 256);
        restored->setStateInformation(state.getData(), int(state.getSize()));
        expect(restored->reEqSettings(1) == changed);
        expectEquals(restored->getLatencySamples(), 240);
        restored->recallSnapshot(0);
        expect(restored->reEqSettings(0) == settings);
        expectEquals(restored->getLatencySamples(), 240);
        restored->recallSnapshot(1);
        restored->removeSlot(0);
        expect(restored->reEqSettings(0) == changed);
        restored->clearSlot(0);
        expect(!restored->reEqSettings(0).native);
        restored->addModule(ModuleType::equalizer);
        expect(restored->reEqSettings(0).native);
        expectEquals(restored->reEqSettings(0).bands[15].enabled, 0);

        beginTest("Schema 13 EQ keeps its exact original filters and zero latency");
        processor->recallSnapshot(0);
        processor->getStateInformation(state);
        auto xml = juce::AudioProcessor::getXmlFromBinary(
            state.getData(), int(state.getSize()));
        expect(xml != nullptr);
        if (xml == nullptr)
            return;
        auto legacy = juce::ValueTree::fromXml(*xml);
        legacy.setProperty("schemaVersion", 13, nullptr);
        for (auto snapshot : legacy.getChildWithName("SNAPSHOTS"))
            for (auto slot : snapshot)
            {
                auto eq = slot.getChildWithName("REEQ2");
                if (eq.isValid())
                    slot.removeChild(eq, nullptr);
            }
        auto legacyEq = legacy.getChildWithName("SNAPSHOTS")
            .getChild(0).getChildWithName("SLOT");
        const ControlValues legacyValues {
            0.31f, 0.65f, 0.40f, 0.55f, 0.28f, 0.60f,
            0.42f, 0.74f, 0.32f, 0.51f, 0.50f, 0.0f
        };
        for (int i = 0; i < controlsPerSlot; ++i)
            legacyEq.setProperty(
                "control" + juce::String(i + 1), legacyValues[size_t(i)], nullptr);
        auto legacyXml = legacy.createXml();
        juce::AudioProcessor::copyXmlToBinary(*legacyXml, state);
        restored->setStateInformation(state.getData(), int(state.getSize()));
        expect(!restored->reEqSettings(0).native);
        expectEquals(restored->getRack().activeModuleInstance(0)->latencySamples(), 0);
        restored->removeSlot(1);
        EqualizerModule original;
        original.prepare({ 48000.0, 256, 2 });
        ControlValues controls {};
        const auto legacySlot = legacy.getChildWithName("SNAPSHOTS")
            .getChild(0).getChildWithName("SLOT");
        juce::MemoryBlock restoredState;
        restored->getStateInformation(restoredState);
        const auto restoredXml = juce::AudioProcessor::getXmlFromBinary(
            restoredState.getData(), int(restoredState.getSize()));
        const auto restoredSlot = juce::ValueTree::fromXml(*restoredXml)
            .getChildWithName("SNAPSHOTS").getChild(0).getChildWithName("SLOT");
        for (int i = 0; i < controlsPerSlot; ++i)
        {
            controls[size_t(i)] = float(
                legacySlot.getProperty("control" + juce::String(i + 1)));
            expectEquals(
                float(restoredSlot.getProperty("control" + juce::String(i + 1))),
                controls[size_t(i)], "Restored legacy control " + juce::String(i));
        }
        juce::AudioBuffer<float> expected(2, 256), actual(2, 256);
        float maximumError = 0.0f;
        for (int block = 0; block < 10; ++block)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                {
                    const auto sample = 0.1f * std::sin(
                        float(block * 256 + i) * (ch == 0 ? 0.11f : 0.32f));
                    expected.setSample(ch, i, sample);
                    actual.setSample(ch, i, sample);
                }
            original.process(expected, controls, {});
            restored->getRack().process(actual, nullptr, 120.0);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                    maximumError = juce::jmax(maximumError, std::abs(
                        actual.getSample(ch, i) - expected.getSample(ch, i)));
        }
        expect(maximumError < 0.00002f,
               "Maximum legacy audio difference: " + juce::String(maximumError, 9));
        restored->upgradeEqualizer(0);
        expect(restored->reEqSettings(0).native);
        expectEquals(restored->reEqSettings(0).bands[0].enabled, 2);
        expectWithinAbsoluteError(
            restored->reEqSettings(0).bands[0].gainDb,
            -18.0f + 36.0f * controls[1], 0.00001f);
        expectEquals(restored->getRack().activeModuleInstance(0)->latencySamples(), 0);
        expect(restored->undo());
        expect(!restored->reEqSettings(0).native);
        expectEquals(restored->getRack().activeModuleInstance(0)->latencySamples(), 0);

        beginTest("Host PDC and suspended bypass follow the selected native lookahead");
        auto aligned = std::make_unique<MegaDSPAudioProcessor>();
        aligned->prepareToPlay(48000.0, 512);
        aligned->addModule(ModuleType::equalizer);
        expectEquals(aligned->getRack().latencySamples(), 0);
        expectEquals(aligned->getLatencySamples(), 0);
        juce::AudioBuffer<float> impulse(2, 1024);
        for (const auto lookahead : { 0.0f, 1.0f, 5.0f, 10.0f })
        {
            auto eq = aligned->reEqSettings(0);
            eq.lookaheadMs = lookahead;
            aligned->setReEqSettings(0, eq);
            const auto latency = static_cast<int>(std::lround(lookahead * 48));
            expectEquals(aligned->getRack().latencySamples(), latency);
            expectEquals(aligned->getLatencySamples(), latency);
            aligned->getRack().reset();
            impulse.clear();
            impulse.setSample(0, 0, 0.1f);
            impulse.setSample(1, 0, -0.1f);
            aligned->getRack().process(impulse, nullptr, 120.0);
            expectEquals(aligned->getRack().latencySamples(), latency);
            expectWithinAbsoluteError(impulse.getSample(0, latency), 0.1f, 0.00001f);
            expectWithinAbsoluteError(impulse.getSample(1, latency), -0.1f, 0.00001f);
        }
        aligned->parameters.getParameter(slotParameterId(0, "bypass"))
            ->setValueNotifyingHost(1.0f);
        impulse.clear();
        aligned->getRack().process(impulse, nullptr, 120.0);
        expect(aligned->getRack().slotDspSuspended(0));
        impulse.setSample(0, 0, 0.1f);
        aligned->getRack().process(impulse, nullptr, 120.0);
        expectWithinAbsoluteError(impulse.getSample(0, 480), 0.1f, 0.00001f);
        expectEquals(aligned->getRack().latencySamples(), 480);
        auto eq = aligned->reEqSettings(0);
        eq.lookaheadMs = 0.0f;
        aligned->setReEqSettings(0, eq);
        expectEquals(aligned->getLatencySamples(), 0);
        impulse.clear();
        impulse.setSample(0, 0, 0.1f);
        aligned->getRack().process(impulse, nullptr, 120.0);
        expect(aligned->getRack().slotDspSuspended(0));
        expectWithinAbsoluteError(impulse.getSample(0, 0), 0.1f, 0.00001f);
        expectEquals(aligned->getRack().latencySamples(), 0);
    }
};

ReEqStateTests reEqStateTests;
}
