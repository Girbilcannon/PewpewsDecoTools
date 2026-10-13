// Pewpew's Deco Tools - New Decorations Window
// Displays decorations first discovered during incremental API catalog updates.

#include "NewDecos.h"

#include "../Core/AppSettings.h"
#include "../Core/DecorationDatabase.h"
#include "../imgui/imgui.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    bool windowOpen = false;

    void RenderList(
        const std::vector<std::string>& names,
        const char* emptyMessage)
    {
        if (names.empty())
        {
            ImGui::TextDisabled("%s", emptyMessage);
            return;
        }

        for (size_t index = 0; index < names.size(); ++index)
        {
            ImGui::TextWrapped("%s", names[index].c_str());
            if (index + 1 < names.size())
            {
                ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight() * 0.55f));
            }
        }
    }
}

void NewDecos::Initialize()
{
    windowOpen = AppSettings::Get().autoShowNewDecorations &&
        DecorationDatabase::HasNewDecorations();
}

void NewDecos::Open()
{
    windowOpen = true;
}

void NewDecos::Render()
{
    if (DecorationDatabase::ConsumeNewDecorationsAdded() &&
        AppSettings::Get().autoShowNewDecorations)
    {
        windowOpen = true;
    }

    if (!windowOpen) return;

    ImGui::SetNextWindowSize(ImVec2(650.0f, 430.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("New Decorations", &windowOpen))
    {
        ImGui::SetWindowFontScale(1.3f);
        ImGui::TextUnformatted("New Decorations");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Separator();
        ImGui::Spacing();

        const float footerHeight =
            ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y * 3.0f;
        const float listAreaHeight = (std::max)(
            1.0f, ImGui::GetContentRegionAvail().y - footerHeight);
        if (ImGui::BeginTable(
            "##NewDecorationLists",
            2,
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame,
            ImVec2(0.0f, listAreaHeight)))
        {
            ImGui::TableSetupColumn("Homestead");
            ImGui::TableSetupColumn("Guild Hall");
            ImGui::TableHeadersRow();
            ImGui::TableNextRow();

            const float childHeight = (std::max)(
                1.0f,
                listAreaHeight - ImGui::GetTextLineHeightWithSpacing() * 1.6f);
            ImGui::TableSetColumnIndex(0);
            ImGui::BeginChild(
                "##NewHomesteadDecorations",
                ImVec2(0.0f, childHeight),
                false);
            RenderList(
                DecorationDatabase::GetNewDecorationNames(0),
                "No recent Homestead discoveries.");
            ImGui::EndChild();

            ImGui::TableSetColumnIndex(1);
            ImGui::BeginChild(
                "##NewGuildDecorations",
                ImVec2(0.0f, childHeight),
                false);
            RenderList(
                DecorationDatabase::GetNewDecorationNames(1),
                "No recent Guild Hall discoveries.");
            ImGui::EndChild();
            ImGui::EndTable();
        }

        ImGui::Spacing();
        constexpr float ClearButtonWidth = 112.0f;
        const float messageWidth = (std::max)(
            1.0f,
            ImGui::GetContentRegionAvail().x -
                ClearButtonWidth - ImGui::GetStyle().ItemSpacing.x);
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + messageWidth);
        ImGui::TextDisabled(
            "Decorations older than 14 days on this list will be pruned.");
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        ImGui::SameLine();
        if (ImGui::Button("Clear List Now", ImVec2(ClearButtonWidth, 0.0f)))
        {
            DecorationDatabase::ClearNewDecorations();
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "Clears only the New Decorations history. The main decoration database is unchanged.");
        }
    }
    ImGui::End();
}
