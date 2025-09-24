#include "common/Logger.h"

#include <exception>
#include <iostream>

int main()
{
	using caldera::backend::common::Logger;

	// Initialize logger as the first action
	Logger::instance().initialize("/tmp/caldera.log");

	try {
		auto root = Logger::instance().get("main");
		root->info("Application starting");
		root->debug("Debugging startup sequence");
		root->warn("This is a sample warning");

		// ... application logic goes here ...

		root->error("This is a sample error message (for demonstration)");
	} catch (const std::exception& ex) {
		Logger::instance().get("main")->error(std::string("Uncaught exception: ") + ex.what());
		return 1;
	} catch (...) {
		Logger::instance().get("main")->error("Uncaught non-standard exception");
		return 2;
	}

	// Shutdown logger before exiting
	Logger::instance().shutdown();

	return 0;
}
