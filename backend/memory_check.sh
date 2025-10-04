#!/bin/bash
# Memory leak detection scripts for Caldera Backend
#
# Usage:
#   ./memory_check.sh asan      # Run tests with AddressSanitizer
#   ./memory_check.sh valgrind  # Run tests with Valgrind
#   ./memory_check.sh ubsan     # Run tests with UndefinedBehaviorSanitizer  
#   ./memory_check.sh tsan      # Run tests with ThreadSanitizer
#   ./memory_check.sh all       # Run all available memory checks

set -e

SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
cd "$SCRIPT_DIR"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m' 
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_header() {
    echo -e "${BLUE}======================================${NC}"
    echo -e "${BLUE} $1${NC}"
    echo -e "${BLUE}======================================${NC}"
}

print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠ $1${NC}"
}

print_error() {
    echo -e "${RED}✗ $1${NC}"
}

run_asan_tests() {
    print_header "Running AddressSanitizer Tests"
    
    # Set ASan environment variables
    export ASAN_OPTIONS="detect_leaks=1:abort_on_error=1:check_initialization_order=1:detect_stack_use_after_return=1:strict_string_checks=1"
    export MSAN_OPTIONS="poison_in_dtor=1"
    
    echo "Building with AddressSanitizer..."
    CALDERA_ENABLE_ASAN=1 ./build.sh CalderaTests
    
    echo "Running tests with AddressSanitizer..."
    ./test.sh memory || {
        print_error "AddressSanitizer detected issues!"
        return 1
    }
    
    print_success "AddressSanitizer tests passed"
}

run_valgrind_tests() {
    print_header "Running Valgrind Tests"
    
    # Check if valgrind is installed
    if ! command -v valgrind &> /dev/null; then
        print_error "Valgrind not found. Install with: sudo apt-get install valgrind"
        return 1
    fi
    
    echo "Building for Valgrind..."
    CALDERA_ENABLE_VALGRIND=1 ./build.sh CalderaTests
    
    echo "Running tests with Valgrind..."
    valgrind --tool=memcheck \
             --leak-check=full \
             --show-leak-kinds=all \
             --track-origins=yes \
             --error-exitcode=1 \
             --suppressions=valgrind.supp \
             --gen-suppressions=all \
             ./build/CalderaTests || {
        print_error "Valgrind detected memory issues!"
        return 1
    }
    
    print_success "Valgrind tests passed"
}

run_ubsan_tests() {
    print_header "Running UndefinedBehaviorSanitizer Tests"
    
    export UBSAN_OPTIONS="print_stacktrace=1:abort_on_error=1"
    
    echo "Building with UndefinedBehaviorSanitizer..."
    CALDERA_ENABLE_UBSAN=1 ./build.sh CalderaTests
    
    echo "Running tests with UBSan..."
    ./test.sh memory || {
        print_error "UndefinedBehaviorSanitizer detected issues!"
        return 1
    }
    
    print_success "UndefinedBehaviorSanitizer tests passed"
}

run_tsan_tests() {
    print_header "Running ThreadSanitizer Tests"
    
    export TSAN_OPTIONS="abort_on_error=1:second_deadlock_stack=1"
    
    echo "Building with ThreadSanitizer..."
    CALDERA_ENABLE_TSAN=1 ./build.sh CalderaTests
    
    echo "Running tests with ThreadSanitizer..."
    ./test.sh memory || {
        print_error "ThreadSanitizer detected data races!"
        return 1
    }
    
    print_success "ThreadSanitizer tests passed"
}

run_memory_stress_test() {
    print_header "Running Memory Stress Tests"
    
    echo "Building stress test binary..."
    CALDERA_ENABLE_ASAN=1 ./build.sh CalderaHeavyTests
    
    echo "Running heavy/stress tests with ASan..."
    ASAN_OPTIONS="detect_leaks=1:abort_on_error=1" ./test.sh --heavy memory || {
        print_error "Memory stress tests failed!"
        return 1
    }
    
    print_success "Memory stress tests passed"
}

show_usage() {
    echo "Usage: $0 [asan|valgrind|ubsan|tsan|stress|all]"
    echo ""
    echo "Memory checking modes:"
    echo "  asan      - AddressSanitizer (fast, detects buffer overflows, use-after-free, leaks)"
    echo "  valgrind  - Valgrind Memcheck (slower, comprehensive leak detection)"  
    echo "  ubsan     - UndefinedBehaviorSanitizer (detects undefined behavior)"
    echo "  tsan      - ThreadSanitizer (detects data races in multi-threaded code)"
    echo "  stress    - Run heavy/stress tests with memory checking"
    echo "  all       - Run all available memory checks (warning: takes time!)"
    echo ""
    echo "Examples:"
    echo "  $0 asan              # Quick memory check with AddressSanitizer"
    echo "  $0 valgrind          # Comprehensive leak check with Valgrind"
    echo "  $0 all               # Run all memory checks"
}

case "${1:-}" in
    asan)
        run_asan_tests
        ;;
    valgrind)
        run_valgrind_tests
        ;;
    ubsan) 
        run_ubsan_tests
        ;;
    tsan)
        run_tsan_tests
        ;;
    stress)
        run_memory_stress_test
        ;;
    all)
        print_header "Running All Memory Checks"
        run_asan_tests && \
        run_ubsan_tests && \
        run_valgrind_tests && \
        run_tsan_tests && \
        run_memory_stress_test
        print_success "All memory checks completed successfully!"
        ;;
    -h|--help|"")
        show_usage
        exit 0
        ;;
    *)
        print_error "Unknown option: $1"
        show_usage
        exit 1
        ;;
esac