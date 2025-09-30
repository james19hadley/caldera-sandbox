cmake_minimum_required(VERSION 3.18)
project(CalderaBackend)

# --- Find Dependencies ---
find_package(spdlog CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)
find_package(freenect2 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LibUSB REQUIRED libusb-1.0)
find_package(JPEG REQUIRED)
pkg_check_modules(VAAPI libva libva-drm)
find_package(libjpeg-turbo CONFIG REQUIRED)

# --- Project Sources ---
add_library(caldera_backend_core STATIC
    src/common/Logger.cpp
    src/hal/HAL_Manager.cpp
    src/hal/KinectV2_Device.cpp
    src/hal/SensorRecorder.cpp
    src/hal/MockSensorDevice.cpp
    src/processing/ProcessingManager.cpp
    src/transport/LocalTransportServer.cpp
    src/transport/SharedMemoryTransportServer.cpp
    src/transport/SharedMemoryReader.cpp
    src/AppManager.cpp
)

target_include_directories(caldera_backend_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${freenect2_INCLUDE_DIRS}
)

target_link_libraries(caldera_backend_core PUBLIC
    spdlog::spdlog
    ${freenect2_LIBRARIES}
    OpenGL::GL
    glfw
    ${LibUSB_LIBRARIES}
    ${JPEG_LIBRARY}
    ${VAAPI_LIBRARIES}
    libjpeg-turbo::turbojpeg-static
)
target_compile_definitions(caldera_backend_core PUBLIC SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

add_executable(SensorBackend
    src/main.cpp
)

target_include_directories(SensorBackend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(SensorBackend PRIVATE caldera_backend_core)

# Kinect Data Viewer Tool
add_executable(KinectViewer
    src/tools/kinect_viewer_main.cpp
    src/tools/KinectDataViewer.cpp
    src/tools/SimpleViewer.cpp
    src/tools/VideoWindow.cpp
)

target_include_directories(KinectViewer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Find OpenGL and GLFW for video windows
find_package(OpenGL REQUIRED)
find_package(glfw3 CONFIG REQUIRED)

target_link_libraries(KinectViewer PRIVATE 
    caldera_backend_core 
    OpenGL::GL 
    glfw
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(CALDERA_BUILD_TESTS "Build Caldera backend tests" ON)
if (CALDERA_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()