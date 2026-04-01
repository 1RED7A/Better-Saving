#include "AutoSaveManager.hpp"
#include "SaveLogic.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/FLAlertLayer.hpp>

using namespace geode::prelude;

// -----------------------------------------------------------------------
// Singleton
// -----------------------------------------------------------------------

AutoSaveManager* AutoSaveManager::get() {
    static AutoSaveManager* s_instance = nullptr;
    if (!s_instance) {
        s_instance = new AutoSaveManager();
        s_instance->init();
        s_instance->retain();
        CCDirector::get()->getScheduler()->resumeTarget(s_instance);

        // Start the cooldown clock far in the past so the very first manual
        // save isn't blocked by the cooldown check.
        s_instance->m_lastSaveTimestamp =
            std::chrono::steady_clock::now() - std::chrono::seconds(3600);
    }
    return s_instance;
}

// -----------------------------------------------------------------------
// Timer control
// -----------------------------------------------------------------------

void AutoSaveManager::scheduleTimer() {
    if (m_timerRunning) {
        if (Mod::get()->getSettingValue<bool>("verbose-logging"))
            log::info("[Better Saving] Timer already running - skipping reschedule.");
        return;
    }

    CCDirector::get()->getScheduler()->scheduleSelector(
        schedule_selector(AutoSaveManager::updateTimer), this, 1.0f, false
    );
    m_accumulator  = 0.0f;
    m_timerRunning = true;

    if (Mod::get()->getSettingValue<bool>("verbose-logging"))
        log::info("[Better Saving] Auto-save timer started.");
}

void AutoSaveManager::stopTimer() {
    CCDirector::get()->getScheduler()->unscheduleSelector(
        schedule_selector(AutoSaveManager::updateTimer), this
    );
    m_pendingSave  = false;
    m_accumulator  = 0.0f;
    m_timerRunning = false;

    if (Mod::get()->getSettingValue<bool>("verbose-logging"))
        log::info("[Better Saving] Auto-save timer stopped.");
}

// -----------------------------------------------------------------------
// Pending save icon (shown in-level when defer-behavior == "hud-icon")
// -----------------------------------------------------------------------

void AutoSaveManager::addPendingIcon() {
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

void AutoSaveManager::removePendingIcon() {
    if (!m_pendingIcon) return;
    m_pendingIcon->removeFromParentAndCleanup(true);
    m_pendingIcon = nullptr;
}

// -----------------------------------------------------------------------
// Save attempt logic
// -----------------------------------------------------------------------

void AutoSaveManager::checkPending() {
    if (!m_pendingSave) return;
    // Only flush the pending save once we're back on the menu.
    if (PlayLayer::get() == nullptr)
        triggerSaveAttempt();
}

void AutoSaveManager::triggerSaveAttempt() {
    bool inLevel      = PlayLayer::get() != nullptr;
    bool inEditor     = LevelEditorLayer::get() != nullptr;
    bool saveInEditor = Mod::get()->getSettingValue<bool>("save-in-editor");

    // Avoid saving if a blocking popup is open - it can cause visual glitches.
    bool modalOpen = false;
    if (auto* scene = CCDirector::get()->getRunningScene())
        if (scene->getChildByType<FLAlertLayer>(0)) modalOpen = true;

    if (inLevel || modalOpen || (inEditor && !saveInEditor)) {
        if (!m_pendingSave) {
            m_pendingSave      = true;
            m_pendingTimestamp = std::chrono::steady_clock::now();

            if (Mod::get()->getSettingValue<bool>("verbose-logging"))
                log::info("[Better Saving] Save deferred (inLevel={} inEditor={} modal={}).",
                    inLevel, inEditor, modalOpen);

            if (Mod::get()->getSettingValue<std::string>("defer-behavior") == "hud-icon" && inLevel)
                addPendingIcon();
        }
        return;
    }

    runSaveLogic();
}

// -----------------------------------------------------------------------
// Scheduler callback (fires every 1 second)
// -----------------------------------------------------------------------

void AutoSaveManager::updateTimer(float dt) {
    if (!Mod::get()->getSettingValue<bool>("enabled")) return;
    if (!Mod::get()->getSettingValue<bool>("auto-save-enabled")) return;

    // If there's already a pending save, keep trying to flush it instead
    // of accumulating towards a new one.
    if (m_pendingSave) {
        auto now     = std::chrono::steady_clock::now();
        auto ageMins = std::chrono::duration_cast<std::chrono::minutes>(
            now - m_pendingTimestamp
        ).count();
        auto timeoutMins = Mod::get()->getSettingValue<int64_t>("pending-save-timeout");

        if (ageMins > timeoutMins) {
            m_pendingSave = false;
            log::warn("[Better Saving] Pending save expired after {} min - discarding.", (int)ageMins);
            removePendingIcon();
            return;
        }

        triggerSaveAttempt();
        return;
    }

    m_accumulator += dt;
    float intervalSecs = static_cast<float>(
        Mod::get()->getSettingValue<int64_t>("save-interval") * 60
    );

    if (m_accumulator >= intervalSecs) {
        m_accumulator = 0.0f;
        triggerSaveAttempt();
    }
}
