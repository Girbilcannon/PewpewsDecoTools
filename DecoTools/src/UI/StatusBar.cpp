#include "StatusBar.h"

#include "../Core/AppSettings.h"
#include "../imgui/imgui.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>

namespace
{
    std::string currentMessage = "Ready.";
    std::unordered_map<const void*, std::string> publishedBySource;

    bool FitsTwoLines(const std::string& text, float wrapWidth)
    {
        const float height = ImGui::CalcTextSize(
            text.c_str(), nullptr, false, wrapWidth).y;
        return height <= ImGui::GetTextLineHeight() * 2.1f;
    }

    std::string FitToTwoLines(const std::string& text, float wrapWidth)
    {
        if (text.empty() || wrapWidth <= 1.0f || FitsTwoLines(text, wrapWidth))
            return text;

        size_t length = text.size();
        while (length > 0)
        {
            --length;
            while (length > 0 &&
                (static_cast<unsigned char>(text[length]) & 0xC0u) == 0x80u)
            {
                --length;
            }

            std::string candidate = text.substr(0, length);
            while (!candidate.empty() &&
                std::isspace(static_cast<unsigned char>(candidate.back())) != 0)
            {
                candidate.pop_back();
            }
            candidate += "...";
            if (FitsTwoLines(candidate, wrapWidth)) return candidate;
        }
        return "...";
    }
}

void StatusBar::Publish(const std::string& message)
{
    if (!message.empty()) currentMessage = message;
}

void StatusBar::PublishIfChanged(const void* source, const std::string& message)
{
    if (source == nullptr) return;
    std::string& previous = publishedBySource[source];
    if (previous == message) return;
    previous = message;
    Publish(message);
}

void StatusBar::Render()
{
    constexpr float FontScale = 1.0f;
    AppSettings::Data& settings = AppSettings::Get();
    const float availableWidth = (std::max)(1.0f, ImGui::GetContentRegionAvail().x);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float desiredControlsWidth = (std::min)(
        260.0f,
        (std::max)(180.0f, availableWidth * 0.45f));
    const float controlsWidth = (std::min)(
        desiredControlsWidth,
        (std::max)(1.0f, availableWidth - 80.0f - spacing));
    const float wrapWidth = (std::max)(
        1.0f,
        availableWidth - controlsWidth - spacing);

    ImGui::SetWindowFontScale(FontScale);
    const float twoLineHeight = ImGui::GetTextLineHeight() * 2.8f;

    ImGui::BeginChild(
        "##StatusMessage",
        ImVec2(wrapWidth, twoLineHeight + 2.0f),
        false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetWindowFontScale(FontScale);
    const std::string display = FitToTwoLines(currentMessage, wrapWidth);
    const float textHeight = ImGui::CalcTextSize(
        display.c_str(),
        nullptr,
        false,
        wrapWidth
    ).y;

    const float extraVerticalSpace =
        ImGui::GetContentRegionAvail().y - textHeight;

    if (extraVerticalSpace > 0.0f)
    {
        ImGui::SetCursorPosY(
            ImGui::GetCursorPosY() + extraVerticalSpace
        );
    }
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapWidth);
    ImGui::TextUnformatted(display.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild(
        "##StatusPreviewControls",
        ImVec2(0.0f, twoLineHeight + 2.0f),
        false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetWindowFontScale(1.0f);
    if (ImGui::Checkbox("Deco Point Visibility", &settings.showDecorationPoints))
        AppSettings::MarkDirty();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show or hide decoration anchor points");

    ImGui::TextUnformatted("Point Size");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat(
        "##SharedPointSize",
        &settings.pointSize,
        1.0f,
        12.0f,
        "%.0f px"))
    {
        AppSettings::MarkDirty();
    }
    ImGui::EndChild();

    ImGui::SetWindowFontScale(1.0f);
}
