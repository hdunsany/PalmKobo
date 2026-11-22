// Renders a mock of the palmkobo screen (layout, silkscreen, button bar, a synthetic
// 4bpp LCD frame and a Graffiti stroke) to a PGM, using the real drawing code.
// Development aid only: build with `make preview`.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdio>
#include <vector>

#include "ButtonBar.h"
#include "Canvas.h"
#include "FrameConvert.h"
#include "Geometry.h"
#include "Pgm.h"

using namespace pk;

int main(int argc, char** argv) {
    const char* silkPath = argc > 1 ? argv[1] : "assets/silkscreen.pgm";
    const char* out = argc > 2 ? argv[2] : "preview.pgm";

    Canvas c(1072, 1448, WHITE);
    const Layout l = Layout::compute(c.w(), c.h(), 160, 160, 60);
    c.frame({l.palm.x - 3, l.palm.y - 3, l.palm.w + 6, l.palm.h + 6}, 2, BLACK);

    const GrayImage art = loadPgm(silkPath);
    if (art.valid()) c.drawResampled(art.pixels.data(), art.w, art.h, l.silk);

    ButtonBar bar;
    bar.layout(l.bar);
    bar.at(1).pressed = true;  // show a pressed button
    bar.draw(c);

    // Synthetic 160x160 4bpp frame: title bar, 16-step gray ramp, a checker.
    std::vector<uint8_t> fb(80 * 160, 0x00);
    auto put = [&](int x, int y, int v) {
        uint8_t& b = fb[y * 80 + x / 2];
        b = (x & 1) ? static_cast<uint8_t>((b & 0xf0) | v) : static_cast<uint8_t>((b & 0x0f) | (v << 4));
    };
    for (int y = 0; y < 14; y++)
        for (int x = 0; x < 64; x++) put(x, y, 15);
    for (int y = 40; y < 70; y++)
        for (int x = 0; x < 160; x++) put(x, y, x / 10);
    for (int y = 90; y < 150; y++)
        for (int x = 20; x < 140; x++) put(x, y, ((x / 8 + y / 8) & 1) ? 15 : 0);

    LcdFrame f;
    f.bpp = 4;
    f.lineWidth = 160;
    f.lines = 160;
    f.bytesPerLine = 80;
    f.firstDirtyLine = 0;
    f.lastDirtyLine = 159;
    f.buffer = fb.data();
    convertFrame(f, c, l);

    // A Graffiti-ish stroke in the letter area.
    const int s = l.scale;
    c.line(l.silk.x + 50 * s, l.silk.y + 45 * s, l.silk.x + 60 * s, l.silk.y + 12 * s, s, BLACK, l.silk);
    c.line(l.silk.x + 60 * s, l.silk.y + 12 * s, l.silk.x + 70 * s, l.silk.y + 45 * s, s, BLACK, l.silk);

    if (FILE* fp = std::fopen(out, "wb")) {
        std::fprintf(fp, "P5\n%d %d\n255\n", c.w(), c.h());
        for (int y = 0; y < c.h(); y++) std::fwrite(c.row(y), 1, c.w(), fp);
        std::fclose(fp);
    }
    std::printf("scale %d, lcd %dx%d at %d,%d, bar %dx%d at %d,%d\n", l.scale, l.lcd.w, l.lcd.h,
                l.lcd.x, l.lcd.y, l.bar.w, l.bar.h, l.bar.x, l.bar.y);
    return 0;
}
