// Pewpew's Deco Tools - Move Tool XML Groups Interface

#pragma once

#include <Windows.h>
#include <string>

namespace MoveToolGroups
{
    void RenderWorkspace();
    bool ImportPath(const std::string& path, bool prepareGroupRestore = true);
    void RefreshCounter();
    void RenderOverlay();
    void ClearImportedData();
    UINT WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
}
