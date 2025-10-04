#!/bin/bash
# Memory profiling and analysis tools for Caldera Backend
#
# Usage:
#   ./memory_profile.sh massif SensorBackend    # Profile with Valgrind Massif
#   ./memory_profile.sh heaptrack SensorViewer # Profile with Heaptrack
#   ./memory_profile.sh analyze                # Analyze existing profiling data

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

check_tool() {
    if ! command -v "$1" &> /dev/null; then
        print_error "$1 not found. Install with: sudo apt-get install $2"
        return 1
    fi
    return 0
}

run_massif_profiling() {
    local target="${1:-SensorBackend}"
    print_header "Memory Profiling with Valgrind Massif: $target"
    
    if ! check_tool "valgrind" "valgrind"; then
        return 1
    fi
    
    # Build target without sanitizers (they conflict with Valgrind)
    echo "Building $target for profiling..."
    ./build.sh "$target"
    
    local output_file="massif.out.$target.$$"
    
    echo "Running Massif profiler..."
    echo "Output file: $output_file"
    
    # Run with massif to track heap usage over time
    valgrind --tool=massif \
             --massif-out-file="$output_file" \
             --detailed-freq=1 \
             --max-snapshots=100 \
             --time-unit=ms \
             --pages-as-heap=yes \
             "./build/$target" --help 2>/dev/null || {
        
        # For SensorBackend, try simulated mode briefly
        if [[ "$target" == "SensorBackend" ]]; then
            echo "Running SensorBackend in simulated mode for profiling..."
            timeout 10s valgrind --tool=massif \
                         --massif-out-file="$output_file" \
                         --detailed-freq=1 \
                         --max-snapshots=100 \
                         --time-unit=ms \
                         "./build/$target" --mode=simulated || true
        fi
    }
    
    if [[ -f "$output_file" ]]; then
        print_success "Massif profiling completed: $output_file"
        
        # Generate text report if ms_print available
        if command -v ms_print &> /dev/null; then
            echo ""
            echo "=== MEMORY USAGE SUMMARY ==="
            ms_print "$output_file" | head -50
            echo ""
            echo "Full report: ms_print $output_file"
            echo "Visual analysis: massif-visualizer $output_file (if installed)"
        fi
        
        return 0
    else
        print_error "Massif profiling failed - no output file generated"
        return 1
    fi
}

run_heaptrack_profiling() {
    local target="${1:-SensorBackend}"
    print_header "Memory Profiling with Heaptrack: $target"
    
    if ! check_tool "heaptrack" "heaptrack"; then
        return 1
    fi
    
    # Build target 
    echo "Building $target for profiling..."
    ./build.sh "$target"
    
    local output_file="heaptrack.$target.$$"
    
    echo "Running Heaptrack profiler..."
    
    if [[ "$target" == "SensorBackend" ]]; then
        # Run SensorBackend briefly in simulated mode
        timeout 15s heaptrack -o "$output_file" "./build/$target" --mode=simulated || true
    else
        # Run other targets with --help
        heaptrack -o "$output_file" "./build/$target" --help || true
    fi
    
    if ls ${output_file}*.gz &>/dev/null; then
        local actual_file=$(ls ${output_file}*.gz | head -1)
        print_success "Heaptrack profiling completed: $actual_file"
        
        # Generate summary
        echo ""
        echo "=== HEAPTRACK SUMMARY ==="
        heaptrack_print "$actual_file" | head -30
        echo ""
        echo "Full analysis: heaptrack_print $actual_file"
        echo "GUI analysis: heaptrack_gui $actual_file (if installed)"
        
        return 0
    else
        print_error "Heaptrack profiling failed - no output file generated"
        return 1
    fi
}

analyze_existing_data() {
    print_header "Analyzing Existing Profiling Data"
    
    # Look for massif files
    local massif_files=(massif.out.*)
    if [[ ${#massif_files[@]} -gt 0 && -f "${massif_files[0]}" ]]; then
        echo "Found Massif files:"
        for file in "${massif_files[@]}"; do
            [[ -f "$file" ]] && echo "  - $file"
        done
        
        echo ""
        echo "Recent Massif data:"
        local latest_massif=$(ls -t massif.out.* 2>/dev/null | head -1)
        if [[ -f "$latest_massif" ]] && command -v ms_print &> /dev/null; then
            echo "=== $latest_massif ==="
            ms_print "$latest_massif" | head -20
        fi
    fi
    
    # Look for heaptrack files
    local heaptrack_files=(heaptrack.*.gz)
    if [[ ${#heaptrack_files[@]} -gt 0 && -f "${heaptrack_files[0]}" ]]; then
        echo ""
        echo "Found Heaptrack files:"
        for file in "${heaptrack_files[@]}"; do
            [[ -f "$file" ]] && echo "  - $file"
        done
        
        echo ""
        echo "Recent Heaptrack data:"
        local latest_heaptrack=$(ls -t heaptrack.*.gz 2>/dev/null | head -1)
        if [[ -f "$latest_heaptrack" ]] && command -v heaptrack_print &> /dev/null; then
            echo "=== $latest_heaptrack ==="
            heaptrack_print "$latest_heaptrack" | head -15
        fi
    fi
    
    # Check for AddressSanitizer logs
    if [[ -d "logs" ]]; then
        local asan_logs=$(find logs -name "*.log" -exec grep -l "AddressSanitizer\|LeakSanitizer" {} \; 2>/dev/null)
        if [[ -n "$asan_logs" ]]; then
            echo ""
            echo "AddressSanitizer/LeakSanitizer logs found:"
            echo "$asan_logs"
        fi
    fi
    
    if [[ ! -f "${massif_files[0]}" && ! -f "${heaptrack_files[0]}" ]]; then
        print_warning "No profiling data found. Run profiling first:"
        echo "  $0 massif SensorBackend"
        echo "  $0 heaptrack SensorViewer"
    fi
}

memory_usage_baseline() {
    print_header "Memory Usage Baseline Test"
    
    # Build tests with ASan for leak detection
    echo "Building tests with AddressSanitizer..."
    CALDERA_ENABLE_ASAN=1 ./build.sh CalderaTests
    
    echo "Running memory leak tests..."
    export ASAN_OPTIONS="detect_leaks=1:abort_on_error=0:exitcode=1"
    
    if ./test.sh memory; then
        print_success "Memory leak tests passed!"
    else
        print_error "Memory leak tests detected issues!"
        return 1
    fi
    
    echo ""
    echo "Running baseline memory usage check..."
    
    # Quick memory usage check
    /usr/bin/time -v ./test.sh MemoryLeakTest.SharedMemoryTransport_CreateDestroy_NoLeaks 2>&1 | \
    grep -E "(Maximum resident set size|Peak virtual memory)" || true
}

show_usage() {
    echo "Usage: $0 [massif|heaptrack|analyze|baseline] [TARGET]"
    echo ""
    echo "Memory profiling modes:"
    echo "  massif TARGET    - Profile memory usage over time with Valgrind Massif"
    echo "  heaptrack TARGET - Profile heap allocations with Heaptrack"
    echo "  analyze         - Analyze existing profiling data files"
    echo "  baseline        - Run memory leak tests and baseline measurements"
    echo ""
    echo "Available targets: SensorBackend, SensorViewer, CalderaTests"
    echo ""
    echo "Examples:"
    echo "  $0 massif SensorBackend     # Profile backend memory usage"
    echo "  $0 heaptrack SensorViewer   # Profile viewer allocations"
    echo "  $0 baseline                 # Run leak detection tests"
    echo "  $0 analyze                  # Review existing profiling data"
    echo ""
    echo "Prerequisites:"
    echo "  sudo apt-get install valgrind heaptrack"
}

case "${1:-}" in
    massif)
        run_massif_profiling "${2:-SensorBackend}"
        ;;
    heaptrack)
        run_heaptrack_profiling "${2:-SensorBackend}"
        ;;
    analyze)
        analyze_existing_data
        ;;
    baseline)
        memory_usage_baseline
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