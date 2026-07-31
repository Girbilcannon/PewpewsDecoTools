#pragma once

#include <filesystem>
#include <string>

namespace Utf8Paths
{
    inline std::string ToUtf8(const std::filesystem::path& path)
    {
        const std::u8string value = path.u8string();
        return std::string(
            reinterpret_cast<const char*>(value.data()),
            value.size()
        );
    }

    inline std::filesystem::path FromUtf8(const std::string& value)
    {
        const std::u8string utf8(
            reinterpret_cast<const char8_t*>(value.data()),
            value.size()
        );
        return std::filesystem::path(utf8);
    }
}
