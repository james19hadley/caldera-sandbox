// Core data contracts for the vertical slice.
// Step 1: Minimal world representation flowing through the backend.

#pragma once

#include <cstdint>
#include <vector>
#include <string>

namespace caldera::backend::common {

struct StabilizedHeightMap {
	int width = 0;
	int height = 0;
	std::vector<float> data; // size expected == width * height
};

struct WorldFrame {
	uint64_t timestamp_ns = 0; // monotonic production timestamp
	uint64_t frame_id = 0; // monotonically increasing sequence id (assigned by processing stage)
	StabilizedHeightMap heightMap; // only terrain for now
	// (No objects, events, metadata yet – added in later steps)
};

struct RawDepthFrame {
	std::string sensorId;
	uint64_t timestamp_ns = 0;
	int width = 640;
	int height = 480;
	std::vector<uint16_t> data; // size expected == width * height
};

} // namespace caldera::backend::common
 
