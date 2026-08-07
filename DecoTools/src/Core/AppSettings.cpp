// Pewpew's Deco Tools - Settings Storage
// Loads and saves DecoTools.json, supplies default XML folders, and manages
// delayed setting writes so frequent UI changes do not constantly access disk.

#include "AppSettings.h"

#include "AppRuntime.h"

#include <Windows.h>
#include <shlobj.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace
{
    AppSettings::Data settings;
    bool dirty = false;
    DWORD dirtySince = 0;

    std::string DocumentsFolder()
    {
        PWSTR widePath = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(
            FOLDERID_Documents,
            0,
            nullptr,
            &widePath
        )))
        {
            const std::filesystem::path path(widePath);
            CoTaskMemFree(widePath);
            return path.string();
        }

        if (widePath != nullptr)
        {
            CoTaskMemFree(widePath);
        }

        char userProfile[512] = "";
        const DWORD length = GetEnvironmentVariableA(
            "USERPROFILE",
            userProfile,
            static_cast<DWORD>(sizeof(userProfile))
        );
        if (length > 0 && length < sizeof(userProfile))
        {
            return (std::filesystem::path(userProfile) / "Documents").string();
        }

        return {};
    }

    std::string DefaultXmlFolder(const char* folderName)
    {
        const std::string documents = DocumentsFolder();
        if (documents.empty())
        {
            return {};
        }

        return (
            std::filesystem::path(documents) /
            "Guild Wars 2" /
            folderName
        ).string();
    }

    std::string ConfigPath()
    {
        return AppRuntime::GetAddonDirectory() + "\\DecoTools.json";
    }

    std::string JsonEscape(const char* value)
    {
        std::string output;
        if (value == nullptr)
        {
            return output;
        }

        for (const char character : std::string(value))
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

    size_t FindValue(const std::string& json, const char* key)
    {
        const std::string token = std::string("\"") + key + "\"";
        size_t position = json.find(token);
        if (position == std::string::npos)
        {
            return position;
        }

        position = json.find(':', position + token.size());
        return position == std::string::npos ? position : position + 1;
    }

    std::string FindString(const std::string& json, const char* key, const char* fallback)
    {
        size_t position = FindValue(json, key);
        if (position == std::string::npos)
        {
            return fallback;
        }

        position = json.find('"', position);
        if (position == std::string::npos)
        {
            return fallback;
        }

        std::string output;
        bool escaped = false;
        for (++position; position < json.size(); ++position)
        {
            const char character = json[position];
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
                continue;
            }

            if (character == '\\')
            {
                escaped = true;
                continue;
            }

            if (character == '"')
            {
                return output;
            }

            output += character;
        }

        return fallback;
    }

    bool FindBool(const std::string& json, const char* key, bool fallback)
    {
        const size_t position = FindValue(json, key);
        if (position == std::string::npos)
        {
            return fallback;
        }

        const size_t first = json.find_first_not_of(" \t\r\n", position);
        if (first == std::string::npos)
        {
            return fallback;
        }

        if (json.compare(first, 4, "true") == 0)
        {
            return true;
        }
        if (json.compare(first, 5, "false") == 0)
        {
            return false;
        }

        return fallback;
    }

    float FindFloat(const std::string& json, const char* key, float fallback)
    {
        const size_t position = FindValue(json, key);
        if (position == std::string::npos)
        {
            return fallback;
        }

        try
        {
            return std::stof(json.substr(position));
        }
        catch (...)
        {
            return fallback;
        }
    }

    bool FindFloat4(const std::string& json, const char* key, float values[4])
    {
        size_t position = FindValue(json, key);
        if (position == std::string::npos)
        {
            return false;
        }

        position = json.find('[', position);
        if (position == std::string::npos)
        {
            return false;
        }

        std::istringstream stream(json.substr(position + 1));
        char comma = '\0';
        return
            static_cast<bool>(stream >> values[0] >> comma) && comma == ',' &&
            static_cast<bool>(stream >> values[1] >> comma) && comma == ',' &&
            static_cast<bool>(stream >> values[2] >> comma) && comma == ',' &&
            static_cast<bool>(stream >> values[3]);
    }

    template <size_t Size>
    void CopyToArray(std::array<char, Size>& destination, const std::string& value)
    {
        strncpy_s(destination.data(), destination.size(), value.c_str(), _TRUNCATE);
    }

    void Load()
    {
        std::ifstream file(ConfigPath(), std::ios::binary);
        if (!file.is_open())
        {
            return;
        }

        std::ostringstream contents;
        contents << file.rdbuf();
        const std::string json = contents.str();

        CopyToArray(settings.apiKey, FindString(json, "apiKey", settings.apiKey.data()));
        CopyToArray(
            settings.homesteadFolder,
            FindString(json, "homesteadFolder", settings.homesteadFolder.data())
        );
        CopyToArray(
            settings.guildHallFolder,
            FindString(json, "guildHallFolder", settings.guildHallFolder.data())
        );

        settings.checkForDatabaseUpdates =
            FindBool(json, "checkForDatabaseUpdates", settings.checkForDatabaseUpdates);
        settings.rememberWindowState =
            FindBool(json, "rememberWindowState", settings.rememberWindowState);
        settings.windowVisible =
            FindBool(json, "windowVisible", settings.windowVisible);
        settings.showDecorationCounter =
            FindBool(json, "showDecorationCounter", settings.showDecorationCounter);

        settings.showBoundingBox =
            FindBool(json, "showBoundingBox", settings.showBoundingBox);
        settings.showSolidFaces =
            FindBool(json, "showSolidFaces", settings.showSolidFaces);
        settings.showDecorationPoints =
            FindBool(json, "showDecorationPoints", settings.showDecorationPoints);
        FindFloat4(json, "boxColor", settings.boxColor);
        FindFloat4(json, "faceColor", settings.faceColor);
        FindFloat4(json, "pointColor", settings.pointColor);
        settings.pointSize = (std::max)(1.0f, (std::min)(12.0f,
            FindFloat(json, "pointSize", settings.pointSize)));

        if (!settings.rememberWindowState)
        {
            settings.windowVisible = true;
        }
    }
}

void AppSettings::Initialize()
{
    settings = Data{};
    CopyToArray(settings.homesteadFolder, DefaultXmlFolder("Homesteads"));
    CopyToArray(settings.guildHallFolder, DefaultXmlFolder("GuildHalls"));
    dirty = false;
    dirtySince = 0;
    Load();

    if (settings.homesteadFolder[0] == '\0')
    {
        CopyToArray(settings.homesteadFolder, DefaultXmlFolder("Homesteads"));
        MarkDirty();
    }
    if (settings.guildHallFolder[0] == '\0')
    {
        CopyToArray(settings.guildHallFolder, DefaultXmlFolder("GuildHalls"));
        MarkDirty();
    }
}

void AppSettings::Shutdown()
{
    if (dirty)
    {
        SaveNow();
    }
}

void AppSettings::Update()
{
    if (dirty && GetTickCount() - dirtySince >= 500)
    {
        SaveNow();
    }
}

void AppSettings::MarkDirty()
{
    dirty = true;
    dirtySince = GetTickCount();
}

void AppSettings::SaveNow()
{
    std::ofstream file(ConfigPath(), std::ios::binary | std::ios::trunc);
    if (!file.is_open())
    {
        return;
    }

    file << std::setprecision(9);
    file << "{\n";
    file << "  \"version\": \"1.2.0.2\",\n";
    file << "  \"apiKey\": \"" << JsonEscape(settings.apiKey.data()) << "\",\n";
    file << "  \"homesteadFolder\": \"" << JsonEscape(settings.homesteadFolder.data()) << "\",\n";
    file << "  \"guildHallFolder\": \"" << JsonEscape(settings.guildHallFolder.data()) << "\",\n";
    file << "  \"checkForDatabaseUpdates\": "
        << (settings.checkForDatabaseUpdates ? "true" : "false") << ",\n";
    file << "  \"rememberWindowState\": "
        << (settings.rememberWindowState ? "true" : "false") << ",\n";
    file << "  \"windowVisible\": "
        << (settings.windowVisible ? "true" : "false") << ",\n";
    file << "  \"showDecorationCounter\": "
        << (settings.showDecorationCounter ? "true" : "false") << ",\n";
    file << "  \"showBoundingBox\": "
        << (settings.showBoundingBox ? "true" : "false") << ",\n";
    file << "  \"showSolidFaces\": "
        << (settings.showSolidFaces ? "true" : "false") << ",\n";
    file << "  \"showDecorationPoints\": "
        << (settings.showDecorationPoints ? "true" : "false") << ",\n";
    file << "  \"boxColor\": [" << settings.boxColor[0] << ", " << settings.boxColor[1]
        << ", " << settings.boxColor[2] << ", " << settings.boxColor[3] << "],\n";
    file << "  \"faceColor\": [" << settings.faceColor[0] << ", " << settings.faceColor[1]
        << ", " << settings.faceColor[2] << ", " << settings.faceColor[3] << "],\n";
    file << "  \"pointColor\": [" << settings.pointColor[0] << ", " << settings.pointColor[1]
        << ", " << settings.pointColor[2] << ", " << settings.pointColor[3] << "],\n";
    file << "  \"pointSize\": " << settings.pointSize << "\n";
    file << "}\n";

    if (file.good())
    {
        dirty = false;
    }
}

AppSettings::Data& AppSettings::Get()
{
    return settings;
}
