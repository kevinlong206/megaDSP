#include "ui/ModuleBrowser.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <optional>
#include <vector>

namespace
{
class ModuleBrowserTests final : public juce::UnitTest
{
public:
    ModuleBrowserTests() : juce::UnitTest("Module browser", "megaDSP") {}

    void runTest() override
    {
        const auto flatten = [](const auto& groups)
        {
            std::vector<megadsp::ModuleType> modules;
            for (const auto& group : groups)
                modules.insert(modules.end(), group.modules.begin(),
                               group.modules.end());
            return modules;
        };
        const auto expectOnly = [this, &flatten](
                                    const juce::String& query,
                                    megadsp::ModuleType expected)
        {
            const auto modules = flatten(
                megadsp::ui::filterAndGroupModules(query));
            expectEquals(static_cast<int>(modules.size()), 1);
            if (!modules.empty())
                expect(modules.front() == expected);
        };

        beginTest("Empty query includes every non-empty module");
        const auto allGroups = megadsp::ui::filterAndGroupModules({});
        const auto allModules = flatten(allGroups);
        expectEquals(static_cast<int>(allModules.size()),
                     megadsp::moduleTypeCount - 1);
        expect(std::find(allModules.begin(), allModules.end(),
                         megadsp::ModuleType::empty) == allModules.end());
        expect(megadsp::ui::filterAndGroupModules("Empty").empty());

        beginTest("Name matching is case-insensitive");
        expectOnly("m/S", megadsp::ModuleType::midSideDecoder);

        beginTest("Category matching supports multiple tokens");
        expectOnly("SATURATION & color", megadsp::ModuleType::saturator);

        beginTest("Description matching finds module copy");
        expectOnly("evolving randomized", megadsp::ModuleType::randomGranulizer);

        beginTest("Search tag matching finds module");
        expectOnly("BBD", megadsp::ModuleType::vintageChorus);
        expectOnly("stutter reverse", megadsp::ModuleType::beatPermuter);
        expectOnly("phase freeze", megadsp::ModuleType::spectralPrism);
        expectOnly("tuned metallic", megadsp::ModuleType::resonantMatrix);
        expectOnly("nonlinear envelope", megadsp::ModuleType::wavefoldGarden);

        beginTest("No matches returns no groups");
        expect(megadsp::ui::filterAndGroupModules(
                   "definitely-not-a-module").empty());

        beginTest("Groups and modules preserve approved ordering");
        for (size_t index = 1; index < allGroups.size(); ++index)
            expect(static_cast<int>(allGroups[index - 1].category)
                   < static_cast<int>(allGroups[index].category));
        expect(!allGroups.empty());
        if (!allGroups.empty())
        {
            const auto& eqGroup = allGroups.front();
            expect(eqGroup.category == megadsp::ModuleCategory::eqAndFilters);
            expectEquals(static_cast<int>(eqGroup.modules.size()), 2);
            if (eqGroup.modules.size() == 2)
            {
                expect(eqGroup.modules[0] == megadsp::ModuleType::equalizer);
                expect(eqGroup.modules[1]
                       == megadsp::ModuleType::dynamicEqualizer);
            }
        }

        beginTest("Result rows are exposed for accessibility");
        std::optional<megadsp::ModuleType> chosen;
        megadsp::ui::ModuleBrowser browser(
            juce::Colours::black,
            [&chosen](megadsp::ModuleType type) { chosen = type; });
        expectEquals(browser.resultRowCountForTesting(),
                     megadsp::moduleTypeCount - 1);
        beginTest("Result rows expose selected accessibility state");
        auto selectedCount = 0;
        for (int index = 0;
             index < browser.resultRowCountForTesting(); ++index)
            selectedCount += browser.resultRowIsSelectedForTesting(
                                 index)
                                 ? 1 : 0;
        expectEquals(selectedCount, 1);
        beginTest("Result rows expose an accessibility press action");
        if (browser.resultRowCountForTesting() > 0)
        {
            expect(browser.resultRowSupportsPressForTesting(0));
            expect(browser.pressResultRowForTesting(0));
            expect(chosen.has_value());
        }
    }
};

ModuleBrowserTests moduleBrowserTests;
} // namespace
