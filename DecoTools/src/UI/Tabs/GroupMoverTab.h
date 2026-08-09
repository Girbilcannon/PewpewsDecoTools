// Pewpew's Deco Tools - Group Mover Interface

#pragma once

#include <Windows.h>

namespace GroupMoverTab
{
    void Render();
    void RenderOverlay();
    void ClearImportedData();
    UINT WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
}
