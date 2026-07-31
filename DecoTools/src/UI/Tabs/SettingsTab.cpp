#include "SettingsTab.h"

#include "../../Core/AppSettings.h"
#include "../../imgui/imgui.h"

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
}
