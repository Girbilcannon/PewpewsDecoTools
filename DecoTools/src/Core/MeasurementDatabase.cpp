#include "MeasurementDatabase.h"

#include "Utf8Paths.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace
{
    std::filesystem::path databasePath;
    std::vector<MeasurementDatabase::Measurement> allMeasurements;
    std::uint64_t nextId = 1;

    std::string Escape(const std::string& value)
    {
        std::string output;
        output.reserve(value.size());
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

    size_t FindValue(
        const std::string& json,
        const char* key,
        size_t begin,
        size_t end)
    {
        const std::string token = std::string("\"") + key + "\"";
        size_t position = json.find(token, begin);
        if (position == std::string::npos || position >= end) return std::string::npos;
        position = json.find(':', position + token.size());
        if (position == std::string::npos || position >= end) return std::string::npos;
        return position + 1;
    }

    std::string ReadString(
        const std::string& json,
        const char* key,
        size_t begin,
        size_t end)
    {
        size_t position = FindValue(json, key, begin, end);
        if (position == std::string::npos) return {};
        position = json.find('"', position);
        if (position == std::string::npos || position >= end) return {};
        std::string output;
        bool escaped = false;
        for (++position; position < end; ++position)
        {
            const char character = json[position];
            if (escaped)
            {
                if (character == 'n') output += '\n';
                else if (character == 'r') output += '\r';
                else if (character == 't') output += '\t';
                else output += character;
                escaped = false;
            }
            else if (character == '\\') escaped = true;
            else if (character == '"') break;
            else output += character;
        }
        return output;
    }

    long long ReadInteger(
        const std::string& json,
        const char* key,
        size_t begin,
        size_t end,
        long long fallback = 0)
    {
        size_t position = FindValue(json, key, begin, end);
        if (position == std::string::npos) return fallback;
        position = json.find_first_not_of(" \t\r\n", position);
        if (position == std::string::npos || position >= end) return fallback;
        try { return std::stoll(json.substr(position, end - position)); }
        catch (...) { return fallback; }
    }

    bool ReadBool(
        const std::string& json,
        const char* key,
        size_t begin,
        size_t end,
        bool fallback)
    {
        size_t position = FindValue(json, key, begin, end);
        if (position == std::string::npos) return fallback;
        position = json.find_first_not_of(" \t\r\n", position);
        if (position == std::string::npos || position >= end) return fallback;
        if (json.compare(position, 4, "true") == 0) return true;
        if (json.compare(position, 5, "false") == 0) return false;
        return fallback;
    }

    bool ReadPoint(
        const std::string& json,
        const char* key,
        size_t begin,
        size_t end,
        MeasurementDatabase::Point& point)
    {
        size_t position = FindValue(json, key, begin, end);
        if (position == std::string::npos) return false;
        position = json.find('[', position);
        if (position == std::string::npos || position >= end) return false;
        std::istringstream stream(json.substr(position + 1, end - position - 1));
        char comma = '\0';
        return static_cast<bool>(stream >> point.x >> comma) && comma == ',' &&
            static_cast<bool>(stream >> point.y >> comma) && comma == ',' &&
            static_cast<bool>(stream >> point.z);
    }

    size_t FindObjectEnd(const std::string& json, size_t objectBegin)
    {
        bool inString = false;
        bool escaped = false;
        int depth = 0;
        for (size_t position = objectBegin; position < json.size(); ++position)
        {
            const char character = json[position];
            if (inString)
            {
                if (escaped) escaped = false;
                else if (character == '\\') escaped = true;
                else if (character == '"') inString = false;
                continue;
            }
            if (character == '"') inString = true;
            else if (character == '{') ++depth;
            else if (character == '}' && --depth == 0) return position;
        }
        return std::string::npos;
    }

    bool Save()
    {
        if (databasePath.empty()) return false;
        std::filesystem::path temporary = databasePath;
        temporary += L".tmp";
        std::error_code error;
        std::filesystem::remove(temporary, error);
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) return false;

        file << std::setprecision(15);
        file << "{\n  \"Version\": 2,\n  \"Measurements\": [\n";
        for (size_t index = 0; index < allMeasurements.size(); ++index)
        {
            const auto& item = allMeasurements[index];
            file << "    {\n";
            file << "      \"Id\": " << item.id << ",\n";
            file << "      \"LayoutKey\": \"" << Escape(item.layoutKey) << "\",\n";
            file << "      \"LayoutName\": \"" << Escape(item.layoutName) << "\",\n";
            file << "      \"LayoutType\": " << item.layoutType << ",\n";
            file << "      \"Name\": \"" << Escape(item.name) << "\",\n";
            file << "      \"MeasurementType\": " << static_cast<int>(item.type) << ",\n";
            file << "      \"PointA\": [" << item.pointA.x << ", " << item.pointA.y << ", " << item.pointA.z << "],\n";
            file << "      \"PointASource\": \"" << Escape(item.pointA.source) << "\",\n";
            file << "      \"PointB\": [" << item.pointB.x << ", " << item.pointB.y << ", " << item.pointB.z << "],\n";
            file << "      \"PointBSource\": \"" << Escape(item.pointB.source) << "\",\n";
            file << "      \"Ticks\": " << static_cast<int>(item.ticks) << ",\n";
            file << "      \"LineStyle\": " << static_cast<int>(item.lineStyle) << ",\n";
            file << "      \"Visible\": " << (item.visible ? "true" : "false") << ",\n";
            file << "      \"CreatedAtUnix\": " << item.createdAtUnix << "\n";
            file << "    }" << (index + 1 < allMeasurements.size() ? "," : "") << "\n";
        }
        file << "  ]\n}\n";
        file.flush();
        if (!file.good()) return false;
        file.close();

        if (!MoveFileExW(
            temporary.c_str(),
            databasePath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            std::filesystem::remove(temporary, error);
            return false;
        }
        return true;
    }

    void Load()
    {
        allMeasurements.clear();
        nextId = 1;
        std::ifstream file(databasePath, std::ios::binary);
        if (!file.is_open()) return;
        std::ostringstream contents;
        contents << file.rdbuf();
        const std::string json = contents.str();
        const size_t arrayStart = json.find("\"Measurements\"");
        if (arrayStart == std::string::npos) return;

        size_t position = json.find('[', arrayStart);
        while (position != std::string::npos)
        {
            const size_t objectBegin = json.find('{', position + 1);
            const size_t arrayEnd = json.find(']', position + 1);
            if (objectBegin == std::string::npos ||
                (arrayEnd != std::string::npos && arrayEnd < objectBegin)) break;
            const size_t objectEnd = FindObjectEnd(json, objectBegin);
            if (objectEnd == std::string::npos) break;

            MeasurementDatabase::Measurement item;
            item.id = static_cast<std::uint64_t>((std::max)(0LL,
                ReadInteger(json, "Id", objectBegin, objectEnd)));
            item.layoutKey = ReadString(json, "LayoutKey", objectBegin, objectEnd);
            item.layoutName = ReadString(json, "LayoutName", objectBegin, objectEnd);
            item.layoutType = static_cast<int>(ReadInteger(
                json, "LayoutType", objectBegin, objectEnd, -1));
            item.name = ReadString(json, "Name", objectBegin, objectEnd);
            item.type = static_cast<MeasurementDatabase::MeasurementType>(
                std::clamp<long long>(ReadInteger(
                    json, "MeasurementType", objectBegin, objectEnd), 0, 2));
            item.ticks = static_cast<MeasurementDatabase::TickDivision>(
                ReadInteger(json, "Ticks", objectBegin, objectEnd, 1));
            item.lineStyle = static_cast<MeasurementDatabase::LineStyle>(
                std::clamp<long long>(ReadInteger(
                    json, "LineStyle", objectBegin, objectEnd), 0, 3));
            item.visible = ReadBool(
                json, "Visible", objectBegin, objectEnd, true);
            item.createdAtUnix = ReadInteger(
                json, "CreatedAtUnix", objectBegin, objectEnd);
            item.pointA.source = ReadString(
                json, "PointASource", objectBegin, objectEnd);
            item.pointB.source = ReadString(
                json, "PointBSource", objectBegin, objectEnd);
            if (item.id != 0 && !item.layoutKey.empty() &&
                ReadPoint(json, "PointA", objectBegin, objectEnd, item.pointA) &&
                ReadPoint(json, "PointB", objectBegin, objectEnd, item.pointB))
            {
                nextId = (std::max)(nextId, item.id + 1);
                allMeasurements.push_back(std::move(item));
            }
            position = objectEnd;
        }
    }
}

void MeasurementDatabase::Initialize(const std::string& addonDirectory)
{
    databasePath = Utf8Paths::FromUtf8(addonDirectory) / "measurements.json";
    Load();
}

void MeasurementDatabase::Shutdown()
{
    allMeasurements.clear();
    databasePath.clear();
    nextId = 1;
}

std::vector<MeasurementDatabase::Measurement>
MeasurementDatabase::LoadForLayout(const std::string& layoutKey)
{
    std::vector<Measurement> result;
    for (const Measurement& item : allMeasurements)
        if (item.layoutKey == layoutKey) result.push_back(item);
    return result;
}

bool MeasurementDatabase::SaveForLayout(
    const std::string& layoutKey,
    const std::vector<Measurement>& measurements)
{
    allMeasurements.erase(
        std::remove_if(
            allMeasurements.begin(),
            allMeasurements.end(),
            [&](const Measurement& item) { return item.layoutKey == layoutKey; }),
        allMeasurements.end());
    allMeasurements.insert(
        allMeasurements.end(), measurements.begin(), measurements.end());
    return Save();
}

std::uint64_t MeasurementDatabase::NextId()
{
    return nextId++;
}
