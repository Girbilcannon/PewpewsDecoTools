// Pewpew's Deco Tools - Group Mover Interface

#pragma once

#include <Windows.h>
#include <string>

namespace GroupMoverTab
{
    void Render();
    void RenderWorkspace();
    bool ImportPath(const std::string& path);
    void RenderOverlay();
    void ClearImportedData();
    UINT WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
}
