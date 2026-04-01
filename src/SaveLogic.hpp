#pragma once

// -----------------------------------------------------------------------
// Core save entry point.
//
// force=true  - bypass the cooldown check (used by force-save button,
//               level-complete hook, and focus-loss handler).
// force=false - normal timer-driven save, respects the cooldown setting.
// -----------------------------------------------------------------------
void runSaveLogic(bool force = false);
