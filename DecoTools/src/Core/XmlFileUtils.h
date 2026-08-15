// Pewpew's Deco Tools - XML File Utilities
// Discovers XML files in configured folders and optional subfolders, formats
// their display paths, and generates correctly indexed operation filenames.

#pragma once

#include "Utf8Paths.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace XmlFileUtils
{
    struct Entry
    {
        std::string name;
        std::string path;
    };

    inline bool HasXmlExtension(const std::filesystem::path& path)
    {
        std::string extension = path.extension().string();
        std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            }
        );
        return extension == ".xml";
    }

    inline std::string DisplayName(
        const std::filesystem::path& root,
        const std::filesystem::path& file
    )
    {
        std::error_code error;
        const std::filesystem::path relative =
            std::filesystem::relative(file, root, error);
        if (error || relative.empty())
        {
            return Utf8Paths::ToUtf8(file.filename());
        }

        std::string name = Utf8Paths::ToUtf8(relative);
        std::replace(name.begin(), name.end(), '\\', '/');
        if (relative.has_parent_path())
        {
            name.insert(name.begin(), '/');
        }
        return name;
    }

    inline bool List(
        const std::string& folder,
        bool includeSubFolders,
        std::vector<Entry>& output
    )
    {
        output.clear();
        std::error_code error;
        const std::filesystem::path root = Utf8Paths::FromUtf8(folder);
        if (!std::filesystem::is_directory(root, error) || error)
        {
            return false;
        }

        const auto addEntry = [&](const std::filesystem::directory_entry& entry)
        {
            std::error_code entryError;
            if (entry.is_regular_file(entryError) && !entryError &&
                HasXmlExtension(entry.path()))
            {
                output.push_back(
                    {
                        DisplayName(root, entry.path()),
                        Utf8Paths::ToUtf8(entry.path())
                    }
                );
            }
        };

        const auto options =
            std::filesystem::directory_options::skip_permission_denied;
        if (includeSubFolders)
        {
            for (std::filesystem::recursive_directory_iterator iterator(
                    root, options, error), end;
                !error && iterator != end;
                iterator.increment(error))
            {
                addEntry(*iterator);
            }
        }
        else
        {
            for (std::filesystem::directory_iterator iterator(
                    root, options, error), end;
                !error && iterator != end;
                iterator.increment(error))
            {
                addEntry(*iterator);
            }
        }

        if (error)
        {
            output.clear();
            return false;
        }

        std::sort(
            output.begin(),
            output.end(),
            [](const Entry& left, const Entry& right)
            {
                const bool leftNested = !left.name.empty() && left.name.front() == '/';
                const bool rightNested = !right.name.empty() && right.name.front() == '/';
                if (leftNested != rightNested)
                {
                    return !leftNested;
                }

                std::string leftName = left.name;
                std::string rightName = right.name;
                std::transform(leftName.begin(), leftName.end(), leftName.begin(),
                    [](unsigned char value)
                    {
                        return static_cast<char>(std::tolower(value));
                    });
                std::transform(rightName.begin(), rightName.end(), rightName.begin(),
                    [](unsigned char value)
                    {
                        return static_cast<char>(std::tolower(value));
                    });
                return leftName < rightName;
            }
        );
        return true;
    }

    inline std::filesystem::path IndexedOperationPath(
        const std::filesystem::path& folder,
        const std::string& inputStem,
        const std::string& operationSuffix
    )
    {
        std::string upperStem = inputStem;
        std::string upperSuffix = operationSuffix;
        std::transform(upperStem.begin(), upperStem.end(), upperStem.begin(),
            [](unsigned char value)
            {
                return static_cast<char>(std::toupper(value));
            });
        std::transform(upperSuffix.begin(), upperSuffix.end(), upperSuffix.begin(),
            [](unsigned char value)
            {
                return static_cast<char>(std::toupper(value));
            });

        std::string base = inputStem;
        unsigned index = 1;
        size_t search = 0;
        while ((search = upperStem.find(upperSuffix, search)) != std::string::npos)
        {
            const size_t digitsStart = search + upperSuffix.size();
            size_t digitsEnd = digitsStart;
            while (digitsEnd < upperStem.size() &&
                std::isdigit(static_cast<unsigned char>(upperStem[digitsEnd])) != 0)
            {
                ++digitsEnd;
            }

            const bool tokenEnd =
                digitsEnd == upperStem.size() || upperStem[digitsEnd] == '_';
            if (digitsEnd > digitsStart && tokenEnd)
            {
                try
                {
                    index = static_cast<unsigned>(
                        std::stoul(upperStem.substr(digitsStart, digitsEnd - digitsStart))
                    ) + 1;
                }
                catch (...)
                {
                    index = 1;
                }
                base.erase(search);
                break;
            }
            search = digitsStart;
        }

        std::error_code error;
        while (true)
        {
            const std::filesystem::path candidate = folder /
                Utf8Paths::FromUtf8(
                    base + operationSuffix + std::to_string(index) + ".xml"
                );
            const bool exists = std::filesystem::exists(candidate, error);
            if (error)
            {
                return {};
            }
            if (!exists)
            {
                return candidate;
            }
            ++index;
        }
    }
}
