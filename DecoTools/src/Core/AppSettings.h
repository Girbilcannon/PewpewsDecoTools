#pragma once

#include <array>

namespace AppSettings
{
    struct Data
    {
        std::array<char, 128> apiKey = {};
        std::array<char, 512> homesteadFolder = {};
        std::array<char, 512> guildHallFolder = {};

        bool checkForDatabaseUpdates = true;
        bool rememberWindowState = true;
        bool windowVisible = true;
        bool showDecorationCounter = true;

        bool showBoundingBox = false;
        bool showSolidFaces = false;
        bool showDecorationPoints = true;
        float boxColor[4] = { 0.18f, 0.65f, 1.0f, 1.0f };
        float faceColor[4] = { 0.18f, 0.65f, 1.0f, 0.18f };
        float pointColor[4] = { 1.0f, 0.72f, 0.16f, 1.0f };
        float pointSize = 5.0f;
    };

    void Initialize();
    void Shutdown();
    void Update();
    void MarkDirty();
    void SaveNow();

    Data& Get();
}
