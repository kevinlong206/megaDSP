#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Diagnostics.h"
#include "ReaperEmbeddedUI.h"
#include "ui/GraphStyle.h"
#include "ui/GuiLayout.h"

#include <NAM/wavenet/a2_fast.h>
#include <json.hpp>
#include <juce_audio_formats/juce_audio_formats.h>

#include <cstring>
#include <optional>

thread_local bool
    MegaDSPAudioProcessor::publishingSnapshotParameters = false;

namespace
{
class ProcessorStateUndoAction final : public juce::UndoableAction
{
public:
    ProcessorStateUndoAction(
        MegaDSPAudioProcessor& processorToUse,
        juce::MemoryBlock beforeToUse,
        juce::MemoryBlock afterToUse)
        : processor(processorToUse),
          before(std::move(beforeToUse)),
          after(std::move(afterToUse))
    {
    }

    bool perform() override
    {
        if (firstPerform)
        {
            firstPerform = false;
            return true;
        }
        processor.restoreUndoState(after);
        return true;
    }

    bool undo() override
    {
        processor.restoreUndoState(before);
        return true;
    }

    int getSizeInUnits() override { return 1; }

private:
    MegaDSPAudioProcessor& processor;
    juce::MemoryBlock before;
    juce::MemoryBlock after;
    bool firstPerform = true;
};

class ScopedProcessorUndoTransaction final
{
public:
    ScopedProcessorUndoTransaction(
        MegaDSPAudioProcessor& processorToUse,
        const juce::String& name)
        : processor(processorToUse)
    {
        processor.beginUndoTransaction(name);
    }

    ~ScopedProcessorUndoTransaction()
    {
        processor.endUndoTransaction();
    }

private:
    MegaDSPAudioProcessor& processor;
};

bool memoryBlocksMatch(
    const juce::MemoryBlock& left,
    const juce::MemoryBlock& right)
{
    return left.getSize() == right.getSize()
        && (left.getSize() == 0
            || std::memcmp(
                   left.getData(), right.getData(), left.getSize()) == 0);
}

juce::File globalSettingsFile()
{
#if defined(MEGADSP_TESTS)
    return juce::File::getSpecialLocation(
               juce::File::tempDirectory)
        .getChildFile("megaDSP-tests")
        .getChildFile("settings.xml");
#else
    return juce::File::getSpecialLocation(
               juce::File::userApplicationDataDirectory)
        .getChildFile("megaDSP")
        .getChildFile("settings.xml");
#endif
}

juce::File namModelLibraryCacheFile()
{
    return globalSettingsFile().getSiblingFile(
        "nam-model-library-cache.xml");
}

juce::File impulseResponseLibraryCacheFile()
{
    return globalSettingsFile().getSiblingFile(
        "impulse-response-library-cache.xml");
}

juce::String impulseResponseSearchableText(
    const ImpulseResponseLibraryEntry& entry)
{
    return (entry.relativePath + " "
            + entry.file.getFileNameWithoutExtension())
        .toLowerCase();
}

juce::String namModelSearchableText(
    const NamModelLibraryEntry& entry)
{
    return (entry.relativePath + " "
            + entry.file.getFileNameWithoutExtension() + " "
            + entry.displayName + " " + entry.metadataTitle + " "
            + entry.architecture + " " + entry.captureDescription + " "
            + entry.gearType)
        .toLowerCase();
}

struct NamSearchTerm
{
    juce::String text;
    bool excluded = false;
    bool phrase = false;
};

std::vector<NamSearchTerm> parseNamSearchQuery(
    const juce::String& query)
{
    std::vector<NamSearchTerm> terms;
    juce::String current;
    auto quoted = false;
    auto phrase = false;
    const auto flush =
        [&]
        {
            auto text = current.trim().toLowerCase();
            current.clear();
            if (text.isEmpty())
            {
                phrase = false;
                return;
            }
            auto excluded = text.startsWithChar('-')
                && text.length() > 1;
            if (excluded)
                text = text.substring(1);
            terms.push_back({ text, excluded, phrase });
            phrase = false;
        };
    for (int index = 0; index < query.length(); ++index)
    {
        const auto character = query[index];
        if (character == '"')
        {
            quoted = !quoted;
            phrase = true;
        }
        else if (juce::CharacterFunctions::isWhitespace(character)
                 && !quoted)
        {
            flush();
        }
        else
        {
            current += character;
        }
    }
    flush();
    return terms;
}

bool isSearchWordCharacter(juce::juce_wchar character)
{
    return juce::CharacterFunctions::isLetterOrDigit(character);
}

bool containsSearchWord(
    const juce::String& text, const juce::String& word)
{
    auto position = text.indexOf(word);
    while (position >= 0)
    {
        const auto before = position == 0
            || !isSearchWordCharacter(text[position - 1]);
        const auto afterIndex = position + word.length();
        const auto after = afterIndex >= text.length()
            || !isSearchWordCharacter(text[afterIndex]);
        if (before && after)
            return true;
        position = text.indexOf(position + 1, word);
    }
    return false;
}

int boundedWordEditDistance(
    const juce::String& text, int wordStart, int wordEnd,
    const juce::String& query, int maximumDistance)
{
    constexpr auto maximumFuzzyLength = 48;
    const auto wordLength = wordEnd - wordStart;
    const auto queryLength = query.length();
    if (wordLength > maximumFuzzyLength
        || queryLength > maximumFuzzyLength
        || std::abs(wordLength - queryLength) > maximumDistance)
        return maximumDistance + 1;

    std::array<int, maximumFuzzyLength + 1> previous {};
    std::array<int, maximumFuzzyLength + 1> current {};
    for (int column = 0; column <= queryLength; ++column)
        previous[static_cast<size_t>(column)] = column;
    for (int row = 1; row <= wordLength; ++row)
    {
        current[0] = row;
        auto rowMinimum = current[0];
        for (int column = 1; column <= queryLength; ++column)
        {
            const auto substitution =
                text[wordStart + row - 1] == query[column - 1]
                ? 0 : 1;
            current[static_cast<size_t>(column)] = std::min({
                previous[static_cast<size_t>(column)] + 1,
                current[static_cast<size_t>(column - 1)] + 1,
                previous[static_cast<size_t>(column - 1)]
                    + substitution
            });
            rowMinimum = std::min(
                rowMinimum,
                current[static_cast<size_t>(column)]);
        }
        if (rowMinimum > maximumDistance)
            return maximumDistance + 1;
        previous = current;
    }
    return previous[static_cast<size_t>(queryLength)];
}

int fuzzyWordMatchScore(
    const juce::String& text, const juce::String& query)
{
    if (query.length() < 4)
        return 0;
    const auto maximumDistance = query.length() >= 8 ? 2 : 1;
    auto bestDistance = maximumDistance + 1;
    for (int start = 0; start < text.length();)
    {
        while (start < text.length()
               && !isSearchWordCharacter(text[start]))
            ++start;
        auto end = start;
        while (end < text.length()
               && isSearchWordCharacter(text[end]))
            ++end;
        if (end > start)
            bestDistance = std::min(
                bestDistance,
                boundedWordEditDistance(
                    text, start, end, query,
                    maximumDistance));
        start = end + 1;
    }
    if (bestDistance == 1)
        return 45;
    if (maximumDistance >= 2 && bestDistance == 2)
        return 25;
    return 0;
}

int textMatchScore(
    const juce::String& searchable, const juce::String& query,
    bool allowFuzzy)
{
    const auto position = searchable.indexOf(query);
    if (position == 0)
        return 120;
    if (position > 0)
        return containsSearchWord(searchable, query) ? 95 : 75;
    return allowFuzzy ? fuzzyWordMatchScore(searchable, query) : 0;
}

int namSearchTermScore(
    const NamModelLibraryEntry& entry, bool favorite,
    const NamSearchTerm& term, bool metadataAvailable)
{
    const auto& token = term.text;
    if (token == "favorite" || token == "fav")
        return favorite ? 140 : 0;
    if (token == "a1")
        return !entry.isA2 ? 130 : 0;
    if (token == "a2")
        return entry.isA2 ? 130 : 0;
    if (token == "cab")
        return entry.cabIncluded == 1 ? 130 : 0;
    if (token == "nocab")
        return entry.cabIncluded == 0 ? 130 : 0;
    if (token == "amp")
        return entry.gearType == "amp"
                || (entry.gearType.isEmpty()
                    && entry.cabIncluded == 0)
            ? 130 : 0;
    if (token == "rig" || token == "full-rig")
        return entry.gearType == "full-rig" ? 130 : 0;
    if (token == "pedal")
        return entry.gearType.contains("pedal") ? 130 : 0;
    if (token == "di" || token == "direct")
        return entry.cabIncluded == 0
                || containsSearchWord(entry.searchableText, "di")
                || containsSearchWord(entry.searchableText, "direct")
            ? 130 : 0;
    if (token.startsWith("sr:") || token.startsWith("rate:"))
    {
        const auto requested = token.fromFirstOccurrenceOf(
            ":", false, false).getDoubleValue();
        return requested > 0.0
                && std::abs(entry.sampleRate - requested) < 1.0
            ? 130 : 0;
    }
    if (token.startsWith("cab:"))
    {
        const auto value = token.fromFirstOccurrenceOf(
            ":", false, false);
        const auto wantsCab = value == "yes" || value == "true"
            || value == "included" || value == "with";
        const auto wantsDirect = value == "no" || value == "false"
            || value == "none" || value == "di"
            || value == "direct";
        return (wantsCab && entry.cabIncluded == 1)
                || (wantsDirect && entry.cabIncluded == 0)
            ? 130 : 0;
    }
    if (token.startsWith("type:"))
    {
        const auto value = token.fromFirstOccurrenceOf(
            ":", false, false);
        return entry.gearType.contains(value) ? 130 : 0;
    }

    if (!metadataAvailable)
        return textMatchScore(
            entry.searchableText, token,
            !term.phrase && !term.excluded);

    if (token == "low-gain" || token == "lowgain")
        return std::max(
            textMatchScore(entry.searchableText, "low gain", false),
            textMatchScore(entry.searchableText, "clean", false));
    if (token == "mid-gain" || token == "midgain")
        return std::max(
            textMatchScore(entry.searchableText, "mid gain", false),
            textMatchScore(entry.searchableText, "crunch", false));
    if (token == "high-gain" || token == "highgain")
        return std::max(
            textMatchScore(entry.searchableText, "high gain", false),
            textMatchScore(entry.searchableText, "lead", false));

    const auto fieldSeparator = token.indexOfChar(':');
    if (fieldSeparator > 0)
    {
        const auto field = token.substring(0, fieldSeparator);
        const auto value = token.substring(fieldSeparator + 1);
        if (field == "name" || field == "author"
            || field == "amp" || field == "model")
            return textMatchScore(
                entry.searchableText, value,
                !term.phrase && !term.excluded);
    }
    return textMatchScore(
        entry.searchableText, token,
        !term.phrase && !term.excluded);
}

int impulseSearchTermScore(
    const ImpulseResponseLibraryEntry& entry, bool favorite,
    const NamSearchTerm& term, bool metadataAvailable)
{
    const auto& token = term.text;
    if (token == "favorite" || token == "fav")
        return favorite ? 140 : 0;
    if (!metadataAvailable)
        return textMatchScore(
            entry.searchableText, token,
            !term.phrase && !term.excluded);
    if (token == "mono")
        return entry.channelCount == 1 ? 130 : 0;
    if (token == "stereo")
        return entry.channelCount >= 2 ? 130 : 0;
    if (token == "short")
        return entry.durationSeconds < 1.0 ? 130 : 0;
    if (token == "medium")
        return entry.durationSeconds >= 1.0
                && entry.durationSeconds <= 3.0
            ? 130 : 0;
    if (token == "long")
        return entry.durationSeconds > 3.0 ? 130 : 0;
    if (token.startsWith("sr:") || token.startsWith("rate:"))
    {
        const auto requested = token.fromFirstOccurrenceOf(
            ":", false, false).getDoubleValue();
        return requested > 0.0
                && std::abs(entry.sampleRate - requested) < 1.0
            ? 130 : 0;
    }
    const auto fieldSeparator = token.indexOfChar(':');
    if (fieldSeparator > 0)
    {
        const auto field = token.substring(0, fieldSeparator);
        const auto value = token.substring(fieldSeparator + 1);
        if (field == "name" || field == "path")
            return textMatchScore(
                entry.searchableText, value,
                !term.phrase && !term.excluded);
    }
    return textMatchScore(
        entry.searchableText, token,
        !term.phrase && !term.excluded);
}

std::vector<ImpulseResponseLibraryEntry>
discoverImpulseResponsePaths(const juce::File& root)
{
    juce::Array<juce::File> files;
    root.findChildFiles(files, juce::File::findFiles, true);
    std::vector<ImpulseResponseLibraryEntry> discovered;
    discovered.reserve(static_cast<size_t>(files.size()));
    for (const auto& file : files)
    {
        const auto extension = file.getFileExtension().toLowerCase();
        if (extension != ".wav" && extension != ".aif"
            && extension != ".aiff" && extension != ".flac")
            continue;
        ImpulseResponseLibraryEntry entry;
        entry.file = file;
        entry.relativePath =
            file.getRelativePathFrom(root).replaceCharacter('\\', '/');
        entry.searchableText = impulseResponseSearchableText(entry);
        discovered.push_back(std::move(entry));
    }
    std::sort(
        discovered.begin(), discovered.end(),
        [](const auto& left, const auto& right)
        {
            const auto order = left.relativePath.compareIgnoreCase(
                right.relativePath);
            return order != 0
                ? order < 0
                : left.relativePath < right.relativePath;
        });
    return discovered;
}

std::vector<NamModelLibraryEntry>
discoverNamModelPaths(const juce::File& root)
{
    juce::Array<juce::File> files;
    root.findChildFiles(files, juce::File::findFiles, true);
    std::vector<NamModelLibraryEntry> discovered;
    discovered.reserve(static_cast<size_t>(files.size()));
    for (const auto& file : files)
    {
        if (!file.hasFileExtension("nam"))
            continue;
        NamModelLibraryEntry entry;
        entry.file = file;
        entry.relativePath =
            file.getRelativePathFrom(root).replaceCharacter('\\', '/');
        entry.displayName = file.getFileNameWithoutExtension();
        entry.searchableText = namModelSearchableText(entry);
        discovered.push_back(std::move(entry));
    }
    std::sort(
        discovered.begin(), discovered.end(),
        [](const auto& left, const auto& right)
        {
            return left.relativePath.compareIgnoreCase(
                       right.relativePath)
                < 0;
        });
    return discovered;
}

juce::PropertiesFile::Options globalSettingsOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "megaDSP";
    options.filenameSuffix = ".xml";
    options.folderName = "megaDSP";
    options.osxLibrarySubFolder = "Application Support";
    options.millisecondsBeforeSaving = 0;
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    return options;
}

juce::CriticalSection& globalSettingsLock()
{
    static juce::CriticalSection lock;
    return lock;
}

juce::String globalImpulseResponseRoot()
{
    const juce::ScopedLock lock(globalSettingsLock());
    juce::PropertiesFile settings(
        globalSettingsFile(), globalSettingsOptions());
    return settings.getValue("impulseResponseRootDirectory");
}

juce::String globalNamModelRoot()
{
    const juce::ScopedLock lock(globalSettingsLock());
    juce::PropertiesFile settings(
        globalSettingsFile(), globalSettingsOptions());
    return settings.getValue("namModelRootDirectory");
}

juce::String globalNamCabinetRoot()
{
    const juce::ScopedLock lock(globalSettingsLock());
    juce::PropertiesFile settings(
        globalSettingsFile(), globalSettingsOptions());
    return settings.getValue("namCabinetRootDirectory");
}

struct NamCabinetPreference
{
    juce::String impulsePath;
    bool enabled = false;
    bool exists = false;
};

std::optional<NamModelLibraryEntry> inspectNamModel(
    const juce::File&, const juce::File&);

bool defaultNamCabinetEnabled(
    const juce::File& model, const juce::File& modelRoot)
{
    const auto matchPath =
        modelRoot.isDirectory() && model.isAChildOf(modelRoot)
        ? model.getRelativePathFrom(modelRoot).replaceCharacter('\\', '/')
        : model.getFileNameWithoutExtension();
    if (matchPath.containsIgnoreCase("Cab"))
        return false;
    if (matchPath.containsIgnoreCase("Direct"))
        return true;

    const auto inspectionRoot = modelRoot.isDirectory()
        ? modelRoot : model.getParentDirectory();
    if (const auto metadata = inspectNamModel(model, inspectionRoot);
        metadata && metadata->cabIncluded >= 0)
        return metadata->cabIncluded == 0;
    return matchPath.contains("DI");
}

NamCabinetPreference namCabinetPreference(
    const juce::String& modelPath, const juce::File& modelRoot)
{
    NamCabinetPreference preference;
    preference.enabled =
        defaultNamCabinetEnabled(juce::File(modelPath), modelRoot);
    if (modelPath.isEmpty())
        return preference;

    const juce::ScopedLock lock(globalSettingsLock());
    juce::PropertiesFile settings(
        globalSettingsFile(), globalSettingsOptions());
    const auto xml = juce::XmlDocument::parse(
        settings.getValue("namCabinetPreferences"));
    if (xml != nullptr && xml->hasTagName("NAM_CABINETS"))
        for (auto* child : xml->getChildWithTagNameIterator("MODEL"))
            if (child->getStringAttribute("path") == modelPath)
            {
                preference.impulsePath =
                    child->getStringAttribute("impulse");
                preference.enabled =
                    child->getBoolAttribute("enabled");
                preference.exists = true;
                break;
            }
    if (preference.impulsePath.isEmpty())
    {
        const auto lastImpulse =
            settings.getValue("lastNamCabinetImpulse");
        if (juce::File(lastImpulse).existsAsFile())
            preference.impulsePath = lastImpulse;
    }
    return preference;
}

juce::Result persistNamCabinetPreference(
    const juce::String& modelPath, const juce::String& impulsePath,
    bool enabled)
{
    if (modelPath.isEmpty())
        return juce::Result::fail(
            "Load a NAM model before choosing a cabinet impulse.");
    const juce::ScopedLock lock(globalSettingsLock());
    const auto file = globalSettingsFile();
    const auto directoryResult =
        file.getParentDirectory().createDirectory();
    if (directoryResult.failed())
        return directoryResult;
    juce::PropertiesFile settings(file, globalSettingsOptions());
    auto xml = juce::XmlDocument::parse(
        settings.getValue("namCabinetPreferences"));
    if (xml == nullptr || !xml->hasTagName("NAM_CABINETS"))
        xml = std::make_unique<juce::XmlElement>("NAM_CABINETS");

    juce::XmlElement* target = nullptr;
    for (auto* child : xml->getChildWithTagNameIterator("MODEL"))
        if (child->getStringAttribute("path") == modelPath)
        {
            target = child;
            break;
        }
    if (target == nullptr)
    {
        target = new juce::XmlElement("MODEL");
        xml->addChildElement(target);
    }
    target->setAttribute("path", modelPath);
    target->setAttribute("impulse", impulsePath);
    target->setAttribute("enabled", enabled);
    settings.setValue(
        "namCabinetPreferences", xml->toString());
    if (impulsePath.isNotEmpty())
        settings.setValue("lastNamCabinetImpulse", impulsePath);
    return settings.saveIfNeeded()
        ? juce::Result::ok()
        : juce::Result::fail(
              "Could not save the NAM cabinet preference.");
}

juce::StringArray globalNamFavorites()
{
    const juce::ScopedLock lock(globalSettingsLock());
    juce::PropertiesFile settings(
        globalSettingsFile(), globalSettingsOptions());
    const auto xml = juce::XmlDocument::parse(
        settings.getValue("namModelFavorites"));
    juce::StringArray favorites;
    if (xml == nullptr || !xml->hasTagName("NAM_FAVORITES"))
        return favorites;
    for (auto* child : xml->getChildWithTagNameIterator("MODEL"))
    {
        const auto path = child->getStringAttribute("path");
        if (path.isNotEmpty())
            favorites.addIfNotAlreadyThere(path);
    }
    return favorites;
}

juce::Result persistNamFavorite(
    const juce::String& modelPath, bool favorite)
{
    if (modelPath.isEmpty())
        return juce::Result::fail("Choose a NAM model first.");
    const juce::ScopedLock lock(globalSettingsLock());
    const auto file = globalSettingsFile();
    const auto directoryResult =
        file.getParentDirectory().createDirectory();
    if (directoryResult.failed())
        return directoryResult;
    juce::PropertiesFile settings(file, globalSettingsOptions());
    auto xml = juce::XmlDocument::parse(
        settings.getValue("namModelFavorites"));
    if (xml == nullptr || !xml->hasTagName("NAM_FAVORITES"))
        xml = std::make_unique<juce::XmlElement>("NAM_FAVORITES");
    for (auto* child : xml->getChildWithTagNameIterator("MODEL"))
        if (child->getStringAttribute("path") == modelPath)
        {
            xml->removeChildElement(child, true);
            break;
        }
    if (favorite)
    {
        auto child = std::make_unique<juce::XmlElement>("MODEL");
        child->setAttribute("path", modelPath);
        xml->addChildElement(child.release());
    }
    settings.setValue("namModelFavorites", xml->toString());
    return settings.saveIfNeeded()
        ? juce::Result::ok()
        : juce::Result::fail(
              "Could not save the NAM favorite.");
}

juce::StringArray globalImpulseResponseFavorites()
{
    const juce::ScopedLock lock(globalSettingsLock());
    juce::PropertiesFile settings(
        globalSettingsFile(), globalSettingsOptions());
    const auto xml = juce::XmlDocument::parse(
        settings.getValue("impulseResponseFavorites"));
    juce::StringArray favorites;
    if (xml == nullptr || !xml->hasTagName("IR_FAVORITES"))
        return favorites;
    for (auto* child : xml->getChildWithTagNameIterator("IR"))
    {
        const auto path = child->getStringAttribute("path");
        if (path.isNotEmpty())
            favorites.addIfNotAlreadyThere(path);
    }
    return favorites;
}

juce::Result persistImpulseResponseFavorite(
    const juce::String& impulsePath, bool favorite)
{
    if (impulsePath.isEmpty())
        return juce::Result::fail(
            "Choose an impulse response to favorite.");
    const juce::ScopedLock lock(globalSettingsLock());
    const auto file = globalSettingsFile();
    const auto directoryResult = file.getParentDirectory().createDirectory();
    if (directoryResult.failed())
        return directoryResult;
    juce::PropertiesFile settings(file, globalSettingsOptions());
    auto xml = juce::XmlDocument::parse(
        settings.getValue("impulseResponseFavorites"));
    if (xml == nullptr || !xml->hasTagName("IR_FAVORITES"))
        xml = std::make_unique<juce::XmlElement>("IR_FAVORITES");
    for (auto* child : xml->getChildWithTagNameIterator("IR"))
        if (child->getStringAttribute("path") == impulsePath)
        {
            if (!favorite)
                xml->removeChildElement(child, true);
            settings.setValue(
                "impulseResponseFavorites", xml->toString());
            return settings.saveIfNeeded()
                ? juce::Result::ok()
                : juce::Result::fail(
                    "Could not save the impulse-response favorite.");
        }
    if (favorite)
    {
        auto child = std::make_unique<juce::XmlElement>("IR");
        child->setAttribute("path", impulsePath);
        xml->addChildElement(child.release());
    }
    settings.setValue(
        "impulseResponseFavorites", xml->toString());
    return settings.saveIfNeeded()
        ? juce::Result::ok()
        : juce::Result::fail(
            "Could not save the impulse-response favorite.");
}

juce::Result removeGlobalNamPreferences(
    const juce::String& modelPath)
{
    const auto favoriteResult =
        persistNamFavorite(modelPath, false);
    if (favoriteResult.failed())
        return favoriteResult;

    const juce::ScopedLock lock(globalSettingsLock());
    const auto file = globalSettingsFile();
    juce::PropertiesFile settings(file, globalSettingsOptions());
    auto xml = juce::XmlDocument::parse(
        settings.getValue("namCabinetPreferences"));
    if (xml != nullptr && xml->hasTagName("NAM_CABINETS"))
    {
        for (auto* child : xml->getChildWithTagNameIterator("MODEL"))
            if (child->getStringAttribute("path") == modelPath)
            {
                xml->removeChildElement(child, true);
                break;
            }
        settings.setValue(
            "namCabinetPreferences", xml->toString());
    }
    return settings.saveIfNeeded()
        ? juce::Result::ok()
        : juce::Result::fail(
              "Could not remove the NAM model preferences.");
}

juce::String globalWidgetSizePreference()
{
    const juce::ScopedLock lock(globalSettingsLock());
    juce::PropertiesFile settings(
        globalSettingsFile(), globalSettingsOptions());
    return megadsp::ui::widgetSizeId(
        megadsp::ui::widgetSizeFromId(
            settings.getValue("widgetSize", "small")));
}

juce::Result persistGlobalWidgetSize(
    const juce::String& identifier)
{
    const auto size = megadsp::ui::widgetSizeFromId(identifier);
    const juce::ScopedLock lock(globalSettingsLock());
    const auto file = globalSettingsFile();
    const auto directoryResult =
        file.getParentDirectory().createDirectory();
    if (directoryResult.failed())
        return directoryResult;
    juce::PropertiesFile settings(
        file, globalSettingsOptions());
    settings.setValue(
        "widgetSize", megadsp::ui::widgetSizeId(size));
    return settings.saveIfNeeded()
        ? juce::Result::ok()
        : juce::Result::fail(
              "Could not save the global widget size.");
}

void inheritGlobalImpulseResponseRoot(juce::ValueTree& state)
{
    if (state.getProperty(
            "impulseResponseRootDirectory").toString().isNotEmpty())
        return;
    const auto root = juce::File(globalImpulseResponseRoot());
    if (!root.isDirectory())
        return;
    state.setProperty(
        "impulseResponseRootDirectory",
        root.getFullPathName(), nullptr);
    state.setProperty(
        "impulseResponseDirectory",
        root.getFullPathName(), nullptr);
}

void inheritGlobalNamModelRoot(juce::ValueTree& state)
{
    if (state.getProperty(
            "namModelRootDirectory").toString().isNotEmpty())
        return;
    const auto root = juce::File(globalNamModelRoot());
    if (root.isDirectory())
        state.setProperty(
            "namModelRootDirectory",
            root.getFullPathName(), nullptr);
}

juce::Result persistGlobalImpulseResponseRoot(const juce::File& root)
{
    const juce::ScopedLock lock(globalSettingsLock());
    const auto file = globalSettingsFile();
    const auto directoryResult =
        file.getParentDirectory().createDirectory();
    if (directoryResult.failed())
        return directoryResult;
    juce::PropertiesFile settings(
        file, globalSettingsOptions());
    settings.setValue(
        "impulseResponseRootDirectory",
        root.getFullPathName());
    return settings.saveIfNeeded()
        ? juce::Result::ok()
        : juce::Result::fail(
              "Could not save the global IR library root.");
}

juce::Result persistGlobalNamModelRoot(const juce::File& root)
{
    const juce::ScopedLock lock(globalSettingsLock());
    const auto file = globalSettingsFile();
    const auto directoryResult =
        file.getParentDirectory().createDirectory();
    if (directoryResult.failed())
        return directoryResult;
    juce::PropertiesFile settings(
        file, globalSettingsOptions());
    settings.setValue(
        "namModelRootDirectory", root.getFullPathName());
    return settings.saveIfNeeded()
        ? juce::Result::ok()
        : juce::Result::fail(
              "Could not save the global NAM library root.");
}

juce::Result persistGlobalNamCabinetRoot(const juce::File& root)
{
    const juce::ScopedLock lock(globalSettingsLock());
    const auto file = globalSettingsFile();
    const auto directoryResult =
        file.getParentDirectory().createDirectory();
    if (directoryResult.failed())
        return directoryResult;
    juce::PropertiesFile settings(file, globalSettingsOptions());
    settings.setValue(
        "namCabinetRootDirectory", root.getFullPathName());
    return settings.saveIfNeeded()
        ? juce::Result::ok()
        : juce::Result::fail(
              "Could not save the global NAM cabinet IR root.");
}

juce::Result seedGlobalImpulseResponseRoot(
    const juce::ValueTree& state)
{
    if (juce::File(globalImpulseResponseRoot()).isDirectory())
        return juce::Result::ok();
    const auto instanceRoot = juce::File(
        state.getProperty(
            "impulseResponseRootDirectory").toString());
    return instanceRoot.isDirectory()
        ? persistGlobalImpulseResponseRoot(instanceRoot)
        : juce::Result::ok();
}

juce::Result seedGlobalNamModelRoot(const juce::ValueTree& state)
{
    if (juce::File(globalNamModelRoot()).isDirectory())
        return juce::Result::ok();
    const auto instanceRoot = juce::File(
        state.getProperty("namModelRootDirectory").toString());
    return instanceRoot.isDirectory()
        ? persistGlobalNamModelRoot(instanceRoot)
        : juce::Result::ok();
}

bool restoreImpulseResponseLibraryCache(
    const juce::File& root,
    std::vector<ImpulseResponseLibraryEntry>& destination)
{
    const juce::ScopedLock lock(globalSettingsLock());
    const auto cache = impulseResponseLibraryCacheFile();
    if (!cache.existsAsFile())
        return false;
    const auto xml = juce::XmlDocument::parse(cache);
    if (xml == nullptr
        || !xml->hasTagName("IR_LIBRARY")
        || xml->getIntAttribute("schema") != 1
        || xml->getStringAttribute("root") != root.getFullPathName())
        return false;

    std::vector<ImpulseResponseLibraryEntry> restored;
    forEachXmlChildElementWithTagName(*xml, child, "IR")
    {
        ImpulseResponseLibraryEntry entry;
        entry.relativePath = child->getStringAttribute("path");
        if (entry.relativePath.isEmpty())
            continue;
        entry.file = root.getChildFile(entry.relativePath);
        entry.sampleRate = child->getDoubleAttribute("sampleRate");
        entry.durationSeconds = child->getDoubleAttribute("duration");
        entry.channelCount = child->getIntAttribute("channels");
        entry.searchableText = impulseResponseSearchableText(entry);
        restored.push_back(std::move(entry));
    }
    if (restored.empty())
        return false;
    destination = std::move(restored);
    return true;
}

juce::Result persistImpulseResponseLibraryCache(
    const juce::File& root,
    const std::vector<ImpulseResponseLibraryEntry>& entries)
{
    juce::XmlElement xml("IR_LIBRARY");
    xml.setAttribute("schema", 1);
    xml.setAttribute("root", root.getFullPathName());
    for (const auto& entry : entries)
    {
        auto child = std::make_unique<juce::XmlElement>("IR");
        child->setAttribute("path", entry.relativePath);
        child->setAttribute("sampleRate", entry.sampleRate);
        child->setAttribute("duration", entry.durationSeconds);
        child->setAttribute("channels", entry.channelCount);
        xml.addChildElement(child.release());
    }

    const juce::ScopedLock lock(globalSettingsLock());
    const auto cache = impulseResponseLibraryCacheFile();
    const auto directoryResult =
        cache.getParentDirectory().createDirectory();
    if (directoryResult.failed())
        return directoryResult;
    return xml.writeTo(cache)
        ? juce::Result::ok()
        : juce::Result::fail(
              "Could not save the impulse-response library cache.");
}

juce::String namJsonString(
    const nlohmann::json& object, const char* key)
{
    const auto found = object.find(key);
    return found != object.end() && found->is_string()
        ? juce::String(found->get<std::string>())
        : juce::String();
}

double namJsonNumber(
    const nlohmann::json& object, const char* key) noexcept
{
    const auto found = object.find(key);
    return found != object.end() && found->is_number()
        ? found->get<double>() : std::numeric_limits<double>::quiet_NaN();
}

std::optional<NamModelLibraryEntry> inspectNamModel(
    const juce::File& file, const juce::File& root)
{
    constexpr double legacyModelSampleRate = 48000.0;
    if (!file.existsAsFile()
        || !file.hasFileExtension("nam"))
        return std::nullopt;
    const auto contents = file.loadFileAsString().toStdString();
    const auto json = nlohmann::json::parse(
        contents, nullptr, false);
    if (json.is_discarded() || !json.is_object())
        return std::nullopt;
    const auto architecture = json.find("architecture");
    const auto config = json.find("config");
    const auto weights = json.find("weights");
    const auto sampleRate = json.find("sample_rate");
    const auto version = json.find("version");
    if (architecture == json.end() || !architecture->is_string()
        || config == json.end() || !config->is_object()
        || weights == json.end() || !weights->is_array()
        || version == json.end() || !version->is_string())
        return std::nullopt;
    auto effectiveSampleRate = legacyModelSampleRate;
    if (sampleRate != json.end())
    {
        if (!sampleRate->is_number()
            || sampleRate->get<double>() <= 0.0)
            return std::nullopt;
        effectiveSampleRate = sampleRate->get<double>();
    }

    static const std::array<const char*, 7> supported {
        "Linear", "LSTM", "ConvNet", "WaveNet", "SlimmableWaveNet",
        "Container", "SlimmableContainer"
    };
    const auto architectureName =
        juce::String(architecture->get<std::string>());
    if (std::none_of(
            supported.begin(), supported.end(),
            [&architectureName](const char* candidate)
            {
                return architectureName == candidate;
            }))
        return std::nullopt;

    const nlohmann::json* effectiveModel = &json;
    auto submodelCount = 1;
    if (architectureName == "Container"
        || architectureName == "SlimmableContainer")
    {
        const auto submodels = config->find("submodels");
        if (submodels == config->end() || !submodels->is_array()
            || submodels->empty())
            return std::nullopt;
        submodelCount = static_cast<int>(submodels->size());
        double highest = -std::numeric_limits<double>::infinity();
        effectiveModel = nullptr;
        for (const auto& candidate : *submodels)
        {
            const auto maximum = candidate.find("max_value");
            const auto model = candidate.find("model");
            if (maximum == candidate.end() || !maximum->is_number()
                || model == candidate.end() || !model->is_object())
                continue;
            if (maximum->get<double>() > highest)
            {
                highest = maximum->get<double>();
                effectiveModel = &*model;
            }

        }
        if (effectiveModel == nullptr)
            return std::nullopt;
    }

    auto effectiveArchitecture = architectureName;
    if (effectiveModel != &json)
    {
        const auto nestedArchitecture =
            effectiveModel->find("architecture");
        if (nestedArchitecture != effectiveModel->end()
            && nestedArchitecture->is_string())
            effectiveArchitecture =
                juce::String(nestedArchitecture->get<std::string>());
    }
    const auto effectiveSupported = std::any_of(
        supported.begin(), supported.end(),
        [&effectiveArchitecture](const char* candidate)
        {
            return effectiveArchitecture == candidate;
        });
    const auto effectiveConfig = effectiveModel->find("config");
    const auto effectiveWeights = effectiveModel->find("weights");
    if (!effectiveSupported
        || effectiveConfig == effectiveModel->end()
        || !effectiveConfig->is_object()
        || effectiveWeights == effectiveModel->end()
        || !effectiveWeights->is_array()
        || effectiveWeights->empty())
        return std::nullopt;

    auto isA2 = false;
#if defined(NAM_ENABLE_A2_FAST)
    if (effectiveArchitecture == "WaveNet")
    {
        int channels = 0;
        isA2 = nam::wavenet::a2_fast::is_a2_shape(
            *effectiveConfig, &channels);
    }
#endif

    NamModelLibraryEntry entry;
    entry.file = file;
    entry.relativePath =
        file.getRelativePathFrom(root).replaceCharacter('\\', '/');
    entry.displayName = file.getFileNameWithoutExtension();
    entry.architecture = architectureName;
    if (effectiveArchitecture != architectureName)
        entry.architecture += " / " + effectiveArchitecture;
    entry.sampleRate = effectiveSampleRate;
    entry.submodelCount = submodelCount;
    entry.isA2 = isA2;

    const auto metadata = json.find("metadata");
    if (metadata != json.end() && metadata->is_object())
    {
        const auto displayName = namJsonString(*metadata, "display_name");
        const auto metadataName = namJsonString(*metadata, "name");
        if (displayName.isNotEmpty())
            entry.metadataTitle = displayName;
        else if (metadataName.isNotEmpty())
            entry.metadataTitle = metadataName;
        if (entry.metadataTitle.isNotEmpty())
            entry.displayName = entry.metadataTitle;

        juce::StringArray capture;
        auto gear = namJsonString(*metadata, "gear_make");
        const auto gearModel = namJsonString(*metadata, "gear_model");
        if (gearModel.isNotEmpty())
            gear += (gear.isNotEmpty() ? " " : "") + gearModel;
        entry.gearType =
            namJsonString(*metadata, "gear_type").toLowerCase();
        if (gear.isEmpty())
            gear = entry.gearType;
        if (gear.isNotEmpty())
            capture.add(gear);
        const auto tone = namJsonString(*metadata, "tone_type");
        if (tone.isNotEmpty())
            capture.add(tone);
        const auto modeledBy = namJsonString(*metadata, "modeled_by");
        if (modeledBy.isNotEmpty())
            capture.add("by " + modeledBy);
        entry.captureDescription = capture.joinIntoString(" | ");
        entry.inputLevelDbU =
            namJsonNumber(*metadata, "input_level_dbu");
        const auto withCab = metadata->find("with_cab");
        if (withCab != metadata->end() && withCab->is_boolean())
            entry.cabIncluded = withCab->get<bool>() ? 1 : 0;
        else if (entry.gearType.contains("cab")
                 || entry.gearType == "full-rig")
            entry.cabIncluded = 1;
        else if (entry.gearType.contains("amp"))
            entry.cabIncluded = 0;
    }
    entry.searchableText = namModelSearchableText(entry);
    return entry;
}

bool restoreNamModelLibraryCache(
    const juce::File& root,
    std::vector<NamModelLibraryEntry>& destination)
{
    const juce::ScopedLock lock(globalSettingsLock());
    const auto cache = namModelLibraryCacheFile();
    if (!cache.existsAsFile())
        return false;
    const auto xml = juce::XmlDocument::parse(cache);
    if (xml == nullptr
        || !xml->hasTagName("NAM_LIBRARY")
        || xml->getIntAttribute("schema") != 2
        || xml->getStringAttribute("root") != root.getFullPathName())
        return false;

    std::vector<NamModelLibraryEntry> restored;
    forEachXmlChildElementWithTagName(*xml, child, "MODEL")
    {
        NamModelLibraryEntry entry;
        entry.relativePath = child->getStringAttribute("path");
        if (entry.relativePath.isEmpty())
            continue;
        entry.file = root.getChildFile(entry.relativePath);
        entry.displayName = child->getStringAttribute("display");
        entry.metadataTitle = child->getStringAttribute("title");
        entry.architecture = child->getStringAttribute("architecture");
        entry.captureDescription = child->getStringAttribute("capture");
        entry.gearType = child->getStringAttribute("gear");
        entry.sampleRate = child->getDoubleAttribute("sampleRate");
        entry.inputLevelDbU = child->hasAttribute("inputLevelDbU")
            ? child->getDoubleAttribute("inputLevelDbU")
            : std::numeric_limits<double>::quiet_NaN();
        entry.submodelCount = child->getIntAttribute("submodels", 1);
        entry.cabIncluded = child->getIntAttribute("cab", -1);
        entry.isA2 = child->getBoolAttribute("a2");
        entry.searchableText = namModelSearchableText(entry);
        restored.push_back(std::move(entry));
    }
    if (restored.empty())
        return false;
    destination = std::move(restored);
    return true;
}

juce::Result persistNamModelLibraryCache(
    const juce::File& root,
    const std::vector<NamModelLibraryEntry>& entries)
{
    juce::XmlElement xml("NAM_LIBRARY");
    xml.setAttribute("schema", 2);
    xml.setAttribute("root", root.getFullPathName());
    for (const auto& entry : entries)
    {
        auto child = std::make_unique<juce::XmlElement>("MODEL");
        child->setAttribute("path", entry.relativePath);
        child->setAttribute("display", entry.displayName);
        child->setAttribute("title", entry.metadataTitle);
        child->setAttribute("architecture", entry.architecture);
        child->setAttribute("capture", entry.captureDescription);
        child->setAttribute("gear", entry.gearType);
        child->setAttribute("sampleRate", entry.sampleRate);
        if (std::isfinite(entry.inputLevelDbU))
            child->setAttribute("inputLevelDbU", entry.inputLevelDbU);
        child->setAttribute("submodels", entry.submodelCount);
        child->setAttribute("cab", entry.cabIncluded);
        child->setAttribute("a2", entry.isA2);
        xml.addChildElement(child.release());
    }

    const juce::ScopedLock lock(globalSettingsLock());
    const auto cache = namModelLibraryCacheFile();
    const auto directoryResult =
        cache.getParentDirectory().createDirectory();
    if (directoryResult.failed())
        return directoryResult;
    return xml.writeTo(cache)
        ? juce::Result::ok()
        : juce::Result::fail(
              "Could not save the NAM model library cache.");
}

struct LibraryFileStamp
{
    juce::String relativePath;
    juce::int64 size = 0;
    juce::int64 modified = 0;

    bool operator==(const LibraryFileStamp&) const = default;
};

std::vector<LibraryFileStamp> scanLibraryFiles(
    const juce::File& root, bool namFiles,
    std::vector<juce::File>* supportedFiles = nullptr)
{
    std::vector<LibraryFileStamp> stamps;
    if (!root.isDirectory())
        return stamps;
    juce::Array<juce::File> files;
    root.findChildFiles(files, juce::File::findFiles, true);
    stamps.reserve(static_cast<size_t>(files.size()));
    if (supportedFiles != nullptr)
        supportedFiles->reserve(static_cast<size_t>(files.size()));
    for (const auto& file : files)
    {
        const auto extension = file.getFileExtension().toLowerCase();
        const auto supported = namFiles
            ? extension == ".nam"
            : extension == ".wav" || extension == ".aif"
                || extension == ".aiff" || extension == ".flac";
        if (!supported)
            continue;
        stamps.push_back({
            file.getRelativePathFrom(root).replaceCharacter('\\', '/'),
            file.getSize(),
            file.getLastModificationTime().toMilliseconds()
        });
        if (supportedFiles != nullptr)
            supportedFiles->push_back(file);
    }
    std::sort(
        stamps.begin(), stamps.end(),
        [](const auto& left, const auto& right)
        {
            return left.relativePath.compareIgnoreCase(
                       right.relativePath)
                < 0;
        });
    if (supportedFiles != nullptr)
        std::sort(
            supportedFiles->begin(), supportedFiles->end(),
            [&root](const auto& left, const auto& right)
            {
                const auto leftPath = left.getRelativePathFrom(root);
                const auto rightPath = right.getRelativePathFrom(root);
                const auto order =
                    leftPath.compareIgnoreCase(rightPath);
                return order != 0
                    ? order < 0 : leftPath < rightPath;
            });
    return stamps;
}

class AutomaticLibraryCoordinator final : private juce::Thread
{
public:
    AutomaticLibraryCoordinator()
        : juce::Thread("megaDSP library monitor")
    {
        startThread(juce::Thread::Priority::background);
    }

    ~AutomaticLibraryCoordinator() override
    {
        signalThreadShouldExit();
        notify();
        stopThread(30000);
    }

    void requestScan() noexcept
    {
        notify();
    }

    std::uint64_t requestedNamGeneration() const noexcept
    {
        return namRequested.load(std::memory_order_acquire);
    }

    std::uint64_t completedNamGeneration() const noexcept
    {
        return namCompleted.load(std::memory_order_acquire);
    }

    std::uint64_t requestedImpulseGeneration() const noexcept
    {
        return impulseRequested.load(std::memory_order_acquire);
    }

    std::uint64_t completedImpulseGeneration() const noexcept
    {
        return impulseCompleted.load(std::memory_order_acquire);
    }

    juce::String monitoredNamRoot()
    {
        const juce::ScopedLock lock(catalogLock);
        return namRootPath;
    }

    juce::String monitoredImpulseRoot()
    {
        const juce::ScopedLock lock(catalogLock);
        return impulseRootPath;
    }

    bool claimNamRebuild() noexcept
    {
        auto expected = false;
        return namRebuildClaimed.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel);
    }

    bool claimImpulseRebuild() noexcept
    {
        auto expected = false;
        return impulseRebuildClaimed.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel);
    }

    void releaseNamRebuild() noexcept
    {
        namRebuildClaimed.store(false, std::memory_order_release);
    }

    void releaseImpulseRebuild() noexcept
    {
        impulseRebuildClaimed.store(false, std::memory_order_release);
    }

    void finishNamRebuild(std::uint64_t generation) noexcept
    {
        namCompleted.store(generation, std::memory_order_release);
        namRebuildClaimed.store(false, std::memory_order_release);
    }

    void finishImpulseRebuild(std::uint64_t generation) noexcept
    {
        impulseCompleted.store(generation, std::memory_order_release);
        impulseRebuildClaimed.store(false, std::memory_order_release);
    }

    void publishNam(
        const juce::File& root,
        const std::vector<NamModelLibraryEntry>& entries)
    {
        const juce::ScopedLock lock(catalogLock);
        namCatalogRoot = root.getFullPathName();
        namCatalog =
            std::make_shared<const std::vector<NamModelLibraryEntry>>(
                entries);
        namCatalogGeneration.fetch_add(1, std::memory_order_release);
    }

    void publishImpulse(
        const juce::File& root,
        const std::vector<ImpulseResponseLibraryEntry>& entries)
    {
        const juce::ScopedLock lock(catalogLock);
        impulseCatalogRoot = root.getFullPathName();
        impulseCatalog = std::make_shared<
            const std::vector<ImpulseResponseLibraryEntry>>(entries);
        impulseCatalogGeneration.fetch_add(1, std::memory_order_release);
    }

    std::shared_ptr<const std::vector<NamModelLibraryEntry>>
    namCatalogSnapshot(
        const juce::File& root, std::uint64_t& observedGeneration)
    {
        const auto generation =
            namCatalogGeneration.load(std::memory_order_acquire);
        if (generation == observedGeneration)
            return {};
        const juce::ScopedLock lock(catalogLock);
        if (namCatalogRoot != root.getFullPathName()
            || namCatalog == nullptr)
            return {};
        observedGeneration = generation;
        return namCatalog;
    }

    std::shared_ptr<const std::vector<ImpulseResponseLibraryEntry>>
    impulseCatalogSnapshot(
        const juce::File& root, std::uint64_t& observedGeneration)
    {
        const auto generation =
            impulseCatalogGeneration.load(std::memory_order_acquire);
        if (generation == observedGeneration)
            return {};
        const juce::ScopedLock lock(catalogLock);
        if (impulseCatalogRoot != root.getFullPathName()
            || impulseCatalog == nullptr)
            return {};
        observedGeneration = generation;
        return impulseCatalog;
    }

    std::vector<juce::File> cabinetCatalog(const juce::File& root)
    {
        const juce::ScopedLock lock(catalogLock);
        return cabinetCatalogRoot == root.getFullPathName()
            ? cabinetFiles : std::vector<juce::File> {};
    }

#if defined(MEGADSP_TESTS)
    void scanNowForTesting()
    {
        scan();
    }
#endif

private:
    void run() override
    {
        scan();
        while (!threadShouldExit())
        {
            wait(30000);
            if (!threadShouldExit())
                scan();
        }
    }

    void scan()
    {
        const juce::ScopedLock scanGuard(scanLock);
        const auto namRoot = juce::File(globalNamModelRoot());
        const auto impulseRoot = juce::File(globalImpulseResponseRoot());
        const auto cabinetRoot = juce::File(globalNamCabinetRoot());
        auto nextNam = scanLibraryFiles(namRoot, true);
        auto nextImpulse = scanLibraryFiles(impulseRoot, false);
        std::vector<juce::File> nextCabinetFiles;
        auto nextCabinet = scanLibraryFiles(
            cabinetRoot, false, &nextCabinetFiles);

        const juce::ScopedLock lock(catalogLock);
        updateMonitoredLibrary(
            namRoot, nextNam, namRootPath, namStamps, namInitialized,
            namRequested);
        updateMonitoredLibrary(
            impulseRoot, nextImpulse, impulseRootPath, impulseStamps,
            impulseInitialized, impulseRequested);
        const auto cabinetChanged = updateMonitoredLibrary(
            cabinetRoot, nextCabinet, cabinetRootPath, cabinetStamps,
            cabinetInitialized, cabinetGeneration);
        if (cabinetChanged || cabinetCatalogRoot.isEmpty())
        {
            cabinetCatalogRoot = cabinetRoot.getFullPathName();
            cabinetFiles = std::move(nextCabinetFiles);
        }
    }

    static bool updateMonitoredLibrary(
        const juce::File& root,
        std::vector<LibraryFileStamp>& next,
        juce::String& previousRoot,
        std::vector<LibraryFileStamp>& previous,
        bool& initialized,
        std::atomic<std::uint64_t>& generation)
    {
        const auto rootPath = root.getFullPathName();
        const auto changed = initialized
            && (rootPath != previousRoot || next != previous);
        previousRoot = rootPath;
        previous = std::move(next);
        initialized = true;
        if (changed)
            generation.fetch_add(1, std::memory_order_release);
        return changed;
    }

    juce::CriticalSection scanLock;
    juce::CriticalSection catalogLock;
    juce::String namRootPath;
    juce::String impulseRootPath;
    juce::String cabinetRootPath;
    std::vector<LibraryFileStamp> namStamps;
    std::vector<LibraryFileStamp> impulseStamps;
    std::vector<LibraryFileStamp> cabinetStamps;
    bool namInitialized = false;
    bool impulseInitialized = false;
    bool cabinetInitialized = false;
    std::atomic<std::uint64_t> namRequested { 0 };
    std::atomic<std::uint64_t> namCompleted { 0 };
    std::atomic<std::uint64_t> impulseRequested { 0 };
    std::atomic<std::uint64_t> impulseCompleted { 0 };
    std::atomic<std::uint64_t> cabinetGeneration { 0 };
    std::atomic<bool> namRebuildClaimed { false };
    std::atomic<bool> impulseRebuildClaimed { false };
    juce::String namCatalogRoot;
    juce::String impulseCatalogRoot;
    juce::String cabinetCatalogRoot;
    std::shared_ptr<const std::vector<NamModelLibraryEntry>> namCatalog;
    std::shared_ptr<
        const std::vector<ImpulseResponseLibraryEntry>> impulseCatalog;
    std::vector<juce::File> cabinetFiles;
    std::atomic<std::uint64_t> namCatalogGeneration { 0 };
    std::atomic<std::uint64_t> impulseCatalogGeneration { 0 };
};

AutomaticLibraryCoordinator& automaticLibraryCoordinator()
{
    static AutomaticLibraryCoordinator coordinator;
    return coordinator;
}

float smoothSnapshotFade(float position) noexcept
{
    position = juce::jlimit(0.0f, 1.0f, position);
    return position * position * (3.0f - 2.0f * position);
}

int programChangeNumber(
    const juce::MidiMessageMetadata& metadata) noexcept
{
    if (metadata.data == nullptr || metadata.numBytes < 2
        || (metadata.data[0] & 0xf0u) != 0xc0u)
        return -1;
    return metadata.data[1] & 0x7f;
}

void applySnapshotTransitionWeight(
    juce::AudioBuffer<float>& buffer, float start, float end,
    bool incoming) noexcept
{
    const auto samples = buffer.getNumSamples();
    if (samples <= 0)
        return;
    const auto increment = (end - start) / static_cast<float>(samples);
    for (int sample = 0; sample < samples; ++sample)
    {
        const auto smoothPosition = smoothSnapshotFade(
            start + increment * static_cast<float>(sample));
        const auto gain =
            incoming ? smoothPosition : 1.0f - smoothPosition;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.setSample(
                channel, sample,
                buffer.getSample(channel, sample) * gain);
    }
}

void applySnapshotSpillFade(
    juce::AudioBuffer<float>& buffer,
    int samplesRemaining,
    int naturalFadeSamples,
    int retiringFadeSamplesRemaining,
    int retiringFadeSamplesTotal) noexcept
{
    const auto naturalFadeActive =
        samplesRemaining <= naturalFadeSamples + buffer.getNumSamples();
    const auto retiringFadeActive = retiringFadeSamplesTotal > 0;
    if (!naturalFadeActive && !retiringFadeActive)
        return;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        auto gain = 1.0f;
        if (naturalFadeActive)
            gain *= smoothSnapshotFade(
                static_cast<float>(samplesRemaining - sample)
                / static_cast<float>(naturalFadeSamples));
        if (retiringFadeActive)
            gain *= smoothSnapshotFade(
                static_cast<float>(
                    retiringFadeSamplesRemaining - sample)
                / static_cast<float>(retiringFadeSamplesTotal));
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.setSample(
                channel, sample,
                buffer.getSample(channel, sample) * gain);
    }
}
} // namespace

MegaDSPAudioProcessor::MegaDSPAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
      parameters(*this, nullptr, "megaDSPState", megadsp::createParameterLayout())
{
    inheritGlobalImpulseResponseRoot(parameters.state);
    inheritGlobalNamModelRoot(parameters.state);
    (void) automaticLibraryCoordinator();
    for (int slot = 0; slot < megadsp::slotCount; ++slot)
    {
        pendingBuiltInCabinetLoads[static_cast<size_t>(slot)].store(
            -1, std::memory_order_relaxed);
        pendingWormholeReloads[static_cast<size_t>(slot)].store(
            0, std::memory_order_relaxed);
        typeParameterIds[static_cast<size_t>(slot)] =
            megadsp::slotParameterId(slot, "type");
        bypassParameterIds[static_cast<size_t>(slot)] =
            megadsp::slotParameterId(slot, "bypass");
        for (int control = 0;
             control < megadsp::controlsPerSlot; ++control)
            controlParameterIds[static_cast<size_t>(slot)]
                               [static_cast<size_t>(control)] =
                megadsp::controlParameterId(slot, control);
    }
    for (int index = 0; index < megadsp::snapshotCount; ++index)
        snapshotRacks[static_cast<size_t>(index)] =
            std::make_unique<megadsp::EffectRack>(
                snapshotStates[static_cast<size_t>(index)]);
    snapshotStates[0].populated.store(true, std::memory_order_relaxed);
    captureParametersToSnapshot(0);
    diagnosticInstanceId =
        megadsp::Diagnostics::instance().registerInstance();
    for (auto* parameter : getParameters())
    {
        parameter->addListener(this);
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
            parameters.addParameterListener(ranged->paramID, this);
    }
    undoManager.setMaxNumberOfStoredUnits(64, 8);
    reaperEmbeddedUI =
        std::make_unique<megadsp::ReaperEmbeddedUI>(*this);
    startTimerHz(30);
}

MegaDSPAudioProcessor::~MegaDSPAudioProcessor()
{
    stopTimer();
    libraryIndexPool.removeAllJobs(true, 30000);
    releaseAutomaticLibraryClaims();
    impulseResponseLoadPool.removeAllJobs(true, 30000);
    namModelLoadPool.removeAllJobs(true, 30000);
    namModelDisposalPool.removeAllJobs(true, 30000);
    {
        const juce::ScopedLock lock(pendingNamModelLoadLock);
        if (pendingNamModelLoad != nullptr
            && pendingNamModelLoad->load.prepared != nullptr)
            pendingNamModelLoad->rack->cancelNamModel(
                pendingNamModelLoad->load);
        pendingNamModelLoad.reset();
    }
    {
        const juce::ScopedLock lock(pendingImpulseResponseLoadLock);
        if (pendingImpulseResponseLoad != nullptr
            && pendingImpulseResponseLoad->load.prepared != nullptr)
            pendingImpulseResponseLoad->rack->cancelImpulseResponse(
                pendingImpulseResponseLoad->load);
        pendingImpulseResponseLoad.reset();
    }
    for (auto* parameter : getParameters())
    {
        parameter->removeListener(this);
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
            parameters.removeParameterListener(ranged->paramID, this);
    }
    cancelPendingUpdate();
    megadsp::Diagnostics::instance().unregisterInstance(
        diagnosticInstanceId);
}

juce::VST3ClientExtensions*
MegaDSPAudioProcessor::getVST3ClientExtensions()
{
    return reaperEmbeddedUI.get();
}

void MegaDSPAudioProcessor::beginUndoTransaction(
    const juce::String& name)
{
    auto* messageManager =
        juce::MessageManager::getInstanceWithoutCreating();
    if (restoringUndoState || messageManager == nullptr
        || !messageManager->isThisTheMessageThread())
        return;
    if (undoTransactionDepth++ != 0)
        return;
    pendingUndoName = name.trim().isNotEmpty()
        ? name.trim() : "Edit megaDSP";
    pendingUndoBefore.reset();
    getStateInformation(pendingUndoBefore);
}

void MegaDSPAudioProcessor::endUndoTransaction()
{
    if (restoringUndoState || undoTransactionDepth <= 0)
        return;
    if (--undoTransactionDepth != 0)
        return;

    juce::MemoryBlock after;
    getStateInformation(after);
    if (!memoryBlocksMatch(pendingUndoBefore, after))
    {
        undoManager.beginNewTransaction(pendingUndoName);
        undoManager.perform(new ProcessorStateUndoAction(
            *this, std::move(pendingUndoBefore), std::move(after)));
    }
    pendingUndoBefore.reset();
    pendingUndoName.clear();
}

bool MegaDSPAudioProcessor::canUndo() const
{
    return undoManager.canUndo();
}

bool MegaDSPAudioProcessor::canRedo() const
{
    return undoManager.canRedo();
}

juce::String MegaDSPAudioProcessor::undoDescription() const
{
    return undoManager.getUndoDescription();
}

juce::String MegaDSPAudioProcessor::redoDescription() const
{
    return undoManager.getRedoDescription();
}

bool MegaDSPAudioProcessor::undo()
{
    if (!canUndo())
        return false;
    const auto restored = undoManager.undo();
    if (restored)
        updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
    return restored;
}

bool MegaDSPAudioProcessor::redo()
{
    if (!canRedo())
        return false;
    const auto restored = undoManager.redo();
    if (restored)
        updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
    return restored;
}

void MegaDSPAudioProcessor::restoreUndoState(
    const juce::MemoryBlock& state)
{
    const juce::ScopedValueSetter<bool> restoring(
        restoringUndoState, true);
    undoTransactionDepth = 0;
    pendingUndoBefore.reset();
    pendingUndoName.clear();
    setStateInformation(
        state.getData(), static_cast<int>(state.getSize()));
}

void MegaDSPAudioProcessor::parameterGestureChanged(
    int parameterIndex, bool starting)
{
    auto* messageManager =
        juce::MessageManager::getInstanceWithoutCreating();
    if (restoringUndoState || messageManager == nullptr
        || !messageManager->isThisTheMessageThread())
        return;
    if (starting)
    {
        const auto& allParameters = getParameters();
        const auto name =
            juce::isPositiveAndBelow(parameterIndex, allParameters.size())
            ? allParameters[parameterIndex]->getName(64)
            : juce::String("Parameter");
        beginUndoTransaction("Adjust " + name);
    }
    else
        endUndoTransaction();
}

void MegaDSPAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    preparedSampleRate = sampleRate;
    preparedMaximumBlockSize = juce::jmax(1, samplesPerBlock);
    preparedChannelCount = juce::jmax(1, getTotalNumOutputChannels());
    snapshotTransition = {};
    for (auto& spill : spillSnapshots)
        spill = {};
    nextSpillOrder = 0;
    activeAudioSnapshot.store(
        selectedSnapshot.load(std::memory_order_acquire),
        std::memory_order_release);
    snapshotTailSamples.store(0, std::memory_order_release);
    pendingSnapshotRecall.store(-1, std::memory_order_release);
    for (int index = 0; index < megadsp::snapshotCount; ++index)
    {
        auto& state = snapshotStates[static_cast<size_t>(index)];
        snapshotRacks[static_cast<size_t>(index)]->prepare(
            sampleRate, samplesPerBlock, preparedChannelCount);
        state.ready.store(
            state.populated.load(std::memory_order_relaxed),
            std::memory_order_release);
    }
    snapshotScratch.setSize(preparedChannelCount, preparedMaximumBlockSize);
    snapshotZero.setSize(preparedChannelCount, preparedMaximumBlockSize);
    snapshotZero.clear();
    for (auto& output : liveSafeSnapshotOutputs)
    {
        output.setSize(
            preparedChannelCount, preparedMaximumBlockSize,
            false, true, false);
        output.clear();
    }
    const auto bypassCapacity = static_cast<size_t>(
        juce::jmax(1, juce::roundToInt(sampleRate * 0.02) * megadsp::slotCount));
    for (auto& channel : hostBypassDelay)
        channel.assign(bypassCapacity, 0.0f);
    hostBypassWritePosition = 0;
    lastHostBypassLatency = 0;
    pendingLatency.store(getRack().latencySamples());
    setLatencySamples(pendingLatency.load());
    latencyDirty.store(false, std::memory_order_release);
    audioCallbackGeneration.store(0, std::memory_order_release);
    lastAudioBlockSamples.store(0, std::memory_order_release);
    observedAudioCallbackGeneration = 0;
    audioIdlePollCount = 0;
    audioStreamActive.store(false, std::memory_order_release);
    megadsp::Diagnostics::instance().log(
        "prepare",
        "instance=" + juce::String(diagnosticInstanceId)
            + " sampleRate=" + juce::String(sampleRate, 1)
            + " maxBlock=" + juce::String(samplesPerBlock)
            + " channels=" + juce::String(getTotalNumOutputChannels()));
}

void MegaDSPAudioProcessor::releaseResources()
{
    for (auto& snapshotRack : snapshotRacks)
        snapshotRack->reset();
}

bool MegaDSPAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto mainInput = layouts.getChannelSet(true, 0);
    const auto mainOutput = layouts.getChannelSet(false, 0);
    if (mainInput != mainOutput
        || (mainInput != juce::AudioChannelSet::mono()
            && mainInput != juce::AudioChannelSet::stereo()))
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sidechain = layouts.getChannelSet(true, 1);
        if (!sidechain.isDisabled()
            && sidechain != juce::AudioChannelSet::mono()
            && sidechain != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void MegaDSPAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& midi)
{
    const auto callbackStart = static_cast<std::uint64_t>(
        juce::Time::getHighResolutionTicks());
    const auto diagnosticsEnabled =
        megadsp::Diagnostics::instance().isEnabled();
    captureRealtimeProfile =
        diagnosticsEnabled
        && (++diagnosticCallbackCount % 31u) == 0u;
    realtimeProfile.reset();
    auto recalledSnapshot = false;
    audioStreamActive.store(true, std::memory_order_release);
    lastAudioBlockSamples.store(
        buffer.getNumSamples(), std::memory_order_relaxed);
    audioCallbackGeneration.fetch_add(1, std::memory_order_relaxed);
    for (int channel = getTotalNumInputChannels();
         channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    auto main = getBusBuffer(buffer, false, 0);
    const juce::AudioBuffer<float>* sidechainPointer = nullptr;
    juce::AudioBuffer<float> sidechain;
    if (getBusCount(true) > 1 && getChannelCountOfBus(true, 1) > 0)
    {
        sidechain = getBusBuffer(buffer, true, 1);
        sidechainPointer = &sidechain;
    }

    const auto bypassLatency = juce::jmin(
        getLatencySamples(), static_cast<int>(hostBypassDelay[0].size()));
    updateHostBypassLatency(bypassLatency);
    if (bypassLatency > 0)
    {
        for (int sample = 0; sample < main.getNumSamples(); ++sample)
        {
            for (int channel = 0; channel < main.getNumChannels(); ++channel)
                hostBypassDelay[static_cast<size_t>(juce::jmin(channel, 1))][
                    static_cast<size_t>(hostBypassWritePosition)] =
                    main.getSample(channel, sample);
            if (++hostBypassWritePosition >= bypassLatency)
                hostBypassWritePosition = 0;
        }
    }

    const auto bpm = currentBpm();
    const auto processRange = [this, &main, sidechainPointer, bpm](
                                  int start, int samples)
    {
        if (samples <= 0)
            return;
        std::array<float*, 2> mainPointers {};
        for (int channel = 0; channel < main.getNumChannels(); ++channel)
            mainPointers[static_cast<size_t>(channel)] =
                main.getWritePointer(channel, start);
        juce::AudioBuffer<float> mainRange(
            mainPointers.data(), main.getNumChannels(), samples);

        std::array<float*, 2> sidechainPointers {};
        std::optional<juce::AudioBuffer<float>> sidechainRange;
        if (sidechainPointer != nullptr)
        {
            for (int channel = 0;
                 channel < sidechainPointer->getNumChannels(); ++channel)
                sidechainPointers[static_cast<size_t>(channel)] =
                    const_cast<float*>(
                        sidechainPointer->getReadPointer(channel, start));
            sidechainRange.emplace(
                sidechainPointers.data(),
                sidechainPointer->getNumChannels(), samples);
        }
        processSnapshotRange(
            mainRange,
            sidechainRange.has_value() ? &*sidechainRange : nullptr,
            bpm);
    };

    if (const auto requested =
            pendingSnapshotRecall.exchange(-1, std::memory_order_acq_rel);
        requested >= 0)
    {
        beginSnapshotRecall(requested);
        recalledSnapshot = true;
    }

    auto position = 0;
    for (const auto metadata : midi)
    {
        const auto program = programChangeNumber(metadata);
        if (program < 0)
            continue;
        const auto eventPosition = juce::jlimit(
            position, main.getNumSamples(), metadata.samplePosition);
        processRange(position, eventPosition - position);
        beginSnapshotRecall(program);
        recalledSnapshot = true;
        position = eventPosition;
    }
    processRange(position, main.getNumSamples() - position);

    auto outputPeak = 0.0f;
    auto outputIsSafe = true;
    const auto outputChannels = juce::jmin(
        buffer.getNumChannels(), getTotalNumOutputChannels());
    for (int channel = 0; channel < outputChannels; ++channel)
    {
        const auto* samples = buffer.getReadPointer(channel);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto magnitude = std::abs(samples[sample]);
            if (!std::isfinite(magnitude))
            {
                outputIsSafe = false;
                outputPeak = std::numeric_limits<float>::infinity();
                break;
            }
            outputPeak = juce::jmax(outputPeak, magnitude);
            if (outputPeak > 8.0f)
            {
                outputIsSafe = false;
                break;
            }
        }
        if (!outputIsSafe)
            break;
    }
    if (!outputIsSafe)
    {
        for (int channel = 0; channel < outputChannels; ++channel)
            buffer.clear(channel, 0, buffer.getNumSamples());
        megadsp::Diagnostics::instance().logRealtime(
            megadsp::DiagnosticEvent::unsafeOutputRejected,
            diagnosticInstanceId,
            std::isfinite(outputPeak)
                ? static_cast<std::int64_t>(
                    juce::roundToInt(outputPeak * 1000.0f))
                : -1,
            activeAudioSnapshot.load(std::memory_order_relaxed));
    }

    const auto latency = getAudioRack().latencySamples();
    if (latency != pendingLatency.exchange(latency))
    {
        latencyDirty.store(true, std::memory_order_release);
        megadsp::Diagnostics::instance().logRealtime(
            megadsp::DiagnosticEvent::latencyChanged,
            diagnosticInstanceId, latency);
    }
    if (main.getNumSamples() != lastDiagnosticBlockSize
        || main.getNumChannels() != lastDiagnosticChannelCount)
    {
        lastDiagnosticBlockSize = main.getNumSamples();
        lastDiagnosticChannelCount = main.getNumChannels();
        megadsp::Diagnostics::instance().logRealtime(
            megadsp::DiagnosticEvent::audioConfiguration,
            diagnosticInstanceId, lastDiagnosticBlockSize,
            lastDiagnosticChannelCount);
    }
    const auto elapsedTicks = static_cast<std::uint64_t>(
        juce::Time::getHighResolutionTicks()) - callbackStart;
    const auto elapsedMicros = static_cast<std::int64_t>(
        juce::Time::highResolutionTicksToSeconds(
            static_cast<juce::int64>(elapsedTicks))
        * 1000000.0);
    if (recalledSnapshot)
    {
        snapshotRecallDurationMicros.store(
            elapsedMicros, std::memory_order_release);
        megadsp::Diagnostics::instance().logRealtime(
            megadsp::DiagnosticEvent::snapshotRecallWorkload,
            diagnosticInstanceId, elapsedMicros,
            processedSnapshotGraphCount.load(
                std::memory_order_relaxed));
    }
    recordRealtimeDiagnostics(elapsedMicros, main.getNumSamples());
    recordPerformanceMetrics(
        elapsedTicks, main.getNumSamples(), false);
}

void MegaDSPAudioProcessor::processBlockBypassed(
    juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const auto callbackStart = static_cast<std::uint64_t>(
        juce::Time::getHighResolutionTicks());
    realtimeProfile.reset();
    audioStreamActive.store(true, std::memory_order_release);
    lastAudioBlockSamples.store(
        buffer.getNumSamples(), std::memory_order_relaxed);
    audioCallbackGeneration.fetch_add(1, std::memory_order_relaxed);
    for (auto& spill : spillSnapshots)
    {
        spill = {};
    }
    snapshotTransition = {};
    snapshotTailSamples.store(0, std::memory_order_release);

    const auto selectWithoutTransition = [this](int index)
    {
        if (!juce::isPositiveAndBelow(index, megadsp::snapshotCount)
            || !snapshotStates[static_cast<size_t>(index)]
                    .populated.load(std::memory_order_acquire)
            || !snapshotStates[static_cast<size_t>(index)]
                    .ready.load(std::memory_order_acquire))
            return;
        const auto previous =
            activeAudioSnapshot.exchange(index, std::memory_order_acq_rel);
        selectedSnapshot.store(index, std::memory_order_release);
        const auto activeVisualSlots =
            snapshotRacks[static_cast<size_t>(previous)]
                ->visualizationData().getActiveSlotMask();
        snapshotRacks[static_cast<size_t>(previous)]
            ->visualizationData().clearActiveSlots();
        snapshotRacks[static_cast<size_t>(index)]
            ->visualizationData().setActiveSlotMask(activeVisualSlots);
        snapshotUiDirty.store(true, std::memory_order_release);
        snapshotGeneration.fetch_add(1, std::memory_order_release);
    };
    if (const auto requested =
            pendingSnapshotRecall.exchange(-1, std::memory_order_acq_rel);
        requested >= 0)
        selectWithoutTransition(requested);
    for (const auto metadata : midi)
        if (const auto program = programChangeNumber(metadata);
            program >= 0)
            selectWithoutTransition(program);

    auto main = getBusBuffer(buffer, false, 0);
    const auto latency = juce::jmin(
        getLatencySamples(), static_cast<int>(hostBypassDelay[0].size()));
    updateHostBypassLatency(latency);
    if (latency <= 0)
    {
        recordPerformanceMetrics(
            static_cast<std::uint64_t>(
                juce::Time::getHighResolutionTicks())
                - callbackStart,
            main.getNumSamples(), true);
        return;
    }

    for (int sample = 0; sample < main.getNumSamples(); ++sample)
    {
        for (int channel = 0; channel < main.getNumChannels(); ++channel)
        {
            auto& delayed = hostBypassDelay[static_cast<size_t>(juce::jmin(channel, 1))][
                static_cast<size_t>(hostBypassWritePosition)];
            const auto input = main.getSample(channel, sample);
            main.setSample(channel, sample, delayed);
            delayed = input;
        }

        if (++hostBypassWritePosition >= latency)
            hostBypassWritePosition = 0;
    }
    recordPerformanceMetrics(
        static_cast<std::uint64_t>(
            juce::Time::getHighResolutionTicks())
            - callbackStart,
        main.getNumSamples(), true);
}

PluginPerformanceSnapshot
MegaDSPAudioProcessor::performanceSnapshot() const noexcept
{
    PluginPerformanceSnapshot result;
    result.currentLoad =
        performanceCurrentLoad.load(std::memory_order_acquire);
    result.averageLoad =
        performanceAverageLoad.load(std::memory_order_acquire);
    result.peakLoad =
        performancePeakLoad.load(std::memory_order_acquire);
    result.lastCallbackMilliseconds =
        performanceLastCallbackMs.load(std::memory_order_acquire);
    result.deadlineMilliseconds =
        performanceDeadlineMs.load(std::memory_order_acquire);
    result.sampleRate =
        performanceSampleRate.load(std::memory_order_acquire);
    result.blockSamples =
        performanceBlockSamples.load(std::memory_order_acquire);
    result.overrunCount =
        performanceOverruns.load(std::memory_order_acquire);
    result.offline =
        performanceOffline.load(std::memory_order_acquire);
    result.liveSafe =
        performanceLiveSafe.load(std::memory_order_acquire);
    result.hostBypassed =
        performanceHostBypassed.load(std::memory_order_acquire);
    result.backgroundActivity =
        impulseResponseIndexInProgress.load(std::memory_order_acquire)
        || namModelIndexInProgress.load(std::memory_order_acquire)
        || topologyMutationInProgress.load(std::memory_order_acquire);

    const auto writeIndex = juce::jlimit(
        0, PluginPerformanceSnapshot::historySize - 1,
        performanceHistoryWriteIndex.load(std::memory_order_acquire));
    result.historyCount = juce::jlimit(
        0, PluginPerformanceSnapshot::historySize,
        performanceHistoryCount.load(std::memory_order_acquire));
    const auto oldest = result.historyCount
            == PluginPerformanceSnapshot::historySize
        ? writeIndex : 0;
    for (int index = 0; index < result.historyCount; ++index)
        result.history[static_cast<size_t>(index)] =
            performanceHistory[static_cast<size_t>(
                (oldest + index)
                % PluginPerformanceSnapshot::historySize)]
                .load(std::memory_order_acquire);

    std::array<std::uint32_t, performanceHistogramBins> histogram {};
    std::uint64_t total = 0;
    for (size_t index = 0; index < histogram.size(); ++index)
    {
        histogram[index] =
            performanceHistogram[index].load(std::memory_order_acquire);
        total += histogram[index];
    }
    const auto percentile = [&histogram, total](double fraction)
    {
        if (total == 0)
            return 0.0f;
        const auto target = static_cast<std::uint64_t>(
            std::ceil(fraction * static_cast<double>(total)));
        std::uint64_t accumulated = 0;
        for (size_t index = 0; index < histogram.size(); ++index)
        {
            accumulated += histogram[index];
            if (accumulated >= target)
                return static_cast<float>(index) / 200.0f;
        }
        return 2.0f;
    };
    result.percentile95 = percentile(0.95);
    result.percentile99 = percentile(0.99);

    for (int slot = 0; slot < megadsp::slotCount; ++slot)
    {
        result.slotAverageLoad[static_cast<size_t>(slot)] =
            performanceSlotAverageLoad[static_cast<size_t>(slot)].load(
                std::memory_order_acquire);
        result.slotPeakLoad[static_cast<size_t>(slot)] =
            performanceSlotPeakLoad[static_cast<size_t>(slot)].load(
                std::memory_order_acquire);
    }
    result.otherSnapshotsAverageLoad =
        performanceOtherSnapshotsAverageLoad.load(
            std::memory_order_acquire);
    result.otherSnapshotsPeakLoad =
        performanceOtherSnapshotsPeakLoad.load(
            std::memory_order_acquire);
    return result;
}

void MegaDSPAudioProcessor::resetPerformanceMetrics() noexcept
{
    performanceCurrentLoad.store(0.0f, std::memory_order_release);
    performanceAverageLoad.store(0.0f, std::memory_order_release);
    performancePeakLoad.store(0.0f, std::memory_order_release);
    performanceLastCallbackMs.store(0.0f, std::memory_order_release);
    performanceOverruns.store(0, std::memory_order_release);
    performanceHistoryWriteIndex.store(0, std::memory_order_release);
    performanceHistoryCount.store(0, std::memory_order_release);
    for (auto& value : performanceHistory)
        value.store(0.0f, std::memory_order_release);
    for (auto& value : performanceHistogram)
        value.store(0, std::memory_order_release);
    for (auto& value : performanceSlotAverageLoad)
        value.store(0.0f, std::memory_order_release);
    for (auto& value : performanceSlotPeakLoad)
        value.store(0.0f, std::memory_order_release);
    performanceOtherSnapshotsAverageLoad.store(
        0.0f, std::memory_order_release);
    performanceOtherSnapshotsPeakLoad.store(
        0.0f, std::memory_order_release);
    performanceResetGeneration.fetch_add(
        1, std::memory_order_acq_rel);
}

void MegaDSPAudioProcessor::recordPerformanceMetrics(
    std::uint64_t elapsedTicks, int blockSamples,
    bool hostBypassed) noexcept
{
    if (blockSamples <= 0 || preparedSampleRate <= 0.0)
        return;

    const auto resetGeneration =
        performanceResetGeneration.load(std::memory_order_acquire);
    if (resetGeneration != observedPerformanceResetGeneration)
    {
        observedPerformanceResetGeneration = resetGeneration;
        performanceAverageState = 0.0f;
        performancePeakState = 0.0f;
        performancePeakHoldSamples = 0;
        performanceSlotAverageState.fill(0.0f);
        performanceSlotPeakState.fill(0.0f);
        performanceSlotPeakHoldSamples.fill(0);
        performanceOtherSnapshotsAverageState = 0.0f;
        performanceOtherSnapshotsPeakState = 0.0f;
        performanceOtherSnapshotsPeakHoldSamples = 0;
        performanceProfileSnapshot = -1;
        performanceHistorySamples = 0;
        performanceHistoryMaximum = 0.0f;
    }

    const auto elapsedSeconds =
        juce::Time::highResolutionTicksToSeconds(
            static_cast<juce::int64>(elapsedTicks));
    const auto deadlineSeconds =
        static_cast<double>(blockSamples) / preparedSampleRate;
    const auto load = static_cast<float>(
        elapsedSeconds / juce::jmax(1.0e-9, deadlineSeconds));
    const auto smoothing = static_cast<float>(
        deadlineSeconds / (1.0 + deadlineSeconds));
    const auto peakDecay = static_cast<float>(
        juce::jmax(0.0, 1.0 - deadlineSeconds / 1.5));
    const auto peakHoldSamples = juce::roundToInt(
        preparedSampleRate * 5.0);
    const auto updatePeak = [
        blockSamples, peakHoldSamples, peakDecay](
        float value, float& peak, int& hold)
    {
        if (value >= peak)
        {
            peak = value;
            hold = peakHoldSamples;
        }
        else if (hold > 0)
            hold = juce::jmax(0, hold - blockSamples);
        else
            peak = juce::jmax(value, peak * peakDecay);
    };

    performanceAverageState +=
        smoothing * (load - performanceAverageState);
    updatePeak(
        load, performancePeakState, performancePeakHoldSamples);
    performanceCurrentLoad.store(load, std::memory_order_release);
    performanceAverageLoad.store(
        performanceAverageState, std::memory_order_release);
    performancePeakLoad.store(
        performancePeakState, std::memory_order_release);
    performanceLastCallbackMs.store(
        static_cast<float>(elapsedSeconds * 1000.0),
        std::memory_order_release);
    performanceDeadlineMs.store(
        static_cast<float>(deadlineSeconds * 1000.0),
        std::memory_order_release);
    performanceSampleRate.store(
        static_cast<float>(preparedSampleRate),
        std::memory_order_release);
    performanceBlockSamples.store(
        blockSamples, std::memory_order_release);
    const auto offline = isNonRealtime();
    if (load > 1.0f && !offline)
        performanceOverruns.fetch_add(1, std::memory_order_relaxed);
    const auto histogramIndex = static_cast<size_t>(juce::jlimit(
        0, performanceHistogramBins - 1,
        juce::roundToInt(load * 200.0f)));
    performanceHistogram[histogramIndex].fetch_add(
        1, std::memory_order_relaxed);
    performanceOffline.store(
        offline, std::memory_order_release);
    performanceLiveSafe.store(
        liveSafeSnapshots.load(std::memory_order_relaxed),
        std::memory_order_release);
    performanceHostBypassed.store(
        hostBypassed, std::memory_order_release);

    const auto profileSnapshot =
        activeAudioSnapshot.load(std::memory_order_relaxed);
    if (profileSnapshot != performanceProfileSnapshot)
    {
        performanceProfileSnapshot = profileSnapshot;
        performanceSlotAverageState.fill(0.0f);
        performanceSlotPeakState.fill(0.0f);
        performanceSlotPeakHoldSamples.fill(0);
    }
    const auto ticksToLoad = [deadlineSeconds](std::uint64_t ticks)
    {
        return static_cast<float>(
            juce::Time::highResolutionTicksToSeconds(
                static_cast<juce::int64>(ticks))
            / juce::jmax(1.0e-9, deadlineSeconds));
    };
    std::uint64_t otherSnapshotTicks = 0;
    for (int snapshot = 0; snapshot < megadsp::snapshotCount; ++snapshot)
        for (int slot = 0; slot < megadsp::slotCount; ++slot)
        {
            const auto ticks = realtimeProfile.slotTicks[
                static_cast<size_t>(snapshot)][static_cast<size_t>(slot)];
            if (snapshot != profileSnapshot)
                otherSnapshotTicks += ticks;
        }
    for (int slot = 0; slot < megadsp::slotCount; ++slot)
    {
        const auto slotLoad = juce::isPositiveAndBelow(
                                  profileSnapshot,
                                  megadsp::snapshotCount)
            ? ticksToLoad(realtimeProfile.slotTicks[
                  static_cast<size_t>(profileSnapshot)]
                  [static_cast<size_t>(slot)])
            : 0.0f;
        auto& average =
            performanceSlotAverageState[static_cast<size_t>(slot)];
        auto& peak =
            performanceSlotPeakState[static_cast<size_t>(slot)];
        auto& hold =
            performanceSlotPeakHoldSamples[static_cast<size_t>(slot)];
        average += smoothing * (slotLoad - average);
        updatePeak(slotLoad, peak, hold);
        performanceSlotAverageLoad[static_cast<size_t>(slot)].store(
            average, std::memory_order_release);
        performanceSlotPeakLoad[static_cast<size_t>(slot)].store(
            peak, std::memory_order_release);
    }
    const auto otherLoad = ticksToLoad(otherSnapshotTicks);
    performanceOtherSnapshotsAverageState += smoothing
        * (otherLoad - performanceOtherSnapshotsAverageState);
    updatePeak(
        otherLoad, performanceOtherSnapshotsPeakState,
        performanceOtherSnapshotsPeakHoldSamples);
    performanceOtherSnapshotsAverageLoad.store(
        performanceOtherSnapshotsAverageState,
        std::memory_order_release);
    performanceOtherSnapshotsPeakLoad.store(
        performanceOtherSnapshotsPeakState,
        std::memory_order_release);

    performanceHistorySamples += blockSamples;
    performanceHistoryMaximum =
        juce::jmax(performanceHistoryMaximum, load);
    const auto historyInterval =
        juce::jmax(1, juce::roundToInt(preparedSampleRate / 10.0));
    if (performanceHistorySamples >= historyInterval)
    {
        const auto writeIndex = performanceHistoryWriteIndex.load(
            std::memory_order_relaxed);
        performanceHistory[static_cast<size_t>(writeIndex)].store(
            performanceHistoryMaximum, std::memory_order_release);
        performanceHistoryWriteIndex.store(
            (writeIndex + 1)
                % PluginPerformanceSnapshot::historySize,
            std::memory_order_release);
        const auto count = performanceHistoryCount.load(
            std::memory_order_relaxed);
        performanceHistoryCount.store(
            juce::jmin(
                PluginPerformanceSnapshot::historySize, count + 1),
            std::memory_order_release);
        performanceHistorySamples %= historyInterval;
        performanceHistoryMaximum = 0.0f;
    }
}

void MegaDSPAudioProcessor::recordRealtimeDiagnostics(
    std::int64_t elapsedMicros, int blockSamples) noexcept
{
    auto& diagnostics = megadsp::Diagnostics::instance();
    if (!diagnostics.isEnabled() || blockSamples <= 0
        || preparedSampleRate <= 0.0)
    {
        diagnosticWindowSamples = 0;
        diagnosticWindowCallbacks = 0;
        diagnosticTotalCallbackMicros = 0;
        diagnosticMaximumCallbackMicros = 0;
        diagnosticMinimumDeadlineMicros = 0;
        diagnosticOverrunCount = 0;
        diagnosticMaximumGraphCount = 0;
        accumulatedRealtimeProfile.reset();
        return;
    }

    const auto deadlineMicros = juce::jmax<std::int64_t>(
        1, static_cast<std::int64_t>(juce::roundToInt(
               1000000.0 * static_cast<double>(blockSamples)
               / preparedSampleRate)));
    diagnosticWindowSamples += blockSamples;
    ++diagnosticWindowCallbacks;
    diagnosticTotalCallbackMicros += elapsedMicros;
    diagnosticMaximumCallbackMicros = juce::jmax(
        diagnosticMaximumCallbackMicros, elapsedMicros);
    diagnosticMinimumDeadlineMicros =
        diagnosticMinimumDeadlineMicros == 0
        ? deadlineMicros
        : juce::jmin(diagnosticMinimumDeadlineMicros, deadlineMicros);
    if (elapsedMicros > deadlineMicros)
        ++diagnosticOverrunCount;
    diagnosticMaximumGraphCount = juce::jmax(
        diagnosticMaximumGraphCount,
        processedSnapshotGraphCount.load(std::memory_order_relaxed));
    if (captureRealtimeProfile)
        accumulatedRealtimeProfile.add(realtimeProfile);

    if (diagnosticWindowSamples
        < static_cast<std::int64_t>(
            juce::roundToInt(preparedSampleRate)))
        return;

    const auto averageMicros =
        diagnosticWindowCallbacks > 0
        ? diagnosticTotalCallbackMicros / diagnosticWindowCallbacks : 0;
    const auto packedOverrunsAndGraphs = static_cast<std::int64_t>(
        (static_cast<std::uint64_t>(
             static_cast<std::uint32_t>(diagnosticOverrunCount)) << 32)
        | static_cast<std::uint32_t>(diagnosticMaximumGraphCount));
    diagnostics.logRealtime(
        megadsp::DiagnosticEvent::audioCallbackWorkload,
        diagnosticInstanceId, averageMicros,
        diagnosticMaximumCallbackMicros,
        diagnosticMinimumDeadlineMicros,
        packedOverrunsAndGraphs);

    const auto ticksToMicros = [](std::uint64_t ticks)
    {
        return static_cast<std::int64_t>(
            juce::Time::highResolutionTicksToSeconds(
                static_cast<juce::int64>(ticks))
            * 1000000.0);
    };
    std::array<bool, megadsp::moduleTypeCount> reportedModules {};
    for (int rank = 0; rank < 4; ++rank)
    {
        size_t hottest = reportedModules.size();
        for (size_t index = 0; index < reportedModules.size(); ++index)
            if (!reportedModules[index]
                && accumulatedRealtimeProfile.moduleTicks[index] > 0
                && (hottest == reportedModules.size()
                    || accumulatedRealtimeProfile.moduleTicks[index]
                        > accumulatedRealtimeProfile.moduleTicks[hottest]))
                hottest = index;
        if (hottest == reportedModules.size())
            break;
        reportedModules[hottest] = true;
        const auto calls =
            accumulatedRealtimeProfile.moduleCalls[hottest];
        diagnostics.logRealtime(
            megadsp::DiagnosticEvent::moduleWorkload,
            diagnosticInstanceId, static_cast<std::int64_t>(hottest),
            calls > 0
                ? ticksToMicros(
                      accumulatedRealtimeProfile.moduleTicks[hottest])
                      / calls
                : 0,
            ticksToMicros(
                accumulatedRealtimeProfile.moduleMaximumTicks[hottest]),
            calls);
    }

    std::array<bool, megadsp::snapshotCount> reportedGraphs {};
    for (int rank = 0; rank < 4; ++rank)
    {
        size_t hottest = reportedGraphs.size();
        for (size_t index = 0; index < reportedGraphs.size(); ++index)
            if (!reportedGraphs[index]
                && accumulatedRealtimeProfile.graphTicks[index] > 0
                && (hottest == reportedGraphs.size()
                    || accumulatedRealtimeProfile.graphTicks[index]
                        > accumulatedRealtimeProfile.graphTicks[hottest]))
                hottest = index;
        if (hottest == reportedGraphs.size())
            break;
        reportedGraphs[hottest] = true;
        const auto calls =
            accumulatedRealtimeProfile.graphCalls[hottest];
        diagnostics.logRealtime(
            megadsp::DiagnosticEvent::snapshotGraphWorkload,
            diagnosticInstanceId, static_cast<std::int64_t>(hottest),
            calls > 0
                ? ticksToMicros(
                      accumulatedRealtimeProfile.graphTicks[hottest])
                      / calls
                : 0,
            ticksToMicros(
                accumulatedRealtimeProfile.graphMaximumTicks[hottest]),
            calls);
    }

    diagnosticWindowSamples = 0;
    diagnosticWindowCallbacks = 0;
    diagnosticTotalCallbackMicros = 0;
    diagnosticMaximumCallbackMicros = 0;
    diagnosticMinimumDeadlineMicros = 0;
    diagnosticOverrunCount = 0;
    diagnosticMaximumGraphCount = 0;
    accumulatedRealtimeProfile.reset();
}

void MegaDSPAudioProcessor::processProfiledRack(
    int snapshot,
    juce::AudioBuffer<float>& main,
    const juce::AudioBuffer<float>* sidechain,
    double bpm)
{
    auto* profile = &realtimeProfile;
    profile->currentSnapshot = snapshot;
    const auto start = juce::Time::getHighResolutionTicks();
    snapshotRacks[static_cast<size_t>(snapshot)]->process(
        main, sidechain, bpm, profile, isNonRealtime());
    if (profile != nullptr)
        profile->recordGraph(
            snapshot, static_cast<std::uint64_t>(
                juce::Time::getHighResolutionTicks() - start));
}

void MegaDSPAudioProcessor::updateHostBypassLatency(
    int latency) noexcept
{
    if (latency == lastHostBypassLatency)
        return;
    for (auto& channel : hostBypassDelay)
        std::fill(channel.begin(), channel.end(), 0.0f);
    hostBypassWritePosition = 0;
    lastHostBypassLatency = latency;
}

double MegaDSPAudioProcessor::currentBpm() const
{
    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
            if (const auto bpm = position->getBpm())
                return *bpm;
    return 120.0;
}

double MegaDSPAudioProcessor::getTailLengthSeconds() const
{
    const auto spillSeconds =
        static_cast<double>(
            snapshotTailSamples.load(std::memory_order_acquire))
        / juce::jmax(1.0, preparedSampleRate);
    return juce::jmax(getAudioRack().tailSeconds(), spillSeconds);
}

int MegaDSPAudioProcessor::getBackgroundThemeIndex() const
{
    return megadsp::ui::safeBackgroundThemeIndex(static_cast<int>(
        parameters.state.getProperty("backgroundTheme", 0)));
}

void MegaDSPAudioProcessor::setBackgroundThemeIndex(int index)
{
    const auto safeIndex = megadsp::ui::safeBackgroundThemeIndex(index);
    const auto storedIndex = static_cast<int>(
        parameters.state.getProperty("backgroundTheme", 0));
    if (safeIndex == storedIndex)
        return;
    ScopedProcessorUndoTransaction undo(*this, "Change theme");
    parameters.state.setProperty("backgroundTheme", safeIndex, nullptr);
    updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
}

juce::String MegaDSPAudioProcessor::getInstanceName() const
{
    return megadsp::ui::normalizeInstanceName(
        parameters.state.getProperty("instanceName").toString());
}

void MegaDSPAudioProcessor::setInstanceName(const juce::String& name)
{
    const auto normalized = megadsp::ui::normalizeInstanceName(name);
    if (normalized
        == parameters.state.getProperty("instanceName").toString())
        return;
    ScopedProcessorUndoTransaction undo(*this, "Rename instance");
    parameters.state.setProperty("instanceName", normalized, nullptr);
    updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
}

void MegaDSPAudioProcessor::setLiveSafeSnapshotsEnabled(bool enabled)
{
    if (liveSafeSnapshots.exchange(
            enabled, std::memory_order_acq_rel) == enabled)
        return;
    ScopedProcessorUndoTransaction undo(*this, "Toggle Live Safe");
    parameters.state.setProperty(
        "liveSafeSnapshots", enabled, nullptr);
    snapshotGeneration.fetch_add(1, std::memory_order_release);
    updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
}

void MegaDSPAudioProcessor::parameterChanged(
    const juce::String& parameterId, float value)
{
    if (publishingSnapshotParameters)
        return;

    const auto snapshotIndex =
        selectedSnapshot.load(std::memory_order_acquire);
    auto& state =
        snapshotStates[static_cast<size_t>(snapshotIndex)];
    if (parameterId == "global.input")
    {
        state.inputGainDb.store(value, std::memory_order_relaxed);
        return;
    }
    if (parameterId == "global.output")
    {
        state.outputGainDb.store(value, std::memory_order_relaxed);
        return;
    }

    for (int slot = 0; slot < megadsp::slotCount; ++slot)
    {
        auto& destination = state.slots[static_cast<size_t>(slot)];
        if (parameterId == typeParameterIds[static_cast<size_t>(slot)])
        {
            destination.type.store(value, std::memory_order_relaxed);
            topologyRequestGeneration.fetch_add(
                1, std::memory_order_release);
            pendingTopologySnapshotMask.fetch_or(
                std::uint32_t { 1 }
                    << static_cast<unsigned int>(snapshotIndex),
                std::memory_order_release);
            topologyDirty.store(true, std::memory_order_release);
            return;
        }
        if (parameterId == bypassParameterIds[static_cast<size_t>(slot)])
        {
            destination.bypass.store(value, std::memory_order_relaxed);
            return;
        }
        for (int control = 0;
             control < megadsp::controlsPerSlot; ++control)
        {
            if (parameterId
                != controlParameterIds[static_cast<size_t>(slot)]
                                      [static_cast<size_t>(control)])
                continue;
            destination.controls[static_cast<size_t>(control)].store(
                value, std::memory_order_relaxed);
            if (control == 0
                && static_cast<megadsp::ModuleType>(
                       juce::roundToInt(destination.type.load(
                           std::memory_order_relaxed)))
                       == megadsp::ModuleType::guitarCabinet)
                pendingBuiltInCabinetLoads[static_cast<size_t>(slot)].store(
                    megadsp::discreteIndex(
                        value,
                        megadsp::GuitarCabinetModule::builtInCount),
                    std::memory_order_release);
            if (static_cast<megadsp::ModuleType>(
                    juce::roundToInt(destination.type.load(
                        std::memory_order_relaxed)))
                    == megadsp::ModuleType::wormholeConvolution)
            {
                const auto lane = control <= 2 ? 0 : control >= 4 && control <= 6
                    ? 1 : -1;
                if (lane >= 0)
                    pendingWormholeReloads[static_cast<size_t>(slot)].fetch_or(
                        static_cast<std::uint8_t>(1u << lane),
                        std::memory_order_release);
            }
            return;
        }
    }
}

void MegaDSPAudioProcessor::captureParametersToSnapshot(int index)
{
    if (!juce::isPositiveAndBelow(index, megadsp::snapshotCount))
        return;
    auto& state = snapshotStates[static_cast<size_t>(index)];
    for (int slot = 0; slot < megadsp::slotCount; ++slot)
    {
        auto& destination = state.slots[static_cast<size_t>(slot)];
        if (const auto* value = parameters.getRawParameterValue(
                megadsp::slotParameterId(slot, "type")))
            destination.type.store(
                value->load(), std::memory_order_relaxed);
        if (const auto* value = parameters.getRawParameterValue(
                megadsp::slotParameterId(slot, "bypass")))
            destination.bypass.store(
                value->load(), std::memory_order_relaxed);
        for (int control = 0;
             control < megadsp::controlsPerSlot; ++control)
            if (const auto* value = parameters.getRawParameterValue(
                    megadsp::controlParameterId(slot, control)))
                destination.controls[static_cast<size_t>(control)].store(
                    value->load(), std::memory_order_relaxed);
    }
    if (const auto* value =
            parameters.getRawParameterValue("global.input"))
        state.inputGainDb.store(value->load(), std::memory_order_relaxed);
    if (const auto* value =
            parameters.getRawParameterValue("global.output"))
        state.outputGainDb.store(value->load(), std::memory_order_relaxed);
    state.populated.store(true, std::memory_order_release);
}

void MegaDSPAudioProcessor::publishSnapshotToParameters(
    int index, bool notifyHost)
{
    if (!juce::isPositiveAndBelow(index, megadsp::snapshotCount))
        return;
    const auto& state = snapshotStates[static_cast<size_t>(index)];
    publishingSnapshotParameters = true;
    const auto setPlain =
        [this, notifyHost](
            const juce::String& id, float plainValue)
    {
        if (auto* parameter = parameters.getParameter(id))
        {
            const auto normalized =
                parameter->convertTo0to1(plainValue);
            if (std::abs(parameter->getValue() - normalized)
                <= 1.0e-7f)
                return;
            if (notifyHost)
                parameter->setValueNotifyingHost(normalized);
            else
                parameter->setValue(normalized);
        }
    };
    for (int slot = 0; slot < megadsp::slotCount; ++slot)
    {
        const auto& source = state.slots[static_cast<size_t>(slot)];
        setPlain(
            megadsp::slotParameterId(slot, "type"),
            source.type.load(std::memory_order_relaxed));
        setPlain(
            megadsp::slotParameterId(slot, "bypass"),
            source.bypass.load(std::memory_order_relaxed));
        for (int control = 0;
             control < megadsp::controlsPerSlot; ++control)
            setPlain(
                megadsp::controlParameterId(slot, control),
                source.controls[static_cast<size_t>(control)].load(
                    std::memory_order_relaxed));
        const auto irProperty =
            juce::Identifier("impulseResponse" + juce::String(slot + 1));
        const auto irProperty2 =
            juce::Identifier("impulseResponse2" + juce::String(slot + 1));
        const auto namProperty =
            juce::Identifier("neuralModel" + juce::String(slot + 1));
        if (source.impulseResponsePath.isEmpty())
            parameters.state.removeProperty(irProperty, nullptr);
        else
            parameters.state.setProperty(
                irProperty, source.impulseResponsePath, nullptr);
        if (source.impulseResponsePath2.isEmpty())
            parameters.state.removeProperty(irProperty2, nullptr);
        else
            parameters.state.setProperty(
                irProperty2, source.impulseResponsePath2, nullptr);
        if (source.namModelPath.isEmpty())
            parameters.state.removeProperty(namProperty, nullptr);
        else
            parameters.state.setProperty(
                namProperty, source.namModelPath, nullptr);
    }
    setPlain(
        "global.input",
        state.inputGainDb.load(std::memory_order_relaxed));
    setPlain(
        "global.output",
        state.outputGainDb.load(std::memory_order_relaxed));
    publishingSnapshotParameters = false;
}

bool MegaDSPAudioProcessor::isSnapshotPopulated(int index) const noexcept
{
    return juce::isPositiveAndBelow(index, megadsp::snapshotCount)
           && snapshotStates[static_cast<size_t>(index)]
                  .populated.load(std::memory_order_acquire);
}

bool MegaDSPAudioProcessor::isSnapshotReady(int index) const noexcept
{
    return juce::isPositiveAndBelow(index, megadsp::snapshotCount)
           && snapshotStates[static_cast<size_t>(index)]
                  .ready.load(std::memory_order_acquire);
}

juce::String MegaDSPAudioProcessor::snapshotError(int index) const
{
    return juce::isPositiveAndBelow(index, megadsp::snapshotCount)
               ? snapshotStates[static_cast<size_t>(index)].error
               : juce::String();
}

void MegaDSPAudioProcessor::prepareSnapshot(int index)
{
    if (!juce::isPositiveAndBelow(index, megadsp::snapshotCount))
        return;
    auto& state = snapshotStates[static_cast<size_t>(index)];
    state.ready.store(false, std::memory_order_release);
    state.error.clear();
    snapshotRacks[static_cast<size_t>(index)]->rebuildModules(false);
    restoreSnapshotAssets(index);
    state.ready.store(true, std::memory_order_release);
}

void MegaDSPAudioProcessor::restoreSnapshotAssets(int index)
{
    if (!juce::isPositiveAndBelow(index, megadsp::snapshotCount))
        return;
    auto& rack = *snapshotRacks[static_cast<size_t>(index)];
    auto& state = snapshotStates[static_cast<size_t>(index)];
    for (int slot = 0; slot < megadsp::slotCount; ++slot)
    {
        if (rack.moduleType(slot)
            == megadsp::ModuleType::convolutionReverb)
        {
            auto& slotState =
                state.slots[static_cast<size_t>(slot)];
            rack.setConvolutionWetBufferSamples(
                slot, slotState.convolutionWetBufferSamples);
            for (int bank = 0;
                 bank < megadsp::ConvolutionReverbModule::
                            impulseBankCount;
                 ++bank)
            {
                const auto& path =
                    slotState.convolutionImpulseBankPaths[
                        static_cast<size_t>(bank)];
                if (path.isEmpty())
                    continue;
                megadsp::EffectRack::PreparedImpulseResponseLoad load;
                auto result =
                    rack.prepareConvolutionImpulseResponseBank(
                        slot, bank, juce::File(path), load);
                if (result.wasOk()
                    && !rack.commitConvolutionImpulseResponseBank(load))
                    result = juce::Result::fail(
                        "The impulse response was superseded.");
                if (result.failed())
                    state.error +=
                        "IR bank " + juce::String(bank + 1)
                        + " slot " + juce::String(slot + 1)
                        + ": " + result.getErrorMessage() + " ";
            }
            rack.selectConvolutionImpulseResponseBank(
                slot, slotState.convolutionActiveBank);
            continue;
        }
        const std::array<juce::String, 2> impulsePaths {
            state.slots[static_cast<size_t>(slot)].impulseResponsePath,
            state.slots[static_cast<size_t>(slot)].impulseResponsePath2
        };
        for (int lane = 0; lane < 2; ++lane)
        {
            const auto& ir = impulsePaths[static_cast<size_t>(lane)];
            if (ir.isNotEmpty())
            {
                const auto result = rack.loadImpulseResponse(
                    slot, lane, juce::File(ir));
                if (result.failed())
                    state.error += "IR " + juce::String(lane == 0 ? "A" : "B")
                        + " slot " + juce::String(slot + 1)
                        + ": " + result.getErrorMessage() + " ";
            }
        }
        const auto nam =
            state.slots[static_cast<size_t>(slot)].namModelPath;
        if (nam.isNotEmpty())
        {
            const auto& slotState =
                state.slots[static_cast<size_t>(slot)];
            megadsp::EffectRack::PreparedNamModelLoad modelLoad;
            megadsp::EffectRack::PreparedNamModelLoad cabinetLoad;
            auto result = rack.prepareNamModel(
                slot, juce::File(nam), modelLoad);
            const auto preference = slotState.namCabinetStateStored
                ? NamCabinetPreference {
                      slotState.namCabinetPath,
                      slotState.namCabinetEnabled }
                : namCabinetPreference(
                      nam, namModelRootDirectory());
            const auto impulse =
                juce::File(preference.impulsePath);
            if (result.wasOk() && impulse.existsAsFile())
                result = rack.prepareNamCabinet(
                    slot, impulse, cabinetLoad);
            else if (result.wasOk()
                     && preference.impulsePath.isNotEmpty())
                result = juce::Result::fail(
                    "NAM cabinet impulse not found: "
                    + preference.impulsePath);
            if (result.wasOk())
            {
                auto committed = rack.commitNamModel(modelLoad);
                if (committed && cabinetLoad.prepared != nullptr)
                    committed = rack.commitNamCabinet(cabinetLoad);
                else if (committed)
                    rack.clearNamCabinet(slot);
                if (committed)
                    rack.setNamCabinetEnabled(
                        slot, preference.enabled);
                else
                    result = juce::Result::fail(
                        "The NAM model was superseded.");
            }
            if (result.failed())
                state.error += "NAM slot " + juce::String(slot + 1)
                               + ": " + result.getErrorMessage() + " ";
        }
    }
}

void MegaDSPAudioProcessor::recallSnapshot(int index)
{
    if (!juce::isPositiveAndBelow(index, megadsp::snapshotCount))
        return;
    if (index == getSelectedSnapshot())
        return;
    ScopedProcessorUndoTransaction undo(*this, "Recall snapshot");
    if (!isSnapshotPopulated(index))
    {
        suspendProcessing(true);
        auto& state = snapshotStates[static_cast<size_t>(index)];
        state.clear();
        state.populated.store(true, std::memory_order_release);
        prepareSnapshot(index);
        suspendProcessing(false);
    }
    if (!isSnapshotReady(index))
        return;
    selectedSnapshot.store(index, std::memory_order_release);
    pendingSnapshotRecall.store(index, std::memory_order_release);
    snapshotUiDirty.store(true, std::memory_order_release);
    snapshotGeneration.fetch_add(1, std::memory_order_release);
}

void MegaDSPAudioProcessor::storeSnapshot(int index, bool overwrite)
{
    if (!juce::isPositiveAndBelow(index, megadsp::snapshotCount)
        || (isSnapshotPopulated(index) && !overwrite))
        return;
    ScopedProcessorUndoTransaction undo(*this, "Store snapshot");
    const auto source = getSelectedSnapshot();
    suspendProcessing(true);
    snapshotStates[static_cast<size_t>(index)].copyFrom(
        snapshotStates[static_cast<size_t>(source)]);
    snapshotStates[static_cast<size_t>(index)]
        .populated.store(true, std::memory_order_release);
    prepareSnapshot(index);
    suspendProcessing(false);
    recallSnapshot(index);
}

void MegaDSPAudioProcessor::copySnapshot(int source, int destination)
{
    if (!juce::isPositiveAndBelow(source, megadsp::snapshotCount)
        || !juce::isPositiveAndBelow(destination, megadsp::snapshotCount)
        || source == destination
        || !isSnapshotPopulated(source)
        || !isSnapshotReady(source))
        return;

    ScopedProcessorUndoTransaction undo(*this, "Copy snapshot");
    suspendProcessing(true);
    for (auto& spill : spillSnapshots)
    {
        if (spill.snapshot >= 0)
            snapshotRacks[static_cast<size_t>(spill.snapshot)]->reset();
        spill = {};
    }
    if (snapshotTransition.outgoing >= 0)
        snapshotRacks[static_cast<size_t>(
            snapshotTransition.outgoing)]->reset();
    snapshotTransition = {};
    snapshotTailSamples.store(0, std::memory_order_release);

    snapshotStates[static_cast<size_t>(destination)].copyFrom(
        snapshotStates[static_cast<size_t>(source)]);
    snapshotStates[static_cast<size_t>(destination)]
        .populated.store(true, std::memory_order_release);
    prepareSnapshot(destination);
    suspendProcessing(false);

    if (destination == getSelectedSnapshot())
        publishSnapshotToParameters(destination);
    topologyGeneration.fetch_add(1, std::memory_order_release);
    snapshotGeneration.fetch_add(1, std::memory_order_release);
    updateHostDisplay(
        ChangeDetails{}.withNonParameterStateChanged(true));
    refreshLatency();
}

void MegaDSPAudioProcessor::beginSnapshotRecall(int index) noexcept
{
    if (!juce::isPositiveAndBelow(index, megadsp::snapshotCount)
        || !snapshotStates[static_cast<size_t>(index)]
                .populated.load(std::memory_order_acquire)
        || !snapshotStates[static_cast<size_t>(index)]
                .ready.load(std::memory_order_acquire))
        return;

    const auto current =
        activeAudioSnapshot.load(std::memory_order_acquire);
    if (index == current)
        return;
    if (snapshotTransition.outgoing == index
        && snapshotTransition.incoming == current)
    {
        std::swap(
            snapshotTransition.outgoing,
            snapshotTransition.incoming);
        snapshotTransition.samplesDone = juce::jlimit(
            0, snapshotTransition.totalSamples,
            snapshotTransition.totalSamples
                - snapshotTransition.samplesDone);
        snapshotTransition.fadeIncomingOutput = false;
        const auto activeVisualSlots =
            snapshotRacks[static_cast<size_t>(current)]
                ->visualizationData().getActiveSlotMask();
        snapshotRacks[static_cast<size_t>(current)]
            ->visualizationData().clearActiveSlots();
        snapshotRacks[static_cast<size_t>(index)]
            ->visualizationData().setActiveSlotMask(activeVisualSlots);
        activeAudioSnapshot.store(index, std::memory_order_release);
        selectedSnapshot.store(index, std::memory_order_release);
        snapshotUiDirty.store(true, std::memory_order_release);
        snapshotGeneration.fetch_add(1, std::memory_order_release);
        return;
    }
    if (snapshotTransition.outgoing >= 0)
        addSpillSnapshot(snapshotTransition.outgoing);
    auto incomingWasSpilling = false;
    for (auto& spill : spillSnapshots)
        if (spill.snapshot == index)
        {
            incomingWasSpilling = true;
            spill = {};
        }

    snapshotTransition.outgoing = current;
    snapshotTransition.incoming = index;
    snapshotTransition.samplesDone = 0;
    snapshotTransition.totalSamples = juce::jmax(
        1, juce::roundToInt(preparedSampleRate * 0.02));
    snapshotTransition.fadeIncomingOutput = !incomingWasSpilling;
    const auto activeVisualSlots =
        snapshotRacks[static_cast<size_t>(current)]
            ->visualizationData().getActiveSlotMask();
    snapshotRacks[static_cast<size_t>(current)]
        ->visualizationData().clearActiveSlots();
    snapshotRacks[static_cast<size_t>(index)]
        ->visualizationData().setActiveSlotMask(activeVisualSlots);
    activeAudioSnapshot.store(index, std::memory_order_release);
    selectedSnapshot.store(index, std::memory_order_release);
    snapshotTailSamples.store(
        juce::jmax(
            snapshotTailSamples.load(std::memory_order_relaxed),
            snapshotTransition.totalSamples
                + juce::roundToInt(
                    snapshotRacks[static_cast<size_t>(current)]
                        ->snapshotSpillSeconds()
                    * preparedSampleRate)),
        std::memory_order_release);
    snapshotUiDirty.store(true, std::memory_order_release);
    snapshotGeneration.fetch_add(1, std::memory_order_release);
}

void MegaDSPAudioProcessor::addSpillSnapshot(int index) noexcept
{
    if (!juce::isPositiveAndBelow(index, megadsp::snapshotCount)
        || index == activeAudioSnapshot.load(std::memory_order_acquire))
        return;
    for (const auto& spill : spillSnapshots)
        if (spill.snapshot == index)
            return;
    const auto tailSamples = juce::jlimit(
        0, juce::roundToInt(preparedSampleRate * 30.0),
        juce::jmax(
            snapshotRacks[static_cast<size_t>(index)]->latencySamples(),
            juce::roundToInt(
                snapshotRacks[static_cast<size_t>(index)]
                    ->snapshotSpillSeconds()
                * preparedSampleRate)));
    if (tailSamples <= 0)
        return;
    snapshotTailSamples.store(
        juce::jmax(
            snapshotTailSamples.load(std::memory_order_relaxed),
            tailSamples),
        std::memory_order_release);

    auto activeSpills = 0;
    for (const auto& spill : spillSnapshots)
        if (spill.snapshot >= 0 && spill.fadeSamplesRemaining <= 0)
            ++activeSpills;
    if (activeSpills >= 2)
    {
        auto* oldest = static_cast<SpillState*>(nullptr);
        for (auto& spill : spillSnapshots)
            if (spill.snapshot >= 0 && spill.fadeSamplesRemaining <= 0
                && (oldest == nullptr || spill.order < oldest->order))
                oldest = &spill;
        jassert(oldest != nullptr);
        oldest->fadeSamplesTotal = juce::jmax(
            1, juce::roundToInt(preparedSampleRate * 0.02));
        oldest->fadeSamplesRemaining = oldest->fadeSamplesTotal;
    }
    for (auto& spill : spillSnapshots)
        if (spill.snapshot < 0)
        {
            spill.snapshot = index;
            spill.samplesRemaining = tailSamples;
            spill.naturalFadeSamples = juce::jmax(
                1, juce::jmin(
                       tailSamples,
                       juce::roundToInt(preparedSampleRate * 0.5)));
            spill.order = ++nextSpillOrder;
            return;
        }

    auto* oldest = &spillSnapshots[0];
    for (auto& spill : spillSnapshots)
        if (spill.order < oldest->order)
            oldest = &spill;
    *oldest = {
        index,
        tailSamples,
        juce::jmax(
            1, juce::jmin(
                   tailSamples,
                   juce::roundToInt(preparedSampleRate * 0.5))),
        0,
        0,
        ++nextSpillOrder
    };
}

void MegaDSPAudioProcessor::processSpillSnapshots(
    juce::AudioBuffer<float>& main,
    const juce::AudioBuffer<float>*,
    double bpm)
{
    for (auto& spill : spillSnapshots)
    {
        if (spill.snapshot < 0)
            continue;
        snapshotZero.clear();
        std::array<float*, 2> pointers {};
        for (int channel = 0; channel < main.getNumChannels(); ++channel)
            pointers[static_cast<size_t>(channel)] =
                snapshotZero.getWritePointer(channel);
        juce::AudioBuffer<float> tail(
            pointers.data(), main.getNumChannels(), main.getNumSamples());
        processProfiledRack(spill.snapshot, tail, nullptr, bpm);

        applySnapshotSpillFade(
            tail, spill.samplesRemaining,
            spill.naturalFadeSamples,
            spill.fadeSamplesRemaining, spill.fadeSamplesTotal);
        spill.fadeSamplesRemaining = juce::jmax(
            0, spill.fadeSamplesRemaining - main.getNumSamples());
        for (int channel = 0; channel < main.getNumChannels(); ++channel)
            main.addFrom(
                channel, 0, tail, channel, 0, main.getNumSamples());
        spill.samplesRemaining -= main.getNumSamples();
        if (spill.samplesRemaining <= 0
            || (spill.fadeSamplesTotal > 0
                && spill.fadeSamplesRemaining <= 0))
        {
            spill = {};
        }
    }
    auto remaining = 0;
    for (const auto& spill : spillSnapshots)
        if (spill.snapshot >= 0)
            remaining = juce::jmax(remaining, spill.samplesRemaining);
    if (snapshotTransition.outgoing >= 0)
        remaining = juce::jmax(
            remaining,
            juce::jmax(
                0,
                snapshotTransition.totalSamples
                    - snapshotTransition.samplesDone
                    - main.getNumSamples())
                + juce::roundToInt(
                    snapshotRacks[static_cast<size_t>(
                        snapshotTransition.outgoing)]
                        ->snapshotSpillSeconds()
                    * preparedSampleRate));
    snapshotTailSamples.store(remaining, std::memory_order_release);
}

void MegaDSPAudioProcessor::mixLiveSafeSpills(
    juce::AudioBuffer<float>& main)
{
    for (auto& spill : spillSnapshots)
    {
        if (spill.snapshot < 0)
            continue;
        auto& tail =
            liveSafeSnapshotOutputs[static_cast<size_t>(spill.snapshot)];
        applySnapshotSpillFade(
            tail, spill.samplesRemaining,
            spill.naturalFadeSamples,
            spill.fadeSamplesRemaining, spill.fadeSamplesTotal);
        spill.fadeSamplesRemaining = juce::jmax(
            0, spill.fadeSamplesRemaining - main.getNumSamples());
        for (int channel = 0; channel < main.getNumChannels(); ++channel)
            main.addFrom(
                channel, 0, tail, channel, 0, main.getNumSamples());
        spill.samplesRemaining -= main.getNumSamples();
        if (spill.samplesRemaining <= 0
            || (spill.fadeSamplesTotal > 0
                && spill.fadeSamplesRemaining <= 0))
            spill = {};
    }

    auto remaining = 0;
    for (const auto& spill : spillSnapshots)
        if (spill.snapshot >= 0)
            remaining = juce::jmax(remaining, spill.samplesRemaining);
    if (snapshotTransition.outgoing >= 0)
        remaining = juce::jmax(
            remaining,
            juce::jmax(
                0,
                snapshotTransition.totalSamples
                    - snapshotTransition.samplesDone
                    - main.getNumSamples())
                + juce::roundToInt(
                    snapshotRacks[static_cast<size_t>(
                        snapshotTransition.outgoing)]
                        ->snapshotSpillSeconds()
                    * preparedSampleRate));
    snapshotTailSamples.store(remaining, std::memory_order_release);
}

void MegaDSPAudioProcessor::processLiveSafeSnapshotRange(
    juce::AudioBuffer<float>& main,
    const juce::AudioBuffer<float>* sidechain,
    double bpm)
{
    const auto active =
        activeAudioSnapshot.load(std::memory_order_acquire);
    const auto transitioning = snapshotTransition.outgoing >= 0;
    auto start = 1.0f;
    auto end = 1.0f;
    if (transitioning)
    {
        start = juce::jlimit(
            0.0f, 1.0f,
            static_cast<float>(snapshotTransition.samplesDone)
                / static_cast<float>(snapshotTransition.totalSamples));
        end = juce::jlimit(
            0.0f, 1.0f,
            static_cast<float>(
                snapshotTransition.samplesDone + main.getNumSamples())
                / static_cast<float>(
                    snapshotTransition.totalSamples));
    }

    auto processed = 0;
    for (int index = 0; index < megadsp::snapshotCount; ++index)
    {
        const auto& state =
            snapshotStates[static_cast<size_t>(index)];
        if (!state.populated.load(std::memory_order_acquire)
            || !state.ready.load(std::memory_order_acquire))
            continue;

        auto& storage =
            liveSafeSnapshotOutputs[static_cast<size_t>(index)];
        std::array<float*, 2> pointers {};
        for (int channel = 0; channel < main.getNumChannels(); ++channel)
            pointers[static_cast<size_t>(channel)] =
                storage.getWritePointer(channel);
        juce::AudioBuffer<float> output(
            pointers.data(), main.getNumChannels(), main.getNumSamples());

        const auto incoming =
            transitioning && index == snapshotTransition.incoming;
        const auto outgoing =
            transitioning && index == snapshotTransition.outgoing;
        const auto receivesLiveInput =
            transitioning ? incoming || outgoing : index == active;
        if (receivesLiveInput)
        {
            for (int channel = 0; channel < main.getNumChannels(); ++channel)
                output.copyFrom(
                    channel, 0, main, channel, 0,
                    main.getNumSamples());
            if (outgoing
                || (incoming
                    && !snapshotTransition.fadeIncomingOutput))
                applySnapshotTransitionWeight(
                    output, start, end, incoming);
        }
        else
        {
            output.clear();
        }

        processProfiledRack(
            index, output, receivesLiveInput ? sidechain : nullptr, bpm);
        if (incoming && snapshotTransition.fadeIncomingOutput)
            applySnapshotTransitionWeight(
                output, start, end, true);
        ++processed;
    }
    processedSnapshotGraphCount.store(
        processed, std::memory_order_release);

    main.clear();
    const auto addSnapshot = [this, &main](int index)
    {
        if (!juce::isPositiveAndBelow(index, megadsp::snapshotCount))
            return;
        const auto& output =
            liveSafeSnapshotOutputs[static_cast<size_t>(index)];
        for (int channel = 0; channel < main.getNumChannels(); ++channel)
            main.addFrom(
                channel, 0, output, channel, 0,
                main.getNumSamples());
    };
    if (transitioning)
    {
        addSnapshot(snapshotTransition.outgoing);
        addSnapshot(snapshotTransition.incoming);
    }
    else
    {
        addSnapshot(active);
    }
    mixLiveSafeSpills(main);

    if (transitioning)
    {
        snapshotTransition.samplesDone += main.getNumSamples();
        if (snapshotTransition.samplesDone
            >= snapshotTransition.totalSamples)
        {
            addSpillSnapshot(snapshotTransition.outgoing);
            snapshotTransition = {};
        }
    }
}

void MegaDSPAudioProcessor::processSnapshotRange(
    juce::AudioBuffer<float>& main,
    const juce::AudioBuffer<float>* sidechain,
    double bpm)
{
    if (snapshotTransition.outgoing >= 0)
    {
        const auto remaining = juce::jmax(
            1, snapshotTransition.totalSamples
                   - snapshotTransition.samplesDone);
        if (main.getNumSamples() > remaining)
        {
            for (int part = 0; part < 2; ++part)
            {
                const auto offset = part == 0 ? 0 : remaining;
                const auto samples = part == 0
                                         ? remaining
                                         : main.getNumSamples() - remaining;
                std::array<float*, 2> mainPointers {};
                for (int channel = 0;
                     channel < main.getNumChannels(); ++channel)
                    mainPointers[static_cast<size_t>(channel)] =
                        main.getWritePointer(channel, offset);
                juce::AudioBuffer<float> mainPart(
                    mainPointers.data(), main.getNumChannels(), samples);

                std::array<float*, 2> sidechainPointers {};
                std::optional<juce::AudioBuffer<float>> sidechainPart;
                if (sidechain != nullptr)
                {
                    for (int channel = 0;
                         channel < sidechain->getNumChannels(); ++channel)
                        sidechainPointers[static_cast<size_t>(channel)] =
                            const_cast<float*>(
                                sidechain->getReadPointer(channel, offset));
                    sidechainPart.emplace(
                        sidechainPointers.data(),
                        sidechain->getNumChannels(), samples);
                }
                processSnapshotRange(
                    mainPart,
                    sidechainPart.has_value() ? &*sidechainPart : nullptr,
                    bpm);
            }
            return;
        }
    }

    if (main.getNumSamples() > preparedMaximumBlockSize)
    {
        for (int offset = 0; offset < main.getNumSamples();
             offset += preparedMaximumBlockSize)
        {
            const auto samples = juce::jmin(
                preparedMaximumBlockSize,
                main.getNumSamples() - offset);
            std::array<float*, 2> mainPointers {};
            for (int channel = 0;
                 channel < main.getNumChannels(); ++channel)
                mainPointers[static_cast<size_t>(channel)] =
                    main.getWritePointer(channel, offset);
            juce::AudioBuffer<float> mainChunk(
                mainPointers.data(), main.getNumChannels(), samples);

            std::array<float*, 2> sidechainPointers {};
            std::optional<juce::AudioBuffer<float>> sidechainChunk;
            if (sidechain != nullptr)
            {
                for (int channel = 0;
                     channel < sidechain->getNumChannels(); ++channel)
                    sidechainPointers[static_cast<size_t>(channel)] =
                        const_cast<float*>(
                            sidechain->getReadPointer(channel, offset));
                sidechainChunk.emplace(
                    sidechainPointers.data(),
                    sidechain->getNumChannels(), samples);
            }
            processSnapshotRange(
                mainChunk,
                sidechainChunk.has_value() ? &*sidechainChunk : nullptr,
                bpm);
        }
        return;
    }

    if (liveSafeSnapshots.load(std::memory_order_acquire))
    {
        processLiveSafeSnapshotRange(main, sidechain, bpm);
        return;
    }

    processedSnapshotGraphCount.store(
        snapshotTransition.outgoing >= 0 ? 2 : 1,
        std::memory_order_release);
    if (snapshotTransition.outgoing < 0)
    {
        processProfiledRack(
            activeAudioSnapshot.load(std::memory_order_relaxed),
            main, sidechain, bpm);
        processSpillSnapshots(main, sidechain, bpm);
        return;
    }

    for (int channel = 0; channel < main.getNumChannels(); ++channel)
        snapshotScratch.copyFrom(
            channel, 0, main, channel, 0, main.getNumSamples());
    std::array<float*, 2> pointers {};
    for (int channel = 0; channel < main.getNumChannels(); ++channel)
        pointers[static_cast<size_t>(channel)] =
            snapshotScratch.getWritePointer(channel);
    juce::AudioBuffer<float> outgoing(
        pointers.data(), main.getNumChannels(), main.getNumSamples());

    const auto start = juce::jlimit(
        0.0f, 1.0f,
        static_cast<float>(snapshotTransition.samplesDone)
            / static_cast<float>(snapshotTransition.totalSamples));
    const auto end = juce::jlimit(
        0.0f, 1.0f,
        static_cast<float>(
            snapshotTransition.samplesDone + main.getNumSamples())
            / static_cast<float>(snapshotTransition.totalSamples));
    applySnapshotTransitionWeight(outgoing, start, end, false);
    if (!snapshotTransition.fadeIncomingOutput)
        applySnapshotTransitionWeight(main, start, end, true);
    processProfiledRack(
        snapshotTransition.outgoing, outgoing, sidechain, bpm);
    processProfiledRack(
        snapshotTransition.incoming, main, sidechain, bpm);
    if (snapshotTransition.fadeIncomingOutput)
        applySnapshotTransitionWeight(main, start, end, true);
    for (int channel = 0; channel < main.getNumChannels(); ++channel)
        main.addFrom(
            channel, 0, outgoing, channel, 0, main.getNumSamples());
    processSpillSnapshots(main, sidechain, bpm);

    snapshotTransition.samplesDone += main.getNumSamples();
    if (snapshotTransition.samplesDone
        >= snapshotTransition.totalSamples)
    {
        addSpillSnapshot(snapshotTransition.outgoing);
        snapshotTransition = {};
    }
}

bool MegaDSPAudioProcessor::beginTopologyMutation()
{
    auto expected = false;
    if (!topologyMutationInProgress.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
    {
        topologyDirty.store(true, std::memory_order_release);
        return false;
    }
    suspendProcessing(true);
    return true;
}

void MegaDSPAudioProcessor::finishTopologyMutation(
    bool compact, bool resetRack, bool publishAlways)
{
    auto& rack = getRack();
    const auto compacted = compact && rack.compactSlots();
    const auto modulesChanged = rack.synchronizeModules(false);
    if (resetRack || compacted || modulesChanged)
    {
        rack.reset();
        rack.visualizationData().clear();
    }
    selectedTab = juce::jlimit(
        0, juce::jmax(0, rack.activeSlotCount() - 1), selectedTab);
    selectedSnapshotState().populated.store(
        true, std::memory_order_release);
    selectedSnapshotState().ready.store(
        true, std::memory_order_release);
    if (compacted)
        publishSnapshotToParameters(getSelectedSnapshot());
    suspendProcessing(false);
    refreshLatency();
    if (publishAlways || compacted || modulesChanged)
        topologyGeneration.fetch_add(1, std::memory_order_release);
    const auto generation =
        topologyGeneration.load(std::memory_order_acquire);
    megadsp::Diagnostics::instance().logRealtime(
        megadsp::DiagnosticEvent::topologyChanged,
        diagnosticInstanceId, static_cast<std::int64_t>(generation),
        rack.activeSlotCount());
    juce::StringArray moduleNames;
    for (int slot = 0; slot < rack.activeSlotCount(); ++slot)
        moduleNames.add(
            megadsp::moduleDefinition(rack.moduleType(slot)).displayName);
    megadsp::Diagnostics::instance().log(
        "topology",
        "instance=" + juce::String(diagnosticInstanceId)
            + " generation=" + juce::String(generation)
            + " modules=[" + moduleNames.joinIntoString(", ") + "]");
    restorePendingImpulseResponses();
    restorePendingNamModels();
    refreshLatency();
    topologyMutationInProgress.store(false, std::memory_order_release);
    if (topologyDirty.load(std::memory_order_acquire))
        triggerAsyncUpdate();
}

void MegaDSPAudioProcessor::restorePendingImpulseResponses()
{
    auto& rack = getRack();
    for (int slot = 0; slot < megadsp::slotCount; ++slot)
    {
        for (int lane = 0; lane < 2; ++lane)
        {
            const auto path =
                rack.pendingImpulseResponsePath(slot, lane);
            if (path.isEmpty())
                continue;
            megadsp::EffectRack::PreparedImpulseResponseLoad load;
            const auto result = rack.prepareImpulseResponse(
                slot, lane, juce::File(path), load);
            if (result.failed())
            {
                rack.discardPendingImpulseResponse(slot, lane, path);
                continue;
            }
            suspendProcessing(true);
            const auto committed = rack.commitImpulseResponse(load);
            suspendProcessing(false);
            if (!committed)
                rack.cancelImpulseResponse(load);
        }
    }
}

void MegaDSPAudioProcessor::restorePendingNamModels()
{
    auto& rack = getRack();
    for (int slot = 0; slot < megadsp::slotCount; ++slot)
    {
        const auto path = rack.pendingNamModelPath(slot);
        if (path.isEmpty())
            continue;

        megadsp::EffectRack::PreparedNamModelLoad load;
        const auto result = rack.prepareNamModel(
            slot, juce::File(path), load);
        if (result.failed())
        {
            rack.discardPendingNamModel(slot, path);
            continue;
        }

        suspendProcessing(true);
        const auto committed = rack.commitNamModel(load);
        refreshLatency();
        suspendProcessing(false);
        if (!committed)
            rack.cancelNamModel(load);
    }
}

void MegaDSPAudioProcessor::handleAsyncUpdate()
{
    if (!topologyDirty.exchange(false, std::memory_order_acq_rel))
        return;
    const auto requestGeneration =
        topologyRequestGeneration.load(std::memory_order_acquire);
    auto snapshotMask = pendingTopologySnapshotMask.exchange(
        0, std::memory_order_acq_rel);
    if (snapshotMask == 0)
        snapshotMask =
            std::uint32_t { 1 }
            << static_cast<unsigned int>(getSelectedSnapshot());

    auto expected = false;
    if (!topologyMutationInProgress.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
    {
        pendingTopologySnapshotMask.fetch_or(
            snapshotMask, std::memory_order_release);
        topologyDirty.store(true, std::memory_order_release);
        return;
    }

    suspendProcessing(true);
    for (int index = 0; index < megadsp::snapshotCount; ++index)
    {
        const auto bit =
            std::uint32_t { 1 } << static_cast<unsigned int>(index);
        if ((snapshotMask & bit) == 0)
            continue;
        auto& rack = *snapshotRacks[static_cast<size_t>(index)];
        const auto compacted = rack.compactSlots();
        const auto changed = rack.synchronizeModules(false);
        if (compacted || changed)
        {
            rack.reset();
            rack.visualizationData().clear();
        }
        snapshotStates[static_cast<size_t>(index)]
            .populated.store(true, std::memory_order_release);
        snapshotStates[static_cast<size_t>(index)]
            .ready.store(true, std::memory_order_release);
        if (index == getSelectedSnapshot() && compacted)
            publishSnapshotToParameters(index);
    }
    suspendProcessing(false);
    topologyGeneration.fetch_add(1, std::memory_order_release);
    refreshLatency();
    topologyMutationInProgress.store(false, std::memory_order_release);

    if (topologyRequestGeneration.load(std::memory_order_acquire)
            != requestGeneration
        || pendingTopologySnapshotMask.load(std::memory_order_acquire) != 0)
    {
        topologyDirty.store(true, std::memory_order_release);
        triggerAsyncUpdate();
    }
}

void MegaDSPAudioProcessor::serializeSnapshots(
    juce::ValueTree& root) const
{
    if (const auto existing = root.getChildWithName("SNAPSHOTS");
        existing.isValid())
        root.removeChild(existing, nullptr);
    juce::ValueTree snapshots("SNAPSHOTS");
    snapshots.setProperty(
        "selected",
        selectedSnapshot.load(std::memory_order_acquire), nullptr);
    for (int index = 0; index < megadsp::snapshotCount; ++index)
        snapshots.addChild(
            snapshotStates[static_cast<size_t>(index)]
                .toValueTree(index),
            -1, nullptr);
    root.addChild(snapshots, -1, nullptr);
}

void MegaDSPAudioProcessor::restoreSnapshots(
    const juce::ValueTree& root, int sourceSchema)
{
    for (auto& state : snapshotStates)
        state.clear();

    auto selected = 0;
    const auto snapshots = root.getChildWithName("SNAPSHOTS");
    if (sourceSchema >= 10 && snapshots.isValid())
    {
        selected = juce::jlimit(
            0, megadsp::snapshotCount - 1,
            static_cast<int>(snapshots.getProperty("selected", 0)));
        for (const auto child : snapshots)
        {
            if (!child.hasType("SNAPSHOT"))
                continue;
            const auto index =
                static_cast<int>(child.getProperty("index", -1));
            if (juce::isPositiveAndBelow(
                    index, megadsp::snapshotCount))
            {
                snapshotStates[static_cast<size_t>(index)]
                    .fromValueTree(child);
                if (sourceSchema < 11)
                {
                    auto& state =
                        snapshotStates[static_cast<size_t>(index)];
                    for (auto& slot : state.slots)
                    {
                        const auto type = static_cast<megadsp::ModuleType>(
                            juce::roundToInt(slot.type.load(
                                std::memory_order_relaxed)));
                        if (type
                            == megadsp::ModuleType::algorithmicReverb)
                            slot.controls[7].store(
                                0.0f, std::memory_order_relaxed);
                        else if (type
                                 == megadsp::ModuleType::convolutionReverb)
                            slot.controls[5].store(
                                0.0f, std::memory_order_relaxed);
                    }
                }
                if (sourceSchema < 13)
                {
                    auto& state =
                        snapshotStates[static_cast<size_t>(index)];
                    for (auto& slot : state.slots)
                    {
                        if (static_cast<megadsp::ModuleType>(
                                juce::roundToInt(slot.type.load(
                                    std::memory_order_relaxed)))
                            != megadsp::ModuleType::stageVocal)
                            continue;
                        std::array<float, megadsp::controlsPerSlot>
                            legacy {};
                        for (int control = 0;
                             control < megadsp::controlsPerSlot; ++control)
                            legacy[static_cast<size_t>(control)] =
                                slot.controls[static_cast<size_t>(control)]
                                    .load(std::memory_order_relaxed);
                        const auto migrated =
                            megadsp::migrateStageVocalControlsFromSchema12(
                                legacy);
                        for (int control = 0;
                             control < megadsp::controlsPerSlot; ++control)
                            slot.controls[static_cast<size_t>(control)].store(
                                migrated[static_cast<size_t>(control)],
                                std::memory_order_relaxed);
                    }
                }
            }
        }
    }
    else
    {
        captureParametersToSnapshot(0);
        for (int slot = 0; slot < megadsp::slotCount; ++slot)
        {
            snapshotStates[0].slots[static_cast<size_t>(slot)]
                .impulseResponsePath =
                root.getProperty(
                    "impulseResponse" + juce::String(slot + 1)).toString();
            snapshotStates[0].slots[static_cast<size_t>(slot)]
                .convolutionImpulseBankPaths[0] =
                    snapshotStates[0].slots[static_cast<size_t>(slot)]
                        .impulseResponsePath;
            snapshotStates[0].slots[static_cast<size_t>(slot)]
                .impulseResponsePath2 =
                root.getProperty(
                    "impulseResponse2" + juce::String(slot + 1)).toString();
            snapshotStates[0].slots[static_cast<size_t>(slot)]
                .namModelPath =
                root.getProperty(
                    "neuralModel" + juce::String(slot + 1)).toString();
        }
        snapshotStates[0].populated.store(
            true, std::memory_order_release);
    }

    if (!snapshotStates[static_cast<size_t>(selected)]
             .populated.load(std::memory_order_acquire))
        selected = 0;
    for (int index = 0; index < megadsp::snapshotCount; ++index)
    {
        auto& state = snapshotStates[static_cast<size_t>(index)];
        if (state.populated.load(std::memory_order_acquire))
        {
            prepareSnapshot(index);
        }
        else
        {
            snapshotRacks[static_cast<size_t>(index)]
                ->rebuildModules();
            state.ready.store(false, std::memory_order_release);
        }
    }
    selectedSnapshot.store(selected, std::memory_order_release);
    activeAudioSnapshot.store(selected, std::memory_order_release);
    snapshotTransition = {};
    for (auto& spill : spillSnapshots)
        spill = {};
    snapshotTailSamples.store(0, std::memory_order_release);
    publishSnapshotToParameters(selected, false);
    snapshotGeneration.fetch_add(1, std::memory_order_release);
}

void MegaDSPAudioProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    auto state = parameters.copyState();
    for (auto child : state)
    {
        if (auto* parameter = parameters.getParameter(
                child.getProperty("id").toString());
            dynamic_cast<juce::AudioParameterBool*>(parameter) != nullptr)
        {
            child.setProperty("value", parameter->getValue(), nullptr);
        }
    }
    state.setProperty("schemaVersion", megadsp::stateSchemaVersion, nullptr);
    state.setProperty("selectedTab", selectedTab, nullptr);
    state.setProperty("editorWidth", editorWidth, nullptr);
    state.setProperty("editorHeight", editorHeight, nullptr);
    serializeSnapshots(state);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destination);
}

void MegaDSPAudioProcessor::setStateInformation(const void* data, int size)
{
    if (!restoringUndoState)
    {
        undoManager.clearUndoHistory();
        undoTransactionDepth = 0;
        pendingUndoBefore.reset();
        pendingUndoName.clear();
    }
    if (auto xml = getXmlFromBinary(data, size))
    {
        if (!xml->hasTagName(parameters.state.getType()))
        {
            megadsp::Diagnostics::instance().logRealtime(
                megadsp::DiagnosticEvent::stateRejected,
                diagnosticInstanceId, 1, size);
            return;
        }
        auto restored = juce::ValueTree::fromXml(*xml);
        const auto schemaVersion = static_cast<int>(
            restored.getProperty("schemaVersion", 0));
        if (schemaVersion < 2
            || schemaVersion > megadsp::stateSchemaVersion)
        {
            megadsp::Diagnostics::instance().logRealtime(
                megadsp::DiagnosticEvent::stateRejected,
                diagnosticInstanceId, schemaVersion, size);
            return;
        }
        restored = megadsp::migrateStateToCurrentSchema(restored);
        editorWidth = juce::jlimit(
            megadsp::ui::editorMinimumWidth,
            megadsp::ui::editorStateMaximumWidth,
            static_cast<int>(
                restored.getProperty("editorWidth", 1100)));
        editorHeight = juce::jlimit(
            megadsp::ui::editorMinimumHeight,
            megadsp::ui::editorStateMaximumHeight,
            static_cast<int>(
                restored.getProperty("editorHeight", 720)));
        selectedTab = static_cast<int>(
            restored.getProperty("selectedTab", 0));
        if (!beginTopologyMutation())
            return;
        restoreParameterState(restored);
        restoreSnapshots(restored, schemaVersion);
        finishTopologyMutation(true, true, true);
        setSelectedTab(selectedTab);
        megadsp::Diagnostics::instance().log(
            "state",
            "restored instance=" + juce::String(diagnosticInstanceId)
                + " schema=" + juce::String(schemaVersion));
    }
}

void MegaDSPAudioProcessor::moveSlot(int source, int destination)
{
    if (source == destination)
        return;
    ScopedProcessorUndoTransaction undo(*this, "Reorder module");
    if (!beginTopologyMutation())
        return;
    getRack().moveSlot(source, destination);
    publishSnapshotToParameters(getSelectedSnapshot());
    finishTopologyMutation(false, true, true);
}

void MegaDSPAudioProcessor::clearSlot(int slot)
{
    ScopedProcessorUndoTransaction undo(*this, "Clear module");
    if (!beginTopologyMutation())
        return;
    getRack().clearSlot(slot);
    publishSnapshotToParameters(getSelectedSnapshot());
    finishTopologyMutation(true, true, true);
}

void MegaDSPAudioProcessor::removeSlot(int slot)
{
    if (!juce::isPositiveAndBelow(slot, getRack().activeSlotCount()))
        return;
    ScopedProcessorUndoTransaction undo(*this, "Remove module");
    if (!beginTopologyMutation())
        return;
    getRack().removeSlot(slot);
    publishSnapshotToParameters(getSelectedSnapshot());
    finishTopologyMutation(false, true, true);
}

void MegaDSPAudioProcessor::addModule(megadsp::ModuleType type)
{
    const auto slot = getRack().activeSlotCount();
    if (!juce::isPositiveAndBelow(slot, megadsp::slotCount)
        || type == megadsp::ModuleType::empty
        || megadsp::findModuleDefinition(type) == nullptr)
        return;
    ScopedProcessorUndoTransaction undo(*this, "Add module");
    if (!beginTopologyMutation())
        return;
    getRack().clearSlot(slot);
    const auto defaults = megadsp::moduleDefaults(type);
    for (int control = 0; control < megadsp::controlsPerSlot; ++control)
        if (auto* parameter = parameters.getParameter(
                megadsp::controlParameterId(slot, control)))
            parameter->setValueNotifyingHost(
                parameter->convertTo0to1(defaults[static_cast<size_t>(control)]));
    if (auto* parameter = parameters.getParameter(
            megadsp::slotParameterId(slot, "type")))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(
            parameter->convertTo0to1(static_cast<float>(type)));
        parameter->endChangeGesture();
    }
    finishTopologyMutation(false, true, true);
    selectedTab = slot;
}

void MegaDSPAudioProcessor::resetModuleToDefaults(int slot)
{
    if (!juce::isPositiveAndBelow(slot, megadsp::slotCount))
        return;
    const auto type = getRack().moduleType(slot);
    if (type == megadsp::ModuleType::empty)
        return;

    ScopedProcessorUndoTransaction undo(*this, "Reset module");
    suspendProcessing(true);
    const auto defaults = megadsp::moduleDefaults(type);
    for (int control = 0; control < megadsp::controlsPerSlot; ++control)
    {
        if (auto* parameter = parameters.getParameter(
                megadsp::controlParameterId(slot, control)))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(
                parameter->convertTo0to1(defaults[static_cast<size_t>(control)]));
            parameter->endChangeGesture();
        }
    }
    getRack().reset();
    getRack().visualizationData().slotData(slot).clear();
    suspendProcessing(false);
    refreshLatency();
}

juce::Result MegaDSPAudioProcessor::loadImpulseResponse(
    int slot, const juce::File& file)
{
    return loadImpulseResponse(slot, 0, file);
}

juce::Result MegaDSPAudioProcessor::loadImpulseResponse(
    int slot, int lane, const juce::File& file)
{
    ScopedProcessorUndoTransaction undo(*this, "Load impulse response");
    if (lane == 0
        && getRack().moduleType(slot)
               == megadsp::ModuleType::convolutionReverb)
        return loadConvolutionImpulseResponseBank(
            slot,
            getRack().activeConvolutionImpulseResponseBank(slot),
            file);

    auto expected = false;
    if (!topologyMutationInProgress.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return juce::Result::fail(
            "A topology update is already in progress.");
    const auto finishDataMutation = [this]
    {
        topologyMutationInProgress.store(
            false, std::memory_order_release);
        if (topologyDirty.load(std::memory_order_acquire))
            triggerAsyncUpdate();
    };

    megadsp::EffectRack::PreparedImpulseResponseLoad load;
    auto result =
        getRack().prepareImpulseResponse(slot, lane, file, load);
    if (result.failed())
    {
        megadsp::Diagnostics::instance().log(
            "ir-failed",
            "instance=" + juce::String(diagnosticInstanceId)
                + " slot=" + juce::String(slot)
                + " file=" + file.getFileName()
                + " error=load-rejected");
        finishDataMutation();
        return result;
    }

    suspendProcessing(true);
    const auto committed = getRack().commitImpulseResponse(load);
    suspendProcessing(false);
    if (!committed)
    {
        getRack().cancelImpulseResponse(load);
        finishDataMutation();
        return juce::Result::fail(
            "The impulse response request was superseded.");
    }

    finishDataMutation();
    if (parameters.state.getProperty(
            "impulseResponseRootDirectory").toString().isEmpty())
    {
        parameters.state.setProperty(
            "impulseResponseRootDirectory",
            file.getParentDirectory().getFullPathName(), nullptr);
        parameters.state.setProperty(
            "impulseResponseDirectory",
            file.getParentDirectory().getFullPathName(), nullptr);
        const juce::ScopedLock lock(impulseResponseLibraryLock);
        indexedImpulseResponseRoot.clear();
        const auto persisted =
            persistGlobalImpulseResponseRoot(
                file.getParentDirectory());
        if (persisted.failed())
            megadsp::Diagnostics::instance().log(
                "ir-root-persist-failed",
                persisted.getErrorMessage());
    }
    publishSnapshotToParameters(getSelectedSnapshot());
    updateHostDisplay(
        ChangeDetails{}.withNonParameterStateChanged(true));
    return juce::Result::ok();
}

juce::Result MegaDSPAudioProcessor::loadImpulseResponseAsync(
    int slot, int lane, const juce::File& file,
    AssetLoadCallback callback)
{
    if (lane == 0
        && getRack().moduleType(slot)
               == megadsp::ModuleType::convolutionReverb)
        return loadConvolutionImpulseResponseBankAsync(
            slot, getRack().activeConvolutionImpulseResponseBank(slot),
            file, std::move(callback));

    auto expected = false;
    if (!topologyMutationInProgress.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return juce::Result::fail(
            "An asset or topology update is already in progress.");
    const auto snapshot = getSelectedSnapshot();
    auto* rack = snapshotRacks[static_cast<size_t>(snapshot)].get();
    impulseResponseLoadPool.addJob(std::function<void()> {
        [this, rack, snapshot, slot, lane, file,
         completionCallback = std::move(callback)]() mutable
        {
            auto completed =
                std::make_unique<PendingImpulseResponseLoad>();
            completed->rack = rack;
            completed->snapshot = snapshot;
            completed->callback = std::move(completionCallback);
            completed->result = rack->prepareImpulseResponse(
                slot, lane, file, completed->load);
            const juce::ScopedLock lock(
                pendingImpulseResponseLoadLock);
            pendingImpulseResponseLoad = std::move(completed);
        } });
    return juce::Result::ok();
}

juce::Result
MegaDSPAudioProcessor::loadConvolutionImpulseResponseBankAsync(
    int slot, int bank, const juce::File& file,
    AssetLoadCallback callback)
{
    auto expected = false;
    if (!topologyMutationInProgress.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return juce::Result::fail(
            "An asset or topology update is already in progress.");
    const auto snapshot = getSelectedSnapshot();
    auto* rack = snapshotRacks[static_cast<size_t>(snapshot)].get();
    impulseResponseLoadPool.addJob(std::function<void()> {
        [this, rack, snapshot, slot, bank, file,
         completionCallback = std::move(callback)]() mutable
        {
            auto completed =
                std::make_unique<PendingImpulseResponseLoad>();
            completed->rack = rack;
            completed->snapshot = snapshot;
            completed->callback = std::move(completionCallback);
            completed->selectConvolutionBank = true;
            completed->result =
                rack->prepareConvolutionImpulseResponseBank(
                    slot, bank, file, completed->load);
            const juce::ScopedLock lock(
                pendingImpulseResponseLoadLock);
            pendingImpulseResponseLoad = std::move(completed);
        } });
    return juce::Result::ok();
}

void MegaDSPAudioProcessor::finishPendingImpulseResponseLoad()
{
    std::unique_ptr<PendingImpulseResponseLoad> completed;
    {
        const juce::ScopedLock lock(pendingImpulseResponseLoadLock);
        completed = std::move(pendingImpulseResponseLoad);
    }
    if (completed == nullptr)
        return;

    auto result = completed->result;
    if (result.wasOk())
    {
        ScopedProcessorUndoTransaction undo(
            *this, "Load impulse response");
        suspendProcessing(true);
        auto committed = completed->selectConvolutionBank
            ? completed->rack->commitConvolutionImpulseResponseBank(
                  completed->load)
            : completed->rack->commitImpulseResponse(completed->load);
        if (committed && completed->selectConvolutionBank)
            committed =
                completed->rack->selectConvolutionImpulseResponseBank(
                    completed->load.slot,
                    completed->load.convolutionBank);
        refreshLatency();
        suspendProcessing(false);
        if (!committed)
        {
            completed->rack->cancelImpulseResponse(completed->load);
            result = juce::Result::fail(
                "Impulse-response commit superseded.");
        }
        else
        {
            const auto file = juce::File(completed->load.path);
            if (parameters.state.getProperty(
                    "impulseResponseRootDirectory").toString().isEmpty())
            {
                parameters.state.setProperty(
                    "impulseResponseRootDirectory",
                    file.getParentDirectory().getFullPathName(), nullptr);
                parameters.state.setProperty(
                    "impulseResponseDirectory",
                    file.getParentDirectory().getFullPathName(), nullptr);
                const juce::ScopedLock lock(impulseResponseLibraryLock);
                indexedImpulseResponseRoot.clear();
                const auto persisted =
                    persistGlobalImpulseResponseRoot(
                        file.getParentDirectory());
                if (persisted.failed())
                    megadsp::Diagnostics::instance().log(
                        "ir-root-persist-failed",
                        persisted.getErrorMessage());
            }
            if (completed->snapshot == getSelectedSnapshot())
                publishSnapshotToParameters(completed->snapshot);
            updateHostDisplay(
                ChangeDetails{}.withNonParameterStateChanged(true));
        }
    }
    topologyMutationInProgress.store(false, std::memory_order_release);
    if (topologyDirty.load(std::memory_order_acquire))
        triggerAsyncUpdate();
    if (completed->callback)
        completed->callback(result);
}

juce::Result
MegaDSPAudioProcessor::loadConvolutionImpulseResponseBank(
    int slot, int bank, const juce::File& file)
{
    ScopedProcessorUndoTransaction undo(
        *this, "Load convolution impulse response");
    auto expected = false;
    if (!topologyMutationInProgress.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return juce::Result::fail(
            "A topology update is already in progress.");
    const auto finishDataMutation = [this]
    {
        topologyMutationInProgress.store(
            false, std::memory_order_release);
        if (topologyDirty.load(std::memory_order_acquire))
            triggerAsyncUpdate();
    };

    megadsp::EffectRack::PreparedImpulseResponseLoad load;
    auto result = getRack().prepareConvolutionImpulseResponseBank(
        slot, bank, file, load);
    if (result.failed())
    {
        finishDataMutation();
        return result;
    }

    suspendProcessing(true);
    const auto committed =
        getRack().commitConvolutionImpulseResponseBank(load);
    const auto selected = committed
        && getRack().selectConvolutionImpulseResponseBank(slot, bank);
    suspendProcessing(false);
    if (!selected)
    {
        getRack().cancelImpulseResponse(load);
        finishDataMutation();
        return juce::Result::fail(
            "The impulse response request was superseded.");
    }
    finishDataMutation();

    if (parameters.state.getProperty(
            "impulseResponseRootDirectory").toString().isEmpty())
    {
        parameters.state.setProperty(
            "impulseResponseRootDirectory",
            file.getParentDirectory().getFullPathName(), nullptr);
        parameters.state.setProperty(
            "impulseResponseDirectory",
            file.getParentDirectory().getFullPathName(), nullptr);
        const juce::ScopedLock lock(impulseResponseLibraryLock);
        indexedImpulseResponseRoot.clear();
        const auto persisted =
            persistGlobalImpulseResponseRoot(
                file.getParentDirectory());
        if (persisted.failed())
            megadsp::Diagnostics::instance().log(
                "ir-root-persist-failed",
                persisted.getErrorMessage());
    }
    publishSnapshotToParameters(getSelectedSnapshot());
    updateHostDisplay(
        ChangeDetails{}.withNonParameterStateChanged(true));
    return juce::Result::ok();
}

bool MegaDSPAudioProcessor::selectConvolutionImpulseResponseBank(
    int slot, int bank)
{
    ScopedProcessorUndoTransaction undo(*this, "Select impulse bank");
    suspendProcessing(true);
    const auto selected =
        getRack().selectConvolutionImpulseResponseBank(slot, bank);
    suspendProcessing(false);
    if (selected)
    {
        publishSnapshotToParameters(getSelectedSnapshot());
        updateHostDisplay(
            ChangeDetails{}.withNonParameterStateChanged(true));
    }
    return selected;
}

int MegaDSPAudioProcessor::activeConvolutionImpulseResponseBank(
    int slot) const
{
    return getRack().activeConvolutionImpulseResponseBank(slot);
}

juce::String
MegaDSPAudioProcessor::convolutionImpulseResponseBankPath(
    int slot, int bank) const
{
    return getRack().convolutionImpulseResponseBankPath(slot, bank);
}

juce::String
MegaDSPAudioProcessor::convolutionImpulseResponseBankName(
    int slot, int bank) const
{
    return getRack().convolutionImpulseResponseBankName(slot, bank);
}

juce::Result MegaDSPAudioProcessor::setConvolutionWetBufferSamples(
    int slot, int samples)
{
    if (std::find(
            megadsp::ConvolutionReverbModule::
                wetProcessingLatencyChoices.begin(),
            megadsp::ConvolutionReverbModule::
                wetProcessingLatencyChoices.end(),
            samples)
        == megadsp::ConvolutionReverbModule::
               wetProcessingLatencyChoices.end())
        return juce::Result::fail(
            "Unsupported convolution wet buffer size.");

    ScopedProcessorUndoTransaction undo(
        *this, "Change convolution buffer");
    auto expected = false;
    if (!topologyMutationInProgress.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return juce::Result::fail(
            "A topology update is already in progress.");
    const auto finishDataMutation = [this]
    {
        topologyMutationInProgress.store(
            false, std::memory_order_release);
        if (topologyDirty.load(std::memory_order_acquire))
            triggerAsyncUpdate();
    };

    auto& rack = getRack();
    if (!rack.setConvolutionWetBufferSamples(slot, samples))
    {
        finishDataMutation();
        return juce::Result::fail(
            "The selected slot is not Convolution Reverb.");
    }

    for (int bank = 0;
         bank < megadsp::ConvolutionReverbModule::impulseBankCount;
         ++bank)
    {
        const auto path =
            rack.convolutionImpulseResponseBankPath(slot, bank);
        if (path.isEmpty())
            continue;
        megadsp::EffectRack::PreparedImpulseResponseLoad load;
        auto result = rack.prepareConvolutionImpulseResponseBank(
            slot, bank, juce::File(path), load);
        if (result.failed())
        {
            finishDataMutation();
            return result;
        }
        suspendProcessing(true);
        const auto committed =
            rack.commitConvolutionImpulseResponseBank(load);
        suspendProcessing(false);
        if (!committed)
        {
            rack.cancelImpulseResponse(load);
            finishDataMutation();
            return juce::Result::fail(
                "The wet-buffer update was superseded.");
        }
    }

    finishDataMutation();
    publishSnapshotToParameters(getSelectedSnapshot());
    updateHostDisplay(
        ChangeDetails{}.withNonParameterStateChanged(true));
    return juce::Result::ok();
}

int MegaDSPAudioProcessor::convolutionWetBufferSamples(
    int slot) const
{
    return getRack().convolutionWetBufferSamples(slot);
}

juce::Result MegaDSPAudioProcessor::loadBuiltInImpulseResponse(
    int slot, int index)
{
    auto expected = false;
    if (!topologyMutationInProgress.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return juce::Result::fail(
            "A topology update is already in progress.");
    megadsp::EffectRack::PreparedImpulseResponseLoad load;
    auto result =
        getRack().prepareBuiltInImpulseResponse(slot, index, load);
    if (result.failed())
    {
        topologyMutationInProgress.store(false, std::memory_order_release);
        return result;
    }
    suspendProcessing(true);
    const auto committed = getRack().commitImpulseResponse(load);
    suspendProcessing(false);
    if (!committed)
    {
        getRack().cancelImpulseResponse(load);
        topologyMutationInProgress.store(false, std::memory_order_release);
        return juce::Result::fail(
            "The cabinet impulse request was superseded.");
    }
    topologyMutationInProgress.store(false, std::memory_order_release);
    publishSnapshotToParameters(getSelectedSnapshot());
    updateHostDisplay(
        ChangeDetails{}.withNonParameterStateChanged(true));
    return juce::Result::ok();
}

juce::File MegaDSPAudioProcessor::impulseResponseDirectory(int slot) const
{
    const auto root = impulseResponseRootDirectory();
    if (root.isDirectory())
        return root;

    const auto currentPath = impulseResponsePath(slot);
    if (currentPath.isNotEmpty())
    {
        const auto currentDirectory =
            juce::File(currentPath).getParentDirectory();
        if (currentDirectory.isDirectory())
            return currentDirectory;
    }

    const auto persistedDirectory = juce::File(
        parameters.state.getProperty(
            "impulseResponseDirectory").toString());
    if (persistedDirectory.isDirectory())
        return persistedDirectory;
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory);
}

juce::File MegaDSPAudioProcessor::impulseResponseRootDirectory() const
{
    auto path = parameters.state.getProperty(
        "impulseResponseRootDirectory").toString();
    if (path.isEmpty())
        path = parameters.state.getProperty(
            "impulseResponseDirectory").toString();
    auto root = juce::File(path);
    if (root.isDirectory())
        return root;
    root = juce::File(globalImpulseResponseRoot());
    return root.isDirectory() ? root : juce::File();
}

juce::Result MegaDSPAudioProcessor::setImpulseResponseRootDirectory(
    const juce::File& directory)
{
    if (!directory.isDirectory())
        return juce::Result::fail(
            "Choose an existing impulse response directory.");
    libraryIndexPool.removeAllJobs(true, 30000);
    releaseAutomaticLibraryClaims();
    impulseResponseIndexInProgress.store(
        false, std::memory_order_release);
    namModelIndexInProgress.store(false, std::memory_order_release);
    parameters.state.setProperty(
        "impulseResponseRootDirectory",
        directory.getFullPathName(), nullptr);
    parameters.state.setProperty(
        "impulseResponseDirectory",
        directory.getFullPathName(), nullptr);
    {
        const juce::ScopedLock lock(impulseResponseLibraryLock);
        indexedImpulseResponseRoot.clear();
        impulseResponseLibrary.clear();
        impulseResponseLibraryValidated.store(
            false, std::memory_order_release);
    }
    const auto result = rebuildImpulseResponseLibrary();
    if (result.wasOk())
    {
        const auto persisted =
            persistGlobalImpulseResponseRoot(directory);
        if (persisted.failed())
            return persisted;
    }
    automaticLibraryCoordinator().requestScan();
    updateHostDisplay(
        ChangeDetails{}.withNonParameterStateChanged(true));
    return result;
}

juce::Result MegaDSPAudioProcessor::rebuildImpulseResponseLibrary()
{
    const auto root = impulseResponseRootDirectory();
    if (!root.isDirectory())
        return juce::Result::fail(
            "Choose an impulse response root directory first.");

    juce::Array<juce::File> files;
    root.findChildFiles(files, juce::File::findFiles, true);
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::vector<ImpulseResponseLibraryEntry> rebuilt;
    rebuilt.reserve(static_cast<size_t>(files.size()));
    for (const auto& file : files)
    {
        const auto extension = file.getFileExtension().toLowerCase();
        if (extension != ".wav" && extension != ".aif"
            && extension != ".aiff" && extension != ".flac")
            continue;
        std::unique_ptr<juce::AudioFormatReader> reader(
            formats.createReaderFor(file));
        if (reader == nullptr || reader->lengthInSamples <= 0
            || reader->sampleRate <= 0.0 || reader->numChannels == 0)
            continue;
        ImpulseResponseLibraryEntry entry;
        entry.file = file;
        entry.relativePath =
            file.getRelativePathFrom(root).replaceCharacter('\\', '/');
        entry.sampleRate = reader->sampleRate;
        entry.durationSeconds =
            static_cast<double>(reader->lengthInSamples)
            / reader->sampleRate;
        entry.channelCount = static_cast<int>(reader->numChannels);
        entry.searchableText = impulseResponseSearchableText(entry);
        rebuilt.push_back(std::move(entry));
    }
    std::sort(
        rebuilt.begin(), rebuilt.end(),
        [](const auto& left, const auto& right)
        {
            const auto order = left.relativePath.compareIgnoreCase(
                right.relativePath);
            return order != 0
                ? order < 0
                : left.file.getFullPathName()
                      < right.file.getFullPathName();
        });
    {
        const juce::ScopedLock lock(impulseResponseLibraryLock);
        impulseResponseLibrary = std::move(rebuilt);
        indexedImpulseResponseRoot = root.getFullPathName();
        impulseResponseLibraryValidated.store(
            true, std::memory_order_release);
    }
    std::vector<ImpulseResponseLibraryEntry> cached;
    {
        const juce::ScopedLock lock(impulseResponseLibraryLock);
        cached = impulseResponseLibrary;
    }
    const auto result =
        persistImpulseResponseLibraryCache(root, cached);
    automaticLibraryCoordinator().publishImpulse(root, cached);
    libraryGeneration.fetch_add(1, std::memory_order_release);
    return result;
}

void MegaDSPAudioProcessor::ensureImpulseResponseLibrary()
{
    const auto root =
        impulseResponseRootDirectory().getFullPathName();
    if (root.isEmpty())
    {
        const juce::ScopedLock lock(impulseResponseLibraryLock);
        indexedImpulseResponseRoot.clear();
        impulseResponseLibrary.clear();
        impulseResponseLibraryValidated.store(
            false, std::memory_order_release);
        return;
    }
    {
        const juce::ScopedLock lock(impulseResponseLibraryLock);
        if (root.isNotEmpty() && root == indexedImpulseResponseRoot)
            return;
    }
    std::vector<ImpulseResponseLibraryEntry> cached;
    const auto rootDirectory = juce::File(root);
    if (restoreImpulseResponseLibraryCache(rootDirectory, cached))
    {
        const juce::ScopedLock lock(impulseResponseLibraryLock);
        impulseResponseLibrary = std::move(cached);
        indexedImpulseResponseRoot = root;
        impulseResponseLibraryValidated.store(
            true, std::memory_order_release);
        libraryGeneration.fetch_add(1, std::memory_order_release);
        automaticLibraryCoordinator().publishImpulse(
            rootDirectory, impulseResponseLibrary);
        return;
    }
    auto discovered = discoverImpulseResponsePaths(rootDirectory);
    {
        const juce::ScopedLock lock(impulseResponseLibraryLock);
        impulseResponseLibrary = std::move(discovered);
        indexedImpulseResponseRoot = root;
        impulseResponseLibraryValidated.store(
            false, std::memory_order_release);
    }
    libraryGeneration.fetch_add(1, std::memory_order_release);
    scheduleImpulseResponseLibraryRebuild();
}

void MegaDSPAudioProcessor::releaseAutomaticLibraryClaims()
{
    auto& coordinator = automaticLibraryCoordinator();
    if (automaticImpulseRebuildOwned.exchange(
            false, std::memory_order_acq_rel))
        coordinator.releaseImpulseRebuild();
    if (automaticNamRebuildOwned.exchange(
            false, std::memory_order_acq_rel))
        coordinator.releaseNamRebuild();
}

void MegaDSPAudioProcessor::pollAutomaticLibraryUpdates()
{
    auto& coordinator = automaticLibraryCoordinator();
    auto impulseRootPath = parameters.state.getProperty(
        "impulseResponseRootDirectory").toString();
    if (impulseRootPath.isEmpty())
        impulseRootPath = coordinator.monitoredImpulseRoot();
    const auto impulseRoot = juce::File(impulseRootPath);
    auto namRootPath = parameters.state.getProperty(
        "namModelRootDirectory").toString();
    if (namRootPath.isEmpty())
        namRootPath = coordinator.monitoredNamRoot();
    const auto namRoot = juce::File(namRootPath);

    if (!impulseResponseIndexInProgress.load(std::memory_order_acquire))
    {
        const auto shared = coordinator.impulseCatalogSnapshot(
            impulseRoot, observedSharedImpulseResponseGeneration);
        if (shared != nullptr)
        {
            auto expected = false;
            if (impulseResponseIndexInProgress.compare_exchange_strong(
                    expected, true, std::memory_order_acq_rel))
                libraryIndexPool.addJob(std::function<void()> {
                    [this, impulseRoot, shared]
                    {
                        {
                            const juce::ScopedLock lock(
                                impulseResponseLibraryLock);
                            impulseResponseLibrary = *shared;
                            indexedImpulseResponseRoot =
                                impulseRoot.getFullPathName();
                            impulseResponseLibraryValidated.store(
                                true, std::memory_order_release);
                        }
                        libraryGeneration.fetch_add(
                            1, std::memory_order_release);
                        impulseResponseIndexInProgress.store(
                            false, std::memory_order_release);
                    } });
        }
    }
    if (!namModelIndexInProgress.load(std::memory_order_acquire))
    {
        const auto shared = coordinator.namCatalogSnapshot(
            namRoot, observedSharedNamModelGeneration);
        if (shared != nullptr)
        {
            auto expected = false;
            if (namModelIndexInProgress.compare_exchange_strong(
                    expected, true, std::memory_order_acq_rel))
                libraryIndexPool.addJob(std::function<void()> {
                    [this, namRoot, shared]
                    {
                        {
                            const juce::ScopedLock lock(namModelLibraryLock);
                            namModelLibrary = *shared;
                            indexedNamModelRoot =
                                namRoot.getFullPathName();
                            namModelLibraryValidated.store(
                                true, std::memory_order_release);
                        }
                        libraryGeneration.fetch_add(
                            1, std::memory_order_release);
                        namModelIndexInProgress.store(
                            false, std::memory_order_release);
                    } });
        }
    }

    const auto globalImpulseRoot = coordinator.monitoredImpulseRoot();
    const auto impulseRequest = coordinator.requestedImpulseGeneration();
    if (impulseRoot.getFullPathName() == globalImpulseRoot
        && impulseRequest > coordinator.completedImpulseGeneration())
    {
        auto expected = false;
        if (impulseResponseIndexInProgress.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel))
        {
            if (coordinator.claimImpulseRebuild())
            {
                automaticImpulseRebuildOwned.store(
                    true, std::memory_order_release);
                libraryIndexPool.addJob(std::function<void()> {
                    [this, impulseRequest]
                    {
                        const auto result = rebuildImpulseResponseLibrary();
                        if (result.failed())
                            megadsp::Diagnostics::instance().log(
                                "ir-auto-index-failed",
                                result.getErrorMessage());
                        automaticImpulseRebuildOwned.store(
                            false, std::memory_order_release);
                        automaticLibraryCoordinator()
                            .finishImpulseRebuild(impulseRequest);
                        impulseResponseIndexInProgress.store(
                            false, std::memory_order_release);
                    } });
            }
            else
            {
                impulseResponseIndexInProgress.store(
                    false, std::memory_order_release);
            }
        }
    }

    const auto globalNamRoot = coordinator.monitoredNamRoot();
    const auto namRequest = coordinator.requestedNamGeneration();
    if (namRoot.getFullPathName() == globalNamRoot
        && namRequest > coordinator.completedNamGeneration())
    {
        auto expected = false;
        if (namModelIndexInProgress.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel))
        {
            if (coordinator.claimNamRebuild())
            {
                automaticNamRebuildOwned.store(
                    true, std::memory_order_release);
                libraryIndexPool.addJob(std::function<void()> {
                    [this, namRequest]
                    {
                        const auto result = rebuildNamModelLibrary();
                        if (result.failed())
                            megadsp::Diagnostics::instance().log(
                                "nam-auto-index-failed",
                                result.getErrorMessage());
                        automaticNamRebuildOwned.store(
                            false, std::memory_order_release);
                        automaticLibraryCoordinator()
                            .finishNamRebuild(namRequest);
                        namModelIndexInProgress.store(
                            false, std::memory_order_release);
                    } });
            }
            else
            {
                namModelIndexInProgress.store(
                    false, std::memory_order_release);
            }
        }
    }
}

void MegaDSPAudioProcessor::scheduleImpulseResponseLibraryRebuild()
{
    auto expected = false;
    if (!impulseResponseIndexInProgress.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return;
    libraryIndexPool.addJob(std::function<void()> {
        [this]
        {
            const auto result = rebuildImpulseResponseLibrary();
            if (result.failed())
                megadsp::Diagnostics::instance().log(
                    "ir-index-failed", result.getErrorMessage());
            impulseResponseIndexInProgress.store(
                false, std::memory_order_release);
        } });
}

std::vector<ImpulseResponseLibraryEntry>
MegaDSPAudioProcessor::searchImpulseResponses(
    const juce::String& query, int maximumResults)
{
    ensureImpulseResponseLibrary();
    maximumResults = juce::jmax(0, maximumResults);
    if (maximumResults == 0)
        return {};
    const auto terms = parseNamSearchQuery(query);
    struct ScoredEntry
    {
        ImpulseResponseLibraryEntry entry;
        int score = 0;
    };
    std::vector<ScoredEntry> scoredMatches;
    const auto favoritePaths = globalImpulseResponseFavorites();
    const auto metadataAvailable =
        impulseResponseLibraryValidated.load(std::memory_order_acquire);
    {
        const juce::ScopedLock lock(impulseResponseLibraryLock);
        scoredMatches.reserve(impulseResponseLibrary.size());
        for (const auto& entry : impulseResponseLibrary)
        {
            const auto favorite = favoritePaths.contains(
                entry.file.getFullPathName());
            auto matched = true;
            auto score = 0;
            for (const auto& term : terms)
            {
                if (term.text.isEmpty())
                    continue;
                const auto termScore = impulseSearchTermScore(
                    entry, favorite, term, metadataAvailable);
                matched = term.excluded
                    ? termScore == 0 : termScore > 0;
                if (!matched)
                    break;
                if (!term.excluded)
                    score += termScore;
            }
            if (!matched)
                continue;
            scoredMatches.push_back({ entry, score });
            scoredMatches.back().entry.favorite = favorite;
        }
    }
    std::stable_sort(
        scoredMatches.begin(), scoredMatches.end(),
        [](const auto& left, const auto& right)
        {
            if (left.score != right.score)
                return left.score > right.score;
            return left.entry.relativePath.compareIgnoreCase(
                       right.entry.relativePath) < 0;
        });
    if (static_cast<int>(scoredMatches.size()) > maximumResults)
        scoredMatches.resize(static_cast<size_t>(maximumResults));
    std::vector<ImpulseResponseLibraryEntry> matches;
    matches.reserve(scoredMatches.size());
    for (auto& match : scoredMatches)
        matches.push_back(std::move(match.entry));
    return matches;
}

int MegaDSPAudioProcessor::impulseResponseLibrarySize()
{
    ensureImpulseResponseLibrary();
    const juce::ScopedLock lock(impulseResponseLibraryLock);
    return static_cast<int>(impulseResponseLibrary.size());
}

bool MegaDSPAudioProcessor::isImpulseResponseFavorite(
    const juce::File& file) const
{
    return file.existsAsFile()
        && globalImpulseResponseFavorites().contains(
            file.getFullPathName());
}

juce::Result MegaDSPAudioProcessor::setImpulseResponseFavorite(
    const juce::File& file, bool favorite)
{
    const auto extension = file.getFileExtension().toLowerCase();
    if (!file.existsAsFile()
        || (extension != ".wav" && extension != ".aif"
            && extension != ".aiff" && extension != ".flac"))
        return juce::Result::fail(
            "Choose an existing impulse response to favorite.");
    const auto result = persistImpulseResponseFavorite(
        file.getFullPathName(), favorite);
    if (result.wasOk())
        libraryGeneration.fetch_add(1, std::memory_order_release);
    return result;
}

juce::Result MegaDSPAudioProcessor::loadAdjacentImpulseResponse(
    int slot, int direction)
{
    if (direction == 0)
        return juce::Result::fail(
            "Choose either the previous or next impulse response.");

    ensureImpulseResponseLibrary();
    std::vector<ImpulseResponseLibraryEntry> impulseResponses;
    {
        const juce::ScopedLock lock(impulseResponseLibraryLock);
        impulseResponses = impulseResponseLibrary;
    }
    if (impulseResponses.empty())
        return juce::Result::fail(
            "The impulse response library contains no valid WAV, AIFF, or "
            "FLAC files.");

    const auto currentFile = juce::File(impulseResponsePath(slot));
    const auto current = std::find_if(
        impulseResponses.begin(), impulseResponses.end(),
        [&currentFile](const ImpulseResponseLibraryEntry& candidate)
        {
            return candidate.file == currentFile;
        });
    int target = direction > 0
        ? 0 : static_cast<int>(impulseResponses.size()) - 1;
    if (current != impulseResponses.end())
    {
        const auto currentIndex =
            static_cast<int>(std::distance(impulseResponses.begin(), current));
        const auto count = static_cast<int>(impulseResponses.size());
        target = (currentIndex + (direction > 0 ? 1 : count - 1)) % count;
    }
    return loadImpulseResponse(
        slot, impulseResponses[static_cast<size_t>(target)].file);
}

void MegaDSPAudioProcessor::clearImpulseResponse(int slot)
{
    clearImpulseResponse(slot, 0);
}

void MegaDSPAudioProcessor::clearImpulseResponse(int slot, int lane)
{
    ScopedProcessorUndoTransaction undo(*this, "Clear impulse response");
    auto expected = false;
    if (!topologyMutationInProgress.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return;
    suspendProcessing(true);
    const auto result = getRack().clearImpulseResponse(slot, lane);
    suspendProcessing(false);
    topologyMutationInProgress.store(false, std::memory_order_release);
    if (topologyDirty.load(std::memory_order_acquire))
        triggerAsyncUpdate();
    if (result.wasOk())
    {
        publishSnapshotToParameters(getSelectedSnapshot());
        updateHostDisplay(
            ChangeDetails{}.withNonParameterStateChanged(true));
    }
}

juce::File MegaDSPAudioProcessor::namModelRootDirectory() const
{
    auto root = juce::File(parameters.state.getProperty(
        "namModelRootDirectory").toString());
    if (root.isDirectory())
        return root;
    root = juce::File(globalNamModelRoot());
    return root.isDirectory() ? root : juce::File();
}

juce::File MegaDSPAudioProcessor::namCabinetRootDirectory() const
{
    const auto root = juce::File(globalNamCabinetRoot());
    return root.isDirectory() ? root : juce::File();
}

juce::Result MegaDSPAudioProcessor::setNamCabinetRootDirectory(
    const juce::File& directory)
{
    if (!directory.isDirectory())
        return juce::Result::fail(
            "Choose an existing speaker impulse directory.");
    const auto result = persistGlobalNamCabinetRoot(directory);
    if (result.wasOk())
        automaticLibraryCoordinator().requestScan();
    return result;
}

juce::Result MegaDSPAudioProcessor::setNamModelRootDirectory(
    const juce::File& directory)
{
    if (!directory.isDirectory())
        return juce::Result::fail(
            "Choose an existing NAM model directory.");
    libraryIndexPool.removeAllJobs(true, 30000);
    releaseAutomaticLibraryClaims();
    impulseResponseIndexInProgress.store(
        false, std::memory_order_release);
    namModelIndexInProgress.store(false, std::memory_order_release);
    parameters.state.setProperty(
        "namModelRootDirectory",
        directory.getFullPathName(), nullptr);
    {
        const juce::ScopedLock lock(namModelLibraryLock);
        indexedNamModelRoot.clear();
        namModelLibrary.clear();
        namModelLibraryValidated.store(
            false, std::memory_order_release);
    }
    const auto result = rebuildNamModelLibrary();
    if (result.failed())
        return result;
    const auto persisted = persistGlobalNamModelRoot(directory);
    if (persisted.failed())
        return persisted;
    automaticLibraryCoordinator().requestScan();
    updateHostDisplay(
        ChangeDetails{}.withNonParameterStateChanged(true));
    return juce::Result::ok();
}

juce::Result MegaDSPAudioProcessor::rebuildNamModelLibrary()
{
    const auto root = namModelRootDirectory();
    if (!root.isDirectory())
        return juce::Result::fail(
            "Choose a NAM model root directory first.");

    juce::Array<juce::File> files;
    root.findChildFiles(files, juce::File::findFiles, true);
    std::vector<NamModelLibraryEntry> rebuilt;
    rebuilt.reserve(static_cast<size_t>(files.size()));
    for (const auto& file : files)
    {
        if (!file.hasFileExtension("nam"))
            continue;
        if (const auto entry = inspectNamModel(file, root))
            rebuilt.push_back(*entry);
    }
    std::sort(
        rebuilt.begin(), rebuilt.end(),
        [](const auto& left, const auto& right)
        {
            const auto order = left.relativePath.compareIgnoreCase(
                right.relativePath);
            return order != 0
                ? order < 0
                : left.file.getFullPathName()
                      < right.file.getFullPathName();
        });
    {
        const juce::ScopedLock lock(namModelLibraryLock);
        namModelLibrary = std::move(rebuilt);
        indexedNamModelRoot = root.getFullPathName();
        namModelLibraryValidated.store(
            true, std::memory_order_release);
    }
    std::vector<NamModelLibraryEntry> cached;
    {
        const juce::ScopedLock lock(namModelLibraryLock);
        cached = namModelLibrary;
    }
    const auto result = persistNamModelLibraryCache(root, cached);
    automaticLibraryCoordinator().publishNam(root, cached);
    libraryGeneration.fetch_add(1, std::memory_order_release);
    return result;
}

void MegaDSPAudioProcessor::ensureNamModelLibrary()
{
    const auto root = namModelRootDirectory().getFullPathName();
    if (root.isEmpty())
    {
        const juce::ScopedLock lock(namModelLibraryLock);
        indexedNamModelRoot.clear();
        namModelLibrary.clear();
        namModelLibraryValidated.store(
            false, std::memory_order_release);
        return;
    }
    {
        const juce::ScopedLock lock(namModelLibraryLock);
        if (root == indexedNamModelRoot)
            return;
    }
    std::vector<NamModelLibraryEntry> cached;
    const auto rootDirectory = juce::File(root);
    if (restoreNamModelLibraryCache(rootDirectory, cached))
    {
        const juce::ScopedLock lock(namModelLibraryLock);
        namModelLibrary = std::move(cached);
        indexedNamModelRoot = root;
        namModelLibraryValidated.store(
            true, std::memory_order_release);
        libraryGeneration.fetch_add(1, std::memory_order_release);
        automaticLibraryCoordinator().publishNam(
            rootDirectory, namModelLibrary);
        return;
    }
    auto discovered = discoverNamModelPaths(rootDirectory);
    {
        const juce::ScopedLock lock(namModelLibraryLock);
        namModelLibrary = std::move(discovered);
        indexedNamModelRoot = root;
        namModelLibraryValidated.store(
            false, std::memory_order_release);
    }
    libraryGeneration.fetch_add(1, std::memory_order_release);
    scheduleNamModelLibraryRebuild();
}

void MegaDSPAudioProcessor::scheduleNamModelLibraryRebuild()
{
    auto expected = false;
    if (!namModelIndexInProgress.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return;
    libraryIndexPool.addJob(std::function<void()> {
        [this]
        {
            const auto result = rebuildNamModelLibrary();
            if (result.failed())
                megadsp::Diagnostics::instance().log(
                    "nam-index-failed", result.getErrorMessage());
            namModelIndexInProgress.store(
                false, std::memory_order_release);
        } });
}

std::vector<NamModelLibraryEntry>
MegaDSPAudioProcessor::searchNamModels(
    const juce::String& query, int maximumResults)
{
    ensureNamModelLibrary();
    maximumResults = juce::jmax(0, maximumResults);
    if (maximumResults == 0)
        return {};
    const auto terms = parseNamSearchQuery(query);
    struct ScoredEntry
    {
        NamModelLibraryEntry entry;
        int score = 0;
    };
    std::vector<ScoredEntry> scoredMatches;
    const auto favoritePaths = globalNamFavorites();
    const auto metadataAvailable =
        namModelLibraryValidated.load(std::memory_order_acquire);
    {
        const juce::ScopedLock lock(namModelLibraryLock);
        scoredMatches.reserve(namModelLibrary.size());
        for (const auto& entry : namModelLibrary)
        {
            const auto favorite = favoritePaths.contains(
                entry.file.getFullPathName());
            auto matched = true;
            auto score = 0;
            for (const auto& term : terms)
            {
                if (term.text.isEmpty())
                    continue;
                const auto termScore = namSearchTermScore(
                    entry, favorite, term, metadataAvailable);
                matched = term.excluded
                    ? termScore == 0 : termScore > 0;
                if (!matched)
                    break;
                if (!term.excluded)
                    score += termScore;
            }
            if (!matched)
                continue;
            scoredMatches.push_back({ entry, score });
            scoredMatches.back().entry.favorite = favorite;
        }
    }
    std::stable_sort(
        scoredMatches.begin(), scoredMatches.end(),
        [](const auto& left, const auto& right)
        {
            if (left.score != right.score)
                return left.score > right.score;
            return left.entry.relativePath.compareIgnoreCase(
                       right.entry.relativePath) < 0;
        });
    if (static_cast<int>(scoredMatches.size()) > maximumResults)
        scoredMatches.resize(static_cast<size_t>(maximumResults));
    std::vector<NamModelLibraryEntry> matches;
    matches.reserve(scoredMatches.size());
    for (auto& match : scoredMatches)
        matches.push_back(std::move(match.entry));
    return matches;
}

int MegaDSPAudioProcessor::namModelLibrarySize()
{
    ensureNamModelLibrary();
    const juce::ScopedLock lock(namModelLibraryLock);
    return static_cast<int>(namModelLibrary.size());
}

bool MegaDSPAudioProcessor::isNamModelFavorite(
    const juce::File& file) const
{
    return file.existsAsFile()
        && globalNamFavorites().contains(file.getFullPathName());
}

juce::Result MegaDSPAudioProcessor::setNamModelFavorite(
    const juce::File& file, bool favorite)
{
    if (!file.existsAsFile() || !file.hasFileExtension("nam"))
        return juce::Result::fail(
            "Choose an existing NAM model to favorite.");
    const auto result = persistNamFavorite(
        file.getFullPathName(), favorite);
    if (result.wasOk())
        libraryGeneration.fetch_add(1, std::memory_order_release);
    return result;
}

juce::Result MegaDSPAudioProcessor::deleteNamModel(int slot)
{
    return deleteNamModel(juce::File(namModelPath(slot)));
}

juce::Result MegaDSPAudioProcessor::deleteNamModel(
    const juce::File& file)
{
    const auto path = file.getFullPathName();
    if (!file.existsAsFile()
        || !file.hasFileExtension("nam"))
        return juce::Result::fail(
            "The selected NAM model file does not exist.");
    auto expected = false;
    if (!topologyMutationInProgress.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return juce::Result::fail(
            "A model or topology update is already in progress.");
    if (!file.deleteFile())
    {
        topologyMutationInProgress.store(
            false, std::memory_order_release);
        return juce::Result::fail(
            "Could not delete the NAM model file.");
    }

    suspendProcessing(true);
    for (int snapshot = 0; snapshot < megadsp::snapshotCount; ++snapshot)
        for (int rackSlot = 0; rackSlot < megadsp::slotCount; ++rackSlot)
            if (snapshotRacks[static_cast<size_t>(snapshot)]
                    ->namModelPath(rackSlot) == path)
                snapshotRacks[static_cast<size_t>(snapshot)]
                    ->clearNamModel(rackSlot);
    refreshLatency();
    suspendProcessing(false);

    std::vector<NamModelLibraryEntry> remaining;
    {
        const juce::ScopedLock lock(namModelLibraryLock);
        std::erase_if(
            namModelLibrary,
            [&file](const auto& entry)
            {
                return entry.file == file;
            });
        remaining = namModelLibrary;
    }
    const auto root = namModelRootDirectory();
    if (root.isDirectory())
    {
        const auto cacheResult =
            persistNamModelLibraryCache(root, remaining);
        if (cacheResult.failed())
            megadsp::Diagnostics::instance().log(
                "nam-cache-delete-failed",
                cacheResult.getErrorMessage());
    }
    const auto preferencesResult =
        removeGlobalNamPreferences(path);
    topologyMutationInProgress.store(false, std::memory_order_release);
    libraryGeneration.fetch_add(1, std::memory_order_release);
    publishSnapshotToParameters(getSelectedSnapshot());
    updateHostDisplay(
        ChangeDetails{}.withNonParameterStateChanged(true));
    if (topologyDirty.load(std::memory_order_acquire))
        triggerAsyncUpdate();
    return preferencesResult;
}

juce::Result MegaDSPAudioProcessor::loadAdjacentNamModel(
    int slot, int direction)
{
    if (direction == 0)
        return juce::Result::fail(
            "Choose either the previous or next NAM model.");

    ensureNamModelLibrary();
    std::vector<NamModelLibraryEntry> models;
    {
        const juce::ScopedLock lock(namModelLibraryLock);
        models = namModelLibrary;
    }
    if (models.empty())
        return juce::Result::fail(
            "The NAM model library contains no valid .nam files.");

    const auto currentFile = juce::File(namModelPath(slot));
    const auto current = std::find_if(
        models.begin(), models.end(),
        [&currentFile](const NamModelLibraryEntry& candidate)
        {
            return candidate.file == currentFile;
        });
    int target =
        direction > 0 ? 0 : static_cast<int>(models.size()) - 1;
    if (current != models.end())
    {
        const auto currentIndex =
            static_cast<int>(std::distance(models.begin(), current));
        const auto count = static_cast<int>(models.size());
        target = (currentIndex + (direction > 0 ? 1 : count - 1)) % count;
    }
    return loadNamModel(slot, models[static_cast<size_t>(target)].file);
}

juce::Result MegaDSPAudioProcessor::loadAdjacentNamModelAsync(
    int slot, int direction, NamModelLoadCallback callback)
{
    if (direction == 0)
        return juce::Result::fail(
            "Choose either the previous or next NAM model.");

    juce::File targetFile;
    {
        const juce::ScopedLock lock(namModelLibraryLock);
        const auto root = namModelRootDirectory().getFullPathName();
        if (root == indexedNamModelRoot && !namModelLibrary.empty()
            && namModelLibraryValidated.load(
                std::memory_order_acquire))
        {
            const auto currentFile = juce::File(namModelPath(slot));
            const auto current = std::find_if(
                namModelLibrary.begin(), namModelLibrary.end(),
                [&currentFile](const NamModelLibraryEntry& candidate)
                {
                    return candidate.file == currentFile;
                });
            auto target = direction > 0
                ? 0 : static_cast<int>(namModelLibrary.size()) - 1;
            if (current != namModelLibrary.end())
            {
                const auto currentIndex = static_cast<int>(
                    std::distance(namModelLibrary.begin(), current));
                const auto count =
                    static_cast<int>(namModelLibrary.size());
                target = (currentIndex
                          + (direction > 0 ? 1 : count - 1))
                    % count;
            }
            targetFile =
                namModelLibrary[static_cast<size_t>(target)].file;
        }
    }
    if (targetFile.existsAsFile())
        return loadNamModelAsync(
            slot, targetFile, std::move(callback));

    auto expected = false;
    if (!topologyMutationInProgress.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return juce::Result::fail(
            "A model or topology update is already in progress.");

    const auto snapshot = getSelectedSnapshot();
    auto* rack = snapshotRacks[static_cast<size_t>(snapshot)].get();
    const auto currentFile = juce::File(namModelPath(slot));
    namModelLoadPool.addJob(std::function<void()> {
        [this, rack, snapshot, slot, direction, currentFile,
         completionCallback = std::move(callback)]() mutable
        {
            auto completed = std::make_unique<PendingNamModelLoad>();
            completed->rack = rack;
            completed->snapshot = snapshot;
            completed->callback = std::move(completionCallback);

            juce::File resolved;
            const auto root = namModelRootDirectory();
            std::vector<NamModelLibraryEntry> cached;
            if (root.isDirectory()
                && restoreNamModelLibraryCache(root, cached))
            {
                const juce::ScopedLock lock(namModelLibraryLock);
                namModelLibrary = std::move(cached);
                indexedNamModelRoot = root.getFullPathName();
            }
            {
                const juce::ScopedLock lock(namModelLibraryLock);
                if (!namModelLibrary.empty())
                {
                    const auto current = std::find_if(
                        namModelLibrary.begin(), namModelLibrary.end(),
                        [&currentFile](
                            const NamModelLibraryEntry& candidate)
                        {
                            return candidate.file == currentFile;
                        });
                    auto target = direction > 0
                        ? 0
                        : static_cast<int>(namModelLibrary.size()) - 1;
                    if (current != namModelLibrary.end())
                    {
                        const auto currentIndex = static_cast<int>(
                            std::distance(
                                namModelLibrary.begin(), current));
                        const auto count =
                            static_cast<int>(namModelLibrary.size());
                        target = (currentIndex
                                  + (direction > 0 ? 1 : count - 1))
                            % count;
                    }
                    resolved = namModelLibrary[
                        static_cast<size_t>(target)].file;
                }
            }
            auto prepared = false;
            auto preparationResult = juce::Result::fail(
                "The NAM model library contains no valid .nam files.");
            if (!resolved.existsAsFile() && root.isDirectory())
            {
                juce::Array<juce::File> discovered;
                root.findChildFiles(
                    discovered, juce::File::findFiles, true);
                std::vector<juce::File> models;
                models.reserve(static_cast<size_t>(discovered.size()));
                for (const auto& candidate : discovered)
                    if (candidate.hasFileExtension("nam"))
                        models.push_back(candidate);
                std::sort(
                    models.begin(), models.end(),
                    [&root](const auto& left, const auto& right)
                    {
                        return left.getRelativePathFrom(root)
                                   .compareIgnoreCase(
                                       right.getRelativePathFrom(root))
                            < 0;
                    });
                const auto current =
                    std::find(models.begin(), models.end(), currentFile);
                auto target = direction > 0
                    ? 0 : static_cast<int>(models.size()) - 1;
                if (current != models.end())
                {
                    const auto currentIndex = static_cast<int>(
                        std::distance(models.begin(), current));
                    const auto count = static_cast<int>(models.size());
                    target = (currentIndex
                              + (direction > 0 ? 1 : count - 1))
                        % count;
                }
                const auto count = static_cast<int>(models.size());
                for (int attempt = 0; attempt < count; ++attempt)
                {
                    const auto candidateIndex =
                        (target
                         + (direction > 0 ? attempt : count - attempt))
                        % count;
                    const auto& candidate =
                        models[static_cast<size_t>(candidateIndex)];
                    preparationResult = rack->prepareNamModel(
                        slot, candidate, completed->load);
                    if (preparationResult.wasOk())
                    {
                        resolved = candidate;
                        prepared = true;
                        break;
                    }
                }
            }
            if (!prepared && resolved.existsAsFile())
            {
                preparationResult = rack->prepareNamModel(
                    slot, resolved, completed->load);
                prepared = preparationResult.wasOk();
            }
            if (prepared)
            {
                const auto preference = namCabinetPreference(
                    resolved.getFullPathName(), namModelRootDirectory());
                completed->cabinetEnabled = preference.enabled;
                completed->clearCabinet = true;
                const auto impulse = juce::File(preference.impulsePath);
                if (preference.impulsePath.isNotEmpty()
                    && !impulse.existsAsFile())
                {
                    preparationResult = juce::Result::fail(
                        "NAM cabinet impulse not found: "
                        + preference.impulsePath);
                    prepared = false;
                }
                else if (impulse.existsAsFile())
                {
                    preparationResult = rack->prepareNamCabinet(
                        slot, impulse, completed->cabinetLoad);
                    if (preparationResult.failed())
                        prepared = false;
                    else
                        completed->clearCabinet = false;
                }
            }
            completed->result = prepared
                ? juce::Result::ok() : preparationResult;
            {
                const juce::ScopedLock lock(pendingNamModelLoadLock);
                pendingNamModelLoad = std::move(completed);
            }
        } });
    return juce::Result::ok();
}

juce::Result MegaDSPAudioProcessor::loadNamModelAsync(
    int slot, const juce::File& file, NamModelLoadCallback callback)
{
    auto expected = false;
    if (!topologyMutationInProgress.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return juce::Result::fail(
            "A model or topology update is already in progress.");

    const auto snapshot = getSelectedSnapshot();
    auto* rack = snapshotRacks[static_cast<size_t>(snapshot)].get();
    namModelLoadPool.addJob(std::function<void()> {
        [this, rack, snapshot, slot, file,
         completionCallback = std::move(callback)]() mutable
        {
            auto completed = std::make_unique<PendingNamModelLoad>();
            completed->rack = rack;
            completed->snapshot = snapshot;
            completed->callback = std::move(completionCallback);
            completed->result =
                rack->prepareNamModel(slot, file, completed->load);
            if (completed->result.wasOk())
            {
                const auto preference = namCabinetPreference(
                    file.getFullPathName(), namModelRootDirectory());
                completed->cabinetEnabled = preference.enabled;
                completed->clearCabinet = true;
                const auto impulse = juce::File(preference.impulsePath);
                if (preference.impulsePath.isNotEmpty()
                    && !impulse.existsAsFile())
                    completed->result = juce::Result::fail(
                        "NAM cabinet impulse not found: "
                        + preference.impulsePath);
                else if (impulse.existsAsFile())
                {
                    completed->result = rack->prepareNamCabinet(
                        slot, impulse, completed->cabinetLoad);
                    if (completed->result.wasOk())
                        completed->clearCabinet = false;
                }
            }
            {
                const juce::ScopedLock lock(pendingNamModelLoadLock);
                pendingNamModelLoad = std::move(completed);
            }
        } });
    return juce::Result::ok();
}

void MegaDSPAudioProcessor::finishPendingNamModelLoad()
{
    std::unique_ptr<PendingNamModelLoad> completed;
    {
        const juce::ScopedLock lock(pendingNamModelLoadLock);
        completed = std::move(pendingNamModelLoad);
    }
    if (completed == nullptr)
        return;

    auto result = completed->result;
    if (result.wasOk())
    {
        ScopedProcessorUndoTransaction undo(
            *this,
            completed->cabinetOnly
                ? "Load NAM cabinet" : "Load NAM model");
        suspendProcessing(true);
        auto committed = completed->cabinetOnly
            ? completed->rack->commitNamCabinet(completed->cabinetLoad)
            : completed->rack->commitNamModel(completed->load);
        if (committed && !completed->cabinetOnly)
        {
            if (completed->clearCabinet)
                completed->rack->clearNamCabinet(
                    completed->load.slot);
            else
                committed = completed->rack->commitNamCabinet(
                    completed->cabinetLoad);
        }
        if (committed)
        {
            const auto slot = completed->cabinetOnly
                ? completed->cabinetLoad.slot : completed->load.slot;
            completed->rack->setNamCabinetEnabled(
                slot, completed->cabinetEnabled);
        }
        refreshLatency();
        suspendProcessing(false);
        if (!committed)
        {
            completed->rack->cancelNamModel(completed->load);
            completed->rack->cancelNamModel(completed->cabinetLoad);
            result = juce::Result::fail(
                completed->cabinetOnly
                    ? "NAM cabinet commit superseded."
                    : "NAM model commit superseded.");
        }
        else
        {
            disposeNamModelAsync(
                std::move(completed->load.prepared));
            disposeNamModelAsync(
                std::move(completed->cabinetLoad.prepared));
            if (completed->cabinetOnly)
            {
                const auto persisted = persistNamCabinetPreference(
                    completed->preferenceModelPath,
                    completed->preferenceImpulsePath,
                    completed->cabinetEnabled);
                if (persisted.failed())
                    result = persisted;
            }
            if (completed->snapshot == getSelectedSnapshot())
                publishSnapshotToParameters(completed->snapshot);
            updateHostDisplay(
                ChangeDetails{}.withNonParameterStateChanged(true));
        }
    }
    topologyMutationInProgress.store(false, std::memory_order_release);
    if (topologyDirty.load(std::memory_order_acquire))
        triggerAsyncUpdate();
    if (completed->callback)
        completed->callback(result);
}

juce::Result MegaDSPAudioProcessor::loadNamCabinetAsync(
    int slot, const juce::File& file, NamModelLoadCallback callback)
{
    const auto modelPath = namModelPath(slot);
    if (modelPath.isEmpty())
        return juce::Result::fail(
            "Load a NAM model before choosing a cabinet impulse.");
    auto expected = false;
    if (!topologyMutationInProgress.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return juce::Result::fail(
            "A model or topology update is already in progress.");

    const auto snapshot = getSelectedSnapshot();
    auto* rack = snapshotRacks[static_cast<size_t>(snapshot)].get();
    namModelLoadPool.addJob(std::function<void()> {
        [this, rack, snapshot, slot, file, modelPath,
         completionCallback = std::move(callback)]() mutable
        {
            auto completed = std::make_unique<PendingNamModelLoad>();
            completed->rack = rack;
            completed->snapshot = snapshot;
            completed->callback = std::move(completionCallback);
            completed->cabinetOnly = true;
            completed->cabinetEnabled = true;
            completed->preferenceModelPath = modelPath;
            completed->preferenceImpulsePath =
                file.getFullPathName();
            completed->result = rack->prepareNamCabinet(
                slot, file, completed->cabinetLoad);
            {
                const juce::ScopedLock lock(pendingNamModelLoadLock);
                pendingNamModelLoad = std::move(completed);
            }
        } });
    return juce::Result::ok();
}

juce::Result MegaDSPAudioProcessor::loadAdjacentNamCabinetAsync(
    int slot, int direction, NamModelLoadCallback callback)
{
    if (direction == 0)
        return juce::Result::fail(
            "Choose either the previous or next cabinet impulse.");
    const auto root = namCabinetRootDirectory();
    if (!root.isDirectory())
        return juce::Result::fail(
            "Choose a speaker impulse root directory first.");
    const auto modelPath = namModelPath(slot);
    if (modelPath.isEmpty())
        return juce::Result::fail(
            "Load a NAM model before choosing a cabinet impulse.");
    auto expected = false;
    if (!topologyMutationInProgress.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return juce::Result::fail(
            "A model or topology update is already in progress.");

    const auto snapshot = getSelectedSnapshot();
    auto* rack = snapshotRacks[static_cast<size_t>(snapshot)].get();
    const auto currentFile = juce::File(namCabinetPath(slot));
    namModelLoadPool.addJob(std::function<void()> {
        [this, rack, snapshot, slot, direction, root, currentFile,
         modelPath, completionCallback = std::move(callback)]() mutable
        {
            auto completed = std::make_unique<PendingNamModelLoad>();
            completed->rack = rack;
            completed->snapshot = snapshot;
            completed->callback = std::move(completionCallback);
            completed->cabinetOnly = true;
            completed->cabinetEnabled = true;
            completed->preferenceModelPath = modelPath;

            auto files =
                automaticLibraryCoordinator().cabinetCatalog(root);
            if (files.empty())
            {
                juce::Array<juce::File> discovered;
                root.findChildFiles(
                    discovered, juce::File::findFiles, true);
                files.reserve(static_cast<size_t>(discovered.size()));
                for (const auto& candidate : discovered)
                    if (candidate.hasFileExtension("wav;aif;aiff;flac"))
                        files.push_back(candidate);
                std::sort(
                    files.begin(), files.end(),
                    [&root](const auto& left, const auto& right)
                    {
                        return left.getRelativePathFrom(root)
                                   .compareIgnoreCase(
                                       right.getRelativePathFrom(root))
                            < 0;
                    });
            }

            completed->result = juce::Result::fail(
                "The speaker impulse root contains no supported audio files.");
            if (!files.empty())
            {
                const auto current =
                    std::find(files.begin(), files.end(), currentFile);
                auto target = direction > 0
                    ? 0 : static_cast<int>(files.size()) - 1;
                if (current != files.end())
                {
                    const auto currentIndex = static_cast<int>(
                        std::distance(files.begin(), current));
                    const auto count = static_cast<int>(files.size());
                    target = (currentIndex
                              + (direction > 0 ? 1 : count - 1))
                        % count;
                }
                const auto file =
                    files[static_cast<size_t>(target)];
                completed->preferenceImpulsePath =
                    file.getFullPathName();
                completed->result = rack->prepareNamCabinet(
                    slot, file, completed->cabinetLoad);
            }
            {
                const juce::ScopedLock lock(pendingNamModelLoadLock);
                pendingNamModelLoad = std::move(completed);
            }
        } });
    return juce::Result::ok();
}

juce::Result MegaDSPAudioProcessor::setNamCabinetEnabled(
    int slot, bool enabled)
{
    const auto modelPath = namModelPath(slot);
    if (modelPath.isEmpty())
        return juce::Result::fail(
            "Load a NAM model before changing its cabinet preference.");
    getRack().setNamCabinetEnabled(slot, enabled);
    const auto result = persistNamCabinetPreference(
        modelPath, namCabinetPath(slot), enabled);
    if (result.wasOk())
        updateHostDisplay(
            ChangeDetails{}.withNonParameterStateChanged(true));
    return result;
}

void MegaDSPAudioProcessor::disposeNamModelAsync(
    megadsp::PreparedNamModelPtr model)
{
    if (model == nullptr)
        return;
    auto retired =
        std::shared_ptr<megadsp::PreparedNamModel>(std::move(model));
    namModelDisposalPool.addJob(std::function<void()> {
        [retired = std::move(retired)]() mutable
        {
            retired.reset();
        } });
}

juce::Result MegaDSPAudioProcessor::loadNamModel(
    int slot, const juce::File& file)
{
    ScopedProcessorUndoTransaction undo(*this, "Load NAM model");
    auto expected = false;
    if (!topologyMutationInProgress.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return juce::Result::fail(
            "A topology update is already in progress.");

    const auto finishDataMutation = [this]
    {
        topologyMutationInProgress.store(
            false, std::memory_order_release);
        if (topologyDirty.load(std::memory_order_acquire))
            triggerAsyncUpdate();
    };

    megadsp::EffectRack::PreparedNamModelLoad load;
    megadsp::EffectRack::PreparedNamModelLoad cabinetLoad;
    auto result = getRack().prepareNamModel(slot, file, load);
    if (result.failed())
    {
        finishDataMutation();
        return result;
    }

    const auto preference = namCabinetPreference(
        file.getFullPathName(), namModelRootDirectory());
    const auto impulse = juce::File(preference.impulsePath);
    if (preference.impulsePath.isNotEmpty()
        && !impulse.existsAsFile())
    {
        getRack().cancelNamModel(load);
        finishDataMutation();
        return juce::Result::fail(
            "NAM cabinet impulse not found: "
            + preference.impulsePath);
    }
    if (impulse.existsAsFile())
    {
        result = getRack().prepareNamCabinet(slot, impulse, cabinetLoad);
        if (result.failed())
        {
            getRack().cancelNamModel(load);
            finishDataMutation();
            return result;
        }
    }

    suspendProcessing(true);
    auto committed = getRack().commitNamModel(load);
    if (committed)
    {
        if (cabinetLoad.prepared != nullptr)
            committed = getRack().commitNamCabinet(cabinetLoad);
        else
            getRack().clearNamCabinet(slot);
        getRack().setNamCabinetEnabled(slot, preference.enabled);
    }
    refreshLatency();
    suspendProcessing(false);
    if (!committed)
    {
        getRack().cancelNamModel(load);
        getRack().cancelNamModel(cabinetLoad);
        finishDataMutation();
        return juce::Result::fail("NAM model commit superseded.");
    }

    finishDataMutation();
    publishSnapshotToParameters(getSelectedSnapshot());
    updateHostDisplay(
        ChangeDetails{}.withNonParameterStateChanged(true));
    return juce::Result::ok();
}

juce::Result MegaDSPAudioProcessor::clearNamModel(int slot)
{
    ScopedProcessorUndoTransaction undo(*this, "Clear NAM model");
    if (!beginTopologyMutation())
        return juce::Result::fail("Topology mutation in progress.");
    auto result = getRack().clearNamModel(slot);
    refreshLatency();
    suspendProcessing(false);
    topologyMutationInProgress.store(false, std::memory_order_release);
    publishSnapshotToParameters(getSelectedSnapshot());
    updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
    return result;
}

void MegaDSPAudioProcessor::topologyChanged()
{
    if (!beginTopologyMutation())
        return;
    finishTopologyMutation(true, false, false);
}

void MegaDSPAudioProcessor::loadFactoryPreset(int presetIndex)
{
    ScopedProcessorUndoTransaction undo(*this, "Load factory preset");
    struct PresetSlot
    {
        megadsp::ModuleType type = megadsp::ModuleType::empty;
        float bypass = 0.0f;
        std::array<float, megadsp::controlsPerSlot> controls {};
    };

    std::array<PresetSlot, megadsp::slotCount> finalState;
    for (auto& slot : finalState)
        slot.controls.fill(0.5f);

    auto setSlot = [&finalState](int slot, megadsp::ModuleType type,
                       std::initializer_list<std::pair<int, float>> values)
    {
        if (!juce::isPositiveAndBelow(slot, megadsp::slotCount))
            return;
        auto& destination = finalState[static_cast<size_t>(slot)];
        destination.type = type;
        destination.bypass = 0.0f;
        for (const auto& [control, value] : values)
            if (juce::isPositiveAndBelow(
                    control, megadsp::controlsPerSlot))
                destination.controls[static_cast<size_t>(control)] = value;
    };

    if (presetIndex == 1)
    {
        setSlot(0, megadsp::ModuleType::equalizer,
                { { 1, 0.46f }, { 4, 0.58f }, { 7, 0.54f } });
        setSlot(1, megadsp::ModuleType::compressor,
                { { 0, 0.64f }, { 1, 0.38f }, { 2, 0.25f },
                  { 3, 0.45f }, { 5, 0.20f }, { 6, 0.78f },
                  { 7, 0.0f }, { 8, 0.0f } });
        setSlot(2, megadsp::ModuleType::limiter,
                { { 0, 0.72f }, { 1, 0.92f }, { 2, 0.35f },
                  { 3, 0.35f }, { 4, 0.0f } });
    }
    else if (presetIndex == 2)
    {
        setSlot(0, megadsp::ModuleType::saturator,
                { { 0, 0.42f }, { 1, 0.70f }, { 3, 0.38f }, { 4, 0.72f } });
        setSlot(1, megadsp::ModuleType::compressor,
                { { 0, 0.48f }, { 1, 0.72f }, { 2, 0.08f },
                  { 3, 0.28f }, { 4, 0.25f }, { 6, 0.90f },
                  { 7, 0.0f }, { 8, 0.0f } });
        setSlot(2, megadsp::ModuleType::limiter,
                { { 0, 0.68f }, { 1, 0.90f }, { 2, 0.28f },
                  { 3, 0.45f }, { 4, 0.0f } });
    }
    else if (presetIndex == 3)
    {
        setSlot(0, megadsp::ModuleType::equalizer,
                { { 1, 0.45f }, { 4, 0.52f }, { 7, 0.56f } });
        setSlot(1, megadsp::ModuleType::delay,
                { { 1, 0.42f }, { 2, 0.36f }, { 3, 0.72f },
                  { 4, 1.0f }, { 5, 1.0f }, { 6, 0.43f },
                  { 7, 0.28f }, { 8, 0.18f } });
    }
    else if (presetIndex == 4)
    {
        setSlot(0, megadsp::ModuleType::equalizer,
                { { 1, 0.47f }, { 4, 0.52f }, { 7, 0.54f } });
        setSlot(1, megadsp::ModuleType::algorithmicReverb,
                { { 0, 0.72f }, { 1, 0.62f }, { 2, 0.48f },
                  { 3, 0.38f }, { 4, 0.10f }, { 5, 0.10f },
                  { 6, 0.78f }, { 7, 0.28f }, { 8, 0.72f },
                  { 9, 0.55f }, { 10, 0.34f }, { 11, 0.70f } });
    }

    if (!beginTopologyMutation())
        return;
    getRack().clearModuleDataState();
    const auto setPlain = [this](const juce::String& id, float value)
    {
        if (auto* parameter = parameters.getParameter(id))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(
                parameter->convertTo0to1(value));
            parameter->endChangeGesture();
        }
    };
    for (int slot = 0; slot < megadsp::slotCount; ++slot)
    {
        const auto& source = finalState[static_cast<size_t>(slot)];
        setPlain(
            megadsp::slotParameterId(slot, "type"),
            static_cast<float>(source.type));
        setPlain(megadsp::slotParameterId(slot, "bypass"), source.bypass);
        for (int control = 0; control < megadsp::controlsPerSlot; ++control)
            setPlain(
                megadsp::controlParameterId(slot, control),
                source.controls[static_cast<size_t>(control)]);
    }
    finishTopologyMutation(true, true, true);
}

juce::Result MegaDSPAudioProcessor::savePreset(const juce::File& file)
{
    auto state = parameters.copyState();
    state.setProperty("schemaVersion", megadsp::stateSchemaVersion, nullptr);
    state.setProperty("selectedTab", selectedTab, nullptr);
    serializeSnapshots(state);
    auto xml = state.createXml();
    if (xml == nullptr)
        return juce::Result::fail("Could not serialize preset state.");

    const auto temporary = file.getSiblingFile(file.getFileName() + ".tmp");
    if (!temporary.replaceWithText(xml->toString()))
        return juce::Result::fail("Could not write preset.");
    if (!temporary.moveFileTo(file))
    {
        temporary.deleteFile();
        return juce::Result::fail("Could not finalize preset.");
    }
    return juce::Result::ok();
}

juce::Result MegaDSPAudioProcessor::loadPreset(const juce::File& file)
{
    if (!file.existsAsFile())
        return juce::Result::fail("Preset does not exist.");
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || !xml->hasTagName(parameters.state.getType()))
        return juce::Result::fail("Preset is not a valid megaDSP preset.");
    auto state = juce::ValueTree::fromXml(*xml);
    const auto schemaVersion = static_cast<int>(
        state.getProperty("schemaVersion", 0));
    if (schemaVersion < 2
        || schemaVersion > megadsp::stateSchemaVersion)
        return juce::Result::fail("Preset uses an incompatible megaDSP state format.");
    ScopedProcessorUndoTransaction undo(*this, "Load preset");
    state = megadsp::migrateStateToCurrentSchema(state);
    if (!beginTopologyMutation())
        return juce::Result::fail(
            "Another topology update is already in progress.");
    selectedTab = static_cast<int>(state.getProperty("selectedTab", 0));
    restoreParameterState(state);
    restoreSnapshots(state, schemaVersion);
    finishTopologyMutation(true, true, true);
    setSelectedTab(selectedTab);
    return juce::Result::ok();
}

void MegaDSPAudioProcessor::refreshLatency()
{
    const auto latency = getAudioRack().latencySamples();
    pendingLatency.store(latency);
    if (!liveSafeSnapshots.load(std::memory_order_acquire)
        || !audioStreamActive.load(std::memory_order_acquire))
    {
        latencyDirty.store(false, std::memory_order_release);
        setLatencySamples(latency);
        updateHostDisplay(juce::AudioProcessorListener::ChangeDetails()
                              .withLatencyChanged(true));
        return;
    }
    latencyDirty.store(true, std::memory_order_release);
}

void MegaDSPAudioProcessor::restoreParameterState(const juce::ValueTree& state)
{
    parameters.replaceState(state);
    const auto seeded =
        seedGlobalImpulseResponseRoot(parameters.state);
    if (seeded.failed())
        megadsp::Diagnostics::instance().log(
            "ir-root-persist-failed",
            seeded.getErrorMessage());
    inheritGlobalImpulseResponseRoot(parameters.state);
    const auto namSeeded =
        seedGlobalNamModelRoot(parameters.state);
    if (namSeeded.failed())
        megadsp::Diagnostics::instance().log(
            "nam-root-persist-failed",
            namSeeded.getErrorMessage());
    inheritGlobalNamModelRoot(parameters.state);
    liveSafeSnapshots.store(
        static_cast<bool>(
            parameters.state.getProperty(
                "liveSafeSnapshots", false)),
        std::memory_order_release);
    parameters.state.setProperty(
        "instanceName",
        megadsp::ui::normalizeInstanceName(
            parameters.state.getProperty("instanceName").toString()),
        nullptr);
    parameters.state.setProperty(
        "backgroundTheme",
        megadsp::ui::safeBackgroundThemeIndex(static_cast<int>(
            parameters.state.getProperty("backgroundTheme", 0))),
        nullptr);
    for (const auto child : state)
    {
        const auto parameterId = child.getProperty("id").toString();
        if (auto* parameter = parameters.getParameter(parameterId))
        {
            const auto plainValue = static_cast<float>(
                static_cast<double>(child.getProperty("value")));
            const auto normalizedValue =
                dynamic_cast<juce::AudioParameterBool*>(parameter) != nullptr
                    ? plainValue
                    : parameter->convertTo0to1(plainValue);
            parameter->setValueNotifyingHost(normalizedValue);
        }
    }
}

void MegaDSPAudioProcessor::timerCallback()
{
    finishPendingImpulseResponseLoad();
    finishPendingNamModelLoad();
    if (++automaticLibraryPollCounter >= 30)
    {
        automaticLibraryPollCounter = 0;
        pollAutomaticLibraryUpdates();
    }

    for (int slot = 0; slot < megadsp::slotCount; ++slot)
    {
        const auto lanes =
            pendingWormholeReloads[static_cast<size_t>(slot)].exchange(
                0, std::memory_order_acq_rel);
        for (int lane = 0; lane < 2; ++lane)
        {
            const auto bit = static_cast<std::uint8_t>(1u << lane);
            if ((lanes & bit) == 0)
                continue;
            const auto path = impulseResponsePath(slot, lane);
            if (path.isEmpty())
                continue;
            const auto result =
                loadImpulseResponse(slot, lane, juce::File(path));
            if (result.failed()
                && result.getErrorMessage().containsIgnoreCase("in progress"))
                pendingWormholeReloads[static_cast<size_t>(slot)].fetch_or(
                    bit, std::memory_order_release);
        }

    }

    for (int slot = 0; slot < megadsp::slotCount; ++slot)
    {
        const auto index =
            pendingBuiltInCabinetLoads[static_cast<size_t>(slot)].exchange(
                -1, std::memory_order_acq_rel);
        if (index < 0)
            continue;
        const auto result = loadBuiltInImpulseResponse(slot, index);
        if (result.failed()
            && result.getErrorMessage().containsIgnoreCase("in progress"))
            pendingBuiltInCabinetLoads[static_cast<size_t>(slot)].store(
                index, std::memory_order_release);
    }

    const auto callbackGeneration =
        audioCallbackGeneration.load(std::memory_order_acquire);
    if (callbackGeneration != observedAudioCallbackGeneration)
    {
        observedAudioCallbackGeneration = callbackGeneration;
        audioIdlePollCount = 0;
        audioStreamActive.store(true, std::memory_order_release);
    }
    else if (audioStreamActive.load(std::memory_order_acquire))
    {
        const auto blockDurationPolls = juce::roundToInt(
            30.0 * static_cast<double>(
                       lastAudioBlockSamples.load(std::memory_order_acquire))
            / juce::jmax(1.0, preparedSampleRate));
        if (++audioIdlePollCount >= juce::jmax(5, blockDurationPolls + 3))
            audioStreamActive.store(false, std::memory_order_release);
    }

    if (snapshotUiDirty.exchange(false, std::memory_order_acq_rel))
    {
        const auto selected = getSelectedSnapshot();
        publishSnapshotToParameters(selected, false);
        selectedTab = juce::jlimit(
            0, juce::jmax(
                   0, getRack().activeSlotCount() - 1),
            selectedTab);
        topologyGeneration.fetch_add(1, std::memory_order_release);
        refreshLatency();
        updateHostDisplay(
            ChangeDetails{}.withNonParameterStateChanged(true));
    }
    if (topologyDirty.load(std::memory_order_acquire)
        && !topologyMutationInProgress.load(std::memory_order_acquire))
        handleAsyncUpdate();
    if ((!liveSafeSnapshots.load(std::memory_order_acquire)
         || !audioStreamActive.load(std::memory_order_acquire))
        && latencyDirty.exchange(false, std::memory_order_acq_rel))
    {
        setLatencySamples(pendingLatency.load());
        updateHostDisplay(juce::AudioProcessorListener::ChangeDetails()
                              .withLatencyChanged(true));
    }
}

#if defined(MEGADSP_TESTS)
void MegaDSPAudioProcessor::finishLibraryIndexForTesting()
{
    const auto deadline =
        juce::Time::getMillisecondCounterHiRes() + 30000.0;
    while (libraryIndexPool.getNumJobs() > 0
           && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::Thread::sleep(1);
}

void MegaDSPAudioProcessor::refreshAssetLibrariesForTesting()
{
    automaticLibraryCoordinator().scanNowForTesting();
    for (int pass = 0; pass < 3; ++pass)
    {
        pollAutomaticLibraryUpdates();
        finishLibraryIndexForTesting();
    }
}

void MegaDSPAudioProcessor::finishImpulseResponseLoadForTesting()
{
    const auto deadline =
        juce::Time::getMillisecondCounterHiRes() + 30000.0;
    while (impulseResponseLoadPool.getNumJobs() > 0
           && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::Thread::sleep(1);
    finishPendingImpulseResponseLoad();
}

void MegaDSPAudioProcessor::finishNamModelLoadForTesting()
{
    const auto deadline =
        juce::Time::getMillisecondCounterHiRes() + 30000.0;
    while (namModelLoadPool.getNumJobs() > 0
           && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::Thread::sleep(1);
    finishPendingNamModelLoad();
}
#endif


juce::AudioProcessorEditor* MegaDSPAudioProcessor::createEditor()
{
    return new MegaDSPAudioProcessorEditor(*this, editorWidth, editorHeight);
}

void MegaDSPAudioProcessor::rememberEditorSize(int width, int height)
{
    editorWidth = juce::jlimit(
        megadsp::ui::editorMinimumWidth,
        megadsp::ui::editorStateMaximumWidth, width);
    editorHeight = juce::jlimit(
        megadsp::ui::editorMinimumHeight,
        megadsp::ui::editorStateMaximumHeight, height);
}

juce::String MegaDSPAudioProcessor::globalWidgetSizeId() const
{
    return globalWidgetSizePreference();
}

juce::Result MegaDSPAudioProcessor::setGlobalWidgetSizeId(
    const juce::String& identifier)
{
    return persistGlobalWidgetSize(identifier);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MegaDSPAudioProcessor();
}
