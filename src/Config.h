// palmkobo - Palm OS (CloudpilotEmu) front end for Kobo e-readers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Settings from palmkobo.ini (simple key = value lines, '#' or ';' comments).
// Relative paths are resolved against the directory containing the ini file.

#ifndef PALMKOBO_CONFIG_H
#define PALMKOBO_CONFIG_H

#include <optional>
#include <string>

namespace pk {

enum class Tristate { Auto, Off, On };

struct Config {
    std::string baseDir{"."};

    std::string rom{"m500.rom"};             // Palm ROM image (dumped from your own device)
    std::string device{"PalmM500"};          // CloudpilotEmu device id; empty = autodetect
    std::string session{"session.img"};      // saved state, restored on next launch
    std::string installDir{"install"};       // .prc/.pdb dropped here are installed on launch
    std::string silkscreen{"silkscreen.pgm"};
    std::string hotsyncName;                 // optional HotSync user name

    // Virtual SD card. Empty or "none" disables it.
    std::string card{"card.img"};
    int cardSizeMb{64};                      // size of a newly created card
    std::string cardImportDir{"to-card"};    // copied onto the card at launch
    std::string cardExportDir{"card-contents"};  // mirror of the card, written at exit

    int scale{0};  // 0 = largest that fits

    // Touch panel orientation; Auto takes FBInk's per-device quirks.
    Tristate touchSwapAxes{Tristate::Auto};
    Tristate touchMirrorX{Tristate::Auto};
    Tristate touchMirrorY{Tristate::Auto};
    bool grabInput{true};

    // Refresh tuning (see RefreshScheduler.h).
    std::string waveformFast{"DU"};
    std::string waveformQuality{"GL16"};
    std::string waveformFull{"GC16"};
    int penIntervalMs{40};
    int idleDelayMs{60};
    int maxLatencyMs{250};
    int fullRefreshEvery{60};

    int penThrottleMs{20};      // min interval between pen-move events sent to the Palm
    bool graffitiEcho{true};    // draw finger trails in the Graffiti area
    int installDelayMs{8000};   // wait for Palm OS to boot before installing apps
    int autosaveMinutes{5};     // save during a pause in input, at most this often (0 = off)
    int idleExitMinutes{15};    // save and exit after this long without input (0 = never)

    // Host test backend only.
    int hostWidth{1072}, hostHeight{1448};
    std::string hostSnapshot{"screen.pgm"};

    std::string resolve(const std::string& path) const;

    // Returns an error message on failure.
    std::optional<std::string> load(const std::string& path);
    std::optional<std::string> set(const std::string& key, const std::string& value);
};

}  // namespace pk

#endif
