// Pewpew's Deco Tools - Group Backup/Restore User Interface

#include "GroupBackupRestoreTab.h"

#include "../../Core/AppRuntime.h"
#include "../../Core/AppSettings.h"
#include "../../Core/GroupBackupDatabase.h"
#include "../../Core/Utf8Paths.h"
#include "../../Core/XmlFileUtils.h"
#include "../XmlComboHelpers.h"
#include "../../imgui/imgui.h"
#include "../../imgui/imgui_internal.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
    using XmlFileEntry = XmlFileUtils::Entry;

    struct GroupedXmlCandidate
    {
        std::string name;
        std::string path;
        size_t groupCount = 0;
        std::filesystem::file_time_type modified = {};
    };

    struct LiveMapInfo
    {
        unsigned liveMapId = 0;
        unsigned xmlMapId = 0;
        const char* mapName = nullptr;
        int xmlType = -1;
    };

    enum class RestoreSelectionKind
    {
        None,
        Database,
        SavedXml
    };

    enum class RestoreSourceFilter
    {
        All,
        SavedXml,
        Automatic,
        Manual,
        Safety
    };

    int selectedFolderType = 0;
    int selectedXmlIndex = -1;
    bool fileListInitialized = false;
    bool listedSubFolders = false;
    std::vector<XmlFileEntry> availableXmlFiles;
    std::string importedPath;
    std::string importedName;
    int importedType = -1;
    size_t importedGroupCount = 0;
    size_t importedPropCount = 0;
    std::array<char, 128> backupName = {};
    RestoreSourceFilter restoreSourceFilter = RestoreSourceFilter::All;
    RestoreSelectionKind restoreSelectionKind = RestoreSelectionKind::None;
    std::string selectedRestoreId;
    std::string selectedRestoreXmlPath;
    std::string selectedRestoreLabel;
    std::vector<GroupedXmlCandidate> manualXmlCandidates;
    GroupBackupDatabase::RestoreStats previewStats;
    bool previewValid = false;
    std::string status = "No XML imported";

    std::string popupTargetPath;
    std::vector<GroupedXmlCandidate> popupCandidates;
    std::vector<GroupBackupDatabase::RestorePointSummary> popupRestorePoints;
    RestoreSelectionKind popupSelectionKind = RestoreSelectionKind::None;
    std::string popupRestoreId;
    std::string popupRestoreXmlPath;
    GroupBackupDatabase::RestoreStats popupPreviewStats;
    bool popupPreviewValid = false;
    std::string popupStatus;

    bool manageWindowOpen = false;
    int manageFilter = 0;
    std::unordered_set<std::string> managedSelections;
    std::string renameSelectionId;
    std::array<char, 128> renameBuffer = {};
    std::string manageStatus;

    bool rebuildWindowOpen = false;
    std::string rebuildSelectionId;
    std::array<char, 260> rebuildName = {};
    std::string rebuildStatus;
    bool rebuildResultPending = false;
    bool rebuildSucceeded = false;
    std::string rebuildResultMessage;

    void RenderSectionHeading(const char* label)
    {
        ImGui::SetWindowFontScale(1.2f);
        ImGui::TextUnformatted(label);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Separator();
    }

    void RenderDisabledButton(const char* label, ImVec2 size = {})
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
        return type == 1 ? settings.guildHallFolder.data() : settings.homesteadFolder.data();
    }

    std::vector<GroupedXmlCandidate> ScanGroupedXmlCandidates(
        int xmlType, const std::string& excludedPath);

    void RefreshXmlList()
    {
        const std::string previous = selectedXmlIndex >= 0 &&
            selectedXmlIndex < static_cast<int>(availableXmlFiles.size())
            ? availableXmlFiles[static_cast<size_t>(selectedXmlIndex)].path : std::string();
        availableXmlFiles.clear();
        selectedXmlIndex = -1;
        fileListInitialized = true;
        listedSubFolders = AppSettings::Get().showXmlsFromSubFolders;
        const std::string folder = FolderForType(selectedFolderType);
        if (folder.empty())
        {
            status = "Set this XML folder path in Settings first.";
            return;
        }
        if (!XmlFileUtils::List(folder, listedSubFolders, availableXmlFiles))
        {
            status = "XML folder not found or could not be read. Check the path in Settings.";
            return;
        }
        for (size_t index = 0; index < availableXmlFiles.size(); ++index)
            if (availableXmlFiles[index].path == previous)
            { selectedXmlIndex = static_cast<int>(index); break; }
        if (selectedXmlIndex < 0 && !availableXmlFiles.empty()) selectedXmlIndex = 0;
        status = availableXmlFiles.empty() ? "No XML files found in this folder." :
            "Found " + std::to_string(availableXmlFiles.size()) + " XML file(s).";
        if (!importedPath.empty())
            manualXmlCandidates = ScanGroupedXmlCandidates(importedType, importedPath);
    }

    void InitializeXmlList()
    {
        if (!fileListInitialized ||
            listedSubFolders != AppSettings::Get().showXmlsFromSubFolders)
            RefreshXmlList();
    }

    const char* RestorePointTypeLabel(GroupBackupDatabase::RestorePointType type)
    {
        if (type == GroupBackupDatabase::RestorePointType::Safety) return "safety";
        if (type == GroupBackupDatabase::RestorePointType::Manual) return "manual";
        return "automatic";
    }

    void ClearRestoreSelection()
    {
        restoreSelectionKind = RestoreSelectionKind::None;
        selectedRestoreId.clear();
        selectedRestoreXmlPath.clear();
        selectedRestoreLabel.clear();
        previewValid = false;
        previewStats = {};
    }

    void UpdatePreview()
    {
        previewValid = false;
        previewStats = {};
        if (restoreSelectionKind == RestoreSelectionKind::None || importedPath.empty()) return;
        std::string previewStatus;
        if (restoreSelectionKind == RestoreSelectionKind::Database)
        {
            previewValid = GroupBackupDatabase::PreviewRestore(
                selectedRestoreId, importedPath, previewStats, previewStatus);
        }
        else
        {
            previewValid = GroupBackupDatabase::PreviewRestoreFromXml(
                selectedRestoreXmlPath, importedPath, importedType,
                previewStats, previewStatus);
        }
    }

    std::vector<GroupedXmlCandidate> ScanGroupedXmlCandidates(
        int xmlType, const std::string& excludedPath)
    {
        std::vector<GroupedXmlCandidate> candidates;
        std::vector<XmlFileEntry> files;
        const std::string folder = FolderForType(xmlType);
        if (!folder.empty())
            XmlFileUtils::List(folder, AppSettings::Get().showXmlsFromSubFolders, files);
        for (const XmlFileEntry& entry : files)
        {
            if (!excludedPath.empty() && entry.path == excludedPath) continue;
            int type = -1;
            size_t groupCount = 0, propCount = 0;
            if (!GroupBackupDatabase::InspectFile(entry.path, type, groupCount, propCount) ||
                type != xmlType || groupCount == 0) continue;
            GroupedXmlCandidate candidate;
            candidate.name = entry.name;
            candidate.path = entry.path;
            candidate.groupCount = groupCount;
            std::error_code error;
            candidate.modified = std::filesystem::last_write_time(
                Utf8Paths::FromUtf8(entry.path), error);
            candidates.push_back(std::move(candidate));
        }
        std::sort(candidates.begin(), candidates.end(),
            [](const GroupedXmlCandidate& left, const GroupedXmlCandidate& right)
            { return left.modified > right.modified; });
        return candidates;
    }

    void ImportSelected()
    {
        if (selectedXmlIndex < 0 ||
            selectedXmlIndex >= static_cast<int>(availableXmlFiles.size())) return;
        const XmlFileEntry& entry = availableXmlFiles[static_cast<size_t>(selectedXmlIndex)];
        int type = -1;
        size_t groups = 0, props = 0;
        if (!GroupBackupDatabase::InspectFile(entry.path, type, groups, props))
        {
            status = "The selected file is not a valid Decorations XML.";
            return;
        }
        importedPath = entry.path;
        importedName = entry.name;
        importedType = type;
        importedGroupCount = groups;
        importedPropCount = props;
        ClearRestoreSelection();
        manualXmlCandidates = ScanGroupedXmlCandidates(importedType, importedPath);
        status = "Imported " + importedName + " with " +
            std::to_string(importedGroupCount) + " group(s).";
        if (AppSettings::Get().automaticGroupBackupRestore &&
            (groups > 0 || AppSettings::Get().backupUngroupedXmls))
        {
            std::string backupStatus;
            if (GroupBackupDatabase::RecordFile(entry.path, type,
                GroupBackupDatabase::RestorePointType::Auto,
                std::string(), backupStatus,
                AppSettings::Get().backupUngroupedXmls))
                status += " " + backupStatus;
            else
                status += " Automatic backup failed: " + backupStatus;
        }
    }

    std::string RestorePointLabel(
        const GroupBackupDatabase::RestorePointSummary& point)
    {
        const std::string name = point.customName.empty()
            ? point.xmlName
            : point.customName + " (" + point.xmlName + ")";
        return name + " | " + RestorePointTypeLabel(point.type) +
            " | " + point.createdUtc + " | " + std::to_string(point.groupCount) + " groups";
    }

    void RefreshPopupCandidates(const GroupBackupDatabase::PendingRestore& pending)
    {
        popupTargetPath = pending.targetPath;
        popupCandidates.clear();
        popupRestorePoints = GroupBackupDatabase::GetRestorePoints(pending.xmlType);
        popupRestorePoints.erase(std::remove_if(
            popupRestorePoints.begin(), popupRestorePoints.end(),
            [](const GroupBackupDatabase::RestorePointSummary& point)
            { return point.groupCount == 0; }), popupRestorePoints.end());
        popupSelectionKind = RestoreSelectionKind::None;
        popupRestoreId.clear();
        popupRestoreXmlPath.clear();
        popupPreviewStats = {};
        popupPreviewValid = false;
        popupStatus.clear();
        popupCandidates = ScanGroupedXmlCandidates(pending.xmlType, pending.targetPath);
    }

    void UpdatePopupPreview(const GroupBackupDatabase::PendingRestore& pending)
    {
        popupPreviewStats = {};
        popupPreviewValid = false;
        std::string previewStatus;
        if (popupSelectionKind == RestoreSelectionKind::Database)
            popupPreviewValid = GroupBackupDatabase::PreviewRestore(
                popupRestoreId, pending.targetPath, popupPreviewStats, previewStatus);
        else if (popupSelectionKind == RestoreSelectionKind::SavedXml)
            popupPreviewValid = GroupBackupDatabase::PreviewRestoreFromXml(
                popupRestoreXmlPath, pending.targetPath, pending.xmlType,
                popupPreviewStats, previewStatus);
        if (!popupPreviewValid) popupStatus = previewStatus;
        else popupStatus.clear();
    }

    bool HasXmlExtension(const std::string& name)
    {
        if (name.size() < 4) return false;
        std::string suffix = name.substr(name.size() - 4);
        std::transform(suffix.begin(), suffix.end(), suffix.begin(),
            [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        return suffix == ".xml";
    }

    bool ValidRebuildFileName(const std::string& name)
    {
        if (name.empty() || name == "." || name == "..") return false;
        return name.find_first_of("\\/:*?\"<>|") == std::string::npos;
    }

    const LiveMapInfo* FindLiveMapInfo(unsigned mapId)
    {
        static constexpr LiveMapInfo maps[] =
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
        for (const LiveMapInfo& map : maps)
            if (map.liveMapId == mapId) return &map;
        return nullptr;
    }

    bool BuildLegacyRootFromMumble(int expectedXmlType,
        std::string& rootOpenTag, std::string& error)
    {
        Mumble::Data* mumble = AppRuntime::GetMumble();
        if (mumble == nullptr || mumble->Context.MapID == 0)
        {
            error = "Load into the Homestead or Guild Hall for this backup before rebuilding it.";
            return false;
        }
        const LiveMapInfo* map = FindLiveMapInfo(mumble->Context.MapID);
        if (map == nullptr)
        {
            error = "The current map is not a supported Homestead or Guild Hall.";
            return false;
        }
        if (map->xmlType != expectedXmlType)
        {
            error = expectedXmlType == 0
                ? "This is a Homestead backup. Load into the intended Homestead before rebuilding it."
                : "This is a Guild Hall backup. Load into the intended Guild Hall before rebuilding it.";
            return false;
        }
        rootOpenTag = "<Decorations version=\"1\" mapId=\"" +
            std::to_string(map->xmlMapId) + "\" mapName=\"" + map->mapName +
            "\" type=\"" + std::to_string(map->xmlType) + "\">";
        return true;
    }
}

void GroupBackupRestoreTab::Render()
{
    InitializeXmlList();
    const bool automatic = AppSettings::Get().automaticGroupBackupRestore;
    ImGui::TextUnformatted("Auto backup/restore is ");
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextColored(automatic ? ImVec4(0.25f, 0.90f, 0.35f, 1.0f)
        : ImVec4(1.0f, 0.30f, 0.25f, 1.0f), automatic ? "enabled" : "disabled");
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextUnformatted(" in Settings.");

    ImGui::Dummy({ 0.0f, 12.0f });
    RenderSectionHeading("Import");
    ImGui::Dummy({ 0.0f, 8.0f });
    ImGui::TextUnformatted("Import Decoration XML");
    if (ImGui::RadioButton("Homestead##GroupBackup", &selectedFolderType, 0)) RefreshXmlList();
    ImGui::SameLine();
    if (ImGui::RadioButton("Guild Hall##GroupBackup", &selectedFolderType, 1)) RefreshXmlList();
    const bool hasSelection = selectedXmlIndex >= 0 &&
        selectedXmlIndex < static_cast<int>(availableXmlFiles.size());
    const char* selectedName = hasSelection
        ? availableXmlFiles[static_cast<size_t>(selectedXmlIndex)].name.c_str()
        : "No XML files available";
    ImGui::SetNextItemWidth(-1.0f);
    XmlComboHelpers::SetPopupWidth(availableXmlFiles);
    if (ImGui::BeginCombo("##GroupBackupXmlList", selectedName))
    {
        for (size_t index = 0; index < availableXmlFiles.size(); ++index)
        {
            const bool selected = selectedXmlIndex == static_cast<int>(index);
            if (ImGui::Selectable(availableXmlFiles[index].name.c_str(), selected))
                selectedXmlIndex = static_cast<int>(index);
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    const float halfWidth =
        (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Refresh List##GroupBackup", { halfWidth, 0.0f })) RefreshXmlList();
    ImGui::SameLine();
    if (hasSelection)
    {
        if (ImGui::Button("Import Selected##GroupBackup", { halfWidth, 0.0f }))
            ImportSelected();
    }
    else RenderDisabledButton("Import Selected##GroupBackup", { halfWidth, 0.0f });

    ImGui::Dummy({ 0.0f, 16.0f });
    RenderSectionHeading("Manual Backup");
    ImGui::TextWrapped("Create a permanent complete-XML restore point, including every grouped and ungrouped decoration.");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##ManualBackupName", "Optional custom backup name",
        backupName.data(), backupName.size());
    if (!importedPath.empty() && importedPropCount > 0)
    {
        if (ImGui::Button("Create Manual Backup"))
        {
            if (GroupBackupDatabase::RecordFile(importedPath, importedType,
                GroupBackupDatabase::RestorePointType::Manual,
                backupName.data(), status)) backupName.fill('\0');
        }
    }
    else RenderDisabledButton("Create Manual Backup");
    if (!importedPath.empty())
        ImGui::TextDisabled("%s: %d decorations, %d groups", importedName.c_str(),
            static_cast<int>(importedPropCount), static_cast<int>(importedGroupCount));

    ImGui::Dummy({ 0.0f, 16.0f });
    RenderSectionHeading("Restore Groups");

    constexpr const char* filterLabels[] =
    {
        "All Restore Options",
        "Saved XMLs",
        "Automatic Backups",
        "Manual Backups",
        "Safety Backups"
    };
    int filterIndex = static_cast<int>(restoreSourceFilter);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##RestoreSourceFilter", &filterIndex,
        filterLabels, static_cast<int>(sizeof(filterLabels) / sizeof(filterLabels[0]))))
    {
        restoreSourceFilter = static_cast<RestoreSourceFilter>(filterIndex);
        ClearRestoreSelection();
    }

    const std::vector<GroupBackupDatabase::RestorePointSummary> points =
        importedType >= 0
            ? GroupBackupDatabase::GetRestorePoints(importedType)
            : std::vector<GroupBackupDatabase::RestorePointSummary>{};
    ImGui::TextUnformatted("Choose Restore Source");
    ImGui::BeginChild("##ManualRestoreSourceList", { 0.0f, 190.0f }, true);
    bool displayedAny = false;
    if (restoreSourceFilter == RestoreSourceFilter::All ||
        restoreSourceFilter == RestoreSourceFilter::SavedXml)
    {
        if (!manualXmlCandidates.empty()) ImGui::TextDisabled("Saved XMLs");
        for (const GroupedXmlCandidate& candidate : manualXmlCandidates)
        {
            displayedAny = true;
            const std::string visibleLabel = candidate.name + " | saved XML | " +
                std::to_string(candidate.groupCount) + " groups";
            const std::string itemLabel = visibleLabel + "##SavedXml" + candidate.path;
            const bool selected = restoreSelectionKind == RestoreSelectionKind::SavedXml &&
                selectedRestoreXmlPath == candidate.path;
            if (ImGui::Selectable(itemLabel.c_str(), selected))
            {
                restoreSelectionKind = RestoreSelectionKind::SavedXml;
                selectedRestoreXmlPath = candidate.path;
                selectedRestoreId.clear();
                selectedRestoreLabel = visibleLabel;
                UpdatePreview();
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
    }

    constexpr GroupBackupDatabase::RestorePointType types[] =
    {
        GroupBackupDatabase::RestorePointType::Auto,
        GroupBackupDatabase::RestorePointType::Manual,
        GroupBackupDatabase::RestorePointType::Safety
    };
    for (GroupBackupDatabase::RestorePointType type : types)
    {
        const bool typeAllowed = restoreSourceFilter == RestoreSourceFilter::All ||
            (restoreSourceFilter == RestoreSourceFilter::Automatic &&
                type == GroupBackupDatabase::RestorePointType::Auto) ||
            (restoreSourceFilter == RestoreSourceFilter::Manual &&
                type == GroupBackupDatabase::RestorePointType::Manual) ||
            (restoreSourceFilter == RestoreSourceFilter::Safety &&
                type == GroupBackupDatabase::RestorePointType::Safety);
        if (!typeAllowed) continue;
        bool headingWritten = false;
        for (const auto& point : points)
        {
            if (point.type != type || point.groupCount == 0) continue;
            if (!headingWritten)
            {
                if (displayedAny) ImGui::Separator();
                const char* heading = type == GroupBackupDatabase::RestorePointType::Auto
                    ? "Automatic Backups" :
                    type == GroupBackupDatabase::RestorePointType::Manual
                        ? "Manual Backups" : "Safety Backups";
                ImGui::TextDisabled("%s", heading);
                headingWritten = true;
            }
            displayedAny = true;
            const std::string visibleLabel = RestorePointLabel(point);
            const std::string itemLabel = visibleLabel + "##RestorePoint" + point.id;
            const bool selected = restoreSelectionKind == RestoreSelectionKind::Database &&
                selectedRestoreId == point.id;
            if (ImGui::Selectable(itemLabel.c_str(), selected))
            {
                restoreSelectionKind = RestoreSelectionKind::Database;
                selectedRestoreId = point.id;
                selectedRestoreXmlPath.clear();
                selectedRestoreLabel = visibleLabel;
                UpdatePreview();
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
    }
    if (!displayedAny) ImGui::TextDisabled("No restore sources available for this filter.");
    ImGui::EndChild();
    if (!selectedRestoreLabel.empty())
        ImGui::TextWrapped("Selected: %s", selectedRestoreLabel.c_str());
    if (previewValid)
    {
        ImGui::Text("Matched: %d", static_cast<int>(previewStats.matched));
        ImGui::SameLine();
        ImGui::Text("Missing/Modified: %d", static_cast<int>(previewStats.missingOrModified));
        ImGui::SameLine();
        ImGui::Text("Ungrouped: %d", static_cast<int>(previewStats.leftUngrouped));
    }
    if (!importedPath.empty() &&
        restoreSelectionKind != RestoreSelectionKind::None && previewValid)
    {
        if (ImGui::Button("Restore Selected Groups"))
        {
            GroupBackupDatabase::RestoreStats stats;
            bool restored = false;
            if (restoreSelectionKind == RestoreSelectionKind::Database)
            {
                restored = GroupBackupDatabase::Restore(
                    selectedRestoreId, importedPath, stats, status);
            }
            else
            {
                restored = GroupBackupDatabase::RestoreFromXml(
                    selectedRestoreXmlPath, importedPath, importedType, stats, status);
            }
            if (restored)
            {
                GroupBackupDatabase::InspectFile(importedPath, importedType,
                    importedGroupCount, importedPropCount);
                manualXmlCandidates = ScanGroupedXmlCandidates(importedType, importedPath);
            }
            UpdatePreview();
        }
    }
    else RenderDisabledButton("Restore Selected Groups");
    ImGui::SameLine();
    if (ImGui::Button("Manage Backups..."))
    {
        manageWindowOpen = true;
        manageStatus.clear();
    }

    ImGui::Dummy({ 0.0f, 16.0f });
    RenderSectionHeading("Emergency XML Recovery");
    ImGui::TextWrapped(
        "OOPS! Did you accidentally delete an XML file? Now you can rebuild it from the last known group backup."
    );
    if (ImGui::Button("Rebuild XML..."))
    {
        rebuildWindowOpen = true;
        rebuildSelectionId.clear();
        rebuildName.fill('\0');
        rebuildStatus.clear();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("%s", status.c_str());
}

void GroupBackupRestoreTab::RenderAutoRestorePopup()
{
    const GroupBackupDatabase::PendingRestore pending =
        GroupBackupDatabase::GetPendingRestore();
    if (!pending.active)
    {
        popupTargetPath.clear();
        return;
    }
    if (pending.confirmationRequired)
    {
        if (!ImGui::IsPopupOpen("Check for Group Restore"))
            ImGui::OpenPopup("Check for Group Restore");
        ImGui::SetNextWindowSize({ 470.0f, 190.0f }, ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Check for Group Restore", nullptr,
            ImGuiWindowFlags_NoResize))
        {
            ImGui::TextWrapped("Is this your first time importing this file?");
            ImGui::Spacing();
            ImGui::TextDisabled("Target: %s", pending.targetName.c_str());
            ImGui::Spacing();
            const float width =
                (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            if (ImGui::Button("Yes - Import as New", { width, 0.0f }))
            {
                GroupBackupDatabase::IgnorePendingRestore();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("No - Restore Groups Now", { width, 0.0f }))
            {
                GroupBackupDatabase::ConfirmPendingRestore();
                ImGui::CloseCurrentPopup();
            }
            ImGui::Spacing();
            constexpr float helpIconSize = 18.0f;
            const ImVec2 helpIconPosition = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##GroupRestoreWhy", { helpIconSize, helpIconSize });
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImU32 helpColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
            const ImVec2 helpCenter = {
                helpIconPosition.x + helpIconSize * 0.5f,
                helpIconPosition.y + helpIconSize * 0.5f
            };
            drawList->AddCircle(helpCenter, 7.5f, helpColor, 20, 1.5f);
            const ImVec2 questionSize = ImGui::CalcTextSize("?");
            drawList->AddText({
                helpCenter.x - questionSize.x * 0.5f,
                helpCenter.y - questionSize.y * 0.5f
            }, helpColor, "?");
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(
                    "Deco Tools has either recognized possible group backups matching this file's name or structure, or Backup Ungrouped XMLs is enabled.");
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Why am I seeing this window?");
            ImGui::EndPopup();
        }
        return;
    }
    if (popupTargetPath != pending.targetPath)
    {
        RefreshPopupCandidates(pending);
    }
    if (!ImGui::IsPopupOpen("Group Auto-Restore"))
    {
        ImGui::OpenPopup("Group Auto-Restore");
    }
    ImGui::SetNextWindowSize({ 620.0f, 440.0f }, ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Group Auto-Restore", nullptr,
        ImGuiWindowFlags_NoResize))
    {
        ImGui::TextWrapped(
            "Couldn't find valid entry to auto-restore Groups. Please choose a restore source."
        );
        ImGui::Spacing();
        ImGui::TextDisabled("Target: %s", pending.targetName.c_str());
        ImGui::Separator();
        ImGui::BeginChild("##AutoRestoreCandidates", { 0.0f, 255.0f }, true);
        if (popupRestorePoints.empty() && popupCandidates.empty())
        {
            ImGui::TextWrapped("No compatible database backups or saved XMLs containing groups were found.");
        }
        if (!popupRestorePoints.empty()) ImGui::TextDisabled("Database Backups");
        for (const auto& point : popupRestorePoints)
        {
            const std::string visibleLabel = RestorePointLabel(point);
            const std::string label = visibleLabel + "##AutoDb" + point.id;
            const bool selected = popupSelectionKind == RestoreSelectionKind::Database &&
                popupRestoreId == point.id;
            if (ImGui::Selectable(label.c_str(), selected))
            {
                popupSelectionKind = RestoreSelectionKind::Database;
                popupRestoreId = point.id;
                popupRestoreXmlPath.clear();
                UpdatePopupPreview(pending);
            }
        }
        if (!popupRestorePoints.empty() && !popupCandidates.empty()) ImGui::Separator();
        if (!popupCandidates.empty()) ImGui::TextDisabled("Saved XMLs");
        for (const GroupedXmlCandidate& candidate : popupCandidates)
        {
            const std::string visibleLabel = candidate.name + " | saved XML | " +
                std::to_string(candidate.groupCount) + " groups";
            const std::string label = visibleLabel + "##AutoXml" + candidate.path;
            const bool selected = popupSelectionKind == RestoreSelectionKind::SavedXml &&
                popupRestoreXmlPath == candidate.path;
            if (ImGui::Selectable(label.c_str(), selected))
            {
                popupSelectionKind = RestoreSelectionKind::SavedXml;
                popupRestoreXmlPath = candidate.path;
                popupRestoreId.clear();
                UpdatePopupPreview(pending);
            }
        }
        ImGui::EndChild();
        if (popupPreviewValid)
        {
            ImGui::Text("Matched: %d", static_cast<int>(popupPreviewStats.matched));
            ImGui::SameLine();
            ImGui::Text("Missing/Modified: %d",
                static_cast<int>(popupPreviewStats.missingOrModified));
            ImGui::SameLine();
            ImGui::Text("Ungrouped: %d",
                static_cast<int>(popupPreviewStats.leftUngrouped));
        }
        if (!popupStatus.empty()) ImGui::TextWrapped("%s", popupStatus.c_str());
        const float width =
            (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (popupSelectionKind != RestoreSelectionKind::None && popupPreviewValid)
        {
            if (ImGui::Button("Restore", { width, 0.0f }))
            {
                GroupBackupDatabase::RestoreStats stats;
                const bool restored = popupSelectionKind == RestoreSelectionKind::Database
                    ? GroupBackupDatabase::Restore(
                        popupRestoreId, pending.targetPath, stats, popupStatus)
                    : GroupBackupDatabase::RestoreFromXml(
                        popupRestoreXmlPath, pending.targetPath,
                        pending.xmlType, stats, popupStatus);
                if (restored)
                {
                    status = popupStatus + " Import the XML again to load the restored groups.";
                    GroupBackupDatabase::ClearPendingRestore();
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        else RenderDisabledButton("Restore", { width, 0.0f });
        ImGui::SameLine();
        if (ImGui::Button("Ignore", { width, 0.0f }))
        {
            GroupBackupDatabase::IgnorePendingRestore();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void GroupBackupRestoreTab::RenderManageBackupsWindow()
{
    if (!manageWindowOpen) return;
    ImGui::SetNextWindowSize({ 720.0f, 520.0f }, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Manage Group Backups", &manageWindowOpen))
    {
        ImGui::End();
        return;
    }

    constexpr const char* manageFilterLabels[] =
        { "All Manageable Backups", "Manual Backups", "Safety Backups" };
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::Combo("Filter", &manageFilter, manageFilterLabels,
        static_cast<int>(sizeof(manageFilterLabels) / sizeof(manageFilterLabels[0]))))
    {
        managedSelections.clear();
        renameSelectionId.clear();
        renameBuffer.fill('\0');
    }

    const std::vector<GroupBackupDatabase::RestorePointSummary> points =
        GroupBackupDatabase::GetRestorePoints(-1);
    std::unordered_set<std::string> existingIds;
    for (const auto& point : points)
        if (point.type != GroupBackupDatabase::RestorePointType::Auto)
            existingIds.insert(point.id);
    for (auto iterator = managedSelections.begin(); iterator != managedSelections.end();)
    {
        if (existingIds.find(*iterator) == existingIds.end())
            iterator = managedSelections.erase(iterator);
        else ++iterator;
    }

    auto visibleInManager = [](const GroupBackupDatabase::RestorePointSummary& point)
    {
        if (point.type == GroupBackupDatabase::RestorePointType::Auto) return false;
        if (manageFilter == 1)
            return point.type == GroupBackupDatabase::RestorePointType::Manual;
        if (manageFilter == 2)
            return point.type == GroupBackupDatabase::RestorePointType::Safety;
        return true;
    };

    if (ImGui::Button("Select All Visible"))
        for (const auto& point : points)
            if (visibleInManager(point)) managedSelections.insert(point.id);
    ImGui::SameLine();
    if (ImGui::Button("Clear Selection")) managedSelections.clear();
    ImGui::SameLine();
    ImGui::TextDisabled("%d selected", static_cast<int>(managedSelections.size()));

    ImGui::BeginChild("##ManagedBackupList", { 0.0f, 285.0f }, true);
    bool displayedAny = false;
    for (const auto& point : points)
    {
        if (!visibleInManager(point)) continue;
        displayedAny = true;
        bool selected = managedSelections.find(point.id) != managedSelections.end();
        const std::string checkboxId = "##ManageBackup" + point.id;
        if (ImGui::Checkbox(checkboxId.c_str(), &selected))
        {
            if (selected) managedSelections.insert(point.id);
            else managedSelections.erase(point.id);
        }
        ImGui::SameLine();
        const std::string name = point.customName.empty() ? point.xmlName : point.customName;
        ImGui::TextWrapped("%s | %s | %s | %s | %d groups | %d props",
            name.c_str(), RestorePointTypeLabel(point.type),
            point.xmlType == 1 ? "Guild Hall" : "Homestead",
            point.createdUtc.c_str(), static_cast<int>(point.groupCount),
            static_cast<int>(point.propCount));
        ImGui::TextDisabled("    Source XML: %s", point.xmlName.c_str());
        ImGui::Separator();
    }
    if (!displayedAny)
        ImGui::TextDisabled("No manual or safety backups are available for this filter.");
    ImGui::EndChild();

    const GroupBackupDatabase::RestorePointSummary* singleSelection = nullptr;
    if (managedSelections.size() == 1)
    {
        const std::string& selectedId = *managedSelections.begin();
        for (const auto& point : points)
            if (point.id == selectedId) { singleSelection = &point; break; }
    }
    if (singleSelection != nullptr && renameSelectionId != singleSelection->id)
    {
        renameSelectionId = singleSelection->id;
        renameBuffer.fill('\0');
        const std::string initialName = singleSelection->customName.empty()
            ? singleSelection->xmlName : singleSelection->customName;
        std::snprintf(renameBuffer.data(), renameBuffer.size(), "%s", initialName.c_str());
    }
    else if (singleSelection == nullptr)
    {
        renameSelectionId.clear();
        renameBuffer.fill('\0');
    }

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##RenameBackup", "New name for one selected backup",
        renameBuffer.data(), renameBuffer.size());
    if (singleSelection != nullptr)
    {
        if (ImGui::Button("Rename Selected"))
        {
            if (GroupBackupDatabase::RenameRestorePoint(
                singleSelection->id, renameBuffer.data(), manageStatus))
            {
                if (selectedRestoreId == singleSelection->id) ClearRestoreSelection();
                renameSelectionId.clear();
            }
        }
    }
    else RenderDisabledButton("Rename Selected");
    ImGui::SameLine();
    if (!managedSelections.empty())
    {
        if (ImGui::Button("Delete Selected..."))
            ImGui::OpenPopup("Delete Group Backups?");
    }
    else RenderDisabledButton("Delete Selected...");

    if (!manageStatus.empty()) ImGui::TextWrapped("%s", manageStatus.c_str());

    if (ImGui::BeginPopupModal("Delete Group Backups?", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped(
            "Delete %d selected backup(s)? This removes only database restore points; XML files are not deleted.",
            static_cast<int>(managedSelections.size()));
        ImGui::Spacing();
        if (ImGui::Button("Delete", { 140.0f, 0.0f }))
        {
            const std::vector<std::string> ids(
                managedSelections.begin(), managedSelections.end());
            size_t deletedCount = 0;
            if (GroupBackupDatabase::DeleteRestorePoints(
                ids, deletedCount, manageStatus))
            {
                if (restoreSelectionKind == RestoreSelectionKind::Database &&
                    managedSelections.find(selectedRestoreId) != managedSelections.end())
                    ClearRestoreSelection();
                managedSelections.clear();
                renameSelectionId.clear();
                renameBuffer.fill('\0');
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 140.0f, 0.0f }))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::End();
}

void GroupBackupRestoreTab::RenderRebuildXmlWindow()
{
    if (rebuildWindowOpen)
    {
        ImGui::SetNextWindowSize({ 720.0f, 500.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Rebuild Decoration XML", &rebuildWindowOpen))
        {
            ImGui::TextWrapped(
                "Choose the backup that most closely matches the lost layout. New backups contain the complete XML; older backups can recover grouped decorations only."
            );
            ImGui::Separator();
            const std::vector<GroupBackupDatabase::RestorePointSummary> points =
                GroupBackupDatabase::GetRestorePoints(-1);
            ImGui::BeginChild("##RebuildBackupList", { 0.0f, 285.0f }, true);
            if (points.empty()) ImGui::TextDisabled("No group backups are available.");
            for (const auto& point : points)
            {
                const std::string name = point.customName.empty()
                    ? point.xmlName : point.customName + " (" + point.xmlName + ")";
                const std::string label = name + " | " + RestorePointTypeLabel(point.type) +
                    " | " + point.createdUtc + " | " +
                    std::to_string(point.propCount) + " decorations | " +
                    (point.completeXml ? "complete XML" : "grouped decorations only") +
                    "##Rebuild" + point.id;
                const bool selected = rebuildSelectionId == point.id;
                if (ImGui::Selectable(label.c_str(), selected))
                {
                    rebuildSelectionId = point.id;
                    std::string defaultName = Utf8Paths::ToUtf8(
                        Utf8Paths::FromUtf8(point.xmlName).stem()) + "_REBUILT.xml";
                    rebuildName.fill('\0');
                    std::snprintf(rebuildName.data(), rebuildName.size(), "%s",
                        defaultName.c_str());
                    rebuildStatus.clear();
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndChild();

            ImGui::TextUnformatted("New XML Name");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##RebuildXmlName", "Recovered layout name",
                rebuildName.data(), rebuildName.size());
            if (!rebuildSelectionId.empty())
            {
                if (ImGui::Button("Rebuild", { 150.0f, 0.0f }))
                {
                    const auto selected = std::find_if(points.begin(), points.end(),
                        [](const GroupBackupDatabase::RestorePointSummary& point)
                        { return point.id == rebuildSelectionId; });
                    std::string fileName = rebuildName.data();
                    if (!HasXmlExtension(fileName)) fileName += ".xml";
                    if (selected == points.end())
                    {
                        rebuildSucceeded = false;
                        rebuildResultMessage = "The selected backup no longer exists.";
                    }
                    else if (!ValidRebuildFileName(fileName))
                    {
                        rebuildSucceeded = false;
                        rebuildResultMessage =
                            "Enter a valid filename without folder separators or reserved characters.";
                    }
                    else
                    {
                        const std::string folder = FolderForType(selected->xmlType);
                        if (folder.empty())
                        {
                            rebuildSucceeded = false;
                            rebuildResultMessage =
                                "Set the matching default XML folder in Settings first.";
                        }
                        else
                        {
                            const std::string outputPath = Utf8Paths::ToUtf8(
                                Utf8Paths::FromUtf8(folder) / Utf8Paths::FromUtf8(fileName));
                            GroupBackupDatabase::RebuildResult result;
                            std::string legacyRootOpenTag;
                            rebuildSucceeded = selected->completeXml ||
                                BuildLegacyRootFromMumble(selected->xmlType,
                                    legacyRootOpenTag, rebuildResultMessage);
                            if (rebuildSucceeded)
                                rebuildSucceeded = GroupBackupDatabase::RebuildXml(
                                    selected->id, outputPath, legacyRootOpenTag,
                                    result, rebuildResultMessage);
                            if (rebuildSucceeded)
                                rebuildResultMessage += " Saved as " + fileName + ".";
                        }
                    }
                    rebuildWindowOpen = false;
                    rebuildResultPending = true;
                }
            }
            else RenderDisabledButton("Rebuild", { 150.0f, 0.0f });
            if (!rebuildStatus.empty()) ImGui::TextWrapped("%s", rebuildStatus.c_str());
        }
        ImGui::End();
    }

    if (rebuildResultPending)
    {
        ImGui::OpenPopup("XML Rebuild Result");
        rebuildResultPending = false;
    }
    if (ImGui::BeginPopupModal("XML Rebuild Result", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextColored(rebuildSucceeded
            ? ImVec4(0.25f, 0.90f, 0.35f, 1.0f)
            : ImVec4(1.0f, 0.30f, 0.25f, 1.0f),
            rebuildSucceeded ? "Rebuild successful" : "Rebuild failed");
        ImGui::PushTextWrapPos(520.0f);
        ImGui::TextWrapped("%s", rebuildResultMessage.c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::Button("OK", { 140.0f, 0.0f })) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void GroupBackupRestoreTab::ClearImportedData()
{
    importedPath.clear();
    importedName.clear();
    importedType = -1;
    importedGroupCount = 0;
    importedPropCount = 0;
    ClearRestoreSelection();
    manualXmlCandidates.clear();
    backupName.fill('\0');
    status = "No XML imported";
}
