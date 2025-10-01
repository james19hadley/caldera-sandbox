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
#   ./build.sh KinectViewer       # Build specific target
#   ./build.sh -i SensorBackend CalderaTests  # Incremental build specific targets
#
# Available targets: SensorBackend, KinectViewer, CalderaTests
#
# Run tests:
#   ./test.sh
#
# Check sensor connections:
#   ./sensor_setup.sh check        # Quick sensor check
#   ./sensor_setup.sh test         # Test sensor access
#
# View live Kinect data (after build):
#   ./build/KinectViewer           # View live depth/color data
#   ./build/KinectViewer -t 10     # View for 10 seconds

# Exit immediately if a command exits with a non-zero status.
set -e

# Use all available CPU cores for VCPKG package builds to speed up the process
NPROC=$(nproc)
export VCPKG_MAX_CONCURRENCY=$NPROC

# Resolve script directory to allow calling from repo root or backend/ directly
SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
cd "$SCRIPT_DIR"

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
			echo "  KinectViewer        Kinect data viewer utility"

			echo "  CalderaTests        Test suite"
			echo ""
			echo "Examples:"
			echo "  $0                           # Build all targets (clean)"
			echo "  $0 -i                        # Build all targets (incremental)"  
			echo "  $0 KinectViewer              # Build only KinectViewer (clean)"
			echo "  $0 -i SensorBackend CalderaTests  # Build specific targets (incremental)"
			exit 0 ;;
		SensorBackend|KinectViewer|CalderaTests)
			TARGETS+=("$1"); shift ;;
		*) 
			echo "Error: Unknown option or target: $1"
			echo "Available targets: SensorBackend, KinectViewer, CalderaTests"
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

# 2. Configure the project using CMake.
echo "--- Configuring project ---"

cmake -B $BUILD_DIR -S . -DCALDERA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake

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
