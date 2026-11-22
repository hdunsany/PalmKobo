// palmkobo - Palm OS (CloudpilotEmu) front end for Kobo e-readers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Decides when, where and how to refresh the e-ink panel. This is the part that
// makes or breaks the feel on e-ink, so it is pure logic and unit-tested.
//
// Strategy:
//  * While the finger is down, flush dirty areas quickly (every penInterval ms)
//    with a fast black/white waveform (DU). Remember those areas: DU leaves grays
//    wrong and ghosts.
//  * Once the finger lifts and the screen has been quiet for idleDelay ms (or a
//    pending change has waited maxLatency ms), redraw everything pending plus all
//    fast-refreshed areas with a quality grayscale waveform (GL16).
//  * After fullAfterPartials quality refreshes, do one flashing full-screen
//    refresh at the next quiet moment to clear accumulated ghosting.

#ifndef PALMKOBO_REFRESH_SCHEDULER_H
#define PALMKOBO_REFRESH_SCHEDULER_H

#include <cstdint>
#include <optional>

#include "Geometry.h"

namespace pk {

enum class Waveform { Fast, Quality, Full };

struct Flush {
    Rect rect;
    Waveform waveform;
};

class RefreshScheduler {
   public:
    struct Params {
        uint64_t penInterval{40};
        uint64_t idleDelay{60};
        uint64_t maxLatency{250};
        int fullAfterPartials{60};
        uint64_t fullIdleDelay{1500};
    };

    explicit RefreshScheduler(Rect screen) : screen(screen) {}
    RefreshScheduler(Rect screen, Params p) : screen(screen), params(p) {}

    void addDirty(const Rect& r, uint64_t now) {
        if (r.empty()) return;
        if (pending.empty()) pendingSince = now;
        pending = pending.united(r);
        lastActivity = now;
    }

    void setPenDown(bool down, uint64_t now) {
        penDown = down;
        lastActivity = now;
    }

    void requestFull() { fullRequested = true; }

    std::optional<Flush> poll(uint64_t now) {
        if (penDown) {
            if (!pending.empty() && now - lastFlush >= params.penInterval) {
                Flush f{pending, Waveform::Fast};
                fastDebt = fastDebt.united(pending);
                pending = {};
                lastFlush = now;
                return f;
            }
            return std::nullopt;
        }

        const Rect cleanup = pending.united(fastDebt);
        if (!cleanup.empty()) {
            const bool quiet = now - lastActivity >= params.idleDelay;
            const bool overdue = !pending.empty() && now - pendingSince >= params.maxLatency;
            if (quiet || overdue) {
                pending = {};
                fastDebt = {};
                lastFlush = now;
                partials++;
                return Flush{cleanup, Waveform::Quality};
            }
            return std::nullopt;
        }

        const bool wantFull = fullRequested || partials >= params.fullAfterPartials;
        if (wantFull && (fullRequested || now - lastActivity >= params.fullIdleDelay)) {
            fullRequested = false;
            partials = 0;
            lastFlush = now;
            return Flush{screen, Waveform::Full};
        }
        return std::nullopt;
    }

    // Milliseconds until poll() might have something to do (for the poll() timeout).
    uint64_t msUntilNextAction(uint64_t now) const {
        auto remaining = [now](uint64_t since, uint64_t delay) -> uint64_t {
            return now - since >= delay ? 0 : delay - (now - since);
        };
        if (penDown) return pending.empty() ? UINT64_MAX : remaining(lastFlush, params.penInterval);
        if (!pending.united(fastDebt).empty()) {
            uint64_t t = remaining(lastActivity, params.idleDelay);
            if (!pending.empty()) t = std::min(t, remaining(pendingSince, params.maxLatency));
            return t;
        }
        if (fullRequested) return 0;
        if (partials >= params.fullAfterPartials) return remaining(lastActivity, params.fullIdleDelay);
        return UINT64_MAX;
    }

    bool idle() const { return pending.empty() && fastDebt.empty() && !fullRequested; }

   private:
    Rect screen;
    Params params;
    Rect pending, fastDebt;
    uint64_t pendingSince{0}, lastActivity{0}, lastFlush{0};
    bool penDown{false};
    bool fullRequested{false};
    int partials{0};
};

}  // namespace pk

#endif
