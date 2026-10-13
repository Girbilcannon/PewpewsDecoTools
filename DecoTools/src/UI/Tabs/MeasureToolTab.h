#pragma once

#include <Windows.h>
#include <vector>

namespace MeasureToolTab
{
    struct SnapPoint
    {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
    };

    void Initialize();
    void Shutdown();
    void SynchronizeSharedWorkspace();
    void SetActive(bool active);
    void Render();
    void RenderOverlay();
    void RenderHud();
    void GetVisibleSnapPoints(std::vector<SnapPoint>& output);
    UINT WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
}
