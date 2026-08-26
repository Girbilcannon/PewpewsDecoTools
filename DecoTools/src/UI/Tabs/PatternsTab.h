// Pewpew's Deco Tools - Patterns Tab Interface
// Declares the procedural pattern editor, its in-world preview, cleanup, and
// Windows mouse-message handling used by the scene manipulators.

#pragma once

#include <Windows.h>

namespace PatternsTab
{
    void Render();
    void RenderOverlay();
    void ClearImportedData();
    UINT WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
}
