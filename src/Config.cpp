// palmkobo - Palm OS (CloudpilotEmu) front end for Kobo e-readers
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Config.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace pk {

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::optional<bool> parseBool(const std::string& v) {
    const std::string l = lower(v);
    if (l == "1" || l == "true" || l == "yes" || l == "on") return true;
    if (l == "0" || l == "false" || l == "no" || l == "off") return false;
    return std::nullopt;
}

std::optional<Tristate> parseTristate(const std::string& v) {
    if (lower(v) == "auto") return Tristate::Auto;
    if (auto b = parseBool(v)) return *b ? Tristate::On : Tristate::Off;
    return std::nullopt;
}

std::optional<int> parseInt(const std::string& v) {
    try {
        size_t used = 0;
        const int n = std::stoi(v, &used);
        if (used != v.size()) return std::nullopt;
        return n;
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace

std::string Config::resolve(const std::string& path) const {
    if (path.empty() || path[0] == '/') return path;
    return baseDir + "/" + path;
}

std::optional<std::string> Config::set(const std::string& key, const std::string& value) {
    auto bad = [&]() { return std::optional<std::string>("invalid value for " + key + ": " + value); };
    auto setInt = [&](int& dst, int lo, int hi) -> std::optional<std::string> {
        auto n = parseInt(value);
        if (!n || *n < lo || *n > hi) return bad();
        dst = *n;
        return std::nullopt;
    };
    auto setBool = [&](bool& dst) -> std::optional<std::string> {
        auto b = parseBool(value);
        if (!b) return bad();
        dst = *b;
        return std::nullopt;
    };
    auto setTri = [&](Tristate& dst) -> std::optional<std::string> {
        auto t = parseTristate(value);
        if (!t) return bad();
        dst = *t;
        return std::nullopt;
    };

    if (key == "rom") rom = value;
    else if (key == "device") device = value;
    else if (key == "session") session = value;
    else if (key == "install_dir") installDir = value;
    else if (key == "silkscreen") silkscreen = value;
    else if (key == "hotsync_name") hotsyncName = value;
    else if (key == "card") card = (value == "none" ? std::string() : value);
    else if (key == "card_size_mb") {
        auto n = parseInt(value);
        if (!n || (*n != 4 && *n != 8 && *n != 16 && *n != 32 && *n != 64 && *n != 128))
            return std::optional<std::string>("card_size_mb must be 4, 8, 16, 32, 64 or 128");
        cardSizeMb = *n;
    }
    else if (key == "card_import_dir") cardImportDir = value;
    else if (key == "card_export_dir") cardExportDir = value;
    else if (key == "scale") return setInt(scale, 0, 32);
    else if (key == "touch_swap_axes") return setTri(touchSwapAxes);
    else if (key == "touch_mirror_x") return setTri(touchMirrorX);
    else if (key == "touch_mirror_y") return setTri(touchMirrorY);
    else if (key == "grab_input") return setBool(grabInput);
    else if (key == "waveform_fast") waveformFast = value;
    else if (key == "waveform_quality") waveformQuality = value;
    else if (key == "waveform_full") waveformFull = value;
    else if (key == "pen_interval_ms") return setInt(penIntervalMs, 0, 5000);
    else if (key == "idle_delay_ms") return setInt(idleDelayMs, 0, 5000);
    else if (key == "max_latency_ms") return setInt(maxLatencyMs, 0, 10000);
    else if (key == "full_refresh_every") return setInt(fullRefreshEvery, 1, 100000);
    else if (key == "pen_throttle_ms") return setInt(penThrottleMs, 0, 1000);
    else if (key == "graffiti_echo") return setBool(graffitiEcho);
    else if (key == "install_delay_ms") return setInt(installDelayMs, 0, 600000);
    else if (key == "autosave_minutes") return setInt(autosaveMinutes, 0, 1440);
    else if (key == "idle_exit_minutes") return setInt(idleExitMinutes, 0, 1440);
    else if (key == "host_width") return setInt(hostWidth, 16, 10000);
    else if (key == "host_height") return setInt(hostHeight, 16, 10000);
    else if (key == "host_snapshot") hostSnapshot = value;
    else return std::optional<std::string>("unknown setting: " + key);

    return std::nullopt;
}

std::optional<std::string> Config::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) return "cannot open " + path;

    const size_t slash = path.find_last_of('/');
    baseDir = slash == std::string::npos ? "." : (slash == 0 ? "/" : path.substr(0, slash));

    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        lineNo++;
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';' || t[0] == '[') continue;
        const size_t eq = t.find('=');
        if (eq == std::string::npos)
            return path + ":" + std::to_string(lineNo) + ": expected key = value";
        if (auto err = set(lower(trim(t.substr(0, eq))), trim(t.substr(eq + 1))))
            return path + ":" + std::to_string(lineNo) + ": " + *err;
    }
    return std::nullopt;
}

}  // namespace pk
