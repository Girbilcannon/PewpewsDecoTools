// Pewpew's Deco Tools - Consistent primary action button styling

#pragma once

#include "../imgui/imgui.h"

namespace PrimaryActionButton
{
    inline bool Draw(
        const char* label,
        const ImVec2& size = ImVec2(0.0f, 0.0f))
    {
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImVec4(0.25f, 0.58f, 0.96f, 1.0f));
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            ImVec4(0.34f, 0.74f, 1.0f, 1.0f));
        ImGui::PushStyleColor(
            ImGuiCol_ButtonActive,
            ImVec4(0.18f, 0.48f, 0.85f, 1.0f));
        const bool pressed = ImGui::Button(label, size);
        ImGui::PopStyleColor(3);
        return pressed;
    }
}
