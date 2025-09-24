cmake_minimum_required(VERSION 3.18)
project(CalderaBackend)

# --- Find Dependencies ---
find_package(spdlog CONFIG REQUIRED)

# --- Project Sources ---
add_executable(SensorBackend
    src/main.cpp
    src/common/Logger.cpp
)

target_include_directories(SensorBackend PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# --- Linking ---
target_link_libraries(SensorBackend PRIVATE
    spdlog::spdlog
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)