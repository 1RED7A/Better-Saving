#pragma once

#include <filesystem>
#include <string>
#include <system_error>

// -----------------------------------------------------------------------
// Backup slot names. Two slots: one rolling (written after every save),
// one safe-exit (written only when the user quits cleanly or alt-tabs out).
// -----------------------------------------------------------------------
inline constexpr const char* BACKUP_ROLLING   = "Save_Backup.bak";
inline constexpr const char* BACKUP_SAFE_EXIT = "Last_Safe_Save_Backup.bak";

// Copies src -> dst safely using a temp file + rename to avoid leaving a
// half-written file if the process dies mid-copy. Retries up to 3 times
// on transient failures (e.g. antivirus locks on Windows).
bool atomicCopy(
    const std::filesystem::path& src,
    const std::filesystem::path& dst,
    std::error_code& ec
);

// Creates a backup of CCGameManager.dat into the mod's save directory.
// slotName should be one of the BACKUP_* constants above.
void createBackup(const char* slotName);

// Overwrites CCGameManager.dat with the named backup, then restarts GD.
// Returns false and shows an error notification if the backup doesn't exist.
bool restoreFromBackup(const std::string& backupName);
