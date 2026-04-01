#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <vector>
#include <string>

using namespace geode::prelude;

// -----------------------------------------------------------------------
// ManageBackupsPopup
//
// Shows both backup slots with their timestamp, size, and Restore / Delete
// actions. Auto-refreshes every second so the display stays current while
// a backup is being written in the background.
// -----------------------------------------------------------------------
class ManageBackupsPopup : public geode::Popup<> {
public:
    static ManageBackupsPopup* create();

protected:
    struct BackupEntry {
        std::string name;
        std::string displayName;
        std::string timeStr;
        std::string sizeStr;
        bool exists = false;
    };

    std::vector<BackupEntry> m_entries;
    CCNode* m_container = nullptr;

    bool init() override;

    void loadEntries();
    void buildUI();

    void checkUpdates(float dt);
    void onRefresh(CCObject*);
    void onRestore(CCObject* sender);
    void onDelete(CCObject* sender);
};
