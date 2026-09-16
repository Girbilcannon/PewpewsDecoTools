#include "SharedXmlWorkspace.h"

#include "AppSettings.h"
#include "GroupBackupDatabase.h"
#include "Utf8Paths.h"
#include "XmlFileUtils.h"
#include "../UI/PrimaryActionButton.h"
#include "../UI/StatusBar.h"
#include "../UI/XmlComboHelpers.h"
#include "../imgui/imgui.h"
#include "../imgui/imgui_internal.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
    using XmlFileEntry = XmlFileUtils::Entry;

    int selectedFolderType = 0;
    int selectedXmlIndex = -1;
    bool fileListInitialized = false;
    bool listedSubFolders = false;
    std::vector<XmlFileEntry> availableXmlFiles;
    std::string importedPath;
    std::string importedFileName;
    std::string status = "Choose an XML file to begin.";
    int importedType = -1;
    size_t importedGroupCount = 0;
    size_t importedPropCount = 0;
    std::uint64_t revision = 0;
    std::filesystem::file_time_type importedWriteTime = {};

    std::string FolderForType(int type)
    {
        const AppSettings::Data& settings = AppSettings::Get();
        return type == 1 ? settings.guildHallFolder.data() : settings.homesteadFolder.data();
    }

    void RefreshXmlList()
    {
        const std::string previousPath = selectedXmlIndex >= 0 &&
            selectedXmlIndex < static_cast<int>(availableXmlFiles.size())
            ? availableXmlFiles[static_cast<size_t>(selectedXmlIndex)].path
            : importedPath;
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
        {
            if (availableXmlFiles[index].path == previousPath)
            {
                selectedXmlIndex = static_cast<int>(index);
                break;
            }
        }
        if (selectedXmlIndex < 0 && !availableXmlFiles.empty()) selectedXmlIndex = 0;
        status = availableXmlFiles.empty()
            ? "No XML files found in this folder."
            : "Found " + std::to_string(availableXmlFiles.size()) +
                (availableXmlFiles.size() == 1 ? " XML file." : " XML files.");
    }

    void ImportSelected()
    {
        if (selectedXmlIndex < 0 ||
            selectedXmlIndex >= static_cast<int>(availableXmlFiles.size())) return;
        const XmlFileEntry& entry = availableXmlFiles[static_cast<size_t>(selectedXmlIndex)];
        const GroupBackupDatabase::ImportResult prepared =
            GroupBackupDatabase::PrepareImport(
                entry.path,
                selectedFolderType,
                AppSettings::Get().automaticGroupBackupRestore,
                AppSettings::Get().backupUngroupedXmls);
        if (prepared.action == GroupBackupDatabase::ImportAction::NeedsUserChoice ||
            prepared.action == GroupBackupDatabase::ImportAction::Error)
        {
            status = prepared.message;
            return;
        }
        int xmlType = -1;
        size_t groupCount = 0;
        size_t propCount = 0;
        if (!GroupBackupDatabase::InspectFile(
            entry.path, xmlType, groupCount, propCount))
        {
            status = "The selected file is not a valid Decorations XML.";
            return;
        }
        importedPath = entry.path;
        importedFileName = entry.name;
        importedType = xmlType;
        importedGroupCount = groupCount;
        importedPropCount = propCount;
        std::error_code error;
        importedWriteTime = std::filesystem::last_write_time(
            Utf8Paths::FromUtf8(importedPath), error);
        ++revision;
        status = "Loaded " + importedFileName + " with " +
            std::to_string(importedPropCount) + " decorations in " +
            (importedGroupCount == 0 ? "Full XML" : "XML Groups") + " mode.";
        if (!prepared.message.empty()) status += " " + prepared.message;
    }

    void RenderDisabledButton(const char* label, ImVec2 size)
    {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
        ImGui::Button(label, size);
        ImGui::PopStyleVar();
        ImGui::PopItemFlag();
    }

    void SelectFolderType(int type)
    {
        if (selectedFolderType == type) return;
        selectedFolderType = type;
        RefreshXmlList();
    }

    void RenderFolderToggle()
    {
        constexpr float ToggleWidth = 46.0f;
        constexpr float ToggleHeight = 22.0f;
        constexpr float LabelSpacing = 9.0f;
        const char* leftLabel = "Homestead";
        const char* rightLabel = "Guild Hall";
        const float rowWidth = ImGui::CalcTextSize(leftLabel).x +
            ImGui::CalcTextSize(rightLabel).x + ToggleWidth + LabelSpacing * 2.0f;
        const float available = ImGui::GetContentRegionAvail().x;
        if (available > rowWidth)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available - rowWidth) * 0.5f);

        const ImVec4 activeText(0.25f, 0.68f, 1.0f, 1.0f);
        const ImVec4 inactiveText = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(
            selectedFolderType == 0 ? activeText : inactiveText,
            "%s",
            leftLabel);
        ImGui::SameLine(0.0f, LabelSpacing);

        ImGui::PushID("SharedImportFolderToggle");
        const bool clicked = ImGui::InvisibleButton(
            "##Toggle",
            ImVec2(ToggleWidth, ToggleHeight));
        const ImVec2 minimum = ImGui::GetItemRectMin();
        const ImVec2 maximum = ImGui::GetItemRectMax();
        const float radius = ToggleHeight * 0.5f;
        const ImU32 track = ImGui::GetColorU32(
            ImGui::IsItemHovered()
                ? ImVec4(0.34f, 0.74f, 1.0f, 1.0f)
                : ImVec4(0.25f, 0.58f, 0.96f, 1.0f));
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(minimum, maximum, track, radius);
        const float knobX = selectedFolderType == 0
            ? minimum.x + radius
            : maximum.x - radius;
        draw->AddCircleFilled(
            ImVec2(knobX, minimum.y + radius),
            radius - 3.0f,
            IM_COL32(245, 247, 251, 255),
            20);
        if (clicked) SelectFolderType(selectedFolderType == 0 ? 1 : 0);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Switch the shared import folder");
        ImGui::PopID();

        ImGui::SameLine(0.0f, LabelSpacing);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(
            selectedFolderType == 1 ? activeText : inactiveText,
            "%s",
            rightLabel);
    }
}

void SharedXmlWorkspace::RenderImporter()
{
    if (!fileListInitialized ||
        listedSubFolders != AppSettings::Get().showXmlsFromSubFolders)
        RefreshXmlList();

    const char* heading = "Import Decoration XML";
    ImGui::SetWindowFontScale(1.2f);
    const float headingWidth = ImGui::CalcTextSize(heading).x;
    ImGui::SetCursorPosX((std::max)(ImGui::GetCursorPosX(),
        ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - headingWidth) * 0.5f));
    ImGui::TextUnformatted(heading);
    ImGui::SetWindowFontScale(1.0f);

    RenderFolderToggle();

    const bool hasSelection = selectedXmlIndex >= 0 &&
        selectedXmlIndex < static_cast<int>(availableXmlFiles.size());
    const char* selectedName = hasSelection
        ? availableXmlFiles[static_cast<size_t>(selectedXmlIndex)].name.c_str()
        : "No XML files available";
    constexpr float ImportHorizontalPadding = 28.0f;
    ImGui::Indent(ImportHorizontalPadding);
    const float controlWidth = (std::max)(
        160.0f,
        ImGui::GetContentRegionAvail().x - ImportHorizontalPadding);
    ImGui::SetNextItemWidth(controlWidth);
    XmlComboHelpers::SetPopupWidth(availableXmlFiles);
    if (ImGui::BeginCombo("##SharedXmlFileList", selectedName))
    {
        for (size_t index = 0; index < availableXmlFiles.size(); ++index)
        {
            const bool selected = selectedXmlIndex == static_cast<int>(index);
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::Selectable(availableXmlFiles[index].name.c_str(), selected))
                selectedXmlIndex = static_cast<int>(index);
            if (selected) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    const float buttonWidth =
        (controlWidth - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Refresh List##SharedImport", { buttonWidth, 0.0f }))
        RefreshXmlList();
    ImGui::SameLine();
    if (hasSelection)
    {
        if (PrimaryActionButton::Draw(
            "Import Selected##SharedImport",
            { buttonWidth, 0.0f }))
            ImportSelected();
    }
    else RenderDisabledButton("Import Selected##SharedImport", { buttonWidth, 0.0f });
    ImGui::Unindent(ImportHorizontalPadding);

    StatusBar::PublishIfChanged(&status, status);
    ImGui::Separator();
    ImGui::Spacing();
}

void SharedXmlWorkspace::RefreshIfChanged()
{
    if (importedPath.empty()) return;
    std::error_code error;
    const std::filesystem::file_time_type current = std::filesystem::last_write_time(
        Utf8Paths::FromUtf8(importedPath), error);
    if (!error && current != importedWriteTime)
    {
        importedWriteTime = current;
        int xmlType = -1;
        size_t groupCount = 0;
        size_t propCount = 0;
        if (GroupBackupDatabase::InspectFile(
            importedPath, xmlType, groupCount, propCount))
        {
            importedType = xmlType;
            importedGroupCount = groupCount;
            importedPropCount = propCount;
            ++revision;
        }
    }
}

bool SharedXmlWorkspace::AdoptGeneratedFile(const std::string& path)
{
    int xmlType = -1;
    size_t groupCount = 0;
    size_t propCount = 0;
    if (!GroupBackupDatabase::InspectFile(path, xmlType, groupCount, propCount))
        return false;
    importedPath = path;
    importedFileName = Utf8Paths::ToUtf8(Utf8Paths::FromUtf8(path).filename());
    importedType = xmlType;
    importedGroupCount = groupCount;
    importedPropCount = propCount;
    selectedFolderType = xmlType;
    std::error_code error;
    importedWriteTime = std::filesystem::last_write_time(
        Utf8Paths::FromUtf8(importedPath), error);
    RefreshXmlList();
    ++revision;
    status = "Continuing with " + importedFileName + " in " +
        (importedGroupCount == 0 ? "Full XML" : "XML Groups") + " mode.";
    return true;
}

bool SharedXmlWorkspace::NotifyCurrentFileChanged()
{
    if (importedPath.empty()) return false;
    int xmlType = -1;
    size_t groupCount = 0;
    size_t propCount = 0;
    if (!GroupBackupDatabase::InspectFile(
        importedPath, xmlType, groupCount, propCount)) return false;
    importedType = xmlType;
    importedGroupCount = groupCount;
    importedPropCount = propCount;
    std::error_code error;
    importedWriteTime = std::filesystem::last_write_time(
        Utf8Paths::FromUtf8(importedPath), error);
    ++revision;
    status = "Updated " + importedFileName + "; " +
        (importedGroupCount == 0 ? "Full XML" : "XML Groups") + " mode is active.";
    return true;
}

bool SharedXmlWorkspace::HasImport() { return !importedPath.empty(); }
const std::string& SharedXmlWorkspace::Path() { return importedPath; }
const std::string& SharedXmlWorkspace::FileName() { return importedFileName; }
int SharedXmlWorkspace::XmlType() { return importedType; }
bool SharedXmlWorkspace::HasGroups() { return importedGroupCount > 0; }
std::uint64_t SharedXmlWorkspace::Revision() { return revision; }
