#include "Diagnostics.h"

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
        folder.deleteRecursively();
    }
};

DiagnosticsTests diagnosticsTests;
} // namespace
