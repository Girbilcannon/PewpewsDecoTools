// Pewpew's Deco Tools - Homestead and Guild Hall Map Converter
// Imports decoration layouts, converts their map and decoration identifiers,
// checks destination availability through the GW2 API, and exports converted XML.

#include "MapSwapTab.h"

#include "../../Core/AppSettings.h"
#include "../../Core/DecorationDatabase.h"
#include "../../Core/GroupBackupDatabase.h"
#include "../../Core/Gw2Api.h"
#include "../../Core/Utf8Paths.h"
#include "../../Core/XmlFileUtils.h"
#include "../DecorationCounterWindow.h"
#include "../XmlComboHelpers.h"
#include "../../imgui/imgui.h"
#include "../../imgui/imgui_internal.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    struct MapInfo
    {
        unsigned mapId;
        const char* mapName;
        int type;
    };

    using XmlFileEntry = XmlFileUtils::Entry;

    struct Prop
    {
        size_t tagStart = 0;
        size_t tagEnd = 0;
        size_t idStart = 0;
        size_t idLength = 0;
        std::string cleanName;
        int targetId = -1;
    };

    struct ImportedXml
    {
        std::string source;
        std::string path;
        std::string fileName;
        std::string mapName;
        unsigned mapId = 0;
        int type = -1;
        std::vector<Prop> props;
    };

    using Guild = Gw2Api::Guild;

    struct MissingEntry
    {
        int id = -1;
        int required = 0;
        int owned = 0;
    };

    struct Precheck
    {
        bool valid = false;
        bool ownershipAvailable = false;
        int destinationIndex = -1;
        bool includeMissing = true;
        std::string guildId;
        std::map<int, int> required;
        std::map<int, int> owned;
        std::vector<std::string> noCounterpart;
        std::vector<MissingEntry> missing;
    };

    enum class JobKind
    {
        None,
        LoadGuilds,
        LoadCounts
    };

    struct JobResult
    {
        JobKind kind = JobKind::None;
        unsigned generation = 0;
        bool success = false;
        std::string error;
        std::vector<Guild> guilds;
        std::map<int, int> counts;
    };

    struct TextEdit
    {
        size_t start = 0;
        size_t length = 0;
        std::string replacement;
    };

    constexpr MapInfo Maps[] =
    {
        { 1558, "Hearth's Glow", 0 },
        { 1596, "Comosus Isle", 0 },
        { 1121, "Gilded Hollow", 1 },
        { 1124, "Lost Precipice", 1 },
        { 1232, "Windswept Haven", 1 },
        { 1462, "Isle of Reflection", 1 }
    };

    int selectedFolderType = 0;
    int selectedXmlIndex = -1;
    int selectedDestinationIndex = 0;
    int selectedGuildIndex = -1;
    bool fileListInitialized = false;
    bool listedSubFolders = false;
    bool includeMissing = true;
    bool guildLoadAttempted = false;

    std::vector<XmlFileEntry> availableXmlFiles;
    std::vector<Guild> guilds;
    ImportedXml imported;
    Precheck precheck;
    std::string status = "No XML imported";
    std::string report;

    std::future<JobResult> activeJob;
    JobKind activeJobKind = JobKind::None;
    unsigned jobGeneration = 0;

    bool HasSelectedGuild()
    {
        return selectedGuildIndex >= 0 &&
            selectedGuildIndex < static_cast<int>(guilds.size());
    }

    bool IsNoSpecificGuild()
    {
        return Maps[selectedDestinationIndex].type == 1 &&
            !HasSelectedGuild();
    }

    bool EffectiveIncludeMissing()
    {
        return IsNoSpecificGuild() || includeMissing;
    }

    void RenderSectionHeading(const char* label)
    {
        ImGui::SetWindowFontScale(1.2f);
        ImGui::Text("%s", label);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Separator();
    }

    void RenderDisabledButton(const char* label, const ImVec2& size = ImVec2(0.0f, 0.0f))
    {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
        ImGui::Button(label, size);
        ImGui::PopStyleVar();
        ImGui::PopItemFlag();
    }

    std::string FolderForType(int type)
    {
        const AppSettings::Data& settings = AppSettings::Get();
        return type == 1
            ? settings.guildHallFolder.data()
            : settings.homesteadFolder.data();
    }

    void InvalidatePrecheck()
    {
        precheck = {};
        report.clear();
    }

    void RefreshXmlList()
    {
        const std::string folder = FolderForType(selectedFolderType);
        const std::string previousPath =
            selectedXmlIndex >= 0 &&
            selectedXmlIndex < static_cast<int>(availableXmlFiles.size())
            ? availableXmlFiles[static_cast<size_t>(selectedXmlIndex)].path
            : std::string();

        availableXmlFiles.clear();
        selectedXmlIndex = -1;
        fileListInitialized = true;
        listedSubFolders = AppSettings::Get().showXmlsFromSubFolders;

        if (folder.empty())
        {
            status = "Set this XML folder path in Settings first.";
            return;
        }

        if (!XmlFileUtils::List(
            folder,
            AppSettings::Get().showXmlsFromSubFolders,
            availableXmlFiles
        ))
        {
            status = "XML folder not found or could not be read. Check the path in Settings.";
            return;
        }

        for (size_t index = 0; index < availableXmlFiles.size(); ++index)
        {
            if (availableXmlFiles[index].path == previousPath)
            {
                selectedXmlIndex = static_cast<int>(index);
                break;
            }
        }

        if (selectedXmlIndex < 0 && !availableXmlFiles.empty())
        {
            selectedXmlIndex = 0;
        }

        status = availableXmlFiles.empty()
            ? "No XML files found in this folder."
            : "Found " + std::to_string(availableXmlFiles.size()) + " XML files.";
    }

    size_t FindTagEnd(const std::string& source, size_t tagStart)
    {
        char quote = '\0';
        for (size_t index = tagStart; index < source.size(); ++index)
        {
            const char character = source[index];
            if (quote != '\0')
            {
                if (character == quote)
                {
                    quote = '\0';
                }
                continue;
            }

            if (character == '"' || character == '\'')
            {
                quote = character;
            }
            else if (character == '>')
            {
                return index;
            }
        }
        return std::string::npos;
    }

    bool ReadAttribute(
        const std::string& source,
        size_t tagStart,
        size_t tagEnd,
        const char* attribute,
        std::string& value,
        size_t* valueStart = nullptr,
        size_t* valueLength = nullptr
    )
    {
        const std::string name(attribute);
        size_t position = tagStart;

        while (true)
        {
            position = source.find(name, position);
            if (position == std::string::npos || position >= tagEnd)
            {
                return false;
            }

            const bool validBefore =
                position == tagStart ||
                std::isspace(static_cast<unsigned char>(source[position - 1])) != 0;

            size_t equals = position + name.size();
            while (equals < tagEnd &&
                std::isspace(static_cast<unsigned char>(source[equals])) != 0)
            {
                ++equals;
            }

            if (!validBefore || equals >= tagEnd || source[equals] != '=')
            {
                position += name.size();
                continue;
            }

            ++equals;
            while (equals < tagEnd &&
                std::isspace(static_cast<unsigned char>(source[equals])) != 0)
            {
                ++equals;
            }

            if (equals >= tagEnd || (source[equals] != '"' && source[equals] != '\''))
            {
                return false;
            }

            const char quote = source[equals];
            const size_t start = equals + 1;
            const size_t end = source.find(quote, start);
            if (end == std::string::npos || end > tagEnd)
            {
                return false;
            }

            value = source.substr(start, end - start);
            if (valueStart != nullptr)
            {
                *valueStart = start;
            }
            if (valueLength != nullptr)
            {
                *valueLength = end - start;
            }
            return true;
        }
    }

    void AppendUtf8(std::string& output, unsigned codePoint)
    {
        if (codePoint <= 0x7F)
        {
            output += static_cast<char>(codePoint);
        }
        else if (codePoint <= 0x7FF)
        {
            output += static_cast<char>(0xC0 | (codePoint >> 6));
            output += static_cast<char>(0x80 | (codePoint & 0x3F));
        }
        else if (codePoint <= 0xFFFF)
        {
            output += static_cast<char>(0xE0 | (codePoint >> 12));
            output += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
            output += static_cast<char>(0x80 | (codePoint & 0x3F));
        }
        else
        {
            output += static_cast<char>(0xF0 | (codePoint >> 18));
            output += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
            output += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
            output += static_cast<char>(0x80 | (codePoint & 0x3F));
        }
    }

    std::string DecodeXml(std::string value)
    {
        std::string output;
        for (size_t index = 0; index < value.size();)
        {
            if (value[index] != '&')
            {
                output += value[index++];
                continue;
            }

            const size_t semicolon = value.find(';', index + 1);
            if (semicolon == std::string::npos)
            {
                output += value[index++];
                continue;
            }

            const std::string entity = value.substr(index + 1, semicolon - index - 1);
            if (entity == "amp") output += '&';
            else if (entity == "lt") output += '<';
            else if (entity == "gt") output += '>';
            else if (entity == "quot") output += '"';
            else if (entity == "apos") output += '\'';
            else if (!entity.empty() && entity[0] == '#')
            {
                try
                {
                    const bool hex =
                        entity.size() > 2 && (entity[1] == 'x' || entity[1] == 'X');
                    const unsigned codePoint = static_cast<unsigned>(
                        std::stoul(entity.substr(hex ? 2 : 1), nullptr, hex ? 16 : 10)
                    );
                    AppendUtf8(output, codePoint);
                }
                catch (...)
                {
                    output.append(value, index, semicolon - index + 1);
                }
            }
            else
            {
                output.append(value, index, semicolon - index + 1);
            }
            index = semicolon + 1;
        }
        return output;
    }

    std::string CleanDecorationName(const std::string& encodedName)
    {
        std::string value = DecodeXml(encodedName);
        const size_t lineBreak = value.find_first_of("\r\n");
        if (lineBreak != std::string::npos)
        {
            value.erase(lineBreak);
        }

        while (true)
        {
            const size_t tagStart = value.find('<');
            if (tagStart == std::string::npos)
            {
                break;
            }
            const size_t tagEnd = value.find('>', tagStart + 1);
            if (tagEnd == std::string::npos)
            {
                break;
            }
            value.erase(tagStart, tagEnd - tagStart + 1);
        }

        std::string output;
        bool pendingSpace = false;
        for (const char character : value)
        {
            if (std::isspace(static_cast<unsigned char>(character)) != 0)
            {
                pendingSpace = !output.empty();
            }
            else
            {
                if (pendingSpace)
                {
                    output += ' ';
                }
                output += character;
                pendingSpace = false;
            }
        }
        return output;
    }

    std::string EncodeXml(const std::string& value)
    {
        std::string output;
        for (const char character : value)
        {
            switch (character)
            {
            case '&': output += "&amp;"; break;
            case '<': output += "&lt;"; break;
            case '"': output += "&quot;"; break;
            case '\'': output += "&apos;"; break;
            default: output += character; break;
            }
        }
        return output;
    }

    bool ImportXml(const std::string& path)
    {
        const GroupBackupDatabase::ImportResult groupRestore =
            GroupBackupDatabase::PrepareImport(
                path,-1,AppSettings::Get().automaticGroupBackupRestore,
                AppSettings::Get().backupUngroupedXmls);
        if (groupRestore.action == GroupBackupDatabase::ImportAction::NeedsUserChoice ||
            groupRestore.action == GroupBackupDatabase::ImportAction::Error)
        {
            status = groupRestore.message;
            return false;
        }
        std::ifstream file(Utf8Paths::FromUtf8(path), std::ios::binary);
        if (!file.is_open())
        {
            status = "Could not open the selected XML file.";
            return false;
        }

        std::ostringstream contents;
        contents << file.rdbuf();
        ImportedXml parsed;
        parsed.source = contents.str();
        parsed.path = path;
        parsed.fileName = Utf8Paths::ToUtf8(
            Utf8Paths::FromUtf8(path).filename()
        );

        const size_t rootStart = parsed.source.find("<Decorations");
        const size_t rootEnd = rootStart == std::string::npos
            ? std::string::npos
            : FindTagEnd(parsed.source, rootStart);
        if (rootStart == std::string::npos || rootEnd == std::string::npos)
        {
            status = "Invalid XML: missing the <Decorations> tag.";
            return false;
        }

        std::string mapIdText;
        std::string typeText;
        if (!ReadAttribute(parsed.source, rootStart, rootEnd, "mapId", mapIdText) ||
            !ReadAttribute(parsed.source, rootStart, rootEnd, "type", typeText) ||
            (typeText != "0" && typeText != "1"))
        {
            status = "Invalid XML: missing or invalid map metadata.";
            return false;
        }

        ReadAttribute(parsed.source, rootStart, rootEnd, "mapName", parsed.mapName);
        parsed.mapName = DecodeXml(parsed.mapName);
        try
        {
            parsed.mapId = static_cast<unsigned>(std::stoul(mapIdText));
        }
        catch (...)
        {
            status = "Invalid XML: mapId is not a number.";
            return false;
        }
        parsed.type = typeText == "1" ? 1 : 0;

        size_t searchPosition = rootEnd + 1;
        while (true)
        {
            const size_t propStart = parsed.source.find("<prop", searchPosition);
            if (propStart == std::string::npos)
            {
                break;
            }

            const size_t propEnd = FindTagEnd(parsed.source, propStart);
            if (propEnd == std::string::npos)
            {
                status = "Invalid XML: an unfinished <prop> tag was found.";
                return false;
            }

            std::string name;
            std::string id;
            Prop prop;
            prop.tagStart = propStart;
            prop.tagEnd = propEnd + 1;
            if (ReadAttribute(parsed.source, propStart, propEnd, "name", name) &&
                ReadAttribute(
                    parsed.source,
                    propStart,
                    propEnd,
                    "id",
                    id,
                    &prop.idStart,
                    &prop.idLength
                ))
            {
                prop.cleanName = CleanDecorationName(name);
                parsed.props.push_back(std::move(prop));
            }
            searchPosition = propEnd + 1;
        }

        if (parsed.props.empty())
        {
            status = "Invalid XML: no decoration entries were found.";
            return false;
        }

        imported = std::move(parsed);
        InvalidatePrecheck();
        status =
            "Loaded " + std::to_string(imported.props.size()) + " decorations from " +
            imported.mapName + " (" +
            (imported.type == 0 ? "Homestead" : "Guild Hall") + ").";
        return true;
    }

    void BuildLocalPrecheck()
    {
        precheck = {};
        precheck.valid = true;
        precheck.destinationIndex = selectedDestinationIndex;
        precheck.includeMissing = EffectiveIncludeMissing();
        precheck.guildId = HasSelectedGuild()
            ? guilds[static_cast<size_t>(selectedGuildIndex)].id
            : std::string();

        const MapInfo& destination = Maps[selectedDestinationIndex];
        std::set<std::string> noCounterpart;

        for (Prop& prop : imported.props)
        {
            const DecorationDatabase::Entry* entry =
                DecorationDatabase::FindByCleanName(prop.cleanName);
            prop.targetId = entry == nullptr
                ? -1
                : (destination.type == 0
                    ? entry->homesteadId
                    : entry->guildUpgradeId);

            if (prop.targetId < 0)
            {
                noCounterpart.insert(prop.cleanName);
            }
            else
            {
                ++precheck.required[prop.targetId];
            }
        }

        precheck.noCounterpart.assign(noCounterpart.begin(), noCounterpart.end());
    }

    void FinishReport()
    {
        if (!precheck.valid)
        {
            return;
        }

        precheck.missing.clear();
        if (precheck.ownershipAvailable)
        {
            for (const auto& [id, required] : precheck.required)
            {
                const auto ownedEntry = precheck.owned.find(id);
                const int owned =
                    ownedEntry == precheck.owned.end() ? 0 : ownedEntry->second;
                if (owned < required)
                {
                    precheck.missing.push_back({ id, required, owned });
                }
            }
            std::sort(
                precheck.missing.begin(),
                precheck.missing.end(),
                [](const MissingEntry& left, const MissingEntry& right)
                {
                    return left.required - left.owned > right.required - right.owned;
                }
            );
        }

        const MapInfo& destination = Maps[precheck.destinationIndex];
        std::ostringstream output;
        output << "MAP SWAP - PRE-CHECK\n";
        output << "----------------------------------------\n";
        output << "Loaded XML: " << imported.fileName << "\n";
        output << "Props found: " << imported.props.size() << "\n";
        output << "From: " << (imported.type == 0 ? "Homestead" : "Guild Hall")
            << " - " << imported.mapName << "\n";
        output << "To: " << (destination.type == 0 ? "Homestead" : "Guild Hall")
            << " - " << destination.mapName << "\n\n";

        if (imported.type == 0 && destination.type == 1)
        {
            output << "WARNING:\n";
            output << "Homestead to Guild Hall swaps can encounter local-area decoration caps.\n\n";
        }

        if (precheck.noCounterpart.empty())
        {
            output << "All decorations have a valid destination counterpart.\n\n";
        }
        else
        {
            output << "No destination counterpart (always excluded):\n";
            for (const std::string& name : precheck.noCounterpart)
            {
                output << "  - " << name << "\n";
            }
            output << "\n";
        }

        if (!precheck.ownershipAvailable)
        {
            output << "Ownership counts were not verified.\n";
            if (destination.type == 1 && precheck.guildId.empty())
            {
                output << "No specific guild was selected. All transferable "
                    "decorations will be retained.\n\n";
            }
            else
            {
                output << "Enter a valid API key in Settings to enable counts.\n\n";
            }
        }
        else if (precheck.missing.empty())
        {
            output << "Ownership check: all required decorations are available.\n\n";
        }
        else
        {
            output << "Missing decorations ("
                << (precheck.includeMissing ? "included" : "excluded")
                << " by current option):\n";
            for (const MissingEntry& missing : precheck.missing)
            {
                const char* name =
                    DecorationDatabase::FindNameById(missing.id, destination.type);
                output << "  - " << (name == nullptr ? "Unknown" : name)
                    << " | Required: " << missing.required
                    << " | Owned: " << missing.owned
                    << " | Missing: " << (missing.required - missing.owned)
                    << "\n";
            }
            output << "\n";
        }

        output << "Include Missing Decorations: "
            << (precheck.includeMissing ? "YES" : "NO") << "\n";
        report = output.str();

        std::vector<DecorationCounterWindow::Requirement> counterItems;
        counterItems.reserve(precheck.required.size());
        for (const auto& [id, required] : precheck.required)
        {
            const char* name = DecorationDatabase::FindNameById(id, destination.type);
            counterItems.push_back(
                { id, name == nullptr ? "Unknown Decoration" : name, required });
        }
        const std::string counterContext =
            imported.fileName + " -> " + destination.mapName;
        if (precheck.ownershipAvailable)
        {
            DecorationCounterWindow::SetResolvedRequirements(
                counterContext,
                destination.type,
                counterItems,
                precheck.owned
            );
        }
        else
        {
            DecorationCounterWindow::SetRequirements(
                counterContext,
                destination.type,
                counterItems,
                precheck.guildId,
                !(destination.type == 1 && precheck.guildId.empty())
            );
        }
    }

     void StartGuildLoad()
    {
        if (activeJobKind != JobKind::None)
        {
            return;
        }

        guildLoadAttempted = true;
        const std::string apiKey = AppSettings::Get().apiKey.data();
        const unsigned generation = jobGeneration;
        activeJobKind = JobKind::LoadGuilds;
        status = "Loading guilds...";
        activeJob = std::async(std::launch::async, [apiKey, generation]()
        {
            JobResult result;
            result.kind = JobKind::LoadGuilds;
            result.generation = generation;
            result.success =
                Gw2Api::LoadGuilds(apiKey, result.guilds, result.error);
            return result;
        });
    }

     void StartCountLoad()
    {
        if (activeJobKind != JobKind::None)
        {
            return;
        }

        BuildLocalPrecheck();
        const MapInfo destination = Maps[selectedDestinationIndex];
        const std::string apiKey = AppSettings::Get().apiKey.data();
        const std::string guildId = precheck.guildId;
        const std::map<int, int> required = precheck.required;
        const unsigned generation = jobGeneration;

        if (apiKey.empty() || (destination.type == 1 && guildId.empty()))
        {
            FinishReport();
            status = "Pre-check complete without ownership counts.";
            return;
        }

        activeJobKind = JobKind::LoadCounts;
        status = "Checking decoration ownership...";
        activeJob = std::async(
            std::launch::async,
            [apiKey, guildId, destination, required, generation]()
            {
                JobResult result;
                result.kind = JobKind::LoadCounts;
                result.generation = generation;
                std::vector<int> ids;
                ids.reserve(required.size());
                for (const auto& [id, count] : required)
                {
                    static_cast<void>(count);
                    ids.push_back(id);
                }
                result.success = Gw2Api::LoadCounts(
                    apiKey,
                    destination.type,
                    guildId,
                    ids,
                    result.counts,
                    result.error
                );
                return result;
            }
        );
    }

    void PollJob()
    {
        if (activeJobKind == JobKind::None || !activeJob.valid() ||
            activeJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            return;
        }

        JobResult result;
        try
        {
            result = activeJob.get();
        }
        catch (...)
        {
            activeJobKind = JobKind::None;
            status = "The background API request failed unexpectedly.";
            return;
        }
        activeJobKind = JobKind::None;

        if (result.generation != jobGeneration)
        {
            return;
        }

        if (!result.success)
        {
            if (result.kind == JobKind::LoadCounts && precheck.valid)
            {
                FinishReport();
            }
            status = result.error.empty() ? "The API request failed." : result.error;
            return;
        }

        if (result.kind == JobKind::LoadGuilds)
        {
            const std::string previousGuild =
                selectedGuildIndex >= 0 &&
                selectedGuildIndex < static_cast<int>(guilds.size())
                ? guilds[static_cast<size_t>(selectedGuildIndex)].id
                : std::string();
            guilds = std::move(result.guilds);
            selectedGuildIndex = -1;
            for (size_t index = 0; index < guilds.size(); ++index)
            {
                if (guilds[index].id == previousGuild)
                {
                    selectedGuildIndex = static_cast<int>(index);
                    break;
                }
            }
            InvalidatePrecheck();
            status = guilds.empty()
                ? "No guilds were returned for this account."
                : "Loaded " + std::to_string(guilds.size()) + " guilds.";
        }
        else if (result.kind == JobKind::LoadCounts)
        {
            precheck.owned = std::move(result.counts);
            precheck.ownershipAvailable = true;
            FinishReport();
            status = "Pre-check complete.";
        }
    }

    bool AddAttributeEdit(
        const std::string& source,
        size_t tagStart,
        size_t tagEnd,
        const char* name,
        const std::string& value,
        std::vector<TextEdit>& edits
    )
    {
        std::string oldValue;
        size_t start = 0;
        size_t length = 0;
        if (!ReadAttribute(
            source, tagStart, tagEnd, name, oldValue, &start, &length))
        {
            return false;
        }
        edits.push_back({ start, length, EncodeXml(value) });
        return true;
    }

    std::string DestinationSuffix(const char* mapName)
    {
        std::string suffix;
        for (const char character : std::string(mapName))
        {
            if (std::isalnum(static_cast<unsigned char>(character)) != 0)
            {
                suffix += character;
            }
            else if ((character == ' ' || character == '-') &&
                !suffix.empty() && suffix.back() != '-')
            {
                suffix += '-';
            }
        }
        while (!suffix.empty() && suffix.back() == '-')
        {
            suffix.pop_back();
        }
        return suffix;
    }

    void ExportSwap()
    {
        const bool effectiveIncludeMissing = EffectiveIncludeMissing();
        if (!precheck.valid ||
            precheck.destinationIndex != selectedDestinationIndex ||
            precheck.includeMissing != effectiveIncludeMissing)
        {
            status = "Run Pre-Check again before exporting.";
            return;
        }

        const std::string currentGuildId = HasSelectedGuild()
            ? guilds[static_cast<size_t>(selectedGuildIndex)].id
            : std::string();
        if (precheck.guildId != currentGuildId)
        {
            status = "Guild selection changed. Run Pre-Check again.";
            return;
        }
        if (!effectiveIncludeMissing && !precheck.ownershipAvailable)
        {
            status =
                "Ownership counts are required to exclude missing decorations.";
            return;
        }

        const MapInfo& destination = Maps[selectedDestinationIndex];
        std::map<int, int> used;
        std::vector<TextEdit> edits;
        int removedNoCounterpart = 0;
        int removedMissing = 0;
        int updatedIds = 0;

        for (const Prop& prop : imported.props)
        {
            bool keep = prop.targetId >= 0;
            bool missingOwnership = false;
            if (keep && !effectiveIncludeMissing)
            {
                const auto ownedEntry = precheck.owned.find(prop.targetId);
                const int owned =
                    ownedEntry == precheck.owned.end() ? 0 : ownedEntry->second;
                const int alreadyUsed = used[prop.targetId]++;
                if (alreadyUsed >= owned)
                {
                    keep = false;
                    missingOwnership = true;
                }
            }

            if (!keep)
            {
                size_t removeStart = prop.tagStart;
                size_t removeEnd = prop.tagEnd;
                const size_t lineStart = imported.source.rfind('\n', prop.tagStart);
                if (lineStart != std::string::npos)
                {
                    const size_t whitespaceStart = lineStart + 1;
                    bool whitespaceOnly = true;
                    for (size_t index = whitespaceStart; index < prop.tagStart; ++index)
                    {
                        if (imported.source[index] != ' ' &&
                            imported.source[index] != '\t' &&
                            imported.source[index] != '\r')
                        {
                            whitespaceOnly = false;
                            break;
                        }
                    }
                    if (whitespaceOnly)
                    {
                        removeStart = whitespaceStart;
                        if (removeEnd < imported.source.size() &&
                            imported.source[removeEnd] == '\r')
                        {
                            ++removeEnd;
                        }
                        if (removeEnd < imported.source.size() &&
                            imported.source[removeEnd] == '\n')
                        {
                            ++removeEnd;
                        }
                    }
                }
                edits.push_back({ removeStart, removeEnd - removeStart, {} });
                if (missingOwnership) ++removedMissing;
                else ++removedNoCounterpart;
            }
            else
            {
                edits.push_back(
                    { prop.idStart, prop.idLength, std::to_string(prop.targetId) }
                );
                ++updatedIds;
            }
        }

        const size_t rootStart = imported.source.find("<Decorations");
        const size_t rootEnd = rootStart == std::string::npos
            ? std::string::npos
            : FindTagEnd(imported.source, rootStart);
        if (rootStart == std::string::npos || rootEnd == std::string::npos ||
            !AddAttributeEdit(
                imported.source,
                rootStart,
                rootEnd,
                "mapId",
                std::to_string(destination.mapId),
                edits
            ) ||
            !AddAttributeEdit(
                imported.source,
                rootStart,
                rootEnd,
                "mapName",
                destination.mapName,
                edits
            ) ||
            !AddAttributeEdit(
                imported.source,
                rootStart,
                rootEnd,
                "type",
                std::to_string(destination.type),
                edits
            ))
        {
            status = "Could not update the XML map metadata.";
            return;
        }

        std::sort(
            edits.begin(),
            edits.end(),
            [](const TextEdit& left, const TextEdit& right)
            {
                return left.start > right.start;
            }
        );

        std::string output = imported.source;
        for (const TextEdit& edit : edits)
        {
            output.replace(edit.start, edit.length, edit.replacement);
        }

        const std::filesystem::path destinationFolder(FolderForType(destination.type));
        std::error_code error;
        std::filesystem::create_directories(destinationFolder, error);
        if (error)
        {
            status = "Could not create the destination XML folder.";
            return;
        }

        const std::string baseName =
            Utf8Paths::ToUtf8(Utf8Paths::FromUtf8(imported.fileName).stem());
        const std::string suffix = "_" + DestinationSuffix(destination.mapName);
        std::filesystem::path outputPath =
            destinationFolder /
            Utf8Paths::FromUtf8(baseName + suffix + ".xml");
        int index = 2;
        while (std::filesystem::exists(outputPath, error) && !error)
        {
            outputPath = destinationFolder /
                Utf8Paths::FromUtf8(
                    baseName + suffix + "_" + std::to_string(index++) + ".xml"
                );
        }

        std::ofstream file(outputPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            status = "Could not create the swapped XML file.";
            return;
        }
        file.write(output.data(), static_cast<std::streamsize>(output.size()));
        if (!file.good())
        {
            status = "The swapped XML could not be written completely.";
            return;
        }
        file.close();

        if (AppSettings::Get().automaticGroupBackupRestore)
        {
            std::string backupStatus;
            GroupBackupDatabase::RecordFile(
                Utf8Paths::ToUtf8(outputPath),
                destination.type,
                GroupBackupDatabase::RestorePointType::Auto,
                std::string(),
                backupStatus,
                AppSettings::Get().backupUngroupedXmls
            );
        }

        std::ostringstream addition;
        addition << "\n\nSWAP EXPORTED\n";
        addition << "----------------------------------------\n";
        addition << "Target map: " << destination.mapName << "\n";
        addition << "Updated IDs: " << updatedIds << "\n";
        addition << "Removed (no counterpart): " << removedNoCounterpart << "\n";
        addition << "Removed (missing ownership): " << removedMissing << "\n";
        addition << "Saved: " << Utf8Paths::ToUtf8(outputPath.filename()) << "\n";
        report += addition.str();

        if (selectedFolderType == destination.type)
        {
            RefreshXmlList();
        }
        status = "Exported " + Utf8Paths::ToUtf8(outputPath.filename()) + ".";
    }
}

void MapSwapTab::Render()
{
    PollJob();

    if (!fileListInitialized ||
        listedSubFolders != AppSettings::Get().showXmlsFromSubFolders)
    {
        RefreshXmlList();
    }

    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    RenderSectionHeading("Import");
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    if (ImGui::RadioButton("Homestead##SwapFolder", selectedFolderType == 0))
    {
        selectedFolderType = 0;
        RefreshXmlList();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Guild Hall##SwapFolder", selectedFolderType == 1))
    {
        selectedFolderType = 1;
        RefreshXmlList();
    }

    const char* selectedFileLabel =
        selectedXmlIndex >= 0 &&
        selectedXmlIndex < static_cast<int>(availableXmlFiles.size())
        ? availableXmlFiles[static_cast<size_t>(selectedXmlIndex)].name.c_str()
        : "No XML files found";
    ImGui::SetNextItemWidth(-1.0f);
    XmlComboHelpers::SetPopupWidth(availableXmlFiles);
    if (ImGui::BeginCombo("##SwapXmlList", selectedFileLabel))
    {
        for (size_t index = 0; index < availableXmlFiles.size(); ++index)
        {
            const bool selected = selectedXmlIndex == static_cast<int>(index);
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::Selectable(availableXmlFiles[index].name.c_str(), selected))
            {
                selectedXmlIndex = static_cast<int>(index);
            }
            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    const float buttonWidth =
        (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Refresh List##Swap", ImVec2(buttonWidth, 0.0f)))
    {
        RefreshXmlList();
    }
    ImGui::SameLine();
    if (selectedXmlIndex >= 0 &&
        selectedXmlIndex < static_cast<int>(availableXmlFiles.size()))
    {
        if (ImGui::Button("Import Selected##Swap", ImVec2(buttonWidth, 0.0f)))
        {
            ImportXml(availableXmlFiles[static_cast<size_t>(selectedXmlIndex)].path);
        }
    }
    else
    {
        RenderDisabledButton("Import Selected##Swap", ImVec2(buttonWidth, 0.0f));
    }

    ImGui::Dummy(ImVec2(0.0f, 16.0f));
    RenderSectionHeading("Destination");

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo(
        "##SwapDestination",
        Maps[selectedDestinationIndex].mapName
    ))
    {
        for (int index = 0; index < static_cast<int>(sizeof(Maps) / sizeof(Maps[0])); ++index)
        {
            const std::string label =
                std::string(Maps[index].mapName) +
                (Maps[index].type == 0 ? " (Homestead)" : " (Guild Hall)");
            const bool selected = selectedDestinationIndex == index;
            if (ImGui::Selectable(label.c_str(), selected))
            {
                selectedDestinationIndex = index;
                selectedGuildIndex = -1;
                guildLoadAttempted = false;
                InvalidatePrecheck();
            }
            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (Maps[selectedDestinationIndex].type == 1)
    {
        if (AppSettings::Get().apiKey[0] == '\0' &&
            (!guilds.empty() || selectedGuildIndex >= 0))
        {
            guilds.clear();
            selectedGuildIndex = -1;
            includeMissing = true;
            InvalidatePrecheck();
        }

        if (guilds.empty() &&
            !guildLoadAttempted &&
            activeJobKind == JobKind::None &&
            AppSettings::Get().apiKey[0] != '\0')
        {
            StartGuildLoad();
        }

        ImGui::Spacing();
        ImGui::Text("Destination Guild");
        const char* guildLabel = HasSelectedGuild()
            ? guilds[static_cast<size_t>(selectedGuildIndex)].name.c_str()
            : "No Specific Guild";
        ImGui::SetNextItemWidth(-110.0f);
        if (ImGui::BeginCombo("##SwapGuild", guildLabel))
        {
            const bool noSpecificGuildSelected = !HasSelectedGuild();
            if (ImGui::Selectable(
                "No Specific Guild",
                noSpecificGuildSelected))
            {
                selectedGuildIndex = -1;
                includeMissing = true;
                InvalidatePrecheck();
            }
            if (noSpecificGuildSelected)
            {
                ImGui::SetItemDefaultFocus();
            }

            for (size_t index = 0; index < guilds.size(); ++index)
            {
                const std::string label = guilds[index].tag.empty()
                    ? guilds[index].name
                    : guilds[index].name + " [" + guilds[index].tag + "]";
                const bool selected = selectedGuildIndex == static_cast<int>(index);
                if (ImGui::Selectable(label.c_str(), selected))
                {
                    selectedGuildIndex = static_cast<int>(index);
                    InvalidatePrecheck();
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (activeJobKind == JobKind::None &&
            AppSettings::Get().apiKey[0] != '\0')
        {
            if (ImGui::Button("Refresh##Guilds", ImVec2(100.0f, 0.0f)))
            {
                guildLoadAttempted = false;
                StartGuildLoad();
            }
        }
        else
        {
            RenderDisabledButton("Refresh##Guilds", ImVec2(100.0f, 0.0f));
        }
    }

    ImGui::Dummy(ImVec2(0.0f, 16.0f));
    RenderSectionHeading("Pre-Check");

    const bool noSpecificGuild = IsNoSpecificGuild();
    if (noSpecificGuild && !includeMissing)
    {
        includeMissing = true;
        InvalidatePrecheck();
    }
    if (noSpecificGuild)
    {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar(
            ImGuiStyleVar_Alpha,
            ImGui::GetStyle().Alpha * 0.5f
        );
    }
    const bool includeMissingChanged =
        ImGui::Checkbox("Include Missing Decorations", &includeMissing);
    if (noSpecificGuild)
    {
        ImGui::PopStyleVar();
        ImGui::PopItemFlag();
    }
    if (includeMissingChanged)
    {
        InvalidatePrecheck();
    }

    if (!imported.source.empty() && activeJobKind == JobKind::None)
    {
        if (ImGui::Button("Run Pre-Check", ImVec2(150.0f, 0.0f)))
        {
            StartCountLoad();
        }
    }
    else
    {
        RenderDisabledButton("Run Pre-Check", ImVec2(150.0f, 0.0f));
    }

    if (!report.empty())
    {
        ImGui::Spacing();
        ImGui::BeginChild(
            "##MapSwapReport",
            ImVec2(0.0f, 190.0f),
            true,
            ImGuiWindowFlags_HorizontalScrollbar
        );
        ImGui::TextUnformatted(report.c_str());
        ImGui::EndChild();
    }

    ImGui::Dummy(ImVec2(0.0f, 16.0f));
    RenderSectionHeading("Export");
    if (precheck.valid && activeJobKind == JobKind::None)
    {
        if (ImGui::Button("Swap Maps and Export"))
        {
            ExportSwap();
        }
    }
    else
    {
        RenderDisabledButton("Swap Maps and Export");
    }

    ImGui::Spacing();
    ImGui::TextDisabled("%s", status.c_str());
    ImGui::TextDisabled(
        "Decoration database: %d entries",
        DecorationDatabase::Count()
    );
}

void MapSwapTab::ClearImportedData()
{
    DecorationCounterWindow::Clear();
    ++jobGeneration;
    imported = ImportedXml{};
    precheck = Precheck{};
    std::string().swap(report);
    selectedXmlIndex = -1;
    status = "No XML imported";
}

void MapSwapTab::Shutdown()
{
    if (activeJob.valid())
    {
        activeJob.wait();
        try
        {
            activeJob.get();
        }
        catch (...)
        {
        }
    }
    activeJobKind = JobKind::None;
}
