#include "SharedMemoryWorldFrameClient.h"
#include "common/Logger.h"
#include "common/Checksum.h"
#include <thread>

using namespace std::chrono_literals;

namespace caldera::backend::transport {

SharedMemoryWorldFrameClient::SharedMemoryWorldFrameClient(std::shared_ptr<spdlog::logger> logger, Config cfg)
    : logger_(std::move(logger)), cfg_(std::move(cfg)), reader_(logger_) {}

SharedMemoryWorldFrameClient::~SharedMemoryWorldFrameClient() { disconnect(); }

bool SharedMemoryWorldFrameClient::connect(uint32_t timeout_ms) {
    if (connected_) return true;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    do {
        if (reader_.open(cfg_.shm_name, cfg_.max_width, cfg_.max_height)) {
            connected_ = true;
            return true;
        }
        if (timeout_ms == 0) break;
        std::this_thread::sleep_for(25ms);
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

void SharedMemoryWorldFrameClient::disconnect() {
    if (!connected_) return;
    reader_.close();
    connected_ = false;
}

std::optional<IWorldFrameClient::FrameView> SharedMemoryWorldFrameClient::latest(bool verify_checksum) {
    auto fv = reader_.latest();
    if (!fv) return std::nullopt;
    // Map SharedMemoryReader::FrameView -> IWorldFrameClient::FrameView (layout is compatible subset)
    IWorldFrameClient::FrameView out{};
    out.frame_id = fv->frame_id;
    out.timestamp_ns = fv->timestamp_ns;
    out.width = fv->width;
    out.height = fv->height;
    out.data = fv->data;
    out.float_count = fv->float_count;
    out.checksum = fv->checksum;
    out.checksum_algorithm = fv->checksum_algorithm;
    out.checksum_valid = fv->checksum_valid;

    // Stats update
    ++stats_.frames_observed;
    if (out.frame_id != stats_.last_frame_id || stats_.distinct_frames == 0) {
        if (!(stats_.distinct_frames == 0 && out.frame_id == 0 && stats_.last_frame_id == 0)) {
            // treat first frame as distinct regardless of id
        }
        if (stats_.distinct_frames == 0 || out.frame_id != stats_.last_frame_id) {
            ++stats_.distinct_frames;
            stats_.last_frame_id = out.frame_id;
        }
    }
    bool hasChecksum = (out.checksum_algorithm == 1 && out.checksum != 0);
    if (hasChecksum) ++stats_.checksum_present;
    if (verify_checksum && hasChecksum) {
        // Need a mutable copy to call SharedMemoryReader::verifyChecksum
        SharedMemoryReader::FrameView mutableView = *fv; // copy
        if (!SharedMemoryReader::verifyChecksum(mutableView)) {
            ++stats_.checksum_mismatch;
            out.checksum_valid = false;
        } else if (mutableView.checksum_valid) {
            ++stats_.checksum_verified;
            out.checksum_valid = true;
        }
    }
    return out;
}

} // namespace caldera::backend::transport
