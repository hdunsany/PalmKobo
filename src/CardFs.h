// palmkobo - Palm OS (CloudpilotEmu) front end for Kobo e-readers
// SPDX-License-Identifier: GPL-3.0-or-later
//
// SD card image helpers built on CloudpilotEmu's own card code:
//   * create a blank, Palm-compatible FAT16 card (CloudpilotEmu's dosfstools mkfs)
//   * load/save the image file, writing back only the 8 KB pages that changed
//   * copy files from a Kobo folder onto the card (import)
//   * mirror the whole card into a Kobo folder you can browse over USB (export)
//
// Import/export use FatFs from CloudpilotEmu's vfs module and must only run
// while the card is NOT mounted in the emulator (Palm OS caches the FAT).
// No emulator dependency, so this is tested on the host.

#ifndef PALMKOBO_CARD_FS_H
#define PALMKOBO_CARD_FS_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class CardImage;

namespace pk::card {

struct Report {
    int files{0};      // files copied
    int skipped{0};    // unchanged files (export)
    int deleted{0};    // stale files removed from the mirror (export)
    int errors{0};
    std::vector<std::string> lines;
};

// Card sizes CloudpilotEmu can format (MB): 4, 8, 16, 32, 64, 128.
bool supportedSize(unsigned sizeMb);

// Blank FAT16 card with a partition table, as CloudpilotEmu's "new card" makes.
bool create(unsigned sizeMb, std::unique_ptr<uint8_t[]>& data, size_t& size, std::string& error);

bool load(const std::string& path, std::unique_ptr<uint8_t[]>& data, size_t& size, std::string& error);

// Write the whole image (atomically, via a temporary file).
bool saveFull(CardImage& image, const std::string& path, std::string& error);

// Write back only pages marked dirty since the last call, then clear the marks.
// Falls back to a full save if the file is missing or has the wrong size.
bool saveDirty(CardImage& image, const std::string& path, std::string& error, size_t* pagesWritten = nullptr);

// Copy everything under srcDir onto the card and move the originals into
// srcDir/imported/. Palm apps and databases (.prc .pdb .pqa) at the top level go
// to /PALM/Launcher so they show up in the Palm launcher; anything else keeps
// its relative path.
Report importDir(CardImage& image, const std::string& srcDir);

// Make dstDir an exact copy of the card's contents (unchanged files are skipped,
// files no longer on the card are removed from dstDir).
Report exportMirror(CardImage& image, const std::string& dstDir);

// True if the image holds a FAT file system we can read.
bool hasFileSystem(CardImage& image);

}  // namespace pk::card

#endif
