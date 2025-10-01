#include "AppManager.h"

#include "hal/ISensorDevice.h"
#include "processing/ProcessingManager.h"
#include "transport/ITransportServer.h"

namespace caldera::backend {

AppManager::AppManager(std::shared_ptr<spdlog::logger> lifecycleLogger,
	   std::unique_ptr<hal::ISensorDevice> device,
	   std::shared_ptr<processing::ProcessingManager> processing,
	   std::shared_ptr<transport::ITransportServer> transport)
	: lifecycleLogger_(std::move(lifecycleLogger)),
	  device_(std::move(device)),
	  processing_(std::move(processing)),
	  transport_(std::move(transport))
{
	// Wire callbacks: Device frames -> Processing -> Transport
	// ISensorDevice delivers both depth & color; current pipeline only uses depth
	device_->setFrameCallback([proc = processing_](const caldera::backend::common::RawDepthFrame& depth,
						    const caldera::backend::common::RawColorFrame& /*color*/) {
		proc->processRawDepthFrame(depth);
	});
	processing_->setWorldFrameCallback([srv = transport_](const caldera::backend::common::WorldFrame& frame){ srv->sendWorldFrame(frame); });
	lifecycleLogger_->info("AppManager pipeline wired (Device -> Processing -> Transport)");
}

void AppManager::start() {
	if (running_) return;
	lifecycleLogger_->info("Starting backend subsystems");
	transport_->start();
	if (!device_->open()) {
		lifecycleLogger_->error("Failed to open sensor device; pipeline will not produce frames");
	}
	running_ = true;
}

void AppManager::stop() {
	if (!running_) return;
	lifecycleLogger_->info("Stopping backend subsystems");
	device_->close();
	transport_->stop();
	running_ = false;
}

} // namespace caldera::backend
