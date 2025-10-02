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
	uint32_t checksum = 0; // simple CRC32 or similar over heightMap.data bytes (future extensibility)
	// (No objects, events, metadata yet – added in later steps)
};

struct RawDepthFrame {
	std::string sensorId;
	uint64_t timestamp_ns = 0;
	int width = 0;  // Determined by actual device
	int height = 0; // Determined by actual device
	std::vector<uint16_t> data; // size expected == width * height
};

struct RawColorFrame {
	std::string sensorId;
	uint64_t timestamp_ns = 0;
	int width = 0;  // Determined by actual device  
	int height = 0; // Determined by actual device
	std::vector<uint8_t> data; // RGB or RGBA format, size = width * height * bytes_per_pixel
};

} // namespace caldera::backend::common
 
