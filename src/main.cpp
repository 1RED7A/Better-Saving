
#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/GameManager.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/AppDelegate.hpp>
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

// button layout for the backup popup
constexpr float kRestoreBtnX  = 235.f;
constexpr float kDeleteBtnX   = 304.f;
constexpr float kEntryStepY   = 75.f;
constexpr float kRowLabelX    = 12.f;
constexpr float kInfoOffsetY  = 20.f;
constexpr float kBtnOffsetY   = 8.f;
// wait for level complete animation + results transition before saving
constexpr float kLevelSaveDelay = 5.0f;

void runSaveLogic(bool force = false);
void createBackup(const std::string& slot);
bool restoreFromBackup(const std::string& backupName);
static void focusLostSave();

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

static std::string fmtLastSaveTime(int64_t ts) {
    if (ts <= 0) return "Never";
    std::time_t t = static_cast<std::time_t>(ts);
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

// shows the two backup slots with restore + delete per entry
class ManageBackupsPopup : public geode::Popup<> {
protected:
    struct BackupEntry {
        std::string name, displayName, timeStr, sizeStr;
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

    void setup() override {}

    void checkUpdates(float) {
        auto old = m_entries;
        loadEntries();
        for (size_t i = 0; i < m_entries.size(); i++) {
            if (m_entries[i].exists  != old[i].exists  ||
                m_entries[i].timeStr != old[i].timeStr ||
                m_entries[i].sizeStr != old[i].sizeStr) {
                buildUI();
                return;
            }
        }
    }

    void loadEntries() {
        m_entries.clear();
        std::error_code ec;
        auto dir = Mod::get()->getSaveDir();

        auto push = [&](const std::string& name, const std::string& display) {
            BackupEntry e;
            e.name = name; e.displayName = display;
            auto p = dir / name;
            e.exists = std::filesystem::exists(p, ec);
            if (e.exists) {
                e.timeStr = fmtFileTime(std::filesystem::last_write_time(p, ec));
                e.sizeStr = humanSize(std::filesystem::file_size(p, ec));
            }
            m_entries.push_back(e);
        };

        push("Save_Backup.bak",           "Regular Backup");
        push("Last_Safe_Save_Backup.bak", "Safe Exit Backup");
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
            nameLabel->setPosition({kRowLabelX, y});
            m_container->addChild(nameLabel);

            if (e.exists) {
                auto* infoLbl = CCLabelBMFont::create(
                    (e.timeStr + "   " + e.sizeStr).c_str(), "chatFont.fnt"
                );
                infoLbl->setScale(0.55f);
                infoLbl->setAnchorPoint({0.f, 0.5f});
                infoLbl->setPosition({kRowLabelX, y - kInfoOffsetY});
                infoLbl->setColor({170, 170, 170});
                m_container->addChild(infoLbl);

                auto* menu = CCMenu::create();
                menu->setPosition({0.f, 0.f});
                m_container->addChild(menu);

                // helper so we don't repeat the button setup twice
                auto mkBtn = [&](const char* lbl, const char* spr, float x, SEL_MenuHandler sel) {
                    auto* sp  = ButtonSprite::create(lbl, "bigFont.fnt", spr, 0.7f);
                    sp->setScale(0.55f);
                    auto* btn = CCMenuItemSpriteExtra::create(sp, this, sel);
                    btn->setUserObject(CCString::create(e.name));
                    btn->setPosition({x, y - kBtnOffsetY});
                    menu->addChild(btn);
                };

                mkBtn("Restore", "GJ_button_01.png", kRestoreBtnX, menu_selector(ManageBackupsPopup::onRestore));
                mkBtn("Delete",  "GJ_button_06.png", kDeleteBtnX,  menu_selector(ManageBackupsPopup::onDelete));
            } else {
                auto* noneLbl = CCLabelBMFont::create("No backup file found", "chatFont.fnt");
                noneLbl->setScale(0.5f);
                noneLbl->setAnchorPoint({0.f, 0.5f});
                noneLbl->setPosition({kRowLabelX, y - kInfoOffsetY});
                noneLbl->setColor({130, 130, 130});
                m_container->addChild(noneLbl);
            }
            y -= kEntryStepY;
        }

        auto* rMenu = CCMenu::create();
        rMenu->setPosition({0.f, 0.f});
        m_container->addChild(rMenu);

        auto* rSpr = ButtonSprite::create("Refresh", "bigFont.fnt", "GJ_button_04.png", 0.7f);
        rSpr->setScale(0.5f);
        auto* rBtn = CCMenuItemSpriteExtra::create(
            rSpr, this, menu_selector(ManageBackupsPopup::onRefresh)
        );
        rBtn->setPosition({170.f, 28.f});
        rMenu->addChild(rBtn);
    }

    void onRefresh(CCObject*) { loadEntries(); buildUI(); }

    void onRestore(CCObject* sender) {
        auto* btn = static_cast<CCMenuItemSpriteExtra*>(sender);
        auto* obj = static_cast<CCString*>(btn->getUserObject());
        if (!obj) return;
        std::string name = obj->getCString();
        createQuickPopup(
            "Restore Backup",
            ("Restore from " + name + "?\nThis will restart the game.").c_str(),
            "Cancel", "Restore",
            [name](auto*, bool ok) { if (ok) restoreFromBackup(name); }
        );
    }

    void onDelete(CCObject* sender) {
        auto* btn = static_cast<CCMenuItemSpriteExtra*>(sender);
        auto* obj = static_cast<CCString*>(btn->getUserObject());
        if (!obj) return;
        std::string name = obj->getCString();
        createQuickPopup(
            "Delete Backup", ("Delete " + name + "?").c_str(),
            "Cancel", "Delete",
            [this, name](auto*, bool ok) {
                if (!ok) return;
                std::error_code ec;
                std::filesystem::remove(Mod::get()->getSaveDir() / name, ec);
                if (ec) {
                    log::warn("[BetterSaving] delete failed for {}: {}", name, ec.message());
                    Notification::create("Delete failed!", NotificationIcon::Error)->show();
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
        if (ret->init()) { ret->autorelease(); return ret; }
        delete ret;
        return nullptr;
    }
};

// reduces the 6-line boilerplate that each custom setting parse() needs
template <typename T>
static Result<std::shared_ptr<SettingV3>> parseCustomSetting(
    std::string const& key, std::string const& modID,
    matjson::Value const& json, const char* typeName
) {
    auto res  = std::make_shared<T>();
    auto root = checkJson(json, typeName);
    res->init(key, modID, root);
    res->parseNameAndDescription(root);
    res->parseEnableIf(root);
    root.checkUnknownKeys();
    return root.ok(std::static_pointer_cast<SettingV3>(res));
}

// ---- force save button ----

class ForceSaveSettingV3 : public SettingV3 {
public:
    static Result<std::shared_ptr<SettingV3>> parse(
        std::string const& key, std::string const& modID, matjson::Value const& json
    ) {
        return parseCustomSetting<ForceSaveSettingV3>(key, modID, json, "ForceSaveSettingV3");
    }
    bool load(matjson::Value const&) override { return true; }
    bool save(matjson::Value&)  const override { return true; }
    bool isDefaultValue()       const override { return true; }
    void reset() override {}
    SettingNodeV3* createNode(float width) override;
};

class ForceSaveSettingNode : public SettingNodeV3 {
protected:
    CCMenuItemSpriteExtra* m_button       = nullptr;
    ButtonSprite*          m_buttonSprite = nullptr;

    bool init(std::shared_ptr<ForceSaveSettingV3> setting, float width) {
        if (!SettingNodeV3::init(setting, width)) return false;
        m_buttonSprite = ButtonSprite::create("Save Now", "goldFont.fnt", "GJ_button_01.png", 0.8f);
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
        bool on = this->getSetting()->shouldEnable();
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
    static ForceSaveSettingNode* create(std::shared_ptr<ForceSaveSettingV3> setting, float width) {
        auto* ret = new ForceSaveSettingNode();
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

// ---- backup manager button ----

class BackupManagerSettingV3 : public SettingV3 {
public:
    static Result<std::shared_ptr<SettingV3>> parse(
        std::string const& key, std::string const& modID, matjson::Value const& json
    ) {
        return parseCustomSetting<BackupManagerSettingV3>(key, modID, json, "BackupManagerSettingV3");
    }
    bool load(matjson::Value const&) override { return true; }
    bool save(matjson::Value&)  const override { return true; }
    bool isDefaultValue()       const override { return true; }
    void reset() override {}
    SettingNodeV3* createNode(float width) override;
};

class BackupManagerSettingNode : public SettingNodeV3 {
protected:
    CCMenuItemSpriteExtra* m_button       = nullptr;
    ButtonSprite*          m_buttonSprite = nullptr;

    bool init(std::shared_ptr<BackupManagerSettingV3> setting, float width) {
        if (!SettingNodeV3::init(setting, width)) return false;
        m_buttonSprite = ButtonSprite::create("Manage", "bigFont.fnt", "GJ_button_04.png", 0.8f);
        m_buttonSprite->setScale(0.5f);
        m_button = CCMenuItemSpriteExtra::create(
            m_buttonSprite, this, menu_selector(BackupManagerSettingNode::onManage)
        );
        this->getButtonMenu()->addChildAtPosition(m_button, Anchor::Center);
        this->getButtonMenu()->setContentWidth(80.f);
        this->getButtonMenu()->updateLayout();
        this->updateState(nullptr);
        return true;
    }

    void updateState(CCNode* invoker) override {
        SettingNodeV3::updateState(invoker);
        bool on = this->getSetting()->shouldEnable();
        m_button->setEnabled(on);
        m_buttonSprite->setCascadeColorEnabled(true);
        m_buttonSprite->setCascadeOpacityEnabled(true);
        m_buttonSprite->setColor(on ? ccColor3B{100, 220, 255} : ccGRAY); // cyan = active
        m_buttonSprite->setOpacity(on ? 255 : 155);
    }

    void onManage(CCObject*) { ManageBackupsPopup::create()->show(); }
    void onCommit() override {}
    void onResetToDefault() override {}

public:
    static BackupManagerSettingNode* create(std::shared_ptr<BackupManagerSettingV3> setting, float width) {
        auto* ret = new BackupManagerSettingNode();
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

// ---- last save timestamp display ----

class LastSaveDisplaySettingV3 : public SettingV3 {
public:
    static Result<std::shared_ptr<SettingV3>> parse(
        std::string const& key, std::string const& modID, matjson::Value const& json
    ) {
        return parseCustomSetting<LastSaveDisplaySettingV3>(key, modID, json, "LastSaveDisplaySettingV3");
    }
    bool load(matjson::Value const&) override { return true; }
    bool save(matjson::Value&)  const override { return true; }
    bool isDefaultValue()       const override { return true; }
    void reset() override {}
    SettingNodeV3* createNode(float width) override;
};

class LastSaveDisplayNode : public SettingNodeV3 {
protected:
    CCLabelBMFont* m_label             = nullptr;
    int64_t        m_lastDisplayedTime = 0;

    bool init(std::shared_ptr<LastSaveDisplaySettingV3> setting, float width) {
        if (!SettingNodeV3::init(setting, width)) return false;

        m_lastDisplayedTime = Mod::get()->getSavedValue<int64_t>("last-save-time", 0);
        auto text = std::string("Last saved: ") + fmtLastSaveTime(m_lastDisplayedTime);

        m_label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        m_label->setScale(0.38f);
        m_label->setColor({180, 210, 255});

        this->getButtonMenu()->addChildAtPosition(m_label, Anchor::Center);
        this->getButtonMenu()->setContentWidth(160.f);
        this->getButtonMenu()->updateLayout();

        this->schedule(schedule_selector(LastSaveDisplayNode::refreshLabel), 1.0f);
        return true;
    }

    void refreshLabel(float) {
        auto ts = Mod::get()->getSavedValue<int64_t>("last-save-time", 0);
        if (ts == m_lastDisplayedTime) return;
        m_lastDisplayedTime = ts;
        m_label->setString(("Last saved: " + fmtLastSaveTime(ts)).c_str());
    }

    void onCommit() override {}
    void onResetToDefault() override {}

public:
    static LastSaveDisplayNode* create(std::shared_ptr<LastSaveDisplaySettingV3> setting, float width) {
        auto* ret = new LastSaveDisplayNode();
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

// singleton that owns the scheduler and all mutable save state
class AutoSaveManager : public CCNode {
public:
    float  m_accumulator  = 0.0f;
    bool   m_pendingSave  = false;
    bool   m_timerRunning = false;
    std::chrono::steady_clock::time_point m_pendingTimestamp;
    std::chrono::steady_clock::time_point m_lastSaveTimestamp;
    CCSprite* m_pendingIcon = nullptr;

    static AutoSaveManager* get() {
        static AutoSaveManager* inst = nullptr;
        if (!inst) {
            inst = new AutoSaveManager();
            inst->init();
            inst->retain();
            CCDirector::get()->getScheduler()->resumeTarget(inst);
            // push the last-save time way back so the first trigger isn't blocked by cooldown
            inst->m_lastSaveTimestamp =
                std::chrono::steady_clock::now() - std::chrono::seconds(3600);
        }
        return inst;
    }

    void addPendingIcon() {
        if (m_pendingIcon) return;
        auto* pl = PlayLayer::get();
        if (!pl) return;
        auto vis  = CCDirector::get()->getVisibleSize();
        m_pendingIcon = CCSprite::create("GJ_infoIcon_001.png");
        if (!m_pendingIcon) return;
        m_pendingIcon->setScale(0.6f);
        m_pendingIcon->setPosition({vis.width - 36.f, vis.height - 36.f});
        pl->addChild(m_pendingIcon, 1000);
    }

    void removePendingIcon() {
        if (!m_pendingIcon) return;
        m_pendingIcon->removeFromParentAndCleanup(true);
        m_pendingIcon = nullptr;
    }

    void checkPending() {
        if (!m_pendingSave) return;
        if (!PlayLayer::get()) triggerSaveAttempt();
    }

    void scheduleTimer() {
        if (m_timerRunning) return;
        CCDirector::get()->getScheduler()->scheduleSelector(
            schedule_selector(AutoSaveManager::updateTimer), this, 1.0f, false
        );
        m_accumulator  = 0.0f;
        m_timerRunning = true;
        if (Mod::get()->getSettingValue<bool>("verbose-logging"))
            log::info("[BetterSaving] timer started");
    }

    void stopTimer() {
        CCDirector::get()->getScheduler()->unscheduleSelector(
            schedule_selector(AutoSaveManager::updateTimer), this
        );
        m_pendingSave  = false;
        m_accumulator  = 0.0f;
        m_timerRunning = false;
        if (Mod::get()->getSettingValue<bool>("verbose-logging"))
            log::info("[BetterSaving] timer stopped");
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
                log::warn("[BetterSaving] pending save discarded after {}min", (int)ageMins);
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
        bool inLevel  = PlayLayer::get() != nullptr;
        bool inEditor = LevelEditorLayer::get() != nullptr;
        bool editorOk = Mod::get()->getSettingValue<bool>("save-in-editor");

        // don't save if a blocking alert popup is visible
        bool modalUp = false;
        if (auto* scene = CCDirector::get()->getRunningScene())
            if (scene->getChildByType<FLAlertLayer>(0)) modalUp = true;

        if (inLevel || modalUp || (inEditor && !editorOk)) {
            if (!m_pendingSave) {
                m_pendingSave      = true;
                m_pendingTimestamp = std::chrono::steady_clock::now();
                auto behavior = Mod::get()->getSettingValue<std::string>("defer-behavior");
                if (behavior == "hud-icon" && inLevel) addPendingIcon();
                if (Mod::get()->getSettingValue<bool>("verbose-logging"))
                    log::info("[BetterSaving] deferred (inLevel={} inEditor={} modal={})",
                        inLevel, inEditor, modalUp);
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

// copy src -> tmp, then atomically rename into place to avoid half-written backups
static bool atomicCopy(
    const std::filesystem::path& src,
    const std::filesystem::path& dst,
    std::error_code& ec
) {
    auto tmp = dst.string() + ".tmp." +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());

    if (!waitForFileReady(src)) {
        ec = std::make_error_code(std::errc::resource_unavailable_try_again);
        log::warn("[BetterSaving] source file locked: {}", src.string());
        return false;
    }

    for (int i = 0; i < 3; ++i) {
        std::filesystem::copy_file(src, tmp, std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (ec) return false;

    // rotate dst -> .old before promoting tmp -> dst
    auto old = dst.string() + ".old";
    if (std::filesystem::exists(dst, ec)) {
        std::filesystem::rename(dst, old, ec);
        if (ec) std::filesystem::remove(dst, ec);
    }

    ec = {};
    for (int i = 0; i < 3; ++i) {
        std::filesystem::rename(tmp, dst, ec);
        if (!ec) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::error_code cleanEc;
    std::filesystem::remove(old, cleanEc);
    return !ec;
}

// slot: "Save_Backup.bak" for rolling saves, "Last_Safe_Save_Backup.bak" for exit/focus-loss
void createBackup(const std::string& slot) {
    if (!Mod::get()->getSettingValue<bool>("enable-backups")) return;

    std::error_code ec;
    auto src    = geode::dirs::getSaveDir() / "CCGameManager.dat";
    auto modDir = Mod::get()->getSaveDir();
    std::filesystem::create_directories(modDir, ec);
    auto dst = modDir / slot;

    if (!std::filesystem::exists(src, ec)) {
        log::warn("[BetterSaving] CCGameManager.dat not found, skipping backup");
        return;
    }

    if (Mod::get()->getSettingValue<bool>("verbose-logging"))
        log::info("[BetterSaving] backup {} -> {}", src.string(), dst.string());

    if (!atomicCopy(src, dst, ec) || ec) {
        log::warn("[BetterSaving] backup to {} failed: {}", slot, ec.message());
        Notification::create("Backup failed!", NotificationIcon::Error)->show();
    } else if (Mod::get()->getSettingValue<bool>("verbose-logging")) {
        log::info("[BetterSaving] backup {} ok", slot);
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
        log::error("[BetterSaving] restore failed: {}", ec.message());
        AutoSaveManager::get()->scheduleTimer();
        Notification::create("Restore failed!", NotificationIcon::Error)->show();
        return false;
    }

    geode::utils::game::restart(false);
    return true;
}

void runSaveLogic(bool force) {
    auto  now     = std::chrono::steady_clock::now();
    auto* manager = AutoSaveManager::get();

    auto cooldown = Mod::get()->getSettingValue<int64_t>("save-cooldown");
    auto elapsed  = std::chrono::duration_cast<std::chrono::seconds>(
        now - manager->m_lastSaveTimestamp
    ).count();

    if (!force && elapsed < cooldown) {
        if (Mod::get()->getSettingValue<bool>("verbose-logging"))
            log::info("[BetterSaving] skipped - cooldown ({}/{}s)", (long long)elapsed, (long long)cooldown);
        if (Mod::get()->getSettingValue<bool>("show-notification")) {
            auto last = Mod::get()->getSavedValue<int64_t>("last-save-time", 0);
            auto msg  = last > 0
                ? "Already saved at " + fmtLastSaveTime(last)
                : std::string("Already saved recently");
            Notification::create(msg.c_str(), NotificationIcon::Warning)->show();
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
        log::error("[BetterSaving] exception during gm->save()!");
        return;
    }

    createBackup("Save_Backup.bak");

    manager->m_lastSaveTimestamp = now;
    manager->m_pendingSave       = false;
    manager->removePendingIcon();

    Mod::get()->setSavedValue<int64_t>(
        "last-save-time",
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())
    );
    Mod::get()->saveData();

    if (Mod::get()->getSettingValue<bool>("verbose-logging"))
        log::info("[BetterSaving] save complete");
    if (Mod::get()->getSettingValue<bool>("show-notification"))
        Notification::create("Game saved!", NotificationIcon::Success)->show();
}

// called on alt-tab (desktop) and app backgrounding (mobile)
// cocos2d-x calls applicationWillResignActive from the WM_ACTIVATE handler on Windows too
static void focusLostSave() {
    if (!Mod::get()->getSettingValue<bool>("enabled")) return;
    createBackup("Last_Safe_Save_Backup.bak");
    runSaveLogic(true);
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

        if (this->m_isPracticeMode) return; // practice completions don't count
        if (!Mod::get()->getSettingValue<bool>("enabled")) return;
        if (!Mod::get()->getSettingValue<bool>("save-on-complete")) return;

        // kLevelSaveDelay lets the results screen transition finish before we save
        auto* seq = CCSequence::create(
            CCDelayTime::create(kLevelSaveDelay),
            CCCallFunc::create(this, callfunc_selector(MyPlayLayer::doPostLevelSave)),
            nullptr
        );
        this->runAction(seq);
    }

    void doPostLevelSave() {
        auto* manager = AutoSaveManager::get();
        manager->m_pendingSave = false;
        manager->removePendingIcon();
        runSaveLogic(true);
        if (Mod::get()->getSettingValue<bool>("verbose-logging"))
            log::info("[BetterSaving] post-level save triggered");
    }
};

class $modify(MyMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        auto* manager = AutoSaveManager::get();
        if (Mod::get()->getSettingValue<bool>("enabled")) {
            bool wasPending = manager->m_pendingSave;
            manager->checkPending();
            auto behavior = Mod::get()->getSettingValue<std::string>("defer-behavior");
            if (wasPending && !manager->m_pendingSave && behavior == "toast-on-menu") {
                if (Mod::get()->getSettingValue<bool>("show-notification"))
                    Notification::create("Deferred save completed", NotificationIcon::Success)->show();
            }
            manager->removePendingIcon();
        }

#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
        auto winSize = CCDirector::get()->getWinSize();
        auto* exitSpr = ButtonSprite::create("X", "bigFont.fnt", "GJ_button_06.png", 0.82f);
        exitSpr->setScale(0.8f);
        auto* exitBtn = CCMenuItemSpriteExtra::create(
            exitSpr, this, menu_selector(MyMenuLayer::onMobileExit)
        );
        auto* exitMenu = CCMenu::create();
        exitMenu->addChild(exitBtn);
        exitMenu->setPosition({48.f, winSize.height - 48.f});
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
                createBackup("Last_Safe_Save_Backup.bak");
                runSaveLogic(true);
                Mod::get()->saveData();
                CCDirector::get()->end();
            }
        );
    }
#endif

    void onQuit(CCObject* sender) {
        createBackup("Last_Safe_Save_Backup.bak");
        MenuLayer::onQuit(sender);
    }
};

// fires on focus loss: alt-tab on Windows (via WM_ACTIVATE -> applicationWillResignActive)
// and app backgrounding on Android/iOS
class $modify(MyAppDelegate, AppDelegate) {
    void applicationWillResignActive() {
        AppDelegate::applicationWillResignActive();
        focusLostSave();
    }
};
