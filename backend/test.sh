#!/bin/bash
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

# If user wants ctest style discovery (e.g. pattern matching) they can pass --ctest [extra-args]
if [ "${1:-}" = "--ctest" ]; then
  shift
  echo -e "${GREEN}Running via ctest (individual test registration)${RESET}"
  (cd "$BUILD_DIR" && ctest --output-on-failure "$@")
  exit 0
fi

# Force gtest color when terminal
if [ -t 1 ]; then
  export GTEST_COLOR=1
fi

RUN_HEAVY=0
EXTRA_ARGS=("$@")
for arg in "$@"; do
  if [ "$arg" = "--heavy" ]; then
    RUN_HEAVY=1
    # Remove from args passed to gtest
    EXTRA_ARGS=( )
    for a2 in "$@"; do [ "$a2" != "--heavy" ] && EXTRA_ARGS+=("$a2"); done
    break
  fi
done

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
  if [ $FILTER_PROVIDED -eq 0 ]; then
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
