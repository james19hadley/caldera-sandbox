#include "AppManager.h"

#include "hal/HAL_Manager.h"
#include "processing/ProcessingManager.h"
#include "transport/ITransportServer.h"

namespace caldera::backend {

AppManager::AppManager(std::shared_ptr<spdlog::logger> lifecycleLogger,
		   std::shared_ptr<hal::HAL_Manager> hal,
		   std::shared_ptr<processing::ProcessingManager> processing,
		   std::shared_ptr<transport::ITransportServer> transport)
	: lifecycleLogger_(std::move(lifecycleLogger)),
	  hal_(std::move(hal)),
	  processing_(std::move(processing)),
	  transport_(std::move(transport))
{
	// Wire callbacks: HAL depth frames -> Processing -> Transport
	hal_->setDepthFrameCallback([proc = processing_](const caldera::backend::common::RawDepthFrame& f){ proc->processRawDepthFrame(f); });
	processing_->setWorldFrameCallback([srv = transport_](const caldera::backend::common::WorldFrame& frame){ srv->sendWorldFrame(frame); });
	lifecycleLogger_->info("AppManager pipeline wired (HAL -> Processing -> Transport)");
}

void AppManager::start() {
	if (running_) return;
	lifecycleLogger_->info("Starting backend subsystems");
	transport_->start();
	hal_->start();
	running_ = true;
}

void AppManager::stop() {
	if (!running_) return;
	lifecycleLogger_->info("Stopping backend subsystems");
	hal_->stop();
	transport_->stop();
	running_ = false;
}

} // namespace caldera::backend
