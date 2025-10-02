#!/bin/bash
# Caldera Backend Build Script
#
# First-time setup (for depth sensor hardware access):
#   ./sensor_setup.sh              # Auto-detect and setup all sensors
#   ./sensor_setup.sh setup        # Manual setup  
#   ./sensor_setup.sh test         # Test sensor connections
#
# Build the project:
#   ./build.sh                    # Clean build all targets
#   ./build.sh -i                 # Incremental build all targets
#   ./build.sh SensorViewer       # Build specific target
#   ./build.sh -i SensorBackend CalderaTests  # Incremental build specific targets
#
# Available targets: SensorBackend, SensorViewer, CalderaTests, CalderaHeavyTests
#
# Run tests:
#   ./test.sh
#
# Check sensor connections:
#   ./sensor_setup.sh check        # Quick sensor check
#   ./sensor_setup.sh test         # Test sensor access
#
# View live sensor data (after build):
#   ./build/SensorViewer           # View live depth/color data (auto-detect sensor)
#   ./build/SensorViewer -t 10     # View for 10 seconds

# Exit immediately if a command exits with a non-zero status.
set -e

# Use all available CPU cores for VCPKG package builds to speed up the process
NPROC=$(nproc)
export VCPKG_MAX_CONCURRENCY=$NPROC

# Resolve script directory to allow calling from repo root or backend/ directly
SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
cd "$SCRIPT_DIR"

# VCPKG_ROOT requirement (explicit by default).
# Opt-in auto-detect only if CALDERA_AUTODETECT_VCPKG=1 is exported.
if [ -z "${VCPKG_ROOT:-}" ]; then
	if [ "${CALDERA_AUTODETECT_VCPKG:-0}" = "1" ] && [ -x "$HOME/vcpkg/vcpkg" ]; then
		export VCPKG_ROOT="$HOME/vcpkg"
		echo "[build.sh] CALDERA_AUTODETECT_VCPKG=1 -> using detected VCPKG_ROOT=$VCPKG_ROOT"
	else
		echo "[build.sh] Error: VCPKG_ROOT is not set."
		echo "           Export it, e.g.: export VCPKG_ROOT=~/vcpkg"
		echo "           (Set CALDERA_AUTODETECT_VCPKG=1 to allow fallback to ~/vcpkg if it exists.)"
		exit 1
	fi
fi

# Set the build directory. All build artifacts will be placed here.
BUILD_DIR="build"

# Parse arguments before any destructive actions
INCREMENTAL=false
TARGETS=()

while [[ $# -gt 0 ]]; do
	case "$1" in
		-i|--incremental)
			INCREMENTAL=true; shift ;;
		-h|--help)
			echo "Usage: $0 [-i|--incremental] [TARGET1] [TARGET2] ..."
			echo ""
			echo "Options:"
			echo "  -i, --incremental    Don't clean build directory"
			echo "  -h, --help          Show this help"
			echo ""
			echo "Available targets:"
			echo "  SensorBackend       Main application"
			echo "  SensorViewer        Multi-sensor viewer utility"

			echo "  CalderaTests        Test suite (regular)"
			echo "  CalderaHeavyTests   Heavy/stress & benchmark tests"
			echo ""
			echo "Examples:"
			echo "  $0                           # Build all targets (clean)"
			echo "  $0 -i                        # Build all targets (incremental)"  
			echo "  $0 SensorViewer              # Build only SensorViewer (clean)"
			echo "  $0 -i SensorBackend CalderaTests  # Build specific targets (incremental)"
			exit 0 ;;
		SensorBackend|SensorViewer|CalderaTests|CalderaHeavyTests)
			TARGETS+=("$1"); shift ;;
		*) 
			echo "Error: Unknown option or target: $1"
			echo "Available targets: SensorBackend, SensorViewer, CalderaTests, CalderaHeavyTests"
			echo "Use -h for help"
			exit 1 ;;
	esac
done

if [ "$INCREMENTAL" = false ]; then
	echo "--- Fresh build: recreating $BUILD_DIR ---"
	rm -rf "$BUILD_DIR"
else
	echo "--- Incremental build: using existing $BUILD_DIR (no clean) ---"
fi
mkdir -p "$BUILD_DIR"

# --- Auto-build vendored libfreenect (Kinect v1) if present and not built ---
VENDOR_FREENECT_ROOT="${SCRIPT_DIR}/../vendor/libfreenect"
if [ -d "$VENDOR_FREENECT_ROOT" ]; then
	FREENECT_LIB_DIR="${VENDOR_FREENECT_ROOT}/build/lib"
	FREENECT_LIB_FILE="${FREENECT_LIB_DIR}/libfreenect.a"
	if [ ! -f "$FREENECT_LIB_FILE" ] && [ ! -f "${FREENECT_LIB_DIR}/libfreenect.so" ]; then
		echo "--- Building vendored libfreenect (v1) ---"
		( cd "$VENDOR_FREENECT_ROOT" && \
			mkdir -p build && cd build && \
			cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=OFF -DBUILD_FAKENECT=OFF -DBUILD_OPENNI2_DRIVER=OFF .. && \
			make -j "$NPROC" )
		if [ ! -f "$FREENECT_LIB_FILE" ] && [ ! -f "${FREENECT_LIB_DIR}/libfreenect.so" ]; then
			echo "Error: Failed to build vendored libfreenect" >&2
			exit 1
		fi
		echo "--- Vendored libfreenect build complete ---"
	else
		echo "--- Vendored libfreenect already built ---"
	fi
else
	echo "[build.sh] Note: vendored libfreenect not present at $VENDOR_FREENECT_ROOT (skipping)"
fi

# 2. Configure the project using CMake.
echo "--- Configuring project ---"

# Forward optional feature flags to CMake
EXTRA_CMAKE_FLAGS=()
if [ "${CALDERA_TRANSPORT_SOCKETS:-0}" = "1" ] || [ "${CALDERA_TRANSPORT_SOCKETS:-OFF}" = "ON" ]; then
	echo "[build.sh] Enabling socket transport (CALDERA_TRANSPORT_SOCKETS=ON)"
	EXTRA_CMAKE_FLAGS+=( -DCALDERA_TRANSPORT_SOCKETS=ON )
else
	EXTRA_CMAKE_FLAGS+=( -DCALDERA_TRANSPORT_SOCKETS=OFF )
fi

# Optional: enable AddressSanitizer (memory error detection)
if [ "${CALDERA_ENABLE_ASAN:-0}" = "1" ] || [ "${CALDERA_ENABLE_ASAN:-OFF}" = "ON" ]; then
    echo "[build.sh] AddressSanitizer ENABLED (CALDERA_ENABLE_ASAN=ON)"
    EXTRA_CMAKE_FLAGS+=( -DCALDERA_ENABLE_ASAN=ON )
else
    EXTRA_CMAKE_FLAGS+=( -DCALDERA_ENABLE_ASAN=OFF )
fi

cmake -B $BUILD_DIR -S . -DCALDERA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake "${EXTRA_CMAKE_FLAGS[@]}"

# 3. Build the project (using all CPU cores)
if [ ${#TARGETS[@]} -eq 0 ]; then
	echo "--- Building all targets (using $NPROC cores) ---"
	cmake --build $BUILD_DIR -j $NPROC
else
	echo "--- Building targets: ${TARGETS[*]} (using $NPROC cores) ---"
	for target in "${TARGETS[@]}"; do
		echo "Building target: $target"
		cmake --build $BUILD_DIR --target "$target" -j $NPROC
	done
fi

echo "--- Build complete ---"
echo "Built executables in $BUILD_DIR/"
echo "Run tests: ./test.sh"
