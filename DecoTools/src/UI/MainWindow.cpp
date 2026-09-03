// Pewpew's Deco Tools - Main Window
// Builds the primary ImGui window, manages tool navigation and cleanup, renders
// addon options, and coordinates the separate decoration counter window.

#include "MainWindow.h"

#include "../Core/AppSettings.h"
#include "../Core/AppRuntime.h"
#include "../imgui/imgui.h"
#include "DecorationCounterWindow.h"
#include "Tabs/DocumentationTab.h"
#include "Tabs/GroupBackupRestoreTab.h"
#include "Tabs/MapSwapTab.h"
#include "Tabs/MergeExtractTab.h"
#include "Tabs/MoveToolTab.h"
#include "Tabs/PatternsTab.h"
#include "Tabs/SettingsTab.h"

namespace
{
    enum class ActiveTab
    {
        None,
        MoveTool,
        Patterns,
        MapSwap,
        MergeExtract,
        GroupBackupRestore,
        Documentation,
        Settings
    };

    ActiveTab activeTab = ActiveTab::MoveTool;

    void ActivateTab(ActiveTab next)
    {
        if (activeTab == next)
        {
            return;
        }

        if (activeTab == ActiveTab::MoveTool)
        {
            MoveToolTab::ClearImportedData();
        }
        else if (activeTab == ActiveTab::Patterns)
        {
            PatternsTab::ClearImportedData();
        }
        else if (activeTab == ActiveTab::MapSwap)
        {
            MapSwapTab::ClearImportedData();
        }
        else if (activeTab == ActiveTab::MergeExtract)
        {
            MergeExtractTab::ClearImportedData();
        }
        else if (activeTab == ActiveTab::GroupBackupRestore)
        {
            GroupBackupRestoreTab::ClearImportedData();
        }

        activeTab = next;
    }

    struct NavigationItem
    {
        ActiveTab tab;
        const char* label;
        const char* id;
        const char* texture;
    };

    bool DrawNavigationItem(const NavigationItem& item)
    {
        ImGui::PushID(item.id);
        const bool selected = activeTab == item.tab;
        const bool clicked = ImGui::Selectable(
            "##NavigationItem",
            selected,
            ImGuiSelectableFlags_None,
            ImVec2(0.0f, 38.0f)
        );
        const ImVec2 minimum = ImGui::GetItemRectMin();
        const ImVec4 tint = selected
            ? ImVec4(0.25f, 0.68f, 1.0f, 1.0f)
            : ImVec4(0.82f, 0.84f, 0.88f, 1.0f);

        AddonAPI_t* api = AppRuntime::GetApi();
        Texture_t* texture = api == nullptr || api->Textures_Get == nullptr
            ? nullptr
            : api->Textures_Get(item.texture);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 iconMin(minimum.x + 8.0f, minimum.y + 9.0f);
        if (texture != nullptr && texture->Resource != nullptr)
        {
            draw->AddImage(
                texture->Resource,
                iconMin,
                ImVec2(iconMin.x + 20.0f, iconMin.y + 20.0f),
                ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 1.0f),
                ImGui::ColorConvertFloat4ToU32(tint)
            );
        }
        draw->AddText(
            ImVec2(minimum.x + 36.0f, minimum.y + 11.0f),
            ImGui::ColorConvertFloat4ToU32(tint),
            item.label
        );
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", item.label);
        ImGui::PopID();
        return clicked;
    }
}

void MainWindow::Render()
{
    AppSettings::Data& settings = AppSettings::Get();
    if (!settings.windowVisible)
    {
        DecorationCounterWindow::Render();
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(760.0f, 560.0f), ImGuiCond_FirstUseEver);

    const bool wasVisible = settings.windowVisible;
    if (ImGui::Begin("Pewpew's Deco Tools", &settings.windowVisible))
    {
        constexpr NavigationItem tools[] =
        {
            { ActiveTab::MergeExtract, "Group Tools", "GroupTools", "DECOTOOLS_NAV_GROUP_TOOLS" },
            { ActiveTab::MoveTool, "Move Tool", "Move", "DECOTOOLS_NAV_MOVE" },
            { ActiveTab::Patterns, "Patterns", "Patterns", "DECOTOOLS_NAV_PATTERNS" },
            { ActiveTab::MapSwap, "Map Swap", "MapSwap", "DECOTOOLS_NAV_MAP_SWAP" }
        };
        const NavigationItem groupBackupRestoreItem =
            { ActiveTab::GroupBackupRestore, "Group Backup/Restore", "GroupBackupRestore", "DECOTOOLS_NAV_GROUP_BACKUP" };
        const NavigationItem settingsItem =
            { ActiveTab::Settings, "Settings", "Settings", "DECOTOOLS_NAV_SETTINGS" };
        const NavigationItem documentationItem =
            { ActiveTab::Documentation, "Documentation", "Documentation", "DECOTOOLS_NAV_DOCUMENTATION" };

        ImGui::BeginChild("##DecoToolsNavigation", ImVec2(190.0f, 0.0f), true);
        ImGui::TextDisabled("TOOLS");
        ImGui::Separator();
        for (const NavigationItem& item : tools)
        {
            if (DrawNavigationItem(item)) ActivateTab(item.tab);
        }
        const float footerY = ImGui::GetWindowHeight() - 129.0f;
        if (ImGui::GetCursorPosY() < footerY) ImGui::SetCursorPosY(footerY);
        ImGui::Separator();
        if (DrawNavigationItem(groupBackupRestoreItem)) ActivateTab(groupBackupRestoreItem.tab);
        if (DrawNavigationItem(documentationItem)) ActivateTab(documentationItem.tab);
        if (DrawNavigationItem(settingsItem)) ActivateTab(settingsItem.tab);
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("##DecoToolsContent", ImVec2(0.0f, 0.0f), false);
        if (activeTab == ActiveTab::MoveTool) MoveToolTab::Render();
        else if (activeTab == ActiveTab::Patterns) PatternsTab::Render();
        else if (activeTab == ActiveTab::MapSwap) MapSwapTab::Render();
        else if (activeTab == ActiveTab::MergeExtract) MergeExtractTab::Render();
        else if (activeTab == ActiveTab::GroupBackupRestore) GroupBackupRestoreTab::Render();
        else if (activeTab == ActiveTab::Documentation) DocumentationTab::Render();
        else if (activeTab == ActiveTab::Settings) SettingsTab::Render();
        ImGui::EndChild();
        GroupBackupRestoreTab::RenderAutoRestorePopup();
    }

    ImGui::End();
    GroupBackupRestoreTab::RenderManageBackupsWindow();
    GroupBackupRestoreTab::RenderRebuildXmlWindow();

    if (settings.rememberWindowState && settings.windowVisible != wasVisible)
    {
        AppSettings::MarkDirty();
    }

    DecorationCounterWindow::Render();
}

void MainWindow::RenderOptions()
{
    ImGui::Text("Pewpew's Deco Tools");
    ImGui::Separator();

    AppSettings::Data& settings = AppSettings::Get();
    bool visible = settings.windowVisible;
    if (ImGui::Checkbox("Show main window", &visible))
    {
        SetWindowsVisible(visible);
    }
}

void MainWindow::SetWindowsVisible(bool visible)
{
    AppSettings::Data& settings = AppSettings::Get();
    settings.windowVisible = visible;
    settings.showDecorationCounter = visible;

    if (settings.rememberWindowState)
    {
        AppSettings::MarkDirty();
    }
}

void MainWindow::ToggleWindows()
{
    SetWindowsVisible(!AppSettings::Get().windowVisible);
}
