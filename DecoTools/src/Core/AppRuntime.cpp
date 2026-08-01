// Pewpew's Deco Tools - Runtime Services Interface
// Declares shared access to the Nexus API, MumbleLink data, addon directory,
// and native Windows file and folder selection dialogs.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "AppRuntime.h"

#include <Windows.h>
#include <commdlg.h>
#include <shlobj.h>

#include <filesystem>

#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")

namespace
{
    AddonAPI_t* runtimeApi = nullptr;
    Mumble::Data* mumbleLink = nullptr;
    Mumble::Identity* mumbleIdentity = nullptr;
    std::string addonDirectory;

    std::string ParentFolder(const std::string& path)
    {
        if (path.empty())
        {
            return {};
        }

        std::filesystem::path value(path);
        if (std::filesystem::is_directory(value))
        {
            return value.string();
        }

        return value.parent_path().string();
    }

    int CALLBACK BrowseCallback(HWND window, UINT message, LPARAM, LPARAM data)
    {
        if (message == BFFM_INITIALIZED && data != 0)
        {
            SendMessageA(window, BFFM_SETSELECTIONA, TRUE, data);
        }

        return 0;
    }

    bool ShowXmlDialog(
        bool save,
        const std::string& initialFolder,
        const std::string& suggestedName,
        std::string& selectedFile
    )
    {
        char filePath[1024] = "";
        if (!suggestedName.empty())
        {
            strncpy_s(filePath, suggestedName.c_str(), _TRUNCATE);
        }

        std::string folder = ParentFolder(initialFolder);

        OPENFILENAMEA dialog = {};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = GetForegroundWindow();
        dialog.lpstrFilter = "Guild Wars 2 Decoration XML (*.xml)\0*.xml\0All Files (*.*)\0*.*\0";
        dialog.lpstrFile = filePath;
        dialog.nMaxFile = static_cast<DWORD>(sizeof(filePath));
        dialog.lpstrInitialDir = folder.empty() ? nullptr : folder.c_str();
        dialog.lpstrDefExt = "xml";
        dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

        BOOL accepted = FALSE;
        if (save)
        {
            dialog.Flags |= OFN_OVERWRITEPROMPT;
            accepted = GetSaveFileNameA(&dialog);
        }
        else
        {
            dialog.Flags |= OFN_FILEMUSTEXIST;
            accepted = GetOpenFileNameA(&dialog);
        }

        if (!accepted)
        {
            return false;
        }

        selectedFile = filePath;
        return true;
    }
}

void AppRuntime::Initialize(AddonAPI_t* api)
{
    runtimeApi = api;
    mumbleLink = runtimeApi == nullptr
        ? nullptr
        : static_cast<Mumble::Data*>(runtimeApi->DataLink_Get(DL_MUMBLE_LINK));
    mumbleIdentity = runtimeApi == nullptr
        ? nullptr
        : static_cast<Mumble::Identity*>(
            runtimeApi->DataLink_Get(DL_MUMBLE_LINK_IDENTITY)
        );

    addonDirectory.clear();
    if (runtimeApi != nullptr && runtimeApi->Paths_GetAddonDirectory != nullptr)
    {
        const char* path = runtimeApi->Paths_GetAddonDirectory("DecoTools");
        if (path != nullptr)
        {
            addonDirectory = path;
        }
    }

    if (addonDirectory.empty())
    {
        addonDirectory = ".";
    }

    std::error_code error;
    std::filesystem::create_directories(addonDirectory, error);
}

void AppRuntime::Shutdown()
{
    mumbleIdentity = nullptr;
    mumbleLink = nullptr;
    runtimeApi = nullptr;
    addonDirectory.clear();
}

AddonAPI_t* AppRuntime::GetApi()
{
    return runtimeApi;
}

Mumble::Data* AppRuntime::GetMumble()
{
    return mumbleLink;
}

Mumble::Identity* AppRuntime::GetMumbleIdentity()
{
    if (mumbleIdentity == nullptr && runtimeApi != nullptr)
    {
        mumbleIdentity = static_cast<Mumble::Identity*>(
            runtimeApi->DataLink_Get(DL_MUMBLE_LINK_IDENTITY)
        );
    }
    return mumbleIdentity;
}

void AppRuntime::SetMumbleIdentity(Mumble::Identity* identity)
{
    mumbleIdentity = identity;
}

const std::string& AppRuntime::GetAddonDirectory()
{
    return addonDirectory;
}

bool AppRuntime::BrowseForFolder(
    const char* title,
    const std::string& initialFolder,
    std::string& selectedFolder
)
{
    BROWSEINFOA info = {};
    info.hwndOwner = GetForegroundWindow();
    info.lpszTitle = title;
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    info.lpfn = BrowseCallback;
    info.lParam = reinterpret_cast<LPARAM>(initialFolder.c_str());

    PIDLIST_ABSOLUTE item = SHBrowseForFolderA(&info);
    if (item == nullptr)
    {
        return false;
    }

    char folderPath[MAX_PATH] = "";
    const bool resolved = SHGetPathFromIDListA(item, folderPath) == TRUE;
    CoTaskMemFree(item);

    if (!resolved)
    {
        return false;
    }

    selectedFolder = folderPath;
    return true;
}

bool AppRuntime::BrowseForXml(const std::string& initialFolder, std::string& selectedFile)
{
    return ShowXmlDialog(false, initialFolder, {}, selectedFile);
}

bool AppRuntime::BrowseForXmlSave(
    const std::string& initialFolder,
    const std::string& suggestedName,
    std::string& selectedFile
)
{
    return ShowXmlDialog(true, initialFolder, suggestedName, selectedFile);
}
