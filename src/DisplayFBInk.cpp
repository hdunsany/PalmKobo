// palmkobo - Palm OS (CloudpilotEmu) front end for Kobo e-readers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// FBInk backend (https://github.com/NiLuJe/FBInk). Needs a full (non-MINIMAL)
// libfbink build: we use raw image drawing and framebuffer dumps.

#ifdef PK_WITH_FBINK

#include <cstdio>
#include <cstring>
#include <vector>

#include "Display.h"
#include "fbink.h"

namespace pk {

namespace {

WFM_MODE_INDEX_T parseWaveform(const std::string& name, WFM_MODE_INDEX_T fallback) {
    struct Entry {
        const char* name;
        WFM_MODE_INDEX_T mode;
    };
    static const Entry table[] = {
        {"AUTO", WFM_AUTO},         {"DU", WFM_DU},     {"A2", WFM_A2},
        {"GC4", WFM_GC4},           {"DU4", WFM_DU4},   {"GL16", WFM_GL16},
        {"GC16", WFM_GC16},         {"REAGL", WFM_REAGL}, {"REAGLD", WFM_REAGLD},
        {"GL16_FAST", WFM_GL16_FAST}, {"GC16_FAST", WFM_GC16_FAST},
    };
    for (const auto& e : table)
        if (name == e.name) return e.mode;
    std::fprintf(stderr, "palmkobo: unknown waveform '%s', using default\n", name.c_str());
    return fallback;
}

class DisplayFBInk final : public Display {
   public:
    explicit DisplayFBInk(const Config& config) {
        wfmFast = parseWaveform(config.waveformFast, WFM_DU);
        wfmQuality = parseWaveform(config.waveformQuality, WFM_GL16);
        wfmFull = parseWaveform(config.waveformFull, WFM_GC16);
    }

    ~DisplayFBInk() override { close(); }

    std::string open() override {
        fbfd = fbink_open();
        if (fbfd < 0) return "fbink_open failed (no framebuffer?)";

        std::memset(&cfg, 0, sizeof(cfg));
        cfg.is_quiet = true;
        cfg.ignore_alpha = true;  // we hand over 8-bit gray (Y8)

        if (fbink_init(fbfd, &cfg) < 0) {
            fbink_close(fbfd);
            fbfd = -1;
            return "fbink_init failed";
        }

        std::memset(&state, 0, sizeof(state));
        fbink_get_state(&cfg, &state);

        // Snapshot whatever Nickel was showing, so we can put it back on exit.
        std::memset(&dump, 0, sizeof(dump));
        haveDump = fbink_dump(fbfd, &dump) >= 0;
        return "";
    }

    void close() override {
        if (fbfd < 0) return;
        if (haveDump) {
            FBInkConfig c = cfg;
            c.is_flashing = true;
            c.wfm_mode = WFM_GC16;
            fbink_restore(fbfd, &c, &dump);
            fbink_free_dump_data(&dump);
            haveDump = false;
        }
        fbink_close(fbfd);
        fbfd = -1;
    }

    // The drawable area. On a few older Kobos this "viewport" is smaller than the
    // panel because the bezel hides some rows; on the Clara BW they are identical.
    int width() const override { return static_cast<int>(state.view_width); }
    int height() const override { return static_cast<int>(state.view_height); }

    void present(const Canvas& canvas, const Rect& rect, Waveform waveform) override {
        const Rect r = rect.intersected(canvas.bounds());
        if (r.empty() || fbfd < 0) return;

        canvas.extract(r, scratch);

        FBInkConfig c = cfg;
        switch (waveform) {
            case Waveform::Fast:
                c.wfm_mode = wfmFast;
                break;
            case Waveform::Quality:
                c.wfm_mode = wfmQuality;
                break;
            case Waveform::Full:
                c.wfm_mode = wfmFull;
                c.is_flashing = true;
                break;
        }

        // Draws and refreshes exactly the region we hand over.
        if (fbink_print_raw_data(fbfd, scratch.data(), r.w, r.h, scratch.size(),
                                 static_cast<short>(r.x), static_cast<short>(r.y), &c) < 0)
            std::fprintf(stderr, "palmkobo: fbink_print_raw_data failed\n");
    }

    TouchTransform touchDefaults() const override {
        return {state.touch_swap_axes, state.touch_mirror_x, state.touch_mirror_y};
    }

    std::string describe() const override {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "FBInk %s on %s (%s), %ux%u @ %ubpp, touch swap=%d mx=%d my=%d",
                      fbink_version(), state.device_name, state.device_codename,
                      state.screen_width, state.screen_height, state.bpp, state.touch_swap_axes,
                      state.touch_mirror_x, state.touch_mirror_y);
        return buf;
    }

   private:
    int fbfd{-1};
    FBInkConfig cfg{};
    FBInkState state{};
    FBInkDump dump{};
    bool haveDump{false};
    WFM_MODE_INDEX_T wfmFast{WFM_DU}, wfmQuality{WFM_GL16}, wfmFull{WFM_GC16};
    std::vector<uint8_t> scratch;
};

}  // namespace

std::unique_ptr<Display> makeDisplay(const Config& config) {
    return std::make_unique<DisplayFBInk>(config);
}

}  // namespace pk

#endif  // PK_WITH_FBINK
