#pragma once
#include <string>
#include <vector>
#include <cstdlib>

namespace caldera { namespace backend { namespace tests {

// RAII helper to temporarily set a list of environment variables and restore previous state on destruction.
// Used to ensure deterministic processing conditions inside integration tests (e.g. disabling spatial/adaptive filters).
class EnvVarGuard {
public:
    struct VarSpec { const char* name; const char* value; };
    EnvVarGuard() = default;
    explicit EnvVarGuard(const std::vector<VarSpec>& vars) {
        for (const auto& vs : vars) {
            const char* cur = std::getenv(vs.name);
            prev_.push_back({vs.name, cur != nullptr, cur ? std::string(cur) : std::string()});
            setenv(vs.name, vs.value, 1);
        }
    }
    EnvVarGuard(const EnvVarGuard&) = delete;
    EnvVarGuard& operator=(const EnvVarGuard&) = delete;
    EnvVarGuard(EnvVarGuard&& other) noexcept : prev_(std::move(other.prev_)), active_(other.active_) { other.active_ = false; }
    EnvVarGuard& operator=(EnvVarGuard&& other) noexcept {
        if (this != &other) {
            restore();
            prev_ = std::move(other.prev_);
            active_ = other.active_;
            other.active_ = false;
        }
        return *this;
    }
    ~EnvVarGuard() { restore(); }

    static EnvVarGuard disableSpatialAndAdaptive() {
        return EnvVarGuard({
            {"CALDERA_ENABLE_SPATIAL_FILTER", "0"},
            {"CALDERA_ADAPTIVE_SPATIAL_ENABLED", "0"}
        });
    }
private:
    struct PrevEntry { std::string name; bool had; std::string value; };
    std::vector<PrevEntry> prev_;
    bool active_ = true;
    void restore() {
        if (!active_) return;
        for (auto it = prev_.rbegin(); it != prev_.rend(); ++it) {
            if (it->had) {
                setenv(it->name.c_str(), it->value.c_str(), 1);
            } else {
                unsetenv(it->name.c_str());
            }
        }
        active_ = false;
    }
};

// Convenience alias specific for deterministic processing context.
using DeterministicProcessingGuard = EnvVarGuard;

}}} // namespace caldera::backend::tests
