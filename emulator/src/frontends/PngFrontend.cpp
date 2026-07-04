#include "PngFrontend.h"

#include <cinttypes>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "../EpdSpiCapture.h"
#include "cdc_log.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO_FAILURE 0
#include "stb_image_write.h"

namespace fs = std::filesystem;

namespace emu {

namespace {

constexpr const char* TAG = "PngFrontend";
constexpr uint16_t    kOutWidth = 296;
constexpr uint16_t    kOutHeight = 128;

}  // namespace

void frameToLandscapeGray(const uint8_t* frame, uint8_t* out)
{
    // Landscape (lx, ly) -> native portrait per CalEPD rotation 1:
    // nx = 127 - ly, ny = lx; MSB-first bits, set bit = white.
    const size_t lineBytes = kPanelWidth / 8;
    for (uint16_t ly = 0; ly < kOutHeight; ++ly) {
        const uint16_t nx = static_cast<uint16_t>(kPanelWidth - 1 - ly);
        const uint8_t  mask = static_cast<uint8_t>(0x80u >> (nx % 8));
        const size_t   byteInLine = nx / 8;
        for (uint16_t lx = 0; lx < kOutWidth; ++lx) {
            const size_t idx = lx * lineBytes + byteInLine;
            out[ly * kOutWidth + lx] = (frame[idx] & mask) ? 0xFF : 0x00;
        }
    }
}

PngFrontend::PngFrontend(std::string framesDir, std::string snapshotDir)
    : frames_dir_(std::move(framesDir)), snapshot_dir_(std::move(snapshotDir))
{
}

uint64_t PngFrontend::hashFrame(const uint8_t* frame, size_t len)
{
    // FNV-1a 64-bit over the raw panel buffer: cheap, stable across platforms.
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < len; ++i) {
        hash ^= frame[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

std::string PngFrontend::writePng(const uint8_t* frame, uint32_t index) const
{
    std::error_code ec;
    fs::create_directories(frames_dir_, ec);
    char name[32];
    snprintf(name, sizeof(name), "frame_%03u.png", index);
    const std::string path = (fs::path(frames_dir_) / name).string();

    uint8_t gray[kOutWidth * kOutHeight];
    frameToLandscapeGray(frame, gray);
    if (!stbi_write_png(path.c_str(), kOutWidth, kOutHeight, 1, gray, kOutWidth)) {
        LOG_E(TAG, "failed to write %s", path.c_str());
        return {};
    }
    return path;
}

void PngFrontend::onFrame(const uint8_t* frame, size_t len)
{
    const uint64_t hash = hashFrame(frame, len);
    if (!hashes_.empty() && hash == last_hash_) {
        return;  // unchanged frame - a re-render without visible effect
    }
    last_hash_ = hash;
    const uint32_t index = static_cast<uint32_t>(hashes_.size());
    hashes_.push_back(hash);
    fprintf(stderr, "FRAME %03u %016" PRIx64 "\n", index, hash);

    if (!frames_dir_.empty()) {
        writePng(frame, index);
    }
    if (!snapshot_dir_.empty()) {
        char name[32];
        snprintf(name, sizeof(name), "frame_%03u.hash", index);
        const fs::path ref = fs::path(snapshot_dir_) / name;
        std::ifstream  in(ref);
        uint64_t       expected = 0;
        if (!(in && (in >> std::hex >> expected))) {
            LOG_E(TAG, "snapshot: missing reference %s", ref.string().c_str());
            mismatch_ = true;
        } else if (expected != hash) {
            LOG_E(TAG,
                  "snapshot: frame %03u mismatch (expected %016" PRIx64
                  ", got %016" PRIx64 ")",
                  index, expected, hash);
            mismatch_ = true;
        }
    }
}

int PngFrontend::finish()
{
    if (!frames_dir_.empty() && !hashes_.empty()) {
        std::ofstream manifest(fs::path(frames_dir_) / "frames.txt");
        for (size_t i = 0; i < hashes_.size(); ++i) {
            char line[64];
            snprintf(line, sizeof(line), "frame_%03zu %016" PRIx64 "\n", i,
                     hashes_[i]);
            manifest << line;
        }
    }
    if (snapshot_dir_.empty()) {
        return 0;
    }
    // Also fail when the reference set expects MORE frames than were produced.
    uint32_t refCount = 0;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(snapshot_dir_, ec)) {
        if (entry.path().extension() == ".hash") {
            ++refCount;
        }
    }
    if (refCount != hashes_.size()) {
        LOG_E(TAG, "snapshot: produced %zu frames, references expect %u",
              hashes_.size(), refCount);
        mismatch_ = true;
    }
    return mismatch_ ? 4 : 0;
}

}  // namespace emu
