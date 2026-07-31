#pragma once

#include <string>

#include "../mumble/Mumble.h"
#include "../nexus/Nexus.h"

namespace AppRuntime
{
    void Initialize(AddonAPI_t* api);
    void Shutdown();

    AddonAPI_t* GetApi();
    Mumble::Data* GetMumble();
    Mumble::Identity* GetMumbleIdentity();
    void SetMumbleIdentity(Mumble::Identity* identity);
    const std::string& GetAddonDirectory();

    bool BrowseForFolder(const char* title, const std::string& initialFolder, std::string& selectedFolder);
    bool BrowseForXml(const std::string& initialFolder, std::string& selectedFile);
    bool BrowseForXmlSave(
        const std::string& initialFolder,
        const std::string& suggestedName,
        std::string& selectedFile
    );
}
