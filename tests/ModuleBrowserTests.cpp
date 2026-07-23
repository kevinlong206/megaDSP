#include "ui/ModuleBrowser.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <optional>
#include <utility>
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
        const auto colorModules = flatten(
            megadsp::ui::filterAndGroupModules("SATURATION & color"));
        expectEquals(static_cast<int>(colorModules.size()), 4);
        expect(std::find(colorModules.begin(), colorModules.end(),
                         megadsp::ModuleType::saturator)
               != colorModules.end());
        expect(std::find(colorModules.begin(), colorModules.end(),
                         megadsp::ModuleType::signalDecay)
               != colorModules.end());
        expect(std::find(colorModules.begin(), colorModules.end(),
                        megadsp::ModuleType::analogTape)
               != colorModules.end());
        expect(std::find(colorModules.begin(), colorModules.end(),
                        megadsp::ModuleType::harmonicMirage)
               != colorModules.end());

        beginTest("Description matching finds module copy");
        expectOnly("evolving randomized", megadsp::ModuleType::randomGranulizer);

        beginTest("Search tag matching finds module");
        const auto bbdModules = flatten(
            megadsp::ui::filterAndGroupModules("BBD"));
        expectEquals(static_cast<int>(bbdModules.size()), 2);
        expect(std::find(bbdModules.begin(), bbdModules.end(),
                         megadsp::ModuleType::vintageChorus)
               != bbdModules.end());
        expect(std::find(bbdModules.begin(), bbdModules.end(),
                         megadsp::ModuleType::studioFlanger)
               != bbdModules.end());
        expectOnly("stutter reverse", megadsp::ModuleType::beatPermuter);
        expectOnly("phase freeze", megadsp::ModuleType::spectralPrism);
        expectOnly("tuned metallic", megadsp::ModuleType::resonantMatrix);
        expectOnly("nonlinear envelope", megadsp::ModuleType::wavefoldGarden);
        expectOnly("reel cassette warmth", megadsp::ModuleType::analogTape);

        beginTest("Next-ten search identities are unique");
        const std::array nextTenSearches {
            std::pair { "noise sidechain envelope",
                        megadsp::ModuleType::gateExpander },
            std::pair { "transient punch",
                        megadsp::ModuleType::transientDesigner },
            std::pair { "multiband crossover",
                        megadsp::ModuleType::multibandCompressor },
            std::pair { "allpass stages",
                        megadsp::ModuleType::studioPhaser },
            std::pair { "through zero jet",
                        megadsp::ModuleType::studioFlanger },
            std::pair { "diffusion echo cloud",
                        megadsp::ModuleType::diffusionDelay },
            std::pair { "shimmer octave fifth",
                        megadsp::ModuleType::pitchBloom },
            std::pair { "hilbert sideband",
                        megadsp::ModuleType::frequencyLab },
            std::pair { "autopan trajectory",
                        megadsp::ModuleType::spatialOrbit },
            std::pair { "bitcrush dropout",
                        megadsp::ModuleType::signalDecay }
        };
        for (const auto& [query, expected] : nextTenSearches)
            expectOnly(query, expected);

        beginTest("Differentiating module search identities are unique");
        const std::array differentiatingSearches {
            std::pair { "adaptive resonance suppression",
                        megadsp::ModuleType::resonanceTamer },
            std::pair { "tonal balance contour",
                        megadsp::ModuleType::spectralBalance },
            std::pair { "correlation alignment mono",
                        megadsp::ModuleType::phaseCoherence },
            std::pair { "lufs automation",
                        megadsp::ModuleType::loudnessRider },
            std::pair { "clipper oversampling",
                        megadsp::ModuleType::adaptiveClipper },
            std::pair { "frequency canvas diffusion",
                        megadsp::ModuleType::spectralDelayCanvas },
            std::pair { "vocal tract creature",
                        megadsp::ModuleType::formantForge },
            std::pair { "partial resynthesis subharmonic",
                        megadsp::ModuleType::harmonicMirage },
            std::pair { "lorenz rossler",
                        megadsp::ModuleType::chaosField },
            std::pair { "history mosaic tile",
                        megadsp::ModuleType::timeMosaic }
        };
        for (const auto& [query, expected] : differentiatingSearches)
            expectOnly(query, expected);

        beginTest("Next-ten browser categories match the approved roster");
        const std::array nextTenCategories {
            std::pair { megadsp::ModuleType::gateExpander,
                        megadsp::ModuleCategory::dynamics },
            std::pair { megadsp::ModuleType::transientDesigner,
                        megadsp::ModuleCategory::dynamics },
            std::pair { megadsp::ModuleType::multibandCompressor,
                        megadsp::ModuleCategory::dynamics },
            std::pair { megadsp::ModuleType::studioPhaser,
                        megadsp::ModuleCategory::modulation },
            std::pair { megadsp::ModuleType::studioFlanger,
                        megadsp::ModuleCategory::modulation },
            std::pair { megadsp::ModuleType::diffusionDelay,
                        megadsp::ModuleCategory::delayAndEcho },
            std::pair { megadsp::ModuleType::pitchBloom,
                        megadsp::ModuleCategory::reverbAndSpace },
            std::pair { megadsp::ModuleType::frequencyLab,
                        megadsp::ModuleCategory::glitchAndCreative },
            std::pair { megadsp::ModuleType::spatialOrbit,
                        megadsp::ModuleCategory::stereoAndUtility },
            std::pair { megadsp::ModuleType::signalDecay,
                        megadsp::ModuleCategory::saturationAndColor }
        };
        for (const auto& [type, expected] : nextTenCategories)
            expect(megadsp::moduleDefinition(type).category == expected);

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
            expectEquals(static_cast<int>(eqGroup.modules.size()), 4);
            if (eqGroup.modules.size() == 4)
            {
                expect(eqGroup.modules[0] == megadsp::ModuleType::equalizer);
                expect(eqGroup.modules[1]
                       == megadsp::ModuleType::dynamicEqualizer);
                expect(eqGroup.modules[2]
                       == megadsp::ModuleType::resonanceTamer);
                expect(eqGroup.modules[3]
                       == megadsp::ModuleType::spectralBalance);
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
