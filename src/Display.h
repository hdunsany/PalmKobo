// palmkobo - Palm OS (CloudpilotEmu) front end for Kobo e-readers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Output to the e-ink panel. Two backends:
//   DisplayFBInk.cpp - the real thing, via FBInk (built with PK_WITH_FBINK)
//   DisplayHost.cpp  - an in-memory panel for testing on a desktop, saved as PGM

#ifndef PALMKOBO_DISPLAY_H
#define PALMKOBO_DISPLAY_H

#include <memory>
#include <string>

#include "Canvas.h"
#include "Config.h"
#include "Geometry.h"
#include "RefreshScheduler.h"

namespace pk {

class Display {
   public:
    virtual ~Display() = default;

    // Returns an error message on failure.
    virtual std::string open() = 0;
    virtual void close() = 0;

    virtual int width() const = 0;
    virtual int height() const = 0;

    // Push a region of the canvas to the panel and refresh it.
    virtual void present(const Canvas& canvas, const Rect& rect, Waveform waveform) = 0;

    // Touch orientation the backend knows about for this device (FBInk quirks).
    virtual TouchTransform touchDefaults() const { return {}; }

    virtual std::string describe() const = 0;
};

std::unique_ptr<Display> makeDisplay(const Config& config);

}  // namespace pk

#endif
