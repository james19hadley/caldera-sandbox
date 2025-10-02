#include "common/Logger.h"
#include "common/LoggingNames.h"

#include "AppManager.h"
#include "hal/ISensorDevice.h"
#include "hal/MockSensorDevice.h"
#include "hal/KinectV2_Device.h"
#include "hal/KinectV1_Device.h"
#include "hal/SyntheticSensorDevice.h"
#include "processing/ProcessingManager.h"
#include "transport/LocalTransportServer.h"
#include "transport/SharedMemoryTransportServer.h"
#include "transport/SocketTransportServer.h"

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
	Logger::instance().setLoggerLevel(TRANSPORT_HANDSHAKE, spdlog::level::info);

	try {
		// Acquire loggers first (so we see creation ordering)
		auto appLog       = Logger::instance().get(APP_LIFECYCLE);
		auto halLog       = Logger::instance().get(HAL_MANAGER);
		auto udpLog       = Logger::instance().get(HAL_UDP);
		auto procOrchLog  = Logger::instance().get(PROC_ORCH);
		auto fusionLog    = Logger::instance().get(PROC_FUSION);
		auto transportLog = Logger::instance().get(TRANSPORT_SERVER);
		auto handshakeLog = Logger::instance().get(TRANSPORT_HANDSHAKE);

		// Construct sensor device via simple factory
		std::unique_ptr<hal::ISensorDevice> device;
		const char* sensorType = std::getenv("CALDERA_SENSOR_TYPE");
		std::string sensor = sensorType ? sensorType : "mock"; // default mock
		if (sensor == "kinect2") {
			device = std::make_unique<hal::KinectV2_Device>();
			halLog->info("Factory: using KinectV2_Device");
		} else if (sensor == "kinect1") {
	#if CALDERA_HAVE_KINECT_V1
			device = std::make_unique<hal::KinectV1_Device>();
			halLog->info("Factory: using KinectV1_Device");
	#else
			halLog->error("Kinect v1 requested but CALDERA_HAVE_KINECT_V1=0 (build without libfreenect)");
			device = std::make_unique<hal::MockSensorDevice>("unused.dat");
	#endif
		} else if (sensor == "mock_recording") {
			// Expect CALDERA_SENSOR_RECORDING_PATH to point to a .dat file created by SensorRecorder
			const char* path = std::getenv("CALDERA_SENSOR_RECORDING_PATH");
			std::string file = path ? path : "test_sensor_data.dat";
			device = std::make_unique<hal::MockSensorDevice>(file);
			halLog->info("Factory: using MockSensorDevice playback file={} (ONCE)", file);
		} else if (sensor == "synthetic") {
            hal::SyntheticSensorDevice::Config cfg; // small deterministic config
            cfg.sensorId = "proc_synth";
            cfg.width = 32; cfg.height = 24; cfg.fps = 30.0f; cfg.pattern = hal::SyntheticSensorDevice::Pattern::RAMP;
            device = std::make_unique<hal::SyntheticSensorDevice>(cfg, halLog);
            halLog->info("Factory: using SyntheticSensorDevice size={}x{} fps={}", cfg.width, cfg.height, cfg.fps);
		} else { // fallback
			device = std::make_unique<hal::MockSensorDevice>("unused.dat");
			halLog->info("Factory: using MockSensorDevice (synthetic; file load may fail if missing)");
		}

		auto processing = std::make_shared<processing::ProcessingManager>(procOrchLog, fusionLog);
		// Transport selection: CALDERA_TRANSPORT=shm|socket|local (default local)
		std::string transportType = [](){ const char* v = std::getenv("CALDERA_TRANSPORT"); return v?std::string(v):std::string("local"); }();
		std::shared_ptr<transport::ITransportServer> transport;
		if (transportType == "shm") {
			transport::SharedMemoryTransportServer::Config cfg;
			const char* shmName = std::getenv("CALDERA_SHM_NAME");
			cfg.shm_name = shmName ? shmName : "/caldera_backend_process";
			const char* maxW = std::getenv("CALDERA_SHM_MAX_WIDTH");
			const char* maxH = std::getenv("CALDERA_SHM_MAX_HEIGHT");
			if (const char* ci = std::getenv("CALDERA_SHM_CHECKSUM_INTERVAL_MS")) { cfg.checksum_interval_ms = static_cast<uint32_t>(std::atoi(ci)); }
			if (maxW) cfg.max_width = std::atoi(maxW);
			if (maxH) cfg.max_height = std::atoi(maxH);
			transport = std::make_shared<transport::SharedMemoryTransportServer>(transportLog, cfg);
			transportLog->info("Using SharedMemoryTransportServer name={} size={}x{} checksum_interval_ms={}", cfg.shm_name, cfg.max_width, cfg.max_height, cfg.checksum_interval_ms);
		} else if (transportType == "socket") {
			transport::SocketTransportServer::Config cfg;
			if (const char* ep = std::getenv("CALDERA_SOCKET_ENDPOINT")) cfg.endpoint = ep;
			if (const char* ci = std::getenv("CALDERA_SOCKET_CHECKSUM_INTERVAL_MS")) cfg.checksum_interval_ms = static_cast<uint32_t>(std::atoi(ci));
			transport = std::make_shared<transport::SocketTransportServer>(transportLog, cfg);
			transportLog->info("Using SocketTransportServer endpoint={} checksum_interval_ms={}", cfg.endpoint, cfg.checksum_interval_ms);
		} else {
			transport = std::make_shared<transport::LocalTransportServer>(transportLog, handshakeLog);
			transportLog->info("Using LocalTransportServer (in-proc FIFO)");
		}

		AppManager app(appLog, std::move(device), processing, transport);
		app.start();

		// Run loop duration override via CALDERA_RUN_SECS (default 2)
		int runSecs = 2; if (const char* rs = std::getenv("CALDERA_RUN_SECS")) { try { runSecs = std::max(1, std::stoi(rs)); } catch(...) {} }
		std::this_thread::sleep_for(std::chrono::seconds(runSecs));
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
