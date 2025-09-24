#include "Logger.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/dist_sink.h>

namespace caldera::backend::common {

Logger& Logger::instance()
{
	static Logger inst;
	return inst;
}

Logger::Logger() = default;

Logger::~Logger() = default;

void Logger::initialize(const std::string& logFilePath)
{
	try {
		// Create and store shared sinks: console and rotating file
		console_sink_ = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		file_sink_ = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logFilePath, 5 * 1024 * 1024, 3);

		// Configure default pattern and global level
		spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
		spdlog::set_level(spdlog::level::trace);
	} catch (const spdlog::spdlog_ex& ex) {
		// If initialization fails, fallback to default stderr logger
		spdlog::error("Logger initialization failed: {}", ex.what());
	}
}

void Logger::shutdown()
{
	spdlog::shutdown();
	console_sink_.reset();
	file_sink_.reset();
}



std::shared_ptr<spdlog::logger> Logger::get(const std::string& name)
{
	// Return existing logger if already registered
	auto existing = spdlog::get(name);
	if (existing) return existing;

	// Create a dist sink that forwards to our stored sinks
	auto dist = std::make_shared<spdlog::sinks::dist_sink_mt>();
	if (console_sink_) dist->add_sink(console_sink_);
	if (file_sink_) dist->add_sink(file_sink_);

	auto new_logger = std::make_shared<spdlog::logger>(name, dist);
	spdlog::register_logger(new_logger);
	return new_logger;
}

} // namespace caldera::backend::common