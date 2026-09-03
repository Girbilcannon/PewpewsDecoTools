// Pewpew's Deco Tools - Group Backup Database Interface

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace GroupBackupDatabase
{
    enum class RestorePointType
    {
        Auto,
        Manual,
        Safety
    };

    enum class ImportAction
    {
        None,
        ExistingGroups,
        Restored,
        NeedsUserChoice,
        Error
    };

    struct RestorePointSummary
    {
        std::string id;
        RestorePointType type = RestorePointType::Auto;
        std::string customName;
        std::string xmlName;
        std::string lineage;
        std::string createdUtc;
        int xmlType = -1;
        size_t groupCount = 0;
        size_t propCount = 0;
        bool completeXml = false;
    };

    struct RebuildResult
    {
        size_t restoredProps = 0;
        size_t skippedProps = 0;
        size_t restoredGroups = 0;
        bool completeXml = false;
    };

    struct RestoreStats
    {
        size_t matched = 0;
        size_t missingOrModified = 0;
        size_t leftUngrouped = 0;
        size_t restoredGroups = 0;
    };

    struct ImportResult
    {
        ImportAction action = ImportAction::None;
        std::string message;
        RestoreStats stats;
    };

    struct PendingRestore
    {
        bool active = false;
        std::string targetPath;
        std::string targetName;
        int xmlType = -1;
    };

    void Initialize(const std::string& addonDirectory);
    void Shutdown();

    bool RecordFile(
        const std::string& path,
        int xmlType,
        RestorePointType type,
        const std::string& customName,
        std::string& status
    );

    ImportResult PrepareImport(
        const std::string& path,
        int expectedXmlType,
        bool automaticEnabled
    );

    std::vector<RestorePointSummary> GetRestorePoints(int xmlType = -1);
    bool PreviewRestore(
        const std::string& restorePointId,
        const std::string& targetPath,
        RestoreStats& stats,
        std::string& status
    );
    bool Restore(
        const std::string& restorePointId,
        const std::string& targetPath,
        RestoreStats& stats,
        std::string& status
    );
    bool RestoreFromXml(
        const std::string& sourcePath,
        const std::string& targetPath,
        int expectedXmlType,
        RestoreStats& stats,
        std::string& status
    );

    bool RebuildXml(
        const std::string& restorePointId,
        const std::string& outputPath,
        RebuildResult& result,
        std::string& status
    );
    bool PreviewRestoreFromXml(
        const std::string& sourcePath,
        const std::string& targetPath,
        int expectedXmlType,
        RestoreStats& stats,
        std::string& status
    );

    bool RenameRestorePoint(
        const std::string& restorePointId,
        const std::string& newName,
        std::string& status
    );
    bool DeleteRestorePoints(
        const std::vector<std::string>& restorePointIds,
        size_t& deletedCount,
        std::string& status
    );

    bool InspectFile(
        const std::string& path,
        int& xmlType,
        size_t& groupCount,
        size_t& propCount
    );

    PendingRestore GetPendingRestore();
    void IgnorePendingRestore();
    void ClearPendingRestore();
    std::string NormalizeLineage(const std::string& fileName);
}
