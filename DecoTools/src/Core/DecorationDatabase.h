// Pewpew's Deco Tools - Decoration Database Interface
// Defines decoration database entries and declares lookup functions for matching
// decoration names with Homestead and Guild Hall API identifiers.

#pragma once

#include <string>

namespace DecorationDatabase
{
    struct Entry
    {
        std::string name;
        int homesteadId = -1;
        int guildUpgradeId = -1;
        int maxCount = -1;
    };

    void Initialize(const std::string& addonDirectory);
    void Update();
    void Shutdown();

    const Entry* FindByCleanName(const std::string& cleanName);
    const char* FindNameById(int id, int type);
    int FindMaxCountById(int id, int type);
    int Count();
    const std::string& GetPath();
}
