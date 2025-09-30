#include "Logger.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/dist_sink.h>
#include <spdlog/async.h>
#include <spdlog/async_logger.h>

#include <filesystem>
#include <cstdlib>

namespace caldera::backend::common {

Logger& Logger::instance()
{
	static Logger inst;
	return inst;
}

Logger::Logger() = default;

Logger::~Logger() = default;

void Logger::initialize(const std::string& logFilePath,
			  spdlog::level::level_enum defaultLevel,
			  std::chrono::seconds flushEvery,
			  spdlog::level::level_enum flushOn)
{
	std::scoped_lock lock(mutex_);
	if (initialized_) {
		spdlog::warn("Logger::initialize() called more than once; ignoring subsequent call");
		return;
	}
	try {
		// Ensure directory exists
		std::filesystem::path p{logFilePath};
		if (p.has_parent_path()) {
			std::error_code ec; // non-throwing
			std::filesystem::create_directories(p.parent_path(), ec);
			if (ec) {
				spdlog::warn("Failed to create log directory '{}': {}", p.parent_path().string(), ec.message());
			}
		}

		// Initialize (once) the global async thread pool BEFORE creating async loggers
		// Queue size / thread count configurable via env:
		//   CALDERA_LOG_QUEUE_SIZE (default 8192)
		//   CALDERA_LOG_WORKERS (default 1)
		auto readEnvSize = [](const char* name, std::size_t def)->std::size_t {
			if (const char* v = std::getenv(name)) {
				try {
					long long parsed = std::stoll(v);
					if (parsed > 0) return static_cast<std::size_t>(parsed);
				} catch(...) {}
			}
			return def;
		};
		const std::size_t kQueueSize = readEnvSize("CALDERA_LOG_QUEUE_SIZE", 8192);
		const std::size_t kWorkerThreads = readEnvSize("CALDERA_LOG_WORKERS", 1);
		try {
			spdlog::init_thread_pool(kQueueSize, kWorkerThreads);
		} catch (...) {
			// Ignore if thread pool already exists; spdlog throws only on double init
		}

		console_sink_ = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		file_sink_ = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logFilePath, 5 * 1024 * 1024, 3);

		// Pattern applies to all newly created loggers
		spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");

		// Resolve global level: env overrides provided default (we manage manually per logger)
		if (auto envLevel = envLogLevel()) {
			globalLevel_ = *envLevel;
		} else {
			globalLevel_ = defaultLevel;
		}
		// Set global registry baseline to trace so our per-logger levels are authoritative
		spdlog::set_level(spdlog::level::trace);

		// Flush policy
		spdlog::flush_on(flushOn);
		spdlog::flush_every(flushEvery);

		initialized_ = true;
	} catch (const spdlog::spdlog_ex& ex) {
		spdlog::error("Logger initialization failed: {}", ex.what());
	}
}

void Logger::shutdown()
{
	std::scoped_lock lock(mutex_);
	if (!initialized_) return;
	spdlog::shutdown();
	console_sink_.reset();
	file_sink_.reset();
	initialized_ = false;
}



std::shared_ptr<spdlog::logger> Logger::get(const std::string& name)
{
	std::scoped_lock lock(mutex_);
	if (!initialized_) {
		throw std::runtime_error("Logger::get() called before initialize()");
	}
	if (auto existing = spdlog::get(name)) {
		return existing;
	}
	auto dist = std::make_shared<spdlog::sinks::dist_sink_mt>();
	if (console_sink_) dist->add_sink(console_sink_);
	if (file_sink_) dist->add_sink(file_sink_);
	// Build async logger with a single dist sink (fan-out inside dist_sink)
	auto new_logger = std::make_shared<spdlog::async_logger>(
		name,
		spdlog::sinks_init_list{dist},
		spdlog::thread_pool(),
		spdlog::async_overflow_policy::block
	);
	// Tentatively set level before registration
	spdlog::level::level_enum desiredLevel = globalLevel_;
	if (auto it = perLoggerLevels_.find(name); it != perLoggerLevels_.end()) {
		desiredLevel = it->second;
	}
	new_logger->set_level(desiredLevel);
	spdlog::register_logger(new_logger);
	// Re-apply desired level after registration to counter registry's global level assignment
	new_logger->set_level(desiredLevel);
	loggerNames_.push_back(name);
	return new_logger;
}

void Logger::setGlobalLevel(spdlog::level::level_enum level)
{
	std::scoped_lock lock(mutex_);
	globalLevel_ = level;
	// Update all loggers that do NOT have explicit overrides
	for (const auto& n : loggerNames_) {
		if (perLoggerLevels_.find(n) == perLoggerLevels_.end()) {
			if (auto l = spdlog::get(n)) {
				l->set_level(level);
			}
		}
	}
}

void Logger::setLoggerLevel(const std::string& name, spdlog::level::level_enum level)
{
	std::scoped_lock lock(mutex_);
	perLoggerLevels_[name] = level;
	if (auto l = spdlog::get(name)) {
		l->set_level(level);
	}
}

void Logger::clearLoggerLevel(const std::string& name)
{
	std::scoped_lock lock(mutex_);
	perLoggerLevels_.erase(name);
	if (auto l = spdlog::get(name)) {
		l->set_level(globalLevel_);
	}
}

std::optional<spdlog::level::level_enum> Logger::envLogLevel() const
{
	const char* lvl = std::getenv("CALDERA_LOG_LEVEL");
	if (!lvl) return std::nullopt;
	try {
		auto l = spdlog::level::from_str(lvl);
		return l;
	} catch (...) {
		return std::nullopt;
	}
}

void Logger::warnRateLimited(const std::string& loggerName, const std::string& key, std::chrono::milliseconds period, const std::string& message)
{
	auto now = std::chrono::steady_clock::now();
	bool shouldLog = false;
	{
		std::scoped_lock lock(mutex_);
		auto &entry = rateLimitMap_[key];
		if (now - entry.last >= period) {
			entry.last = now;
			shouldLog = true;
		}
	}
	if (shouldLog) {
		try {
			auto lg = get(loggerName);
			lg->warn(message);
		} catch(...) {
			// ignore
		}
	}
}

} // namespace caldera::backend::common