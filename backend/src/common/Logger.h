#ifndef CALDERA_BACKEND_COMMON_LOGGER_H
#define CALDERA_BACKEND_COMMON_LOGGER_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <mutex>
#include <chrono>
#include <unordered_map>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

namespace caldera::backend::common {

class Logger {
public:
	// Get the singleton instance
	static Logger& instance();

	// Configure shared sinks used by all loggers (console + rotating file)
	// Parameters:
	//   logFilePath  - path to main log file (rotating)
	//   defaultLevel - fallback global level (overridden by CALDERA_LOG_LEVEL env var if set)
	//   flushEvery   - periodic flush interval
	//   flushOn      - level on/above which every message forces flush
	void initialize(const std::string& logFilePath,
			  spdlog::level::level_enum defaultLevel = spdlog::level::info,
			  std::chrono::seconds flushEvery = std::chrono::seconds{1},
			  spdlog::level::level_enum flushOn = spdlog::level::warn);

	// Whether initialize() has been successfully called
	[[nodiscard]] bool isInitialized() const noexcept { return initialized_; }

	// Set new global level (does NOT override per-logger explicit levels)
	void setGlobalLevel(spdlog::level::level_enum level);
	spdlog::level::level_enum getGlobalLevel() const noexcept { return globalLevel_; }

	// Set explicit level for a named logger (created now or in future)
	void setLoggerLevel(const std::string& name, spdlog::level::level_enum level);

	// Clear explicit per-logger override (logger reverts to global level)
	void clearLoggerLevel(const std::string& name);

	// Rate-limited warning: emits at most once per period per key.
	// key: logical grouping (e.g., "shm_drop"). period: minimum interval between emissions.
	void warnRateLimited(const std::string& loggerName, const std::string& key, std::chrono::milliseconds period, const std::string& message);

	// Shutdown all spdlog resources
	void shutdown();

	// Get (or create) a named logger that uses the shared sinks
	std::shared_ptr<spdlog::logger> get(const std::string& name);

	// Deleted copy/move to enforce singleton
	Logger(const Logger&) = delete;
	Logger& operator=(const Logger&) = delete;

private:
	Logger();
	~Logger();

	// Internal helper to parse CALDERA_LOG_LEVEL env var (returns empty if not set / invalid)
	std::optional<spdlog::level::level_enum> envLogLevel() const;

	// Guarded state
	bool initialized_ = false;
	spdlog::level::level_enum globalLevel_ = spdlog::level::info;
	std::unordered_map<std::string, spdlog::level::level_enum> perLoggerLevels_;
	std::vector<std::string> loggerNames_; // names of created loggers (for global level updates)
	mutable std::mutex mutex_;

	// Stored sinks used to construct new loggers
	std::shared_ptr<spdlog::sinks::stdout_color_sink_mt> console_sink_;
	std::shared_ptr<spdlog::sinks::rotating_file_sink_mt> file_sink_;

	struct RateLimitEntry { std::chrono::steady_clock::time_point last; };
	std::unordered_map<std::string, RateLimitEntry> rateLimitMap_; // key -> entry
};

} // namespace caldera::backend::common

#endif // CALDERA_BACKEND_COMMON_LOGGER_H
