// Pewpew's Deco Tools - Merge, Group, and Extract Interface
// Declares XML layout merging, interactive decoration grouping, group extraction,
// overlay rendering, imported-data cleanup, and Windows input handling.

#pragma once

#include <Windows.h>

namespace MergeExtractTab
{
    void Render();
    void RenderOverlay();
    void ClearImportedData();
    UINT WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
}
