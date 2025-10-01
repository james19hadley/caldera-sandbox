// Orchestrator for backend pipeline.

#ifndef CALDERA_BACKEND_APP_MANAGER_H
#define CALDERA_BACKEND_APP_MANAGER_H

#include <memory>

#include <spdlog/logger.h>

#include "common/DataTypes.h"

namespace caldera::backend::hal { class ISensorDevice; }
namespace caldera::backend::processing { class ProcessingManager; }
namespace caldera::backend::transport { class ITransportServer; }

namespace caldera::backend {

class AppManager {
public:
	AppManager(std::shared_ptr<spdlog::logger> lifecycleLogger,
		   std::unique_ptr<hal::ISensorDevice> device,
		   std::shared_ptr<processing::ProcessingManager> processing,
		   std::shared_ptr<transport::ITransportServer> transport);

	void start();
	void stop();

private:
	std::shared_ptr<spdlog::logger> lifecycleLogger_;
	std::unique_ptr<hal::ISensorDevice> device_;
	std::shared_ptr<processing::ProcessingManager> processing_;
	std::shared_ptr<transport::ITransportServer> transport_;
	bool running_ = false;
};

} // namespace caldera::backend

#endif // CALDERA_BACKEND_APP_MANAGER_H
