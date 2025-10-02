cmake_minimum_required(VERSION 3.18)
project(CalderaBackend)

# --- Find Dependencies ---
find_package(spdlog CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)
find_package(freenect2 CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LibUSB REQUIRED libusb-1.0)
find_package(JPEG REQUIRED)
pkg_check_modules(VAAPI libva libva-drm)
find_package(libjpeg-turbo CONFIG REQUIRED)

option(CALDERA_TRANSPORT_SOCKETS "Enable socket transport (Unix domain sockets) support" OFF)
if (CALDERA_TRANSPORT_SOCKETS)
    message(STATUS "Socket transport: ENABLED")
else()
    message(STATUS "Socket transport: DISABLED (CALDERA_TRANSPORT_SOCKETS=OFF)")
endif()

option(CALDERA_WITH_KINECT_V1 "Enable Kinect v1 (libfreenect) support" ON)
if (CALDERA_WITH_KINECT_V1)
    message(STATUS "Kinect v1 support: ENABLED")
    # Manual libfreenect (Kinect v1) local build discovery
    find_path(FREENECT_INCLUDE_DIR
        NAMES libfreenect.h
        PATHS
            ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/libfreenect/include
            ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libfreenect/include
        NO_DEFAULT_PATH
    )

    find_library(FREENECT_LIBRARY
        NAMES freenect
        PATHS
            ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/libfreenect/build/lib
            ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/libfreenect/build/lib/Release
            ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/libfreenect/build/lib/Debug
            ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libfreenect/build/lib
        NO_DEFAULT_PATH
    )

    if (FREENECT_INCLUDE_DIR AND FREENECT_LIBRARY)
        message(STATUS "libfreenect(v1) include: ${FREENECT_INCLUDE_DIR}")
        message(STATUS "libfreenect(v1) library: ${FREENECT_LIBRARY}")
        set(HAVE_FREENECT_V1 TRUE)
    else()
        message(WARNING "libfreenect (v1) not found; Kinect v1 will build as stub (set CALDERA_WITH_KINECT_V1=OFF to silence).")
        set(HAVE_FREENECT_V1 FALSE)
    endif()
else()
    message(STATUS "Kinect v1 support: DISABLED (CALDERA_WITH_KINECT_V1=OFF)")
    set(HAVE_FREENECT_V1 FALSE)
endif()

# --- Project Sources ---
add_library(caldera_backend_core STATIC
    src/common/Logger.cpp
    src/common/Checksum.cpp
    src/hal/KinectV2_Device.cpp
    # KinectV1 source always compiled (file has internal stubs if libfreenect headers absent)
    src/hal/KinectV1_Device.cpp
    src/hal/SensorRecorder.cpp
    src/hal/MockSensorDevice.cpp
    src/hal/SyntheticSensorDevice.cpp
    src/processing/ProcessingManager.cpp
    src/processing/IHeightMapFilter.h
    src/transport/LocalTransportServer.cpp
    src/transport/HandshakeServer.cpp
    src/transport/SharedMemoryTransportServer.cpp
    src/transport/SharedMemoryReader.cpp
    src/transport/SharedMemoryWorldFrameClient.cpp
    src/transport/FifoManager.cpp
    src/AppManager.cpp
)

# Socket transport sources (optional)
if (CALDERA_TRANSPORT_SOCKETS)
    target_sources(caldera_backend_core PRIVATE
        src/transport/SocketTransportServer.cpp
        src/transport/SocketWorldFrameClient.cpp
    )
endif()

target_include_directories(caldera_backend_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${freenect2_INCLUDE_DIRS}
)
if (HAVE_FREENECT_V1)
    target_include_directories(caldera_backend_core PUBLIC ${FREENECT_INCLUDE_DIR})
    target_compile_definitions(caldera_backend_core PUBLIC CALDERA_HAVE_KINECT_V1=1)
else()
    target_compile_definitions(caldera_backend_core PUBLIC CALDERA_HAVE_KINECT_V1=0)
endif()

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
if (HAVE_FREENECT_V1)
    target_link_libraries(caldera_backend_core PUBLIC ${FREENECT_LIBRARY})
endif()
target_compile_definitions(caldera_backend_core PUBLIC SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

# Propagate CALDERA_TRANSPORT_SOCKETS to dependents
if (CALDERA_TRANSPORT_SOCKETS)
    target_compile_definitions(caldera_backend_core PUBLIC CALDERA_TRANSPORT_SOCKETS=1)
else()
    target_compile_definitions(caldera_backend_core PUBLIC CALDERA_TRANSPORT_SOCKETS=0)
endif()

add_executable(SensorBackend
    src/main.cpp
)

target_include_directories(SensorBackend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(SensorBackend PRIVATE caldera_backend_core)

# Ensure SensorBackend also sees the sockets flag
if (CALDERA_TRANSPORT_SOCKETS)
    target_compile_definitions(SensorBackend PRIVATE CALDERA_TRANSPORT_SOCKETS=1)
else()
    target_compile_definitions(SensorBackend PRIVATE CALDERA_TRANSPORT_SOCKETS=0)
endif()

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