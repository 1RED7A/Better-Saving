
#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/GameManager.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/loader/Dirs.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/Notification.hpp>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <atomic>
#include <sstream>
#include <thread>
#include <fstream>

using namespace geode::prelude;

void runSaveLogic(bool force = false);
void doRollingBackup();
void doSafeExitBackup();
bool restoreFromBackup(const std::string& backupName);

static std::atomic<bool> s_saving{false};

struct SavingGuard {
    ~SavingGuard() { s_saving = false; }
};

static std::string fmtFileTime(const std::filesystem::file_time_type& ft) {
    std::time_t t = 0;
    try {
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ft - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
        );
        t = std::chrono::system_clock::to_time_t(sctp);
    } catch (...) {
        return "Unknown Time";
    }

    if (t <= 0) return "Unknown Time";

    std::tm tm{};
#ifdef _WIN32
    if (localtime_s(&tm, &t) != 0) return "Unknown Time";
#else
    if (!localtime_r(&t, &tm)) return "Unknown Time";
#endif

    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    return std::string(buf);
}

static std::string humanSize(uint64_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    double kb = bytes / 1024.0;
    if (kb < 1024) {
        char b[32]; std::snprintf(b, sizeof(b), "%.1f KB", kb); return b;
    }
    double mb = kb / 1024.0;
    char b[32]; std::snprintf(b, sizeof(b), "%.1f MB", mb); return b;
}

static std::string fmtLastSaveTime(int64_t timestamp) {
    if (timestamp <= 0) return "Never";
    std::time_t t = static_cast<std::time_t>(timestamp);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%H:%M", &tm);
    return std::string(buf);
}

// Shows the 2 backup slots with Restore and Delete actions.

class ManageBackupsPopup : public geode::Popup {
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

    bool init() {
        if (!Popup::init(340.f, 240.f)) return false;
        this->setTitle("Manage Backups");
        loadEntries();
        buildUI();
        this->schedule(schedule_selector(ManageBackupsPopup::checkUpdates), 1.0f);
        return true;
    }

    void checkUpdates(float dt) {
        auto oldEntries = m_entries;
        loadEntries();

        bool changed = false;
        for (size_t i = 0; i < m_entries.size(); i++) {
            if (m_entries[i].exists  != oldEntries[i].exists  ||
                m_entries[i].timeStr != oldEntries[i].timeStr ||
                m_entries[i].sizeStr != oldEntries[i].sizeStr) {
                changed = true;
                break;
            }
        }

        if (changed) buildUI();
    }

    void loadEntries() {
        m_entries.clear();
        std::error_code ec;
        auto dir = Mod::get()->getSaveDir();

        auto makeEntry = [&](const std::string& name, const std::string& displayName) {
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

        makeEntry("Save_Backup.bak",           "Regular Backup");
        makeEntry("Last_Safe_Save_Backup.bak", "Safe Exit Backup");
    }

    void buildUI() {
        if (m_container) m_container->removeFromParent();

        m_container = CCNode::create();
        m_mainLayer->addChild(m_container);

        float y = 175.f;

        for (auto& e : m_entries) {
            auto* nameLabel = CCLabelBMFont::create(e.displayName.c_str(), "bigFont.fnt");
            nameLabel->setScale(0.45f);
            nameLabel->setAnchorPoint({0.f, 0.5f});
            nameLabel->setPosition({12.f, y});
            m_container->addChild(nameLabel);

            if (e.exists) {
                auto info     = e.timeStr + "   " + e.sizeStr;
                auto* infoLbl = CCLabelBMFont::create(info.c_str(), "chatFont.fnt");
                infoLbl->setScale(0.55f);
                infoLbl->setAnchorPoint({0.f, 0.5f});
                infoLbl->setPosition({12.f, y - 20.f});
                infoLbl->setColor({170, 170, 170});
                m_container->addChild(infoLbl);

                auto* menu = CCMenu::create();
                menu->setPosition({0.f, 0.f});
                m_container->addChild(menu);

                auto* restoreSpr = ButtonSprite::create(
                    "Restore", "bigFont.fnt", "GJ_button_01.png", 0.7f
                );
                restoreSpr->setScale(0.55f);
                auto* restoreBtn = CCMenuItemSpriteExtra::create(
                    restoreSpr, this,
                    menu_selector(ManageBackupsPopup::onRestore)
                );
                restoreBtn->setUserObject(CCString::create(e.name));
                restoreBtn->setPosition({235.f, y - 8.f});
                menu->addChild(restoreBtn);

                auto* deleteSpr = ButtonSprite::create(
                    "Delete", "bigFont.fnt", "GJ_button_06.png", 0.7f
                );
                deleteSpr->setScale(0.55f);
                auto* deleteBtn = CCMenuItemSpriteExtra::create(
                    deleteSpr, this,
                    menu_selector(ManageBackupsPopup::onDelete)
                );
                deleteBtn->setUserObject(CCString::create(e.name));
                deleteBtn->setPosition({304.f, y - 8.f});
                menu->addChild(deleteBtn);

            } else {
                auto* noneLbl = CCLabelBMFont::create("No backup file found", "chatFont.fnt");
                noneLbl->setScale(0.5f);
                noneLbl->setAnchorPoint({0.f, 0.5f});
                noneLbl->setPosition({12.f, y - 20.f});
                noneLbl->setColor({130, 130, 130});
                m_container->addChild(noneLbl);
            }

            y -= 75.f;
        }

        auto* refreshMenu = CCMenu::create();
        refreshMenu->setPosition({0.f, 0.f});
        m_container->addChild(refreshMenu);

        auto* refreshSpr = ButtonSprite::create(
            "Refresh", "bigFont.fnt", "GJ_button_04.png", 0.7f
        );
        refreshSpr->setScale(0.5f);
        auto* refreshBtn = CCMenuItemSpriteExtra::create(
            refreshSpr, this,
            menu_selector(ManageBackupsPopup::onRefresh)
        );
        refreshBtn->setPosition({170.f, 28.f});
        refreshMenu->addChild(refreshBtn);
    }

    void onRefresh(CCObject*) {
        loadEntries();
        buildUI();
    }

    void onRestore(CCObject* sender) {
        auto* btn = static_cast<CCMenuItemSpriteExtra*>(sender);
        auto* obj = static_cast<CCString*>(btn->getUserObject());
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

    void onDelete(CCObject* sender) {
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
                    Notification::create("Delete failed!", NotificationIcon::Error)->show();
                    log::warn("[Better Saving] Delete failed for {}: {}", name, ec.message());
                } else {
                    Notification::create("Backup deleted", NotificationIcon::Success)->show();
                }
                this->onClose(nullptr);
            }
        );
    }

public:
    static ManageBackupsPopup* create() {
        auto* ret = new ManageBackupsPopup();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

class ForceSaveSettingV3 : public SettingV3 {
public:
    static Result<std::shared_ptr<SettingV3>> parse(
        std::string const& key, std::string const& modID, matjson::Value const& json
    ) {
        auto res  = std::make_shared<ForceSaveSettingV3>();
        auto root = checkJson(json, "ForceSaveSettingV3");
        res->init(key, modID, root);
        res->parseNameAndDescription(root);
        res->parseEnableIf(root);
        root.checkUnknownKeys();
        return root.ok(std::static_pointer_cast<SettingV3>(res));
    }
    bool load(matjson::Value const& json) override { return true; }
    bool save(matjson::Value& json) const override { return true; }
    bool isDefaultValue() const override { return true; }
    void reset() override {}
    SettingNodeV3* createNode(float width) override;
};

class ForceSaveSettingNode : public SettingNodeV3 {
protected:
    CCMenuItemSpriteExtra* m_button       = nullptr;
    ButtonSprite*          m_buttonSprite = nullptr;

    bool init(std::shared_ptr<ForceSaveSettingV3> setting, float width) {
        if (!SettingNodeV3::init(setting, width)) return false;

        m_buttonSprite = ButtonSprite::create(
            "Save Now", "goldFont.fnt", "GJ_button_01.png", 0.8f
        );
        m_buttonSprite->setScale(0.5f);
        m_button = CCMenuItemSpriteExtra::create(
            m_buttonSprite, this, menu_selector(ForceSaveSettingNode::onForceSave)
        );
        this->getButtonMenu()->addChildAtPosition(m_button, Anchor::Center);
        this->getButtonMenu()->setContentWidth(80.f);
        this->getButtonMenu()->updateLayout();
        this->updateState(nullptr);
        return true;
    }

    void updateState(CCNode* invoker) override {
        SettingNodeV3::updateState(invoker);
        auto on = this->getSetting()->shouldEnable();
        m_button->setEnabled(on);
        m_buttonSprite->setCascadeColorEnabled(true);
        m_buttonSprite->setCascadeOpacityEnabled(true);
        m_buttonSprite->setColor(on ? ccWHITE : ccGRAY);
        m_buttonSprite->setOpacity(on ? 255 : 155);
    }

    void onForceSave(CCObject*) { runSaveLogic(true); }
    void onCommit() override {}
    void onResetToDefault() override {}

public:
    static ForceSaveSettingNode* create(
        std::shared_ptr<ForceSaveSettingV3> setting, float width
    ) {
        auto ret = new ForceSaveSettingNode();
        if (ret->init(setting, width)) { ret->autorelease(); return ret; }
        delete ret;
        return nullptr;
    }
    bool hasUncommittedChanges() const override { return false; }
    bool hasNonDefaultValue()    const override { return false; }
};

SettingNodeV3* ForceSaveSettingV3::createNode(float width) {
    return ForceSaveSettingNode::create(
        std::static_pointer_cast<ForceSaveSettingV3>(shared_from_this()), width
    );
}

class BackupManagerSettingV3 : public SettingV3 {
public:
    static Result<std::shared_ptr<SettingV3>> parse(
        std::string const& key, std::string const& modID, matjson::Value const& json
    ) {
        auto res  = std::make_shared<BackupManagerSettingV3>();
        auto root = checkJson(json, "BackupManagerSettingV3");
        res->init(key, modID, root);
        res->parseNameAndDescription(root);
        res->parseEnableIf(root);
        root.checkUnknownKeys();
        return root.ok(std::static_pointer_cast<SettingV3>(res));
    }
    bool load(matjson::Value const& json) override { return true; }
    bool save(matjson::Value& json) const override { return true; }
    bool isDefaultValue() const override { return true; }
    void reset() override {}
    SettingNodeV3* createNode(float width) override;
};

class BackupManagerSettingNode : public SettingNodeV3 {
protected:
    CCMenuItemSpriteExtra* m_button       = nullptr;
    ButtonSprite*          m_buttonSprite = nullptr;

    bool init(std::shared_ptr<BackupManagerSettingV3> setting, float width) {
        if (!SettingNodeV3::init(setting, width)) return false;

        m_buttonSprite = ButtonSprite::create(
            "Manage", "bigFont.fnt", "GJ_button_04.png", 0.8f
        );
        m_buttonSprite->setScale(0.5f);
        m_button = CCMenuItemSpriteExtra::create(
            m_buttonSprite, this,
            menu_selector(BackupManagerSettingNode::onManage)
        );
        this->getButtonMenu()->addChildAtPosition(m_button, Anchor::Center);
        this->getButtonMenu()->setContentWidth(80.f);
        this->getButtonMenu()->updateLayout();
        this->updateState(nullptr);
        return true;
    }

    void updateState(CCNode* invoker) override {
        SettingNodeV3::updateState(invoker);
        auto on = this->getSetting()->shouldEnable();
        m_button->setEnabled(on);
        m_buttonSprite->setCascadeColorEnabled(true);
        m_buttonSprite->setCascadeOpacityEnabled(true);
        // Cyan when active, grey when disabled
        m_buttonSprite->setColor(on ? ccColor3B{100, 220, 255} : ccGRAY);
        m_buttonSprite->setOpacity(on ? 255 : 155);
    }

    void onManage(CCObject*) { ManageBackupsPopup::create()->show(); }
    void onCommit() override {}
    void onResetToDefault() override {}

public:
    static BackupManagerSettingNode* create(
        std::shared_ptr<BackupManagerSettingV3> setting, float width
    ) {
        auto ret = new BackupManagerSettingNode();
        if (ret->init(setting, width)) { ret->autorelease(); return ret; }
        delete ret;
        return nullptr;
    }
    bool hasUncommittedChanges() const override { return false; }
    bool hasNonDefaultValue()    const override { return false; }
};

SettingNodeV3* BackupManagerSettingV3::createNode(float width) {
    return BackupManagerSettingNode::create(
        std::static_pointer_cast<BackupManagerSettingV3>(shared_from_this()), width
    );
}

// Shows "Last saved: HH:MM" in the settings panel, updates every second.

class LastSaveDisplaySettingV3 : public SettingV3 {
public:
    static Result<std::shared_ptr<SettingV3>> parse(
        std::string const& key, std::string const& modID, matjson::Value const& json
    ) {
        auto res  = std::make_shared<LastSaveDisplaySettingV3>();
        auto root = checkJson(json, "LastSaveDisplaySettingV3");
        res->init(key, modID, root);
        res->parseNameAndDescription(root);
        res->parseEnableIf(root);
        root.checkUnknownKeys();
        return root.ok(std::static_pointer_cast<SettingV3>(res));
    }
    bool load(matjson::Value const& json) override { return true; }
    bool save(matjson::Value& json) const override { return true; }
    bool isDefaultValue() const override { return true; }
    void reset() override {}
    SettingNodeV3* createNode(float width) override;
};

class LastSaveDisplayNode : public SettingNodeV3 {
protected:
    CCLabelBMFont* m_label            = nullptr;
    int64_t        m_lastDisplayedTime = 0;

    bool init(std::shared_ptr<LastSaveDisplaySettingV3> setting, float width) {
        if (!SettingNodeV3::init(setting, width)) return false;

        m_lastDisplayedTime = Mod::get()->getSavedValue<int64_t>("last-save-time", 0);
        std::string text = "Last saved: " + fmtLastSaveTime(m_lastDisplayedTime);

        m_label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        m_label->setScale(0.38f);
        m_label->setColor({180, 210, 255});

        this->getButtonMenu()->addChildAtPosition(m_label, Anchor::Center);
        this->getButtonMenu()->setContentWidth(160.f);
        this->getButtonMenu()->updateLayout();

        this->schedule(schedule_selector(LastSaveDisplayNode::updateTimeLabel), 1.0f);
        return true;
    }

    void updateTimeLabel(float dt) {
        auto currentTs = Mod::get()->getSavedValue<int64_t>("last-save-time", 0);
        if (currentTs != m_lastDisplayedTime) {
            m_lastDisplayedTime = currentTs;
            m_label->setString(("Last saved: " + fmtLastSaveTime(currentTs)).c_str());
        }
    }

    void onCommit() override {}
    void onResetToDefault() override {}

public:
    static LastSaveDisplayNode* create(
        std::shared_ptr<LastSaveDisplaySettingV3> setting, float width
    ) {
        auto ret = new LastSaveDisplayNode();
        if (ret->init(setting, width)) { ret->autorelease(); return ret; }
        delete ret;
        return nullptr;
    }
    bool hasUncommittedChanges() const override { return false; }
    bool hasNonDefaultValue()    const override { return false; }
};

SettingNodeV3* LastSaveDisplaySettingV3::createNode(float width) {
    return LastSaveDisplayNode::create(
        std::static_pointer_cast<LastSaveDisplaySettingV3>(shared_from_this()), width
    );
}

// Singleton CCNode that owns the scheduler and all save state.

class AutoSaveManager : public CCNode {
public:
    float m_accumulator  = 0.0f;
    bool  m_pendingSave  = false;
    bool  m_timerRunning = false;
    std::chrono::steady_clock::time_point m_pendingTimestamp;
    std::chrono::steady_clock::time_point m_lastSaveTimestamp;

    CCSprite* m_pendingIcon = nullptr;

    static AutoSaveManager* get() {
        static AutoSaveManager* s_instance = nullptr;
        if (!s_instance) {
            s_instance = new AutoSaveManager();
            s_instance->init();
            s_instance->retain();
            CCDirector::get()->getScheduler()->resumeTarget(s_instance);
            s_instance->m_lastSaveTimestamp =
                std::chrono::steady_clock::now() - std::chrono::seconds(3600);
        }
        return s_instance;
    }

    void addPendingIcon() {
        if (m_pendingIcon) return;
        auto* pl = PlayLayer::get();
        if (!pl) return;

        auto visible  = CCDirector::get()->getVisibleSize();
        m_pendingIcon = CCSprite::create("GJ_infoIcon_001.png");
        if (!m_pendingIcon) return;

        m_pendingIcon->setScale(0.6f);
        m_pendingIcon->setPosition({ visible.width - 36.f, visible.height - 36.f });
        pl->addChild(m_pendingIcon, 1000);
    }

    void removePendingIcon() {
        if (!m_pendingIcon) return;
        m_pendingIcon->removeFromParentAndCleanup(true);
        m_pendingIcon = nullptr;
    }

    void checkPending() {
        if (!m_pendingSave) return;
        if (PlayLayer::get() == nullptr) triggerSaveAttempt();
    }

    void scheduleTimer() {
        if (m_timerRunning) {
            if (Mod::get()->getSettingValue<bool>("verbose-logging"))
                log::info("[Better Saving] Timer already running â€” skipping reschedule.");
            return;
        }

        CCDirector::get()->getScheduler()->scheduleSelector(
            schedule_selector(AutoSaveManager::updateTimer), this, 1.0f, false
        );
        m_accumulator  = 0.0f;
        m_timerRunning = true;

        if (Mod::get()->getSettingValue<bool>("verbose-logging"))
            log::info("[Better Saving] Timer scheduled.");
    }

    void stopTimer() {
        CCDirector::get()->getScheduler()->unscheduleSelector(
            schedule_selector(AutoSaveManager::updateTimer), this
        );
        m_pendingSave  = false;
        m_accumulator  = 0.0f;
        m_timerRunning = false;

        if (Mod::get()->getSettingValue<bool>("verbose-logging"))
            log::info("[Better Saving] Timer stopped.");
    }

    void updateTimer(float dt) {
        if (!Mod::get()->getSettingValue<bool>("enabled")) return;
        if (!Mod::get()->getSettingValue<bool>("auto-save-enabled")) return;

        if (m_pendingSave) {
            auto now     = std::chrono::steady_clock::now();
            auto ageMins = std::chrono::duration_cast<std::chrono::minutes>(
                now - m_pendingTimestamp
            ).count();

            if (ageMins > Mod::get()->getSettingValue<int64_t>("pending-save-timeout")) {
                m_pendingSave = false;
                log::warn("[Better Saving] Pending save expired after {} minutes â€” discarded.", (int)ageMins);
                removePendingIcon();
                return;
            }

            triggerSaveAttempt();
            return;
        }

        m_accumulator += dt;
        float targetSecs = static_cast<float>(
            Mod::get()->getSettingValue<int64_t>("save-interval") * 60
        );

        if (m_accumulator >= targetSecs) {
            m_accumulator = 0.0f;
            triggerSaveAttempt();
        }
    }

    void triggerSaveAttempt() {
        bool inLevel      = PlayLayer::get() != nullptr;
        bool inEditor     = LevelEditorLayer::get() != nullptr;
        bool saveInEditor = Mod::get()->getSettingValue<bool>("save-in-editor");

        bool isModalActive = false;
        if (auto* scene = CCDirector::get()->getRunningScene()) {
            if (scene->getChildByType<FLAlertLayer>(0)) isModalActive = true;
        }

        if (inLevel || isModalActive || (inEditor && !saveInEditor)) {
            if (!m_pendingSave) {
                m_pendingSave      = true;
                m_pendingTimestamp = std::chrono::steady_clock::now();

                if (Mod::get()->getSettingValue<bool>("verbose-logging"))
                    log::info("[Better Saving] Save deferred: inLevel={} inEditor={} modal={}",
                        inLevel, inEditor, isModalActive);

                auto behavior = Mod::get()->getSettingValue<std::string>("defer-behavior");
                if (behavior == "hud-icon" && inLevel) addPendingIcon();
            }
            return;
        }

        runSaveLogic();
    }
};

static bool waitForFileReady(const std::filesystem::path& p, int maxRetries = 5) {
    for (int i = 0; i < maxRetries; ++i) {
        std::ifstream f(p, std::ios::binary);
        if (f.is_open()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return false;
}

bool atomicCopy(
    const std::filesystem::path& src,
    const std::filesystem::path& dst,
    std::error_code& ec
) {
    auto tmp = dst.string() + ".tmp." +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());

    if (!waitForFileReady(src)) {
        ec = std::make_error_code(std::errc::resource_unavailable_try_again);
        log::warn("[Better Saving] Source file locked after retries: {}", src.string());
        return false;
    }

    for (int attempt = 0; attempt < 3; ++attempt) {
        std::filesystem::copy_file(src, tmp, std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (ec) return false;

    auto old = dst.string() + ".old";
    if (std::filesystem::exists(dst, ec)) {
        std::filesystem::rename(dst, old, ec);
        if (ec) std::filesystem::remove(dst, ec);
    }

    ec = {};
    for (int attempt = 0; attempt < 3; ++attempt) {
        std::filesystem::rename(tmp, dst, ec);
        if (!ec) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::error_code cleanEc;
    std::filesystem::remove(old, cleanEc);

    return !ec;
}

void doRollingBackup() {
    if (!Mod::get()->getSettingValue<bool>("enable-backups")) return;

    std::error_code ec;
    auto mainFile = geode::dirs::getSaveDir() / "CCGameManager.dat";
    auto modDir = Mod::get()->getSaveDir();
    std::filesystem::create_directories(modDir, ec);
    auto b0 = modDir / "Save_Backup.bak";

    if (Mod::get()->getSettingValue<bool>("verbose-logging")) {
        log::info("[Better Saving] Source: {}", mainFile.string());
        log::info("[Better Saving] Dest:   {}", b0.string());
    }

    if (!std::filesystem::exists(mainFile, ec)) {
        log::warn("[Better Saving] CCGameManager.dat NOT found in GD folder! Skipping.");
        return;
    }

    bool ok = atomicCopy(mainFile, b0, ec);

    if (!ok || ec) {
        log::warn("[Better Saving] Backup failed: {} -> {}: {}", mainFile.string(), b0.string(), ec.message());
        Notification::create("Backup failed!", NotificationIcon::Error)->show();
    } else if (Mod::get()->getSettingValue<bool>("verbose-logging")) {
        log::info("[Better Saving] Regular backup created successfully.");
    }
}

void doSafeExitBackup() {
    if (!Mod::get()->getSettingValue<bool>("enable-backups")) return;

    std::error_code ec;
    auto mainFile   = geode::dirs::getSaveDir() / "CCGameManager.dat"; 
    
    auto modDir = Mod::get()->getSaveDir();
    std::filesystem::create_directories(modDir, ec);
    auto safeFile = modDir / "Last_Safe_Save_Backup.bak";

    if (!std::filesystem::exists(mainFile, ec)) return;

    bool ok = atomicCopy(mainFile, safeFile, ec);

    if (!ok || ec) {
        log::warn("[Better Saving] Safe exit backup failed: {} -> {}: {}", mainFile.string(), safeFile.string(), ec.message());
        Notification::create("Safe exit backup failed!", NotificationIcon::Error)->show();
    } else if (Mod::get()->getSettingValue<bool>("verbose-logging")) {
        log::info("[Better Saving] Safe exit backup created.");
    }
}

bool restoreFromBackup(const std::string& backupName) {
    std::error_code ec;
    auto backupFile = Mod::get()->getSaveDir() / backupName;
auto mainFile   = geode::dirs::getSaveDir() / "CCGameManager.dat"; 

    if (!std::filesystem::exists(backupFile, ec)) {
        Notification::create("Backup not found!", NotificationIcon::Error)->show();
        return false;
    }

    AutoSaveManager::get()->stopTimer();
    AutoSaveManager::get()->removePendingIcon();

    atomicCopy(backupFile, mainFile, ec);

    if (ec) {
        log::error("[Better Saving] Restore failed: {}", ec.message());
        AutoSaveManager::get()->scheduleTimer();
        Notification::create("Restore failed!", NotificationIcon::Error)->show();
        return false;
    }

    geode::utils::game::restart(false);
    return true;
}

void runSaveLogic(bool force) {
    auto now = std::chrono::steady_clock::now();
    auto* manager = AutoSaveManager::get();

    auto cooldownSecs = Mod::get()->getSettingValue<int64_t>("save-cooldown");
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - manager->m_lastSaveTimestamp
    ).count();

    if (!force && elapsed < cooldownSecs) {
        if (Mod::get()->getSettingValue<bool>("verbose-logging"))
            log::info("[Better Saving] Save skipped â€” cooldown ({}/{}s).",
                (long long)elapsed, (long long)cooldownSecs);

if (Mod::get()->getSettingValue<bool>("show-notification")) {
            auto last = Mod::get()->getSavedValue<int64_t>("last-save-time", 0);
            if (last > 0) {
                std::ostringstream ss;
                ss << "Already saved at " << fmtLastSaveTime(last);
                Notification::create(ss.str().c_str(), NotificationIcon::Warning)->show();
            } else {
                Notification::create("Already saved recently", NotificationIcon::Warning)->show();
            }
        }
        return;
    }

    auto* gm = GameManager::get();
    if (!gm) return;

    bool expected = false;
    if (!s_saving.compare_exchange_strong(expected, true)) return;
    SavingGuard guard;

    try {
        gm->save();
    } catch (...) {
        log::error("[Better Saving] Exception thrown during save!");
        return;
    }
    
    doRollingBackup(); 

    manager->m_lastSaveTimestamp = now;
    manager->m_pendingSave       = false;
    manager->removePendingIcon();

    Mod::get()->setSavedValue<int64_t>(
        "last-save-time",
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())
    );
Mod::get()->saveData();

    if (Mod::get()->getSettingValue<bool>("verbose-logging"))
        log::info("[Better Saving] Save executed successfully.");
    if (Mod::get()->getSettingValue<bool>("show-notification"))
        Notification::create("Game saved!", NotificationIcon::Success)->show();
}

$execute {
    (void)Mod::get()->registerCustomSettingType("force-save-button", &ForceSaveSettingV3::parse);
    (void)Mod::get()->registerCustomSettingType("backup-manager",    &BackupManagerSettingV3::parse);
    (void)Mod::get()->registerCustomSettingType("last-save-display", &LastSaveDisplaySettingV3::parse);

    if (Mod::get()->getSettingValue<bool>("enabled"))
        AutoSaveManager::get()->scheduleTimer();

    listenForSettingChanges<bool>("enabled", [](bool value) {
        if (value) AutoSaveManager::get()->scheduleTimer();
        else       AutoSaveManager::get()->stopTimer();
        Mod::get()->saveData();
    });

    listenForSettingChanges<int64_t>("save-interval", [](int64_t) {
        if (Mod::get()->getSettingValue<bool>("enabled")) {
            AutoSaveManager::get()->stopTimer();
            AutoSaveManager::get()->scheduleTimer();
        }
        Mod::get()->saveData();
    });

    listenForSettingChanges<bool>("auto-save-enabled", [](bool value) {
        if (Mod::get()->getSettingValue<bool>("enabled")) {
            if (value) AutoSaveManager::get()->scheduleTimer();
            else       AutoSaveManager::get()->stopTimer();
        }
        Mod::get()->saveData();
    });
}

class $modify(MyPlayLayer, PlayLayer) {
    void levelComplete() {
        PlayLayer::levelComplete();

        if (this->m_isPracticeMode) {
            if (Mod::get()->getSettingValue<bool>("verbose-logging"))
                log::info("[Better Saving] Practice mode â€” skipping save.");
            return;
        }

        if (!Mod::get()->getSettingValue<bool>("enabled")) return;
        if (!Mod::get()->getSettingValue<bool>("save-on-complete")) return;

        auto sequence = CCSequence::create(
            CCDelayTime::create(5.0f),
            CCCallFunc::create(this, callfunc_selector(MyPlayLayer::triggerDelayedSave)),
            nullptr
        );
        this->runAction(sequence);
    }

    void triggerDelayedSave() {
        auto* manager = AutoSaveManager::get();
        manager->m_pendingSave = false;
        manager->removePendingIcon();
        runSaveLogic(true);

        if (Mod::get()->getSettingValue<bool>("verbose-logging"))
            log::info("[Better Saving] Level complete save triggered.");
    }
};

class $modify(MyMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        auto* manager = AutoSaveManager::get();

        if (Mod::get()->getSettingValue<bool>("enabled")) {
            bool wasPending = manager->m_pendingSave;
            manager->checkPending();
            bool nowPending = manager->m_pendingSave;
            auto behavior   = Mod::get()->getSettingValue<std::string>("defer-behavior");

            if (wasPending && !nowPending && behavior == "toast-on-menu") {
                if (Mod::get()->getSettingValue<bool>("show-notification"))
                    Notification::create("Deferred save completed", NotificationIcon::Success)->show();
            }

            manager->removePendingIcon();
        }

#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
        auto winSize = CCDirector::get()->getWinSize();

        auto* exitSpr = ButtonSprite::create(
            "X", "bigFont.fnt", "GJ_button_06.png", 0.82f
        );
        exitSpr->setScale(0.8f);

        auto* exitBtn = CCMenuItemSpriteExtra::create(
            exitSpr, this,
            menu_selector(MyMenuLayer::onMobileExit)
        );

        auto* exitMenu = CCMenu::create();
        exitMenu->addChild(exitBtn);
        exitMenu->setPosition({ 48.f, winSize.height - 48.f });
        this->addChild(exitMenu, 10);
#endif

        return true;
    }

#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    void onMobileExit(CCObject*) {
        createQuickPopup(
            "Exit Game",
            "<cy>This button saves and exits the game.</c>",
            "Cancel", "Exit",
            [](auto*, bool ok) {
                if (!ok) return;
                doSafeExitBackup();
                runSaveLogic(true);
                Mod::get()->saveData();
                CCDirector::get()->end();
            }
        );
    }
#endif

    void onQuit(CCObject* sender) {
        doSafeExitBackup();
        MenuLayer::onQuit(sender);
    }
};

//end :)
