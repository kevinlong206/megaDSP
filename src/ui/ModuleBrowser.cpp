#include "ModuleBrowser.h"

#include "GraphStyle.h"

#include <algorithm>
#include <map>
#include <optional>

namespace megadsp::ui
{
std::vector<ModuleBrowserGroup> filterAndGroupModules(
    const juce::String& query)
{
    juce::StringArray tokens;
    tokens.addTokens(query.toLowerCase(), " \t\r\n", "");
    tokens.removeEmptyStrings();

    std::map<int, ModuleBrowserGroup> grouped;
    for (const auto& definition : moduleRegistry())
    {
        if (definition.type == ModuleType::empty)
            continue;

        const auto haystack = (
            juce::String(definition.displayName) + " "
            + moduleCategoryName(definition.category) + " "
            + definition.description + " " + definition.searchTags)
                                  .toLowerCase();
        const auto matches = std::all_of(
            tokens.begin(), tokens.end(),
            [&haystack](const auto& token) { return haystack.contains(token); });
        if (!matches)
            continue;

        const auto key = static_cast<int>(definition.category);
        auto iterator = grouped.try_emplace(
            key, ModuleBrowserGroup { definition.category, {} }).first;
        iterator->second.modules.push_back(definition.type);
    }

    std::vector<ModuleBrowserGroup> result;
    result.reserve(grouped.size());
    for (auto& [key, group] : grouped)
    {
        juce::ignoreUnused(key);
        result.push_back(std::move(group));
    }
    return result;
}

class ModuleBrowser::ResultsComponent final : public juce::Component
{
public:
    ResultsComponent()
    {
        setTitle("Module results");
        setHelpText("Filtered modules grouped by category.");
    }

private:
    std::unique_ptr<juce::AccessibilityHandler>
        createAccessibilityHandler() override
    {
        return std::make_unique<juce::AccessibilityHandler>(
            *this, juce::AccessibilityRole::list);
    }
};

class ModuleBrowser::ResultRow final : public juce::Component
{
public:
    ResultRow(ModuleBrowser& ownerToUse, ModuleType typeToUse)
        : owner(ownerToUse), type(typeToUse)
    {
        const auto& definition = moduleDefinition(type);
        setTitle(definition.displayName);
        setDescription(definition.description);
        setHelpText("Click or press Enter to add this module.");
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus(true);
    }

    void setSelected(bool shouldBeSelected)
    {
        if (selected == shouldBeSelected)
            return;
        selected = shouldBeSelected;
        repaint();
    }

    ModuleType moduleType() const { return type; }

#if defined(MEGADSP_TESTS)
    bool accessibilitySupportsPressForTesting()
    {
        return createAccessibilityHandler()->getActions().contains(
            juce::AccessibilityActionType::press);
    }

    bool accessibilityIsSelectedForTesting()
    {
        return createAccessibilityHandler()->getCurrentState().isSelected();
    }

    bool invokeAccessibilityPressForTesting()
    {
        return createAccessibilityHandler()->getActions().invoke(
            juce::AccessibilityActionType::press);
    }
#endif

    void paint(juce::Graphics& graphics) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        graphics.setColour(selected ? accent.withAlpha(0.22f)
                                    : juce::Colour(0xff17202a));
        graphics.fillRoundedRectangle(bounds, 5.0f);
        graphics.setColour(selected ? accent : juce::Colour(0xff3a4553));
        graphics.drawRoundedRectangle(bounds, 5.0f, selected ? 1.5f : 1.0f);

        const auto& definition = moduleDefinition(type);
        auto text = getLocalBounds().reduced(10, 4);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        graphics.drawText(definition.displayName, text,
                          juce::Justification::centredLeft);
        graphics.setColour(selected ? accent : juce::Colour(0xff738092));
        graphics.drawText("+", text.removeFromRight(18),
                          juce::Justification::centred);
    }

    void mouseEnter(const juce::MouseEvent&) override
    {
        owner.selectType(type);
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        if (event.mouseWasClicked())
        {
            owner.selectType(type);
            owner.activateSelection();
        }
    }

private:
    class RowAccessibilityHandler final
        : public juce::AccessibilityHandler
    {
    public:
        explicit RowAccessibilityHandler(ResultRow& rowToUse)
            : juce::AccessibilityHandler(
                  rowToUse, juce::AccessibilityRole::listItem,
                  makeActions(rowToUse)),
              row(rowToUse)
        {
        }

        juce::AccessibleState getCurrentState() const override
        {
            auto state =
                juce::AccessibilityHandler::getCurrentState()
                    .withSelectable();
            return row.selected ? state.withSelected() : state;
        }

    private:
        static juce::AccessibilityActions makeActions(
            ResultRow& row)
        {
            juce::AccessibilityActions actions;
            actions.addAction(
                juce::AccessibilityActionType::press,
                [&row]
                {
                    row.owner.selectType(row.type);
                    row.owner.activateSelection();
                });
            return actions;
        }

        ResultRow& row;
    };

    std::unique_ptr<juce::AccessibilityHandler>
        createAccessibilityHandler() override
    {
        return std::make_unique<RowAccessibilityHandler>(*this);
    }

    ModuleBrowser& owner;
    ModuleType type;
    bool selected = false;
};

ModuleBrowser::~ModuleBrowser() = default;

ModuleBrowser::ModuleBrowser(
    juce::Colour background,
    std::function<void(ModuleType)> moduleChosen)
    : backgroundColour(background), onModuleChosen(std::move(moduleChosen)),
      results(std::make_unique<ResultsComponent>())
{
    setSize(440, 350);
    setTitle("Search Modules");
    setHelpText("Search for a module by name, category, or purpose.");
    setWantsKeyboardFocus(true);
    addKeyListener(this);

    heading.setText("Search Modules", juce::dontSendNotification);
    heading.setFont(juce::FontOptions(17.0f, juce::Font::bold));
    heading.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(heading);

    search.setTextToShowWhenEmpty("Search modules...", juce::Colour(0xff7f8b99));
    search.setTitle("Search modules");
    search.setDescription(
        "Filter by module name, category, description, or search tags.");
    search.setHelpText(
        "Type to filter. Use Up and Down to select, Enter to add, and Escape to close.");
    search.setMultiLine(false);
    search.setReturnKeyStartsNewLine(false);
    search.setColour(juce::TextEditor::backgroundColourId,
                     juce::Colour(0xff111820));
    search.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    search.setColour(juce::TextEditor::outlineColourId,
                     juce::Colour(0xff3a4553));
    search.setColour(juce::TextEditor::focusedOutlineColourId, accent);
    search.addKeyListener(this);
    search.onTextChange = [this] { rebuildResults(); };
    search.onReturnKey = [this] { activateSelection(); };
    search.onEscapeKey = [this] { dismiss(); };
    addAndMakeVisible(search);

    viewport.setViewedComponent(results.get(), false);
    viewport.setScrollBarsShown(true, false);
    viewport.setColour(juce::ScrollBar::thumbColourId, accent.withAlpha(0.5f));
    addAndMakeVisible(viewport);

    noResults.setText("No matching modules", juce::dontSendNotification);
    noResults.setTitle("No matching modules");
    noResults.setDescription("No modules match the current search.");
    noResults.setHelpText("Change or clear the search to show modules.");
    noResults.setJustificationType(juce::Justification::centred);
    noResults.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    noResults.setColour(juce::Label::textColourId, juce::Colour(0xffa8b3c0));
    results->addAndMakeVisible(noResults);

    rebuildResults();
}

void ModuleBrowser::paint(juce::Graphics& graphics)
{
    graphics.fillAll(backgroundColour.brighter(0.12f));
    graphics.setColour(juce::Colour(0xff3a4553));
    graphics.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f),
                                  7.0f, 1.0f);
}

void ModuleBrowser::resized()
{
    auto bounds = getLocalBounds().reduced(10);
    heading.setBounds(bounds.removeFromTop(28));
    bounds.removeFromTop(6);
    search.setBounds(bounds.removeFromTop(34));
    bounds.removeFromTop(7);
    viewport.setBounds(bounds);
    layoutResults();
}

void ModuleBrowser::focusSearch()
{
    search.grabKeyboardFocus();
}

#if defined(MEGADSP_TESTS)
int ModuleBrowser::resultRowCountForTesting() const
{
    return static_cast<int>(rows.size());
}

bool ModuleBrowser::resultRowSupportsPressForTesting(int index) const
{
    if (!juce::isPositiveAndBelow(index, static_cast<int>(rows.size())))
        return false;
    return rows[static_cast<size_t>(index)]
        ->accessibilitySupportsPressForTesting();
}

bool ModuleBrowser::resultRowIsSelectedForTesting(int index) const
{
    if (!juce::isPositiveAndBelow(index, static_cast<int>(rows.size())))
        return false;
    return rows[static_cast<size_t>(index)]
        ->accessibilityIsSelectedForTesting();
}

bool ModuleBrowser::pressResultRowForTesting(int index)
{
    if (!juce::isPositiveAndBelow(index, static_cast<int>(rows.size())))
        return false;
    return rows[static_cast<size_t>(index)]
        ->invokeAccessibilityPressForTesting();
}

#endif

bool ModuleBrowser::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    if (key.getKeyCode() == juce::KeyPress::upKey)
    {
        selectIndex(selectedIndex <= 0
                        ? (rows.empty() ? -1 : 0)
                        : selectedIndex - 1);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::downKey)
    {
        selectIndex(rows.empty()
                        ? -1
                        : juce::jmin(static_cast<int>(rows.size()) - 1,
                                     selectedIndex + 1));
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::returnKey)
    {
        activateSelection();
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::escapeKey)
    {
        dismiss();
        return true;
    }
    return false;
}

void ModuleBrowser::rebuildResults()
{
    std::optional<ModuleType> previousSelection;
    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(rows.size()))
        previousSelection = rows[static_cast<size_t>(selectedIndex)]->moduleType();

    results->removeAllChildren();
    categoryLabels.clear();
    rows.clear();
    groupSizes.clear();
    results->addAndMakeVisible(noResults);

    for (const auto& group : filterAndGroupModules(search.getText()))
    {
        auto label = std::make_unique<juce::Label>();
        label->setText(moduleCategoryName(group.category),
                       juce::dontSendNotification);
        label->setTitle(juce::String(moduleCategoryName(group.category))
                        + " category");
        label->setFont(juce::FontOptions(11.0f, juce::Font::bold));
        label->setColour(juce::Label::textColourId, accent);
        results->addAndMakeVisible(*label);
        categoryLabels.push_back(std::move(label));
        groupSizes.push_back(static_cast<int>(group.modules.size()));

        for (const auto type : group.modules)
        {
            auto row = std::make_unique<ResultRow>(*this, type);
            results->addAndMakeVisible(*row);
            rows.push_back(std::move(row));
        }
    }

    noResults.setVisible(rows.empty());
    selectedIndex = -1;
    if (!rows.empty())
    {
        auto nextSelection = 0;
        if (previousSelection.has_value())
            for (int index = 0; index < static_cast<int>(rows.size()); ++index)
                if (rows[static_cast<size_t>(index)]->moduleType()
                    == *previousSelection)
                {
                    nextSelection = index;
                    break;
                }
        selectIndex(nextSelection);
    }
    layoutResults();
}

void ModuleBrowser::layoutResults()
{
    constexpr int categoryHeight = 21;
    constexpr int rowHeight = 34;
    constexpr int gap = 3;
    const auto width = juce::jmax(1, viewport.getWidth() - 12);
    int y = 0;
    int rowIndex = 0;
    for (int group = 0; group < static_cast<int>(groupSizes.size()); ++group)
    {
        categoryLabels[static_cast<size_t>(group)]->setBounds(
            5, y, width - 10, categoryHeight);
        y += categoryHeight;
        for (int index = 0; index < groupSizes[static_cast<size_t>(group)];
             ++index)
        {
            rows[static_cast<size_t>(rowIndex++)]->setBounds(
                2, y, width - 4, rowHeight);
            y += rowHeight + gap;
        }
        y += 3;
    }

    if (rows.empty())
    {
        y = juce::jmax(100, viewport.getHeight());
        noResults.setBounds(0, 0, width, y);
    }
    results->setSize(width, juce::jmax(y, viewport.getHeight()));
}

void ModuleBrowser::selectIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(rows.size()))
        index = -1;
    const auto previousIndex = selectedIndex;
    selectedIndex = index;
    for (int row = 0; row < static_cast<int>(rows.size()); ++row)
        rows[static_cast<size_t>(row)]->setSelected(row == selectedIndex);

    if (selectedIndex < 0)
        return;
    const auto rowBounds = rows[static_cast<size_t>(selectedIndex)]->getBounds();
    const auto viewTop = viewport.getViewPositionY();
    if (rowBounds.getY() < viewTop)
        viewport.setViewPosition(0, rowBounds.getY());
    else if (rowBounds.getBottom() > viewTop + viewport.getHeight())
        viewport.setViewPosition(
            0, rowBounds.getBottom() - viewport.getHeight());

    if (previousIndex != selectedIndex)
    {
        if (auto* handler = results->getAccessibilityHandler())
            handler->notifyAccessibilityEvent(
                juce::AccessibilityEvent::rowSelectionChanged);
        juce::AccessibilityHandler::postAnnouncement(
            rows[static_cast<size_t>(selectedIndex)]->getTitle()
                + " selected",
            juce::AccessibilityHandler::AnnouncementPriority::low);
    }
}

void ModuleBrowser::selectType(ModuleType type)
{
    for (int index = 0; index < static_cast<int>(rows.size()); ++index)
        if (rows[static_cast<size_t>(index)]->moduleType() == type)
        {
            selectIndex(index);
            return;
        }
}

void ModuleBrowser::activateSelection()
{
    if (activated || selectedIndex < 0
        || selectedIndex >= static_cast<int>(rows.size()))
        return;

    activated = true;
    const auto type = rows[static_cast<size_t>(selectedIndex)]->moduleType();
    if (onModuleChosen)
        onModuleChosen(type);
    dismiss();
}

void ModuleBrowser::dismiss()
{
    if (auto* callout = findParentComponentOfClass<juce::CallOutBox>())
        callout->dismiss();
}
} // namespace megadsp::ui
