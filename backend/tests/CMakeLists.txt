# CMake configuration for Caldera backend tests

add_executable(CalderaTests
    test_main.cpp
    test_logger_basic.cpp
    test_logger_levels.cpp
)

# Enable CTest integration (optional)
include(CTest)
add_test(NAME CalderaTests COMMAND CalderaTests)

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

target_link_libraries(CalderaTests PRIVATE
    GTest::gtest
    GTest::gtest_main
    caldera_backend_core
)

target_compile_definitions(CalderaTests PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

set_target_properties(CalderaTests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
