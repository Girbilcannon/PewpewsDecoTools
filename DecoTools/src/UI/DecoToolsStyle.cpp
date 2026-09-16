// Pewpew's Deco Tools - Scoped Interface Style
// Applies a predictable local ImGui theme and font for the duration of this
// addon's render callbacks, then restores the shared Nexus ImGui state.

#include "DecoToolsStyle.h"

#include <Windows.h>

#include <atomic>
#include <cmath>

#include "../Core/AppRuntime.h"
#include "../Core/AppSettings.h"
#include "../imgui/imgui.h"
#include "../resource.h"

namespace
{
    constexpr const char* FontIdentifier = "DECOTOOLS_INTERFACE_FONT";
    constexpr float BaseFontSize = 15.0f;
    constexpr int StyleVariableCount = 24;

    std::atomic<ImFont*> interfaceFont = nullptr;
    AddonAPI_t* fontApi = nullptr;
    float requestedFontSize = 0.0f;

    void ReceiveFont(const char*, void* font)
    {
        interfaceFont.store(static_cast<ImFont*>(font), std::memory_order_release);
    }

    float InterfaceScale()
    {
        AddonAPI_t* api = AppRuntime::GetApi();
        if (api == nullptr || api->DataLink_Get == nullptr)
        {
            return 1.0f;
        }

        const auto* nexusLink = static_cast<const NexusLinkData_t*>(
            api->DataLink_Get(DL_NEXUS_LINK));
        if (nexusLink == nullptr || nexusLink->Scaling <= 0.0f)
        {
            return 1.0f;
        }

        return nexusLink->Scaling;
    }

    void UpdateFontSize(float scale)
    {
        if (fontApi == nullptr || fontApi->Fonts_Resize == nullptr)
        {
            return;
        }

        const float desiredSize = BaseFontSize * scale;
        if (std::fabs(desiredSize - requestedFontSize) < 0.05f)
        {
            return;
        }

        requestedFontSize = desiredSize;
        fontApi->Fonts_Resize(FontIdentifier, desiredSize);
    }

    ImGuiStyle BuildProtectedStyle(float scale)
    {
        ImGuiStyle style;
        ImGui::StyleColorsDark(&style);

        style.Alpha = 1.0f;
        style.WindowPadding = ImVec2(8.0f * scale, 8.0f * scale);
        style.WindowRounding = 5.0f * scale;
        style.WindowBorderSize = 1.0f;
        style.WindowMinSize = ImVec2(32.0f * scale, 32.0f * scale);
        style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
        style.ChildRounding = 4.0f * scale;
        style.ChildBorderSize = 1.0f;
        style.PopupRounding = 4.0f * scale;
        style.PopupBorderSize = 1.0f;
        style.FramePadding = ImVec2(4.0f * scale, 3.0f * scale);
        style.FrameRounding = 4.0f * scale;
        style.FrameBorderSize = 0.0f;
        style.ItemSpacing = ImVec2(8.0f * scale, 4.0f * scale);
        style.ItemInnerSpacing = ImVec2(4.0f * scale, 4.0f * scale);
        style.IndentSpacing = 21.0f * scale;
        style.CellPadding = ImVec2(4.0f * scale, 2.0f * scale);
        style.ScrollbarSize = 14.0f * scale;
        style.ScrollbarRounding = 9.0f * scale;
        style.GrabMinSize = 10.0f * scale;
        style.GrabRounding = 4.0f * scale;
        style.TabRounding = 4.0f * scale;
        style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
        style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

        // Keep the familiar dark Nexus presentation while preventing global
        // color and transparency overrides from making this addon's UI unreadable.
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.07f, 0.09f, 0.96f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.04f, 0.05f, 0.07f, 0.35f);
        style.Colors[ImGuiCol_PopupBg] = ImVec4(0.06f, 0.07f, 0.09f, 0.98f);
        style.Colors[ImGuiCol_Border] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.22f, 0.22f, 0.26f, 0.92f);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.28f, 0.29f, 0.34f, 1.00f);
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.31f, 0.32f, 0.38f, 1.00f);
        style.Colors[ImGuiCol_Button] = ImVec4(0.24f, 0.24f, 0.29f, 0.92f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.55f, 0.93f, 1.00f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.18f, 0.47f, 0.85f, 1.00f);
        style.Colors[ImGuiCol_Header] = ImVec4(0.30f, 0.31f, 0.37f, 0.90f);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.24f, 0.55f, 0.93f, 0.80f);
        style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.18f, 0.47f, 0.85f, 1.00f);
        style.Colors[ImGuiCol_CheckMark] = ImVec4(0.22f, 0.61f, 1.00f, 1.00f);
        style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.56f, 0.57f, 0.64f, 1.00f);
        style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.22f, 0.61f, 1.00f, 1.00f);
        style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.22f, 0.61f, 1.00f, 0.25f);
        style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.22f, 0.61f, 1.00f, 0.67f);
        style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.22f, 0.61f, 1.00f, 0.95f);

        return style;
    }

    void PushStyleVariables(const ImGuiStyle& style)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style.Alpha);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, style.WindowPadding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, style.WindowRounding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, style.WindowBorderSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, style.WindowMinSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowTitleAlign, style.WindowTitleAlign);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, style.ChildRounding);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, style.ChildBorderSize);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, style.PopupRounding);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, style.PopupBorderSize);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, style.FramePadding);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, style.FrameRounding);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, style.FrameBorderSize);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, style.ItemSpacing);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, style.ItemInnerSpacing);
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, style.IndentSpacing);
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, style.CellPadding);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, style.ScrollbarSize);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, style.ScrollbarRounding);
        ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, style.GrabMinSize);
        ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, style.GrabRounding);
        ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, style.TabRounding);
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, style.ButtonTextAlign);
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, style.SelectableTextAlign);
    }
}

void DecoToolsStyle::Initialize(void* addonModule)
{
    fontApi = AppRuntime::GetApi();
    if (fontApi == nullptr || fontApi->Fonts_AddFromResource == nullptr)
    {
        return;
    }

    requestedFontSize = BaseFontSize * InterfaceScale();
    fontApi->Fonts_AddFromResource(
        FontIdentifier,
        requestedFontSize,
        IDR_DECOTOOLS_INTERFACE_FONT,
        static_cast<HMODULE>(addonModule),
        ReceiveFont,
        nullptr);
}

void DecoToolsStyle::Shutdown()
{
    if (fontApi != nullptr && fontApi->Fonts_Release != nullptr)
    {
        fontApi->Fonts_Release(FontIdentifier, ReceiveFont);
    }

    interfaceFont.store(nullptr, std::memory_order_release);
    requestedFontSize = 0.0f;
    fontApi = nullptr;
}

DecoToolsStyle::Scope::Scope()
{
    if (!AppSettings::Get().useDecoToolsInterfaceStyle)
    {
        return;
    }

    const float scale = InterfaceScale();
    UpdateFontSize(scale);

    if (ImFont* font = interfaceFont.load(std::memory_order_acquire))
    {
        ImGui::PushFont(font);
        fontPushed = true;
    }

    const ImGuiStyle style = BuildProtectedStyle(scale);
    PushStyleVariables(style);
    for (int color = 0; color < ImGuiCol_COUNT; ++color)
    {
        ImGui::PushStyleColor(color, style.Colors[color]);
    }

    active = true;
}

DecoToolsStyle::Scope::~Scope()
{
    if (!active)
    {
        return;
    }

    ImGui::PopStyleColor(ImGuiCol_COUNT);
    ImGui::PopStyleVar(StyleVariableCount);
    if (fontPushed)
    {
        ImGui::PopFont();
    }
}
