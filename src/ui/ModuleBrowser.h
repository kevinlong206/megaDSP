#pragma once

#include "../modules/ModuleRegistry.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace megadsp::ui
{
struct ModuleBrowserGroup
{
    ModuleCategory category;
    std::vector<ModuleType> modules;
};

std::vector<ModuleBrowserGroup> filterAndGroupModules(
    const juce::String& query);

class ModuleBrowser final : public juce::Component,
                            private juce::KeyListener
{
public:
    ModuleBrowser(juce::Colour background,
                  std::function<void(ModuleType)> moduleChosen);
    ~ModuleBrowser() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void focusSearch();

#if defined(MEGADSP_TESTS)
    int resultRowCountForTesting() const;
    bool resultRowSupportsPressForTesting(int index) const;
    bool resultRowIsSelectedForTesting(int index) const;
    bool pressResultRowForTesting(int index);
#endif

private:
    using juce::Component::keyPressed;

    class ResultRow;
    class ResultsComponent;

    bool keyPressed(const juce::KeyPress&, juce::Component*) override;
    void rebuildResults();
    void layoutResults();
    void selectIndex(int index);
    void selectType(ModuleType type);
    void activateSelection();
    void dismiss();

    juce::Colour backgroundColour;
    std::function<void(ModuleType)> onModuleChosen;
    juce::Label heading;
    juce::TextEditor search;
    std::unique_ptr<ResultsComponent> results;
    juce::Viewport viewport;
    juce::Label noResults;
    std::vector<std::unique_ptr<juce::Label>> categoryLabels;
    std::vector<std::unique_ptr<ResultRow>> rows;
    std::vector<int> groupSizes;
    int selectedIndex = -1;
    bool activated = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModuleBrowser)
};
} // namespace megadsp::ui
