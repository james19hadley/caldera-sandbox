// Core data contracts for the vertical slice.
// Step 1: Minimal world representation flowing through the backend.

#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <cmath>

namespace caldera::backend::common {

struct Point3D {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	bool valid = false;
	
	Point3D() = default;
	Point3D(float x_, float y_, float z_, bool valid_ = true) 
		: x(x_), y(y_), z(z_), valid(valid_) {}
};

struct DepthFrame {
	std::string sensorId;
	uint64_t timestamp_ns = 0;
	int width = 0;
	int height = 0;
	std::vector<float> data; // depth values in millimeters or meters
};

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

// 2D point in image coordinate space
struct Point2D {
	int x = 0;
	int y = 0;
	
	Point2D() = default;
	Point2D(int x_, int y_) : x(x_), y(y_) {}
};

} // namespace caldera::backend::common
 
