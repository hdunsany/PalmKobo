// palmkobo - Palm OS (CloudpilotEmu) front end for Kobo e-readers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host (desktop) backend for development and tests: keeps an in-memory copy of
// the "panel", counts refreshes by waveform, and writes a PGM snapshot on close.

#ifndef PK_WITH_FBINK

#include <cstdio>

#include "Display.h"

namespace pk {

namespace {

class DisplayHost final : public Display {
   public:
    explicit DisplayHost(const Config& config)
        : w(config.hostWidth), h(config.hostHeight), snapshot(config.resolve(config.hostSnapshot)) {}

    ~DisplayHost() override { close(); }

    std::string open() override {
        panel.resize(w, h, 0x80);  // mid gray, so undrawn areas are obvious in snapshots
        isOpen = true;
        return "";
    }

    void close() override {
        if (!isOpen) return;
        isOpen = false;
        if (snapshot.empty()) return;
        if (FILE* f = std::fopen(snapshot.c_str(), "wb")) {
            std::fprintf(f, "P5\n%d %d\n255\n", w, h);
            for (int y = 0; y < h; y++) std::fwrite(panel.row(y), 1, w, f);
            std::fclose(f);
            std::printf("host display: wrote %s (refreshes fast=%d quality=%d full=%d)\n",
                        snapshot.c_str(), counts[0], counts[1], counts[2]);
        }
    }

    int width() const override { return w; }
    int height() const override { return h; }

    void present(const Canvas& canvas, const Rect& rect, Waveform waveform) override {
        const Rect r = rect.intersected(canvas.bounds()).intersected(panel.bounds());
        for (int y = r.y; y < r.bottom(); y++)
            std::copy(canvas.row(y) + r.x, canvas.row(y) + r.right(), panel.row(y) + r.x);
        counts[static_cast<int>(waveform)]++;
    }

    std::string describe() const override {
        return "host display " + std::to_string(w) + "x" + std::to_string(h);
    }

   private:
    int w, h;
    std::string snapshot;
    Canvas panel;
    bool isOpen{false};
    int counts[3]{0, 0, 0};
};

}  // namespace

std::unique_ptr<Display> makeDisplay(const Config& config) {
    return std::make_unique<DisplayHost>(config);
}

}  // namespace pk

#endif  // !PK_WITH_FBINK
