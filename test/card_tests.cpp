// palmkobo SD card image tests: create, import, export, dirty-page saving.
// Validates the resulting FAT file systems with CloudpilotEmu's dosfstools fsck.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <getopt.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "CardFs.h"
#include "CardImage.h"
#include "CardVolume.h"
#include "card_io.h"
#include "dosfstools/fsck.h"

namespace fs = std::filesystem;
using namespace pk;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        checks++;                                                       \
        if (!(cond)) {                                                  \
            failures++;                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                               \
    } while (0)

static void writeFile(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << content;
}

static std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static bool fsckOk(CardImage& image) {
    CardVolume volume(image);
    if (volume.GetType() == CardVolume::Type::invalid) return false;
    card_initialize(&volume);
    std::fflush(stdout);
    // dosfstools prints a report; keep the test output readable.
    const int saved = dup(1);
    FILE* devnull = std::freopen("/dev/null", "w", stdout);
    (void)devnull;
    optind = 0;
    const int result = runFsck(0);
    std::fflush(stdout);
    dup2(saved, 1);
    close(saved);
    return result != 0;
}

static std::unique_ptr<CardImage> makeCard(unsigned mb) {
    std::unique_ptr<uint8_t[]> data;
    size_t size = 0;
    std::string err;
    if (!card::create(mb, data, size, err)) {
        std::printf("create failed: %s\n", err.c_str());
        return nullptr;
    }
    return std::make_unique<CardImage>(data.release(), size / CardImage::BLOCK_SIZE);
}

int main() {
    char tmpl[] = "/tmp/palmkobo-card-XXXXXX";
    const fs::path root = mkdtemp(tmpl);

    // --- create ---
    CHECK(card::supportedSize(64));
    CHECK(!card::supportedSize(7));
    {
        std::unique_ptr<uint8_t[]> d;
        size_t s;
        std::string e;
        CHECK(!card::create(7, d, s, e));
    }

    auto card = makeCard(8);
    CHECK(card != nullptr);
    if (!card) return 1;
    CHECK(card::hasFileSystem(*card));
    CHECK(fsckOk(*card));

    // --- import ---
    const fs::path in = root / "to-card";
    writeFile(in / "Hello.prc", std::string(5000, 'P'));
    writeFile(in / "MemoDB.PDB", "memo database");
    writeFile(in / "docs" / "note.txt", "hello from the kobo");
    writeFile(in / ".hidden", "skip me");

    card::Report r = card::importDir(*card, in.string());
    CHECK(r.files == 3);
    CHECK(r.errors == 0);
    CHECK(fs::exists(in / "imported" / "Hello.prc"));
    CHECK(fs::exists(in / "imported" / "docs" / "note.txt"));
    CHECK(!fs::exists(in / "Hello.prc"));
    CHECK(!fs::exists(in / "docs"));
    CHECK(fs::exists(in / ".hidden"));
    CHECK(fsckOk(*card));

    // Running the import again does nothing (originals were moved).
    r = card::importDir(*card, in.string());
    CHECK(r.files == 0);

    // Sanity check that fsck really detects damage: wipe part of the FAT of a copy.
    {
        const size_t bytes = card->BlocksTotal() * CardImage::BLOCK_SIZE;
        auto* raw = new uint8_t[bytes];
        std::memcpy(raw, card->RawData(), bytes);
        CardImage broken(raw, card->BlocksTotal());
        CardVolume v(broken);
        const size_t vbr = static_cast<size_t>(v.GetPartitionStartSector()) * 512;
        const size_t reserved = raw[vbr + 14] | (raw[vbr + 15] << 8);
        const size_t fat = vbr + reserved * 512;
        std::memset(raw + fat + 4, 0, 1020);  // keep media/EOC entries 0 and 1
        CHECK(!fsckOk(broken));
    }

    // --- export ---
    const fs::path out = root / "card-contents";
    r = card::exportMirror(*card, out.string());
    CHECK(r.errors == 0);
    CHECK(r.files == 3);
    CHECK(readFile(out / "PALM" / "Launcher" / "Hello.prc") == std::string(5000, 'P'));
    CHECK(readFile(out / "PALM" / "Launcher" / "MemoDB.PDB") == "memo database");
    CHECK(readFile(out / "docs" / "note.txt") == "hello from the kobo");

    // Unchanged second export copies nothing; stray files are removed.
    writeFile(out / "stray.txt", "not on the card");
    writeFile(out / "olddir" / "x.txt", "gone");
    r = card::exportMirror(*card, out.string());
    CHECK(r.files == 0);
    CHECK(r.skipped == 3);
    CHECK(!fs::exists(out / "stray.txt"));
    CHECK(!fs::exists(out / "olddir"));
    CHECK(fs::exists(out / "docs" / "note.txt"));

    // --- saving ---
    const fs::path img = root / "card.img";
    std::string err;
    CHECK(card::saveFull(*card, img.string(), err));
    CHECK(fs::file_size(img) == card->BlocksTotal() * CardImage::BLOCK_SIZE);

    // Nothing dirty right after a full save.
    size_t pages = 99;
    CHECK(card::saveDirty(*card, img.string(), err, &pages));
    CHECK(pages == 0);

    // Change the card, write back only dirty pages, and compare with a fresh load.
    writeFile(in / "Second.prc", std::string(20000, 'S'));
    r = card::importDir(*card, in.string());
    CHECK(r.files == 1);
    CHECK(card::saveDirty(*card, img.string(), err, &pages));
    CHECK(pages > 0 && pages < 50);
    {
        std::unique_ptr<uint8_t[]> d;
        size_t s = 0;
        CHECK(card::load(img.string(), d, s, err));
        CHECK(s == card->BlocksTotal() * CardImage::BLOCK_SIZE);
        CHECK(std::memcmp(d.get(), card->RawData(), s) == 0);
    }

    // Export picks up the new file only.
    r = card::exportMirror(*card, out.string());
    CHECK(r.files == 1);
    CHECK(r.skipped == 3);

    // --- card full ---
    auto small = makeCard(4);
    CHECK(small != nullptr);
    if (small) {
        const fs::path in2 = root / "big";
        writeFile(in2 / "huge.bin", std::string(6 * 1024 * 1024, 'X'));
        writeFile(in2 / "ok.txt", "fits");
        r = card::importDir(*small, in2.string());
        CHECK(r.errors == 1);
        CHECK(r.files == 1);
        CHECK(fs::exists(in2 / "huge.bin"));  // left in place for another try
        CHECK(fsckOk(*small));
        const fs::path out2 = root / "small-out";
        card::exportMirror(*small, out2.string());
        CHECK(!fs::exists(out2 / "huge.bin"));
        CHECK(fs::exists(out2 / "ok.txt"));
    }

    // Garbage image: no file system, import reports an error instead of crashing.
    {
        auto* raw = new uint8_t[1024 * 1024]();
        CardImage junk(raw, 2048);
        CHECK(!card::hasFileSystem(junk));
        writeFile(root / "j" / "a.txt", "a");
        r = card::importDir(junk, (root / "j").string());
        CHECK(r.errors == 1);
    }

    fs::remove_all(root);
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
