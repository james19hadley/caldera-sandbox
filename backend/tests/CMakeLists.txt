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
    sensor/test_sensor_mock_negative.cpp
    # integration
    integration/test_synthetic_sensor_pass_through.cpp
    integration/test_processing_scale_semantics.cpp
    integration/test_transport_latency.cpp
    integration/test_fault_injection_sensor_pause.cpp
    integration/test_fault_injection_jitter_drop.cpp
    integration/test_pipeline_throughput.cpp
    # helpers (non-test utilities referenced by tests)
    helpers/TestLocalTransportClient.cpp
    # Disabled (require hardware / pending fixes)
    # sensor/test_sensor_kinect_data_viewer.cpp
    # sensor/test_kinect_data_validation.cpp
)

# Heavy / performance / stress tests
add_executable(CalderaHeavyTests
    test_main.cpp
    performance/test_performance_processing_stress.cpp
    performance/test_performance_pipeline_robust.cpp
    performance/test_performance_shm_benchmark.cpp
    performance/test_performance_shm_throughput_verified.cpp
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
