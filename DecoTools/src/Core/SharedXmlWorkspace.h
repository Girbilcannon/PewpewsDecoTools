#pragma once

#include <cstdint>
#include <string>

namespace SharedXmlWorkspace
{
    void RenderImporter();
    void RefreshIfChanged();
    bool AdoptGeneratedFile(const std::string& path);
    bool NotifyCurrentFileChanged();

    bool HasImport();
    const std::string& Path();
    const std::string& FileName();
    int XmlType();
    bool HasGroups();
    std::uint64_t Revision();
}
