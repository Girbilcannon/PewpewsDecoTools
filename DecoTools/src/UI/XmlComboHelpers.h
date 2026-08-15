// Pewpew's Deco Tools - XML Dropdown Display Helpers
// Sizes XML selection popups to display long filenames and subfolder paths
// without changing the width of the addon window or closed dropdown control.

#pragma once

#include "../imgui/imgui.h"

#include <algorithm>
#include <vector>

namespace XmlComboHelpers
{
    template <typename Entry>
    void SetPopupWidth(const std::vector<Entry>& entries)
    {
        float widest = ImGui::GetContentRegionAvail().x;
        for (const Entry& entry : entries)
        {
            widest = (std::max)(
                widest,
                ImGui::CalcTextSize(entry.name.c_str()).x +
                    ImGui::GetStyle().FramePadding.x * 4.0f
            );
        }

        const float displayLimit = (std::max)(
            80.0f,
            ImGui::GetIO().DisplaySize.x - 24.0f
        );
        const float popupWidth = (std::min)(widest, displayLimit);
        const float popupHeight =
            ImGui::GetTextLineHeightWithSpacing() * 8.0f +
            ImGui::GetStyle().WindowPadding.y * 2.0f;
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(popupWidth, 0.0f),
            ImVec2(popupWidth, popupHeight)
        );
    }
}
