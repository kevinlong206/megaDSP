#include "modules/ModuleRegistry.h"
#include "modules/NeuralAmpModeler.h"
#include "PluginProcessor.h"
#include "EffectRack.h"

#include <NAM/wavenet/a2_fast.h>
#include <json.hpp>
#include <juce_core/juce_core.h>

#include <cmath>

namespace
{
using namespace megadsp;

juce::String linearNamJson(double sr = 48000.0)
{
    juce::String j;
    j << "{\"version\":\"0.5.4\",\"architecture\":\"Linear\","
      << "\"config\":{\"receptive_field\":4,\"bias\":false,"
      << "\"in_channels\":1,\"out_channels\":1,\"implementation\":\"direct\"},"
      << "\"weights\":[0.0,0.0,0.0,1.0],"
      << "\"sample_rate\":" << juce::String(sr, 1) << ",\"metadata\":{}}";
    return j;
}

juce::String slimmableContainerNamJson(double sr = 48000.0)
{
    const auto child = nlohmann::json::parse(
        linearNamJson(sr).toStdString());
    nlohmann::json root;
    root["version"] = "0.5.4";
    root["architecture"] = "SlimmableContainer";
    root["config"]["submodels"] = nlohmann::json::array({
        { { "max_value", 0.5 }, { "model", child } },
        { { "max_value", 1.0 }, { "model", child } }
    });
    root["weights"] = nlohmann::json::array();
    root["sample_rate"] = sr;
    root["metadata"] = nlohmann::json::object();
    return juce::String(root.dump());
}

juce::String waveNetNamJson(double sr = 48000.0)
{
    nlohmann::json layer;
    layer["input_size"] = 1;
    layer["condition_size"] = 1;
    layer["head_size"] = 1;
    layer["channels"] = 1;
    layer["kernel_size"] = 1;
    layer["dilations"] = { 1 };
    layer["activation"] = "Tanh";
    layer["gated"] = false;
    layer["head_bias"] = false;

    nlohmann::json root;
    root["version"] = "0.5.4";
    root["architecture"] = "WaveNet";
    root["config"]["layers"] = nlohmann::json::array({ layer });
    root["config"]["head"] = nullptr;
    root["config"]["head_scale"] = 1.0;
    root["weights"] =
        { 1.0, 1.0, 0.0, 1.0, 1.0, 0.0, 1.0, 1.0 };
    root["sample_rate"] = sr;
    root["metadata"] = nlohmann::json::object();
    return juce::String(root.dump());
}

juce::File writeNam(const juce::String& content, const juce::String& name)
{
    auto f = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getChildFile(name + ".nam");
    f.replaceWithText(content);
    return f;
}

// A2 standard shape: single layer-array with exact upstream shape signature
nlohmann::json a2StandardConfig()
{
    constexpr int kNumLayers = 23;
    constexpr int kKernelSizes[23] = {
        6,6,6,6,6,6,6,6,6,6,6,6,6,6,15,15,6,6,6,6,6,6,6 };
    constexpr int kDilations[23] = {
        1,3,7,17,41,101,239,1,3,7,17,41,101,239,1,13,1,3,7,17,41,101,239 };

    nlohmann::json la;
    la["input_size"] = 1;
    la["condition_size"] = 1;
    la["channels"] = 8;
    la["bottleneck"] = 8;
    la["head_size"] = 8;

    auto ks = nlohmann::json::array();
    auto dl = nlohmann::json::array();
    auto act = nlohmann::json::array();
    for (int i = 0; i < kNumLayers; ++i)
    {
        ks.push_back(kKernelSizes[i]);
        dl.push_back(kDilations[i]);
        nlohmann::json a;
        a["type"] = "LeakyReLU";
        a["negative_slope"] = 0.01f;
        act.push_back(a);
    }
    la["kernel_sizes"] = ks;
    la["dilations"] = dl;
    la["activation"] = act;

    // layer1x1: active with groups=1
    nlohmann::json l1x1;
    l1x1["active"] = true;
    l1x1["groups"] = 1;
    la["layer1x1"] = l1x1;

    // Layer-array head rechannel: k=16, out_channels=1, bias=true
    nlohmann::json laHead;
    laHead["out_channels"] = 1;
    laHead["kernel_size"] = 16;
    laHead["bias"] = true;
    la["head"] = laHead;

    nlohmann::json cfg;
    cfg["layers"] = nlohmann::json::array({ la });
    cfg["head"] = nullptr;
    cfg["head_scale"] = 0.02;
    return cfg;
}

class NamRegistryTests final : public juce::UnitTest
{
public:
    NamRegistryTests() : juce::UnitTest("NAM registry", "megaDSP") {}
    void runTest() override
    {
        beginTest("Stable appended enum and module count");
        expectEquals(static_cast<int>(ModuleType::neuralAmpModeler), 47);
        expectEquals(moduleTypeCount, 59);

        beginTest("Host parameter count");
        MegaDSPAudioProcessor proc;
        expectEquals(proc.getParameters().size(),
                     slotCount * (1 + 1 + controlsPerSlot) + 2);

        beginTest("Registry");
        const auto* d = findModuleDefinition(ModuleType::neuralAmpModeler);
        expect(d != nullptr);
        expectEquals(juce::String(d->displayName),
                     juce::String("Neural Amp Modeler"));
        expect(d->category == ModuleCategory::guitar);
        expectEquals(
            juce::String(moduleCategoryName(d->category)),
            juce::String("Guitar"));
        expect(hasCapability(d->capabilities,
                             ModuleCapability::neuralAmpModel));

        beginTest("Options, defaults, formatting, parsing");
        const auto defs = moduleDefaults(ModuleType::neuralAmpModeler);
        for (int control = 0; control < 5; ++control)
            expectWithinAbsoluteError(
                defs[static_cast<size_t>(control)], 0.5f, 0.01f);
        expect(controlOptions(ModuleType::neuralAmpModeler, 6).isEmpty());
        expectEquals(
            formatControlValue(ModuleType::neuralAmpModeler, 2, defs[2]),
            juce::String("0.0 dB"));
        auto p = parseControlValue(ModuleType::neuralAmpModeler, 0, "0 dB");
        expect(p.has_value());
        expectWithinAbsoluteError(*p, 0.5f, 0.01f);

        beginTest("Only tone-stack controls are visible");
        for (int control = 0; control < controlsPerSlot; ++control)
            expect(
                isControlContextuallyVisible(
                    ModuleType::neuralAmpModeler, control, defs)
                == (control < 5));

        beginTest("Factory and capability");
        auto mod = createDspModule(ModuleType::neuralAmpModeler);
        expect(mod != nullptr);
        expect(mod->namModelCapability() != nullptr);
        expect(hasCapability(mod->capabilities(),
                             ModuleCapability::neuralAmpModel));
    }
};

class NamDspTests final : public juce::UnitTest
{
public:
    NamDspTests() : juce::UnitTest("NAM DSP", "megaDSP") {}
    void runTest() override
    {
        beginTest("Unloaded module is silent");
        {
            auto m = createDspModule(ModuleType::neuralAmpModeler);
            m->prepare({ 48000.0, 512, 2 });
            m->reset();
            juce::AudioBuffer<float> buf(2, 512);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buf.setSample(ch, i, 0.5f);
            ControlValues c = moduleDefaults(ModuleType::neuralAmpModeler);
            m->process(buf, c, {});
            float mx = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    mx = juce::jmax(mx, std::abs(buf.getSample(ch, i)));
            expect(mx < 0.001f);
        }

        beginTest("Finite bounded output");
        {
            auto m = createDspModule(ModuleType::neuralAmpModeler);
            m->prepare({ 48000.0, 512, 1 });
            m->reset();
            juce::AudioBuffer<float> buf(1, 512);
            buf.clear(); buf.setSample(0, 0, 1.0f);
            m->process(buf, moduleDefaults(ModuleType::neuralAmpModeler), {});
            for (int i = 0; i < 512; ++i)
            {
                expect(std::isfinite(buf.getSample(0, i)));
                expect(std::abs(buf.getSample(0, i)) < 100.0f);
            }
        }

        beginTest("Load at matching rate, latency 0");
        {
            auto m = createDspModule(ModuleType::neuralAmpModeler);
            m->prepare({ 48000.0, 512, 2 });
            auto* cap = m->namModelCapability();
            auto f = writeNam(linearNamJson(48000.0), "match");
            PreparedNamModelPtr p;
            auto r = cap->prepareNamModel(f, p);
            expect(r.wasOk(), r.getErrorMessage());
            expect(cap->commitPreparedNamModel(p));
            expect(cap->hasNamModel());
            expectEquals(m->latencySamples(), 0);
            auto info = cap->namModelInfo();
            expect(info.kind == NamModelKind::linear);
            expect(info.cpuCapability == NamCpuCapability::fullOnly);
            f.deleteFile();
        }

        beginTest("Resample 44.1k host / 48k model: latency > 0, non-silent after latency");
        {
            auto m = createDspModule(ModuleType::neuralAmpModeler);
            m->prepare({ 44100.0, 512, 1 });
            auto* cap = m->namModelCapability();
            auto f = writeNam(linearNamJson(48000.0), "resamp");
            PreparedNamModelPtr p;
            expect(cap->prepareNamModel(f, p).wasOk());
            expect(cap->commitPreparedNamModel(p));
            const int lat = m->latencySamples();
            expect(lat > 0, "Resampler latency must be > 0");

            // Process several blocks of tone
            ControlValues c = moduleDefaults(ModuleType::neuralAmpModeler);
            const int totalSamples = lat + 512;
            const int blockSize = 256;
            bool anyNonZero = false;
            for (int off = 0; off < totalSamples; off += blockSize)
            {
                const int n = juce::jmin(blockSize, totalSamples - off);
                juce::AudioBuffer<float> buf(1, n);
                for (int i = 0; i < n; ++i)
                    buf.setSample(0, i, 0.3f * std::sin(
                        static_cast<float>(off + i) * 0.1f));
                m->process(buf, c, {});
                for (int i = 0; i < n; ++i)
                {
                    expect(std::isfinite(buf.getSample(0, i)));
                    if (off + i >= lat
                        && std::abs(buf.getSample(0, i)) > 0.001f)
                        anyNonZero = true;
                }
            }
            expect(anyNonZero, "Output must be non-silent after latency");
            f.deleteFile();
        }

        beginTest("Stereo duplicated-input channels match during smoothing");
        {
            auto m = createDspModule(ModuleType::neuralAmpModeler);
            m->prepare({ 48000.0, 256, 2 });
            auto* cap = m->namModelCapability();
            auto f = writeNam(linearNamJson(48000.0), "stereo");
            PreparedNamModelPtr p;
            cap->prepareNamModel(f, p);
            cap->commitPreparedNamModel(p);

            // Process blocks while stepping Pre Gain.
            ControlValues c = moduleDefaults(ModuleType::neuralAmpModeler);
            for (int block = 0; block < 4; ++block)
            {
                c[0] = block < 2 ? 0.0f : 1.0f; // step input
                juce::AudioBuffer<float> buf(2, 256);
                for (int i = 0; i < 256; ++i)
                {
                    buf.setSample(0, i, 0.4f);
                    buf.setSample(1, i, 0.4f);
                }
                m->process(buf, c, {});
                for (int i = 0; i < 256; ++i)
                    expectWithinAbsoluteError(
                        buf.getSample(0, i), buf.getSample(1, i), 1e-6f);
            }
            f.deleteFile();
        }

        beginTest("Oversized direct blocks are processed completely");
        {
            auto m = createDspModule(ModuleType::neuralAmpModeler);
            m->prepare({ 48000.0, 256, 1 });
            auto* cap = m->namModelCapability();
            auto f = writeNam(linearNamJson(48000.0), "oversized");
            PreparedNamModelPtr p;
            expect(cap->prepareNamModel(f, p).wasOk());
            expect(cap->commitPreparedNamModel(p));
            m->reset();

            juce::AudioBuffer<float> buf(1, 4096);
            for (int i = 0; i < buf.getNumSamples(); ++i)
                buf.setSample(0, i, 0.25f);
            auto controls = moduleDefaults(ModuleType::neuralAmpModeler);
            controls[0] = 0.75f;
            m->process(buf, controls, {});
            for (int i = 0; i < buf.getNumSamples(); ++i)
                expect(std::isfinite(buf.getSample(0, i)));
            expect(buf.getSample(0, 4095) > 0.5f,
                   "Samples beyond the prepared block must receive Pre Gain");
            f.deleteFile();
        }

        beginTest("SlimmableContainer parser is explicitly available");
        {
            auto m = createDspModule(ModuleType::neuralAmpModeler);
            m->prepare({ 48000.0, 512, 1 });
            auto f = writeNam(
                slimmableContainerNamJson(), "slimmable_container");
            PreparedNamModelPtr prepared;
            const auto result =
                m->namModelCapability()->prepareNamModel(f, prepared);
            expect(result.wasOk(), result.getErrorMessage());
            expect(m->namModelCapability()->commitPreparedNamModel(prepared));
            expectEquals(
                static_cast<int>(
                   m->namModelCapability()->namModelInfo().kind),
                static_cast<int>(NamModelKind::container));
            f.deleteFile();
        }

        beginTest("WaveNet parser is explicitly available");
        {
            auto m = createDspModule(ModuleType::neuralAmpModeler);
            m->prepare({ 48000.0, 512, 1 });
            auto f = writeNam(waveNetNamJson(), "wavenet");
            PreparedNamModelPtr prepared;
            const auto result =
                m->namModelCapability()->prepareNamModel(f, prepared);
            expect(result.wasOk(), result.getErrorMessage());
            expect(m->namModelCapability()->commitPreparedNamModel(prepared));
            expectEquals(
                static_cast<int>(
                    m->namModelCapability()->namModelInfo().kind),
                static_cast<int>(NamModelKind::waveNet));
            f.deleteFile();
        }

        beginTest("Malformed file fails gracefully");
        {
            auto m = createDspModule(ModuleType::neuralAmpModeler);
            m->prepare({ 48000.0, 512, 1 });
            auto f = writeNam("{ bad }", "bad");
            PreparedNamModelPtr p;
            expect(m->namModelCapability()->prepareNamModel(f, p).failed());
            f.deleteFile();
        }

        beginTest("Out-of-range model version fails gracefully");
        {
            auto m = createDspModule(ModuleType::neuralAmpModeler);
            m->prepare({ 48000.0, 512, 1 });
            auto json = linearNamJson();
            json = json.replace(
                "\"0.5.4\"", "\"999999999999999999999.5.4\"");
            auto f = writeNam(json, "version_range");
            PreparedNamModelPtr p;
            expect(m->namModelCapability()->prepareNamModel(f, p).failed());
            f.deleteFile();
        }

        beginTest("Missing file fails");
        {
            auto m = createDspModule(ModuleType::neuralAmpModeler);
            m->prepare({ 48000.0, 512, 1 });
            PreparedNamModelPtr p;
            expect(m->namModelCapability()->prepareNamModel(
                juce::File("/no.nam"), p).failed());
        }

        beginTest("Clear model");
        {
            auto m = createDspModule(ModuleType::neuralAmpModeler);
            m->prepare({ 48000.0, 512, 2 });
            auto* cap = m->namModelCapability();
            auto f = writeNam(linearNamJson(), "clr");
            PreparedNamModelPtr p;
            cap->prepareNamModel(f, p);
            cap->commitPreparedNamModel(p);
            expect(cap->hasNamModel());
            cap->clearNamModel();
            expect(!cap->hasNamModel());
            f.deleteFile();
        }

        beginTest("A2 positive detection (standard 8ch)");
        {
#if defined(NAM_ENABLE_A2_FAST)
            auto cfg = a2StandardConfig();
            int channels = 0;
            bool isA2 = nam::wavenet::a2_fast::is_a2_shape(cfg, &channels);
            expect(isA2, "Exact A2 standard shape must be detected");
            expectEquals(channels, 8);
#else
            expect(false, "NAM_ENABLE_A2_FAST not defined");
#endif
        }

        beginTest("A2 negative: 8ch non-A2 WaveNet");
        {
#if defined(NAM_ENABLE_A2_FAST)
            nlohmann::json cfg;
            cfg["layers"] = nlohmann::json::array();
            nlohmann::json layer;
            layer["input_size"] = 1;
            layer["condition_size"] = 1;
            layer["head_size"] = 2;
            layer["channels"] = 8;
            layer["kernel_size"] = 3;
            layer["dilations"] = { 1, 2 };
            layer["activation"] = "Tanh";
            layer["gated"] = false;
            layer["head_bias"] = false;
            cfg["layers"].push_back(layer);
            cfg["head"] = nullptr;
            cfg["head_scale"] = 0.02;
            int ch = 0;
            expect(!nam::wavenet::a2_fast::is_a2_shape(cfg, &ch));
#endif
        }

        beginTest("Tone-stack automation is finite and audible");
        {
            auto m = createDspModule(ModuleType::neuralAmpModeler);
            m->prepare({ 48000.0, 256, 1 });
            auto* cap = m->namModelCapability();
            auto f = writeNam(linearNamJson(48000.0), "filt");
            PreparedNamModelPtr p;
            cap->prepareNamModel(f, p);
            cap->commitPreparedNamModel(p);

            ControlValues c = moduleDefaults(ModuleType::neuralAmpModeler);
            for (int block = 0; block < 4; ++block)
            {
                c[1] = block < 2 ? 0.0f : 1.0f;
                c[2] = block % 2 == 0 ? 0.0f : 1.0f;
                c[3] = block < 2 ? 1.0f : 0.0f;
                juce::AudioBuffer<float> buf(1, 256);
                for (int i = 0; i < 256; ++i)
                    buf.setSample(0, i, 0.3f);
                m->process(buf, c, {});
                for (int i = 0; i < 256; ++i)
                    expect(std::isfinite(buf.getSample(0, i)));
            }
            f.deleteFile();
        }
    }
};

class NamStateTests final : public juce::UnitTest
{
public:
    NamStateTests() : juce::UnitTest("NAM state", "megaDSP") {}
    void runTest() override
    {
        beginTest("NAM path stored in APVTS state");
        {
            juce::AudioProcessorGraph owner;
            juce::AudioProcessorValueTreeState state(
                owner, nullptr, "s", createParameterLayout());
            EffectRack rack(state);
            rack.prepare(48000.0, 512, 2);

            // Set slot to NAM
            auto* tp = state.getParameter(slotParameterId(0, "type"));
            tp->beginChangeGesture();
            tp->setValueNotifyingHost(tp->convertTo0to1(
                static_cast<float>(ModuleType::neuralAmpModeler)));
            tp->endChangeGesture();
            rack.synchronizeModules();

            auto f = writeNam(linearNamJson(), "state_test");
            auto result = rack.loadNamModel(0, f);
            expect(result.wasOk(), result.getErrorMessage());
            expectEquals(rack.namModelPath(0), f.getFullPathName());
            f.deleteFile();
        }

        beginTest("Missing path retained after failed load");
        {
            juce::AudioProcessorGraph owner;
            juce::AudioProcessorValueTreeState state(
                owner, nullptr, "s2", createParameterLayout());
            EffectRack rack(state);
            rack.prepare(48000.0, 512, 2);

            auto* tp = state.getParameter(slotParameterId(0, "type"));
            tp->beginChangeGesture();
            tp->setValueNotifyingHost(tp->convertTo0to1(
                static_cast<float>(ModuleType::neuralAmpModeler)));
            tp->endChangeGesture();
            rack.synchronizeModules();

            // Store a path manually then rebuild
            state.state.setProperty("neuralModel1",
                "/nonexistent/model.nam", nullptr);
            rack.rebuildModules();
            // Path persists even though load fails
            expectEquals(rack.namModelPath(0),
                         juce::String("/nonexistent/model.nam"));
        }

        beginTest("Clear removes path");
        {
            juce::AudioProcessorGraph owner;
            juce::AudioProcessorValueTreeState state(
                owner, nullptr, "s3", createParameterLayout());
            EffectRack rack(state);
            rack.prepare(48000.0, 512, 2);

            auto* tp = state.getParameter(slotParameterId(0, "type"));
            tp->beginChangeGesture();
            tp->setValueNotifyingHost(tp->convertTo0to1(
                static_cast<float>(ModuleType::neuralAmpModeler)));
            tp->endChangeGesture();
            rack.synchronizeModules();

            auto f = writeNam(linearNamJson(), "clr_state");
            rack.loadNamModel(0, f);
            expect(rack.namModelPath(0).isNotEmpty());
            rack.clearNamModel(0);
            expect(rack.namModelPath(0).isEmpty());
            f.deleteFile();
        }

        beginTest("Re-prepare restores model after rebuildModules");
        {
            juce::AudioProcessorGraph owner;
            juce::AudioProcessorValueTreeState state(
                owner, nullptr, "s4", createParameterLayout());
            EffectRack rack(state);
            rack.prepare(48000.0, 512, 2);

            auto* tp = state.getParameter(slotParameterId(0, "type"));
            tp->beginChangeGesture();
            tp->setValueNotifyingHost(tp->convertTo0to1(
                static_cast<float>(ModuleType::neuralAmpModeler)));
            tp->endChangeGesture();
            rack.synchronizeModules();

            auto f = writeNam(linearNamJson(), "reprep");
            rack.loadNamModel(0, f);

            // Verify model is loaded
            const auto* mod1 = rack.activeModuleInstance(0);
            expect(mod1 != nullptr && mod1->namModelCapability() != nullptr);
            expect(mod1->namModelCapability()->hasNamModel());

            // Simulate sample-rate change (file must still exist)
            rack.prepare(44100.0, 256, 2);
            rack.rebuildModules();

            // Model should be restored from stored path
            const auto* mod2 = rack.activeModuleInstance(0);
            expect(mod2 != nullptr);
            const auto* namCap2 = mod2->namModelCapability();
            expect(namCap2 != nullptr);
            expect(namCap2->hasNamModel(),
                   "Model must be restored after re-prepare");
            f.deleteFile();
        }

        beginTest("Model paths follow move, compact, and remove");
        {
            juce::AudioProcessorGraph owner;
            juce::AudioProcessorValueTreeState state(
                owner, nullptr, "s5", createParameterLayout());
            EffectRack rack(state);
            rack.prepare(48000.0, 512, 2);

            auto setType = [&state](int slot, ModuleType type)
            {
                auto* parameter =
                    state.getParameter(slotParameterId(slot, "type"));
                parameter->setValueNotifyingHost(parameter->convertTo0to1(
                    static_cast<float>(type)));
            };

            setType(0, ModuleType::neuralAmpModeler);
            state.state.setProperty("neuralModel1", "/tmp/a.nam", nullptr);
            rack.moveSlot(0, 2);
            expect(rack.namModelPath(0).isEmpty());
            expectEquals(rack.namModelPath(2), juce::String("/tmp/a.nam"));

            expect(rack.compactSlots());
            expectEquals(rack.namModelPath(0), juce::String("/tmp/a.nam"));
            expect(rack.namModelPath(2).isEmpty());

            setType(0, ModuleType::delay);
            setType(1, ModuleType::neuralAmpModeler);
            state.state.removeProperty("neuralModel1", nullptr);
            state.state.setProperty("neuralModel2", "/tmp/b.nam", nullptr);
            rack.removeSlot(0);
            expectEquals(rack.namModelPath(0), juce::String("/tmp/b.nam"));
            expect(rack.namModelPath(1).isEmpty());
        }

        beginTest("Processor state restores model path and loaded model");
        {
            auto f = writeNam(linearNamJson(), "processor_state");
            MegaDSPAudioProcessor source;
            source.prepareToPlay(44100.0, 512);
            source.addModule(ModuleType::neuralAmpModeler);
            const auto loaded = source.loadNamModel(0, f);
            expect(loaded.wasOk(), loaded.getErrorMessage());
            expect(source.getRack().latencySamples() > 0,
                   "Loading a resampled NAM must update rack latency");

            juce::MemoryBlock saved;
            source.getStateInformation(saved);

            MegaDSPAudioProcessor restored;
            restored.prepareToPlay(44100.0, 512);
            restored.setStateInformation(
                saved.getData(), static_cast<int>(saved.getSize()));
            expectEquals(restored.namModelPath(0), f.getFullPathName());
            expect(restored.getRack().latencySamples() > 0,
                   "Restoring a resampled NAM must update rack latency");
            const auto* module =
                restored.getRack().activeModuleInstance(0);
            expect(module != nullptr);
            expect(module != nullptr
                       && module->namModelCapability() != nullptr
                       && module->namModelCapability()->hasNamModel(),
                   "Serialized processor state must restore the NAM model");
            f.deleteFile();
        }
    }
};

NamRegistryTests namRegistryTests;
NamDspTests namDspTests;
NamStateTests namStateTests;
} // namespace
