// palmkobo - Palm OS (CloudpilotEmu) front end for Kobo e-readers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Minimal binary PGM (P5, 8-bit) reader for the silkscreen artwork.

#ifndef PALMKOBO_PGM_H
#define PALMKOBO_PGM_H

#include <cctype>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace pk {

struct GrayImage {
    int w{0}, h{0};
    std::vector<uint8_t> pixels;
    bool valid() const { return w > 0 && h > 0 && pixels.size() == static_cast<size_t>(w) * h; }
};

inline GrayImage loadPgm(const std::string& path) {
    GrayImage img;
    std::ifstream in(path, std::ios::binary);
    if (!in) return img;

    auto token = [&in]() {
        std::string t;
        int c;
        while ((c = in.get()) != EOF) {
            if (c == '#') {
                while ((c = in.get()) != EOF && c != '\n') {
                }
                continue;
            }
            if (std::isspace(c)) {
                if (!t.empty()) break;
                continue;
            }
            t.push_back(static_cast<char>(c));
        }
        return t;
    };

    if (token() != "P5") return img;
    try {
        const int w = std::stoi(token()), h = std::stoi(token()), maxval = std::stoi(token());
        if (w <= 0 || h <= 0 || maxval != 255 || w > 8192 || h > 8192) return img;
        img.pixels.resize(static_cast<size_t>(w) * h);
        in.read(reinterpret_cast<char*>(img.pixels.data()), static_cast<std::streamsize>(img.pixels.size()));
        if (in.gcount() != static_cast<std::streamsize>(img.pixels.size())) return GrayImage{};
        img.w = w;
        img.h = h;
    } catch (...) {
        return GrayImage{};
    }
    return img;
}

}  // namespace pk

#endif
