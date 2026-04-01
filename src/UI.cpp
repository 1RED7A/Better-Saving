#include "UI.hpp"
#include "Backups.hpp"
#include "SaveUtils.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/loader/Dirs.hpp>
#include <Geode/ui/Notification.hpp>

#include <filesystem>

using namespace geode::prelude;

// Layout constants - named so they're easy to tweak without hunting magic numbers.
static constexpr float POPUP_W          = 340.f;
static constexpr float POPUP_H          = 240.f;
static constexpr float ROW_START_Y      = 175.f;
static constexpr float ROW_SPACING      = 75.f;
static constexpr float LABEL_X          = 12.f;
static constexpr float INFO_Y_OFFSET    = -20.f;
static constexpr float BTN_Y_OFFSET     = -8.f;
static constexpr float BTN_RESTORE_X    = 235.f;
static constexpr float BTN_DELETE_X     = 304.f;
static constexpr float BTN_REFRESH_X    = 170.f;
static constexpr float BTN_REFRESH_Y    = 28.f;
static constexpr float LABEL_NAME_SCALE = 0.45f;
static constexpr float LABEL_INFO_SCALE = 0.55f;
static constexpr float LABEL_NONE_SCALE = 0.50f;
static constexpr float BTN_SCALE        = 0.55f;
static constexpr float BTN_REFRESH_SCALE = 0.50f;

// -----------------------------------------------------------------------

bool ManageBackupsPopup::init() {
    if (!Popup::init(POPUP_W, POPUP_H)) return false;
    this->setTitle("Manage Backups");
    loadEntries();
    buildUI();
    // Poll every second to catch a backup finishing while the popup is open.
    this->schedule(schedule_selector(ManageBackupsPopup::checkUpdates), 1.0f);
    return true;
}

ManageBackupsPopup* ManageBackupsPopup::create() {
    auto* ret = new ManageBackupsPopup();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

// -----------------------------------------------------------------------
// Data loading
// -----------------------------------------------------------------------

void ManageBackupsPopup::loadEntries() {
    m_entries.clear();
    std::error_code ec;
    auto dir = Mod::get()->getSaveDir();

    auto makeEntry = [&](const char* name, const char* displayName) {
        BackupEntry e;
        e.name        = name;
        e.displayName = displayName;
        auto p        = dir / name;
        e.exists      = std::filesystem::exists(p, ec);
        if (e.exists) {
            e.timeStr = fmtFileTime(std::filesystem::last_write_time(p, ec));
            e.sizeStr = humanSize(std::filesystem::file_size(p, ec));
        }
        m_entries.push_back(e);
    };

    makeEntry(BACKUP_ROLLING,   "Regular Backup");
    makeEntry(BACKUP_SAFE_EXIT, "Safe Exit Backup");
}

void ManageBackupsPopup::checkUpdates(float dt) {
    auto old = m_entries;
    loadEntries();

    for (size_t i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].exists  != old[i].exists  ||
            m_entries[i].timeStr != old[i].timeStr ||
            m_entries[i].sizeStr != old[i].sizeStr)
        {
            buildUI();
            return;
        }
    }
}

// -----------------------------------------------------------------------
// UI construction
// -----------------------------------------------------------------------

void ManageBackupsPopup::buildUI() {
    if (m_container) m_container->removeFromParent();
    m_container = CCNode::create();
    m_mainLayer->addChild(m_container);

    float y = ROW_START_Y;

    for (auto& e : m_entries) {
        auto* nameLabel = CCLabelBMFont::create(e.displayName.c_str(), "bigFont.fnt");
        nameLabel->setScale(LABEL_NAME_SCALE);
        nameLabel->setAnchorPoint({ 0.f, 0.5f });
        nameLabel->setPosition({ LABEL_X, y });
        m_container->addChild(nameLabel);

        if (e.exists) {
            auto info     = e.timeStr + "   " + e.sizeStr;
            auto* infoLbl = CCLabelBMFont::create(info.c_str(), "chatFont.fnt");
            infoLbl->setScale(LABEL_INFO_SCALE);
            infoLbl->setAnchorPoint({ 0.f, 0.5f });
            infoLbl->setPosition({ LABEL_X, y + INFO_Y_OFFSET });
            infoLbl->setColor({ 170, 170, 170 });
            m_container->addChild(infoLbl);

            auto* menu = CCMenu::create();
            menu->setPosition({ 0.f, 0.f });
            m_container->addChild(menu);

            // Restore button
            auto* restoreSpr = ButtonSprite::create("Restore", "bigFont.fnt", "GJ_button_01.png", 0.7f);
            restoreSpr->setScale(BTN_SCALE);
            auto* restoreBtn = CCMenuItemSpriteExtra::create(
                restoreSpr, this, menu_selector(ManageBackupsPopup::onRestore)
            );
            restoreBtn->setUserObject(CCString::create(e.name));
            restoreBtn->setPosition({ BTN_RESTORE_X, y + BTN_Y_OFFSET });
            menu->addChild(restoreBtn);

            // Delete button
            auto* deleteSpr = ButtonSprite::create("Delete", "bigFont.fnt", "GJ_button_06.png", 0.7f);
            deleteSpr->setScale(BTN_SCALE);
            auto* deleteBtn = CCMenuItemSpriteExtra::create(
                deleteSpr, this, menu_selector(ManageBackupsPopup::onDelete)
            );
            deleteBtn->setUserObject(CCString::create(e.name));
            deleteBtn->setPosition({ BTN_DELETE_X, y + BTN_Y_OFFSET });
            menu->addChild(deleteBtn);

        } else {
            auto* noneLbl = CCLabelBMFont::create("No backup file found", "chatFont.fnt");
            noneLbl->setScale(LABEL_NONE_SCALE);
            noneLbl->setAnchorPoint({ 0.f, 0.5f });
            noneLbl->setPosition({ LABEL_X, y + INFO_Y_OFFSET });
            noneLbl->setColor({ 130, 130, 130 });
            m_container->addChild(noneLbl);
        }

        y -= ROW_SPACING;
    }

    // Refresh button at the bottom of the popup
    auto* refreshMenu = CCMenu::create();
    refreshMenu->setPosition({ 0.f, 0.f });
    m_container->addChild(refreshMenu);

    auto* refreshSpr = ButtonSprite::create("Refresh", "bigFont.fnt", "GJ_button_04.png", 0.7f);
    refreshSpr->setScale(BTN_REFRESH_SCALE);
    auto* refreshBtn = CCMenuItemSpriteExtra::create(
        refreshSpr, this, menu_selector(ManageBackupsPopup::onRefresh)
    );
    refreshBtn->setPosition({ BTN_REFRESH_X, BTN_REFRESH_Y });
    refreshMenu->addChild(refreshBtn);
}

// -----------------------------------------------------------------------
// Button callbacks
// -----------------------------------------------------------------------

void ManageBackupsPopup::onRefresh(CCObject*) {
    loadEntries();
    buildUI();
}

void ManageBackupsPopup::onRestore(CCObject* sender) {
    auto* btn  = static_cast<CCMenuItemSpriteExtra*>(sender);
    auto* obj  = static_cast<CCString*>(btn->getUserObject());
    if (!obj) return;

    std::string name = obj->getCString();
    createQuickPopup(
        "Restore Backup",
        ("Restore from " + name + "?\nThis will restart the game.").c_str(),
        "Cancel", "Restore",
        [name](auto*, bool ok) {
            if (ok) restoreFromBackup(name);
        }
    );
}

void ManageBackupsPopup::onDelete(CCObject* sender) {
    auto* btn = static_cast<CCMenuItemSpriteExtra*>(sender);
    auto* obj = static_cast<CCString*>(btn->getUserObject());
    if (!obj) return;

    std::string name = obj->getCString();
    createQuickPopup(
        "Delete Backup",
        ("Delete " + name + "?").c_str(),
        "Cancel", "Delete",
        [this, name](auto*, bool ok) {
            if (!ok) return;
            std::error_code ec;
            std::filesystem::remove(Mod::get()->getSaveDir() / name, ec);
            if (ec) {
                log::warn("[Better Saving] Delete failed for {}: {}", name, ec.message());
                Notification::create("Delete failed!", NotificationIcon::Error)->show();
            } else {
                Notification::create("Backup deleted.", NotificationIcon::Success)->show();
            }
            this->onClose(nullptr);
        }
    );
}
