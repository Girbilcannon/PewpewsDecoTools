// Pewpew's Deco Tools - Settings Tab
// Provides controls for the GW2 API key, default XML folders, local database
// behavior, window preferences, and Move Tool visualization settings.

#include "SettingsTab.h"

#include "../../Core/AppSettings.h"
#include "../../imgui/imgui.h"
#include "../QuickStartWindow.h"

namespace
{
    void RenderSectionHeading(const char* label)
    {
        ImGui::SetWindowFontScale(1.2f);
        ImGui::Text("%s", label);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Separator();
    }

}

void SettingsTab::Render()
{
    AppSettings::Data& settings = AppSettings::Get();

    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    RenderSectionHeading("Guild Wars 2 API");

    ImGui::TextWrapped(
        "An API key is required for accurate decoration counts."
    );

    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText(
        "##ApiKey",
        settings.apiKey.data(),
        settings.apiKey.size(),
        ImGuiInputTextFlags_Password
    ))
    {
        AppSettings::MarkDirty();
    }

    ImGui::Dummy(ImVec2(0.0f, 16.0f));
    RenderSectionHeading("Default XML Folders");

    if (ImGui::Checkbox(
        "Show XMLs from Sub-Folders",
        &settings.showXmlsFromSubFolders
    ))
    {
        AppSettings::MarkDirty();
    }

    ImGui::Spacing();

    ImGui::Text("Homestead");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText(
        "##HomesteadFolder",
        settings.homesteadFolder.data(),
        settings.homesteadFolder.size()
    ))
    {
        AppSettings::MarkDirty();
    }

    ImGui::Text("Guild Hall");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText(
        "##GuildHallFolder",
        settings.guildHallFolder.data(),
        settings.guildHallFolder.size()
    ))
    {
        AppSettings::MarkDirty();
    }

    ImGui::Dummy(ImVec2(0.0f, 16.0f));
    RenderSectionHeading("Local Data");

    if (ImGui::Checkbox(
        "Check for decoration database updates when the addon loads",
        &settings.checkForDatabaseUpdates
    ))
    {
        AppSettings::MarkDirty();
    }

    if (ImGui::Checkbox(
        "Remember addon window state",
        &settings.rememberWindowState
    ))
    {
        if (!settings.rememberWindowState)
        {
            settings.windowVisible = true;
        }
        AppSettings::MarkDirty();
    }

    if (ImGui::Checkbox(
        "Show decoration count window",
        &settings.showDecorationCounter
    ))
    {
        AppSettings::MarkDirty();
    }

    if (ImGui::Checkbox(
        "Use Deco Tools Interface Style",
        &settings.useDecoToolsInterfaceStyle
    ))
    {
        AppSettings::MarkDirty();
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            "Protects Deco Tools from global Nexus style changes that can disrupt its layout. Disable this to inherit the complete Nexus interface style."
        );
    }

    if (ImGui::Checkbox(
        "Automatically backup and restore XML groups",
        &settings.automaticGroupBackupRestore
    ))
    {
        AppSettings::MarkDirty();
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            "Creates restore points when grouped XMLs are imported or written, and attempts to restore groups when XMLs are imported."
        );
    }

    ImGui::Indent();
    if (ImGui::Checkbox(
        "Backup Ungrouped XMLs",
        &settings.backupUngroupedXmls
    ))
    {
        AppSettings::MarkDirty();
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            "Automatically creates complete rebuild backups for XMLs without named groups. Grouped XML backups always include both grouped and ungrouped decorations."
        );
    }
    ImGui::Unindent();

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

    if (ImGui::ColorEdit4(
        "Box Color",
        settings.boxColor,
        ImGuiColorEditFlags_NoInputs))
    {
        AppSettings::MarkDirty();
    }
    if (ImGui::ColorEdit4(
        "Face Color",
        settings.faceColor,
        ImGuiColorEditFlags_NoInputs))
    {
        AppSettings::MarkDirty();
    }
    if (ImGui::ColorEdit4(
        "Point Color",
        settings.pointColor,
        ImGuiColorEditFlags_NoInputs))
    {
        AppSettings::MarkDirty();
    }

    ImGui::TextDisabled(
        "Decoration point visibility and size are available in the bottom status bar."
    );

    ImGui::Dummy(ImVec2(0.0f, 16.0f));
    RenderSectionHeading("Quick Start Guide");
    ImGui::TextWrapped(
        "Reopen the initial setup pages and guided tutorials at any time."
    );
    ImGui::Spacing();
    if (ImGui::Button("Open Quick Start / Tutorials", ImVec2(-1.0f, 0.0f)))
    {
        QuickStartWindow::Open();
    }
}
