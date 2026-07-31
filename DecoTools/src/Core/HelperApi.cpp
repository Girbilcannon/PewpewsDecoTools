#include "HelperApi.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <winhttp.h>

#include <sstream>

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

    std::string Request(
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
            error = "Could not initialize the helper connection.";
            return {};
        }

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
            succeeded = WinHttpSendRequest(
                request,
                headers,
                body.empty() ? 0 : static_cast<DWORD>(-1L),
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
            if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
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

    std::string EscapeJson(const std::string& value)
    {
        std::string output;
        for (const char character : value)
        {
            if (character == '\\' || character == '"') output += '\\';
            output += character;
        }
        return output;
    }

    bool SyncKey(const std::string& apiKey, std::string& error)
    {
        if (apiKey.empty())
        {
            error = "Enter an API key in Settings first.";
            return false;
        }
        Request(
            L"POST",
            L"/config/apikey",
            "{\"apiKey\":\"" + EscapeJson(apiKey) + "\"}",
            error
        );
        return error.empty();
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
                output += character;
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

    std::map<int, int> ParseCounts(const std::string& json)
    {
        std::map<int, int> result;
        size_t position = 0;
        while (true)
        {
            const size_t keyStart = json.find('"', position);
            if (keyStart == std::string::npos) break;
            const size_t keyEnd = json.find('"', keyStart + 1);
            const size_t colon = keyEnd == std::string::npos
                ? std::string::npos : json.find(':', keyEnd + 1);
            if (keyEnd == std::string::npos || colon == std::string::npos) break;
            try
            {
                result[std::stoi(json.substr(keyStart + 1, keyEnd - keyStart - 1))] =
                    std::stoi(json.substr(colon + 1));
            }
            catch (...) {}
            position = colon + 1;
        }
        return result;
    }
}

bool HelperApi::LoadGuilds(
    const std::string& apiKey,
    std::vector<Guild>& guilds,
    std::string& error
)
{
    if (!SyncKey(apiKey, error)) return false;
    const std::string json = Request(L"GET", L"/guilds", {}, error);
    if (!error.empty()) return false;

    size_t position = 0;
    while (true)
    {
        const size_t begin = json.find('{', position);
        if (begin == std::string::npos) break;
        const size_t end = json.find('}', begin + 1);
        if (end == std::string::npos) break;
        Guild guild;
        guild.id = JsonString(json, "Id", begin, end);
        if (guild.id.empty()) guild.id = JsonString(json, "id", begin, end);
        guild.name = JsonString(json, "Name", begin, end);
        if (guild.name.empty()) guild.name = JsonString(json, "name", begin, end);
        guild.tag = JsonString(json, "Tag", begin, end);
        if (guild.tag.empty()) guild.tag = JsonString(json, "tag", begin, end);
        if (!guild.id.empty()) guilds.push_back(std::move(guild));
        position = end + 1;
    }
    return true;
}

bool HelperApi::LoadCounts(
    const std::string& apiKey,
    int decorationType,
    const std::string& guildId,
    const std::vector<int>& ids,
    std::map<int, int>& counts,
    std::string& error
)
{
    if (!SyncKey(apiKey, error)) return false;

    std::string json;
    if (decorationType == 0)
    {
        json = Request(L"GET", L"/decos/homestead", {}, error);
    }
    else
    {
        if (guildId.empty())
        {
            error = "Select a guild to load Guild Hall counts.";
            return false;
        }
        std::ostringstream body;
        body << "{\"ids\":[";
        for (size_t index = 0; index < ids.size(); ++index)
        {
            if (index != 0) body << ',';
            body << ids[index];
        }
        body << "]}";
        json = Request(
            L"POST",
            L"/decos/guild/" + Utf8ToWide(guildId),
            body.str(),
            error
        );
    }
    if (!error.empty()) return false;
    counts = ParseCounts(json);
    return true;
}
