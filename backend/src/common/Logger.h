#ifndef CALDERA_BACKEND_COMMON_LOGGER_H
#define CALDERA_BACKEND_COMMON_LOGGER_H

#include <memory>
#include <string>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

namespace caldera::backend::common {

class Logger {
public:
	// Get the singleton instance
	static Logger& instance();

	// Configure shared sinks used by all loggers (console + rotating file)
	void initialize(const std::string& logFilePath);

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

	// Shared sinks used to construct new loggers
	std::shared_ptr<spdlog::sinks::stdout_color_sink_mt> console_sink_;
	std::shared_ptr<spdlog::sinks::rotating_file_sink_mt> file_sink_;
};

} // namespace caldera::backend::common

#endif // CALDERA_BACKEND_COMMON_LOGGER_H
