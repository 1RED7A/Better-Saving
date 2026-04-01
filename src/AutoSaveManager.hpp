#pragma once

#include <Geode/Geode.hpp>
#include <chrono>

using namespace geode::prelude;

// -----------------------------------------------------------------------
// AutoSaveManager
//
// A retained CCNode singleton that owns the Cocos scheduler entry for
// the auto-save timer. Keeping it as a CCNode lets us use
// schedule_selector without needing a scene layer.
//
// Members are public so that SaveLogic.cpp can update timestamps without
// an extra layer of getters/setters.
// -----------------------------------------------------------------------
class AutoSaveManager : public CCNode {
public:
    // Accumulated seconds since the last timer fire.
    float m_accumulator  = 0.0f;

    // True when a save was requested while the player was inside a level.
    bool  m_pendingSave  = false;
    bool  m_timerRunning = false;

    // When the pending save was first deferred - used for timeout logic.
    std::chrono::steady_clock::time_point m_pendingTimestamp;

    // When we last successfully saved - used for cooldown enforcement.
    std::chrono::steady_clock::time_point m_lastSaveTimestamp;

    // Small HUD sprite shown when a save is pending and defer-behavior == hud-icon.
    CCSprite* m_pendingIcon = nullptr;

    static AutoSaveManager* get();

    // --- Timer control ---
    void scheduleTimer();
    void stopTimer();

    // --- Pending icon ---
    void addPendingIcon();
    void removePendingIcon();

    // --- Called each timer tick and from hooks ---
    void triggerSaveAttempt();
    void checkPending();

private:
    void updateTimer(float dt);
};
