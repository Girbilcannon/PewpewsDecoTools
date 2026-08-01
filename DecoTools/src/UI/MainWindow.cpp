// Pewpew's Deco Tools - Main Window
// Builds the primary ImGui window, manages tab selection and cleanup, renders
// addon options, and coordinates the separate decoration counter window.

#include "MainWindow.h"

#include "../Core/AppSettings.h"
#include "../imgui/imgui.h"
#include "DecorationCounterWindow.h"
#include "Tabs/MapSwapTab.h"
#include "Tabs/MergeExtractTab.h"
#include "Tabs/MoveToolTab.h"
#include "Tabs/SettingsTab.h"

namespace
{
    enum class ActiveTab
    {
        None,
        MoveTool,
        MapSwap,
        MergeExtract,
        Settings
    };

    ActiveTab activeTab = ActiveTab::None;

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
        else if (activeTab == ActiveTab::MapSwap)
        {
            MapSwapTab::ClearImportedData();
        }
        else if (activeTab == ActiveTab::MergeExtract)
        {
            MergeExtractTab::ClearImportedData();
        }

        activeTab = next;
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

    ImGui::SetNextWindowSize(ImVec2(720.0f, 520.0f), ImGuiCond_FirstUseEver);

    const bool wasVisible = settings.windowVisible;
    if (ImGui::Begin("Pewpew's Deco Tools", &settings.windowVisible))
    {
        if (ImGui::BeginTabBar("DecoToolsTabs"))
        {
            if (ImGui::BeginTabItem("Move Tool"))
            {
                ActivateTab(ActiveTab::MoveTool);
                MoveToolTab::Render();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Map Swap"))
            {
                ActivateTab(ActiveTab::MapSwap);
                MapSwapTab::Render();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Merge / Extract"))
            {
                ActivateTab(ActiveTab::MergeExtract);
                MergeExtractTab::Render();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Settings"))
            {
                ActivateTab(ActiveTab::Settings);
                SettingsTab::Render();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }

    ImGui::End();

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
