// Pewpew's Deco Tools - Guild Wars 2 API Client
// Sends authenticated HTTPS requests directly to api.guildwars2.com and parses
// guild information and decoration availability without an external helper app.

#include "Gw2Api.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <winhttp.h>

#include <cctype>
#include <algorithm>
#include <sstream>
#include <vector>

#pragma comment(lib, "Winhttp.lib")

namespace
{
    std::wstring Utf8ToWide(const std::string& text)
    {
        if (text.empty()) return {};
        const int count = MultiByteToWideChar(
            CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (count <= 0) return {};
        std::wstring output(static_cast<size_t>(count), L'\0');
        MultiByteToWideChar(
            CP_UTF8, 0, text.data(), static_cast<int>(text.size()), output.data(), count);
        return output;
    }

    std::string ApiErrorText(const std::string& json)
    {
        const std::string token = "\"text\"";
        size_t position = json.find(token);
        if (position == std::string::npos) return {};
        position = json.find(':', position + token.size());
        if (position == std::string::npos) return {};
        position = json.find('"', position + 1);
        if (position == std::string::npos) return {};
        const size_t end = json.find('"', position + 1);
        return end == std::string::npos
            ? std::string()
            : json.substr(position + 1, end - position - 1);
    }

    std::string Get(
        const std::wstring& path,
        const std::string& apiKey,
        std::string& error
    )
    {
        error.clear();
        HINTERNET session = WinHttpOpen(
            L"PewpewsDecoTools/1.3.2.5",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0
        );
        if (session == nullptr)
        {
            error = "Could not initialize the GW2 API connection.";
            return {};
        }

        WinHttpSetTimeouts(session, 5000, 5000, 8000, 12000);
        HINTERNET connection = WinHttpConnect(
            session, L"api.guildwars2.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
        HINTERNET request = connection == nullptr
            ? nullptr
            : WinHttpOpenRequest(
                connection,
                L"GET",
                path.c_str(),
                nullptr,
                WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES,
                WINHTTP_FLAG_SECURE
            );

        std::wstring headers = L"Accept: application/json\r\n";
        if (!apiKey.empty())
        {
            headers += L"Authorization: Bearer " + Utf8ToWide(apiKey) + L"\r\n";
        }
        bool succeeded = request != nullptr &&
            WinHttpSendRequest(
                request,
                headers.c_str(),
                static_cast<DWORD>(headers.size()),
                WINHTTP_NO_REQUEST_DATA,
                0,
                0,
                0
            ) == TRUE;
        if (succeeded)
        {
            succeeded = WinHttpReceiveResponse(request, nullptr) == TRUE;
        }

        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        if (succeeded)
        {
            succeeded = WinHttpQueryHeaders(
                request,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX,
                &statusCode,
                &statusSize,
                WINHTTP_NO_HEADER_INDEX
            ) == TRUE;
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
            if (!WinHttpReadData(request, response.data() + oldSize, available, &read))
            {
                succeeded = false;
                break;
            }
            response.resize(oldSize + read);
        }

        const DWORD windowsError = succeeded ? ERROR_SUCCESS : GetLastError();
        if (request != nullptr) WinHttpCloseHandle(request);
        if (connection != nullptr) WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);

        if (!succeeded)
        {
            error = "Could not connect to the Guild Wars 2 API";
            if (windowsError != ERROR_SUCCESS)
            {
                error += " (Windows error " + std::to_string(windowsError) + ")";
            }
            error += ".";
            return {};
        }
        if (statusCode < 200 || statusCode >= 300)
        {
            error = "Guild Wars 2 API returned HTTP " + std::to_string(statusCode);
            const std::string apiText = ApiErrorText(response);
            if (!apiText.empty()) error += ": " + apiText;
            error += ".";
            return {};
        }
        return response;
    }

    size_t FindObjectEnd(const std::string& source, size_t begin)
    {
        int depth = 0;
        bool quoted = false;
        bool escaped = false;
        for (size_t position = begin; position < source.size(); ++position)
        {
            const char character = source[position];
            if (quoted)
            {
                if (escaped) escaped = false;
                else if (character == '\\') escaped = true;
                else if (character == '"') quoted = false;
                continue;
            }
            if (character == '"') quoted = true;
            else if (character == '{') ++depth;
            else if (character == '}' && --depth == 0) return position;
        }
        return std::string::npos;
    }

    std::string JsonString(
        const std::string& source,
        const char* key,
        size_t begin,
        size_t end
    )
    {
        const std::string token = std::string("\"") + key + "\"";
        size_t position = source.find(token, begin);
        if (position == std::string::npos || position >= end) return {};
        position = source.find(':', position + token.size());
        if (position == std::string::npos || position >= end) return {};
        position = source.find('"', position + 1);
        if (position == std::string::npos || position >= end) return {};

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
            else if (character == '\\') escaped = true;
            else if (character == '"') break;
            else output += character;
        }
        return output;
    }

    int JsonInt(
        const std::string& source,
        const char* key,
        size_t begin,
        size_t end,
        int fallback = -1
    )
    {
        const std::string token = std::string("\"") + key + "\"";
        size_t position = source.find(token, begin);
        if (position == std::string::npos || position >= end) return fallback;
        position = source.find(':', position + token.size());
        if (position == std::string::npos || position >= end) return fallback;
        ++position;
        while (position < end &&
            std::isspace(static_cast<unsigned char>(source[position])) != 0)
        {
            ++position;
        }
        try
        {
            return std::stoi(source.substr(position, end - position));
        }
        catch (...)
        {
            return fallback;
        }
    }

    std::vector<std::string> ParseGuildIds(const std::string& json)
    {
        std::vector<std::string> result;
        const size_t key = json.find("\"guilds\"");
        size_t position = key == std::string::npos
            ? std::string::npos : json.find('[', key + 8);
        const size_t end = position == std::string::npos
            ? std::string::npos : json.find(']', position + 1);
        if (position == std::string::npos || end == std::string::npos) return result;

        while (position < end)
        {
            const size_t quote = json.find('"', position + 1);
            if (quote == std::string::npos || quote >= end) break;
            const size_t quoteEnd = json.find('"', quote + 1);
            if (quoteEnd == std::string::npos || quoteEnd > end) break;
            result.push_back(json.substr(quote + 1, quoteEnd - quote - 1));
            position = quoteEnd;
        }
        return result;
    }

    std::vector<int> ParseIdArray(const std::string& json)
    {
        std::vector<int> result;
        size_t position = 0;
        while (position < json.size())
        {
            while (position < json.size() &&
                (std::isspace(static_cast<unsigned char>(json[position])) != 0 ||
                    json[position] == '[' || json[position] == ']' ||
                    json[position] == ','))
            {
                ++position;
            }
            if (position >= json.size()) break;
            try
            {
                size_t consumed = 0;
                const int id = std::stoi(json.substr(position), &consumed);
                if (id > 0) result.push_back(id);
                position += consumed;
            }
            catch (...)
            {
                break;
            }
        }
        return result;
    }

    std::map<int, int> ParseCountObjects(const std::string& json)
    {
        std::map<int, int> result;
        size_t position = 0;
        while (true)
        {
            const size_t begin = json.find('{', position);
            if (begin == std::string::npos) break;
            const size_t end = FindObjectEnd(json, begin);
            if (end == std::string::npos) break;
            const int id = JsonInt(json, "id", begin, end);
            const int count = JsonInt(json, "count", begin, end, 0);
            if (id >= 0) result[id] = count;
            position = end + 1;
        }
        return result;
    }

    std::wstring BuildGuildStoragePath(
        const std::string& guildId,
        const std::vector<int>& ids
    )
    {
        std::wostringstream path;
        path << L"/v2/guild/" << Utf8ToWide(guildId) << L"/storage";
        if (!ids.empty())
        {
            path << L"?ids=";
            for (size_t index = 0; index < ids.size(); ++index)
            {
                if (index != 0) path << L',';
                path << ids[index];
            }
        }
        return path.str();
    }
}

bool Gw2Api::LoadGuilds(
    const std::string& apiKey,
    std::vector<Guild>& guilds,
    std::string& error
)
{
    guilds.clear();
    const std::string account = Get(L"/v2/account", apiKey, error);
    if (!error.empty()) return false;

    for (const std::string& guildId : ParseGuildIds(account))
    {
        const std::string json =
            Get(L"/v2/guild/" + Utf8ToWide(guildId), apiKey, error);
        if (!error.empty()) return false;

        Guild guild;
        guild.id = JsonString(json, "id", 0, json.size());
        guild.name = JsonString(json, "name", 0, json.size());
        guild.tag = JsonString(json, "tag", 0, json.size());
        if (guild.id.empty()) guild.id = guildId;
        if (!guild.id.empty()) guilds.push_back(std::move(guild));
    }
    return true;
}

bool Gw2Api::LoadCounts(
    const std::string& apiKey,
    int decorationType,
    const std::string& guildId,
    const std::vector<int>& ids,
    std::map<int, int>& counts,
    std::string& error
)
{
    counts.clear();
    std::string json;
    if (decorationType == 0)
    {
        json = Get(L"/v2/account/homestead/decorations", apiKey, error);
    }
    else
    {
        if (guildId.empty())
        {
            error = "Select a guild to load Guild Hall counts.";
            return false;
        }
        for (const int id : ids) counts[id] = 0;
        json = Get(BuildGuildStoragePath(guildId, ids), apiKey, error);
    }
    if (!error.empty()) return false;

    const std::map<int, int> parsed = ParseCountObjects(json);
    for (const auto& [id, count] : parsed) counts[id] = count;
    return true;
}

bool Gw2Api::LoadHomesteadDecorationDefinitions(
    std::vector<HomesteadDecorationDefinition>& definitions,
    std::string& error
)
{
    definitions.clear();
    const std::string idJson = Get(L"/v2/homestead/decorations", {}, error);
    if (!error.empty()) return false;

    const std::vector<int> ids = ParseIdArray(idJson);
    if (ids.empty())
    {
        error = "The Guild Wars 2 API returned no Homestead decoration IDs.";
        return false;
    }

    constexpr size_t BatchSize = 150;
    for (size_t batchStart = 0; batchStart < ids.size(); batchStart += BatchSize)
    {
        std::wostringstream path;
        path << L"/v2/homestead/decorations?ids=";
        const size_t batchEnd = (std::min)(ids.size(), batchStart + BatchSize);
        for (size_t index = batchStart; index < batchEnd; ++index)
        {
            if (index != batchStart) path << L',';
            path << ids[index];
        }

        const std::string json = Get(path.str(), {}, error);
        if (!error.empty()) return false;

        size_t position = 0;
        while (true)
        {
            const size_t begin = json.find('{', position);
            if (begin == std::string::npos) break;
            const size_t end = FindObjectEnd(json, begin);
            if (end == std::string::npos) break;

            HomesteadDecorationDefinition definition;
            definition.id = JsonInt(json, "id", begin, end);
            definition.name = JsonString(json, "name", begin, end);
            definition.maxCount = JsonInt(json, "max_count", begin, end);
            if (definition.id > 0)
            {
                definitions.push_back(std::move(definition));
            }
            position = end + 1;
        }
    }

    if (definitions.empty())
    {
        error = "The Guild Wars 2 API returned no Homestead decoration definitions.";
        return false;
    }
    return true;
}
