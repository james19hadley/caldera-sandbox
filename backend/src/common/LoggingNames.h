#ifndef CALDERA_BACKEND_COMMON_LOGGING_NAMES_H
#define CALDERA_BACKEND_COMMON_LOGGING_NAMES_H

// Central place for canonical logger name strings.
// Keep names stable for tooling / filtering.
namespace caldera::backend::logging_names {
// Application / lifecycle
constexpr const char* APP_LIFECYCLE = "App.Lifecycle";
constexpr const char* APP_CONFIG    = "App.Config";

// HAL layer
constexpr const char* HAL_MANAGER    = "HAL.Manager";
constexpr const char* HAL_UDP        = "HAL.UdpListener";
constexpr const char* HAL_DEVICE     = "HAL.Device"; // generic placeholder

// Processing pipeline
constexpr const char* PROC_ORCH      = "Processing.Orchestrator";
constexpr const char* PROC_CALIB     = "Processing.Calibration";
constexpr const char* PROC_FILTER    = "Processing.Filtering";
constexpr const char* PROC_FUSION    = "Processing.Fusion";
constexpr const char* PROC_ANALYSIS  = "Processing.Analysis";

// Transport
constexpr const char* TRANSPORT_SERVER   = "Transport.Server";
constexpr const char* TRANSPORT_HANDSHAKE= "Transport.Handshake";

// Misc
constexpr const char* TEST      = "Test";
}

#endif // CALDERA_BACKEND_COMMON_LOGGING_NAMES_H
