// palmkobo - Palm OS (CloudpilotEmu) front end for Kobo e-readers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Converts the emulator's LCD frame (1/2/4 bpp grayscale, or 32-bit color) into
// scaled 8-bit grayscale on the canvas. Deliberately independent of the emulator
// headers: App.cpp copies the fields out of CloudpilotEmu's Frame into LcdFrame.

#ifndef PALMKOBO_FRAME_CONVERT_H
#define PALMKOBO_FRAME_CONVERT_H

#include <cstdint>

#include "Canvas.h"
#include "Geometry.h"

namespace pk {

struct LcdFrame {
    unsigned bpp{0};
    unsigned lineWidth{0};     // pixels per line (frame units)
    unsigned lines{0};         // number of lines (frame units)
    unsigned margin{0};        // pixels to skip at the start of each line
    unsigned bytesPerLine{0};
    unsigned firstDirtyLine{0};
    unsigned lastDirtyLine{0};
    unsigned scaleX{1}, scaleY{1};  // frame pixel -> Palm screen pixels
    bool dirtyRegionIsVertical{false};
    uint16_t lcd2bitMapping{0x0000};  // EmHAL::GetLCD2bitMapping(), used for 2 bpp
    const uint8_t* buffer{nullptr};
};

// Palm grayscale index (0 = lightest, 15 = darkest) -> e-ink gray.
// CloudpilotEmu tints index 0 to a greenish LCD gray; on paper white reads better.
inline uint8_t paletteGray16(unsigned index) {
    return static_cast<uint8_t>(0xff - (index & 0x0f) * 17);
}

// Unpack the n-th pixel of a packed row (MSB first, like CloudpilotEmu's Nibbler).
inline unsigned unpackPixel(const uint8_t* row, unsigned bpp, unsigned index) {
    const unsigned bit = index * bpp;
    const uint8_t byte = row[bit / 8];
    const unsigned shift = 8 - bpp - (bit % 8);
    return (byte >> shift) & ((1u << bpp) - 1);
}

inline uint8_t lumaRGB(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint8_t>((77u * r + 150u * g + 29u * b) >> 8);
}

// Draws the dirty lines of the frame into the LCD area of the canvas.
// Returns the panel rectangle that changed (empty if nothing was drawn).
inline Rect convertFrame(const LcdFrame& f, Canvas& canvas, const Layout& layout) {
    if (!f.buffer || f.lineWidth == 0 || f.lines == 0 || layout.scale <= 0) return {};
    if (f.lineWidth * f.scaleX != static_cast<unsigned>(layout.palmW) ||
        f.lines * f.scaleY != static_cast<unsigned>(layout.palmH))
        return {};  // geometry mismatch (e.g. mid-mode-switch); skip like CloudpilotEmu does

    unsigned first = f.firstDirtyLine, last = f.lastDirtyLine;
    if (f.dirtyRegionIsVertical || last >= f.lines || first > last) {
        // Vertical dirty regions (rotated layouts) describe columns; just redraw it all.
        first = 0;
        last = f.lines - 1;
    }

    uint8_t pal2[4];
    for (unsigned i = 0; i < 4; i++) pal2[i] = paletteGray16((f.lcd2bitMapping >> (4 * i)) & 0x0f);

    const int px = layout.scale * static_cast<int>(f.scaleX);  // panel px per frame px
    const int py = layout.scale * static_cast<int>(f.scaleY);
    const Rect clip = layout.lcd.intersected(canvas.bounds());

    for (unsigned y = first; y <= last; y++) {
        const uint8_t* src = f.buffer + static_cast<size_t>(y) * f.bytesPerLine;
        const int ty = layout.lcd.y + static_cast<int>(y) * py;

        for (unsigned x = 0; x < f.lineWidth; x++) {
            uint8_t v;
            switch (f.bpp) {
                case 1:
                    v = unpackPixel(src, 1, f.margin + x) ? BLACK : WHITE;
                    break;
                case 2:
                    v = pal2[unpackPixel(src, 2, f.margin + x)];
                    break;
                case 4:
                    v = paletteGray16(unpackPixel(src, 4, f.margin + x));
                    break;
                case 24: {
                    // 4 bytes per pixel, R G B A in memory (SDL ABGR8888 on little endian).
                    const uint8_t* p = src + 4 * (f.margin + x);
                    v = lumaRGB(p[0], p[1], p[2]);
                } break;
                default:
                    return {};
            }

            const int tx = layout.lcd.x + static_cast<int>(x) * px;
            for (int yy = 0; yy < py; yy++) {
                const int cy = ty + yy;
                if (cy < clip.y || cy >= clip.bottom()) continue;
                uint8_t* out = canvas.row(cy);
                for (int xx = 0; xx < px; xx++) {
                    const int cx = tx + xx;
                    if (cx >= clip.x && cx < clip.right()) out[cx] = v;
                }
            }
        }
    }

    return Rect{layout.lcd.x, layout.lcd.y + static_cast<int>(first) * py, layout.lcd.w,
                static_cast<int>(last - first + 1) * py}
        .intersected(clip);
}

}  // namespace pk

#endif
