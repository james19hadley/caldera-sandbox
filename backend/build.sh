#!/bin/bash
# Exit immediately if a command exits with a non-zero status.
set -e

# Set the build directory. All build artifacts will be placed here.
BUILD_DIR="build"

# Parse arguments before any destructive actions
INCREMENTAL=false
while [[ $# -gt 0 ]]; do
	case "$1" in
		-i|--incremental)
			INCREMENTAL=true; shift ;;
		-h|--help)
			echo "Usage: $0 [-i|--incremental]"; exit 0 ;;
		*) echo "Unknown option: $1"; exit 1 ;;
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

# 3. Build the project.
echo "--- Building project ---"
cmake --build $BUILD_DIR --target CalderaTests
echo "--- Build complete ---"
echo "Run tests: ./test.sh"
