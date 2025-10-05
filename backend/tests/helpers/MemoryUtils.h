#ifndef CALDERA_TESTS_MEMORY_UTILS_H
#define CALDERA_TESTS_MEMORY_UTILS_H

#include <fstream>
#include <string>
#include <cstddef>

// Memory testing utilities shared across memory test files
namespace MemoryUtils {

    inline bool isAsan() {
#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
    return true;
#  endif
#endif
#if defined(__SANITIZE_ADDRESS__)
    return true;
#endif
    const char* env = std::getenv("CALDERA_ENABLE_ASAN");
    if (env && (std::string(env)=="1" || std::string(env)=="ON")) return true;
    return false;
    }
    
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
        if (current <= baseline) return true; // No growth (or reduction)
        // Detect ASAN to allow higher transient RSS (redzones, allocator quarantine) without failing tests
        auto isAsan = [](){
#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
            return true;
#  endif
#endif
#if defined(__SANITIZE_ADDRESS__)
            return true;
#endif
            const char* env = std::getenv("CALDERA_ENABLE_ASAN");
            if (env && (std::string(env)=="1" || std::string(env)=="ON")) return true;
            return false;
        }();
        double growthPercent = (static_cast<double>(current - baseline) / static_cast<double>(baseline)) * 100.0;
        double allowanceFactor = isAsan ? 6.0 : 1.0; // generous multiplier under ASAN/debug builds
        return growthPercent <= (maxGrowthPercent * allowanceFactor);
    }

    inline double calculateGrowthPercent(size_t baseline, size_t current) {
        if (baseline == 0) return 0.0;
        if (current <= baseline) return 0.0; // treat non-growth as 0% for reporting simplicity
        return (static_cast<double>(current - baseline) / static_cast<double>(baseline)) * 100.0;
    }

    inline bool checkMemoryGrowthAdaptive(size_t baseline, size_t current, double maxGrowthPercent, size_t absAllowanceBytes) {
        if (baseline == 0) return true;
        if (current <= baseline) return true;
        size_t delta = current - baseline;
        double percent = calculateGrowthPercent(baseline, current);
        // Reuse ASAN detection logic
        auto isAsan = [](){
#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
            return true;
#  endif
#endif
#if defined(__SANITIZE_ADDRESS__)
            return true;
#endif
            const char* env = std::getenv("CALDERA_ENABLE_ASAN");
            if (env && (std::string(env)=="1" || std::string(env)=="ON")) return true;
            return false;
        }();
        double allowanceFactor = isAsan ? 6.0 : 1.0;
        double effPercentLimit = maxGrowthPercent * allowanceFactor;
        if (percent <= effPercentLimit) return true;
        if (delta <= absAllowanceBytes * (isAsan ? 2 : 1)) return true; // permit more absolute growth under ASAN
        return false;
    }

    // Check if shared memory segment exists
    inline bool shmExists(const std::string& name) {
        std::string path = "/dev/shm/" + name;
        std::ifstream file(path);
        return file.good();
    }
}

#endif // CALDERA_TESTS_MEMORY_UTILS_H