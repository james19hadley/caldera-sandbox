cmake_minimum_required(VERSION 3.18)
project(CalderaBackend)

# Optional sanitizers for debugging and memory checking
option(CALDERA_ENABLE_ASAN "Enable AddressSanitizer for memory error detection" OFF)
option(CALDERA_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF) 
option(CALDERA_ENABLE_TSAN "Enable ThreadSanitizer for data race detection" OFF)
option(CALDERA_ENABLE_MSAN "Enable MemorySanitizer for uninitialized memory detection" OFF)

# AddressSanitizer (detects buffer overflows, use-after-free, double-free, memory leaks)
if (CALDERA_ENABLE_ASAN)
    message(STATUS "AddressSanitizer: ENABLED")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address)
    # Enable leak detection by default with ASan
    set(ENV{ASAN_OPTIONS} "detect_leaks=1:abort_on_error=1:check_initialization_order=1")
endif()

# UndefinedBehaviorSanitizer (detects undefined behavior like integer overflow, null pointer dereference)
if (CALDERA_ENABLE_UBSAN)
    message(STATUS "UndefinedBehaviorSanitizer: ENABLED")
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=undefined)
endif()

# ThreadSanitizer (detects data races in multi-threaded code) - mutually exclusive with ASan
if (CALDERA_ENABLE_TSAN)
    if (CALDERA_ENABLE_ASAN)
        message(FATAL_ERROR "ThreadSanitizer and AddressSanitizer are mutually exclusive")
    endif()
    message(STATUS "ThreadSanitizer: ENABLED") 
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=thread)
endif()

# MemorySanitizer (detects uninitialized memory reads) - requires special libc++
if (CALDERA_ENABLE_MSAN)
    if (CALDERA_ENABLE_ASAN OR CALDERA_ENABLE_TSAN)
        message(FATAL_ERROR "MemorySanitizer is mutually exclusive with other sanitizers")
    endif()
    message(STATUS "MemorySanitizer: ENABLED")
    add_compile_options(-fsanitize=memory -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=memory)
endif()

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

    # Fallback to system libfreenect via pkg-config if vendored not found
    if (NOT FREENECT_INCLUDE_DIR OR NOT FREENECT_LIBRARY)
        pkg_check_modules(FREENECT_PKG freenect)
        if (FREENECT_PKG_FOUND)
            message(STATUS "Using system libfreenect via pkg-config")
            set(FREENECT_INCLUDE_DIR ${FREENECT_PKG_INCLUDE_DIRS})
            if (NOT FREENECT_LIBRARY)
                find_library(FREENECT_LIBRARY NAMES freenect PATHS ${FREENECT_PKG_LIBRARY_DIRS})
            endif()
        endif()
    endif()

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
    src/processing/FastGaussianBlur.cpp
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

## Sensor Viewer Tool (multi-sensor viewer utility)
add_executable(SensorViewer
    src/tools/sensor_viewer_main.cpp
    src/tools/SensorViewerCore.cpp
    src/tools/SimpleViewer.cpp
    src/tools/VideoWindow.cpp
    src/tools/SensorEnumerator.cpp
)

target_include_directories(SensorViewer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Find OpenGL and GLFW for video windows
find_package(OpenGL REQUIRED)
find_package(glfw3 CONFIG REQUIRED)

target_link_libraries(SensorViewer PRIVATE 
    caldera_backend_core 
    OpenGL::GL 
    glfw
)


# Kinect v1 control CLI (LED/Tilt) using vendored libfreenect
add_executable(KinectV1Ctl
    src/tools/kinectv1_ctl.cpp
)
target_include_directories(KinectV1Ctl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(KinectV1Ctl PRIVATE caldera_backend_core)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Valgrind support for memory leak detection
option(CALDERA_ENABLE_VALGRIND "Enable Valgrind memory checking in tests" OFF)
if (CALDERA_ENABLE_VALGRIND)
    find_program(VALGRIND_EXECUTABLE valgrind)
    if (VALGRIND_EXECUTABLE)
        message(STATUS "Valgrind found: ${VALGRIND_EXECUTABLE}")
        set(VALGRIND_OPTIONS 
            --tool=memcheck
            --leak-check=full
            --show-leak-kinds=all  
            --track-origins=yes
            --error-exitcode=1
            --suppressions=${CMAKE_CURRENT_SOURCE_DIR}/valgrind.supp
        )
    else()
        message(WARNING "Valgrind not found, memory checking disabled")
        set(CALDERA_ENABLE_VALGRIND OFF)
    endif()
endif()

option(CALDERA_BUILD_TESTS "Build Caldera backend tests" ON)
if (CALDERA_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()