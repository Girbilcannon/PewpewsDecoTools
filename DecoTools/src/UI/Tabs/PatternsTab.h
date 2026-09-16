// Pewpew's Deco Tools - Patterns Tab Interface
// Declares the procedural pattern editor, its in-world preview, cleanup, and
// Windows mouse-message handling used by the scene manipulators.

#pragma once

#include <Windows.h>
#include <string>

namespace PatternsTab
{
    void Render();
    void RenderOverlay();
    bool ImportSharedPath(const std::string& path, bool hasGroups);
    void SetActive(bool active);
    void ClearImportedData();
    UINT WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
}
