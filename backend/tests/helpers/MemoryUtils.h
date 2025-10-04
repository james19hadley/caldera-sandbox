#ifndef CALDERA_TESTS_MEMORY_UTILS_H
#define CALDERA_TESTS_MEMORY_UTILS_H

#include <fstream>
#include <string>
#include <cstddef>

// Memory testing utilities shared across memory test files
namespace MemoryUtils {
    
    inline size_t getCurrentRSS() {
        std::ifstream file("/proc/self/status");
        std::string line;
        while (std::getline(file, line)) {
            if (line.find("VmRSS:") == 0) {
                size_t pos = line.find_first_of("0123456789");
                if (pos != std::string::npos) {
                    size_t value = std::stoull(line.substr(pos));
                    return value * 1024; // Convert from KB to bytes
                }
            }
        }
        return 0;
    }

    inline bool checkMemoryGrowth(size_t baseline, size_t current, double maxGrowthPercent) {
        if (baseline == 0) return true; // Can't check growth without baseline
        double growthPercent = ((double)(current - baseline) / baseline) * 100.0;
        return growthPercent <= maxGrowthPercent;
    }

    inline double calculateGrowthPercent(size_t baseline, size_t current) {
        if (baseline == 0) return 0.0;
        return ((double)(current - baseline) / baseline) * 100.0;
    }

    // Check if shared memory segment exists
    inline bool shmExists(const std::string& name) {
        std::string path = "/dev/shm/" + name;
        std::ifstream file(path);
        return file.good();
    }
}

#endif // CALDERA_TESTS_MEMORY_UTILS_H