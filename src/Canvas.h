// palmkobo - Palm OS (CloudpilotEmu) front end for Kobo e-readers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// An 8-bit grayscale off-screen canvas the size of the panel. Everything is
// composed here first, then dirty regions are pushed to the e-ink display.

#ifndef PALMKOBO_CANVAS_H
#define PALMKOBO_CANVAS_H

#include <cstdint>
#include <cstdlib>
#include <vector>

#include "Geometry.h"

namespace pk {

constexpr uint8_t WHITE = 0xff;
constexpr uint8_t BLACK = 0x00;

class Canvas {
   public:
    Canvas() = default;
    Canvas(int w, int h, uint8_t fill = WHITE) { resize(w, h, fill); }

    void resize(int w, int h, uint8_t fill = WHITE) {
        width = w;
        height = h;
        pixels.assign(static_cast<size_t>(w) * h, fill);
    }

    int w() const { return width; }
    int h() const { return height; }
    Rect bounds() const { return {0, 0, width, height}; }

    uint8_t* row(int y) { return pixels.data() + static_cast<size_t>(y) * width; }
    const uint8_t* row(int y) const { return pixels.data() + static_cast<size_t>(y) * width; }
    uint8_t at(int x, int y) const { return row(y)[x]; }

    void fill(Rect r, uint8_t v) {
        r = r.intersected(bounds());
        for (int y = r.y; y < r.bottom(); y++) {
            uint8_t* p = row(y) + r.x;
            for (int x = 0; x < r.w; x++) p[x] = v;
        }
    }

    void frame(Rect r, int thickness, uint8_t v) {
        fill({r.x, r.y, r.w, thickness}, v);
        fill({r.x, r.bottom() - thickness, r.w, thickness}, v);
        fill({r.x, r.y, thickness, r.h}, v);
        fill({r.right() - thickness, r.y, thickness, r.h}, v);
    }

    void invert(Rect r) {
        r = r.intersected(bounds());
        for (int y = r.y; y < r.bottom(); y++) {
            uint8_t* p = row(y) + r.x;
            for (int x = 0; x < r.w; x++) p[x] = static_cast<uint8_t>(0xff - p[x]);
        }
    }

    // Square "brush" stamped along a line (Bresenham). Used for Graffiti ink echo.
    void line(int x0, int y0, int x1, int y1, int thickness, uint8_t v, Rect clip) {
        const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        const int half = thickness / 2;
        for (;;) {
            fill(Rect{x0 - half, y0 - half, thickness, thickness}.intersected(clip), v);
            if (x0 == x1 && y0 == y1) break;
            const int e2 = 2 * err;
            if (e2 >= dy) {
                err += dy;
                x0 += sx;
            }
            if (e2 <= dx) {
                err += dx;
                y0 += sy;
            }
        }
    }

    // Area-averaging resample of a grayscale image into dst.
    void drawResampled(const uint8_t* src, int sw, int sh, Rect dst) {
        if (sw <= 0 || sh <= 0 || dst.empty()) return;
        for (int y = 0; y < dst.h; y++) {
            const int ty = dst.y + y;
            if (ty < 0 || ty >= height) continue;
            const int sy0 = y * sh / dst.h;
            const int sy1 = std::max(sy0 + 1, (y + 1) * sh / dst.h);
            for (int x = 0; x < dst.w; x++) {
                const int tx = dst.x + x;
                if (tx < 0 || tx >= width) continue;
                const int sx0 = x * sw / dst.w;
                const int sx1 = std::max(sx0 + 1, (x + 1) * sw / dst.w);
                unsigned sum = 0, n = 0;
                for (int yy = sy0; yy < sy1; yy++)
                    for (int xx = sx0; xx < sx1; xx++) {
                        sum += src[static_cast<size_t>(yy) * sw + xx];
                        n++;
                    }
                row(ty)[tx] = static_cast<uint8_t>(sum / n);
            }
        }
    }

    // Copy a rectangle out as a tightly packed buffer (for the display backend).
    void extract(Rect r, std::vector<uint8_t>& out) const {
        r = r.intersected(bounds());
        out.resize(static_cast<size_t>(r.w) * r.h);
        for (int y = 0; y < r.h; y++) {
            const uint8_t* s = row(r.y + y) + r.x;
            std::copy(s, s + r.w, out.begin() + static_cast<size_t>(y) * r.w);
        }
    }

   private:
    int width{0}, height{0};
    std::vector<uint8_t> pixels;
};

}  // namespace pk

#endif
