// Pewpew's Deco Tools - Group Backup/Restore Interface

#pragma once

#include <string>

namespace GroupBackupRestoreTab
{
    void Render();
    void RenderAutoRestorePopup();
    void RenderManageBackupsWindow();
    void RenderRebuildXmlWindow();
    bool ImportSharedPath(const std::string& path);
    void ClearImportedData();
}
