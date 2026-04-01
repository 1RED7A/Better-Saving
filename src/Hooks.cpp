#include "AutoSaveManager.hpp"
#include "SaveLogic.hpp"
#include "Backups.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/Notification.hpp>

using namespace geode::prelude;

// -----------------------------------------------------------------------
// PlayLayer hook
// -----------------------------------------------------------------------

class $modify(MyPlayLayer, PlayLayer) {
    void levelComplete() {
        PlayLayer::levelComplete();

        if (this->m_isPracticeMode) {
            if (Mod::get()->getSettingValue<bool>("verbose-logging"))
                log::info("[Better Saving] Practice mode complete - skipping save.");
            return;
        }

        if (!Mod::get()->getSettingValue<bool>("enabled")) return;
        if (!Mod::get()->getSettingValue<bool>("save-on-complete")) return;

        // Delay slightly so GD has time to finish its own level-complete
        // sequence (fanfare, stat updates) before we call save.
        auto delay = Mod::get()->getSettingValue<int64_t>("level-complete-delay");
        auto* seq  = CCSequence::create(
            CCDelayTime::create(static_cast<float>(delay)),
            CCCallFunc::create(this, callfunc_selector(MyPlayLayer::onLevelCompleteSave)),
            nullptr
        );
        this->runAction(seq);
    }

    void onLevelCompleteSave() {
        auto* manager = AutoSaveManager::get();
        manager->m_pendingSave = false;
        manager->removePendingIcon();
        runSaveLogic(true);

        if (Mod::get()->getSettingValue<bool>("verbose-logging"))
            log::info("[Better Saving] Level complete save done.");
    }
};

// -----------------------------------------------------------------------
// MenuLayer hook
// -----------------------------------------------------------------------

class $modify(MyMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        auto* manager = AutoSaveManager::get();
        if (!Mod::get()->getSettingValue<bool>("enabled")) return true;

        // If a save was deferred while we were in a level, flush it now
        // that we're back on the menu.
        bool wasPending = manager->m_pendingSave;
        manager->checkPending();

        auto behavior = Mod::get()->getSettingValue<std::string>("defer-behavior");
        if (wasPending && !manager->m_pendingSave && behavior == "toast-on-menu") {
            if (Mod::get()->getSettingValue<bool>("show-notification"))
                Notification::create("Deferred save completed.", NotificationIcon::Success)->show();
        }

        manager->removePendingIcon();

#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
        // Mobile doesn't have a native quit button, so add one.
        // It saves + writes a safe-exit backup before handing off to the OS.
        auto winSize = CCDirector::get()->getWinSize();

        auto* exitSpr = ButtonSprite::create("X", "bigFont.fnt", "GJ_button_06.png", 0.82f);
        exitSpr->setScale(0.8f);

        auto* exitBtn = CCMenuItemSpriteExtra::create(
            exitSpr, this, menu_selector(MyMenuLayer::onMobileExit)
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
            "<cy>Saves and exits the game.</c>",
            "Cancel", "Exit",
            [](auto*, bool ok) {
                if (!ok) return;
                createBackup(BACKUP_SAFE_EXIT);
                runSaveLogic(true);
                Mod::get()->saveData();
                CCDirector::get()->end();
            }
        );
    }
#endif

    // Desktop quit - write a safe-exit backup before letting GD shut down.
    void onQuit(CCObject* sender) {
        createBackup(BACKUP_SAFE_EXIT);
        MenuLayer::onQuit(sender);
    }
};

// -----------------------------------------------------------------------
// Focus-loss handler
//
// Saves the game whenever the GD window loses focus (alt-tab on desktop,
// app backgrounding on mobile). Uses Geode's GLFWWindow callbacks on
// desktop and Android's onPause on mobile.
//
// We force the save (bypass cooldown) because losing focus is exactly the
// kind of "you might not come back" moment we want to protect against.
// -----------------------------------------------------------------------

#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)

#include <Geode/modify/CCEGLView.hpp>

class $modify(MyCCEGLView, CCEGLView) {
    void onGLFWWindowFocusCallback(GLFWwindow* window, int focused) {
        CCEGLView::onGLFWWindowFocusCallback(window, focused);

        // focused == 0 means the window just lost focus.
        if (focused != 0) return;
        if (!Mod::get()->getSettingValue<bool>("enabled")) return;

        if (Mod::get()->getSettingValue<bool>("verbose-logging"))
            log::info("[Better Saving] Window lost focus - saving.");

        createBackup(BACKUP_SAFE_EXIT);
        runSaveLogic(true);
    }
};

#elif defined(GEODE_IS_ANDROID)

#include <Geode/modify/AppDelegate.hpp>

class $modify(MyAppDelegate, AppDelegate) {
    void applicationDidEnterBackground() {
        AppDelegate::applicationDidEnterBackground();

        if (!Mod::get()->getSettingValue<bool>("enabled")) return;

        if (Mod::get()->getSettingValue<bool>("verbose-logging"))
            log::info("[Better Saving] App backgrounded - saving.");

        createBackup(BACKUP_SAFE_EXIT);
        runSaveLogic(true);
    }
};

#elif defined(GEODE_IS_IOS)

#include <Geode/modify/AppDelegate.hpp>

class $modify(MyAppDelegate, AppDelegate) {
    void applicationDidEnterBackground() {
        AppDelegate::applicationDidEnterBackground();

        if (!Mod::get()->getSettingValue<bool>("enabled")) return;

        if (Mod::get()->getSettingValue<bool>("verbose-logging"))
            log::info("[Better Saving] App backgrounded (iOS) - saving.");

        createBackup(BACKUP_SAFE_EXIT);
        runSaveLogic(true);
    }
};

#endif
