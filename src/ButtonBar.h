// palmkobo - Palm OS (CloudpilotEmu) front end for Kobo e-readers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The on-screen row of Palm hardware buttons below the Graffiti area.
// Labels use a tiny built-in 5x7 font so we need no font files or FBInk text API.

#ifndef PALMKOBO_BUTTON_BAR_H
#define PALMKOBO_BUTTON_BAR_H

#include <cstdint>
#include <string>
#include <vector>

#include "Canvas.h"
#include "Geometry.h"

namespace pk {

enum class BarAction { App1, App2, App3, App4, Up, Down, Refresh, Power, Exit };

class ButtonBar {
   public:
    struct Button {
        BarAction action;
        std::string label;
        Rect rect;
        bool pressed{false};
    };

    void layout(const Rect& bar);

    // Index of the button under (x, y), or -1.
    int hit(int x, int y) const;

    // Redraw one button (or all with index -1). Returns the dirty rect.
    Rect draw(Canvas& canvas, int index = -1) const;

    Button& at(int i) { return buttons[i]; }
    const Button& at(int i) const { return buttons[i]; }
    int count() const { return static_cast<int>(buttons.size()); }
    int indexOf(BarAction a) const;

    // Text rendering, exposed for the touch-test screen.
    static void drawText(Canvas& canvas, const std::string& text, int cx, int cy, int pixel,
                         uint8_t color);
    static int textWidth(const std::string& text, int pixel);

   private:
    Rect area;
    std::vector<Button> buttons;
};

}  // namespace pk

#endif
