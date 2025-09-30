cmake_minimum_required(VERSION 3.18)
project(CalderaBackend)

# --- Find Dependencies ---
find_package(spdlog CONFIG REQUIRED)

# --- Project Sources ---
add_library(caldera_backend_core STATIC
    src/common/Logger.cpp
    src/hal/HAL_Manager.cpp
    src/processing/ProcessingManager.cpp
    src/transport/LocalTransportServer.cpp
    src/AppManager.cpp
)

target_include_directories(caldera_backend_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(caldera_backend_core PUBLIC
    spdlog::spdlog
)
target_compile_definitions(caldera_backend_core PUBLIC SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

add_executable(SensorBackend
    src/main.cpp
)

target_include_directories(SensorBackend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(SensorBackend PRIVATE caldera_backend_core)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(CALDERA_BUILD_TESTS "Build Caldera backend tests" ON)
if (CALDERA_BUILD_TESTS)
    add_subdirectory(tests)
endif()