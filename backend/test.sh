#!/bin/bash
set -e
BUILD_DIR="build"
if [ ! -d "$BUILD_DIR" ]; then
  echo "Build directory not found. Run ./build.sh first." >&2
  exit 1
fi
TEST_BIN="${BUILD_DIR}/CalderaTests"
if [ ! -f "$TEST_BIN" ]; then
  # Some generators (e.g., multi-config or layout differences) may place it in a subdir
  if [ -f "${BUILD_DIR}/tests/CalderaTests" ]; then
    TEST_BIN="${BUILD_DIR}/tests/CalderaTests"
  else
    echo "Test binary not found. Building tests..."
    ./build.sh --incremental
  fi
fi

if [ ! -f "$TEST_BIN" ]; then
  # Re-evaluate after build
  if [ -f "${BUILD_DIR}/CalderaTests" ]; then
    TEST_BIN="${BUILD_DIR}/CalderaTests"
  elif [ -f "${BUILD_DIR}/tests/CalderaTests" ]; then
    TEST_BIN="${BUILD_DIR}/tests/CalderaTests"
  else
    echo "Failed to locate CalderaTests after build." >&2
    exit 2
  fi
fi

"$TEST_BIN" "$@"
