// Pewpew's Deco Tools - Persistent Group Backup and Restore Database

#include "GroupBackupDatabase.h"

#include "Utf8Paths.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <ctime>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
    constexpr std::array<char, 8> DatabaseMagic =
        { 'D', 'T', 'G', 'R', 'P', 'D', 'B', '1' };
    constexpr std::uint32_t DatabaseVersion = 3;
    constexpr size_t MaxAutomaticPointsPerLineage = 20;
    constexpr std::uint64_t MaxStoredString = 64ull * 1024ull * 1024ull;

    struct StoredProp
    {
        std::string signature;
        std::string rawXml;
    };

    struct StoredGroup
    {
        std::string name;
        std::vector<StoredProp> props;
    };

    struct RestorePoint
    {
        std::string id;
        GroupBackupDatabase::RestorePointType type =
            GroupBackupDatabase::RestorePointType::Auto;
        std::string customName;
        std::string xmlName;
        std::string lineage;
        std::string createdUtc;
        int xmlType = -1;
        std::string rootOpenTag;
        std::vector<StoredProp> ungroupedProps;
        std::vector<StoredGroup> groups;
    };

    struct ParsedProp
    {
        size_t start = 0;
        size_t end = 0;
        std::string rawXml;
        std::string signature;
        int groupIndex = -1;
    };

    struct ParsedGroup
    {
        std::string name;
        std::vector<size_t> propIndices;
    };

    struct ParsedXml
    {
        std::string source;
        int type = -1;
        size_t rootOpenStart = 0;
        size_t rootOpenEnd = 0;
        size_t rootCloseStart = 0;
        std::vector<ParsedProp> props;
        std::vector<ParsedGroup> groups;
    };

    std::filesystem::path databasePath;
    std::vector<RestorePoint> restorePoints;
    GroupBackupDatabase::PendingRestore pendingRestore;
    std::set<std::string> ignoredImportPaths;
    std::uint64_t idCounter = 0;

    std::string Lower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char character)
            { return static_cast<char>(std::tolower(character)); });
        return value;
    }

    std::string Trim(const std::string& value)
    {
        size_t first = 0;
        while (first < value.size() &&
            std::isspace(static_cast<unsigned char>(value[first])) != 0) ++first;
        size_t last = value.size();
        while (last > first &&
            std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) --last;
        return value.substr(first, last - first);
    }

    bool EndsWith(const std::string& value, const std::string& suffix)
    {
        return value.size() >= suffix.size() &&
            value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    size_t FindTagEnd(const std::string& source, size_t start)
    {
        char quote = '\0';
        for (size_t index = start; index < source.size(); ++index)
        {
            const char character = source[index];
            if (quote != '\0')
            {
                if (character == quote) quote = '\0';
            }
            else if (character == '"' || character == '\'')
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

    bool IsSelfClosing(const std::string& source, size_t start, size_t tagEnd)
    {
        size_t position = tagEnd;
        while (position > start &&
            std::isspace(static_cast<unsigned char>(source[position - 1])) != 0)
            --position;
        return position > start && source[position - 1] == '/';
    }

    bool ReadAttribute(const std::string& source, size_t tagStart, size_t tagEnd,
        const char* attribute, std::string& value)
    {
        const std::string name(attribute);
        size_t position = tagStart;
        while (true)
        {
            position = source.find(name, position);
            if (position == std::string::npos || position >= tagEnd) return false;
            const bool validBefore = position == tagStart ||
                std::isspace(static_cast<unsigned char>(source[position - 1])) != 0;
            size_t equals = position + name.size();
            while (equals < tagEnd &&
                std::isspace(static_cast<unsigned char>(source[equals])) != 0) ++equals;
            if (!validBefore || equals >= tagEnd || source[equals] != '=')
            {
                position += name.size();
                continue;
            }
            ++equals;
            while (equals < tagEnd &&
                std::isspace(static_cast<unsigned char>(source[equals])) != 0) ++equals;
            if (equals >= tagEnd || (source[equals] != '"' && source[equals] != '\''))
                return false;
            const char quote = source[equals];
            const size_t start = equals + 1;
            const size_t end = source.find(quote, start);
            if (end == std::string::npos || end > tagEnd) return false;
            value = source.substr(start, end - start);
            return true;
        }
    }

    bool ParseTriple(const std::string& value, double& x, double& y, double& z)
    {
        std::istringstream stream(value);
        std::string extra;
        return static_cast<bool>(stream >> x >> y >> z) && !(stream >> extra);
    }

    bool ParseScalar(const std::string& value, double& number)
    {
        std::istringstream stream(value);
        std::string extra;
        return static_cast<bool>(stream >> number) && !(stream >> extra);
    }

    std::string FormatNumber(double value)
    {
        if (std::abs(value) < 0.0000005) value = 0.0;
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(6) << value;
        std::string output = stream.str();
        while (output.size() > 1 && output.back() == '0') output.pop_back();
        if (!output.empty() && output.back() == '.') output.pop_back();
        return output;
    }

    std::string NormalizeAttributeValue(const std::string& name,
        const std::string& value)
    {
        if (name == "pos" || name == "rot" || name == "scale")
        {
            double x = 0.0, y = 0.0, z = 0.0;
            if (ParseTriple(value, x, y, z))
                return FormatNumber(x) + " " + FormatNumber(y) + " " + FormatNumber(z);
            if (name == "scale")
            {
                double scalar = 0.0;
                if (ParseScalar(value, scalar)) return FormatNumber(scalar);
            }
        }
        return Trim(value);
    }

    std::string CanonicalSignature(const std::string& raw,
        bool omitTransforms = false)
    {
        const size_t tagStart = raw.find("<prop");
        const size_t tagEnd = tagStart == std::string::npos
            ? std::string::npos : FindTagEnd(raw, tagStart);
        if (tagStart == std::string::npos || tagEnd == std::string::npos)
            return raw;

        std::map<std::string, std::string> attributes;
        size_t position = tagStart + 5;
        while (position < tagEnd)
        {
            while (position < tagEnd &&
                (std::isspace(static_cast<unsigned char>(raw[position])) != 0 ||
                    raw[position] == '/')) ++position;
            if (position >= tagEnd) break;
            const size_t nameStart = position;
            while (position < tagEnd &&
                std::isspace(static_cast<unsigned char>(raw[position])) == 0 &&
                raw[position] != '=' && raw[position] != '/' && raw[position] != '>')
                ++position;
            if (position == nameStart) { ++position; continue; }
            std::string name = Lower(raw.substr(nameStart, position - nameStart));
            while (position < tagEnd &&
                std::isspace(static_cast<unsigned char>(raw[position])) != 0) ++position;
            if (position >= tagEnd || raw[position] != '=') continue;
            ++position;
            while (position < tagEnd &&
                std::isspace(static_cast<unsigned char>(raw[position])) != 0) ++position;
            if (position >= tagEnd || (raw[position] != '"' && raw[position] != '\''))
                continue;
            const char quote = raw[position++];
            const size_t valueStart = position;
            while (position < tagEnd && raw[position] != quote) ++position;
            const std::string value = raw.substr(valueStart, position - valueStart);
            if (!omitTransforms ||
                (name != "pos" && name != "rot" && name != "scale" && name != "scl"))
            {
                attributes[name] = NormalizeAttributeValue(name, value);
            }
            if (position < tagEnd) ++position;
        }

        std::ostringstream signature;
        for (const auto& [name, value] : attributes)
            signature << name << '=' << value.size() << ':' << value << ';';

        if (!IsSelfClosing(raw, tagStart, tagEnd))
        {
            const size_t close = raw.rfind("</prop>");
            if (close != std::string::npos && close > tagEnd)
            {
                std::string content;
                bool pendingSpace = false;
                for (size_t index = tagEnd + 1; index < close; ++index)
                {
                    const char character = raw[index];
                    if (std::isspace(static_cast<unsigned char>(character)) != 0)
                        pendingSpace = !content.empty();
                    else
                    {
                        if (pendingSpace) content += ' ';
                        content += character;
                        pendingSpace = false;
                    }
                }
                signature << "content=" << content.size() << ':' << content;
            }
        }
        return signature.str();
    }

    struct PropMatchData
    {
        std::string stableSignature;
        std::array<double, 3> position = {};
        std::array<double, 3> rotation = {};
        double scale = 1.0;
        bool valid = false;
    };

    PropMatchData BuildPropMatchData(const std::string& raw)
    {
        PropMatchData data;
        const size_t tagStart = raw.find("<prop");
        const size_t tagEnd = tagStart == std::string::npos
            ? std::string::npos : FindTagEnd(raw, tagStart);
        if (tagStart == std::string::npos || tagEnd == std::string::npos)
            return data;

        std::string positionText, rotationText, scaleText;
        if (!ReadAttribute(raw, tagStart, tagEnd, "pos", positionText) ||
            !ReadAttribute(raw, tagStart, tagEnd, "rot", rotationText) ||
            (!ReadAttribute(raw, tagStart, tagEnd, "scl", scaleText) &&
                !ReadAttribute(raw, tagStart, tagEnd, "scale", scaleText)))
            return data;

        if (!ParseTriple(positionText, data.position[0], data.position[1],
                data.position[2]) ||
            !ParseTriple(rotationText, data.rotation[0], data.rotation[1],
                data.rotation[2]) ||
            !ParseScalar(scaleText, data.scale))
            return data;

        data.stableSignature = CanonicalSignature(raw, true);
        data.valid = true;
        return data;
    }

    double Distance3(const std::array<double, 3>& left,
        const std::array<double, 3>& right)
    {
        const double x = left[0] - right[0];
        const double y = left[1] - right[1];
        const double z = left[2] - right[2];
        return std::sqrt(x * x + y * y + z * z);
    }

    double RotationDistance(const std::array<double, 3>& left,
        const std::array<double, 3>& right)
    {
        constexpr double TwoPi = 6.28318530717958647692;
        double total = 0.0;
        for (size_t axis = 0; axis < 3; ++axis)
        {
            double delta = std::fmod(std::abs(left[axis] - right[axis]), TwoPi);
            delta = (std::min)(delta, TwoPi - delta);
            total += delta * delta;
        }
        return std::sqrt(total);
    }

    bool ParseXml(const std::string& source, ParsedXml& parsed, std::string& error)
    {
        parsed = ParsedXml{};
        parsed.source = source;
        const size_t rootStart = source.find("<Decorations");
        const size_t rootEnd = rootStart == std::string::npos
            ? std::string::npos : FindTagEnd(source, rootStart);
        const size_t rootClose = source.rfind("</Decorations>");
        if (rootStart == std::string::npos || rootEnd == std::string::npos ||
            rootClose == std::string::npos || rootClose <= rootEnd)
        {
            error = "Invalid Decorations XML.";
            return false;
        }
        std::string typeText;
        if (!ReadAttribute(source, rootStart, rootEnd, "type", typeText) ||
            (typeText != "0" && typeText != "1"))
        {
            error = "The Decorations XML has an invalid type.";
            return false;
        }
        parsed.type = typeText == "1" ? 1 : 0;
        parsed.rootOpenStart = rootStart;
        parsed.rootOpenEnd = rootEnd;
        parsed.rootCloseStart = rootClose;

        size_t search = rootEnd + 1;
        while (true)
        {
            const size_t start = source.find("<prop", search);
            if (start == std::string::npos || start >= rootClose) break;
            const size_t tagEnd = FindTagEnd(source, start);
            if (tagEnd == std::string::npos || tagEnd >= rootClose)
            {
                error = "The XML contains an unfinished prop tag.";
                return false;
            }
            size_t end = tagEnd + 1;
            if (!IsSelfClosing(source, start, tagEnd))
            {
                const size_t close = source.find("</prop>", tagEnd + 1);
                if (close == std::string::npos || close >= rootClose)
                {
                    error = "The XML contains a prop without a closing tag.";
                    return false;
                }
                end = close + sizeof("</prop>") - 1;
            }
            ParsedProp prop;
            prop.start = start;
            prop.end = end;
            prop.rawXml = source.substr(start, end - start);
            prop.signature = CanonicalSignature(prop.rawXml);
            parsed.props.push_back(std::move(prop));
            search = end;
        }
        if (parsed.props.empty())
        {
            error = "The Decorations XML does not contain any props.";
            return false;
        }

        size_t comment = source.find("<!--", rootEnd + 1);
        while (comment != std::string::npos && comment < rootClose)
        {
            const size_t marker = source.find("-->", comment + 4);
            if (marker == std::string::npos || marker >= rootClose) break;
            const size_t next = source.find("<!--", marker + 3);
            const size_t groupEnd = next == std::string::npos || next >= rootClose
                ? rootClose : next;
            ParsedGroup group;
            group.name = Trim(source.substr(comment + 4, marker - comment - 4));
            if (!group.name.empty())
            {
                const int groupIndex = static_cast<int>(parsed.groups.size());
                for (size_t index = 0; index < parsed.props.size(); ++index)
                {
                    ParsedProp& prop = parsed.props[index];
                    if (prop.start > marker && prop.start < groupEnd)
                    {
                        prop.groupIndex = groupIndex;
                        group.propIndices.push_back(index);
                    }
                }
                if (!group.propIndices.empty()) parsed.groups.push_back(std::move(group));
            }
            comment = next;
        }
        return true;
    }

    bool ReadFile(const std::string& path, std::string& source, std::string& error)
    {
        std::ifstream file(Utf8Paths::FromUtf8(path), std::ios::binary);
        if (!file.is_open())
        {
            error = "Could not open " +
                Utf8Paths::ToUtf8(Utf8Paths::FromUtf8(path).filename()) + ".";
            return false;
        }
        std::ostringstream contents;
        contents << file.rdbuf();
        source = contents.str();
        return true;
    }

    bool ParseFile(const std::string& path, ParsedXml& parsed, std::string& error)
    {
        std::string source;
        return ReadFile(path, source, error) && ParseXml(source, parsed, error);
    }

    std::string TimestampUtc()
    {
        const std::time_t now = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now());
        std::tm utc = {};
#ifdef _WIN32
        gmtime_s(&utc, &now);
#else
        gmtime_r(&now, &utc);
#endif
        std::ostringstream stream;
        stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
        return stream.str();
    }

    std::string MakeId()
    {
        std::string id = TimestampUtc();
        id.erase(std::remove_if(id.begin(), id.end(),
            [](unsigned char character)
            { return std::isalnum(character) == 0; }), id.end());
        return id + "-" + std::to_string(++idCounter);
    }

    template <typename Value>
    bool WriteValue(std::ostream& stream, const Value& value)
    {
        stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
        return stream.good();
    }

    template <typename Value>
    bool ReadValue(std::istream& stream, Value& value)
    {
        stream.read(reinterpret_cast<char*>(&value), sizeof(value));
        return stream.good();
    }

    bool WriteString(std::ostream& stream, const std::string& value)
    {
        const std::uint64_t size = static_cast<std::uint64_t>(value.size());
        if (!WriteValue(stream, size)) return false;
        stream.write(value.data(), static_cast<std::streamsize>(value.size()));
        return stream.good();
    }

    bool ReadString(std::istream& stream, std::string& value)
    {
        std::uint64_t size = 0;
        if (!ReadValue(stream, size) || size > MaxStoredString) return false;
        value.resize(static_cast<size_t>(size));
        if (size > 0)
            stream.read(value.data(), static_cast<std::streamsize>(size));
        return stream.good();
    }

    bool SaveDatabase()
    {
        if (databasePath.empty()) return false;
        std::filesystem::path temporary = databasePath;
        temporary += L".tmp";
        std::error_code error;
        std::filesystem::remove(temporary, error);
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) return false;
        file.write(DatabaseMagic.data(), static_cast<std::streamsize>(DatabaseMagic.size()));
        const std::uint32_t count = static_cast<std::uint32_t>(restorePoints.size());
        if (!WriteValue(file, DatabaseVersion) || !WriteValue(file, count)) return false;
        for (const RestorePoint& point : restorePoints)
        {
            const std::uint8_t type = point.type ==
                GroupBackupDatabase::RestorePointType::Safety ? 2 :
                point.type == GroupBackupDatabase::RestorePointType::Manual ? 1 : 0;
            const std::int32_t xmlType = point.xmlType;
            const std::uint32_t ungroupedCount =
                static_cast<std::uint32_t>(point.ungroupedProps.size());
            const std::uint32_t groupCount = static_cast<std::uint32_t>(point.groups.size());
            if (!WriteString(file, point.id) || !WriteValue(file, type) ||
                !WriteString(file, point.customName) || !WriteString(file, point.xmlName) ||
                !WriteString(file, point.lineage) || !WriteString(file, point.createdUtc) ||
                !WriteValue(file, xmlType) || !WriteString(file, point.rootOpenTag) ||
                !WriteValue(file, ungroupedCount)) return false;
            for (const StoredProp& prop : point.ungroupedProps)
                if (!WriteString(file, prop.signature) || !WriteString(file, prop.rawXml))
                    return false;
            if (!WriteValue(file, groupCount)) return false;
            for (const StoredGroup& group : point.groups)
            {
                const std::uint32_t propCount = static_cast<std::uint32_t>(group.props.size());
                if (!WriteString(file, group.name) || !WriteValue(file, propCount)) return false;
                for (const StoredProp& prop : group.props)
                    if (!WriteString(file, prop.signature) || !WriteString(file, prop.rawXml))
                        return false;
            }
        }
        file.flush();
        if (!file.good()) return false;
        file.close();

        if (std::filesystem::exists(databasePath, error) && !error)
        {
            std::filesystem::path backup = databasePath;
            backup += L".bak";
            std::filesystem::copy_file(databasePath, backup,
                std::filesystem::copy_options::overwrite_existing, error);
        }
        if (!MoveFileExW(temporary.c_str(), databasePath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            std::filesystem::remove(temporary, error);
            return false;
        }
        return true;
    }

    bool LoadDatabasePath(const std::filesystem::path& path,
        std::vector<RestorePoint>& output, bool& migrated)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return false;
        std::array<char, 8> magic = {};
        file.read(magic.data(), static_cast<std::streamsize>(magic.size()));
        std::uint32_t version = 0, count = 0;
        if (!file.good() || magic != DatabaseMagic ||
            !ReadValue(file, version) ||
            (version != 1 && version != 2 && version != DatabaseVersion) ||
            !ReadValue(file, count) || count > 100000) return false;
        migrated = version < DatabaseVersion;
        std::vector<RestorePoint> loaded;
        loaded.reserve(count);
        for (std::uint32_t pointIndex = 0; pointIndex < count; ++pointIndex)
        {
            RestorePoint point;
            std::uint8_t type = 0;
            std::int32_t xmlType = -1;
            std::uint32_t groupCount = 0;
            const std::uint8_t maximumType = version >= 2 ? 2 : 1;
            if (!ReadString(file, point.id) || !ReadValue(file, type) || type > maximumType ||
                !ReadString(file, point.customName) || !ReadString(file, point.xmlName) ||
                !ReadString(file, point.lineage) || !ReadString(file, point.createdUtc) ||
                !ReadValue(file, xmlType))
                return false;
            point.type = type == 2 ? GroupBackupDatabase::RestorePointType::Safety :
                type == 1 ? GroupBackupDatabase::RestorePointType::Manual :
                GroupBackupDatabase::RestorePointType::Auto;
            if (version == 1 && point.type == GroupBackupDatabase::RestorePointType::Manual &&
                Lower(Trim(point.customName)) == "pre-restore safety")
            {
                point.type = GroupBackupDatabase::RestorePointType::Safety;
            }
            point.xmlType = xmlType;
            if (version >= 3)
            {
                std::uint32_t ungroupedCount = 0;
                if (!ReadString(file, point.rootOpenTag) ||
                    !ReadValue(file, ungroupedCount) || ungroupedCount > 1000000)
                    return false;
                point.ungroupedProps.reserve(ungroupedCount);
                for (std::uint32_t propIndex = 0; propIndex < ungroupedCount; ++propIndex)
                {
                    StoredProp prop;
                    if (!ReadString(file, prop.signature) || !ReadString(file, prop.rawXml))
                        return false;
                    point.ungroupedProps.push_back(std::move(prop));
                }
            }
            if (!ReadValue(file, groupCount) || groupCount > 100000) return false;
            point.groups.reserve(groupCount);
            for (std::uint32_t groupIndex = 0; groupIndex < groupCount; ++groupIndex)
            {
                StoredGroup group;
                std::uint32_t propCount = 0;
                if (!ReadString(file, group.name) || !ReadValue(file, propCount) ||
                    propCount > 1000000) return false;
                group.props.reserve(propCount);
                for (std::uint32_t propIndex = 0; propIndex < propCount; ++propIndex)
                {
                    StoredProp prop;
                    if (!ReadString(file, prop.signature) || !ReadString(file, prop.rawXml))
                        return false;
                    group.props.push_back(std::move(prop));
                }
                point.groups.push_back(std::move(group));
            }
            loaded.push_back(std::move(point));
        }
        output = std::move(loaded);
        return true;
    }

    bool LoadDatabase()
    {
        std::vector<RestorePoint> loaded;
        bool migrated = false;
        if (LoadDatabasePath(databasePath, loaded, migrated))
        {
            restorePoints = std::move(loaded);
            if (migrated) SaveDatabase();
            return true;
        }
        std::filesystem::path backup = databasePath;
        backup += L".bak";
        if (LoadDatabasePath(backup, loaded, migrated))
        {
            restorePoints = std::move(loaded);
            // The primary is known to be unreadable. Remove only that corrupt
            // copy before recreating it so SaveDatabase does not overwrite the
            // valid recovery file with corrupt bytes.
            std::error_code error;
            std::filesystem::remove(databasePath, error);
            SaveDatabase();
            return true;
        }
        return false;
    }

    RestorePoint BuildRestorePoint(const ParsedXml& parsed,
        const std::string& path, GroupBackupDatabase::RestorePointType type,
        const std::string& customName)
    {
        RestorePoint point;
        point.id = MakeId();
        point.type = type;
        point.customName = customName;
        point.xmlName = Utf8Paths::ToUtf8(Utf8Paths::FromUtf8(path).filename());
        point.lineage = GroupBackupDatabase::NormalizeLineage(point.xmlName);
        point.createdUtc = TimestampUtc();
        point.xmlType = parsed.type;
        point.rootOpenTag = parsed.source.substr(parsed.rootOpenStart,
            parsed.rootOpenEnd - parsed.rootOpenStart + 1);
        for (const ParsedProp& parsedProp : parsed.props)
        {
            if (parsedProp.groupIndex < 0)
                point.ungroupedProps.push_back(
                    { parsedProp.signature, parsedProp.rawXml });
        }
        point.groups.reserve(parsed.groups.size());
        for (const ParsedGroup& parsedGroup : parsed.groups)
        {
            StoredGroup group;
            group.name = parsedGroup.name;
            group.props.reserve(parsedGroup.propIndices.size());
            for (size_t propIndex : parsedGroup.propIndices)
            {
                const ParsedProp& parsedProp = parsed.props[propIndex];
                group.props.push_back({ parsedProp.signature, parsedProp.rawXml });
            }
            if (!group.props.empty()) point.groups.push_back(std::move(group));
        }
        return point;
    }

    size_t StoredPropCount(const RestorePoint& point)
    {
        size_t count = 0;
        for (const StoredGroup& group : point.groups) count += group.props.size();
        return count;
    }

    size_t TotalStoredPropCount(const RestorePoint& point)
    {
        return StoredPropCount(point) + point.ungroupedProps.size();
    }

    bool SameStoredProps(const std::vector<StoredProp>& left,
        const std::vector<StoredProp>& right)
    {
        if (left.size() != right.size()) return false;
        for (size_t index = 0; index < left.size(); ++index)
            if (left[index].signature != right[index].signature) return false;
        return true;
    }

    bool SameRestoreContent(const RestorePoint& left, const RestorePoint& right)
    {
        if (left.xmlType != right.xmlType ||
            left.rootOpenTag != right.rootOpenTag ||
            !SameStoredProps(left.ungroupedProps, right.ungroupedProps) ||
            left.groups.size() != right.groups.size()) return false;
        for (size_t index = 0; index < left.groups.size(); ++index)
        {
            if (left.groups[index].name != right.groups[index].name ||
                !SameStoredProps(left.groups[index].props, right.groups[index].props))
                return false;
        }
        return true;
    }

    const RestorePoint* FindRestorePoint(const std::string& id)
    {
        for (const RestorePoint& point : restorePoints)
            if (point.id == id) return &point;
        return nullptr;
    }

    bool BuildRestoredXml(const RestorePoint& point, const ParsedXml& target,
        std::string& output, GroupBackupDatabase::RestoreStats& stats)
    {
        constexpr double PositionTolerance = 0.01;
        constexpr double RotationTolerance = 0.02;
        constexpr double ScaleTolerance = 0.00001;

        struct StoredRequest
        {
            size_t groupIndex = 0;
            const StoredProp* prop = nullptr;
            size_t currentIndex = (std::numeric_limits<size_t>::max)();
        };
        struct TolerantCandidate
        {
            size_t requestIndex = 0;
            size_t currentIndex = 0;
            double score = 0.0;
        };

        stats = {};
        std::unordered_map<std::string, std::deque<size_t>> available;
        for (size_t index = 0; index < target.props.size(); ++index)
            available[target.props[index].signature].push_back(index);
        std::vector<bool> assigned(target.props.size(), false);
        std::vector<std::vector<size_t>> grouped(point.groups.size());
        std::vector<StoredRequest> requests;

        for (size_t groupIndex = 0; groupIndex < point.groups.size(); ++groupIndex)
        {
            for (const StoredProp& stored : point.groups[groupIndex].props)
            {
                StoredRequest request;
                request.groupIndex = groupIndex;
                request.prop = &stored;
                auto found = available.find(stored.signature);
                if (found != available.end() && !found->second.empty())
                {
                    request.currentIndex = found->second.front();
                    found->second.pop_front();
                    assigned[request.currentIndex] = true;
                }
                requests.push_back(request);
            }
        }

        std::vector<PropMatchData> currentMatchData(target.props.size());
        std::unordered_map<std::string, std::vector<size_t>> currentByStableSignature;
        for (size_t currentIndex = 0; currentIndex < target.props.size(); ++currentIndex)
        {
            if (assigned[currentIndex]) continue;
            currentMatchData[currentIndex] =
                BuildPropMatchData(target.props[currentIndex].rawXml);
            if (currentMatchData[currentIndex].valid)
            {
                currentByStableSignature[currentMatchData[currentIndex].stableSignature]
                    .push_back(currentIndex);
            }
        }

        std::vector<TolerantCandidate> candidates;
        for (size_t requestIndex = 0; requestIndex < requests.size(); ++requestIndex)
        {
            StoredRequest& request = requests[requestIndex];
            if (request.currentIndex != (std::numeric_limits<size_t>::max)()) continue;
            const PropMatchData storedData = BuildPropMatchData(request.prop->rawXml);
            if (!storedData.valid) continue;
            const auto found = currentByStableSignature.find(storedData.stableSignature);
            if (found == currentByStableSignature.end()) continue;
            for (size_t currentIndex : found->second)
            {
                const PropMatchData& currentData = currentMatchData[currentIndex];
                const double positionDistance =
                    Distance3(storedData.position, currentData.position);
                const double rotationDistance =
                    RotationDistance(storedData.rotation, currentData.rotation);
                const double scaleDistance =
                    std::abs(storedData.scale - currentData.scale);
                if (positionDistance > PositionTolerance ||
                    rotationDistance > RotationTolerance ||
                    scaleDistance > ScaleTolerance)
                    continue;
                candidates.push_back({ requestIndex, currentIndex,
                    positionDistance / PositionTolerance +
                    rotationDistance / RotationTolerance +
                    scaleDistance / ScaleTolerance });
            }
        }
        std::sort(candidates.begin(), candidates.end(),
            [](const TolerantCandidate& left, const TolerantCandidate& right)
            { return left.score < right.score; });
        for (const TolerantCandidate& candidate : candidates)
        {
            StoredRequest& request = requests[candidate.requestIndex];
            if (request.currentIndex != (std::numeric_limits<size_t>::max)() ||
                assigned[candidate.currentIndex]) continue;
            request.currentIndex = candidate.currentIndex;
            assigned[candidate.currentIndex] = true;
        }

        for (const StoredRequest& request : requests)
        {
            if (request.currentIndex == (std::numeric_limits<size_t>::max)())
            {
                ++stats.missingOrModified;
                continue;
            }
            grouped[request.groupIndex].push_back(request.currentIndex);
            ++stats.matched;
        }
        for (const std::vector<size_t>& group : grouped)
            if (!group.empty()) ++stats.restoredGroups;

        stats.leftUngrouped = static_cast<size_t>(
            std::count(assigned.begin(), assigned.end(), false));
        if (stats.matched == 0 || stats.restoredGroups == 0) return false;

        const char* newline = target.source.find("\r\n") != std::string::npos
            ? "\r\n" : "\n";
        std::ostringstream body;
        bool wroteSection = false;
        for (size_t index = 0; index < target.props.size(); ++index)
        {
            if (!assigned[index])
            {
                body << "  " << target.props[index].rawXml << newline;
                wroteSection = true;
            }
        }
        for (size_t groupIndex = 0; groupIndex < grouped.size(); ++groupIndex)
        {
            if (grouped[groupIndex].empty()) continue;
            if (wroteSection) body << newline;
            body << "  <!-- " << point.groups[groupIndex].name << " -->" << newline;
            for (size_t currentIndex : grouped[groupIndex])
                body << "  " << target.props[currentIndex].rawXml << newline;
            wroteSection = true;
        }
        output = target.source.substr(0, target.rootOpenEnd + 1) + newline +
            body.str() + target.source.substr(target.rootCloseStart);
        return true;
    }

    bool WriteXmlAtomically(const std::string& path, const std::string& output,
        std::string& status)
    {
        const std::filesystem::path target = Utf8Paths::FromUtf8(path);
        std::filesystem::path temporary = target;
        temporary += L".decotools.restore.tmp";
        std::error_code error;
        std::filesystem::remove(temporary, error);
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            status = "Could not create the temporary restore file.";
            return false;
        }
        file.write(output.data(), static_cast<std::streamsize>(output.size()));
        file.flush();
        if (!file.good())
        {
            file.close();
            std::filesystem::remove(temporary, error);
            status = "The restored XML could not be written completely.";
            return false;
        }
        file.close();
        if (!MoveFileExW(temporary.c_str(), target.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            std::filesystem::remove(temporary, error);
            status = "The restored XML could not replace the original. The original was left unchanged.";
            return false;
        }
        return true;
    }

    bool RestorePointToTarget(const RestorePoint& point, const std::string& targetPath,
        bool write, GroupBackupDatabase::RestoreStats& stats, std::string& status)
    {
        ParsedXml target;
        if (!ParseFile(targetPath, target, status)) return false;
        if (target.type != point.xmlType)
        {
            status = "The restore point and target XML use different map types.";
            return false;
        }
        std::string output;
        if (!BuildRestoredXml(point, target, output, stats))
        {
            status = "No unchanged decorations from this restore point were found in the target XML.";
            return false;
        }
        if (!write)
        {
            status = "Restore preview ready.";
            return true;
        }
        if (!target.groups.empty())
        {
            std::string safetyStatus;
            if (!GroupBackupDatabase::RecordFile(targetPath, target.type,
                GroupBackupDatabase::RestorePointType::Safety,
                "Pre-Restore Safety", safetyStatus))
            {
                status = "The safety backup could not be created, so the restore was canceled. " +
                    safetyStatus;
                return false;
            }
        }
        if (!WriteXmlAtomically(targetPath, output, status)) return false;
        status = "Restored " + std::to_string(stats.restoredGroups) +
            " group(s): " + std::to_string(stats.matched) + " matched, " +
            std::to_string(stats.missingOrModified) + " missing or modified, " +
            std::to_string(stats.leftUngrouped) + " left ungrouped.";
        return true;
    }

    std::string AbsoluteKey(const std::string& path)
    {
        std::error_code error;
        std::filesystem::path absolute = std::filesystem::absolute(
            Utf8Paths::FromUtf8(path), error);
        if (error) absolute = Utf8Paths::FromUtf8(path);
        return Lower(Utf8Paths::ToUtf8(absolute.lexically_normal()));
    }

    bool RecordParsedFile(const ParsedXml& parsed, const std::string& path,
        GroupBackupDatabase::RestorePointType type, const std::string& customName,
        std::string& status)
    {
        RestorePoint point = BuildRestorePoint(parsed, path, type, Trim(customName));
        const bool containsGroups = !point.groups.empty();
        if (type == GroupBackupDatabase::RestorePointType::Auto)
        {
            const std::string pointName = Lower(point.xmlName);
            for (auto iterator = restorePoints.rbegin(); iterator != restorePoints.rend(); ++iterator)
            {
                if (iterator->type == GroupBackupDatabase::RestorePointType::Auto &&
                    iterator->xmlType == point.xmlType &&
                    Lower(iterator->xmlName) == pointName &&
                    SameRestoreContent(*iterator, point))
                {
                    status = "Automatic group backup is already current.";
                    return true;
                }
            }
        }

        const std::vector<RestorePoint> previousPoints = restorePoints;
        restorePoints.push_back(std::move(point));
        if (type == GroupBackupDatabase::RestorePointType::Auto)
        {
            const std::string lineage = restorePoints.back().lineage;
            const int pointType = restorePoints.back().xmlType;
            size_t autoCount = 0;
            for (auto iterator = restorePoints.rbegin(); iterator != restorePoints.rend(); ++iterator)
            {
                if (iterator->type == GroupBackupDatabase::RestorePointType::Auto &&
                    iterator->xmlType == pointType && iterator->lineage == lineage)
                {
                    ++autoCount;
                    if (autoCount > MaxAutomaticPointsPerLineage)
                    {
                        restorePoints.erase(std::next(iterator).base());
                        break;
                    }
                }
            }
        }
        if (!SaveDatabase())
        {
            restorePoints = previousPoints;
            status = "The group backup database could not be saved.";
            return false;
        }
        status = type == GroupBackupDatabase::RestorePointType::Safety
            ? "Created safety restore point with the complete XML."
            : type == GroupBackupDatabase::RestorePointType::Manual
                ? "Created manual restore point with the complete XML."
                : containsGroups
                    ? "Created automatic group restore point with the complete XML."
                    : "Created automatic complete-XML restore point.";
        return true;
    }
}

void GroupBackupDatabase::Initialize(const std::string& addonDirectory)
{
    databasePath = Utf8Paths::FromUtf8(addonDirectory) / L"group_backups.db";
    restorePoints.clear();
    pendingRestore = {};
    ignoredImportPaths.clear();
    idCounter = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::error_code error;
    std::filesystem::create_directories(databasePath.parent_path(), error);
    // LoadDatabase also checks the recovery copy. This matters if an interrupted
    // replace left only group_backups.db.bak behind.
    if (!LoadDatabase())
    {
        restorePoints.clear();
        SaveDatabase();
    }
}

void GroupBackupDatabase::Shutdown()
{
    pendingRestore = {};
    ignoredImportPaths.clear();
    restorePoints.clear();
    databasePath.clear();
}

bool GroupBackupDatabase::RecordFile(const std::string& path, int xmlType,
    RestorePointType type, const std::string& customName, std::string& status,
    bool allowUngroupedAutomatic)
{
    ParsedXml parsed;
    if (!ParseFile(path, parsed, status)) return false;
    if (parsed.type != xmlType)
    {
        status = "The requested backup type does not match the XML.";
        return false;
    }
    if (parsed.groups.empty() &&
        type == RestorePointType::Auto && !allowUngroupedAutomatic)
    {
        status = "This XML does not contain any named groups to back up.";
        return false;
    }
    return RecordParsedFile(parsed, path, type, customName, status);
}

GroupBackupDatabase::ImportResult GroupBackupDatabase::PrepareImport(
    const std::string& path, int expectedXmlType, bool automaticEnabled,
    bool backupUngroupedXmls)
{
    ImportResult result;
    if (!automaticEnabled) return result;
    ParsedXml target;
    if (!ParseFile(path, target, result.message))
    {
        result.action = ImportAction::Error;
        return result;
    }
    if (expectedXmlType >= 0 && target.type != expectedXmlType)
    {
        result.action = ImportAction::Error;
        result.message = "The selected folder type does not match the XML type.";
        return result;
    }
    if (!target.groups.empty())
    {
        std::string backupStatus;
        if (!RecordParsedFile(target, path, RestorePointType::Auto,
            std::string(), backupStatus))
        {
            result.message = "The XML was imported, but its automatic group backup failed: " +
                backupStatus;
        }
        else result.message = backupStatus;
        result.action = ImportAction::ExistingGroups;
        return result;
    }
    const std::string fileName = Utf8Paths::ToUtf8(Utf8Paths::FromUtf8(path).filename());
    const std::string lineage = NormalizeLineage(fileName);
    bool knownHistory = false;
    bool relatedGroupedHistory = false;
    for (const RestorePoint& point : restorePoints)
    {
        if (point.xmlType != target.type) continue;
        const bool related = Lower(point.xmlName) == Lower(fileName) ||
            point.lineage == lineage;
        if (!related) continue;
        knownHistory = true;
        if (!point.groups.empty()) relatedGroupedHistory = true;
    }
    const bool firstUngroupedImport = backupUngroupedXmls && !knownHistory;

    if (backupUngroupedXmls)
    {
        std::string backupStatus;
        if (!RecordParsedFile(target, path, RestorePointType::Auto,
            std::string(), backupStatus))
        {
            result.message = "The XML was imported, but its automatic backup failed: " +
                backupStatus;
        }
    }
    if (ignoredImportPaths.find(AbsoluteKey(path)) != ignoredImportPaths.end())
        return result;

    const RestorePoint* best = nullptr;
    RestoreStats bestStats;
    bool exact = false;
    for (auto iterator = restorePoints.rbegin(); iterator != restorePoints.rend(); ++iterator)
    {
        if (iterator->xmlType != target.type) continue;
        if (iterator->groups.empty()) continue;
        const bool candidateExact = Lower(iterator->xmlName) == Lower(fileName);
        if (!candidateExact && iterator->lineage != lineage) continue;
        if (best != nullptr && exact && !candidateExact) continue;
        std::string previewOutput;
        RestoreStats stats;
        if (!BuildRestoredXml(*iterator, target, previewOutput, stats)) continue;
        const size_t stored = StoredPropCount(*iterator);
        const double coverage = stored == 0 ? 0.0 :
            static_cast<double>(stats.matched) / static_cast<double>(stored);
        if (coverage < 0.50) continue;
        if (best == nullptr || (candidateExact && !exact) || stats.matched > bestStats.matched)
        {
            best = &*iterator;
            bestStats = stats;
            exact = candidateExact;
        }
        if (candidateExact && coverage >= 0.999) break;
    }

    if (best != nullptr)
    {
        std::string status;
        if (RestorePointToTarget(*best, path, true, result.stats, status))
        {
            result.action = ImportAction::Restored;
            result.message = status;
            ParsedXml restored;
            std::string backupStatus;
            if (ParseFile(path, restored, backupStatus) &&
                !restored.groups.empty())
            {
                if (RecordParsedFile(restored, path, RestorePointType::Auto,
                    std::string(), backupStatus))
                    result.message += " " + backupStatus;
                else
                    result.message += " Automatic backup failed: " + backupStatus;
            }
            return result;
        }
    }

    if (!relatedGroupedHistory && !firstUngroupedImport)
        return result;

    pendingRestore.active = true;
    pendingRestore.confirmationRequired = true;
    pendingRestore.targetPath = path;
    pendingRestore.targetName = fileName;
    pendingRestore.xmlType = target.type;
    result.action = ImportAction::NeedsUserChoice;
    result.message = "Confirm whether this is a new XML before restoring groups.";
    return result;
}

std::vector<GroupBackupDatabase::RestorePointSummary>
GroupBackupDatabase::GetRestorePoints(int xmlType)
{
    std::vector<RestorePointSummary> output;
    for (auto iterator = restorePoints.rbegin(); iterator != restorePoints.rend(); ++iterator)
    {
        if (xmlType >= 0 && iterator->xmlType != xmlType) continue;
        RestorePointSummary summary;
        summary.id = iterator->id;
        summary.type = iterator->type;
        summary.customName = iterator->customName;
        summary.xmlName = iterator->xmlName;
        summary.lineage = iterator->lineage;
        summary.createdUtc = iterator->createdUtc;
        summary.xmlType = iterator->xmlType;
        summary.groupCount = iterator->groups.size();
        summary.propCount = TotalStoredPropCount(*iterator);
        summary.completeXml = !iterator->rootOpenTag.empty();
        output.push_back(std::move(summary));
    }
    return output;
}

bool GroupBackupDatabase::PreviewRestore(const std::string& restorePointId,
    const std::string& targetPath, RestoreStats& stats, std::string& status)
{
    const RestorePoint* point = FindRestorePoint(restorePointId);
    if (point == nullptr)
    {
        status = "The selected restore point no longer exists.";
        return false;
    }
    return RestorePointToTarget(*point, targetPath, false, stats, status);
}

bool GroupBackupDatabase::Restore(const std::string& restorePointId,
    const std::string& targetPath, RestoreStats& stats, std::string& status)
{
    const RestorePoint* point = FindRestorePoint(restorePointId);
    if (point == nullptr)
    {
        status = "The selected restore point no longer exists.";
        return false;
    }
    return RestorePointToTarget(*point, targetPath, true, stats, status);
}

bool GroupBackupDatabase::RestoreFromXml(const std::string& sourcePath,
    const std::string& targetPath, int expectedXmlType, RestoreStats& stats,
    std::string& status)
{
    ParsedXml source;
    if (!ParseFile(sourcePath, source, status)) return false;
    if (source.type != expectedXmlType)
    {
        status = "The selected restore source uses the wrong map type.";
        return false;
    }
    if (source.groups.empty())
    {
        status = "The selected restore source does not contain groups.";
        return false;
    }
    const RestorePoint temporary = BuildRestorePoint(source, sourcePath,
        RestorePointType::Auto, std::string());
    return RestorePointToTarget(temporary, targetPath, true, stats, status);
}

bool GroupBackupDatabase::RebuildXml(const std::string& restorePointId,
    const std::string& outputPath, const std::string& legacyRootOpenTag,
    RebuildResult& result, std::string& status)
{
    result = {};
    const RestorePoint* point = FindRestorePoint(restorePointId);
    if (point == nullptr)
    {
        status = "The selected restore point no longer exists.";
        return false;
    }
    if (outputPath.empty())
    {
        status = "Enter a name for the rebuilt XML.";
        return false;
    }
    std::error_code error;
    if (std::filesystem::exists(Utf8Paths::FromUtf8(outputPath), error) && !error)
    {
        status = "An XML with that name already exists. Choose a new name so no file is overwritten.";
        return false;
    }

    auto validProp = [](const StoredProp& prop)
    {
        const size_t start = prop.rawXml.find("<prop");
        if (start == std::string::npos) return false;
        const size_t tagEnd = FindTagEnd(prop.rawXml, start);
        if (tagEnd == std::string::npos) return false;
        return IsSelfClosing(prop.rawXml, start, tagEnd) ||
            prop.rawXml.find("</prop>", tagEnd + 1) != std::string::npos;
    };

    if (point->rootOpenTag.empty() && legacyRootOpenTag.empty())
    {
        status = "This older backup needs current map information before it can be rebuilt.";
        return false;
    }
    const std::string root = point->rootOpenTag.empty()
        ? legacyRootOpenTag : point->rootOpenTag;
    std::ostringstream output;
    output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" << root << "\n";
    bool wroteSection = false;
    for (const StoredProp& prop : point->ungroupedProps)
    {
        if (!validProp(prop)) { ++result.skippedProps; continue; }
        if (!wroteSection) output << "\n";
        output << "  " << prop.rawXml << "\n";
        wroteSection = true;
        ++result.restoredProps;
    }
    for (const StoredGroup& group : point->groups)
    {
        std::vector<const StoredProp*> valid;
        for (const StoredProp& prop : group.props)
        {
            if (validProp(prop)) valid.push_back(&prop);
            else ++result.skippedProps;
        }
        if (valid.empty()) continue;
        output << "\n  <!-- " << group.name << " -->\n";
        for (const StoredProp* prop : valid)
        {
            output << "  " << prop->rawXml << "\n";
            ++result.restoredProps;
        }
        ++result.restoredGroups;
        wroteSection = true;
    }
    output << "\n</Decorations>\n";
    if (!wroteSection || result.restoredProps == 0)
    {
        status = "This restore point does not contain any valid prop data to rebuild.";
        return false;
    }
    if (!WriteXmlAtomically(outputPath, output.str(), status)) return false;
    result.completeXml = !point->rootOpenTag.empty();
    status = "Rebuilt " + std::to_string(result.restoredProps) +
        " decoration(s) in " + std::to_string(result.restoredGroups) + " group(s).";
    if (!result.completeXml)
        status += " This older backup contained grouped decorations only; current Mumble map information supplied its XML header, but original ungrouped decorations were not available.";
    if (result.skippedProps > 0)
        status += " " + std::to_string(result.skippedProps) +
            " malformed decoration record(s) could not be restored.";
    return true;
}

bool GroupBackupDatabase::PreviewRestoreFromXml(const std::string& sourcePath,
    const std::string& targetPath, int expectedXmlType, RestoreStats& stats,
    std::string& status)
{
    ParsedXml source;
    if (!ParseFile(sourcePath, source, status)) return false;
    if (source.type != expectedXmlType)
    {
        status = "The selected restore source uses the wrong map type.";
        return false;
    }
    if (source.groups.empty())
    {
        status = "The selected restore source does not contain groups.";
        return false;
    }
    const RestorePoint temporary = BuildRestorePoint(source, sourcePath,
        RestorePointType::Auto, std::string());
    return RestorePointToTarget(temporary, targetPath, false, stats, status);
}

bool GroupBackupDatabase::RenameRestorePoint(const std::string& restorePointId,
    const std::string& newName, std::string& status)
{
    RestorePoint* point = nullptr;
    for (RestorePoint& candidate : restorePoints)
        if (candidate.id == restorePointId) { point = &candidate; break; }
    if (point == nullptr)
    {
        status = "The selected restore point no longer exists.";
        return false;
    }
    if (point->type == RestorePointType::Auto)
    {
        status = "Automatic restore points cannot be renamed.";
        return false;
    }
    const std::string trimmedName = Trim(newName);
    if (trimmedName.empty())
    {
        status = "Enter a backup name before renaming it.";
        return false;
    }
    const std::string previousName = point->customName;
    point->customName = trimmedName;
    if (!SaveDatabase())
    {
        point->customName = previousName;
        status = "The group backup database could not be saved.";
        return false;
    }
    status = "Renamed the selected backup.";
    return true;
}

bool GroupBackupDatabase::DeleteRestorePoints(
    const std::vector<std::string>& restorePointIds, size_t& deletedCount,
    std::string& status)
{
    deletedCount = 0;
    if (restorePointIds.empty())
    {
        status = "Select at least one backup to delete.";
        return false;
    }
    const std::unordered_set<std::string> selected(
        restorePointIds.begin(), restorePointIds.end());
    const std::vector<RestorePoint> previousPoints = restorePoints;
    restorePoints.erase(std::remove_if(restorePoints.begin(), restorePoints.end(),
        [&](const RestorePoint& point)
        {
            const bool remove = point.type != RestorePointType::Auto &&
                selected.find(point.id) != selected.end();
            if (remove) ++deletedCount;
            return remove;
        }), restorePoints.end());
    if (deletedCount == 0)
    {
        status = "No manageable backups were selected.";
        return false;
    }
    if (!SaveDatabase())
    {
        restorePoints = previousPoints;
        deletedCount = 0;
        status = "The group backup database could not be saved.";
        return false;
    }
    status = "Deleted " + std::to_string(deletedCount) + " backup(s).";
    return true;
}

bool GroupBackupDatabase::InspectFile(const std::string& path, int& xmlType,
    size_t& groupCount, size_t& propCount)
{
    ParsedXml parsed;
    std::string error;
    if (!ParseFile(path, parsed, error)) return false;
    xmlType = parsed.type;
    groupCount = parsed.groups.size();
    propCount = parsed.props.size();
    return true;
}

GroupBackupDatabase::PendingRestore GroupBackupDatabase::GetPendingRestore()
{
    return pendingRestore;
}

void GroupBackupDatabase::ConfirmPendingRestore()
{
    pendingRestore.confirmationRequired = false;
}

void GroupBackupDatabase::IgnorePendingRestore()
{
    if (pendingRestore.active)
        ignoredImportPaths.insert(AbsoluteKey(pendingRestore.targetPath));
    pendingRestore = {};
}

void GroupBackupDatabase::ClearPendingRestore()
{
    pendingRestore = {};
}

std::string GroupBackupDatabase::NormalizeLineage(const std::string& fileName)
{
    std::string stem = Lower(Utf8Paths::ToUtf8(Utf8Paths::FromUtf8(fileName).stem()));
    bool changed = true;
    while (changed)
    {
        changed = false;
        std::string candidate = stem;
        size_t digitStart = candidate.size();
        while (digitStart > 0 &&
            std::isdigit(static_cast<unsigned char>(candidate[digitStart - 1])) != 0)
            --digitStart;
        if (digitStart < candidate.size())
        {
            if (digitStart > 0 && candidate[digitStart - 1] == '_') --digitStart;
            candidate.erase(digitStart);
        }
        constexpr const char* operations[] =
            { "_moved", "_merged", "_stripped", "_pattern" };
        for (const char* operation : operations)
            if (EndsWith(candidate, operation))
            {
                stem = candidate.substr(0,
                    candidate.size() - std::char_traits<char>::length(operation));
                changed = true;
                break;
            }
        if (changed) continue;
        constexpr const char* mapSuffixes[] =
        {
            "_hearths-glow", "_comosus-isle", "_lost-precipice",
            "_gilded-hollow", "_windswept-haven", "_isle-of-reflection"
        };
        for (const char* suffix : mapSuffixes)
            if (EndsWith(candidate, suffix))
            {
                stem = candidate.substr(0,
                    candidate.size() - std::char_traits<char>::length(suffix));
                changed = true;
                break;
            }
    }
    while (!stem.empty() && (stem.back() == '_' || stem.back() == '-' || stem.back() == ' '))
        stem.pop_back();
    return stem;
}
