#include "SaveLogic.hpp"
#include "AutoSaveManager.hpp"
#include "Backups.hpp"
#include "SaveUtils.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/ui/Notification.hpp>

#include <atomic>
#include <chrono>
#include <sstream>

using namespace geode::prelude;

// -----------------------------------------------------------------------
// Save guard
//
// s_saving is set to true at the start of a save and cleared by the guard
// destructor. The compare_exchange_strong call in runSaveLogic means only
// one save can run at a time even if something triggers a second one
// (e.g. the user hits the force-save button while the timer fires).
// -----------------------------------------------------------------------
static std::atomic<bool> s_saving{false};

struct SavingGuard {
    ~SavingGuard() { s_saving = false; }
};

// -----------------------------------------------------------------------

void runSaveLogic(bool force) {
    auto  now      = std::chrono::steady_clock::now();
    auto* manager  = AutoSaveManager::get();
    auto  cooldown = Mod::get()->getSettingValue<int64_t>("save-cooldown");
    auto  elapsed  = std::chrono::duration_cast<std::chrono::seconds>(
        now - manager->m_lastSaveTimestamp
    ).count();

    if (!force && elapsed < cooldown) {
        if (Mod::get()->getSettingValue<bool>("verbose-logging"))
            log::info("[Better Saving] Save skipped - cooldown ({}/{}s).",
                (long long)elapsed, (long long)cooldown);

        if (Mod::get()->getSettingValue<bool>("show-notification")) {
            auto lastTs = Mod::get()->getSavedValue<int64_t>("last-save-time", 0);
            if (lastTs > 0) {
                std::ostringstream ss;
                ss << "Already saved at " << fmtLastSaveTime(lastTs);
                Notification::create(ss.str().c_str(), NotificationIcon::Warning)->show();
            } else {
                Notification::create("Already saved recently", NotificationIcon::Warning)->show();
            }
        }
        return;
    }

    auto* gm = GameManager::get();
    if (!gm) return;

    // Bail out if another save is already in progress.
    bool expected = false;
    if (!s_saving.compare_exchange_strong(expected, true)) return;
    SavingGuard guard;

    try {
        gm->save();
    } catch (...) {
        log::error("[Better Saving] Exception during save - aborting.");
        return;
    }

    // Rolling backup after every successful save.
    createBackup(BACKUP_ROLLING);

    manager->m_lastSaveTimestamp = now;
    manager->m_pendingSave       = false;
    manager->removePendingIcon();

    Mod::get()->setSavedValue<int64_t>(
        "last-save-time",
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())
    );
    (void)Mod::get()->saveData();

    if (Mod::get()->getSettingValue<bool>("verbose-logging"))
        log::info("[Better Saving] Save completed successfully.");
    if (Mod::get()->getSettingValue<bool>("show-notification"))
        Notification::create("Game saved!", NotificationIcon::Success)->show();
}
