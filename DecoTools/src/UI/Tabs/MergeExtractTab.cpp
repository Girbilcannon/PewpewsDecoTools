// Pewpew's Deco Tools - XML Merge, Group, and Extraction Tool
// Merges layouts, interactively groups decoration points while preserving existing
// group order, safely rewrites source XML, and extracts named groups.

#include "MergeExtractTab.h"

#include "../../Core/AppRuntime.h"
#include "../../Core/AppSettings.h"
#include "../../Core/DecorationDatabase.h"
#include "../../Core/GroupBackupDatabase.h"
#include "../../Core/Utf8Paths.h"
#include "../../Core/XmlFileUtils.h"
#include "../../imgui/imgui.h"
#include "../../imgui/imgui_internal.h"
#include "../DecorationCounterWindow.h"
#include "../XmlComboHelpers.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    constexpr float DecorationScale = 0.025400052f;
    constexpr float NearClip = 0.05f;
    constexpr float DefaultFovRadians = 0.872664626f;

    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    using XmlFileEntry = XmlFileUtils::Entry;

    struct Prop
    {
        size_t start = 0;
        size_t end = 0;
        int id = -1;
        std::string name;
        Vec3 position;
        bool hasPosition = false;
        int groupIndex = -1;
        bool selected = false;
    };

    struct XmlDocument
    {
        std::string source;
        std::string path;
        std::string fileName;
        std::string mapName;
        int type = -1;
        size_t rootOpenEnd = 0;
        size_t rootCloseStart = 0;
        std::vector<Prop> props;
    };

    struct Group
    {
        std::string name;
        size_t start = 0;
        size_t end = 0;
        std::vector<Prop> props;
        bool selected = false;
    };

    struct Camera
    {
        Vec3 position;
        Vec3 forward;
        Vec3 up;
        Vec3 right;
        float fovRadians = DefaultFovRadians;

        bool Project(Vec3 world, ImVec2 viewport, ImVec2& screen) const;
    };

    int operation = 0;
    int selectedFolderType = 0;
    int baseXmlIndex = -1;
    int extractXmlIndex = -1;
    int groupXmlIndex = -1;
    bool fileListInitialized = false;
    bool listedSubFolders = false;
    std::vector<XmlFileEntry> availableXmlFiles;
    std::vector<unsigned char> additionalSelected;
    std::vector<XmlDocument> mergeDocuments;
    XmlDocument extractDocument;
    std::vector<Group> extractGroups;
    XmlDocument groupDocument;
    std::vector<Group> groups;
    std::array<char, 128> groupName = {};
    bool hideGrouped = false;
    bool marqueeMode = false;
    int visibilityDistance = 100;
    bool groupInputCaptured = false;
    bool groupMouseDown = false;
    bool groupReleasePending = false;
    bool groupRightClickPending = false;
    int hoveredGroupProp = -1;
    ImVec2 groupMousePosition(0.0f, 0.0f);
    ImVec2 marqueeStart(0.0f, 0.0f);
    ImVec2 marqueeEnd(0.0f, 0.0f);
    std::string status = "No XML imported";
    std::string report;

    Vec3 Subtract(Vec3 left, Vec3 right)
    {
        return { left.x - right.x, left.y - right.y, left.z - right.z };
    }

    Vec3 Multiply(Vec3 value, float scale)
    {
        return { value.x * scale, value.y * scale, value.z * scale };
    }

    float Dot(Vec3 left, Vec3 right)
    {
        return left.x * right.x + left.y * right.y + left.z * right.z;
    }

    Vec3 Cross(Vec3 left, Vec3 right)
    {
        return
        {
            left.y * right.z - left.z * right.y,
            left.z * right.x - left.x * right.z,
            left.x * right.y - left.y * right.x
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

    Vec3 FromMumble(const Mumble::Vector3& value)
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
        camera.up = Normalize(Cross(camera.forward, camera.right), worldUp);

        const Mumble::Identity* identity = AppRuntime::GetMumbleIdentity();
        if (identity != nullptr && std::isfinite(identity->FOV) &&
            identity->FOV > 0.1f && identity->FOV < 3.0f)
        {
            camera.fovRadians = identity->FOV;
        }
        return camera;
    }

    bool Camera::Project(Vec3 world, ImVec2 viewport, ImVec2& screen) const
    {
        if (viewport.x <= 1.0f || viewport.y <= 1.0f) return false;

        const Vec3 relative = Subtract(world, position);
        const float cameraX = Dot(relative, right);
        const float cameraY = Dot(relative, up);
        const float cameraZ = Dot(relative, forward);
        if (cameraZ <= NearClip) return false;

        const float focalY =
            (viewport.y * 0.5f) / std::tan(fovRadians * 0.5f);
        screen.x = viewport.x * 0.5f + cameraX * focalY / cameraZ;
        screen.y = viewport.y * 0.5f - cameraY * focalY / cameraZ;
        return
            screen.x >= -4000.0f && screen.x <= viewport.x + 4000.0f &&
            screen.y >= -4000.0f && screen.y <= viewport.y + 4000.0f;
    }

    bool ParseFloat3(const std::string& value, Vec3& result)
    {
        std::istringstream stream(value);
        if (!(stream >> result.x >> result.y >> result.z)) return false;
        std::string extra;
        return !(stream >> extra);
    }

    std::string Trim(std::string value)
    {
        while (!value.empty() &&
            std::isspace(static_cast<unsigned char>(value.front())) != 0)
        {
            value.erase(value.begin());
        }
        while (!value.empty() &&
            std::isspace(static_cast<unsigned char>(value.back())) != 0)
        {
            value.pop_back();
        }
        return value;
    }

    void RenderSectionHeading(const char* label)
    {
        ImGui::SetWindowFontScale(1.2f);
        ImGui::Text("%s", label);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Separator();
    }

    void RenderDisabledButton(
        const char* label,
        const ImVec2& size = ImVec2(0.0f, 0.0f)
    )
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

    void ClearLoaded()
    {
        std::vector<XmlDocument>().swap(mergeDocuments);
        extractDocument = XmlDocument{};
        std::vector<Group>().swap(extractGroups);
        groupDocument = XmlDocument{};
        std::vector<Group>().swap(groups);
        groupName.fill('\0');
        hideGrouped = false;
        marqueeMode = false;
        visibilityDistance = 100;
        groupInputCaptured = false;
        groupMouseDown = false;
        groupReleasePending = false;
        groupRightClickPending = false;
        hoveredGroupProp = -1;
        std::string().swap(report);
        DecorationCounterWindow::Clear();
        status = "No XML imported";
    }

    void RefreshXmlList()
    {
        ClearLoaded();
        availableXmlFiles.clear();
        additionalSelected.clear();
        baseXmlIndex = -1;
        extractXmlIndex = -1;
        groupXmlIndex = -1;
        fileListInitialized = true;
        listedSubFolders = AppSettings::Get().showXmlsFromSubFolders;

        const std::string folder = FolderForType(selectedFolderType);
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
        additionalSelected.assign(availableXmlFiles.size(), false);
        if (!availableXmlFiles.empty())
        {
            baseXmlIndex = 0;
            extractXmlIndex = 0;
            groupXmlIndex = 0;
        }
        status = availableXmlFiles.empty()
            ? "No XML files found in the selected folder."
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

    bool IsSelfClosingTag(
        const std::string& source,
        size_t tagStart,
        size_t tagEnd
    )
    {
        if (tagEnd <= tagStart || tagEnd > source.size()) return false;
        size_t position = tagEnd;
        while (position > tagStart &&
            std::isspace(static_cast<unsigned char>(source[position - 1])) != 0)
        {
            --position;
        }
        return position > tagStart && source[position - 1] == '/';
    }

    bool ReadAttribute(
        const std::string& source,
        size_t tagStart,
        size_t tagEnd,
        const char* attribute,
        std::string& value
    )
    {
        const std::string name(attribute);
        size_t position = tagStart;
        while (true)
        {
            position = source.find(name, position);
            if (position == std::string::npos || position >= tagEnd) return false;
            const bool validBefore =
                position == tagStart ||
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
            {
                return false;
            }
            const char quote = source[equals];
            const size_t start = equals + 1;
            const size_t end = source.find(quote, start);
            if (end == std::string::npos || end > tagEnd) return false;
            value = source.substr(start, end - start);
            return true;
        }
    }

    bool LoadXml(const std::string& path, XmlDocument& document, std::string& error)
    {
        const GroupBackupDatabase::ImportResult groupRestore =
            GroupBackupDatabase::PrepareImport(
                path,-1,AppSettings::Get().automaticGroupBackupRestore,
                AppSettings::Get().backupUngroupedXmls);
        if (groupRestore.action == GroupBackupDatabase::ImportAction::NeedsUserChoice ||
            groupRestore.action == GroupBackupDatabase::ImportAction::Error)
        {
            error = groupRestore.message;
            return false;
        }
        const std::filesystem::path nativePath = Utf8Paths::FromUtf8(path);
        std::ifstream file(nativePath, std::ios::binary);
        if (!file.is_open())
        {
            error = "Could not open " +
                Utf8Paths::ToUtf8(nativePath.filename()) + ".";
            return false;
        }
        std::ostringstream contents;
        contents << file.rdbuf();
        document = XmlDocument{};
        document.source = contents.str();
        document.path = path;
        document.fileName = Utf8Paths::ToUtf8(nativePath.filename());

        const size_t rootStart = document.source.find("<Decorations");
        const size_t rootEnd = rootStart == std::string::npos
            ? std::string::npos : FindTagEnd(document.source, rootStart);
        const size_t rootClose = document.source.rfind("</Decorations>");
        if (rootStart == std::string::npos ||
            rootEnd == std::string::npos ||
            rootClose == std::string::npos)
        {
            error = document.fileName + " is not a valid Decorations XML.";
            return false;
        }
        document.rootOpenEnd = rootEnd;
        document.rootCloseStart = rootClose;

        std::string typeText;
        if (!ReadAttribute(document.source, rootStart, rootEnd, "type", typeText) ||
            (typeText != "0" && typeText != "1"))
        {
            error = document.fileName + " has an invalid map type.";
            return false;
        }
        document.type = typeText == "1" ? 1 : 0;
        ReadAttribute(document.source, rootStart, rootEnd, "mapName", document.mapName);

        size_t search = rootEnd + 1;
        while (true)
        {
            const size_t propStart = document.source.find("<prop", search);
            if (propStart == std::string::npos || propStart >= rootClose) break;
            const size_t propEnd = FindTagEnd(document.source, propStart);
            if (propEnd == std::string::npos)
            {
                error = document.fileName + " contains an unfinished prop tag.";
                return false;
            }
            Prop prop;
            prop.start = propStart;
            if (IsSelfClosingTag(document.source, propStart, propEnd))
            {
                prop.end = propEnd + 1;
            }
            else
            {
                const size_t propClose = document.source.find("</prop>", propEnd + 1);
                if (propClose == std::string::npos || propClose >= rootClose)
                {
                    error = document.fileName +
                        " contains a prop element without a closing </prop> tag.";
                    return false;
                }
                prop.end = propClose + sizeof("</prop>") - 1;
            }
            std::string idText;
            if (ReadAttribute(document.source, propStart, propEnd, "id", idText))
            {
                try { prop.id = std::stoi(idText); }
                catch (...) { prop.id = -1; }
            }
            const char* name =
                DecorationDatabase::FindNameById(prop.id, document.type);
            prop.name = name == nullptr ? "Unknown Decoration" : name;
            std::string positionText;
            if (ReadAttribute(
                document.source,
                propStart,
                propEnd,
                "pos",
                positionText
            ))
            {
                prop.hasPosition = ParseFloat3(positionText, prop.position);
            }
            document.props.push_back(std::move(prop));
            search = document.props.back().end;
        }
        return true;
    }

    std::vector<DecorationCounterWindow::Requirement> BuildRequirements(
        const std::vector<XmlDocument>& documents
    )
    {
        std::map<int, DecorationCounterWindow::Requirement> byId;
        for (const XmlDocument& document : documents)
        {
            for (const Prop& prop : document.props)
            {
                auto& entry = byId[prop.id];
                entry.id = prop.id;
                entry.name = prop.name;
                ++entry.required;
            }
        }
        std::vector<DecorationCounterWindow::Requirement> output;
        for (const auto& [id, entry] : byId)
        {
            static_cast<void>(id);
            output.push_back(entry);
        }
        return output;
    }

    std::string Stem(const std::string& fileName)
    {
        return Utf8Paths::ToUtf8(Utf8Paths::FromUtf8(fileName).stem());
    }

    bool WriteFile(
        const std::filesystem::path& output,
        const std::string& contents,
        std::string& error
    )
    {
        std::ofstream file(output, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            error = "Could not create " +
                Utf8Paths::ToUtf8(output.filename()) + ".";
            return false;
        }
        file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (!file.good())
        {
            error = "Could not finish writing " +
                Utf8Paths::ToUtf8(output.filename()) + ".";
            return false;
        }
        return true;
    }

    bool ReplaceFileSafely(
        const std::filesystem::path& output,
        const std::string& contents,
        std::string& error
    )
    {
        std::filesystem::path temporary = output;
        temporary += L".decotools.tmp";

        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        if (!WriteFile(temporary, contents, error))
        {
            std::filesystem::remove(temporary, cleanupError);
            return false;
        }

        if (!MoveFileExW(
            temporary.c_str(),
            output.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
        ))
        {
            std::filesystem::remove(temporary, cleanupError);
            error = "Could not replace " +
                Utf8Paths::ToUtf8(output.filename()) +
                ". The original XML was left unchanged.";
            return false;
        }
        return true;
    }

    std::vector<Group> ParseGroups(XmlDocument& document)
    {
        for (Prop& prop : document.props)
        {
            prop.groupIndex = -1;
            prop.selected = false;
        }

        std::vector<Group> parsed;
        size_t comment = document.source.find(
            "<!--", document.rootOpenEnd + 1);
        while (comment != std::string::npos && comment < document.rootCloseStart)
        {
            const size_t commentEnd = document.source.find("-->", comment + 4);
            if (commentEnd == std::string::npos ||
                commentEnd >= document.rootCloseStart)
            {
                break;
            }
            const size_t next = document.source.find("<!--", commentEnd + 3);
            const size_t groupEnd =
                next == std::string::npos || next >= document.rootCloseStart
                ? document.rootCloseStart
                : next;

            Group group;
            group.name = Trim(document.source.substr(
                comment + 4, commentEnd - comment - 4));
            group.start = comment;
            group.end = groupEnd;

            const int groupIndex = static_cast<int>(parsed.size());
            for (Prop& prop : document.props)
            {
                if (prop.start > commentEnd && prop.start < groupEnd)
                {
                    prop.groupIndex = groupIndex;
                    group.props.push_back(prop);
                }
            }

            if (!group.name.empty())
            {
                parsed.push_back(std::move(group));
            }
            else
            {
                for (Prop& prop : document.props)
                {
                    if (prop.groupIndex == groupIndex) prop.groupIndex = -1;
                }
            }
            comment = next;
        }
        return parsed;
    }

    std::string BuildGroupedXml(
        const XmlDocument& document,
        const std::vector<Group>& orderedGroups
    )
    {
        const char* newline = document.source.find("\r\n") != std::string::npos
            ? "\r\n"
            : "\n";
        std::ostringstream body;
        for (const Prop& prop : document.props)
        {
            if (prop.groupIndex < 0)
            {
                body << newline << "  "
                    << document.source.substr(prop.start, prop.end - prop.start);
            }
        }
        for (size_t groupIndex = 0; groupIndex < orderedGroups.size(); ++groupIndex)
        {
            const Group& group = orderedGroups[groupIndex];
            body << newline << newline << "  <!--" << group.name << "-->";
            for (const Prop& prop : document.props)
            {
                if (prop.groupIndex == static_cast<int>(groupIndex))
                {
                    body << newline << "  "
                        << document.source.substr(prop.start, prop.end - prop.start);
                }
            }
        }
        body << newline;
        return
            document.source.substr(0, document.rootOpenEnd + 1) +
            body.str() +
            document.source.substr(document.rootCloseStart);
    }

    void PrepareMerge()
    {
        mergeDocuments.clear();
        report.clear();
        if (baseXmlIndex < 0 ||
            baseXmlIndex >= static_cast<int>(availableXmlFiles.size()))
        {
            status = "Select a base layout first.";
            return;
        }

        std::vector<int> selected;
        for (size_t index = 0; index < additionalSelected.size(); ++index)
        {
            if (additionalSelected[index] &&
                static_cast<int>(index) != baseXmlIndex)
            {
                selected.push_back(static_cast<int>(index));
            }
        }
        if (selected.empty())
        {
            status = "Select at least one additional XML to merge.";
            return;
        }

        std::string error;
        XmlDocument base;
        if (!LoadXml(
            availableXmlFiles[static_cast<size_t>(baseXmlIndex)].path,
            base,
            error))
        {
            status = error;
            return;
        }
        mergeDocuments.push_back(std::move(base));

        bool mismatch = false;
        for (const int index : selected)
        {
            XmlDocument document;
            if (!LoadXml(
                availableXmlFiles[static_cast<size_t>(index)].path,
                document,
                error))
            {
                status = error;
                mergeDocuments.clear();
                return;
            }
            mismatch = mismatch || document.type != mergeDocuments[0].type;
            mergeDocuments.push_back(std::move(document));
        }

        size_t total = 0;
        std::ostringstream output;
        output << "Base Layout: " << mergeDocuments[0].fileName << "\n";
        output << "Map Type: "
            << (mergeDocuments[0].type == 0 ? "Homestead" : "Guild Hall") << "\n\n";
        output << "Additional Files:\n";
        for (size_t index = 1; index < mergeDocuments.size(); ++index)
        {
            output << "  " << mergeDocuments[index].fileName;
            if (mergeDocuments[index].type != mergeDocuments[0].type)
            {
                output << " (MAP TYPE MISMATCH)";
            }
            output << "\n";
        }
        for (const XmlDocument& document : mergeDocuments) total += document.props.size();
        output << "\nTotal Decorations: " << total << " / 2000\n";
        if (total > 2000)
        {
            output << "WARNING: This merge exceeds the 2000 decoration limit.\n";
        }
        output << (mismatch
            ? "\nMerge blocked - all XML map types must match."
            : "\nAll map types match - ready to merge.");
        report = output.str();

        if (mismatch)
        {
            status = "Map type mismatch. Merge is disabled.";
            DecorationCounterWindow::Clear();
            return;
        }

        DecorationCounterWindow::SetRequirements(
            Stem(mergeDocuments[0].fileName) + " Merged Layout",
            mergeDocuments[0].type,
            BuildRequirements(mergeDocuments)
        );
        status = "Merge pre-check complete.";
    }

    void ExportMerge()
    {
        if (mergeDocuments.size() < 2)
        {
            status = "Prepare a valid merge first.";
            return;
        }
        const XmlDocument& base = mergeDocuments[0];
        for (const XmlDocument& document : mergeDocuments)
        {
            if (document.type != base.type)
            {
                status = "Map type mismatch. Merge is disabled.";
                return;
            }
        }

        std::ostringstream addition;
        for (size_t index = 1; index < mergeDocuments.size(); ++index)
        {
            const XmlDocument& document = mergeDocuments[index];
            addition << "\n  <!--" << Stem(document.fileName) << "-->\n";
            for (const Prop& prop : document.props)
            {
                addition << "  "
                    << document.source.substr(prop.start, prop.end - prop.start)
                    << "\n";
            }
        }

        const std::string merged =
            base.source.substr(0, base.rootCloseStart) +
            addition.str() +
            base.source.substr(base.rootCloseStart);
        const std::filesystem::path folder =
            Utf8Paths::FromUtf8(base.path).parent_path();
        const std::filesystem::path output =
            XmlFileUtils::IndexedOperationPath(
                folder, Stem(base.fileName), "_MERGED");
        std::string error;
        if (WriteFile(output, merged, error))
        {
            if (AppSettings::Get().automaticGroupBackupRestore)
            {
                std::string backupStatus;
                GroupBackupDatabase::RecordFile(Utf8Paths::ToUtf8(output),base.type,
                    GroupBackupDatabase::RestorePointType::Auto,std::string(),backupStatus,
                    AppSettings::Get().backupUngroupedXmls);
            }
            status = "Exported " + Utf8Paths::ToUtf8(output.filename()) + ".";
        }
        else
        {
            status = error;
        }
    }

    void ClearGroupSelection(bool clearName)
    {
        for (Prop& prop : groupDocument.props) prop.selected = false;
        if (clearName) groupName.fill('\0');
    }

    bool ReloadGroupDocument(const std::string& path)
    {
        XmlDocument reloaded;
        std::string error;
        if (!LoadXml(path, reloaded, error))
        {
            status = error;
            return false;
        }
        groupDocument = std::move(reloaded);
        groups = ParseGroups(groupDocument);
        ClearGroupSelection(true);
        return true;
    }

    void ImportGroup()
    {
        groupDocument = XmlDocument{};
        groups.clear();
        groupName.fill('\0');
        hideGrouped = false;
        marqueeMode = false;
        if (groupXmlIndex < 0 ||
            groupXmlIndex >= static_cast<int>(availableXmlFiles.size()))
        {
            status = "Select an XML to group first.";
            return;
        }

        const std::string path =
            availableXmlFiles[static_cast<size_t>(groupXmlIndex)].path;
        if (!ReloadGroupDocument(path)) return;
        if (groupDocument.props.empty())
        {
            groupDocument = XmlDocument{};
            groups.clear();
            status = "The selected XML contains no decorations to group.";
            return;
        }
        for (const Prop& prop : groupDocument.props)
        {
            if (!prop.hasPosition)
            {
                groupDocument = XmlDocument{};
                groups.clear();
                status = "Group requires a valid pos value on every decoration.";
                return;
            }
        }
        if (groupDocument.props.size() > 2000)
        {
            groupDocument = XmlDocument{};
            groups.clear();
            status = "Group supports the Guild Wars 2 limit of 2000 decorations.";
            return;
        }

        DecorationCounterWindow::SetRequirements(
            groupDocument.fileName,
            groupDocument.type,
            BuildRequirements({ groupDocument })
        );
        status = "Loaded " + std::to_string(groupDocument.props.size()) +
            " decorations. Select ungrouped orange points.";
    }

    void CreateGroup()
    {
        const std::string name = Trim(groupName.data());
        if (name.empty())
        {
            status = "Enter a group name first.";
            return;
        }
        if (name.find("--") != std::string::npos || name.back() == '-')
        {
            status = "Group names cannot contain -- or end with a hyphen.";
            return;
        }
        for (const Group& group : groups)
        {
            if (group.name == name)
            {
                status = "A group with that name already exists.";
                return;
            }
        }

        size_t selectedCount = 0;
        const int newGroupIndex = static_cast<int>(groups.size());
        for (Prop& prop : groupDocument.props)
        {
            if (prop.selected && prop.groupIndex < 0)
            {
                prop.groupIndex = newGroupIndex;
                ++selectedCount;
            }
        }
        if (selectedCount == 0)
        {
            status = "Select at least one ungrouped decoration point.";
            return;
        }

        Group group;
        group.name = name;
        groups.push_back(std::move(group));
        const std::string rewritten = BuildGroupedXml(groupDocument, groups);
        std::string error;
        const std::string path = groupDocument.path;
        if (!ReplaceFileSafely(Utf8Paths::FromUtf8(path), rewritten, error))
        {
            for (Prop& prop : groupDocument.props)
            {
                if (prop.groupIndex == newGroupIndex) prop.groupIndex = -1;
            }
            groups.pop_back();
            status = error;
            return;
        }

        if (AppSettings::Get().automaticGroupBackupRestore)
        {
            std::string backupStatus;
            GroupBackupDatabase::RecordFile(path,groupDocument.type,
                GroupBackupDatabase::RestorePointType::Auto,std::string(),backupStatus,
                AppSettings::Get().backupUngroupedXmls);
        }

        if (!ReloadGroupDocument(path)) return;
        status = "Created group \"" + name + "\" with " +
            std::to_string(selectedCount) + " decorations.";
    }

    void Ungroup(size_t index)
    {
        if (index >= groups.size()) return;
        const std::string name = groups[index].name;
        size_t count = 0;
        for (Prop& prop : groupDocument.props)
        {
            if (prop.groupIndex == static_cast<int>(index))
            {
                prop.groupIndex = -1;
                ++count;
            }
            else if (prop.groupIndex > static_cast<int>(index))
            {
                --prop.groupIndex;
            }
        }
        groups.erase(groups.begin() + static_cast<std::ptrdiff_t>(index));

        const std::string rewritten = BuildGroupedXml(groupDocument, groups);
        std::string error;
        const std::string path = groupDocument.path;
        if (!ReplaceFileSafely(Utf8Paths::FromUtf8(path), rewritten, error))
        {
            ReloadGroupDocument(path);
            status = error;
            return;
        }
        if (AppSettings::Get().automaticGroupBackupRestore)
        {
            std::string backupStatus;
            GroupBackupDatabase::RecordFile(path,groupDocument.type,
                GroupBackupDatabase::RestorePointType::Auto,std::string(),backupStatus,
                AppSettings::Get().backupUngroupedXmls);
        }
        if (!ReloadGroupDocument(path)) return;
        status = "Ungrouped \"" + name + "\" (" +
            std::to_string(count) + " decorations).";
    }

    void ImportExtract()
    {
        extractDocument = XmlDocument{};
        extractGroups.clear();
        report.clear();
        if (extractXmlIndex < 0 ||
            extractXmlIndex >= static_cast<int>(availableXmlFiles.size()))
        {
            status = "Select a merged XML first.";
            return;
        }
        std::string error;
        if (!LoadXml(
            availableXmlFiles[static_cast<size_t>(extractXmlIndex)].path,
            extractDocument,
            error))
        {
            status = error;
            return;
        }

        extractGroups = ParseGroups(extractDocument);

        if (extractGroups.empty())
        {
            status =
                "No merger groups found. Extract works with XMLs created by DecoTools Merge.";
            DecorationCounterWindow::Clear();
            return;
        }

        DecorationCounterWindow::SetRequirements(
            extractDocument.fileName,
            extractDocument.type,
            BuildRequirements({ extractDocument })
        );
        report = "Found " + std::to_string(extractGroups.size()) +
            " named decoration groups.";
        status = "Select one or more groups to extract.";
    }

    std::string SafeFileStem(std::string value)
    {
        for (char& character : value)
        {
            const unsigned char byte = static_cast<unsigned char>(character);
            if (!(std::isalnum(byte) || character == '-' || character == '_'))
            {
                character = '_';
            }
        }
        return value.empty() ? "Decoration_Group" : value;
    }

    void ExportExtract()
    {
        std::vector<const Group*> selected;
        for (const Group& group : extractGroups)
        {
            if (group.selected) selected.push_back(&group);
        }
        if (selected.empty())
        {
            status = "Select at least one group to extract.";
            return;
        }

        const std::filesystem::path folder =
            Utf8Paths::FromUtf8(extractDocument.path).parent_path();
        std::string stripped = extractDocument.source;
        std::vector<const Group*> descending = selected;
        std::sort(descending.begin(), descending.end(),
            [](const Group* left, const Group* right)
            {
                return left->start > right->start;
            });
        for (const Group* group : descending)
        {
            stripped.erase(group->start, group->end - group->start);
        }

        std::string error;
        const std::filesystem::path strippedPath =
            XmlFileUtils::IndexedOperationPath(
                folder, Stem(extractDocument.fileName), "_STRIPPED");
        if (!WriteFile(strippedPath, stripped, error))
        {
            status = error;
            return;
        }
        if (AppSettings::Get().automaticGroupBackupRestore)
        {
            std::string backupStatus;
            GroupBackupDatabase::RecordFile(Utf8Paths::ToUtf8(strippedPath),extractDocument.type,
                GroupBackupDatabase::RestorePointType::Auto,std::string(),backupStatus,
                AppSettings::Get().backupUngroupedXmls);
        }

        int exported = 0;
        for (const Group* group : selected)
        {
            std::ostringstream body;
            body << "\n  <!--" << group->name << "-->\n";
            for (const Prop& prop : group->props)
            {
                body << "  "
                    << extractDocument.source.substr(prop.start, prop.end - prop.start)
                    << "\n";
            }
            const std::string outputXml =
                extractDocument.source.substr(0, extractDocument.rootOpenEnd + 1) +
                body.str() +
                extractDocument.source.substr(extractDocument.rootCloseStart);
            const std::filesystem::path output = XmlFileUtils::IndexedOperationPath(
                folder, SafeFileStem(group->name), "_EXTRACTED");
            if (!WriteFile(output, outputXml, error))
            {
                status = error;
                return;
            }
            if (AppSettings::Get().automaticGroupBackupRestore)
            {
                std::string backupStatus;
                GroupBackupDatabase::RecordFile(Utf8Paths::ToUtf8(output),extractDocument.type,
                    GroupBackupDatabase::RestorePointType::Auto,std::string(),backupStatus,
                    AppSettings::Get().backupUngroupedXmls);
            }
            ++exported;
        }
        status = "Exported " + std::to_string(exported) +
            " group file(s) and " +
            Utf8Paths::ToUtf8(strippedPath.filename()) + ".";
    }

    void RenderFolderChoice()
    {
        if (ImGui::RadioButton("Homestead##MergeFolder", selectedFolderType == 0))
        {
            selectedFolderType = 0;
            RefreshXmlList();
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Guild Hall##MergeFolder", selectedFolderType == 1))
        {
            selectedFolderType = 1;
            RefreshXmlList();
        }
    }

    void RenderXmlCombo(const char* id, int& selectedIndex)
    {
        const char* label =
            selectedIndex >= 0 &&
            selectedIndex < static_cast<int>(availableXmlFiles.size())
            ? availableXmlFiles[static_cast<size_t>(selectedIndex)].name.c_str()
            : "No XML files found";
        ImGui::SetNextItemWidth(-1.0f);
        XmlComboHelpers::SetPopupWidth(availableXmlFiles);
        if (ImGui::BeginCombo(id, label))
        {
            for (size_t index = 0; index < availableXmlFiles.size(); ++index)
            {
                const bool selected = selectedIndex == static_cast<int>(index);
                ImGui::PushID(static_cast<int>(index));
                if (ImGui::Selectable(availableXmlFiles[index].name.c_str(), selected))
                {
                    selectedIndex = static_cast<int>(index);
                    ClearLoaded();
                }
                if (selected) ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    }
}

void MergeExtractTab::Render()
{
    if (!fileListInitialized ||
        listedSubFolders != AppSettings::Get().showXmlsFromSubFolders)
    {
        RefreshXmlList();
    }

    RenderSectionHeading("Operation");
    if (ImGui::RadioButton("Merge XML Files", operation == 0))
    {
        operation = 0;
        ClearLoaded();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Group Decorations", operation == 1))
    {
        operation = 1;
        ClearLoaded();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Extract Groups", operation == 2))
    {
        operation = 2;
        ClearLoaded();
    }

    ImGui::Dummy(ImVec2(0.0f, 16.0f));
    RenderSectionHeading(
        operation == 0 ? "Merge Source Files" :
        operation == 1 ? "Group Source File" : "Extract Source File"
    );
    RenderFolderChoice();
    ImGui::Spacing();

    if (operation == 0)
    {
        ImGui::Text("Base Layout");
        RenderXmlCombo("##MergeBaseXml", baseXmlIndex);

        ImGui::Dummy(ImVec2(0.0f, 14.0f));
        ImGui::Text("Additional XML Files");
        ImGui::BeginChild("##MergeFileChecklist", ImVec2(0.0f, 130.0f), true);
        for (size_t index = 0; index < availableXmlFiles.size(); ++index)
        {
            if (static_cast<int>(index) == baseXmlIndex) continue;
            bool selected = additionalSelected[index] != 0;
            if (ImGui::Checkbox(
                (availableXmlFiles[index].name + "##MergeAdd" +
                    std::to_string(index)).c_str(),
                &selected
            ))
            {
                additionalSelected[index] = selected ? 1 : 0;
                ClearLoaded();
            }
        }
        ImGui::EndChild();

        const float width =
            (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button("Refresh List##Merge", ImVec2(width, 0.0f)))
        {
            RefreshXmlList();
        }
        ImGui::SameLine();
        if (ImGui::Button("Prepare Merge", ImVec2(width, 0.0f)))
        {
            PrepareMerge();
        }

        if (!report.empty())
        {
            ImGui::Spacing();
            ImGui::BeginChild("##MergeReport", ImVec2(0.0f, 120.0f), true);
            ImGui::TextUnformatted(report.c_str());
            ImGui::EndChild();
        }

        ImGui::Dummy(ImVec2(0.0f, 16.0f));
        RenderSectionHeading("Export");
        if (mergeDocuments.size() >= 2)
        {
            if (ImGui::Button("Merge and Export")) ExportMerge();
        }
        else
        {
            RenderDisabledButton("Merge and Export");
        }
    }
    else if (operation == 1)
    {
        RenderXmlCombo("##GroupXml", groupXmlIndex);
        const float width =
            (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button("Refresh List##Group", ImVec2(width, 0.0f)))
        {
            RefreshXmlList();
        }
        ImGui::SameLine();
        if (ImGui::Button("Import Selected##Group", ImVec2(width, 0.0f)))
        {
            ImportGroup();
        }

        if (!groupDocument.props.empty())
        {
            ImGui::Spacing();
            ImGui::Checkbox("Marquee Select", &marqueeMode);
            ImGui::SameLine();
            ImGui::Checkbox("Hide Grouped Decorations", &hideGrouped);

            ImGui::TextUnformatted("Visibility Distance:");
            ImGui::SameLine();
            ImGui::TextUnformatted("None");
            ImGui::SameLine();
            const float allLabelWidth = ImGui::CalcTextSize("All").x;
            const float sliderWidth = (std::max)(
                60.0f,
                ImGui::GetContentRegionAvail().x - allLabelWidth -
                ImGui::GetStyle().ItemSpacing.x
            );
            ImGui::SetNextItemWidth(sliderWidth);
            ImGui::SliderInt(
                "##GroupVisibilityDistance",
                &visibilityDistance,
                0,
                100,
                "",
                ImGuiSliderFlags_NoInput
            );
            ImGui::SameLine();
            ImGui::TextUnformatted("All");

            size_t selectedCount = 0;
            for (const Prop& prop : groupDocument.props)
            {
                if (prop.selected) ++selectedCount;
            }
            ImGui::Text("Selected: %zu", selectedCount);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint(
                "##GroupName",
                "Group name",
                groupName.data(),
                groupName.size()
            );

            if (ImGui::Button("Create Group", ImVec2(width, 0.0f)))
            {
                CreateGroup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear Selection", ImVec2(width, 0.0f)))
            {
                ClearGroupSelection(true);
                status = "Selection and group name cleared.";
            }

            ImGui::Dummy(ImVec2(0.0f, 16.0f));
            RenderSectionHeading("Decoration Groups");
            if (groups.empty())
            {
                ImGui::TextDisabled("No groups have been created yet.");
            }
            else
            {
                ImGui::TextDisabled("Ungroup");
                ImGui::BeginChild("##GroupList", ImVec2(0.0f, 150.0f), true);
                for (size_t index = 0; index < groups.size(); ++index)
                {
                    const Group& group = groups[index];
                    ImGui::PushID(static_cast<int>(index));
                    if (ImGui::SmallButton("X"))
                    {
                        ImGui::PopID();
                        Ungroup(index);
                        break;
                    }
                    ImGui::SameLine();
                    ImGui::Text("%s (%zu decorations)",
                        group.name.c_str(), group.props.size());
                    ImGui::PopID();
                }
                ImGui::EndChild();
            }
        }
    }
    else
    {
        RenderXmlCombo("##ExtractXml", extractXmlIndex);
        const float width =
            (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button("Refresh List##Extract", ImVec2(width, 0.0f)))
        {
            RefreshXmlList();
        }
        ImGui::SameLine();
        if (ImGui::Button("Import Selected##Extract", ImVec2(width, 0.0f)))
        {
            ImportExtract();
        }

        if (!extractGroups.empty())
        {
            ImGui::Spacing();
            ImGui::Text("Decoration Groups");
            ImGui::BeginChild("##ExtractGroups", ImVec2(0.0f, 170.0f), true);
            for (Group& group : extractGroups)
            {
                const std::string label = group.name + " (" +
                    std::to_string(group.props.size()) + " decorations)";
                ImGui::Checkbox(label.c_str(), &group.selected);
            }
            ImGui::EndChild();
        }

        ImGui::Dummy(ImVec2(0.0f, 16.0f));
        RenderSectionHeading("Export");
        if (!extractGroups.empty())
        {
            if (ImGui::Button("Extract Selected Groups")) ExportExtract();
        }
        else
        {
            RenderDisabledButton("Extract Selected Groups");
        }
    }

    ImGui::Spacing();
    ImGui::TextDisabled("%s", status.c_str());
}

void MergeExtractTab::ClearImportedData()
{
    ClearLoaded();
    baseXmlIndex = -1;
    extractXmlIndex = -1;
    groupXmlIndex = -1;
    additionalSelected.assign(availableXmlFiles.size(), false);
}

void MergeExtractTab::RenderOverlay()
{
    if (operation != 1 || groupDocument.props.empty() ||
        !AppSettings::Get().windowVisible)
    {
        hoveredGroupProp = -1;
        groupInputCaptured = false;
        groupMouseDown = false;
        groupReleasePending = false;
        groupRightClickPending = false;
        return;
    }

    Mumble::Data* mumble = AppRuntime::GetMumble();
    if (mumble == nullptr || mumble->Context.MapID == 0)
    {
        hoveredGroupProp = -1;
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    if (!groupInputCaptured && io.MousePos.x >= 0.0f && io.MousePos.y >= 0.0f)
    {
        groupMousePosition = io.MousePos;
    }

    const ImVec2 viewport = io.DisplaySize;
    const Camera camera = CameraFromMumble(*mumble);
    const Vec3 avatarPosition = FromMumble(mumble->AvatarPosition);
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    const float pointSize = (std::max)(2.0f, AppSettings::Get().pointSize);
    const float hitRadius = pointSize + 5.0f;
    const ImU32 orange = IM_COL32(255, 166, 36, 255);
    const ImU32 blue = IM_COL32(50, 150, 255, 255);
    const ImU32 gray = IM_COL32(125, 125, 125, 220);

    hoveredGroupProp = -1;
    float closestDistance = std::numeric_limits<float>::infinity();
    std::vector<ImVec2> projected(groupDocument.props.size());
    std::vector<unsigned char> visible(groupDocument.props.size(), 0);

    float farthestDistance = 0.0f;
    if (visibilityDistance > 0 && visibilityDistance < 100)
    {
        for (const Prop& prop : groupDocument.props)
        {
            if (hideGrouped && prop.groupIndex >= 0)
            {
                continue;
            }
            const Vec3 world = DecorationToWorld(prop.position);
            farthestDistance = (std::max)(
                farthestDistance,
                Length(Subtract(world, avatarPosition))
            );
        }
    }
    const float visibleRange = farthestDistance *
        (static_cast<float>(visibilityDistance) / 100.0f);

    for (size_t index = 0; index < groupDocument.props.size(); ++index)
    {
        Prop& prop = groupDocument.props[index];
        if (visibilityDistance <= 0 || (hideGrouped && prop.groupIndex >= 0))
        {
            if (visibilityDistance <= 0 && prop.groupIndex < 0)
            {
                prop.selected = false;
            }
            continue;
        }

        const Vec3 world = DecorationToWorld(prop.position);
        if (visibilityDistance < 100 &&
            Length(Subtract(world, avatarPosition)) > visibleRange)
        {
            if (prop.groupIndex < 0)
            {
                prop.selected = false;
            }
            continue;
        }

        ImVec2 point;
        if (!camera.Project(world, viewport, point))
        {
            continue;
        }
        projected[index] = point;
        visible[index] = 1;

        const ImU32 color = prop.groupIndex >= 0
            ? gray : prop.selected ? blue : orange;
        draw->AddCircleFilled(point, pointSize, color, 12);

        if (prop.groupIndex < 0)
        {
            const float dx = point.x - groupMousePosition.x;
            const float dy = point.y - groupMousePosition.y;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance <= hitRadius && distance < closestDistance)
            {
                closestDistance = distance;
                hoveredGroupProp = static_cast<int>(index);
            }
        }
    }

    if (hoveredGroupProp >= 0 && !groupInputCaptured && !io.WantCaptureMouse)
    {
        const Prop& hovered =
            groupDocument.props[static_cast<size_t>(hoveredGroupProp)];
        const ImVec2 textSize = ImGui::CalcTextSize(hovered.name.c_str());
        const ImVec2 padding(7.0f, 5.0f);
        ImVec2 tooltipPosition(
            groupMousePosition.x + 18.0f,
            groupMousePosition.y + 8.0f
        );
        const ImVec2 tooltipSize(
            textSize.x + padding.x * 2.0f,
            textSize.y + padding.y * 2.0f
        );
        tooltipPosition.x = (std::min)(
            tooltipPosition.x,
            (std::max)(4.0f, viewport.x - tooltipSize.x - 4.0f)
        );
        tooltipPosition.y = (std::min)(
            tooltipPosition.y,
            (std::max)(4.0f, viewport.y - tooltipSize.y - 4.0f)
        );
        ImDrawList* foreground = ImGui::GetForegroundDrawList();
        foreground->AddRectFilled(
            tooltipPosition,
            ImVec2(
                tooltipPosition.x + tooltipSize.x,
                tooltipPosition.y + tooltipSize.y
            ),
            IM_COL32(24, 24, 28, 245),
            4.0f
        );
        foreground->AddText(
            ImVec2(
                tooltipPosition.x + padding.x,
                tooltipPosition.y + padding.y
            ),
            IM_COL32(255, 255, 255, 255),
            hovered.name.c_str()
        );
    }

    if (groupInputCaptured && marqueeMode)
    {
        const ImVec2 minimum(
            (std::min)(marqueeStart.x, marqueeEnd.x),
            (std::min)(marqueeStart.y, marqueeEnd.y)
        );
        const ImVec2 maximum(
            (std::max)(marqueeStart.x, marqueeEnd.x),
            (std::max)(marqueeStart.y, marqueeEnd.y)
        );
        draw->AddRectFilled(minimum, maximum, IM_COL32(50, 150, 255, 35));
        draw->AddRect(minimum, maximum, blue, 0.0f, 0, 1.5f);
    }

    if (groupRightClickPending)
    {
        if (hoveredGroupProp >= 0)
        {
            groupDocument.props[static_cast<size_t>(hoveredGroupProp)].selected = false;
        }
        groupRightClickPending = false;
    }

    if (groupReleasePending)
    {
        const float width = std::fabs(marqueeEnd.x - marqueeStart.x);
        const float height = std::fabs(marqueeEnd.y - marqueeStart.y);
        if (marqueeMode && (width >= 4.0f || height >= 4.0f))
        {
            const float left = (std::min)(marqueeStart.x, marqueeEnd.x);
            const float right = (std::max)(marqueeStart.x, marqueeEnd.x);
            const float top = (std::min)(marqueeStart.y, marqueeEnd.y);
            const float bottom = (std::max)(marqueeStart.y, marqueeEnd.y);
            for (size_t index = 0; index < groupDocument.props.size(); ++index)
            {
                Prop& prop = groupDocument.props[index];
                if (prop.groupIndex < 0 && visible[index] &&
                    projected[index].x >= left && projected[index].x <= right &&
                    projected[index].y >= top && projected[index].y <= bottom)
                {
                    prop.selected = true;
                }
            }
        }
        else if (hoveredGroupProp >= 0)
        {
            groupDocument.props[static_cast<size_t>(hoveredGroupProp)].selected = true;
        }
        groupReleasePending = false;
    }
}

UINT MergeExtractTab::WndProc(HWND, UINT message, WPARAM, LPARAM lParam)
{
    if (operation != 1 || groupDocument.props.empty() ||
        !AppSettings::Get().windowVisible)
    {
        return 1;
    }

    switch (message)
    {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
        groupMousePosition = ImVec2(
            static_cast<float>(static_cast<short>(LOWORD(lParam))),
            static_cast<float>(static_cast<short>(HIWORD(lParam)))
        );
        if (groupInputCaptured) marqueeEnd = groupMousePosition;
        break;
    default:
        return 1;
    }

    if (!groupInputCaptured && ImGui::GetIO().WantCaptureMouse) return 1;

    if (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK)
    {
        if (marqueeMode || hoveredGroupProp >= 0)
        {
            groupInputCaptured = true;
            groupMouseDown = true;
            marqueeStart = groupMousePosition;
            marqueeEnd = groupMousePosition;
            return 0;
        }
    }
    else if (message == WM_LBUTTONUP)
    {
        groupMouseDown = false;
        if (groupInputCaptured)
        {
            marqueeEnd = groupMousePosition;
            groupInputCaptured = false;
            groupReleasePending = true;
            return 0;
        }
    }
    else if (message == WM_RBUTTONDOWN)
    {
        if (hoveredGroupProp >= 0 &&
            groupDocument.props[static_cast<size_t>(hoveredGroupProp)].selected)
        {
            groupRightClickPending = true;
            return 0;
        }
    }
    else if (message == WM_RBUTTONUP && groupRightClickPending)
    {
        return 0;
    }
    else if (message == WM_MOUSEMOVE && groupInputCaptured)
    {
        return 0;
    }

    return 1;
}
