// Pewpew's Deco Tools - Shared Scene Manipulator Sizing

#pragma once

#include <algorithm>
#include <cmath>

namespace ManipulatorUtils
{
    constexpr float MoveAxisPixels = 52.0f;
    constexpr float RotationRingPixels = 38.0f;
    constexpr float CenterHalfSize = 7.0f;
    constexpr float CenterHitHalfSize = 12.0f;

    inline float WorldSizeForScreenPixels(
        float cameraDepth,
        float nearClip,
        float viewportHeight,
        float verticalFovRadians,
        float pixels
    )
    {
        if (viewportHeight <= 1.0f) return 0.0f;
        const float depth = (std::max)(nearClip, cameraDepth);
        const float focal =
            (viewportHeight * 0.5f) / std::tan(verticalFovRadians * 0.5f);
        return depth * pixels / focal;
    }
}
