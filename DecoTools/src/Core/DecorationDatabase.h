#pragma once

#include <string>

namespace DecorationDatabase
{
    struct Entry
    {
        std::string name;
        int homesteadId;
        int guildUpgradeId;
    };

    void Initialize(const std::string& addonDirectory);
    void Shutdown();

    const Entry* FindByCleanName(const std::string& cleanName);
    const char* FindNameById(int id, int type);
    int Count();
    const std::string& GetPath();
}
