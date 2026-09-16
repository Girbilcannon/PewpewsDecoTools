// Pewpew's Deco Tools - Map Swap Interface
// Declares the Map Swap tab and its cleanup and shutdown operations.

#pragma once

#include <string>

namespace MapSwapTab
{
    void Render();
    bool ImportSharedPath(const std::string& path);
    void SetActive(bool active);
    void ClearImportedData();
    void Shutdown();
}
