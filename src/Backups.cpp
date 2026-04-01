#include "Backups.hpp"
#include "AutoSaveManager.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/Dirs.hpp>
#include <Geode/ui/Notification.hpp>

#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <string>
#include <system_error>

using namespace geode::prelude;

// -----------------------------------------------------------------------
// Internal helpers
// -----------------------------------------------------------------------

// Blocks until the file can be opened for reading, or until we give up.
// Needed on Windows where GD's own save routine briefly holds an exclusive
// lock right after writing - without this we'd copy a partially flushed file.
static bool waitForFileReady(const std::filesystem::path& p, int maxRetries = 5) {
    for (int i = 0; i < maxRetries; ++i) {
        std::ifstream f(p, std::ios::binary);
        if (f.is_open()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return false;
}

// Runs a callable up to `attempts` times, sleeping between failures.
// Returns true on the first success.
template<typename Fn>
static bool retryOp(Fn&& fn, int attempts, std::chrono::milliseconds delay) {
    for (int i = 0; i < attempts; ++i) {
        if (fn()) return true;
        if (i + 1 < attempts)
            std::this_thread::sleep_for(delay);
    }
    return false;
}

// -----------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------

bool atomicCopy(
    const std::filesystem::path& src,
    const std::filesystem::path& dst,
    std::error_code& ec
) {
    // Write to a temp file first so that if we crash mid-copy the destination
    // is never left in a half-written state.
    auto tmp = dst.string() + ".tmp."
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());

    if (!waitForFileReady(src)) {
        ec = std::make_error_code(std::errc::resource_unavailable_try_again);
        log::warn("[Better Saving] Source file locked after retries: {}", src.string());
        return false;
    }

    // Copy to temp - retry a few times in case of a transient lock.
    bool copied = retryOp([&] {
        std::filesystem::copy_file(src, tmp,
            std::filesystem::copy_options::overwrite_existing, ec);
        return !ec;
    }, 3, std::chrono::milliseconds(100));

    if (!copied) return false;

    // Shuffle old destination aside so the rename below is as close to
    // atomic as the OS allows.
    auto old = dst.string() + ".old";
    if (std::filesystem::exists(dst, ec)) {
        std::filesystem::rename(dst, old, ec);
        if (ec) std::filesystem::remove(dst, ec);
    }

    ec = {};
    bool renamed = retryOp([&] {
        std::filesystem::rename(tmp, dst, ec);
        return !ec;
    }, 3, std::chrono::milliseconds(100));

    // Best-effort cleanup of the .old file - not fatal if it fails.
    std::error_code cleanEc;
    std::filesystem::remove(old, cleanEc);

    return renamed;
}

void createBackup(const char* slotName) {
    if (!Mod::get()->getSettingValue<bool>("enable-backups")) return;

    std::error_code ec;
    auto src    = geode::dirs::getSaveDir() / "CCGameManager.dat";
    auto modDir = Mod::get()->getSaveDir();
    std::filesystem::create_directories(modDir, ec);
    auto dst = modDir / slotName;

    if (!std::filesystem::exists(src, ec)) {
        log::warn("[Better Saving] CCGameManager.dat not found - skipping backup.");
        return;
    }

    if (Mod::get()->getSettingValue<bool>("verbose-logging")) {
        log::info("[Better Saving] Writing backup: {} -> {}", src.string(), dst.string());
    }

    bool ok = atomicCopy(src, dst, ec);
    if (!ok || ec) {
        log::warn("[Better Saving] Backup failed ({} -> {}): {}",
            src.string(), dst.string(), ec.message());
        Notification::create("Backup failed!", NotificationIcon::Error)->show();
    } else if (Mod::get()->getSettingValue<bool>("verbose-logging")) {
        log::info("[Better Saving] Backup written to slot '{}'.", slotName);
    }
}

bool restoreFromBackup(const std::string& backupName) {
    std::error_code ec;
    auto src = Mod::get()->getSaveDir() / backupName;
    auto dst = geode::dirs::getSaveDir() / "CCGameManager.dat";

    if (!std::filesystem::exists(src, ec)) {
        Notification::create("Backup file not found!", NotificationIcon::Error)->show();
        return false;
    }

    // Stop the auto-save timer so it doesn't fire while we're mid-restore.
    AutoSaveManager::get()->stopTimer();
    AutoSaveManager::get()->removePendingIcon();

    atomicCopy(src, dst, ec);
    if (ec) {
        log::error("[Better Saving] Restore failed: {}", ec.message());
        // Resume the timer since we bailed out.
        AutoSaveManager::get()->scheduleTimer();
        Notification::create("Restore failed!", NotificationIcon::Error)->show();
        return false;
    }

    geode::utils::game::restart(false);
    return true;
}
