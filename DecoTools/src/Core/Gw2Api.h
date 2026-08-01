// Pewpew's Deco Tools - Guild Wars 2 API Interface
// Declares the direct API operations used to load account guild information and
// available Homestead or Guild Hall decoration counts.

#pragma once

#include <map>
#include <string>
#include <vector>

namespace Gw2Api
{
    struct Guild
    {
        std::string id;
        std::string name;
        std::string tag;
    };

    bool LoadGuilds(
        const std::string& apiKey,
        std::vector<Guild>& guilds,
        std::string& error
    );

    bool LoadCounts(
        const std::string& apiKey,
        int decorationType,
        const std::string& guildId,
        const std::vector<int>& ids,
        std::map<int, int>& counts,
        std::string& error
    );
}
