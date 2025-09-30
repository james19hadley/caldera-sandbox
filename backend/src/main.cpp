#include "common/Logger.h"
#include "common/LoggingNames.h"

#include "AppManager.h"
#include "hal/HAL_Manager.h"
#include "processing/ProcessingManager.h"
#include "transport/LocalTransportServer.h"

#include <exception>
#include <thread>
#include <chrono>

int main() {
	using caldera::backend::common::Logger;
	using namespace caldera::backend::logging_names;
	using namespace caldera::backend;

	// Initialize logging early
	Logger::instance().initialize("logs/backend/backend.log", spdlog::level::info);

	// Global baseline (can override via CALDERA_LOG_LEVEL)
	Logger::instance().setGlobalLevel(spdlog::level::warn);
	// Focus on Fusion + HAL UDP for deep tracing
	Logger::instance().setLoggerLevel(PROC_FUSION, spdlog::level::trace);
	Logger::instance().setLoggerLevel(HAL_UDP, spdlog::level::debug);
	Logger::instance().setLoggerLevel(APP_LIFECYCLE, spdlog::level::info);
	Logger::instance().setLoggerLevel(TRANSPORT_SERVER, spdlog::level::debug);

	try {
		// Acquire loggers first (so we see creation ordering)
		auto appLog       = Logger::instance().get(APP_LIFECYCLE);
		auto halLog       = Logger::instance().get(HAL_MANAGER);
		auto udpLog       = Logger::instance().get(HAL_UDP);
		auto procOrchLog  = Logger::instance().get(PROC_ORCH);
		auto fusionLog    = Logger::instance().get(PROC_FUSION);
		auto transportLog = Logger::instance().get(TRANSPORT_SERVER);
		auto handshakeLog = Logger::instance().get(TRANSPORT_HANDSHAKE);

		// Construct subsystems with DI of loggers
		auto hal = std::make_shared<hal::HAL_Manager>(halLog, udpLog);
		auto processing = std::make_shared<processing::ProcessingManager>(procOrchLog, fusionLog);
		auto transport = std::make_shared<transport::LocalTransportServer>(transportLog, handshakeLog);

		AppManager app(appLog, hal, processing, transport);
		app.start();

		// Mock run loop
		std::this_thread::sleep_for(std::chrono::seconds(2));
		app.stop();
	} catch (const std::exception& ex) {
		Logger::instance().get(APP_LIFECYCLE)->critical(std::string("Fatal exception: ") + ex.what());
		Logger::instance().shutdown();
		return 1;
	} catch (...) {
		Logger::instance().get(APP_LIFECYCLE)->critical("Unknown fatal exception");
		Logger::instance().shutdown();
		return 2;
	}

	Logger::instance().shutdown();
	return 0;
}
