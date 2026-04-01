#include "AutoSaveManager.hpp"
#include "Settings.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

// -----------------------------------------------------------------------
// Startup and setting listeners
// -----------------------------------------------------------------------

$execute {
    // Register the three custom setting types declared in mod.json.
    (void)Mod::get()->registerCustomSettingType("force-save-button", &ForceSaveSettingV3::parse);
    (void)Mod::get()->registerCustomSettingType("backup-manager",    &BackupManagerSettingV3::parse);
    (void)Mod::get()->registerCustomSettingType("last-save-display", &LastSaveDisplaySettingV3::parse);

    // Kick off the auto-save timer if the mod is enabled at launch.
    if (Mod::get()->getSettingValue<bool>("enabled"))
        AutoSaveManager::get()->scheduleTimer();

    // Master on/off toggle.
    listenForSettingChanges<bool>("enabled", [](bool on) {
        if (on) AutoSaveManager::get()->scheduleTimer();
        else    AutoSaveManager::get()->stopTimer();
        (void)Mod::get()->saveData();
    });

    // Auto-save sub-toggle - same effect as the master but only controls
    // the timed component; manual saves still work either way.
    listenForSettingChanges<bool>("auto-save-enabled", [](bool on) {
        if (!Mod::get()->getSettingValue<bool>("enabled")) return;
        if (on) AutoSaveManager::get()->scheduleTimer();
        else    AutoSaveManager::get()->stopTimer();
        (void)Mod::get()->saveData();
    });

    // When the interval changes, restart the timer so the new interval
    // takes effect immediately rather than at the next tick.
    listenForSettingChanges<int64_t>("save-interval", [](int64_t) {
        if (!Mod::get()->getSettingValue<bool>("enabled")) return;
        AutoSaveManager::get()->stopTimer();
        AutoSaveManager::get()->scheduleTimer();
        (void)Mod::get()->saveData();
    });
}
