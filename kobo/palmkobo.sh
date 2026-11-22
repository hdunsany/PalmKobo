#!/bin/sh
# palmkobo launcher - started from NickelMenu.
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Pauses Nickel (Kobo's reader UI) and its watchdog helpers while palmkobo owns the
# screen and touch panel, then resumes them. palmkobo puts Nickel's last screen
# back when it exits, so you land where you left off.

PK_DIR="$(dirname "$(readlink -f "$0")")"
cd "$PK_DIR" || exit 1

LOG="$PK_DIR/palmkobo.log"
if [ -f "$LOG" ] && [ "$(wc -c < "$LOG")" -gt 262144 ]; then
    mv -f "$LOG" "$LOG.old"
fi
echo "=== palmkobo $(date) $* ===" >> "$LOG"

# Refuse to start twice (e.g. double-tapped menu entry).
if pidof palmkobo >/dev/null 2>&1; then
    echo "already running" >> "$LOG"
    exit 0
fi

resume_nickel() {
    # Reverse order: helpers first, Nickel last.
    for p in hindenburg sickel nickel; do
        killall -CONT "$p" 2>/dev/null
    done
}
trap resume_nickel EXIT INT TERM

# sickel watches Nickel and would treat a paused Nickel as hung, so pause it too.
for p in nickel sickel hindenburg; do
    killall -STOP "$p" 2>/dev/null
done

./palmkobo -c "$PK_DIR/palmkobo.ini" "$@" >> "$LOG" 2>&1
echo "exit status $?" >> "$LOG"
