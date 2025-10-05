##############################
# Caldera Tests (Categorized)
##############################

add_executable(CalderaTests
    test_main.cpp
    # logger
    logger/test_logger_basic.cpp
    logger/test_logger_levels.cpp
    logger/test_logger_concurrency.cpp
    logger/test_logger_ratelimit.cpp
    # pipeline
    pipeline/test_pipeline_basic.cpp
    pipeline/test_pipeline_failures.cpp
    pipeline/test_pipeline_frame_id_monotonic.cpp
    # processing
    processing/test_processing_conversion.cpp
    processing/test_processing_shm_negatives.cpp
    processing/test_FastGaussianBlur.cpp
    processing/test_DepthCorrector.cpp
    processing/test_CoordinateTransform.cpp
    processing/test_TemporalFilter.cpp
    processing/test_processing_invalid_pixels.cpp
    processing/test_fusion_passthrough.cpp
    processing/test_processing_plane_validation.cpp
    processing/test_processing_spatial_filter_impulse.cpp
    processing/test_processing_env_calibration_fallback.cpp
    processing/test_processing_profile_loading.cpp
    processing/test_processing_stability_metrics.cpp
    processing/test_processing_adaptive_spatial.cpp
    processing/test_processing_adaptive_spatial_phase2.cpp
    processing/test_processing_spatial_sampling_metric.cpp
    processing/test_processing_spatial_kernel_wide5.cpp
    processing/test_processing_adaptive_temporal_scale.cpp
    # shm
    shm/test_shm_reader.cpp
    shm/test_shm_overflow.cpp
    shm/test_shm_integrity.cpp
    shm/test_shm_latency.cpp
    shm/test_shm_extended.cpp
    shm/test_shm_realistic_fps.cpp
    shm/test_shm_stats.cpp
    shm/test_shm_verified_matrix.cpp
    # transport
    transport/test_transport_handshake.cpp
    transport/test_transport_handshake_stats.cpp
    transport/test_transport_health.cpp
    # sensor
    sensor/test_sensor_kinectv2_device.cpp
    sensor/test_sensor_recording.cpp
    sensor/test_sensor_kinectv1_device.cpp
    sensor/test_sensor_mock_negative.cpp
    # integration
    integration/test_synthetic_sensor_pass_through.cpp
    integration/test_processing_scale_semantics.cpp
    integration/test_transport_latency.cpp
    integration/test_fault_injection_sensor_pause.cpp
    integration/test_fault_injection_jitter_drop.cpp
    integration/test_pipeline_throughput.cpp
    integration/test_transport_midstream_attach.cpp
    integration/test_pipeline_metrics.cpp
    integration/test_process_shm_blackbox.cpp
    integration/test_worldframe_client_shm.cpp
    # memory leak tests
    memory/test_memory_leaks.cpp
    memory/test_hal_memory.cpp
    memory/test_memory_hal_processing_integration.cpp
    memory/test_memory_processing_transport_integration.cpp
    memory/test_memory_full_pipeline_integration.cpp
    memory/test_processing_memory.cpp
    memory/test_memory_stress.cpp
    memory/test_memory_extended_runtime.cpp
    memory/test_memory_pressure.cpp
    # helpers (non-test utilities referenced by tests)
    helpers/TestLocalTransportClient.cpp
    helpers/TestCalderaClient.cpp
    # Disabled (require hardware / pending fixes)
    # sensor/test_sensor_kinect_data_viewer.cpp
    # sensor/test_kinect_data_validation.cpp
)

# Optional socket parity test
if (CALDERA_TRANSPORT_SOCKETS)
    target_sources(CalderaTests PRIVATE integration/test_transport_socket_parity.cpp)
endif()

# Heavy / performance / stress tests
add_executable(CalderaHeavyTests
    test_main.cpp
    performance/test_performance_processing_stress.cpp
    performance/test_performance_pipeline_robust.cpp
    performance/test_performance_shm_benchmark.cpp
    performance/test_performance_shm_throughput_verified.cpp
    # helpers used by performance tests
    helpers/TestCalderaClient.cpp
)

# Let CTest discover individual GoogleTest test cases instead of a single umbrella test.
include(GoogleTest)

# Link with gtest and production sources (header-only parts picked via include paths)
find_package(GTest CONFIG REQUIRED)

# We reuse the logger implementation object file by linking the library objects via target link (simplest for now)
# Alternatively, we could factor SensorBackend sources into a static lib.

# Include source directories for headers
target_include_directories(CalderaTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/helpers
)

# Link libraries
# gtest::gtest gtest::gtest_main available when using vcpkg config package
# We link spdlog too because tests use logger implementation.
find_package(spdlog CONFIG REQUIRED)

target_link_libraries(CalderaTests PRIVATE GTest::gtest GTest::gtest_main caldera_backend_core)
target_link_libraries(CalderaHeavyTests PRIVATE GTest::gtest GTest::gtest_main caldera_backend_core)

gtest_discover_tests(CalderaTests WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR} DISCOVERY_TIMEOUT 30)
gtest_discover_tests(CalderaHeavyTests WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR} DISCOVERY_TIMEOUT 60)

target_compile_definitions(CalderaTests PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

set_target_properties(CalderaTests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
set_target_properties(CalderaHeavyTests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)

# Include source directories for headers in heavy tests as well
target_include_directories(CalderaHeavyTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/helpers
)
