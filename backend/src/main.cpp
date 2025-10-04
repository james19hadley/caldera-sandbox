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
#if CALDERA_TRANSPORT_SOCKETS
#include "transport/SocketTransportServer.h"
#endif
#include "common/SensorResolutions.h"

#include <exception>
#include <thread>
#include <chrono>
#include <iostream>
#include <string>

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --sensor TYPE     Sensor type: kinect_v1, kinect_v2, mock, mock_recording, synthetic\n";
    std::cout << "  --shm             Enable SharedMemory transport (default: LocalTransport)\n";
#if CALDERA_TRANSPORT_SOCKETS
    std::cout << "  --socket          Enable Socket transport\n";
#endif
    std::cout << "  --help, -h        Show this help message\n\n";
    std::cout << "Environment Variables:\n";
    std::cout << "  CALDERA_SENSOR_TYPE               Sensor type (same as --sensor)\n";
    std::cout << "  CALDERA_SENSOR_RECORDING_PATH     Path to recording file for mock_recording\n";
    std::cout << "  CALDERA_SHM_MAX_WIDTH             SharedMemory max width (default: auto)\n";
    std::cout << "  CALDERA_SHM_MAX_HEIGHT            SharedMemory max height (default: auto)\n";
    std::cout << "  CALDERA_MULTI_SENSOR              Enable multi-sensor mode (1/true): larger SHM for fusion\n";
    std::cout << "  CALDERA_LOG_LEVEL                 Global log level\n";
}

// Auto-detect optimal SharedMemory size based on sensor type and future multi-sensor scenarios
std::pair<uint32_t, uint32_t> getOptimalShmSize(const std::string& sensor_type) {
    using namespace caldera::backend::common;
    
    // Check environment for multi-sensor hints
    const char* multi_env = std::getenv("CALDERA_MULTI_SENSOR");
    bool multi_sensor_mode = multi_env && (std::string(multi_env) == "1" || std::string(multi_env) == "true");
    
    if (sensor_type == "kinect_v1" || sensor_type == "kinect1") {
        if (multi_sensor_mode) {
            // Future: dual Kinect v1 setup - allocate for side-by-side fusion
            auto [w, h] = Transport::getOptimalSize(Transport::SensorConfiguration::DUAL_SENSOR);
            return {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
        }
        // Single Kinect v1
        return {KinectV1::WIDTH, KinectV1::HEIGHT};
    } else if (sensor_type == "kinect_v2" || sensor_type == "kinect2") {
        if (multi_sensor_mode) {
            // Future: multi-sensor Kinect v2 array - processing layer can create large fused frames
            auto [w, h] = Transport::getOptimalSize(Transport::SensorConfiguration::PROCESSING_FUSION);
            return {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
        }
        // Single Kinect v2: accommodate largest stream (color)
        return {KinectV2::COLOR_WIDTH, KinectV2::COLOR_HEIGHT};
    } else if (sensor_type == "synthetic") {
        // Small synthetic sensor (testing/demo)
        auto [w, h] = Transport::getOptimalSize(Transport::SensorConfiguration::LEGACY_SMALL);
        return {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
    } else if (sensor_type == "multi" || multi_sensor_mode) {
        // Explicit multi-sensor mode: processing layer may create fused frames
        auto [w, h] = Transport::getOptimalSize(Transport::SensorConfiguration::PROCESSING_FUSION);
        return {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
    } else {
        // Default: single modern sensor with high resolution capability
        auto [w, h] = Transport::getOptimalSize(Transport::SensorConfiguration::SINGLE_KINECT_V2);
        return {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
    }
}

int main(int argc, char* argv[]) {
	using caldera::backend::common::Logger;
	using namespace caldera::backend::logging_names;
	using namespace caldera::backend;

	// Parse command line arguments
	std::string sensor_override;
	bool use_shm = false;
	bool use_socket = false;
	
	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		if (arg == "--help" || arg == "-h") {
			printUsage(argv[0]);
			return 0;
		} else if (arg == "--sensor" && i + 1 < argc) {
			sensor_override = argv[++i];
		} else if (arg == "--shm") {
			use_shm = true;
#if CALDERA_TRANSPORT_SOCKETS
		} else if (arg == "--socket") {
			use_socket = true;
#endif
		} else {
			std::cerr << "Unknown argument: " << arg << std::endl;
			printUsage(argv[0]);
			return 1;
		}
	}

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
		
		// Priority: command line flag > environment variable > default
		std::string sensor;
		if (!sensor_override.empty()) {
			sensor = sensor_override;
		} else {
			const char* sensorType = std::getenv("CALDERA_SENSOR_TYPE");
			sensor = sensorType ? sensorType : "mock"; // default mock
		}
		if (sensor == "kinect_v2" || sensor == "kinect2") { // Support both new and legacy names
			device = std::make_unique<hal::KinectV2_Device>();
			halLog->info("Factory: using KinectV2_Device");
		} else if (sensor == "kinect_v1" || sensor == "kinect1") { // Support both new and legacy names
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
		// Transport selection: command line flag > CALDERA_TRANSPORT env > default local
		std::string transportType;
		if (use_shm) {
			transportType = "shm";
		} else if (use_socket) {
			transportType = "socket";  
		} else {
			const char* v = std::getenv("CALDERA_TRANSPORT");
			transportType = v ? std::string(v) : std::string("local");
		}
		
		std::shared_ptr<transport::ITransportServer> transport;
		if (transportType == "shm") {
			transport::SharedMemoryTransportServer::Config cfg;
			const char* shmName = std::getenv("CALDERA_SHM_NAME");
			cfg.shm_name = shmName ? shmName : "/caldera_backend_process";
			
			// Auto-detect optimal size based on sensor type, allow manual override
			const char* maxW = std::getenv("CALDERA_SHM_MAX_WIDTH");
			const char* maxH = std::getenv("CALDERA_SHM_MAX_HEIGHT");
			if (maxW && maxH) {
				// Manual override
				cfg.max_width = std::atoi(maxW);
				cfg.max_height = std::atoi(maxH);
				transportLog->info("SHM size: manual override {}x{}", cfg.max_width, cfg.max_height);
			} else {
				// Auto-detect based on sensor type
				auto [width, height] = getOptimalShmSize(sensor);
				cfg.max_width = width;
				cfg.max_height = height;
				transportLog->info("SHM size: auto-detected {}x{} for sensor '{}'", cfg.max_width, cfg.max_height, sensor);
			}
			
			if (const char* ci = std::getenv("CALDERA_SHM_CHECKSUM_INTERVAL_MS")) { 
				cfg.checksum_interval_ms = static_cast<uint32_t>(std::atoi(ci)); 
			}
			transport = std::make_shared<transport::SharedMemoryTransportServer>(transportLog, cfg);
			transportLog->info("Using SharedMemoryTransportServer name={} size={}x{} checksum_interval_ms={}", cfg.shm_name, cfg.max_width, cfg.max_height, cfg.checksum_interval_ms);
		} else if (transportType == "socket") {
#if CALDERA_TRANSPORT_SOCKETS
			transport::SocketTransportServer::Config cfg;
			if (const char* ep = std::getenv("CALDERA_SOCKET_ENDPOINT")) cfg.endpoint = ep;
			if (const char* ci = std::getenv("CALDERA_SOCKET_CHECKSUM_INTERVAL_MS")) cfg.checksum_interval_ms = static_cast<uint32_t>(std::atoi(ci));
			transport = std::make_shared<transport::SocketTransportServer>(transportLog, cfg);
			transportLog->info("Using SocketTransportServer endpoint={} checksum_interval_ms={}", cfg.endpoint, cfg.checksum_interval_ms);
#else
			transportLog->error("Socket transport requested but disabled at build time (CALDERA_TRANSPORT_SOCKETS=OFF). Falling back to LocalTransportServer.");
			transport = std::make_shared<transport::LocalTransportServer>(transportLog, handshakeLog);
#endif
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
