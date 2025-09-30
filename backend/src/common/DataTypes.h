// Placeholder domain data structures for pipeline wiring.
// Will be expanded with real fields later.

#ifndef CALDERA_BACKEND_COMMON_DATA_TYPES_H
#define CALDERA_BACKEND_COMMON_DATA_TYPES_H

#include <cstdint>
#include <vector>
#include <string>

namespace caldera::backend::data {

struct RawDataPacket {
	uint64_t timestamp_ns = 0; // monotonic time
	int sourceId = 0;          // device/source identifier
	std::vector<uint8_t> payload; // raw bytes (depth/color/whatever)
};

struct WorldFrame {
	uint64_t frameIndex = 0;
	uint64_t timestamp_ns = 0; // time produced
	// Placeholder: height map, objects, events etc.
	std::string debugInfo; // temporary for logging demonstration
};

} // namespace caldera::backend::data

#endif // CALDERA_BACKEND_COMMON_DATA_TYPES_H
