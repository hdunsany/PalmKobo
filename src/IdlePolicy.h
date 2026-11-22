// palmkobo - Palm OS (CloudpilotEmu) front end for Kobo e-readers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// When to autosave and when to give up and exit after inactivity.
//
//  * Autosave: at most every autosaveMs, only if there was input since the last
//    save, and only once input has paused for quietMs - so saving (which stalls
//    the emulator for a moment) never interrupts typing or a pen stroke.
//  * Idle exit: after exitMs without any touch or key input, palmkobo saves and
//    exits so the Kobo can go back to sleeping normally. warnMs before that, a
//    warning is shown; any input dismisses it and restarts the clock.
//
// Pure logic, unit-tested on the host.

#ifndef PALMKOBO_IDLE_POLICY_H
#define PALMKOBO_IDLE_POLICY_H

#include <algorithm>
#include <cstdint>

namespace pk {

class IdlePolicy {
   public:
    struct Params {
        uint64_t autosaveMs{5 * 60000};  // 0 = no autosave
        uint64_t quietMs{10000};
        uint64_t exitMs{15 * 60000};     // 0 = never exit on idle
        uint64_t warnMs{60000};
    };

    enum class Action { None, Autosave, ShowWarning, HideWarning, Exit };

    IdlePolicy(Params p, uint64_t now) : params(p), lastInput(now), lastSave(now) {}

    // Call for every touch or key event.
    void input(uint64_t now) {
        lastInput = now;
        dirty = true;
    }

    // A save happened (autosave or otherwise).
    void saved(uint64_t now) {
        lastSave = now;
        dirty = false;
    }

    // Call regularly; returns at most one thing to do now.
    Action poll(uint64_t now, bool busy) {
        const uint64_t idle = now - lastInput;

        if (warning && idle < warnStart()) {
            warning = false;
            return Action::HideWarning;
        }

        if (params.exitMs > 0) {
            if (idle >= params.exitMs) return Action::Exit;
            if (!warning && idle >= warnStart()) {
                warning = true;
                return Action::ShowWarning;
            }
        }

        if (params.autosaveMs > 0 && dirty && !busy && idle >= params.quietMs &&
            now - lastSave >= params.autosaveMs)
            return Action::Autosave;

        return Action::None;
    }

    bool warningShown() const { return warning; }

    // Milliseconds until poll() may return something new (for poll timeouts).
    uint64_t msUntilNext(uint64_t now) const {
        uint64_t t = UINT64_MAX;
        const uint64_t idle = now - lastInput;
        auto until = [&](uint64_t at) { return at > idle ? at - idle : 0; };
        if (params.exitMs > 0) t = std::min<uint64_t>(t, until(warning ? params.exitMs : warnStart()));
        if (params.autosaveMs > 0 && dirty) {
            const uint64_t sinceSave = now - lastSave;
            const uint64_t saveWait = sinceSave >= params.autosaveMs ? 0 : params.autosaveMs - sinceSave;
            t = std::min<uint64_t>(t, std::max<uint64_t>(saveWait, until(params.quietMs)));
        }
        return t;
    }

   private:
    uint64_t warnStart() const {
        return params.exitMs > params.warnMs ? params.exitMs - params.warnMs : 0;
    }

    Params params;
    uint64_t lastInput;
    uint64_t lastSave;
    bool dirty{false};
    bool warning{false};
};

}  // namespace pk

#endif
