#pragma once

#include <Windows.h>

namespace MoveToolTab
{
    void Render();
    void RenderOverlay();
    void ClearImportedData();
    UINT WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
}
