// Pewpew's Deco Tools - Decoration Counter Interface
// Defines decoration requirements and declares the window used to compare an
// XML layout's required decorations against the player's available inventory.

#pragma once

#include <map>
#include <string>
#include <vector>

namespace DecorationCounterWindow
{
    struct Requirement
    {
        int id = -1;
        std::string name;
        int required = 0;
    };

    void SetRequirements(
        const std::string& context,
        int decorationType,
        const std::vector<Requirement>& requirements,
        const std::string& guildId = {}
    );

    void SetResolvedRequirements(
        const std::string& context,
        int decorationType,
        const std::vector<Requirement>& requirements,
        const std::map<int, int>& available
    );

    void Clear();
    void Render();
    void Shutdown();
}
