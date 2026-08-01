// Pewpew's Deco Tools - Move Tool Interface
// Declares the Move Tool tab, its in-game overlay renderer, imported-data
// cleanup, and Windows input handling for interactive movement controls.

#pragma once

#include <Windows.h>

namespace MoveToolTab
{
    void Render();
    void RenderOverlay();
    void ClearImportedData();
    UINT WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
}
