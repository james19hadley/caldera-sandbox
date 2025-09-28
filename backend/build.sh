#!/bin/bash
# Exit immediately if a command exits with a non-zero status.
set -e

# Set the build directory. All build artifacts will be placed here.
BUILD_DIR="build"

# 1. Clean and create the build directory.
echo "--- Preparing build directory: $BUILD_DIR ---"
rm -rf $BUILD_DIR
mkdir -p $BUILD_DIR

# 2. Configure the project using CMake.
echo "--- Configuring project ---"
cmake -B $BUILD_DIR -S . -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake

# 3. Build the project.
echo "--- Building project ---"
cmake --build $BUILD_DIR
