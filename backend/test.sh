#!/bin/bash
# Caldera Backend Test Script
#
# Run all tests:
#   ./test.sh                    # Run light tests (excluding Stress)
#   ./test.sh --heavy            # Run heavy tests
#   ./test.sh --all              # Run all tests (light + heavy)
#
# Run specific tests:
#   ./test.sh LoggerBasic        # Run specific test suite
#   ./test.sh LoggerBasic.InitializeAndGet  # Run specific test
#   ./test.sh Logger*            # Run tests matching pattern
#
# Advanced options:
#   ./test.sh --ctest            # Use ctest instead of direct gtest
#   ./test.sh --list             # List available tests
#   ./test.sh --help             # Show help
#
# Available test suites: LoggerBasic, LoggerLevelsFixture, PipelineBasic, 
#                       ProcessingConversion, LoggerConcurrency, FrameId, SharedMemory

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

GREEN="\033[32m"
RED="\033[31m"
YELLOW="\033[33m"
RESET="\033[0m"

if [ ! -d "$BUILD_DIR" ]; then
  echo -e "${YELLOW}Build directory not found. Running fresh build...${RESET}" >&2
  (cd "$SCRIPT_DIR" && ./build.sh)
fi

TEST_BIN="${BUILD_DIR}/CalderaTests"
if [ ! -f "$TEST_BIN" ]; then
  if [ -f "${BUILD_DIR}/tests/CalderaTests" ]; then
    TEST_BIN="${BUILD_DIR}/tests/CalderaTests"
  else
    echo -e "${YELLOW}Test binary not found. Building tests...${RESET}" >&2
  (cd "$SCRIPT_DIR" && ./build.sh --incremental)
  fi
fi

if [ ! -f "$TEST_BIN" ]; then
  if [ -f "${BUILD_DIR}/CalderaTests" ]; then
    TEST_BIN="${BUILD_DIR}/CalderaTests"
  elif [ -f "${BUILD_DIR}/tests/CalderaTests" ]; then
  TEST_BIN="${BUILD_DIR}/tests/CalderaTests"
  else
    echo -e "${RED}Failed to locate CalderaTests after build.${RESET}" >&2
    exit 2
  fi
fi

# Parse arguments and handle special cases
SHOW_HELP=0
LIST_TESTS=0
USE_CTEST=0
RUN_ALL_TESTS=0
SPECIFIC_TESTS=()
EXTRA_ARGS=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --help|-h)
      SHOW_HELP=1; shift ;;
    --list)
      LIST_TESTS=1; shift ;;
    --ctest)
      USE_CTEST=1; shift ;;
    --heavy)
      EXTRA_ARGS+=("$1"); shift ;;
    --all)
      RUN_ALL_TESTS=1; shift ;;
    --gtest_*)
      EXTRA_ARGS+=("$1"); shift ;;
    LoggerBasic*|LoggerLevelsFixture*|PipelineBasic*|ProcessingConversion*|LoggerConcurrency*|FrameId*|SharedMemory*|*Stress*)
      SPECIFIC_TESTS+=("$1"); shift ;;
    *)
      EXTRA_ARGS+=("$1"); shift ;;
  esac
done

if [ $SHOW_HELP -eq 1 ]; then
  echo "Caldera Backend Test Script"
  echo ""
  echo "Usage: $0 [OPTIONS] [TEST_PATTERNS...]"
  echo ""
  echo "Options:"
  echo "  --help, -h      Show this help"
  echo "  --list          List all available tests"
  echo "  --ctest         Use ctest instead of direct gtest"
  echo "  --heavy         Run heavy/stress tests"
  echo "  --all           Run all tests (light + heavy)"
  echo ""
  echo "Test Patterns:"
  echo "  LoggerBasic                    # Run entire test suite"
  echo "  LoggerBasic.InitializeAndGet   # Run specific test"
  echo "  Logger*                        # Run tests matching pattern"
  echo ""
  echo "Examples:"
  echo "  $0                           # Run light tests"
  echo "  $0 --heavy                   # Run heavy tests"
  echo "  $0 LoggerBasic               # Run Logger tests only"
  echo "  $0 --list                    # Show all available tests"
  exit 0
fi

if [ $LIST_TESTS -eq 1 ]; then
  echo -e "${GREEN}Available tests:${RESET}"
  "$TEST_BIN" --gtest_list_tests 2>/dev/null
  exit 0
fi

if [ $USE_CTEST -eq 1 ]; then
  echo -e "${GREEN}Running via ctest (individual test registration)${RESET}"
  (cd "$BUILD_DIR" && ctest --output-on-failure "${EXTRA_ARGS[@]}")
  exit 0
fi

# Force gtest color when terminal
if [ -t 1 ]; then
  export GTEST_COLOR=1
fi

# Check if heavy tests requested
RUN_HEAVY=0
for arg in "${EXTRA_ARGS[@]}"; do
  if [ "$arg" = "--heavy" ]; then
    RUN_HEAVY=1
    break
  fi
done

# Build test filter from specific tests
if [ ${#SPECIFIC_TESTS[@]} -gt 0 ]; then
  TEST_FILTER=""
  for test in "${SPECIFIC_TESTS[@]}"; do
    # Auto-add .* for suite names (no dot in name)
    if [[ "$test" != *.* ]] && [[ "$test" != *\** ]]; then
      test="$test.*"
    fi
    
    if [ -z "$TEST_FILTER" ]; then
      TEST_FILTER="$test"
    else
      TEST_FILTER="$TEST_FILTER:$test"
    fi
  done
  EXTRA_ARGS+=(--gtest_filter="$TEST_FILTER")
fi

if [ $RUN_HEAVY -eq 1 ]; then
  # Switch to heavy binary
  echo -e "${YELLOW}Building heavy tests target...${RESET}" >&2
  (cd "$SCRIPT_DIR/build" && cmake --build . --target CalderaHeavyTests >/dev/null 2>&1 || cmake --build . --target CalderaHeavyTests)
  if [ -f "${BUILD_DIR}/tests/CalderaHeavyTests" ]; then
    TEST_BIN="${BUILD_DIR}/tests/CalderaHeavyTests"
  elif [ -f "${BUILD_DIR}/CalderaHeavyTests" ]; then
    TEST_BIN="${BUILD_DIR}/CalderaHeavyTests"
  else
    echo -e "${YELLOW}Heavy tests binary not found; did CMake generate it?${RESET}" >&2
  fi
  echo -e "${GREEN}Running HEAVY tests binary:${RESET} $TEST_BIN" >&2
  set +e
  "$TEST_BIN" --gtest_color=yes "${EXTRA_ARGS[@]}"
else
  # Light tests: exclude Stress by default if user didn't supply custom filter
  FILTER_PROVIDED=0
  for a in "${EXTRA_ARGS[@]}"; do
    case $a in
      --gtest_filter=*) FILTER_PROVIDED=1;;
    esac
  done
  # If running all tests or user provided filter, don't add default exclusion
  if [ $FILTER_PROVIDED -eq 0 ] && [ $RUN_ALL_TESTS -eq 0 ]; then
    EXTRA_ARGS+=(--gtest_filter=-*Stress)
  fi
  echo -e "${GREEN}Running GoogleTest binary:${RESET} $TEST_BIN" >&2
  set +e
  "$TEST_BIN" --gtest_color=yes "${EXTRA_ARGS[@]}"
fi
STATUS=$?
set -e

if [ $STATUS -eq 0 ]; then
  echo -e "${GREEN}All tests passed.${RESET}"
else
  echo -e "${RED}Some tests failed (exit code $STATUS).${RESET}" >&2
fi

exit $STATUS
