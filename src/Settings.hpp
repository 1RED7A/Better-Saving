#pragma once

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>

using namespace geode::prelude;

// -----------------------------------------------------------------------
// Three custom setting types used in mod.json:
//
//   custom:force-save-button  - "Save Now" button that calls runSaveLogic(true)
//   custom:backup-manager     - "Manage" button that opens ManageBackupsPopup
//   custom:last-save-display  - read-only label showing the last save time
//
// Each type needs a SettingV3 subclass (handles JSON parsing / Geode
// plumbing) and a SettingNodeV3 subclass (handles the actual UI node).
//
// The parse/load/save/isDefaultValue/reset overrides are identical across
// all three because none of them store any persistent value - they're
// purely action/display widgets.
// -----------------------------------------------------------------------

// --- Force Save ---

class ForceSaveSettingV3 : public SettingV3 {
public:
    static Result<std::shared_ptr<SettingV3>> parse(
        std::string const& key, std::string const& modID, matjson::Value const& json
    );
    bool load(matjson::Value const& json) override { return true; }
    bool save(matjson::Value& json)  const override { return true; }
    bool isDefaultValue()            const override { return true; }
    void reset()                           override {}
    SettingNodeV3* createNode(float width) override;
};

// --- Backup Manager ---

class BackupManagerSettingV3 : public SettingV3 {
public:
    static Result<std::shared_ptr<SettingV3>> parse(
        std::string const& key, std::string const& modID, matjson::Value const& json
    );
    bool load(matjson::Value const& json) override { return true; }
    bool save(matjson::Value& json)  const override { return true; }
    bool isDefaultValue()            const override { return true; }
    void reset()                           override {}
    SettingNodeV3* createNode(float width) override;
};

// --- Last Save Display ---

class LastSaveDisplaySettingV3 : public SettingV3 {
public:
    static Result<std::shared_ptr<SettingV3>> parse(
        std::string const& key, std::string const& modID, matjson::Value const& json
    );
    bool load(matjson::Value const& json) override { return true; }
    bool save(matjson::Value& json)  const override { return true; }
    bool isDefaultValue()            const override { return true; }
    void reset()                           override {}
    SettingNodeV3* createNode(float width) override;
};
