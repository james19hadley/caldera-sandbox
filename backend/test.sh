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
# Memory testing:
#   ./test.sh --memory-quick     # Quick memory tests only (no Extended Runtime)
#   ./test.sh --memory-asan      # Memory tests with AddressSanitizer
#   ./test.sh --memory-full      # Full memory tests (including long-running)
#
# Advanced options:
#   ./test.sh --ctest            # Use ctest instead of direct gtest
#   ./test.sh --list             # List available tests
#   ./test.sh --help             # Show help
#
# Available test suites: LoggerBasic, LoggerLevelsFixture, PipelineBasic, 
#                       ProcessingConversion, LoggerConcurrency, FrameId, SharedMemory,
#                       SensorRecording, KinectV2_DeviceTest, SharedMemoryRealisticFPS,
#                       SharedMemoryStats, MemoryLeakTest
#
# Available test categories (auto-discovered): logger, pipeline, processing, shm, transport, sensor, 
#                          integration, memory, performance
#                          Use --list to see which tests belong to each category
#
# Long-running memory tests (5+ minutes each) are skipped by default.
# To enable them: export CALDERA_ENABLE_LONG_MEMORY_TESTS=1

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

GREEN="\033[32m"
RED="\033[31m"
YELLOW="\033[33m"
RESET="\033[0m"

# Set default environment variables for more stable test execution
setup_test_environment() {
  # Set reasonable defaults for test stability (can be overridden by user)
  export CALDERA_STRICT_RECONNECT="${CALDERA_STRICT_RECONNECT:-0}"
  
  # Reduce log noise during tests - only show warnings and errors by default
  export CALDERA_LOG_LEVEL="${CALDERA_LOG_LEVEL:-warn}"  # Changed from 'error' to 'warn' for better balance
  export CALDERA_COMPACT_FRAME_LOG="${CALDERA_COMPACT_FRAME_LOG:-1}"
  
  # Disable frame-level tracing during tests to reduce noise
  export CALDERA_LOG_FRAME_TRACE_EVERY="${CALDERA_LOG_FRAME_TRACE_EVERY:-0}"
  
  # Quiet mode for synthetic sensors during tests
  export CALDERA_QUIET_MODE="${CALDERA_QUIET_MODE:-1}"
}

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
MEMORY_QUICK=0
MEMORY_ASAN=0
MEMORY_FULL=0
RUN_ALL_TESTS=0
SPECIFIC_TESTS=()
EXTRA_ARGS=()
CATEGORIES=()
EXCLUDE_PATTERNS=()

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
    --memory-quick)
      MEMORY_QUICK=1; shift ;;
    --memory-asan)
      MEMORY_ASAN=1; shift ;;
    --memory-full)
      MEMORY_FULL=1; shift ;;
    --gtest_*)
      EXTRA_ARGS+=("$1"); shift ;;
    logger|pipeline|processing|shm|transport|sensor|integration|memory|performance)
      CATEGORIES+=("$1"); shift ;;
    LoggerBasic*|LoggerLevelsFixture*|PipelineBasic*|ProcessingConversion*|LoggerConcurrency*|FrameId*|SharedMemory*|SensorRecording*|KinectV2_DeviceTest*|SharedMemoryRealisticFPS*|SharedMemoryStats*|MemoryLeakTest*|MemoryStressTest*|ExtendedRuntimeMemoryTest*|MemoryPressureTest*|*Stress*)
      SPECIFIC_TESTS+=("$1"); shift ;;
    *)
      # Treat any remaining bare argument as a test suite/pattern (e.g. SyntheticSensorPipeline)
      SPECIFIC_TESTS+=("$1"); shift ;;
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
  echo "  --memory-quick  Run quick memory tests (no ExtendedRuntime)"
  echo "  --memory-asan   Build with AddressSanitizer and run memory tests"
  echo "  --memory-full   Run full memory tests (including long-running)"
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
  echo "  $0 --memory-asan             # Memory tests with AddressSanitizer"
  echo "  $0 --memory-quick            # Quick memory validation"
  echo "  $0 --list                    # Show all available tests"
  exit 0
fi

if [ $LIST_TESTS -eq 1 ]; then
  echo -e "${GREEN}Available tests:${RESET}"
  "$TEST_BIN" --gtest_list_tests 2>/dev/null
  
  echo -e "\n${GREEN}Available categories (auto-discovered):${RESET}"
  AVAILABLE_TESTS=$("$TEST_BIN" --gtest_list_tests 2>/dev/null | grep -E '^\S+\.$' | tr -d '.')
  
  echo "  logger     - $(echo "$AVAILABLE_TESTS" | grep -c "^Logger") tests: $(echo "$AVAILABLE_TESTS" | grep "^Logger" | tr '\n' ' ')"
  echo "  pipeline   - $(echo "$AVAILABLE_TESTS" | grep -c -E "^(Pipeline|FrameId)") tests: $(echo "$AVAILABLE_TESTS" | grep -E "^(Pipeline|FrameId)" | tr '\n' ' ')"
  echo "  processing - $(echo "$AVAILABLE_TESTS" | grep -c -E "^(Processing|FastGaussian)") tests: $(echo "$AVAILABLE_TESTS" | grep -E "^(Processing|FastGaussian)" | tr '\n' ' ')"
  echo "  shm        - $(echo "$AVAILABLE_TESTS" | grep -c "^SharedMemory") tests: $(echo "$AVAILABLE_TESTS" | grep "^SharedMemory" | tr '\n' ' ')"
  echo "  transport  - $(echo "$AVAILABLE_TESTS" | grep -c -E "^(Handshake|ClientHealth|Transport)") tests: $(echo "$AVAILABLE_TESTS" | grep -E "^(Handshake|ClientHealth|Transport)" | tr '\n' ' ')"
  echo "  sensor     - $(echo "$AVAILABLE_TESTS" | grep -c -E "(Sensor|Kinect)") tests: $(echo "$AVAILABLE_TESTS" | grep -E "(Sensor|Kinect)" | tr '\n' ' ')"
  echo "  integration- $(echo "$AVAILABLE_TESTS" | grep -c -E "^(SyntheticSensorPipeline|ProcessingScaleSemantics|ProcessBlackBox|FaultInjection|TransportMidStreamAttach)") tests: $(echo "$AVAILABLE_TESTS" | grep -E "^(SyntheticSensorPipeline|ProcessingScaleSemantics|ProcessBlackBox|FaultInjection|TransportMidStreamAttach)" | tr '\n' ' ')"
  echo "  memory     - $(echo "$AVAILABLE_TESTS" | grep -c "Memory") tests: $(echo "$AVAILABLE_TESTS" | grep "Memory" | tr '\n' ' ')"
  echo "  performance- Heavy tests (requires --heavy flag)"
  
  exit 0
fi

# Handle memory testing flags
if [ $MEMORY_QUICK -eq 1 ]; then
  echo -e "${GREEN}Running quick memory tests (no ExtendedRuntime)...${RESET}"
  CATEGORIES=("memory")
  EXCLUDE_PATTERNS=("*ExtendedRuntime*")
fi

if [ $MEMORY_ASAN -eq 1 ]; then
  echo -e "${GREEN}Building with AddressSanitizer and running memory tests...${RESET}"
  export ASAN_OPTIONS="abort_on_error=1:halt_on_error=1:check_initialization_order=1:detect_leaks=1"
  (cd "$SCRIPT_DIR" && ./build.sh -s asan)
  # Update test binary path after rebuild
  if [ -f "${BUILD_DIR}/tests/CalderaTests" ]; then
    TEST_BIN="${BUILD_DIR}/tests/CalderaTests"
  elif [ -f "${BUILD_DIR}/CalderaTests" ]; then
    TEST_BIN="${BUILD_DIR}/CalderaTests"
  fi
  CATEGORIES=("memory")
  EXCLUDE_PATTERNS=("*ExtendedRuntime*")  # Skip long tests by default
fi

if [ $MEMORY_FULL -eq 1 ]; then
  echo -e "${GREEN}Running full memory tests (including long-running ExtendedRuntime)...${RESET}"
  export CALDERA_ENABLE_LONG_MEMORY_TESTS=1
  CATEGORIES=("memory")
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

if [ ${#CATEGORIES[@]} -gt 0 ]; then
  # Auto-discover tests and build category patterns dynamically
  AVAILABLE_TESTS=$("$TEST_BIN" --gtest_list_tests 2>/dev/null | grep -E '^\S+\.$' | tr -d '.')
  
  CAT_PATTERNS=()
  for c in "${CATEGORIES[@]}"; do
    case $c in
      logger) 
        # Find all Logger-related tests
        while IFS= read -r test; do
          if [[ $test =~ ^Logger ]]; then
            CAT_PATTERNS+=("$test*")
          fi
        done <<< "$AVAILABLE_TESTS"
        ;;
      pipeline) 
        # Find Pipeline and FrameId tests
        while IFS= read -r test; do
          if [[ $test =~ ^(Pipeline|FrameId) ]]; then
            CAT_PATTERNS+=("$test*")
          fi
        done <<< "$AVAILABLE_TESTS"
        ;;
      processing) 
        # Find Processing and FastGaussian tests
        while IFS= read -r test; do
          if [[ $test =~ ^(Processing|FastGaussian) ]]; then
            CAT_PATTERNS+=("$test*")
          fi
        done <<< "$AVAILABLE_TESTS"
        ;;
      shm) 
        # Find SharedMemory tests
        while IFS= read -r test; do
          if [[ $test =~ ^SharedMemory ]]; then
            CAT_PATTERNS+=("$test*")
          fi
        done <<< "$AVAILABLE_TESTS"
        ;;
      transport) 
        # Find transport-related tests
        while IFS= read -r test; do
          if [[ $test =~ ^(Handshake|ClientHealth|Transport) ]]; then
            CAT_PATTERNS+=("$test*")
          fi
        done <<< "$AVAILABLE_TESTS"
        ;;
      sensor) 
        # Find sensor tests
        while IFS= read -r test; do
          if [[ $test =~ (Sensor|Kinect) ]]; then
            CAT_PATTERNS+=("$test*")
          fi
        done <<< "$AVAILABLE_TESTS"
        ;;
      integration) 
        # Find integration tests
        while IFS= read -r test; do
          if [[ $test =~ ^(SyntheticSensorPipeline|ProcessingScaleSemantics|ProcessBlackBox|FaultInjection|TransportMidStreamAttach) ]]; then
            CAT_PATTERNS+=("$test*")
          fi
        done <<< "$AVAILABLE_TESTS"
        ;;
      memory) 
        # Find memory tests
        while IFS= read -r test; do
          if [[ $test =~ Memory ]]; then
            CAT_PATTERNS+=("$test*")
          fi
        done <<< "$AVAILABLE_TESTS"
        ;;
      performance) 
        RUN_HEAVY=1 ;; # heavy handled later
      *)
        echo -e "${YELLOW}Unknown category: $c. Available test suites:${RESET}" >&2
        echo "$AVAILABLE_TESTS" | head -5 >&2
        ;;
    esac
  done
  if [ ${#CAT_PATTERNS[@]} -gt 0 ]; then
    # Append category patterns to filter (if user also specified SPECIFIC_TESTS, combine)
    CAT_JOIN=""
    for p in "${CAT_PATTERNS[@]}"; do
      if [ -z "$CAT_JOIN" ]; then CAT_JOIN="$p"; else CAT_JOIN="$CAT_JOIN:$p"; fi
    done
    # If a previous --gtest_filter exists, merge; else add new
    FILTER_FOUND=0
    for a in "${EXTRA_ARGS[@]}"; do
      case $a in --gtest_filter=*) FILTER_FOUND=1;; esac
    done
    if [ $FILTER_FOUND -eq 1 ]; then
      # Merge into existing filter argument
      for i in "${!EXTRA_ARGS[@]}"; do
        case ${EXTRA_ARGS[$i]} in
          --gtest_filter=*)
            EXISTING=${EXTRA_ARGS[$i]#--gtest_filter=}
            EXTRA_ARGS[$i]=--gtest_filter="$EXISTING:$CAT_JOIN"
            ;;
        esac
      done
    else
      EXTRA_ARGS+=(--gtest_filter="$CAT_JOIN")
    fi
    
    # Handle exclusion patterns if specified
    if [ -n "${EXCLUDE_PATTERNS:-}" ] && [ ${#EXCLUDE_PATTERNS[@]} -gt 0 ]; then
      # Build exclusion string
      EXCLUDE_JOIN=""
      for ex in "${EXCLUDE_PATTERNS[@]}"; do
        if [ -z "$EXCLUDE_JOIN" ]; then EXCLUDE_JOIN="-$ex"; else EXCLUDE_JOIN="$EXCLUDE_JOIN:-$ex"; fi
      done
      
      # Append exclusions to existing filter
      for i in "${!EXTRA_ARGS[@]}"; do
        case ${EXTRA_ARGS[$i]} in
          --gtest_filter=*)
            EXISTING=${EXTRA_ARGS[$i]#--gtest_filter=}
            EXTRA_ARGS[$i]=--gtest_filter="$EXISTING:$EXCLUDE_JOIN"
            ;;
        esac
      done
    fi
  fi
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
  setup_test_environment
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
  setup_test_environment
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
