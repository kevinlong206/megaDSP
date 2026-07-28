#include "Diagnostics.h"
#include "modules/ModuleRegistry.h"

#include <juce_core/juce_core.h>

namespace
{
class DiagnosticsTests final : public juce::UnitTest
{
public:
    DiagnosticsTests() : UnitTest("Diagnostics") {}

    void runTest() override
    {
        auto& diagnostics = megadsp::Diagnostics::instance();
        diagnostics.setEnabled(false);
        const auto folder = diagnostics.folder();
        folder.deleteRecursively();

        beginTest("Diagnostics write bounded event context");
        diagnostics.setEnabled(true);
        expect(diagnostics.isEnabled());
        diagnostics.log("test", "message");
        diagnostics.logRealtime(
            megadsp::DiagnosticEvent::audioConfiguration, 7, 512, 2);
        diagnostics.logRealtime(
            megadsp::DiagnosticEvent::audioCallbackWorkload,
            7, 420, 3100, 2667,
            static_cast<std::int64_t>(
                (static_cast<std::uint64_t>(3) << 32) | 6));
        diagnostics.logRealtime(
            megadsp::DiagnosticEvent::moduleWorkload,
            7, static_cast<int>(megadsp::ModuleType::convolutionReverb),
            140, 190, 12);
        juce::Thread::sleep(250);
        diagnostics.setEnabled(false);
        expect(!diagnostics.isEnabled());
        expect(!folder.getChildFile("diagnostics.enabled").existsAsFile());
        expect(folder.getChildFile("diagnostics.disabled").existsAsFile());

        const auto contents =
            folder.getChildFile("megaDSP.log").loadFileAsString();
        expect(contents.contains("test message"));
        expect(contents.contains(
            "audio-config instance=7 a=512 b=2"));
        expect(contents.contains(
            "audio-workload instance=7 avg_us=420 max_us=3100 "
            "deadline_us=2667 max_utilization_pct=116 overruns=3 graphs=6"));
        expect(contents.contains(
            "module-profile instance=7 module=\"Convolution Reverb\""));
        folder.deleteRecursively();
    }
};

DiagnosticsTests diagnosticsTests;
} // namespace
