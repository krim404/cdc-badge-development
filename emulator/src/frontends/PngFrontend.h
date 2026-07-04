/**
 * \file PngFrontend.h
 * \brief Headless frontend: one PNG per committed (changed) frame plus a
 *        frames.txt manifest with FNV-1a frame hashes; optional snapshot
 *        compare mode for CI regression (FR-026, SC-006).
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "IFrontend.h"

namespace emu {

class PngFrontend : public IFrontend {
public:
    /// \param framesDir  Directory for frame_NNN.png + frames.txt ("" = none).
    /// \param snapshotDir Reference directory to compare against ("" = off).
    PngFrontend(std::string framesDir, std::string snapshotDir);

    void onFrame(const uint8_t* frame, size_t len) override;

    /// Snapshot verdict: 0 = all frames matched the references (or snapshot
    /// mode is off); non-zero on any mismatch or missing reference.
    int finish();

    uint32_t frameCount() const { return static_cast<uint32_t>(hashes_.size()); }

    static uint64_t hashFrame(const uint8_t* frame, size_t len);

private:
    std::string writePng(const uint8_t* frame, uint32_t index) const;

    std::string           frames_dir_;
    std::string           snapshot_dir_;
    std::vector<uint64_t> hashes_;
    uint64_t              last_hash_ = 0;
    bool                  mismatch_ = false;
};

}  // namespace emu
