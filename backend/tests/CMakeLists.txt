# CMake configuration for Caldera backend tests

add_executable(CalderaTests
    test_main.cpp
    test_logger_basic.cpp
    test_logger_levels.cpp
    test_pipeline_basic.cpp
    test_processing_conversion.cpp
    test_logger_concurrency.cpp
    test_frame_id_monotonic.cpp
    test_shm_reader.cpp
    test_shm_overflow.cpp
    test_logger_ratelimit.cpp
    test_kinectv2_device.cpp
    test_sensor_recording.cpp
    test_pipeline_failures.cpp
    test_shm_and_processing_negatives.cpp
    test_transport_handshake.cpp
    test_shm_integrity.cpp
    test_shm_latency.cpp
    test_shm_extended.cpp
    test_shm_realistic_fps.cpp
    test_shm_stats.cpp
    test_shm_verified_matrix.cpp
    test_transport_health.cpp
    test_transport_handshake_stats.cpp
    test_integration_phase0_basic.cpp
    TestLocalTransportClient.cpp
    # test_kinect_data_viewer.cpp       # Disabled due to compilation issues
    # test_kinect_data_validation.cpp   # Disabled due to compilation issues
)

# Heavy / longer running tests separated to allow fast default cycle
add_executable(CalderaHeavyTests
    test_main.cpp
    test_processing_stress.cpp
    test_pipeline_robust.cpp
    test_shm_benchmark.cpp
    test_shm_throughput_verified.cpp
)

# Let CTest discover individual GoogleTest test cases instead of a single umbrella test.
include(GoogleTest)

# Link with gtest and production sources (header-only parts picked via include paths)
find_package(GTest CONFIG REQUIRED)

# We reuse the logger implementation object file by linking the library objects via target link (simplest for now)
# Alternatively, we could factor SensorBackend sources into a static lib.

# Include source directories for headers
target_include_directories(CalderaTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

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
