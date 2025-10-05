#pragma once

#include <vector>
#include <string>
#include "tools/viewer/SensorViewerCore.h" // for SensorType enum

namespace caldera::backend::tools {

struct SensorInfo {
    SensorType type;
    std::string id; // device serial or descriptive id
};

// Enumerate available physical sensors (currently Kinect V2 then V1 fallback if compiled)
// This is a best-effort, lightweight probe (open then close immediately).
std::vector<SensorInfo> enumerateSensors();

} // namespace caldera::backend::tools
