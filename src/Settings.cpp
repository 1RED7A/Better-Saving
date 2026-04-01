#include "Settings.hpp"
#include "SaveLogic.hpp"
#include "SaveUtils.hpp"
#include "UI.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/loader/SettingV3.hpp>

using namespace geode::prelude;

// -----------------------------------------------------------------------
// Shared parse helper
//
// All three setting types have identical JSON structure (just name,
// description, and enable-if). This macro cuts the boilerplate down to
// one line per class without hiding what's going on.
// -----------------------------------------------------------------------
#define IMPL_PARSE(ClassName)                                                    \
    Result<std::shared_ptr<SettingV3>> ClassName::parse(                         \
        std::string const& key, std::string const& modID, matjson::Value const& json \
    ) {                                                                          \
        auto res  = std::make_shared<ClassName>();                               \
        auto root = checkJson(json, #ClassName);                                 \
        res->init(key, modID, root);                                             \
        res->parseNameAndDescription(root);                                      \
        res->parseEnableIf(root);                                                \
        root.checkUnknownKeys();                                                 \
        return root.ok(std::static_pointer_cast<SettingV3>(res));                \
    }

IMPL_PARSE(ForceSaveSettingV3)
IMPL_PARSE(BackupManagerSettingV3)
IMPL_PARSE(LastSaveDisplaySettingV3)

// -----------------------------------------------------------------------
// Shared node helper for the two button-style settings
// -----------------------------------------------------------------------

// Sets button color/opacity depending on the setting's enabled state.
static void applyButtonState(
    CCMenuItemSpriteExtra* btn,
    ButtonSprite* spr,
    bool enabled,
    ccColor3B activeColor = ccWHITE
) {
    btn->setEnabled(enabled);
    spr->setCascadeColorEnabled(true);
    spr->setCascadeOpacityEnabled(true);
    spr->setColor(enabled ? activeColor : ccGRAY);
    spr->setOpacity(enabled ? 255 : 155);
}

// -----------------------------------------------------------------------
// ForceSaveSettingNode
// -----------------------------------------------------------------------

class ForceSaveSettingNode : public SettingNodeV3 {
protected:
    CCMenuItemSpriteExtra* m_button = nullptr;
    ButtonSprite*  m_buttonSprite   = nullptr;

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
        applyButtonState(m_button, m_buttonSprite, this->getSetting()->shouldEnable());
    }

    void onForceSave(CCObject*) { runSaveLogic(true); }
    void onCommit()        override {}
    void onResetToDefault() override {}

public:
    static ForceSaveSettingNode* create(
        std::shared_ptr<ForceSaveSettingV3> setting, float width
    ) {
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

// -----------------------------------------------------------------------
// BackupManagerSettingNode
// -----------------------------------------------------------------------

class BackupManagerSettingNode : public SettingNodeV3 {
protected:
    CCMenuItemSpriteExtra* m_button = nullptr;
    ButtonSprite*  m_buttonSprite   = nullptr;

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
        // Use a cyan tint when active to visually distinguish it from the gold save button.
        applyButtonState(m_button, m_buttonSprite,
            this->getSetting()->shouldEnable(), ccColor3B{100, 220, 255});
    }

    void onManage(CCObject*) { ManageBackupsPopup::create()->show(); }
    void onCommit()        override {}
    void onResetToDefault() override {}

public:
    static BackupManagerSettingNode* create(
        std::shared_ptr<BackupManagerSettingV3> setting, float width
    ) {
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

// -----------------------------------------------------------------------
// LastSaveDisplayNode
//
// Read-only label that shows "Last saved: HH:MM".
// Updates every second via a scheduler callback so it stays fresh while
// the settings panel is open.
// -----------------------------------------------------------------------

class LastSaveDisplayNode : public SettingNodeV3 {
protected:
    CCLabelBMFont* m_label           = nullptr;
    int64_t        m_shownTimestamp  = 0;

    bool init(std::shared_ptr<LastSaveDisplaySettingV3> setting, float width) {
        if (!SettingNodeV3::init(setting, width)) return false;

        m_shownTimestamp = Mod::get()->getSavedValue<int64_t>("last-save-time", 0);
        std::string text = "Last saved: " + fmtLastSaveTime(m_shownTimestamp);

        m_label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        m_label->setScale(0.38f);
        m_label->setColor({ 180, 210, 255 });

        this->getButtonMenu()->addChildAtPosition(m_label, Anchor::Center);
        this->getButtonMenu()->setContentWidth(160.f);
        this->getButtonMenu()->updateLayout();

        this->schedule(schedule_selector(LastSaveDisplayNode::tick), 1.0f);
        return true;
    }

    // Refresh the label only when the saved timestamp actually changes,
    // so we're not calling setString every second for no reason.
    void tick(float) {
        auto ts = Mod::get()->getSavedValue<int64_t>("last-save-time", 0);
        if (ts != m_shownTimestamp) {
            m_shownTimestamp = ts;
            m_label->setString(("Last saved: " + fmtLastSaveTime(ts)).c_str());
        }
    }

    void onCommit()        override {}
    void onResetToDefault() override {}

public:
    static LastSaveDisplayNode* create(
        std::shared_ptr<LastSaveDisplaySettingV3> setting, float width
    ) {
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
