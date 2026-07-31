#include "MoveToolTab.h"

#include "../../Core/AppRuntime.h"
#include "../../Core/AppSettings.h"
#include "../../Core/DecorationDatabase.h"
#include "../DecorationCounterWindow.h"
#include "../../imgui/imgui.h"
#include "../../imgui/imgui_internal.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    constexpr float DecorationScale = 0.025400052f;
    constexpr float NearClip = 0.05f;

    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct PropPosition
    {
        size_t valueStart = 0;
        size_t valueLength = 0;
        Vec3 position;
        int id = -1;
        std::string name;
    };

    struct MapInfo
    {
        unsigned liveMapId;
        unsigned xmlMapId;
        const char* mapName;
        int type;
    };

    struct XmlFileEntry
    {
        std::string name;
        std::string path;
    };

    struct Camera
    {
        Vec3 position;
        Vec3 forward;
        Vec3 up;
        Vec3 right;
        float fovRadians = 65.0f * 0.01745329251994329577f;

        bool Project(Vec3 world, ImVec2 viewport, ImVec2& screen) const;
    };

    float anchorPosition[3] = { 0.0f, 0.0f, 0.0f };

    std::string xmlSource;
    std::string importedFileName;
    std::string status = "No XML imported";
    std::vector<PropPosition> props;
    std::vector<XmlFileEntry> availableXmlFiles;
    unsigned xmlMapId = 0;
    int xmlType = -1;
    int selectedFolderType = 0;
    int selectedXmlIndex = -1;
    bool fileListInitialized = false;

    int hoveredAxis = 0;
    int activeAxis = 0;
    bool inputCaptured = false;
    bool mouseDown = false;
    bool clickPending = false;
    ImVec2 wndMousePosition(0.0f, 0.0f);
    ImVec2 dragStartMouse(0.0f, 0.0f);
    float dragStartAnchor[3] = { 0.0f, 0.0f, 0.0f };
    ImVec2 activeAxisDirection(0.0f, 0.0f);
    float activeDecoUnitsPerPixel = 0.0f;

    Vec3 Add(Vec3 a, Vec3 b)
    {
        return { a.x + b.x, a.y + b.y, a.z + b.z };
    }

    Vec3 Subtract(Vec3 a, Vec3 b)
    {
        return { a.x - b.x, a.y - b.y, a.z - b.z };
    }

    Vec3 Multiply(Vec3 value, float scale)
    {
        return { value.x * scale, value.y * scale, value.z * scale };
    }

    float Dot(Vec3 a, Vec3 b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    Vec3 Cross(Vec3 a, Vec3 b)
    {
        return
        {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        };
    }

    float Length(Vec3 value)
    {
        return std::sqrt(Dot(value, value));
    }

    Vec3 Normalize(Vec3 value, Vec3 fallback)
    {
        const float length = Length(value);
        return length <= 0.000001f ? fallback : Multiply(value, 1.0f / length);
    }

    Vec3 FromMumble(const Vector3& value)
    {
        return { value.X, value.Y, value.Z };
    }

    Vec3 DecorationToWorld(Vec3 decoration)
    {
        return
        {
            decoration.x * DecorationScale,
            -decoration.z * DecorationScale,
            decoration.y * DecorationScale
        };
    }

    Vec3 WorldToDecoration(Vec3 world)
    {
        return
        {
            world.x / DecorationScale,
            world.z / DecorationScale,
            -world.y / DecorationScale
        };
    }

    Camera CameraFromMumble(const Mumble::Data& mumble)
    {
        Camera camera;
        camera.position = FromMumble(mumble.CameraPosition);
        camera.forward = Normalize(
            FromMumble(mumble.CameraFront),
            { 0.0f, 0.0f, 1.0f }
        );

        const Vec3 worldUp = { 0.0f, 1.0f, 0.0f };
        camera.right = Normalize(
            Cross(worldUp, camera.forward),
            { 1.0f, 0.0f, 0.0f }
        );
        camera.up = Normalize(
            Cross(camera.forward, camera.right),
            worldUp
        );
        return camera;
    }

    bool Camera::Project(Vec3 world, ImVec2 viewport, ImVec2& screen) const
    {
        if (viewport.x <= 1.0f || viewport.y <= 1.0f)
        {
            return false;
        }

        const Vec3 relative = Subtract(world, position);
        const float cameraX = Dot(relative, right);
        const float cameraY = Dot(relative, up);
        const float cameraZ = Dot(relative, forward);

        if (cameraZ <= NearClip)
        {
            return false;
        }

        const float focalY =
            (viewport.y * 0.5f) / std::tan(fovRadians * 0.5f);

        screen.x = viewport.x * 0.5f + cameraX * focalY / cameraZ;
        screen.y = viewport.y * 0.5f - cameraY * focalY / cameraZ;
        return
            screen.x >= -4000.0f &&
            screen.x <= viewport.x + 4000.0f &&
            screen.y >= -4000.0f &&
            screen.y <= viewport.y + 4000.0f;
    }

    const MapInfo* FindMapInfo(unsigned mapId)
    {
        static constexpr MapInfo maps[] =
        {
            { 1558, 1558, "Hearth's Glow", 0 },
            { 1596, 1596, "Comosus Isle", 0 },

            { 1069, 1124, "Lost Precipice", 1 },
            { 1071, 1124, "Lost Precipice", 1 },
            { 1076, 1124, "Lost Precipice", 1 },
            { 1104, 1124, "Lost Precipice", 1 },
            { 1124, 1124, "Lost Precipice", 1 },
            { 1144, 1124, "Lost Precipice", 1 },

            { 1068, 1121, "Gilded Hollow", 1 },
            { 1101, 1121, "Gilded Hollow", 1 },
            { 1107, 1121, "Gilded Hollow", 1 },
            { 1108, 1121, "Gilded Hollow", 1 },
            { 1121, 1121, "Gilded Hollow", 1 },
            { 1125, 1121, "Gilded Hollow", 1 },

            { 1214, 1232, "Windswept Haven", 1 },
            { 1215, 1232, "Windswept Haven", 1 },
            { 1224, 1232, "Windswept Haven", 1 },
            { 1232, 1232, "Windswept Haven", 1 },
            { 1243, 1232, "Windswept Haven", 1 },
            { 1250, 1232, "Windswept Haven", 1 },

            { 1419, 1462, "Isle of Reflection", 1 },
            { 1426, 1462, "Isle of Reflection", 1 },
            { 1435, 1462, "Isle of Reflection", 1 },
            { 1444, 1462, "Isle of Reflection", 1 },
            { 1462, 1462, "Isle of Reflection", 1 }
        };

        for (const MapInfo& map : maps)
        {
            if (map.liveMapId == mapId)
            {
                return &map;
            }
        }

        return nullptr;
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

    bool ParseFloat3(const std::string& value, Vec3& result)
    {
        std::istringstream stream(value);
        if (!(stream >> result.x >> result.y >> result.z))
        {
            return false;
        }

        std::string extra;
        return !(stream >> extra);
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

    void ComputeAnchor()
    {
        if (props.empty())
        {
            anchorPosition[0] = 0.0f;
            anchorPosition[1] = 0.0f;
            anchorPosition[2] = 0.0f;
            return;
        }

        double sumX = 0.0;
        double sumY = 0.0;
        float bottomZ = -std::numeric_limits<float>::infinity();

        for (const PropPosition& prop : props)
        {
            sumX += prop.position.x;
            sumY += prop.position.y;
            bottomZ = (std::max)(bottomZ, prop.position.z);
        }

        anchorPosition[0] = static_cast<float>(sumX / props.size());
        anchorPosition[1] = static_cast<float>(sumY / props.size());
        anchorPosition[2] = bottomZ;
    }

    void MoveAnchorTo(const float target[3])
    {
        const Vec3 delta =
        {
            target[0] - anchorPosition[0],
            target[1] - anchorPosition[1],
            target[2] - anchorPosition[2]
        };

        for (PropPosition& prop : props)
        {
            prop.position = Add(prop.position, delta);
        }

        anchorPosition[0] = target[0];
        anchorPosition[1] = target[1];
        anchorPosition[2] = target[2];
    }

    std::string FormatFloat(float value)
    {
        if (std::fabs(value) < 0.0000005f)
        {
            value = 0.0f;
        }

        std::ostringstream stream;
        stream << std::fixed << std::setprecision(6) << value;
        std::string text = stream.str();

        while (text.size() > 1 && text.back() == '0')
        {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.')
        {
            text.pop_back();
        }
        return text;
    }

    std::string ExportBaseName(const std::string& fileName)
    {
        std::string baseName = std::filesystem::path(fileName).stem().string();
        std::string upperName = baseName;
        std::transform(
            upperName.begin(),
            upperName.end(),
            upperName.begin(),
            [](unsigned char character)
            {
                return static_cast<char>(std::toupper(character));
            }
        );

        size_t suffixStart = upperName.size();
        while (suffixStart > 0 &&
            std::isdigit(static_cast<unsigned char>(upperName[suffixStart - 1])) != 0)
        {
            --suffixStart;
        }

        constexpr const char* movedSuffix = "_MOVED";
        constexpr size_t movedSuffixLength = 6;
        if (suffixStart >= movedSuffixLength &&
            upperName.compare(
                suffixStart - movedSuffixLength,
                movedSuffixLength,
                movedSuffix
            ) == 0)
        {
            baseName.erase(suffixStart - movedSuffixLength);
        }

        return baseName;
    }

    std::string BuildUpdatedXml()
    {
        std::string output = xmlSource;
        for (size_t index = props.size(); index-- > 0;)
        {
            const PropPosition& prop = props[index];
            const std::string position =
                FormatFloat(prop.position.x) + " " +
                FormatFloat(prop.position.y) + " " +
                FormatFloat(prop.position.z);

            output.replace(prop.valueStart, prop.valueLength, position);
        }
        return output;
    }

    bool ReplaceRootAttribute(
        std::string& xml,
        const char* attribute,
        const std::string& value
    )
    {
        const size_t rootStart = xml.find("<Decorations");
        const size_t rootEnd = rootStart == std::string::npos
            ? std::string::npos
            : FindTagEnd(xml, rootStart);
        if (rootStart == std::string::npos || rootEnd == std::string::npos)
        {
            return false;
        }

        std::string oldValue;
        size_t valueStart = 0;
        size_t valueLength = 0;
        if (!ReadAttribute(
            xml,
            rootStart,
            rootEnd,
            attribute,
            oldValue,
            &valueStart,
            &valueLength
        ))
        {
            return false;
        }

        std::string encoded;
        const char quote = valueStart > 0 ? xml[valueStart - 1] : '"';
        for (const char character : value)
        {
            if (character == '&')
            {
                encoded += "&amp;";
            }
            else if (character == '<')
            {
                encoded += "&lt;";
            }
            else if (character == '"' && quote == '"')
            {
                encoded += "&quot;";
            }
            else if (character == '\'' && quote == '\'')
            {
                encoded += "&apos;";
            }
            else
            {
                encoded += character;
            }
        }

        xml.replace(valueStart, valueLength, encoded);
        return true;
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

        std::filesystem::directory_iterator iterator(directory, error);
        const std::filesystem::directory_iterator end;
        while (!error && iterator != end)
        {
            const std::filesystem::directory_entry& entry = *iterator;
            if (entry.is_regular_file(error) && !error &&
                HasXmlExtension(entry.path()))
            {
                availableXmlFiles.push_back(
                    { entry.path().filename().string(), entry.path().string() }
                );
            }

            iterator.increment(error);
        }

        if (error)
        {
            availableXmlFiles.clear();
            status = "The XML folder could not be read.";
            return;
        }

        std::sort(
            availableXmlFiles.begin(),
            availableXmlFiles.end(),
            [](const XmlFileEntry& left, const XmlFileEntry& right)
            {
                std::string leftName = left.name;
                std::string rightName = right.name;
                std::transform(
                    leftName.begin(),
                    leftName.end(),
                    leftName.begin(),
                    [](unsigned char value)
                    {
                        return static_cast<char>(std::tolower(value));
                    }
                );
                std::transform(
                    rightName.begin(),
                    rightName.end(),
                    rightName.begin(),
                    [](unsigned char value)
                    {
                        return static_cast<char>(std::tolower(value));
                    }
                );
                return leftName < rightName;
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
            : "Found " + std::to_string(availableXmlFiles.size()) +
                (availableXmlFiles.size() == 1 ? " XML file." : " XML files.");
    }

    void InitializeXmlList()
    {
        if (fileListInitialized)
        {
            return;
        }

        RefreshXmlList();
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
        std::string source = contents.str();

        const size_t rootStart = source.find("<Decorations");
        const size_t rootEnd = rootStart == std::string::npos
            ? std::string::npos
            : FindTagEnd(source, rootStart);
        if (rootStart == std::string::npos || rootEnd == std::string::npos)
        {
            status = "Invalid XML: missing the <Decorations> tag.";
            return false;
        }

        std::string typeText;
        if (!ReadAttribute(source, rootStart, rootEnd, "type", typeText) ||
            (typeText != "0" && typeText != "1"))
        {
            status = "Invalid XML: Decorations type must be 0 or 1.";
            return false;
        }

        std::string mapIdText;
        if (!ReadAttribute(source, rootStart, rootEnd, "mapId", mapIdText))
        {
            status = "Invalid XML: missing Decorations mapId.";
            return false;
        }

        unsigned parsedMapId = 0;
        try
        {
            parsedMapId = static_cast<unsigned>(std::stoul(mapIdText));
        }
        catch (...)
        {
            status = "Invalid XML: Decorations mapId is not a number.";
            return false;
        }

        std::vector<PropPosition> parsedProps;
        size_t searchPosition = rootEnd;
        while (true)
        {
            const size_t propStart = source.find("<prop", searchPosition);
            if (propStart == std::string::npos)
            {
                break;
            }

            const size_t propEnd = FindTagEnd(source, propStart);
            if (propEnd == std::string::npos)
            {
                status = "Invalid XML: an unfinished <prop> tag was found.";
                return false;
            }

            std::string positionText;
            std::string idText;
            PropPosition prop;
            if (ReadAttribute(
                source,
                propStart,
                propEnd,
                "pos",
                positionText,
                &prop.valueStart,
                &prop.valueLength
            ))
            {
                if (!ParseFloat3(positionText, prop.position))
                {
                    status = "Invalid XML: a decoration has an invalid pos value.";
                    return false;
                }
                if (ReadAttribute(source, propStart, propEnd, "id", idText))
                {
                    try { prop.id = std::stoi(idText); }
                    catch (...) { prop.id = -1; }
                }
                const char* databaseName =
                    DecorationDatabase::FindNameById(prop.id, typeText == "1" ? 1 : 0);
                prop.name = databaseName == nullptr
                    ? "Unknown Decoration"
                    : databaseName;
                parsedProps.push_back(prop);
            }

            searchPosition = propEnd + 1;
        }

        if (parsedProps.empty())
        {
            status = "Invalid XML: no positioned <prop> entries were found.";
            return false;
        }

        xmlSource = std::move(source);
        props = std::move(parsedProps);
        xmlMapId = parsedMapId;
        xmlType = typeText == "0" ? 0 : 1;
        importedFileName = std::filesystem::path(path).filename().string();
        ComputeAnchor();

        std::map<int, DecorationCounterWindow::Requirement> countById;
        for (const PropPosition& prop : props)
        {
            auto& item = countById[prop.id];
            item.id = prop.id;
            item.name = prop.name;
            ++item.required;
        }
        std::vector<DecorationCounterWindow::Requirement> counterItems;
        for (const auto& [id, item] : countById)
        {
            static_cast<void>(id);
            counterItems.push_back(item);
        }
        DecorationCounterWindow::SetRequirements(
            importedFileName,
            xmlType,
            counterItems
        );

        status =
            "Loaded " + std::to_string(props.size()) + " decorations (" +
            (xmlType == 0 ? "Homestead" : "Guild Hall") + ").";
        return true;
    }

    void MoveToCharacter()
    {
        Mumble::Data* mumble = AppRuntime::GetMumble();
        if (mumble == nullptr || mumble->Context.MapID == 0)
        {
            status = "Character position is unavailable. Load into a map first.";
            return;
        }

        const Vec3 target = WorldToDecoration(FromMumble(mumble->AvatarPosition));
        const float targetArray[3] = { target.x, target.y, target.z };
        MoveAnchorTo(targetArray);
        status =
            "Moved the decoration group to the character on map " +
            std::to_string(mumble->Context.MapID) + ".";
    }

    void ExportXml()
    {
        Mumble::Data* mumble = AppRuntime::GetMumble();
        if (mumble == nullptr || mumble->Context.MapID == 0)
        {
            status = "Load into the destination map before exporting.";
            return;
        }

        const MapInfo* destinationMap = FindMapInfo(mumble->Context.MapID);
        if (destinationMap == nullptr)
        {
            status =
                "The current map is not a supported Homestead or Guild Hall.";
            return;
        }

        const std::string baseName = ExportBaseName(importedFileName);

        std::string output = BuildUpdatedXml();
        const bool headerUpdated =
            ReplaceRootAttribute(
                output,
                "mapId",
                std::to_string(destinationMap->xmlMapId)
            ) &&
            ReplaceRootAttribute(
                output,
                "mapName",
                destinationMap->mapName
            ) &&
            ReplaceRootAttribute(
                output,
                "type",
                std::to_string(destinationMap->type)
            );

        if (!headerUpdated)
        {
            status = "Could not update the destination map information.";
            return;
        }

        const std::string destinationFolder =
            FolderForType(destinationMap->type);
        if (destinationFolder.empty())
        {
            status = "Set the destination XML folder path in Settings first.";
            return;
        }

        std::error_code error;
        std::filesystem::create_directories(destinationFolder, error);
        if (error)
        {
            status = "The destination XML folder could not be created.";
            return;
        }

        std::filesystem::path selectedFile;
        for (unsigned index = 1; ; ++index)
        {
            const std::string candidateName =
                baseName + "_MOVED" + std::to_string(index) + ".xml";
            const std::filesystem::path candidate =
                std::filesystem::path(destinationFolder) / candidateName;

            if (!std::filesystem::exists(candidate, error))
            {
                if (error)
                {
                    status = "The destination XML folder could not be checked.";
                    return;
                }

                selectedFile = candidate;
                break;
            }
        }

        std::ofstream file(selectedFile, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            status = "Could not create the exported XML file.";
            return;
        }

        file.write(output.data(), static_cast<std::streamsize>(output.size()));
        if (!file.good())
        {
            status = "The exported XML could not be written completely.";
            return;
        }

        if (selectedFolderType == destinationMap->type)
        {
            RefreshXmlList();
        }

        status =
            "Exported " +
            selectedFile.filename().string() +
            " for " + destinationMap->mapName + ".";
    }

    float DistanceToSegment(ImVec2 point, ImVec2 start, ImVec2 end)
    {
        const float x = end.x - start.x;
        const float y = end.y - start.y;
        const float lengthSquared = x * x + y * y;
        if (lengthSquared <= 0.000001f)
        {
            const float dx = point.x - start.x;
            const float dy = point.y - start.y;
            return std::sqrt(dx * dx + dy * dy);
        }

        float amount =
            ((point.x - start.x) * x + (point.y - start.y) * y) /
            lengthSquared;
        amount = (std::max)(0.0f, (std::min)(1.0f, amount));

        const float closestX = start.x + amount * x;
        const float closestY = start.y + amount * y;
        const float dx = point.x - closestX;
        const float dy = point.y - closestY;
        return std::sqrt(dx * dx + dy * dy);
    }

    void DrawArrow(
        ImDrawList* draw,
        ImVec2 start,
        ImVec2 end,
        ImU32 color,
        float thickness
    )
    {
        draw->AddLine(start, end, color, thickness);
        const float dx = end.x - start.x;
        const float dy = end.y - start.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length <= 1.0f)
        {
            return;
        }

        const float unitX = dx / length;
        const float unitY = dy / length;
        const float sideX = -unitY;
        const float sideY = unitX;
        const float head = 9.0f;

        draw->AddTriangleFilled(
            end,
            ImVec2(end.x - unitX * head + sideX * head * 0.5f,
                end.y - unitY * head + sideY * head * 0.5f),
            ImVec2(end.x - unitX * head - sideX * head * 0.5f,
                end.y - unitY * head - sideY * head * 0.5f),
            color
        );
    }

    void DrawPreview(const Camera& camera, ImVec2 viewport, ImDrawList* draw)
    {
        AppSettings::Data& settings = AppSettings::Get();

        Vec3 minimum =
        {
            std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::infinity()
        };
        Vec3 maximum =
        {
            -std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity()
        };

        for (const PropPosition& prop : props)
        {
            minimum.x = (std::min)(minimum.x, prop.position.x);
            minimum.y = (std::min)(minimum.y, prop.position.y);
            minimum.z = (std::min)(minimum.z, prop.position.z);
            maximum.x = (std::max)(maximum.x, prop.position.x);
            maximum.y = (std::max)(maximum.y, prop.position.y);
            maximum.z = (std::max)(maximum.z, prop.position.z);

            if (settings.showDecorationPoints)
            {
                ImVec2 point;
                if (camera.Project(DecorationToWorld(prop.position), viewport, point))
                {
                    draw->AddCircleFilled(
                        point,
                        settings.pointSize,
                        ImGui::ColorConvertFloat4ToU32(ImVec4(
                            settings.pointColor[0],
                            settings.pointColor[1],
                            settings.pointColor[2],
                            settings.pointColor[3]
                        )),
                        12
                    );
                }
            }
        }

        const Vec3 decorationCorners[8] =
        {
            { minimum.x, minimum.y, minimum.z },
            { maximum.x, minimum.y, minimum.z },
            { maximum.x, maximum.y, minimum.z },
            { minimum.x, maximum.y, minimum.z },
            { minimum.x, minimum.y, maximum.z },
            { maximum.x, minimum.y, maximum.z },
            { maximum.x, maximum.y, maximum.z },
            { minimum.x, maximum.y, maximum.z }
        };

        ImVec2 corners[8];
        bool visible[8] = {};
        for (int index = 0; index < 8; ++index)
        {
            visible[index] = camera.Project(
                DecorationToWorld(decorationCorners[index]),
                viewport,
                corners[index]
            );
        }

        const int faces[6][4] =
        {
            { 0, 1, 2, 3 },
            { 4, 7, 6, 5 },
            { 0, 4, 5, 1 },
            { 1, 5, 6, 2 },
            { 2, 6, 7, 3 },
            { 3, 7, 4, 0 }
        };

        if (settings.showSolidFaces)
        {
            const ImU32 faceColor = ImGui::ColorConvertFloat4ToU32(ImVec4(
                settings.faceColor[0],
                settings.faceColor[1],
                settings.faceColor[2],
                settings.faceColor[3]
            ));

            for (const auto& face : faces)
            {
                if (visible[face[0]] && visible[face[1]] &&
                    visible[face[2]] && visible[face[3]])
                {
                    draw->AddTriangleFilled(
                        corners[face[0]], corners[face[1]], corners[face[2]], faceColor);
                    draw->AddTriangleFilled(
                        corners[face[0]], corners[face[2]], corners[face[3]], faceColor);
                }
            }
        }

        if (settings.showBoundingBox)
        {
            const ImU32 boxColor = ImGui::ColorConvertFloat4ToU32(ImVec4(
                settings.boxColor[0],
                settings.boxColor[1],
                settings.boxColor[2],
                settings.boxColor[3]
            ));
            const int edges[12][2] =
            {
                { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
                { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
                { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
            };

            for (const auto& edge : edges)
            {
                if (visible[edge[0]] && visible[edge[1]])
                {
                    draw->AddLine(corners[edge[0]], corners[edge[1]], boxColor, 2.0f);
                }
            }
        }
    }

    void DrawManipulator(const Camera& camera, ImVec2 viewport, ImDrawList* draw)
    {
        const Vec3 originDecoration =
        {
            anchorPosition[0],
            anchorPosition[1],
            anchorPosition[2]
        };
        const Vec3 originWorld = DecorationToWorld(originDecoration);

        ImVec2 originScreen;
        if (!camera.Project(originWorld, viewport, originScreen))
        {
            hoveredAxis = 0;
            return;
        }

        const float distanceToCamera = Length(Subtract(originWorld, camera.position));
        const float axisWorldLength = (std::max)(1.5f, distanceToCamera * 0.035f);
        const Vec3 worldAxes[3] =
        {
            { axisWorldLength, 0.0f, 0.0f },
            { 0.0f, 0.0f, axisWorldLength },
            { 0.0f, axisWorldLength, 0.0f }
        };

        ImVec2 axisScreen[3];
        bool axisVisible[3] = {};
        for (int index = 0; index < 3; ++index)
        {
            axisVisible[index] = camera.Project(
                Add(originWorld, worldAxes[index]),
                viewport,
                axisScreen[index]
            );
        }

        ImVec2 mouse = inputCaptured || activeAxis != 0
            ? wndMousePosition
            : ImGui::GetIO().MousePos;

        hoveredAxis = 0;
        float bestDistance = 38.0f;
        if (activeAxis == 0 && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow))
        {
            for (int index = 0; index < 3; ++index)
            {
                if (!axisVisible[index])
                {
                    continue;
                }

                const float distance = DistanceToSegment(
                    mouse,
                    originScreen,
                    axisScreen[index]
                );
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    hoveredAxis = index + 1;
                }
            }
        }

        const ImU32 baseColors[3] =
        {
            ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.18f, 0.15f, 1.0f)),
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.20f, 0.90f, 0.25f, 1.0f)),
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.25f, 0.55f, 1.0f, 1.0f))
        };
        const ImU32 highlight =
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.95f, 1.0f, 1.0f));

        for (int index = 0; index < 3; ++index)
        {
            if (!axisVisible[index])
            {
                continue;
            }

            const bool selected =
                hoveredAxis == index + 1 ||
                activeAxis == index + 1;
            DrawArrow(
                draw,
                originScreen,
                axisScreen[index],
                selected ? highlight : baseColors[index],
                selected ? 5.0f : 3.0f
            );
        }

        draw->AddCircleFilled(
            originScreen,
            5.0f,
            ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.95f)),
            12
        );

        if (hoveredAxis != 0 || activeAxis != 0)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::GetIO().WantCaptureMouse = true;
        }

        const bool clicked =
            clickPending ||
            (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow));
        clickPending = false;

        if (clicked && activeAxis == 0 && hoveredAxis != 0)
        {
            activeAxis = hoveredAxis;
            inputCaptured = true;
            dragStartMouse = mouse;
            dragStartAnchor[0] = anchorPosition[0];
            dragStartAnchor[1] = anchorPosition[1];
            dragStartAnchor[2] = anchorPosition[2];

            const ImVec2 end = axisScreen[activeAxis - 1];
            const float x = end.x - originScreen.x;
            const float y = end.y - originScreen.y;
            const float screenLength = std::sqrt(x * x + y * y);
            if (screenLength > 1.0f)
            {
                activeAxisDirection = ImVec2(x / screenLength, y / screenLength);
                activeDecoUnitsPerPixel =
                    axisWorldLength / screenLength / DecorationScale;
                if (activeAxis == 3)
                {
                    activeDecoUnitsPerPixel = -activeDecoUnitsPerPixel;
                }
            }
        }

        if (activeAxis != 0 && mouseDown)
        {
            const float mouseX = mouse.x - dragStartMouse.x;
            const float mouseY = mouse.y - dragStartMouse.y;
            const float pixels =
                mouseX * activeAxisDirection.x +
                mouseY * activeAxisDirection.y;

            float target[3] =
            {
                dragStartAnchor[0],
                dragStartAnchor[1],
                dragStartAnchor[2]
            };
            target[activeAxis - 1] += pixels * activeDecoUnitsPerPixel;
            MoveAnchorTo(target);
            status = "Moved the decoration group with the scene manipulator.";
        }

        if (activeAxis != 0 && !mouseDown && inputCaptured)
        {
            activeAxis = 0;
            inputCaptured = false;
        }
    }
}

void MoveToolTab::Render()
{
    AppSettings::Data& settings = AppSettings::Get();
    const bool hasXml = !props.empty();
    InitializeXmlList();

    RenderSectionHeading("Import");

    ImGui::TextWrapped(
        "Save the current decoration group you are working on in-game and import it here to edit. Once you are satisfied with the new position, Hit the Export button and in-game you can re-import the newly saved file."
    );

    ImGui::Spacing();
    ImGui::Text("Import Decoration XML");

    if (ImGui::RadioButton("Homestead", &selectedFolderType, 0))
    {
        RefreshXmlList();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Guild Hall", &selectedFolderType, 1))
    {
        RefreshXmlList();
    }

    const bool hasSelection =
        selectedXmlIndex >= 0 &&
        selectedXmlIndex < static_cast<int>(availableXmlFiles.size());
    const char* selectedName = hasSelection
        ? availableXmlFiles[static_cast<size_t>(selectedXmlIndex)].name.c_str()
        : "No XML files available";

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##XmlFileList", selectedName))
    {
        for (size_t index = 0; index < availableXmlFiles.size(); ++index)
        {
            const bool selected =
                selectedXmlIndex == static_cast<int>(index);
            if (ImGui::Selectable(
                availableXmlFiles[index].name.c_str(),
                selected
            ))
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

    const float actionWidth =
        (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) *
        0.5f;
    if (ImGui::Button("Refresh List", ImVec2(actionWidth, 0.0f)))
    {
        RefreshXmlList();
    }
    ImGui::SameLine();
    if (hasSelection)
    {
        if (ImGui::Button("Import Selected", ImVec2(actionWidth, 0.0f)))
        {
            ImportXml(
                availableXmlFiles[static_cast<size_t>(selectedXmlIndex)].path
            );
        }
    }
    else
    {
        RenderDisabledButton(
            "Import Selected",
            ImVec2(actionWidth, 0.0f)
        );
    }

    ImGui::TextDisabled("%s", status.c_str());
    ImGui::Spacing();

    ImGui::Dummy(ImVec2(0.0f, 16.0f));
    RenderSectionHeading("Position");

    ImGui::SetNextItemWidth(360.0f);
    if (hasXml)
    {
        float editedAnchor[3] =
        {
            anchorPosition[0],
            anchorPosition[1],
            anchorPosition[2]
        };
        if (ImGui::InputFloat3("##AnchorPosition", editedAnchor, "%.3f"))
        {
            MoveAnchorTo(editedAnchor);
            status = "Updated the decoration group position.";
        }
    }
    else
    {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
        ImGui::InputFloat3("##AnchorPosition", anchorPosition, "%.3f");
        ImGui::PopStyleVar();
        ImGui::PopItemFlag();
    }

    if (hasXml)
    {
        if (ImGui::Button("Move to Character"))
        {
            MoveToCharacter();
        }
    }
    else
    {
        RenderDisabledButton("Move to Character");
    }

    ImGui::Dummy(ImVec2(0.0f, 16.0f));
    RenderSectionHeading("Preview");

    if (ImGui::Checkbox("Show Bounding Box", &settings.showBoundingBox))
    {
        AppSettings::MarkDirty();
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Show Solid Faces", &settings.showSolidFaces))
    {
        AppSettings::MarkDirty();
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Show Decoration Points", &settings.showDecorationPoints))
    {
        AppSettings::MarkDirty();
    }

    if (ImGui::ColorEdit4(
        "Box Color",
        settings.boxColor,
        ImGuiColorEditFlags_NoInputs
    ))
    {
        AppSettings::MarkDirty();
    }
    if (ImGui::ColorEdit4(
        "Face Color",
        settings.faceColor,
        ImGuiColorEditFlags_NoInputs
    ))
    {
        AppSettings::MarkDirty();
    }
    if (ImGui::ColorEdit4(
        "Point Color",
        settings.pointColor,
        ImGuiColorEditFlags_NoInputs
    ))
    {
        AppSettings::MarkDirty();
    }

    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::SliderFloat(
        "Point Size",
        &settings.pointSize,
        1.0f,
        12.0f,
        "%.0f px"
    ))
    {
        AppSettings::MarkDirty();
    }

    ImGui::Dummy(ImVec2(0.0f, 16.0f));
    if (hasXml)
    {
        if (ImGui::Button("Export Updated XML"))
        {
            ExportXml();
        }
    }
    else
    {
        RenderDisabledButton("Export Updated XML");
    }
}

void MoveToolTab::RenderOverlay()
{
    if (props.empty())
    {
        hoveredAxis = 0;
        activeAxis = 0;
        inputCaptured = false;
        return;
    }

    Mumble::Data* mumble = AppRuntime::GetMumble();
    if (mumble == nullptr || mumble->Context.MapID == 0)
    {
        hoveredAxis = 0;
        activeAxis = 0;
        inputCaptured = false;
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    if (!inputCaptured && io.MousePos.x >= 0.0f && io.MousePos.y >= 0.0f)
    {
        wndMousePosition = io.MousePos;
        mouseDown = io.MouseDown[0];
    }

    const ImVec2 viewport = io.DisplaySize;
    const Camera camera = CameraFromMumble(*mumble);
    ImDrawList* draw = ImGui::GetBackgroundDrawList();

    DrawPreview(camera, viewport, draw);
    DrawManipulator(camera, viewport, draw);
}

void MoveToolTab::ClearImportedData()
{
    DecorationCounterWindow::Clear();
    std::string().swap(xmlSource);
    std::string().swap(importedFileName);
    std::vector<PropPosition>().swap(props);

    anchorPosition[0] = 0.0f;
    anchorPosition[1] = 0.0f;
    anchorPosition[2] = 0.0f;
    xmlMapId = 0;
    xmlType = -1;
    selectedXmlIndex = -1;
    status = "No XML imported";

    hoveredAxis = 0;
    activeAxis = 0;
    inputCaptured = false;
    mouseDown = false;
    clickPending = false;
    activeDecoUnitsPerPixel = 0.0f;
}

UINT MoveToolTab::WndProc(HWND, UINT message, WPARAM, LPARAM lParam)
{
    switch (message)
    {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
        wndMousePosition = ImVec2(
            static_cast<float>(static_cast<short>(LOWORD(lParam))),
            static_cast<float>(static_cast<short>(HIWORD(lParam)))
        );
        break;
    default:
        return 1;
    }

    const bool canClaim =
        inputCaptured ||
        activeAxis != 0 ||
        hoveredAxis != 0;

    if (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK)
    {
        if (canClaim)
        {
            inputCaptured = true;
            mouseDown = true;
            clickPending = true;
            return 0;
        }
    }
    else if (message == WM_LBUTTONUP)
    {
        mouseDown = false;
        if (inputCaptured || activeAxis != 0)
        {
            inputCaptured = false;
            activeAxis = 0;
            return 0;
        }
    }
    else if (message == WM_MOUSEMOVE && (inputCaptured || activeAxis != 0))
    {
        return 0;
    }

    return 1;
}
