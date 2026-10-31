#include "modules/ModuleRegistry.h"
#include "modules/NeuralAmpModeler.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "EffectRack.h"
#include "ui/EffectGraph.h"

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

juce::String metadataLinearNamJson(double sr = 48000.0)
{
    auto model = nlohmann::json::parse(
        linearNamJson(sr).toStdString());
    model["version"] = "0.7.0";
    model["metadata"] = {
        { "name", "Studio Lead" },
        { "modeled_by", "Test Modeler" },
        { "gear_make", "Example" },
        { "gear_model", "Amp 50" },
        { "tone_type", "crunch" },
        { "with_cab", true },
        { "input_level_dbu", 12.4 },
        { "validation_esr", 0.0025 }
    };
    return juce::String(model.dump());
}

juce::String slimmableContainerNamJson(double sr = 48000.0)
{
    auto slimChild = nlohmann::json::parse(
        linearNamJson(sr).toStdString());
    slimChild["weights"] = { 0.0, 0.0, 0.0, 0.0 };
    const auto fullChild = nlohmann::json::parse(
        linearNamJson(sr).toStdString());
    nlohmann::json root;
    root["version"] = "0.5.4";
    root["architecture"] = "SlimmableContainer";
    root["config"]["submodels"] = nlohmann::json::array({
        { { "max_value", 0.5 }, { "model", slimChild } },
        { { "max_value", 1.0 }, { "model", fullChild } }
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

juce::File writeImpulse(
    const juce::File& file, int delaySamples = 0)
{
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream =
        file.createOutputStream();
    if (stream == nullptr)
        return {};
    auto writer = format.createWriterFor(
        stream,
        juce::AudioFormatWriterOptions {}
            .withSampleRate(48000.0)
            .withNumChannels(1)
            .withBitsPerSample(24));
    if (writer == nullptr)
        return {};
    juce::AudioBuffer<float> impulse(
        1, juce::jmax(128, delaySamples + 1));
    impulse.clear();
    if (delaySamples > 0)
        impulse.setSample(0, 0, 0.25f);
    impulse.setSample(0, delaySamples, 1.0f);
    writer->writeFromAudioSampleBuffer(
        impulse, 0, impulse.getNumSamples());
    return file;
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
        expectEquals(moduleTypeCount, 62);

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

        beginTest("Runaway models are muted before reaching the host");
        {
            auto runaway = nlohmann::json::parse(
                linearNamJson(48000.0).toStdString());
            runaway["weights"] = { 0.0, 0.0, 0.0, 1000.0 };
            auto f = writeNam(
                juce::String(runaway.dump()), "runaway");
            auto m = createDspModule(ModuleType::neuralAmpModeler);
            m->prepare({ 48000.0, 512, 1 });
            auto* cap = m->namModelCapability();
            PreparedNamModelPtr p;
            const auto result = cap->prepareNamModel(f, p);
            expect(result.wasOk(), result.getErrorMessage());
            expect(cap->commitPreparedNamModel(p));

            juce::AudioBuffer<float> buffer(1, 512);
            buffer.clear();
            buffer.setSample(0, 0, 0.25f);
            m->process(
                buffer,
                moduleDefaults(ModuleType::neuralAmpModeler), {});
            expect(buffer.getMagnitude(
                       0, buffer.getNumSamples()) < 0.000001f);
            for (int sample = 0;
                 sample < buffer.getNumSamples(); ++sample)
                expect(std::isfinite(buffer.getSample(0, sample)));
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

        beginTest("Cabinet impulse processes after the NAM model");
        {
            auto module =
                createDspModule(ModuleType::neuralAmpModeler);
            module->prepare({ 48000.0, 256, 1 });
            auto* capability = module->namModelCapability();
            auto model = writeNam(
                linearNamJson(48000.0), "nam-cabinet-signal-path");
            auto impulse = writeImpulse(
                juce::File::getSpecialLocation(
                    juce::File::tempDirectory)
                    .getNonexistentChildFile(
                        "nam-cabinet-signal-path", ".wav"),
                24);
            PreparedNamModelPtr preparedModel;
            PreparedNamModelPtr preparedCabinet;
            expect(capability->prepareNamModel(
                       model, preparedModel).wasOk());
            expect(capability->commitPreparedNamModel(preparedModel));
            expect(capability->prepareNamCabinet(
                       impulse, preparedCabinet).wasOk());
            expect(capability->commitPreparedNamCabinet(
                preparedCabinet));
            capability->setNamCabinetEnabled(true);

            const auto controls =
                moduleDefaults(ModuleType::neuralAmpModeler);
            juce::AudioBuffer<float> buffer(1, 256);
            for (int block = 0; block < 8; ++block)
            {
                buffer.clear();
                module->process(buffer, controls, {});
            }
            buffer.clear();
            buffer.setSample(0, 0, 0.5f);
            module->process(buffer, controls, {});
            auto peakSample = 0;
            auto peak = 0.0f;
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                if (const auto magnitude =
                        std::abs(buffer.getSample(0, sample));
                    magnitude > peak)
                {
                    peak = magnitude;
                    peakSample = sample;
                }
            expect(peak > 0.0001f);
            expect(peakSample >= 20,
                   "The post-NAM cabinet must apply the delayed IR.");
            model.deleteFile();
            impulse.deleteFile();
        }

        beginTest("NAM cabinet name and button reflect its active state");
        {
            MegaDSPAudioProcessor processor;
            processor.prepareToPlay(48000.0, 256);
            processor.addModule(ModuleType::neuralAmpModeler);
            const auto model = writeNam(
                linearNamJson(), "nam-cabinet-widget");
            const auto impulse = writeImpulse(
                juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getNonexistentChildFile("nam-cabinet-widget", ".wav"));
            expect(processor.loadNamModel(0, model).wasOk());
            expect(processor.loadNamCabinetAsync(0, impulse).wasOk());
            processor.finishNamModelLoadForTesting();
            expect(processor.setNamCabinetEnabled(0, true).wasOk());

            MegaDSPAudioProcessorEditor editor(processor, 1100, 720);
            bool foundA1Label = false;
            juce::Label* cabinetName = nullptr;
            juce::TextButton* cabinetButton = nullptr;
            const auto findCabinetControls = [&](auto&& self,
                                                  juce::Component& parent) -> void
            {
                for (auto* child : parent.getChildren())
                {
                    if (auto* label = dynamic_cast<juce::Label*>(child);
                        label != nullptr && label->getText().startsWith("CAB  "))
                        cabinetName = label;
                    if (auto* label = dynamic_cast<juce::Label*>(child);
                        label != nullptr
                        && label->getText().startsWith("MODEL  A1  "))
                        foundA1Label = true;
                    if (auto* button = dynamic_cast<juce::TextButton*>(child);
                        button != nullptr && button->getButtonText() == "IR ON")
                        cabinetButton = button;
                    self(self, *child);
                }
            };
            findCabinetControls(findCabinetControls, editor);
            expect(cabinetName != nullptr);
            expect(cabinetButton != nullptr);
            expect(foundA1Label, "The loaded model must show its A1 generation.");
            if (cabinetName != nullptr && cabinetButton != nullptr)
            {
                expect(cabinetName->isVisible());
                expect(cabinetName->getText().contains(
                    impulse.getFileNameWithoutExtension()));
                expect(cabinetButton->getToggleState());
                expect(cabinetButton->findColour(
                           juce::TextButton::buttonOnColourId)
                       != cabinetButton->findColour(
                           juce::TextButton::buttonColourId));

                cabinetButton->onClick();
                expect(!processor.isNamCabinetEnabled(0));
                expect(!cabinetName->isVisible());
                expect(!cabinetButton->getToggleState());
                expectEquals(cabinetButton->getButtonText(), juce::String("IR OFF"));
                expectEquals(
                    processor.namCabinetPath(0), impulse.getFullPathName());

                cabinetButton->onClick();
                expect(processor.isNamCabinetEnabled(0));
                expect(cabinetName->isVisible());
                expect(cabinetButton->getToggleState());
                expectEquals(cabinetButton->getButtonText(), juce::String("IR ON"));
            }
            model.deleteFile();
            impulse.deleteFile();
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
            const auto info = m->namModelCapability()->namModelInfo();
            expect(info.fullQuality);
            expectEquals(info.submodelCount, 2);

            juce::AudioBuffer<float> buffer(1, 128);
            buffer.clear();
            buffer.setSample(0, 0, 1.0f);
            m->process(
                buffer,
                moduleDefaults(ModuleType::neuralAmpModeler), {});
            expect(buffer.getMagnitude(0, 128) > 0.1f,
                   "The highest-quality container model must be active");
            f.deleteFile();
        }

        beginTest("Model metadata and A1 generation are reported");
        {
            auto m = createDspModule(ModuleType::neuralAmpModeler);
            m->prepare({ 48000.0, 512, 1 });
            auto f = writeNam(metadataLinearNamJson(), "metadata");
            PreparedNamModelPtr prepared;
            expect(m->namModelCapability()
                       ->prepareNamModel(f, prepared).wasOk());
            expect(m->namModelCapability()
                       ->commitPreparedNamModel(prepared));
            const auto info = m->namModelCapability()->namModelInfo();
            expectEquals(info.name, juce::String("Studio Lead"));
            expectEquals(info.formatVersion, juce::String("0.7.0"));
            expect(
                info.generation == NamArchitectureGeneration::a1);
            expect(info.captureDescription.contains("Example Amp 50"));
            expect(info.captureDescription.contains("Cab included"));
            expectWithinAbsoluteError(info.inputLevelDbU, 12.4, 0.001);
            expectWithinAbsoluteError(info.validationEsr, 0.0025, 0.00001);
            f.deleteFile();
        }

        beginTest("Metadata loudness drives bounded automatic matching");
        {
            auto json = nlohmann::json::parse(
                linearNamJson().toStdString());
            json["metadata"]["loudness"] = -30.0;
            auto file = writeNam(
                juce::String(json.dump()), "metadata_level_match");
            auto module =
                createDspModule(ModuleType::neuralAmpModeler);
            module->prepare({ 48000.0, 256, 1 });
            PreparedNamModelPtr prepared;
            auto* capability = module->namModelCapability();
            expect(capability->prepareNamModel(
                       file, prepared).wasOk());
            expect(capability->commitPreparedNamModel(prepared));
            expectWithinAbsoluteError(
                capability->namModelInfo().levelMatchDb,
                12.0, 0.01);
            juce::AudioBuffer<float> buffer(1, 256);
            for (int block = 0; block < 12; ++block)
            {
                for (int sample = 0;
                     sample < buffer.getNumSamples(); ++sample)
                    buffer.setSample(0, sample, 0.1f);
                module->process(
                    buffer,
                    moduleDefaults(ModuleType::neuralAmpModeler), {});
            }
            expect(buffer.getMagnitude(0, 0, buffer.getNumSamples())
                       > 0.25f,
                   "The metadata level-match trim must affect audio.");
            file.deleteFile();
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

        beginTest("Loaded A2 models and browser rows show their generation");
        {
            const auto root =
                juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getNonexistentChildFile("megadsp-a2-display", {}, true);
            expect(root.createDirectory().wasOk());
            const auto model = root.getChildFile("a2-display.nam");
            auto config = a2StandardConfig();
            int weightCount = 8 + 8 * 16 + 2;
            for (const auto& kernel : config["layers"][0]["kernel_sizes"])
                weightCount += 8 * 8 * kernel.get<int>() + 3 * 8 + 8 * 8;
            nlohmann::json json {
                { "version", "0.5.4" },
                { "architecture", "WaveNet" },
                { "config", config },
                { "sample_rate", 48000.0 },
                { "metadata", nlohmann::json::object() },
                { "weights", std::vector<float>(
                                 static_cast<size_t>(weightCount), 0.0f) }
            };
            expect(model.replaceWithText(juce::String(json.dump())));
            MegaDSPAudioProcessor processor;
            processor.prepareToPlay(48000.0, 64);
            processor.addModule(ModuleType::neuralAmpModeler);
            expect(processor.loadNamModel(0, model).wasOk());
            expect(processor.setNamModelRootDirectory(root).wasOk());
            expectEquals(processor.namModelLibrarySize(), 1);
            MegaDSPAudioProcessorEditor editor(processor, 1100, 720);
            bool foundA2 = false;
            const auto findLabel = [&](auto&& self,
                                       juce::Component& parent) -> void
            {
                for (auto* child : parent.getChildren())
                {
                    if (const auto* label = dynamic_cast<juce::Label*>(child);
                        label != nullptr
                        && label->getText().startsWith("MODEL  A2  "))
                        foundA2 = true;
                    self(self, *child);
                }
            };
            findLabel(findLabel, editor);
            expect(foundA2, "The loaded A2 model must be identified.");
            megadsp::ui::EffectGraph graph(
                processor, 0, ModuleType::neuralAmpModeler);
            graph.setBounds(0, 0, 384, 100);
            expectEquals(
                graph.compactNamModelNameForTesting(0),
                juce::String("[A2] a2-display"));
            root.deleteRecursively();
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

class LibraryRebuildTests final : public juce::UnitTest
{
public:
    LibraryRebuildTests()
        : juce::UnitTest("Library rebuild progress", "megaDSP") {}

    void runTest() override
    {
        beginTest("Preparing isolated libraries");
        const auto root = juce::File::getSpecialLocation(
            juce::File::tempDirectory).getNonexistentChildFile(
                "megaDSP-library-progress", {}, true);
        expect(root.createDirectory().wasOk());
        {
            MegaDSPAudioProcessor processor;
            expect(processor.setNamModelRootDirectory(root).wasOk());
            expect(processor.setImpulseResponseRootDirectory(root).wasOk());
            expect(root.getChildFile("model.nam").replaceWithText(
                linearNamJson()));
            expect(root.getChildFile("broken.nam").replaceWithText(
                "{ not a model }"));
            expect(writeImpulse(root.getChildFile("impulse.wav"))
                       .existsAsFile());
            expect(root.getChildFile("broken.wav").replaceWithText(
                "not audio"));

            for (const auto library : {
                     MegaDSPAudioProcessor::Library::namModels,
                     MegaDSPAudioProcessor::Library::impulseResponses })
            {
                beginTest("Manual rebuild runs asynchronously and completes");
                auto progress = std::make_shared<
                    MegaDSPAudioProcessor::LibraryRebuildProgress>();
                expectEquals(progress->fraction.load(), -1.0);
                expect(!progress->finished.load());
                expect(processor.rebuildLibraryAsync(
                    library, progress).wasOk());
                processor.finishLibraryIndexForTesting();
                const auto finished = progress->finished.load(
                    std::memory_order_acquire);
                expect(finished);
                if (finished)
                    expect(progress->result.wasOk());
                expectEquals(progress->fraction.load(), 1.0);
                expect(!processor.isNamModelLibraryIndexing());
                expect(!processor.isImpulseResponseLibraryIndexing());
            }
            expectEquals(processor.namModelLibrarySize(), 1);
            expectEquals(processor.impulseResponseLibrarySize(), 1);

            beginTest("Validation progress is bounded and monotonic");
            std::vector<double> fractions;
            const auto record = [&fractions](double fraction)
            {
                fractions.push_back(fraction);
            };
            for (bool namModels : { true, false })
            {
                fractions.clear();
                expect((namModels
                    ? processor.rebuildNamModelLibrary(record)
                    : processor.rebuildImpulseResponseLibrary(record))
                           .wasOk());
                expect(fractions.size() >= 3);
                if (!fractions.empty())
                {
                    expectEquals(fractions.front(), 0.0);
                    expectEquals(fractions.back(), 1.0);
                }
                double previous = 0.0;
                for (const auto fraction : fractions)
                {
                    expect(fraction >= previous && fraction <= 1.0);
                    previous = fraction;
                }
            }

            beginTest("Manual progress dialog confirms completion");
            for (const auto library : {
                     MegaDSPAudioProcessor::Library::namModels,
                     MegaDSPAudioProcessor::Library::impulseResponses })
            {
                MegaDSPAudioProcessorEditor editor(processor, 1100, 720);
                editor.rebuildLibraryForTesting(library);
                auto* window = editor.libraryRebuildWindowForTesting();
                expect(window != nullptr);
                if (window != nullptr)
                    expect(window->isCurrentlyModal());
                processor.finishLibraryIndexForTesting();
                editor.refreshLibraryRebuildProgressForTesting();
                if (window != nullptr)
                {
                    expectEquals(window->getName(),
                                 juce::String("Library index rebuilt"));
                    expectEquals(window->getNumButtons(), 1);
                    window->exitModalState(1);
                }
                editor.refreshLibraryRebuildProgressForTesting();
                expect(editor.libraryRebuildWindowForTesting() == nullptr);
            }

            beginTest("Closing the editor does not cancel a library rebuild");
            {
                MegaDSPAudioProcessorEditor editor(processor, 1100, 720);
                editor.rebuildLibraryForTesting(
                    MegaDSPAudioProcessor::Library::namModels);
            }
            processor.finishLibraryIndexForTesting();
            expect(!processor.isNamModelLibraryIndexing());
            expectEquals(processor.namModelLibrarySize(), 1);
        }
        expect(root.deleteRecursively());

        beginTest("Missing library roots fail explicitly");
        MegaDSPAudioProcessor processor;
        auto progress = std::make_shared<
            MegaDSPAudioProcessor::LibraryRebuildProgress>();
        expect(processor.rebuildLibraryAsync(
            MegaDSPAudioProcessor::Library::namModels, progress).failed());
        expect(processor.rebuildLibraryAsync(
            MegaDSPAudioProcessor::Library::impulseResponses,
            progress).failed());
        expect(!processor.isNamModelLibraryIndexing());
        expect(!processor.isImpulseResponseLibraryIndexing());
    }
};

class NamStateTests final : public juce::UnitTest
{
public:
    NamStateTests() : juce::UnitTest("NAM state", "megaDSP") {}
    void runTest() override
    {
        beginTest("Full presets retain NAM, cabinet, IR banks and EQ across wrapper contexts");
        {
            const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                .getNonexistentChildFile("megaDSP-asset-preset", {}, false);
            expect(root.createDirectory().wasOk());
            const auto model = writeNam(linearNamJson(), "full-preset-model");
            const auto first = writeImpulse(root.getChildFile("first.wav"), 7);
            const auto second = writeImpulse(root.getChildFile("second.wav"), 23);
            const auto preset = root.getChildFile("Assets.megadsp");
            juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Standalone);
            auto source = std::make_unique<MegaDSPAudioProcessor>();
            juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Undefined);
            source->prepareToPlay(48000.0, 64);
            source->setInstanceName("Asset preset");
            source->addModule(ModuleType::neuralAmpModeler);
            source->addModule(ModuleType::convolutionReverb);
            source->addModule(ModuleType::equalizer);
            expect(source->loadNamModel(0, model).wasOk());
            expect(source->loadNamCabinetAsync(0, first).wasOk());
            source->finishNamModelLoadForTesting();
            expect(source->setNamCabinetEnabled(0, false).wasOk());
            expect(source->loadConvolutionImpulseResponseBank(1, 0, first).wasOk());
            expect(source->loadConvolutionImpulseResponseBank(1, 1, second).wasOk());
            expect(source->selectConvolutionImpulseResponseBank(1, 1));
            expect(source->setConvolutionWetBufferSamples(1, 512).wasOk());
            auto eq = source->reEqSettings(2);
            eq.bands[9].enabled = 1;
            eq.bands[9].frequency = 2300.0f;
            eq.bands[9].gainDb = -3.0f;
            source->setReEqSettings(2, eq);
            source->storeSnapshot(1, false);
            source->flushPendingUpdatesForTesting();
            expect(source->setNamCabinetEnabled(0, true).wasOk());
            source->recallSnapshot(0);
            source->flushPendingUpdatesForTesting();
            expect(source->savePreset(preset).wasOk());
            for (const auto wrapper : {
                     juce::AudioProcessor::wrapperType_AudioUnit,
                     juce::AudioProcessor::wrapperType_VST3,
                     juce::AudioProcessor::wrapperType_Undefined })
            {
                juce::AudioProcessor::setTypeOfNextNewPlugin(wrapper);
                auto restored = std::make_unique<MegaDSPAudioProcessor>();
                juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Undefined);
                restored->prepareToPlay(48000.0, 64);
                const auto result = restored->loadPreset(preset);
                expect(result.wasOk(), result.getErrorMessage());
                expectEquals(restored->namModelPath(0), model.getFullPathName());
                expectEquals(restored->getRack().namCabinetPath(0), first.getFullPathName());
                expect(!restored->isNamCabinetEnabled(0));
                expectEquals(restored->convolutionImpulseResponseBankPath(1, 0), first.getFullPathName());
                expectEquals(restored->convolutionImpulseResponseBankPath(1, 1), second.getFullPathName());
                expectEquals(restored->activeConvolutionImpulseResponseBank(1), 1);
                expectEquals(restored->convolutionWetBufferSamples(1), 512);
                expect(restored->reEqSettings(2) == eq);
                expect(restored->isSnapshotReady(0));
                expect(restored->isSnapshotReady(1));
                restored->recallSnapshot(1);
                expect(restored->isNamCabinetEnabled(0));
                juce::AudioBuffer<float> audio(2, 64);
                for (int channel = 0; channel < 2; ++channel)
                    juce::FloatVectorOperations::fill(audio.getWritePointer(channel), 0.1f, 64);
                juce::MidiBuffer midi;
                restored->processBlock(audio, midi);
                expect(audio.getMagnitude(0, 0, 64) > 0.0f);
                for (int channel = 0; channel < 2; ++channel)
                    for (int sample = 0; sample < 64; ++sample)
                        expect(std::isfinite(audio.getSample(channel, sample)));
            }
            expect(model.deleteFile());
            MegaDSPAudioProcessor missing;
            missing.prepareToPlay(48000.0, 64);
            missing.setInstanceName("Before missing preset");
            const auto result = missing.loadPreset(preset);
            expect(result.failed());
            expect(result.getErrorMessage().contains("unavailable assets"));
            expect(result.getErrorMessage().contains("Snapshot"));
            expect(missing.snapshotError(0).isNotEmpty());
            expect(missing.undo());
            expectEquals(missing.getInstanceName(), juce::String("Before missing preset"));
            expect(root.deleteRecursively());
        }

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

        beginTest("Host restore supersedes an in-flight NAM load");
        {
            const auto unique = juce::String::toHexString(
                juce::Time::getHighResolutionTicks());
            const auto root =
                juce::File::getSpecialLocation(
                    juce::File::tempDirectory)
                    .getNonexistentChildFile(
                        "megaDSP-nam-host-restore-" + unique, {}, true);
            expect(root.createDirectory().wasOk());
            const auto restoredModel = writeNam(
                linearNamJson(), "host-restore-a-" + unique);
            const auto pendingModel = writeNam(
                linearNamJson(), "host-restore-b-" + unique);
            const auto cabinet = writeImpulse(
                root.getChildFile("host-restore.wav"), 7);

            MegaDSPAudioProcessor source;
            source.prepareToPlay(48000.0, 64);
            source.addModule(ModuleType::neuralAmpModeler);
            juce::MemoryBlock saved;
            source.getStateInformation(saved);
            auto stateXml = juce::AudioProcessor::getXmlFromBinary(
                saved.getData(), static_cast<int>(saved.getSize()));
            expect(stateXml != nullptr);
            if (stateXml != nullptr)
            {
                auto state = juce::ValueTree::fromXml(*stateXml);
                auto snapshots = state.getChildWithName("SNAPSHOTS");
                auto snapshot = snapshots.getChild(0);
                auto slot = snapshot.getChild(0);
                slot.setProperty(
                    "namModel", restoredModel.getFullPathName(), nullptr);
                slot.setProperty(
                    "namCabinet", cabinet.getFullPathName(), nullptr);
                slot.setProperty("namCabinetEnabled", true, nullptr);
                stateXml = state.createXml();
                saved.reset();
                juce::AudioProcessor::copyXmlToBinary(*stateXml, saved);
            }

            MegaDSPAudioProcessor restored;
            restored.prepareToPlay(48000.0, 64);
            restored.addModule(ModuleType::neuralAmpModeler);
            std::atomic<bool> cancellationReported { false };
            expect(restored.loadNamModelAsync(
                0, pendingModel,
                [&cancellationReported](const juce::Result& result)
                {
                    cancellationReported.store(
                        result.failed(), std::memory_order_release);
                }).wasOk());

            restored.setStateInformation(
                saved.getData(), static_cast<int>(saved.getSize()));
            expect(cancellationReported.load(std::memory_order_acquire));
            expectEquals(
                restored.namModelPath(0),
                restoredModel.getFullPathName());
            expectEquals(
                restored.namCabinetPath(0),
                cabinet.getFullPathName());
            expect(restored.isNamCabinetEnabled(0));

            expect(restored.clearNamModel(0).wasOk(),
                   "State restore must release the topology mutation gate.");
            expect(restored.namModelPath(0).isEmpty());

            restoredModel.deleteFile();
            pendingModel.deleteFile();
            root.deleteRecursively();
        }

        beginTest("Cabinet defaults use relative paths and NAM metadata");
        {
            const auto root =
                juce::File::getSpecialLocation(
                    juce::File::tempDirectory)
                    .getNonexistentChildFile(
                        "megaDSP-nam-cabinet-defaults", {}, true);
            const auto directDirectory = root.getChildFile("Direct");
            const auto cabDirectory = root.getChildFile("Cab Models");
            const auto diDirectory = root.getChildFile("DI Models");
            const auto metadataDirectory =
                root.getChildFile("Metadata Models");
            expect(directDirectory.createDirectory().wasOk());
            expect(cabDirectory.createDirectory().wasOk());
            expect(diDirectory.createDirectory().wasOk());
            expect(metadataDirectory.createDirectory().wasOk());
            const auto unique = juce::String::toHexString(
                juce::Time::getHighResolutionTicks());
            const auto direct = directDirectory.getChildFile(
                "speaker-" + unique + ".nam");
            const auto cabDi = cabDirectory.getChildFile(
                "speaker-" + unique + "-DI.nam");
            const auto di = diDirectory.getChildFile(
                "speaker-" + unique + ".nam");
            const auto metadataNoCab = metadataDirectory.getChildFile(
                "metadata-" + unique + ".nam");
            const auto metadataCab = metadataDirectory.getChildFile(
                "metadata-cab-" + unique + ".nam");
            expect(direct.replaceWithText(linearNamJson()));
            expect(cabDi.replaceWithText(linearNamJson()));
            expect(di.replaceWithText(linearNamJson()));
            auto noCabJson = nlohmann::json::parse(
                linearNamJson().toStdString());
            noCabJson["metadata"]["with_cab"] = false;
            expect(metadataNoCab.replaceWithText(
                juce::String(noCabJson.dump())));
            auto withCabJson = nlohmann::json::parse(
                linearNamJson().toStdString());
            withCabJson["metadata"]["with_cab"] = true;
            expect(metadataCab.replaceWithText(
                juce::String(withCabJson.dump())));
            MegaDSPAudioProcessor processor;
            processor.prepareToPlay(48000.0, 64);
            processor.addModule(ModuleType::neuralAmpModeler);
            expect(processor.setNamModelRootDirectory(root).wasOk());
            expect(processor.loadNamModel(0, direct).wasOk());
            expect(processor.isNamCabinetEnabled(0),
                   "Direct in a parent path must enable the cabinet.");
            expect(processor.loadNamModel(0, cabDi).wasOk());
            expect(!processor.isNamCabinetEnabled(0),
                   "Cab in a parent path must override DI.");
            expect(processor.loadNamModel(0, di).wasOk());
            expect(processor.isNamCabinetEnabled(0),
                   "Exact-case DI in a parent path must enable the cabinet.");
            expect(processor.loadNamModel(0, metadataNoCab).wasOk());
            expect(processor.isNamCabinetEnabled(0),
                   "with_cab false must enable the cabinet.");
            expect(processor.loadNamModel(0, metadataCab).wasOk());
            expect(!processor.isNamCabinetEnabled(0),
                   "with_cab true must disable the cabinet.");
            root.deleteRecursively();
        }

        beginTest("Per-model cabinet choice and override persist globally");
        {
            const auto root =
                juce::File::getSpecialLocation(
                    juce::File::tempDirectory)
                    .getNonexistentChildFile(
                        "megaDSP-nam-cabinet", {}, true);
            expect(root.createDirectory().wasOk());
            const auto unique = juce::String::toHexString(
                juce::Time::getHighResolutionTicks());
            auto model = writeNam(
                linearNamJson(), "persist-cabinet-" + unique);
            const auto first = writeImpulse(
                root.getChildFile("A.wav"));
            const auto second = writeImpulse(
                root.getChildFile("B.wav"), 8);
            expect(first.existsAsFile());
            expect(second.existsAsFile());

            MegaDSPAudioProcessor source;
            source.prepareToPlay(48000.0, 64);
            source.addModule(ModuleType::neuralAmpModeler);
            expect(source.loadNamModel(0, model).wasOk());
            expect(source.setNamCabinetRootDirectory(root).wasOk());
            expect(source.loadNamCabinetAsync(0, first).wasOk());
            source.finishNamModelLoadForTesting();
            expectEquals(
                source.namCabinetPath(0), first.getFullPathName());
            expect(source.isNamCabinetEnabled(0));
            source.refreshAssetLibrariesForTesting();
            const auto automaticallyAdded = writeImpulse(
                root.getChildFile("C.wav"), 16);
            expect(automaticallyAdded.existsAsFile());
            source.refreshAssetLibrariesForTesting();
            expect(source.loadAdjacentNamCabinetAsync(0, -1).wasOk());
            source.finishNamModelLoadForTesting();
            expectEquals(
                source.namCabinetPath(0),
                automaticallyAdded.getFullPathName());
            expect(source.loadNamCabinetAsync(0, first).wasOk());
            source.finishNamModelLoadForTesting();

            const auto modelRoot =
                juce::File::getSpecialLocation(
                    juce::File::tempDirectory)
                    .getNonexistentChildFile(
                        "megaDSP-direct-fallback", {}, true);
            const auto mixedCaseDirect =
                modelRoot.getChildFile("Captures")
                    .getChildFile("dIrEcT")
                .getChildFile("metadata-included.nam");
            expect(mixedCaseDirect.getParentDirectory()
                       .createDirectory().wasOk());
            auto directWithCabMetadata = nlohmann::json::parse(
                linearNamJson().toStdString());
            directWithCabMetadata["metadata"]["with_cab"] = true;
            expect(mixedCaseDirect.replaceWithText(
                juce::String(directWithCabMetadata.dump())));
            MegaDSPAudioProcessor relativeDirect;
            relativeDirect.prepareToPlay(48000.0, 64);
            relativeDirect.addModule(ModuleType::neuralAmpModeler);
            expect(relativeDirect.setNamModelRootDirectory(
                modelRoot).wasOk());
            expect(relativeDirect.loadNamModelAsync(
                0, mixedCaseDirect).wasOk());
            relativeDirect.finishNamModelLoadForTesting();
            expectEquals(
                relativeDirect.namCabinetPath(0),
                first.getFullPathName(),
                "Nested mixed-case Direct paths must load the last IR.");
            expect(relativeDirect.isNamCabinetEnabled(0),
                   "Direct paths must override cabinet-included metadata.");

            auto inheritedModel = writeNam(
                linearNamJson(), "fallback-Direct-" + unique);
            MegaDSPAudioProcessor inherited;
            inherited.prepareToPlay(48000.0, 64);
            inherited.addModule(ModuleType::neuralAmpModeler);
            expect(inherited.loadNamModel(0, inheritedModel).wasOk());
            expectEquals(
                inherited.namCabinetPath(0), first.getFullPathName());
            expect(inherited.isNamCabinetEnabled(0),
                   "A new Direct model must use the last cabinet IR.");
            expect(inherited.loadNamCabinetAsync(0, second).wasOk());
            inherited.finishNamModelLoadForTesting();

            auto latestModel = writeNam(
                linearNamJson(), "latest-Direct-" + unique);
            MegaDSPAudioProcessor latest;
            latest.prepareToPlay(48000.0, 64);
            latest.addModule(ModuleType::neuralAmpModeler);
            expect(latest.loadNamModel(0, latestModel).wasOk());
            expectEquals(
                latest.namCabinetPath(0), second.getFullPathName());

            MegaDSPAudioProcessor modelSpecific;
            modelSpecific.prepareToPlay(48000.0, 64);
            modelSpecific.addModule(ModuleType::neuralAmpModeler);
            expect(modelSpecific.loadNamModel(0, model).wasOk());
            expectEquals(
                modelSpecific.namCabinetPath(0), first.getFullPathName(),
                "A model-specific cabinet must override the last used IR.");

            expect(source.loadAdjacentNamCabinetAsync(0, 1).wasOk());
            source.finishNamModelLoadForTesting();
            expectEquals(
                source.namCabinetPath(0), second.getFullPathName());
            expect(source.setNamCabinetEnabled(0, false).wasOk());

            MegaDSPAudioProcessor restored;
            restored.prepareToPlay(48000.0, 64);
            restored.addModule(ModuleType::neuralAmpModeler);
            expect(restored.loadNamModel(0, model).wasOk());
            expectEquals(
                restored.namCabinetPath(0), second.getFullPathName());
            expect(!restored.isNamCabinetEnabled(0),
                   "The manual cabinet override must survive restart.");

            expect(second.deleteFile());
            MegaDSPAudioProcessor missing;
            missing.prepareToPlay(48000.0, 64);
            missing.addModule(ModuleType::neuralAmpModeler);
            const auto missingResult =
                missing.loadNamModel(0, model);
            expect(missingResult.failed(),
                   "A missing persisted cabinet IR must fail explicitly.");

            model.deleteFile();
            inheritedModel.deleteFile();
            latestModel.deleteFile();
            modelRoot.deleteRecursively();
            root.deleteRecursively();
        }

        beginTest("Per-model controls and cabinet persist without overriding project state");
        {
            const auto root =
                juce::File::getSpecialLocation(
                    juce::File::tempDirectory)
                    .getNonexistentChildFile(
                        "megaDSP-nam-control-memory", {}, true);
            expect(root.createDirectory().wasOk());
            const auto unique = juce::String::toHexString(
                juce::Time::getHighResolutionTicks());
            auto model = writeNam(
                linearNamJson(), "control-memory-" + unique);
            auto unrelatedModel = writeNam(
                linearNamJson(), "control-memory-other-" + unique);
            const auto cabinet = writeImpulse(
                root.getChildFile("remembered.wav"));
            const std::array<float, 5> remembered {
                0.13f, 0.27f, 0.41f, 0.68f, 0.84f
            };
            const std::array<float, 5> projectValues {
                0.91f, 0.76f, 0.59f, 0.34f, 0.18f
            };

            MegaDSPAudioProcessor source;
            source.prepareToPlay(48000.0, 64);
            source.addModule(ModuleType::neuralAmpModeler);
            expect(source.loadNamModel(0, model).wasOk());
            expect(source.setNamCabinetRootDirectory(root).wasOk());
            expect(source.loadNamCabinetAsync(0, cabinet).wasOk());
            source.finishNamModelLoadForTesting();
            for (int control = 0; control < 5; ++control)
            {
                auto* parameter = source.parameters.getParameter(
                    controlParameterId(0, control));
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost(
                    remembered[static_cast<size_t>(control)]);
                parameter->endChangeGesture();
            }
            source.finishNamPreferenceSaveForTesting();

            MegaDSPAudioProcessor recalled;
            recalled.prepareToPlay(48000.0, 64);
            recalled.addModule(ModuleType::neuralAmpModeler);
            expect(recalled.loadNamModelAsync(0, model).wasOk());
            recalled.finishNamModelLoadForTesting();
            for (int control = 0; control < 5; ++control)
                expectWithinAbsoluteError(
                    recalled.parameters.getParameter(
                        controlParameterId(0, control))->getValue(),
                    remembered[static_cast<size_t>(control)], 0.0001f);
            expectEquals(
                recalled.namCabinetPath(0),
                cabinet.getFullPathName(),
                "Saving controls must retain the model cabinet preference.");
            expect(recalled.isNamCabinetEnabled(0));

            MegaDSPAudioProcessor unrelated;
            unrelated.prepareToPlay(48000.0, 64);
            unrelated.addModule(ModuleType::neuralAmpModeler);
            expect(unrelated.loadNamModel(0, unrelatedModel).wasOk());
            for (int control = 0; control < 5; ++control)
                expectWithinAbsoluteError(
                    unrelated.parameters.getParameter(
                        controlParameterId(0, control))->getValue(),
                    0.5f, 0.0001f,
                    "Remembered controls must be isolated by model.");

            MegaDSPAudioProcessor project;
            project.prepareToPlay(48000.0, 64);
            project.addModule(ModuleType::neuralAmpModeler);
            expect(project.loadNamModel(0, model).wasOk());
            for (int control = 0; control < 5; ++control)
                project.parameters.getParameter(
                    controlParameterId(0, control))
                    ->setValueNotifyingHost(
                        projectValues[static_cast<size_t>(control)]);
            juce::MemoryBlock saved;
            project.getStateInformation(saved);

            MegaDSPAudioProcessor restored;
            restored.prepareToPlay(48000.0, 64);
            restored.setStateInformation(
                saved.getData(), static_cast<int>(saved.getSize()));
            for (int control = 0; control < 5; ++control)
                expectWithinAbsoluteError(
                    restored.parameters.getParameter(
                        controlParameterId(0, control))->getValue(),
                    projectValues[static_cast<size_t>(control)], 0.0001f,
                    "Project state must override global model memory.");
            expectEquals(
                restored.namCabinetPath(0), cabinet.getFullPathName());

            expect(restored.deleteNamModel(model).wasOk());
            unrelatedModel.deleteFile();
            root.deleteRecursively();
        }

        beginTest("Multiple NAM slots preserve independent state");
        {
            const auto root =
                juce::File::getSpecialLocation(
                    juce::File::tempDirectory)
                    .getNonexistentChildFile(
                        "megaDSP-independent-nam-slots", {}, true);
            expect(root.createDirectory().wasOk());
            const auto unique = juce::String::toHexString(
                juce::Time::getHighResolutionTicks());
            auto model = writeNam(
                linearNamJson(), "independent-slots-" + unique);
            const auto first = writeImpulse(
                root.getChildFile("First.wav"));
            const auto second = writeImpulse(
                root.getChildFile("Second.wav"), 12);

            MegaDSPAudioProcessor source;
            source.prepareToPlay(48000.0, 64);
            source.addModule(ModuleType::neuralAmpModeler);
            source.addModule(ModuleType::neuralAmpModeler);
            expect(source.loadNamModel(0, model).wasOk());
            expect(source.loadNamModel(1, model).wasOk());
            expect(source.loadNamCabinetAsync(0, first).wasOk());
            source.finishNamModelLoadForTesting();
            expect(source.loadNamCabinetAsync(1, second).wasOk());
            source.finishNamModelLoadForTesting();
            expect(source.setNamCabinetEnabled(0, false).wasOk());

            auto* firstGain = source.parameters.getParameter(
                controlParameterId(0, 0));
            auto* secondGain = source.parameters.getParameter(
                controlParameterId(1, 0));
            firstGain->setValueNotifyingHost(0.2f);
            secondGain->setValueNotifyingHost(0.8f);

            expectEquals(
                source.namCabinetPath(0), first.getFullPathName());
            expectEquals(
                source.namCabinetPath(1), second.getFullPathName());
            expect(!source.isNamCabinetEnabled(0));
            expect(source.isNamCabinetEnabled(1));

            juce::MemoryBlock saved;
            source.getStateInformation(saved);
            MegaDSPAudioProcessor restored;
            restored.prepareToPlay(48000.0, 64);
            restored.setStateInformation(
                saved.getData(), static_cast<int>(saved.getSize()));

            expectEquals(
                restored.namCabinetPath(0), first.getFullPathName());
            expectEquals(
                restored.namCabinetPath(1), second.getFullPathName());
            expect(!restored.isNamCabinetEnabled(0));
            expect(restored.isNamCabinetEnabled(1));
            expectWithinAbsoluteError(
                restored.parameters.getParameter(
                    controlParameterId(0, 0))->getValue(),
                0.2f, 0.0001f);
            expectWithinAbsoluteError(
                restored.parameters.getParameter(
                    controlParameterId(1, 0))->getValue(),
                0.8f, 0.0001f);

            model.deleteFile();
            root.deleteRecursively();
        }

        beginTest("Legacy NAM models without sample rate remain available");
        {
            const auto root =
                juce::File::getSpecialLocation(
                    juce::File::tempDirectory)
                    .getNonexistentChildFile(
                        "megaDSP-legacy-nam-browser", {}, true);
            expect(root.createDirectory().wasOk());
            const auto legacy = root.getChildFile("legacy.nam");
            const auto invalid = root.getChildFile("invalid-rate.nam");
            auto legacyJson = nlohmann::json::parse(
                linearNamJson().toStdString());
            legacyJson.erase("sample_rate");
            expect(legacy.replaceWithText(
                juce::String(legacyJson.dump())));
            auto invalidJson = legacyJson;
            invalidJson["sample_rate"] = 0.0;
            expect(invalid.replaceWithText(
                juce::String(invalidJson.dump())));

            MegaDSPAudioProcessor browser;
            browser.prepareToPlay(48000.0, 64);
            browser.addModule(ModuleType::neuralAmpModeler);
            expect(browser.setNamModelRootDirectory(root).wasOk());
            expectEquals(browser.namModelLibrarySize(), 1);
            const auto results = browser.searchNamModels("legacy sr:48000");
            expectEquals(static_cast<int>(results.size()), 1);
            if (!results.empty())
                expectWithinAbsoluteError(
                    results.front().sampleRate, 48000.0, 0.001);
            expect(browser.loadNamModel(0, legacy).wasOk());

            root.deleteRecursively();
        }

        beginTest("NAM library sorts newest filesystem profiles first");
        {
            const auto root =
                juce::File::getSpecialLocation(
                    juce::File::tempDirectory)
                    .getNonexistentChildFile(
                        "megaDSP-nam-newest", {}, true);
            expect(root.createDirectory().wasOk());
            const auto oldest = root.getChildFile("oldest.nam");
            const auto middle = root.getChildFile("middle.nam");
            const auto newest = root.getChildFile("newest.nam");
            expect(oldest.replaceWithText(linearNamJson()));
            juce::Thread::sleep(20);
            expect(middle.replaceWithText(linearNamJson()));
            juce::Thread::sleep(20);
            expect(newest.replaceWithText(linearNamJson()));

            MegaDSPAudioProcessor processor;
            processor.prepareToPlay(48000.0, 64);
            processor.addModule(ModuleType::neuralAmpModeler);
            expect(processor.setNamModelRootDirectory(root).wasOk());
            const auto sorted = processor.searchNamModels(
                {}, std::numeric_limits<int>::max(),
                NamModelSort::newestAdded);
            expectEquals(static_cast<int>(sorted.size()), 3);
            if (sorted.size() == 3)
            {
                expectEquals(
                    sorted[0].file.getFullPathName(),
                    newest.getFullPathName());
                expectEquals(
                    sorted[1].file.getFullPathName(),
                    middle.getFullPathName());
                expectEquals(
                    sorted[2].file.getFullPathName(),
                    oldest.getFullPathName());
            }

            megadsp::ui::EffectGraph graph(
                processor, 0, ModuleType::neuralAmpModeler);
            graph.setBounds(0, 0, 384, 70);
            graph.setCompactNamNewestSortForTesting(true);
            expectEquals(
                graph.compactNamModelPathForTesting(0),
                newest.getFullPathName());
            expectEquals(
                graph.compactNamModelNameForTesting(0),
                juce::String("[A1] newest"));
            root.deleteRecursively();
        }

        beginTest("Recursive NAM library validates, searches, and persists");
        {
            const auto root =
                juce::File::getSpecialLocation(
                    juce::File::tempDirectory)
                    .getNonexistentChildFile(
                        "megaDSP-nam-browser", {}, true);
            const auto nested = root.getChildFile("High Gain");
            expect(nested.createDirectory().wasOk());
            const auto cabModel =
                nested.getChildFile("studio-lead.nam");
            const auto ampModel = root.getChildFile("clean-amp.NAM");
            const auto pedalModel = root.getChildFile("drive-pedal.nam");
            const auto invalid = root.getChildFile("broken.nam");
            auto cabJson = nlohmann::json::parse(
                metadataLinearNamJson().toStdString());
            cabJson["metadata"]["gear_type"] = "full-rig";
            expect(cabModel.replaceWithText(
                juce::String(cabJson.dump())));
            auto ampJson = nlohmann::json::parse(
                linearNamJson().toStdString());
            ampJson["metadata"] = {
                { "display_name", "Clean Platform" },
                { "gear_make", "Example" },
                { "gear_model", "Clean 100" },
                { "gear_type", "amp" },
                { "tone_type", "clean" }
            };
            expect(ampModel.replaceWithText(
                juce::String(ampJson.dump())));
            auto pedalJson = nlohmann::json::parse(
                linearNamJson().toStdString());
            pedalJson["metadata"] = {
                { "name", "Copper Drive" },
                { "gear_type", "pedal" },
                { "with_cab", false }
            };
            expect(pedalModel.replaceWithText(
                juce::String(pedalJson.dump())));
            expect(invalid.replaceWithText("{ not a model }"));
            MegaDSPAudioProcessor browser;
            expect(browser.setNamModelRootDirectory(root).wasOk());
            expectEquals(browser.namModelLibrarySize(), 3);
            const auto expectSearchCount =
                [this, &browser](
                    const juce::String& query, int expected)
                {
                    const auto results =
                        browser.searchNamModels(query);
                    juce::StringArray paths;
                    for (const auto& result : results)
                        paths.add(result.relativePath);
                    expectEquals(
                        static_cast<int>(results.size()), expected,
                        "Query '" + query + "' returned: "
                            + paths.joinIntoString(", "));
                };
            expectSearchCount("linear", 3);
            expectSearchCount("cab", 1);
            expectSearchCount("amp", 1);
            expectSearchCount("rig", 1);
            expectSearchCount("pedal", 1);
            expectSearchCount("nocab", 2);
            expectSearchCount("studio a1 cab sr:48000", 1);
            expectSearchCount("lead studio", 1);
            expectSearchCount("\"studio lead\"", 1);
            expectSearchCount("coper", 1);
            expectSearchCount("clean -pedal", 1);
            expectSearchCount("type:pedal", 1);
            expectSearchCount("cab:no", 2);
            expectSearchCount("direct", 2);
            expect(browser.searchNamModels("a2").empty());
            browser.refreshAssetLibrariesForTesting();
            const auto automaticallyAdded =
                root.getChildFile("automatic-update.nam");
            expect(automaticallyAdded.replaceWithText(linearNamJson()));
            browser.refreshAssetLibrariesForTesting();
            expectEquals(browser.namModelLibrarySize(), 4);
            MegaDSPAudioProcessor sharedCatalogBrowser;
            sharedCatalogBrowser.refreshAssetLibrariesForTesting();
            expectEquals(sharedCatalogBrowser.namModelLibrarySize(), 4);
            expect(automaticallyAdded.deleteFile());
            browser.refreshAssetLibrariesForTesting();
            expectEquals(browser.namModelLibrarySize(), 3);
            const auto orderBeforeFavorite =
                browser.searchNamModels({});
            expect(browser.setNamModelFavorite(
                       pedalModel, true).wasOk());
            const auto favorites =
                browser.searchNamModels("favorite");
            expectEquals(
                static_cast<int>(favorites.size()), 1);
            if (!favorites.empty())
            {
                expectEquals(
                    favorites.front().file.getFullPathName(),
                    pedalModel.getFullPathName());
                expect(favorites.front().favorite);
            }
            const auto orderAfterFavorite =
                browser.searchNamModels({});
            expectEquals(
                static_cast<int>(orderAfterFavorite.size()),
                static_cast<int>(orderBeforeFavorite.size()));
            for (size_t index = 0;
                 index < std::min(
                     orderBeforeFavorite.size(),
                     orderAfterFavorite.size());
                 ++index)
                expectEquals(
                    orderAfterFavorite[index]
                        .file.getFullPathName(),
                    orderBeforeFavorite[index]
                        .file.getFullPathName(),
                    "Favoriting must not reorder NAM results.");
            MegaDSPAudioProcessor favoriteRestored;
            expect(favoriteRestored.isNamModelFavorite(pedalModel),
                   "Favorites must persist across processor instances.");
            browser.prepareToPlay(48000.0, 64);
            browser.addModule(ModuleType::neuralAmpModeler);
            expect(browser.loadAdjacentNamModel(0, 1).wasOk());
            expectEquals(
                browser.namModelPath(0),
                ampModel.getFullPathName());
            bool asyncCompleted = false;
            juce::Result asyncResult = juce::Result::fail(
                "Async NAM callback was not called.");
            const auto requestStart =
                juce::Time::getHighResolutionTicks();
            expect(browser.loadAdjacentNamModelAsync(
                0, 1,
                [&asyncCompleted, &asyncResult](
                    const juce::Result& result)
                {
                    asyncCompleted = true;
                    asyncResult = result;
                }).wasOk());
            const auto requestMicros =
                juce::Time::highResolutionTicksToSeconds(
                    juce::Time::getHighResolutionTicks() - requestStart)
                * 1000000.0;
            expect(
                requestMicros < 500000.0,
                "Scheduling an adjacent NAM load must not parse the model.");
            expectEquals(
                browser.namModelPath(0),
                ampModel.getFullPathName());
            browser.finishNamModelLoadForTesting();
            expect(asyncCompleted);
            expect(asyncResult.wasOk(), asyncResult.getErrorMessage());
            expectEquals(
                browser.namModelPath(0),
                pedalModel.getFullPathName());
            expect(browser.loadAdjacentNamModel(0, -1).wasOk());
            expectEquals(
                browser.namModelPath(0),
                ampModel.getFullPathName());

            {
                MegaDSPAudioProcessor compactBrowser;
                compactBrowser.prepareToPlay(48000.0, 64);
                compactBrowser.addModule(
                    ModuleType::neuralAmpModeler);
                expect(compactBrowser.loadNamModel(
                           0, ampModel).wasOk());
                megadsp::ui::EffectGraph graph(
                    compactBrowser, 0,
                    ModuleType::neuralAmpModeler);
                graph.setBounds(0, 0, 384, 70);
                expectEquals(
                    graph.compactNamLibraryModelCountForTesting(), 3);
                graph.setCompactNamFavoritesOnlyForTesting(true);
                expectEquals(
                    graph.compactNamLibraryModelCountForTesting(), 1);
                graph.toggleCompactNamFavoriteForTesting(0);
                expectEquals(
                    graph.compactNamLibraryModelCountForTesting(), 0);
                expect(!compactBrowser.isNamModelFavorite(pedalModel));
                expect(compactBrowser.setNamModelFavorite(
                           pedalModel, true).wasOk());
                graph.setCompactNamFavoritesOnlyForTesting(false);
                expectEquals(
                    graph.compactNamLibraryModelCountForTesting(), 3);
                graph.scrollCompactNamLibraryForTesting(1);
                expectEquals(
                    graph.compactNamFirstVisibleModelForTesting(), 1);
                graph.toggleCompactNamFavoriteForTesting(1);
                expectEquals(
                    graph.compactNamFirstVisibleModelForTesting(), 1,
                    "Favoriting must preserve NAM list scroll position.");
                graph.setCompactAssetFilterForTesting("COPPER");
                expectEquals(
                    graph.compactNamLibraryModelCountForTesting(), 1);
                graph.activateCompactNamModelForTesting(0);
                compactBrowser.finishNamModelLoadForTesting();
                expectEquals(
                    compactBrowser.namModelPath(0),
                    pedalModel.getFullPathName(),
                    "The inline NAM filter must be case-insensitive and "
                    "load the clicked match.");
            }

            const auto testCache =
                juce::File::getSpecialLocation(
                    juce::File::tempDirectory)
                    .getChildFile("megaDSP-tests")
                    .getChildFile(
                        "nam-model-library-cache.xml");
            expect(!testCache.existsAsFile()
                   || testCache.deleteFile());
            MegaDSPAudioProcessor coldBrowser;
            coldBrowser.prepareToPlay(48000.0, 64);
            coldBrowser.addModule(ModuleType::neuralAmpModeler);
            bool coldCompleted = false;
            const auto coldStart =
                juce::Time::getHighResolutionTicks();
            expect(coldBrowser.loadAdjacentNamModelAsync(
                0, 1,
                [&coldCompleted](const juce::Result& result)
                {
                    coldCompleted = result.wasOk();
                }).wasOk());
            const auto coldRequestMicros =
                juce::Time::highResolutionTicksToSeconds(
                    juce::Time::getHighResolutionTicks() - coldStart)
                * 1000000.0;
            expect(
                coldRequestMicros < 500000.0,
                "A cold catalog rebuild must not run on the UI thread.");
            coldBrowser.finishNamModelLoadForTesting();
            expect(coldCompleted);
            expectEquals(
                coldBrowser.namModelPath(0),
                ampModel.getFullPathName());

            MegaDSPAudioProcessor coldSearchBrowser;
            const auto coldSearchStart =
                juce::Time::getHighResolutionTicks();
            const auto immediateNamMatches =
                coldSearchBrowser.searchNamModels("clean");
            const auto coldSearchMicros =
                juce::Time::highResolutionTicksToSeconds(
                    juce::Time::getHighResolutionTicks()
                    - coldSearchStart)
                * 1000000.0;
            expect(
                coldSearchMicros < 500000.0,
                "A cold NAM search must schedule indexing off the UI thread.");
            expectEquals(
                static_cast<int>(immediateNamMatches.size()), 1);
            coldSearchBrowser.finishLibraryIndexForTesting();
            expectEquals(
                static_cast<int>(
                    coldSearchBrowser.searchNamModels("clean").size()),
                1);

            expect(browser.rebuildNamModelLibrary().wasOk());
            const auto persistedCache =
                juce::XmlDocument::parse(testCache);
            expect(persistedCache != nullptr);
            if (persistedCache != nullptr)
            {
                expectEquals(
                    persistedCache->getIntAttribute("schema"), 3);
                auto* cachedModel =
                    persistedCache->getChildByAttribute(
                        "path", "drive-pedal.nam");
                expect(cachedModel != nullptr);
                if (cachedModel != nullptr)
                    expect(cachedModel->hasAttribute("addedTimeMs"));
            }
            expect(pedalModel.replaceWithText("{ cache probe }"));
            MegaDSPAudioProcessor cachedBrowser;
            expectEquals(cachedBrowser.namModelLibrarySize(), 3);
            expect(pedalModel.replaceWithText(
                juce::String(pedalJson.dump())));

            MegaDSPAudioProcessor inherited;
            expectEquals(
                inherited.namModelRootDirectory().getFullPathName(),
                root.getFullPathName());
            expectEquals(inherited.namModelLibrarySize(), 3);
            const auto clean =
                inherited.searchNamModels("clean platform");
            expectEquals(static_cast<int>(clean.size()), 1);
            if (!clean.empty())
                expectEquals(
                    clean.front().file.getFullPathName(),
                    ampModel.getFullPathName());
            expectEquals(
                static_cast<int>(
                    inherited.searchNamModels("clean-amp").size()),
                1);
            expectEquals(
                static_cast<int>(
                    inherited.searchNamModels("copper drive").size()),
                1);
            const auto unloadedDeletable =
                root.getChildFile("delete-unloaded-model.nam");
            expect(unloadedDeletable.replaceWithText(linearNamJson()));
            expect(browser.rebuildNamModelLibrary().wasOk());
            expectEquals(browser.namModelLibrarySize(), 4);
            expect(browser.loadNamModel(0, ampModel).wasOk());
            expect(browser.deleteNamModel(unloadedDeletable).wasOk());
            expect(!unloadedDeletable.existsAsFile());
            expectEquals(
                browser.namModelPath(0), ampModel.getFullPathName());
            expectEquals(browser.namModelLibrarySize(), 3);
            const auto deletable =
                root.getChildFile("delete-this-model.nam");
            expect(deletable.replaceWithText(linearNamJson()));
            expect(browser.rebuildNamModelLibrary().wasOk());
            expectEquals(browser.namModelLibrarySize(), 4);
            expect(browser.loadNamModel(0, deletable).wasOk());
            expect(browser.setNamModelFavorite(
                       deletable, true).wasOk());
            expect(browser.deleteNamModel(0).wasOk());
            expect(!deletable.existsAsFile());
            expect(browser.namModelPath(0).isEmpty());
            expectEquals(browser.namModelLibrarySize(), 3);
            expect(!browser.isNamModelFavorite(deletable));
            expect(browser.setNamModelFavorite(
                       pedalModel, false).wasOk());
            root.deleteRecursively();
        }
    }
};

class NamReorderTests final : public juce::UnitTest
{
public:
    NamReorderTests() : juce::UnitTest("NAM reordering", "megaDSP") {}

    void runTest() override
    {
        const juce::ScopedJuceInitialiser_GUI gui;
        beginTest("Preparing isolated NAM and cabinet fixtures");
        const auto root = juce::File::getSpecialLocation(
            juce::File::tempDirectory).getNonexistentChildFile(
                "megaDSP-nam-reordering", {}, true);
        expect(root.createDirectory().wasOk());
        const auto model = root.getChildFile("model.nam");
        expect(model.replaceWithText(linearNamJson()));
        const auto cabinet = writeImpulse(root.getChildFile("cabinet.wav"), 24);
        const std::array<float, 5> gains { 0.35f, 0.45f, 0.55f, 0.60f, 0.40f };

        for (const auto enabled : { false, true })
        {
            beginTest(enabled
                ? "Moving across NAM preserves its enabled cabinet, controls, and audio"
                : "Moving across NAM retains a disabled cabinet for later use");
            MegaDSPAudioProcessor processor;
            processor.prepareToPlay(48000.0, 256);
            processor.addModule(ModuleType::guitarTuner);
            processor.addModule(ModuleType::neuralAmpModeler);
            processor.addModule(ModuleType::envelopeModulator);
            expect(processor.loadNamModel(1, model).wasOk());
            expect(processor.loadNamCabinetAsync(1, cabinet).wasOk());
            processor.finishNamModelLoadForTesting();
            expect(processor.setNamCabinetEnabled(1, enabled).wasOk());
            for (int control = 0; control < 5; ++control)
                processor.parameters.getParameter(controlParameterId(1, control))
                    ->setValueNotifyingHost(gains[static_cast<size_t>(control)]);
            processor.parameters.getParameter(controlParameterId(
                2, EnvelopeModulatorModule::mixControl))->setValueNotifyingHost(0.0f);

            const auto* nam = processor.getRack().activeModuleInstance(1);
            const auto* envelope = processor.getRack().activeModuleInstance(2);
            const auto* tuner = processor.getRack().activeModuleInstance(0);
            const auto levelMatch = processor.namLevelMatchDb(1);
            const auto baseline = render(processor, 24);
            expect(baseline > 0.00001f);
            struct Move { int source, destination, namSlot; };
            for (const auto move : {
                     Move { 2, 1, 2 }, Move { 1, 2, 1 },
                     Move { 2, 0, 2 }, Move { 0, 2, 1 } })
            {
                processor.moveSlot(move.source, move.destination);
                processor.flushPendingUpdatesForTesting();
                const auto& rack = processor.getRack();
                expect(rack.activeModuleInstance(move.namSlot) == nam,
                       "Reordering must move the prepared NAM, not recreate it.");
                expect(rack.hasNamCabinet(move.namSlot),
                       "Reordering dropped the prepared cabinet.");
                expect(processor.isNamCabinetEnabled(move.namSlot) == enabled);
                expectEquals(processor.namModelPath(move.namSlot), model.getFullPathName());
                expectEquals(processor.namCabinetPath(move.namSlot), cabinet.getFullPathName());
                expectWithinAbsoluteError(
                    processor.namLevelMatchDb(move.namSlot), levelMatch, 0.0001f);
                for (int control = 0; control < 5; ++control)
                    expectWithinAbsoluteError(
                        processor.parameters.getParameter(
                            controlParameterId(move.namSlot, control))->getValue(),
                        gains[static_cast<size_t>(control)], 0.0001f);
                for (int slot = 0; slot < 3; ++slot)
                {
                    if (rack.moduleType(slot) == ModuleType::envelopeModulator)
                        expect(rack.activeModuleInstance(slot) == envelope);
                    if (rack.moduleType(slot) == ModuleType::guitarTuner)
                        expect(rack.activeModuleInstance(slot) == tuner);
                }
                expectWithinAbsoluteError(render(processor, 1), baseline,
                                          baseline * 0.005f + 0.000001f,
                                          "The first block after a move must retain its level.");
                expectWithinAbsoluteError(render(processor, 24), baseline,
                                          baseline * 0.005f + 0.000001f,
                                          "Reordering a dry modulation path must not change NAM level.");
            }

            beginTest("Reorder undo, redo, and project restore retain the cabinet");
            expect(processor.undo());
            expect(processor.getRack().hasNamCabinet(2));
            expect(processor.isNamCabinetEnabled(2) == enabled);
            expectEquals(processor.namCabinetPath(2), cabinet.getFullPathName());
            expect(processor.redo());
            expect(processor.getRack().hasNamCabinet(1));
            expect(processor.isNamCabinetEnabled(1) == enabled);
            expectEquals(processor.namCabinetPath(1), cabinet.getFullPathName());
            juce::MemoryBlock saved;
            processor.getStateInformation(saved);
            MegaDSPAudioProcessor restored;
            restored.prepareToPlay(48000.0, 256);
            restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
            expect(restored.getRack().hasNamCabinet(1));
            expect(restored.isNamCabinetEnabled(1) == enabled);
            expectEquals(restored.namCabinetPath(1), cabinet.getFullPathName());
            expectWithinAbsoluteError(render(restored, 24), baseline,
                                      baseline * 0.005f + 0.000001f);
        }

        beginTest("Two NAM instances keep independent cabinets and controls when swapped");
        {
            const auto secondCabinet = writeImpulse(root.getChildFile("second.wav"), 48);
            MegaDSPAudioProcessor processor;
            processor.prepareToPlay(48000.0, 256);
            for (int slot = 0; slot < 2; ++slot)
            {
                processor.addModule(ModuleType::neuralAmpModeler);
                expect(processor.loadNamModel(slot, model).wasOk());
                expect(processor.loadNamCabinetAsync(
                    slot, slot == 0 ? cabinet : secondCabinet).wasOk());
                processor.finishNamModelLoadForTesting();
                expect(processor.setNamCabinetEnabled(slot, slot == 1).wasOk());
                processor.parameters.getParameter(controlParameterId(slot, 0))
                    ->setValueNotifyingHost(slot == 0 ? 0.3f : 0.7f);
            }
            const auto* first = processor.getRack().activeModuleInstance(0);
            const auto* second = processor.getRack().activeModuleInstance(1);
            const auto baseline = render(processor, 24);
            for (const auto source : { 0, 1 })
            {
                processor.moveSlot(source, 1 - source);
                expect(processor.getRack().activeModuleInstance(1 - source) == first);
                expect(processor.getRack().activeModuleInstance(source) == second);
                expect(processor.getRack().hasNamCabinet(1 - source));
                expect(processor.getRack().hasNamCabinet(source));
                expect(!processor.isNamCabinetEnabled(1 - source));
                expect(processor.isNamCabinetEnabled(source));
                expectEquals(processor.namCabinetPath(1 - source), cabinet.getFullPathName());
                expectEquals(processor.namCabinetPath(source), secondCabinet.getFullPathName());
                expectWithinAbsoluteError(processor.parameters.getParameter(
                    controlParameterId(1 - source, 0))->getValue(), 0.3f, 0.0001f);
                expectWithinAbsoluteError(processor.parameters.getParameter(
                    controlParameterId(source, 0))->getValue(), 0.7f, 0.0001f);
                expectWithinAbsoluteError(render(processor, 24), baseline,
                                          baseline * 0.005f + 0.000001f);
            }
        }

        beginTest("Prepared NAM and cabinet move without their source files");
        {
            MegaDSPAudioProcessor processor;
            processor.prepareToPlay(48000.0, 256);
            processor.addModule(ModuleType::neuralAmpModeler);
            processor.addModule(ModuleType::envelopeModulator);
            expect(processor.loadNamModel(0, model).wasOk());
            expect(processor.loadNamCabinetAsync(0, cabinet).wasOk());
            processor.finishNamModelLoadForTesting();
            expect(processor.setNamCabinetEnabled(0, true).wasOk());
            processor.parameters.getParameter(controlParameterId(
                1, EnvelopeModulatorModule::mixControl))->setValueNotifyingHost(0.0f);
            const auto* nam = processor.getRack().activeModuleInstance(0);
            const auto baseline = render(processor, 24);
            expect(model.deleteFile());
            expect(cabinet.deleteFile());
            for (const auto source : { 1, 0, 1, 0 })
            {
                processor.moveSlot(source, 1 - source);
                expect(processor.getRack().activeModuleInstance(source) == nam);
                expect(processor.getRack().hasNamCabinet(source));
                expect(processor.isNamCabinetEnabled(source));
                expectWithinAbsoluteError(render(processor, 1), baseline,
                                          baseline * 0.005f + 0.000001f);
            }
        }
        expect(root.deleteRecursively());
    }

private:
    float render(MegaDSPAudioProcessor& processor, int blocks)
    {
        juce::AudioBuffer<float> buffer(2, 256);
        juce::MidiBuffer midi;
        for (int block = 0; block < blocks; ++block)
        {
            juce::FloatVectorOperations::fill(buffer.getWritePointer(0), 0.2f, 256);
            juce::FloatVectorOperations::fill(buffer.getWritePointer(1), 0.08f, 256);
            processor.processBlock(buffer, midi);
        }
        bool finite = true;
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < 256; ++sample)
                finite = finite && std::isfinite(buffer.getSample(channel, sample));
        expect(finite);
        const auto rms = buffer.getRMSLevel(0, 0, 256);
        const auto expectedRight = rms * 0.4f;
        expectWithinAbsoluteError(buffer.getRMSLevel(1, 0, 256),
                                  expectedRight, expectedRight * 0.001f + 0.000001f);
        return rms;
    }
};

NamReorderTests namReorderTests;
NamRegistryTests namRegistryTests;
NamDspTests namDspTests;
LibraryRebuildTests libraryRebuildTests;
NamStateTests namStateTests;
} // namespace
