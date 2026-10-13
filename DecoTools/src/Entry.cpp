// Pewpew's Deco Tools - Addon Entry Point
// Defines the Nexus addon metadata and manages loading, unloading, rendering,
// input bindings, the quick-access icon, and Windows message forwarding.

#include <Windows.h>
#include <atomic>
#include <cstddef>

#include "nexus/Nexus.h"
#include "imgui/imgui.h"
#include "resource.h"

#include "Core/AppRuntime.h"
#include "Core/AppSettings.h"
#include "Core/DecorationDatabase.h"
#include "Core/GroupBackupDatabase.h"
#include "Core/MeasurementDatabase.h"
#include "UI/MainWindow.h"
#include "UI/DecorationCounterWindow.h"
#include "UI/DecoToolsStyle.h"
#include "UI/NewDecos.h"
#include "UI/QuickStartWindow.h"
#include "UI/Tabs/MapSwapTab.h"
#include "UI/Tabs/MergeExtractTab.h"
#include "UI/Tabs/MoveToolTab.h"
#include "UI/Tabs/PatternsTab.h"
#include "UI/Tabs/MeasureToolTab.h"

namespace
{
    constexpr const char* AddonName = "Pewpew's Deco Tools";
    constexpr const char* QuickAccessIdentifier = "DECOTOOLS_QUICKACCESS";
    constexpr const char* QuickAccessTextureIdentifier = "DECOTOOLS_QUICKACCESS_ICON";
    constexpr const char* ToggleWindowsInputBind = "KB_DECOTOOLS_TOGGLE_WINDOWS";

    struct NavigationTexture
    {
        const char* identifier;
        unsigned resourceId;
    };

    constexpr NavigationTexture NavigationTextures[] =
    {
        { "DECOTOOLS_NAV_MOVE", IDR_DECOTOOLS_NAV_MOVE },
        { "DECOTOOLS_NAV_PATTERNS", IDR_DECOTOOLS_NAV_PATTERNS },
        { "DECOTOOLS_NAV_MEASURE", IDR_DECOTOOLS_NAV_MEASURE },
        { "DECOTOOLS_NAV_MAP_SWAP", IDR_DECOTOOLS_NAV_MAP_SWAP },
        { "DECOTOOLS_NAV_GROUP_TOOLS", IDR_DECOTOOLS_NAV_GROUP_TOOLS },
        { "DECOTOOLS_NAV_GROUP_BACKUP", IDR_DECOTOOLS_NAV_GROUP_BACKUP },
        { "DECOTOOLS_NAV_DOCUMENTATION", IDR_DECOTOOLS_NAV_DOCUMENTATION },
        { "DECOTOOLS_NAV_SETTINGS", IDR_DECOTOOLS_NAV_SETTINGS }
    };

    AddonDefinition_t addonDefinition = {};
    AddonAPI_t* nexusApi = nullptr;
    HMODULE addonModule = nullptr;
    std::atomic_bool toggleWindowsRequested = false;

    void AddonLoad(AddonAPI_t* api);
    void AddonUnload();
    void AddonRender();
    void AddonOptions();
    void OnToggleWindows(const char* identifier, bool isRelease);
    void OnMumbleIdentityUpdated(void* eventArgs);
    UINT AddonWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        addonModule = module;
        DisableThreadLibraryCalls(module);
    }

    return TRUE;
}

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef()
{
    addonDefinition.Signature = 0xEF0D6957;
    addonDefinition.APIVersion = NEXUS_API_VERSION;
    addonDefinition.Name = AddonName;

    addonDefinition.Version.Major = 1;
    addonDefinition.Version.Minor = 5;
    addonDefinition.Version.Build = 0;
    addonDefinition.Version.Revision = 1;

    addonDefinition.Author = "Girbilcannon.8259";
    addonDefinition.Description =
        "Move, pattern, measure, group, merge, extract, and map swap large decoration builds for Homesteads and Guild Halls.";

    addonDefinition.Load = AddonLoad;
    addonDefinition.Unload = AddonUnload;
    addonDefinition.Flags = AF_None;
    addonDefinition.Provider = UP_GitHub;
    addonDefinition.UpdateLink =
        "https://github.com/Girbilcannon/PewpewsDecoTools";

    return &addonDefinition;
}

namespace
{
    void AddonLoad(AddonAPI_t* api)
    {
        nexusApi = api;

        ImGui::SetCurrentContext(
            static_cast<ImGuiContext*>(nexusApi->ImguiContext)
        );

        ImGui::SetAllocatorFunctions(
            reinterpret_cast<void* (*)(std::size_t, void*)>(
                nexusApi->ImguiMalloc
                ),
            reinterpret_cast<void (*)(void*, void*)>(
                nexusApi->ImguiFree
                )
        );

        AppRuntime::Initialize(nexusApi);
        AppSettings::Initialize();
        DecorationDatabase::Initialize(AppRuntime::GetAddonDirectory());
        DecoToolsStyle::Initialize(addonModule);
        GroupBackupDatabase::Initialize(AppRuntime::GetAddonDirectory());
        MeasurementDatabase::Initialize(AppRuntime::GetAddonDirectory());
        MeasureToolTab::Initialize();
        QuickStartWindow::Initialize();
        NewDecos::Initialize();

        nexusApi->Textures_GetOrCreateFromResource(
            QuickAccessTextureIdentifier,
            IDR_DECOTOOLS_QUICKACCESS_ICON,
            addonModule
        );
        for (const NavigationTexture& texture : NavigationTextures)
        {
            nexusApi->Textures_GetOrCreateFromResource(
                texture.identifier,
                texture.resourceId,
                addonModule
            );
        }
        nexusApi->InputBinds_RegisterWithString(
            ToggleWindowsInputBind,
            OnToggleWindows,
            ""
        );
        nexusApi->Events_Subscribe(
            EV_MUMBLE_IDENTITY_UPDATED,
            OnMumbleIdentityUpdated
        );
        nexusApi->QuickAccess_Add(
            QuickAccessIdentifier,
            QuickAccessTextureIdentifier,
            QuickAccessTextureIdentifier,
            ToggleWindowsInputBind,
            AddonName
        );

        nexusApi->GUI_Register(RT_Render, AddonRender);
        nexusApi->GUI_Register(RT_OptionsRender, AddonOptions);
        nexusApi->WndProc_Register(AddonWndProc);

        nexusApi->Log(
            LOGL_INFO,
            AddonName,
            "Pewpew's Deco Tools 1.5.0.1 loaded."
        );
    }

    void AddonUnload()
    {
        if (nexusApi == nullptr)
        {
            return;
        }

        nexusApi->QuickAccess_Remove(QuickAccessIdentifier);
        nexusApi->InputBinds_Deregister(ToggleWindowsInputBind);
        nexusApi->Events_Unsubscribe(
            EV_MUMBLE_IDENTITY_UPDATED,
            OnMumbleIdentityUpdated
        );

        MapSwapTab::Shutdown();
        MeasureToolTab::Shutdown();
        PatternsTab::ClearImportedData();
        DecorationCounterWindow::Shutdown();
        QuickStartWindow::Shutdown();
        DecoToolsStyle::Shutdown();
        AppSettings::Shutdown();
        GroupBackupDatabase::Shutdown();
        MeasurementDatabase::Shutdown();
        DecorationDatabase::Shutdown();

        nexusApi->WndProc_Deregister(AddonWndProc);
        nexusApi->GUI_Deregister(AddonRender);
        nexusApi->GUI_Deregister(AddonOptions);

        nexusApi->Log(
            LOGL_INFO,
            AddonName,
            "Pewpew's Deco Tools unloaded."
        );

        AppRuntime::Shutdown();
        nexusApi = nullptr;
    }

    void AddonRender()
    {
        if (toggleWindowsRequested.exchange(false, std::memory_order_acq_rel))
        {
            MainWindow::ToggleWindows();
        }

        AppSettings::Update();
        DecorationDatabase::Update();
        MeasureToolTab::SynchronizeSharedWorkspace();
        {
            const DecoToolsStyle::Scope interfaceStyle;
            MainWindow::Render();
            NewDecos::Render();
            MoveToolTab::RenderOverlay();
            PatternsTab::RenderOverlay();
            MergeExtractTab::RenderOverlay();
            MeasureToolTab::RenderOverlay();
        }
        // The Ruler HUD intentionally renders after the protected Deco Tools
        // scope so it can inherit Nexus font/DPI choices and keep its own HUD theme.
        MeasureToolTab::RenderHud();
    }

    void AddonOptions()
    {
        const DecoToolsStyle::Scope interfaceStyle;
        MainWindow::RenderOptions();
    }

    void OnToggleWindows(const char*, bool isRelease)
    {
        if (!isRelease)
        {
            toggleWindowsRequested.store(true, std::memory_order_release);
        }
    }

    void OnMumbleIdentityUpdated(void* eventArgs)
    {
        if (eventArgs != nullptr)
        {
            AppRuntime::SetMumbleIdentity(
                static_cast<Mumble::Identity*>(eventArgs)
            );
        }
    }

    UINT AddonWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (MeasureToolTab::WndProc(window, message, wParam, lParam) == 0)
        {
            return 0;
        }
        if (PatternsTab::WndProc(window, message, wParam, lParam) == 0)
        {
            return 0;
        }
        if (MergeExtractTab::WndProc(window, message, wParam, lParam) == 0)
        {
            return 0;
        }
        return MoveToolTab::WndProc(window, message, wParam, lParam);
    }
}
