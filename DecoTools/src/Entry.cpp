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
#include "UI/MainWindow.h"
#include "UI/DecorationCounterWindow.h"
#include "UI/Tabs/MapSwapTab.h"
#include "UI/Tabs/MoveToolTab.h"

namespace
{
    constexpr const char* AddonName = "Pewpew's Deco Tools";
    constexpr const char* QuickAccessIdentifier = "DECOTOOLS_QUICKACCESS";
    constexpr const char* QuickAccessTextureIdentifier = "DECOTOOLS_QUICKACCESS_ICON";
    constexpr const char* ToggleWindowsInputBind = "KB_DECOTOOLS_TOGGLE_WINDOWS";

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
    addonDefinition.Version.Minor = 0;
    addonDefinition.Version.Build = 0;
    addonDefinition.Version.Revision = 2;

    addonDefinition.Author = "Girbilcannon.8259";
    addonDefinition.Description =
        "Move, Merge, Replicate, and  Map Swap large decoration builds for Homesteads and Guild Halls. Migrated directly from GW2DecoTools.com.";

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
        DecorationDatabase::Initialize(AppRuntime::GetAddonDirectory());
        AppSettings::Initialize();

        nexusApi->Textures_GetOrCreateFromResource(
            QuickAccessTextureIdentifier,
            IDR_DECOTOOLS_QUICKACCESS_ICON,
            addonModule
        );
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
            "Pewpew's Deco Tools 1.0.0.2 loaded."
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
        DecorationCounterWindow::Shutdown();
        AppSettings::Shutdown();
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
        MainWindow::Render();
        MoveToolTab::RenderOverlay();
    }

    void AddonOptions()
    {
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
        return MoveToolTab::WndProc(window, message, wParam, lParam);
    }
}
