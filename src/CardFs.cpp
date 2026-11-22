// palmkobo - Palm OS (CloudpilotEmu) front end for Kobo e-readers
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CardFs.h"

#include <fcntl.h>
#include <getopt.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>

// CloudpilotEmu common / vfs / fstools
#include "CardImage.h"
#include "CardVolume.h"
#include "card_io.h"
#include "dosfstools/mkfs.h"
#include "fatfs/diskio.h"
#include "fatfs/ff.h"
#include "fstools_util.h"

namespace pk::card {

namespace fs = std::filesystem;

namespace {

constexpr const char* DRIVE = "0:";
constexpr unsigned VOLUME_INDEX = 0;
constexpr const char* IMPORTED_DIR = "imported";

// RAII FatFs mount of a card image on volume 0.
class FatMount {
   public:
    explicit FatMount(CardImage& image) : volume(image) {
        if (volume.GetType() == CardVolume::Type::invalid) return;
        register_card_volume(VOLUME_INDEX, &volume);
        registered = true;
        ok = f_mount(&fs, DRIVE, 1) == FR_OK;
    }
    ~FatMount() {
        if (ok) f_mount(nullptr, DRIVE, 0);
        if (registered) unregister_card_volume(VOLUME_INDEX);
    }
    bool mounted() const { return ok; }

   private:
    CardVolume volume;
    FATFS fs{};
    bool registered{false};
    bool ok{false};
};

std::string cardPath(const std::string& rel) { return std::string(DRIVE) + "/" + rel; }

std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    return a + "/" + b;
}

bool hostMkdirs(const std::string& path) {
    std::error_code ec;
    fs::create_directories(path, ec);
    return !ec;
}

FRESULT cardMkdirs(const std::string& rel) {
    size_t pos = 0;
    while (pos != std::string::npos) {
        const size_t next = rel.find('/', pos ? pos + 1 : 0);
        const std::string part = rel.substr(0, next);
        if (!part.empty()) {
            const FRESULT r = f_mkdir(cardPath(part).c_str());
            if (r != FR_OK && r != FR_EXIST) return r;
        }
        pos = next;
    }
    return FR_OK;
}

std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool isPalmDatabase(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;
    const std::string ext = upper(name.substr(dot + 1));
    return ext == "PRC" || ext == "PDB" || ext == "PQA";
}

// FAT date/time <-> Unix time (FAT stores local time).
time_t fatToUnix(WORD fdate, WORD ftime) {
    struct tm t{};
    t.tm_year = ((fdate >> 9) & 0x7f) + 80;
    t.tm_mon = ((fdate >> 5) & 0x0f) - 1;
    t.tm_mday = fdate & 0x1f;
    t.tm_hour = (ftime >> 11) & 0x1f;
    t.tm_min = (ftime >> 5) & 0x3f;
    t.tm_sec = (ftime & 0x1f) * 2;
    t.tm_isdst = -1;
    return mktime(&t);
}

void unixToFat(time_t when, WORD& fdate, WORD& ftime) {
    struct tm t{};
    localtime_r(&when, &t);
    if (t.tm_year < 80) t.tm_year = 80, t.tm_mon = 0, t.tm_mday = 1;
    fdate = static_cast<WORD>(((t.tm_year - 80) << 9) | ((t.tm_mon + 1) << 5) | t.tm_mday);
    ftime = static_cast<WORD>((t.tm_hour << 11) | (t.tm_min << 5) | (t.tm_sec / 2));
}

bool readHostFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !in.bad();
}

const char* fresultText(FRESULT r) {
    switch (r) {
        case FR_DENIED:
            return "card full or directory full";
        case FR_EXIST:
            return "already exists";
        case FR_INVALID_NAME:
            return "invalid file name";
        case FR_NO_PATH:
            return "path not found";
        default:
            return "file system error";
    }
}

// ---- import ------------------------------------------------------------

void importWalk(const std::string& srcRoot, const std::string& rel, Report& report) {
    const std::string dir = joinPath(srcRoot, rel);
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        const std::string n = e.path().filename().string();
        if (rel.empty() && n == IMPORTED_DIR) continue;
        if (!n.empty() && n[0] == '.') continue;  // hidden files (e.g. macOS ._ files)
        names.push_back(n);
    }
    std::sort(names.begin(), names.end());

    for (const auto& n : names) {
        const std::string relPath = joinPath(rel, n);
        const std::string hostPath = joinPath(srcRoot, relPath);
        struct stat st;
        if (stat(hostPath.c_str(), &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            importWalk(srcRoot, relPath, report);
            rmdir(hostPath.c_str());  // only succeeds once everything has been moved out
            continue;
        }
        if (!S_ISREG(st.st_mode)) continue;

        const std::string dest =
            (rel.empty() && isPalmDatabase(n)) ? joinPath("PALM/Launcher", n) : relPath;

        std::vector<uint8_t> content;
        if (!readHostFile(hostPath, content)) {
            report.errors++;
            report.lines.push_back("cannot read " + hostPath);
            continue;
        }

        const size_t slash = dest.find_last_of('/');
        FRESULT r = slash == std::string::npos ? FR_OK : cardMkdirs(dest.substr(0, slash));
        FIL f;
        UINT written = 0;
        if (r == FR_OK) r = f_open(&f, cardPath(dest).c_str(), FA_WRITE | FA_CREATE_ALWAYS);
        if (r == FR_OK) {
            r = f_write(&f, content.data(), static_cast<UINT>(content.size()), &written);
            const FRESULT rc = f_close(&f);
            if (r == FR_OK) r = rc;
            if (r == FR_OK && written != content.size()) r = FR_DENIED;  // disk full
            if (r != FR_OK) f_unlink(cardPath(dest).c_str());
        }
        if (r != FR_OK) {
            report.errors++;
            report.lines.push_back("cannot copy " + relPath + " to card: " + fresultText(r));
            continue;
        }

        FILINFO fi{};
        unixToFat(st.st_mtime, fi.fdate, fi.ftime);
        f_utime(cardPath(dest).c_str(), &fi);

        // Move the original out of the way so it isn't imported twice.
        const std::string done = joinPath(joinPath(srcRoot, IMPORTED_DIR), relPath);
        hostMkdirs(fs::path(done).parent_path().string());
        std::error_code rec;
        fs::rename(hostPath, done, rec);
        if (rec) {  // e.g. a file of that name was imported before
            fs::remove(done, rec);
            fs::rename(hostPath, done, rec);
        }

        report.files++;
        report.lines.push_back("copied " + relPath + " -> /" + dest);
    }
}

// ---- export ------------------------------------------------------------

bool copyCardFileOut(const std::string& cardRel, const std::string& hostPath, FSIZE_t size,
                     time_t mtime) {
    FIL f;
    if (f_open(&f, cardPath(cardRel).c_str(), FA_READ) != FR_OK) return false;
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    UINT got = 0;
    const FRESULT r = buf.empty() ? FR_OK : f_read(&f, buf.data(), static_cast<UINT>(buf.size()), &got);
    f_close(&f);
    if (r != FR_OK || got != buf.size()) return false;

    const std::string tmp = hostPath + ".palmkobo-tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
        if (!out) return false;
    }
    if (std::rename(tmp.c_str(), hostPath.c_str()) != 0) return false;

    struct utimbuf ut{mtime, mtime};
    utime(hostPath.c_str(), &ut);
    return true;
}

void exportWalk(const std::string& dstRoot, const std::string& rel, std::set<std::string>& seen,
                Report& report) {
    std::vector<FILINFO> entries;
    {
        DIR d;
        if (f_opendir(&d, cardPath(rel).c_str()) != FR_OK) {
            report.errors++;
            report.lines.push_back("cannot read card folder /" + rel);
            return;
        }
        FILINFO fi;
        while (f_readdir(&d, &fi) == FR_OK && fi.fname[0] != 0) {
            if (std::strcmp(fi.fname, ".") == 0 || std::strcmp(fi.fname, "..") == 0) continue;
            entries.push_back(fi);
        }
        f_closedir(&d);
    }

    for (const auto& fi : entries) {
        const std::string relPath = joinPath(rel, fi.fname);
        const std::string hostPath = joinPath(dstRoot, relPath);

        if (fi.fattrib & AM_DIR) {
            seen.insert(relPath);
            hostMkdirs(hostPath);
            exportWalk(dstRoot, relPath, seen, report);
            continue;
        }

        seen.insert(relPath);
        const time_t mtime = fatToUnix(fi.fdate, fi.ftime);
        struct stat st;
        if (stat(hostPath.c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
            static_cast<FSIZE_t>(st.st_size) == fi.fsize && st.st_mtime == mtime) {
            report.skipped++;
            continue;
        }

        if (copyCardFileOut(relPath, hostPath, fi.fsize, mtime)) {
            report.files++;
        } else {
            report.errors++;
            report.lines.push_back("cannot export /" + relPath);
        }
    }
}

}  // namespace

bool supportedSize(unsigned sizeMb) { return FSToolsUtil::determineImageSize(sizeMb) != 0; }

bool create(unsigned sizeMb, std::unique_ptr<uint8_t[]>& data, size_t& size, std::string& error) {
    const uint32_t bytes = FSToolsUtil::determineImageSize(sizeMb);
    if (bytes == 0) {
        error = "unsupported card size " + std::to_string(sizeMb) + " MB (use 4, 8, 16, 32, 64 or 128)";
        return false;
    }

    // CardImage takes ownership of the buffer, so build into a CardImage and copy out.
    auto* raw = new uint8_t[bytes]();
    CardImage image(raw, bytes / CardImage::BLOCK_SIZE);
    CardVolume volume(image);
    card_initialize(&volume);
    volume.Format();
    optind = 0;  // dosfstools parses an argv with getopt; force a full getopt reset
    if (!mkfs(static_cast<int>(volume.AdvicedClusterSize()), "PALMKOBO")) {
        error = "formatting the card image failed";
        return false;
    }
    volume.FixupPartitionType();

    data = std::make_unique<uint8_t[]>(bytes);
    std::memcpy(data.get(), image.RawData(), bytes);
    size = bytes;
    return true;
}

bool load(const std::string& path, std::unique_ptr<uint8_t[]>& data, size_t& size, std::string& error) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        error = "cannot open " + path;
        return false;
    }
    const std::streamoff len = in.tellg();
    if (len <= 0 || len % CardImage::BLOCK_SIZE != 0) {
        error = path + " is not a card image (size must be a multiple of 512 bytes)";
        return false;
    }
    in.seekg(0);
    data = std::make_unique<uint8_t[]>(static_cast<size_t>(len));
    in.read(reinterpret_cast<char*>(data.get()), len);
    if (in.gcount() != len) {
        error = "error reading " + path;
        return false;
    }
    size = static_cast<size_t>(len);
    return true;
}

bool saveFull(CardImage& image, const std::string& path, std::string& error) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "cannot write " + tmp;
            return false;
        }
        out.write(reinterpret_cast<const char*>(image.RawData()),
                  static_cast<std::streamsize>(image.BlocksTotal() * CardImage::BLOCK_SIZE));
        if (!out) {
            error = "error writing " + tmp;
            return false;
        }
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        error = "cannot replace " + path;
        return false;
    }
    const size_t pages = (image.BlocksTotal() * CardImage::BLOCK_SIZE + CardImage::DIRTY_PAGE_SIZE - 1) /
                         CardImage::DIRTY_PAGE_SIZE;
    std::memset(image.DirtyPages(), 0, (pages + 7) / 8);
    return true;
}

bool saveDirty(CardImage& image, const std::string& path, std::string& error, size_t* pagesWritten) {
    const size_t total = image.BlocksTotal() * CardImage::BLOCK_SIZE;
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || static_cast<size_t>(st.st_size) != total) {
        if (pagesWritten) *pagesWritten = SIZE_MAX;
        return saveFull(image, path, error);
    }

    const int fd = ::open(path.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        error = "cannot open " + path + " for writing";
        return false;
    }

    const size_t pages = (total + CardImage::DIRTY_PAGE_SIZE - 1) / CardImage::DIRTY_PAGE_SIZE;
    uint8_t* dirty = image.DirtyPages();
    size_t written = 0;
    bool ok = true;

    for (size_t page = 0; page < pages && ok; page++) {
        if (!(dirty[page >> 3] & (1 << (page & 7)))) continue;
        const size_t offset = page * CardImage::DIRTY_PAGE_SIZE;
        const size_t len = std::min(CardImage::DIRTY_PAGE_SIZE, total - offset);
        const uint8_t* src = image.RawData() + offset;
        size_t done = 0;
        while (done < len) {
            const ssize_t n = pwrite(fd, src + done, len - done, static_cast<off_t>(offset + done));
            if (n <= 0) {
                ok = false;
                break;
            }
            done += static_cast<size_t>(n);
        }
        if (ok) {
            dirty[page >> 3] &= static_cast<uint8_t>(~(1 << (page & 7)));
            written++;
        }
    }

    if (fsync(fd) != 0) ok = false;
    ::close(fd);
    if (!ok) error = "error writing " + path;
    if (pagesWritten) *pagesWritten = written;
    return ok;
}

bool hasFileSystem(CardImage& image) {
    FatMount m(image);
    return m.mounted();
}

Report importDir(CardImage& image, const std::string& srcDir) {
    Report report;
    struct stat st;
    if (stat(srcDir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return report;

    FatMount m(image);
    if (!m.mounted()) {
        report.errors++;
        report.lines.push_back("card has no readable FAT file system; nothing imported");
        return report;
    }
    importWalk(srcDir, "", report);
    return report;
}

Report exportMirror(CardImage& image, const std::string& dstDir) {
    Report report;
    FatMount m(image);
    if (!m.mounted()) {
        report.errors++;
        report.lines.push_back("card has no readable FAT file system; nothing exported");
        return report;
    }
    if (!hostMkdirs(dstDir)) {
        report.errors++;
        report.lines.push_back("cannot create " + dstDir);
        return report;
    }

    std::set<std::string> seen;
    exportWalk(dstDir, "", seen, report);

    // Remove what is no longer on the card. Compare case-insensitively, since
    // both FAT and the Kobo's storage are case-insensitive.
    std::set<std::string> seenUpper;
    for (const auto& s : seen) seenUpper.insert(upper(s));

    std::vector<fs::path> stale;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dstDir, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        const std::string rel = fs::relative(it->path(), dstDir, ec).generic_string();
        if (ec) break;
        if (!seenUpper.count(upper(rel))) {
            stale.push_back(it->path());
            if (it->is_directory()) it.disable_recursion_pending();
        }
    }
    for (const auto& p : stale) {
        std::error_code rec;
        report.deleted += static_cast<int>(fs::remove_all(p, rec));
    }
    return report;
}

}  // namespace pk::card
