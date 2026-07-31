#include "MapSwapTab.h"

#include "../../Core/AppSettings.h"
#include "../../Core/DecorationDatabase.h"
#include "../DecorationCounterWindow.h"
#include "../../imgui/imgui.h"
#include "../../imgui/imgui_internal.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <winhttp.h>

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

#pragma comment(lib, "Winhttp.lib")

namespace
{
    struct MapInfo
    {
        unsigned mapId;
        const char* mapName;
        int type;
    };

    struct XmlFileEntry
    {
        std::string name;
        std::string path;
    };

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

    struct Guild
    {
        std::string id;
        std::string name;
        std::string tag;
    };

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

    bool HasXmlExtension(const std::filesystem::path& path)
    {
        std::string extension = path.extension().string();
        std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            }
        );
        return extension == ".xml";
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

        if (folder.empty())
        {
            status = "Set this XML folder path in Settings first.";
            return;
        }

        std::error_code error;
        const std::filesystem::path directory(folder);
        if (!std::filesystem::is_directory(directory, error))
        {
            status = "XML folder not found. Check the path in Settings.";
            return;
        }

        for (std::filesystem::directory_iterator iterator(directory, error), end;
            !error && iterator != end;
            iterator.increment(error))
        {
            const std::filesystem::directory_entry& entry = *iterator;
            if (entry.is_regular_file(error) && !error && HasXmlExtension(entry.path()))
            {
                availableXmlFiles.push_back(
                    { entry.path().filename().string(), entry.path().string() }
                );
            }
        }

        std::sort(
            availableXmlFiles.begin(),
            availableXmlFiles.end(),
            [](const XmlFileEntry& left, const XmlFileEntry& right)
            {
                return left.name < right.name;
            }
        );

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
        std::ifstream file(path, std::ios::binary);
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
        parsed.fileName = std::filesystem::path(path).filename().string();

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
        precheck.includeMissing = includeMissing;
        precheck.guildId =
            selectedGuildIndex >= 0 &&
            selectedGuildIndex < static_cast<int>(guilds.size())
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
            output << "Enter an API key and ensure DecoToolsHelper is running to enable counts.\n\n";
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
                precheck.guildId
            );
        }
    }

    std::wstring Utf8ToWide(const std::string& text)
    {
        if (text.empty())
        {
            return {};
        }
        const int count = MultiByteToWideChar(
            CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (count <= 0)
        {
            return {};
        }
        std::wstring output(static_cast<size_t>(count), L'\0');
        MultiByteToWideChar(
            CP_UTF8, 0, text.data(), static_cast<int>(text.size()), output.data(), count);
        return output;
    }

    std::string HttpRequest(
        const wchar_t* method,
        const std::wstring& path,
        const std::string& body,
        std::string& error
    )
    {
        HINTERNET session = WinHttpOpen(
            L"DecoTools/0.0.1.9",
            WINHTTP_ACCESS_TYPE_NO_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0
        );
        if (session == nullptr)
        {
            error = "Could not initialize the local helper connection.";
            return {};
        }

        WinHttpSetTimeouts(session, 2500, 2500, 5000, 8000);
        HINTERNET connection = WinHttpConnect(
            session, L"localhost", 61337, 0);
        HINTERNET request = connection == nullptr
            ? nullptr
            : WinHttpOpenRequest(
                connection,
                method,
                path.c_str(),
                nullptr,
                WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES,
                0
            );

        bool succeeded = request != nullptr;
        if (succeeded)
        {
            const wchar_t* headers = body.empty()
                ? WINHTTP_NO_ADDITIONAL_HEADERS
                : L"Content-Type: application/json\r\n";
            const DWORD headerLength = body.empty()
                ? 0
                : static_cast<DWORD>(-1L);
            succeeded = WinHttpSendRequest(
                request,
                headers,
                headerLength,
                body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
                static_cast<DWORD>(body.size()),
                static_cast<DWORD>(body.size()),
                0
            ) == TRUE;
        }
        if (succeeded)
        {
            succeeded = WinHttpReceiveResponse(request, nullptr) == TRUE;
        }

        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        if (succeeded)
        {
            WinHttpQueryHeaders(
                request,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX,
                &statusCode,
                &statusSize,
                WINHTTP_NO_HEADER_INDEX
            );
        }

        std::string response;
        while (succeeded)
        {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available) || available == 0)
            {
                break;
            }
            const size_t oldSize = response.size();
            response.resize(oldSize + available);
            DWORD read = 0;
            if (!WinHttpReadData(
                request, response.data() + oldSize, available, &read))
            {
                succeeded = false;
                break;
            }
            response.resize(oldSize + read);
        }

        if (request != nullptr) WinHttpCloseHandle(request);
        if (connection != nullptr) WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);

        if (!succeeded)
        {
            error = "DecoToolsHelper is not responding on localhost:61337.";
            return {};
        }
        if (statusCode < 200 || statusCode >= 300)
        {
            error = "DecoToolsHelper returned HTTP " + std::to_string(statusCode) + ".";
            return {};
        }
        return response;
    }

    std::string JsonEscape(const std::string& value)
    {
        std::string output;
        for (const char character : value)
        {
            switch (character)
            {
            case '\\': output += "\\\\"; break;
            case '"': output += "\\\""; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default: output += character; break;
            }
        }
        return output;
    }

    bool SyncApiKey(const std::string& apiKey, std::string& error)
    {
        if (apiKey.empty())
        {
            error = "Enter an API key in Settings first.";
            return false;
        }
        const std::string body = "{\"apiKey\":\"" + JsonEscape(apiKey) + "\"}";
        HttpRequest(L"POST", L"/config/apikey", body, error);
        return error.empty();
    }

    std::string FindJsonString(
        const std::string& source,
        const std::string& key,
        size_t begin,
        size_t end
    )
    {
        const std::string token = "\"" + key + "\"";
        size_t position = source.find(token, begin);
        if (position == std::string::npos || position >= end)
        {
            return {};
        }
        position = source.find(':', position + token.size());
        if (position == std::string::npos || position >= end)
        {
            return {};
        }
        position = source.find('"', position + 1);
        if (position == std::string::npos || position >= end)
        {
            return {};
        }

        std::string output;
        bool escaped = false;
        for (++position; position < end; ++position)
        {
            const char character = source[position];
            if (escaped)
            {
                switch (character)
                {
                case 'n': output += '\n'; break;
                case 'r': output += '\r'; break;
                case 't': output += '\t'; break;
                default: output += character; break;
                }
                escaped = false;
            }
            else if (character == '\\')
            {
                escaped = true;
            }
            else if (character == '"')
            {
                break;
            }
            else
            {
                output += character;
            }
        }
        return output;
    }

    std::vector<Guild> ParseGuilds(const std::string& json)
    {
        std::vector<Guild> result;
        size_t position = 0;
        while (true)
        {
            const size_t begin = json.find('{', position);
            if (begin == std::string::npos)
            {
                break;
            }
            const size_t end = json.find('}', begin + 1);
            if (end == std::string::npos)
            {
                break;
            }

            Guild guild;
            guild.id = FindJsonString(json, "Id", begin, end);
            if (guild.id.empty()) guild.id = FindJsonString(json, "id", begin, end);
            guild.name = FindJsonString(json, "Name", begin, end);
            if (guild.name.empty()) guild.name = FindJsonString(json, "name", begin, end);
            guild.tag = FindJsonString(json, "Tag", begin, end);
            if (guild.tag.empty()) guild.tag = FindJsonString(json, "tag", begin, end);
            if (!guild.id.empty())
            {
                result.push_back(std::move(guild));
            }
            position = end + 1;
        }
        return result;
    }

    std::map<int, int> ParseCounts(const std::string& json)
    {
        std::map<int, int> result;
        size_t position = 0;
        while (true)
        {
            const size_t keyStart = json.find('"', position);
            if (keyStart == std::string::npos)
            {
                break;
            }
            const size_t keyEnd = json.find('"', keyStart + 1);
            const size_t colon = keyEnd == std::string::npos
                ? std::string::npos
                : json.find(':', keyEnd + 1);
            if (keyEnd == std::string::npos || colon == std::string::npos)
            {
                break;
            }
            try
            {
                const int id = std::stoi(json.substr(keyStart + 1, keyEnd - keyStart - 1));
                const int count = std::stoi(json.substr(colon + 1));
                result[id] = count;
            }
            catch (...)
            {
            }
            position = colon + 1;
        }
        return result;
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
            if (!SyncApiKey(apiKey, result.error))
            {
                return result;
            }

            const std::string json =
                HttpRequest(L"GET", L"/guilds", {}, result.error);
            if (!result.error.empty())
            {
                return result;
            }

            result.guilds = ParseGuilds(json);
            result.success = true;
            return result;
        });
    }

    std::string BuildIdsJson(const std::map<int, int>& required)
    {
        std::ostringstream output;
        output << "{\"ids\":[";
        bool first = true;
        for (const auto& [id, count] : required)
        {
            static_cast<void>(count);
            if (!first)
            {
                output << ',';
            }
            output << id;
            first = false;
        }
        output << "]}";
        return output.str();
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

        if (apiKey.empty())
        {
            FinishReport();
            status = "Pre-check complete without ownership counts.";
            return;
        }
        if (destination.type == 1 && guildId.empty())
        {
            precheck.valid = false;
            status = "Select a guild before running a Guild Hall pre-check.";
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
                if (!SyncApiKey(apiKey, result.error))
                {
                    return result;
                }

                std::string json;
                if (destination.type == 0)
                {
                    json = HttpRequest(
                        L"GET", L"/decos/homestead", {}, result.error);
                }
                else
                {
                    const std::wstring path =
                        L"/decos/guild/" + Utf8ToWide(guildId);
                    json = HttpRequest(
                        L"POST", path, BuildIdsJson(required), result.error);
                }

                if (!result.error.empty())
                {
                    return result;
                }
                result.counts = ParseCounts(json);
                result.success = true;
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
            if (selectedGuildIndex < 0 && guilds.size() == 1)
            {
                selectedGuildIndex = 0;
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
        if (!precheck.valid ||
            precheck.destinationIndex != selectedDestinationIndex ||
            precheck.includeMissing != includeMissing)
        {
            status = "Run Pre-Check again before exporting.";
            return;
        }

        const std::string currentGuildId =
            selectedGuildIndex >= 0 &&
            selectedGuildIndex < static_cast<int>(guilds.size())
            ? guilds[static_cast<size_t>(selectedGuildIndex)].id
            : std::string();
        if (precheck.guildId != currentGuildId)
        {
            status = "Guild selection changed. Run Pre-Check again.";
            return;
        }
        if (!includeMissing && !precheck.ownershipAvailable)
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
            if (keep && !includeMissing)
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
            std::filesystem::path(imported.fileName).stem().string();
        const std::string suffix = "_" + DestinationSuffix(destination.mapName);
        std::filesystem::path outputPath =
            destinationFolder / (baseName + suffix + ".xml");
        int index = 2;
        while (std::filesystem::exists(outputPath, error) && !error)
        {
            outputPath = destinationFolder /
                (baseName + suffix + "_" + std::to_string(index++) + ".xml");
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

        std::ostringstream addition;
        addition << "\n\nSWAP EXPORTED\n";
        addition << "----------------------------------------\n";
        addition << "Target map: " << destination.mapName << "\n";
        addition << "Updated IDs: " << updatedIds << "\n";
        addition << "Removed (no counterpart): " << removedNoCounterpart << "\n";
        addition << "Removed (missing ownership): " << removedMissing << "\n";
        addition << "Saved: " << outputPath.filename().string() << "\n";
        report += addition.str();

        if (selectedFolderType == destination.type)
        {
            RefreshXmlList();
        }
        status = "Exported " + outputPath.filename().string() + ".";
    }
}

void MapSwapTab::Render()
{
    PollJob();

    if (!fileListInitialized)
    {
        RefreshXmlList();
    }

    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    RenderSectionHeading("Import");
    ImGui::TextWrapped(
        "Choose a decoration XML, then select the map it should be converted for."
    );

    ImGui::Spacing();
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
    if (ImGui::BeginCombo("##SwapXmlList", selectedFileLabel))
    {
        for (size_t index = 0; index < availableXmlFiles.size(); ++index)
        {
            const bool selected = selectedXmlIndex == static_cast<int>(index);
            if (ImGui::Selectable(availableXmlFiles[index].name.c_str(), selected))
            {
                selectedXmlIndex = static_cast<int>(index);
            }
            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
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
        if (guilds.empty() &&
            !guildLoadAttempted &&
            activeJobKind == JobKind::None &&
            AppSettings::Get().apiKey[0] != '\0')
        {
            StartGuildLoad();
        }

        ImGui::Spacing();
        ImGui::Text("Destination Guild");
        const char* guildLabel =
            selectedGuildIndex >= 0 &&
            selectedGuildIndex < static_cast<int>(guilds.size())
            ? guilds[static_cast<size_t>(selectedGuildIndex)].name.c_str()
            : "Select a guild...";
        ImGui::SetNextItemWidth(-110.0f);
        if (ImGui::BeginCombo("##SwapGuild", guildLabel))
        {
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
        if (activeJobKind == JobKind::None)
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

    if (ImGui::Checkbox("Include Missing Decorations", &includeMissing))
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
