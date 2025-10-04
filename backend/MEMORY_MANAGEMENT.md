# Memory Management and Leak Detection Guide

## Overview

This document provides comprehensive guidance for detecting, preventing, and fixing memory leaks in the Caldera Backend. The project now includes multiple tools and approaches for memory safety validation.

## Available Tools

### 1. AddressSanitizer (ASan) - Recommended
**Best for: Development, CI/CD, general leak detection**

```bash
# Enable during build
CALDERA_ENABLE_ASAN=1 ./build.sh

# Or use convenience script
./memory_check.sh asan
```

**Advantages:**
- Fast execution (2-3x slowdown)
- Detects buffer overflows, use-after-free, double-free
- Built-in leak detection
- Excellent stack traces
- Integrates well with CI/CD

**Limitations:**
- Increases memory usage ~3x
- May miss some static initialization leaks

### 2. Valgrind Memcheck - Comprehensive
**Best for: Deep leak analysis, production validation**

```bash
# Run with memory leak detection
./memory_check.sh valgrind

# Or manual invocation
valgrind --tool=memcheck --leak-check=full ./build/CalderaTests
```

**Advantages:**
- Very thorough leak detection
- Detects uninitialized memory reads
- No recompilation needed
- Works with release builds

**Limitations:**
- Slow execution (10-50x slowdown)  
- Large memory overhead
- May have false positives with some system libraries

### 3. ThreadSanitizer (TSan) - Concurrency Issues
**Best for: Multi-threaded code validation**

```bash
CALDERA_ENABLE_TSAN=1 ./build.sh
./memory_check.sh tsan
```

**Use for:**
- Data race detection in HAL, Transport layers
- Concurrent SharedMemory access validation
- Processing pipeline thread safety

### 4. Memory Profiling Tools

#### Valgrind Massif - Memory Usage Over Time
```bash
./memory_profile.sh massif SensorBackend
```

#### Heaptrack - Allocation Analysis  
```bash
./memory_profile.sh heaptrack SensorViewer
```

## Critical Areas to Monitor

### 1. SharedMemory Transport (`SharedMemoryTransportServer`)
**Potential issues:**
- Shared memory segments not unlinked on shutdown
- Memory mapping leaks
- Double-buffer allocation errors

**Detection:**
```bash
# Check for leaked shm segments
ls /dev/shm/caldera_*

# Run dedicated tests
./build/CalderaTests --gtest_filter="MemoryLeakTest.SharedMemoryTransport_*"
```

### 2. HAL Layer (Kinect Devices)
**Potential issues:**
- libfreenect/libfreenect2 resource leaks
- OpenGL context leaks in SensorViewer
- USB device handle leaks

**Detection:**
```bash
# Profile sensor operations
./memory_profile.sh massif SensorViewer

# Test synthetic sensors (safer)
./build/CalderaTests --gtest_filter="MemoryLeakTest.SyntheticSensor_*"
```

### 3. Processing Pipeline
**Potential issues:**
- Raw frame buffer accumulation
- Callback reference cycles
- Height map data copying inefficiencies

**Detection:**
```bash
./build/CalderaTests --gtest_filter="MemoryLeakTest.ProcessingManager_*"
```

## Memory Leak Testing Strategy

### Development Workflow

1. **Pre-commit checks:**
   ```bash
   ./memory_check.sh asan  # Quick ASan check (~2 minutes)
   ```

2. **Feature testing:**
   ```bash
   ./memory_check.sh all   # Comprehensive validation (~10 minutes)
   ```

3. **Performance analysis:**
   ```bash
   ./memory_profile.sh baseline  # Memory usage baseline
   ./memory_profile.sh massif SensorBackend  # Usage over time
   ```

### CI/CD Integration

Recommended CI pipeline stages:

```yaml
# .github/workflows/memory-check.yml (example)
memory-check:
  runs-on: ubuntu-latest
  steps:
    - name: Install tools
      run: sudo apt-get install valgrind
    
    - name: Build with ASan
      run: |
        cd backend
        CALDERA_ENABLE_ASAN=1 ./build.sh CalderaTests
    
    - name: Run leak detection
      run: |
        cd backend  
        ./memory_check.sh asan
```

## Common Memory Issues and Solutions

### 1. Shared Memory Segment Leaks

**Symptoms:**
```bash
$ ls /dev/shm/
caldera_worldframe  caldera_memleak_test_1  # <- leftover segments
```

**Root cause:** `shm_unlink()` not called in destructor

**Solution:**
```cpp
SharedMemoryTransportServer::~SharedMemoryTransportServer() {
    stop();  // Must call stop() which handles cleanup
}

void SharedMemoryTransportServer::stop() {
    // ... existing cleanup ...
    
    // Ensure shm segment is unlinked
    if (!cfg_.shm_name.empty()) {
        shm_unlink(cfg_.shm_name.c_str());  // Critical!
    }
}
```

### 2. Callback Reference Cycles

**Symptoms:** Objects not destroyed despite going out of scope

**Root cause:** `std::function` callbacks capturing `shared_ptr` to the object itself

**Solution:**
```cpp
// BAD: Creates circular reference
device_->setCallback([this, transport=transport_](const Frame& f) {
    transport->send(f);  // transport keeps 'this' alive
});

// GOOD: Use weak_ptr or raw pointer with lifetime guarantees
device_->setCallback([logger=logger_](const Frame& f) {
    // Only capture what you need, avoid 'this'
});
```

### 3. OpenGL/Graphics Resource Leaks

**Symptoms:** Growing memory usage in SensorViewer, GPU memory exhaustion

**Detection:** Run with `MESA_DEBUG=1` environment variable

**Solution:** Ensure proper cleanup in GLFW/OpenGL code:
```cpp
// In SensorViewer destructors
if (window_) {
    glfwDestroyWindow(window_);
    window_ = nullptr;
}
glfwTerminate();  // Only call once globally
```

### 4. Thread and Async Resource Leaks

**Symptoms:** Threads not joining, async operations continuing after shutdown

**Detection:** ThreadSanitizer or manual thread monitoring

**Solution:**
```cpp
// Proper thread lifecycle management
class Component {
    std::atomic<bool> shutdown_{false};
    std::thread worker_;

public:
    ~Component() {
        shutdown_ = true;
        if (worker_.joinable()) {
            worker_.join();  // Critical: always join
        }
    }
};
```

## Environment Variables

Configure memory checking behavior:

```bash
# AddressSanitizer options
export ASAN_OPTIONS="detect_leaks=1:abort_on_error=1:check_initialization_order=1"

# Valgrind suppressions
export VALGRIND_OPTS="--suppressions=valgrind.supp"

# Caldera-specific debugging
export CALDERA_LOG_LEVEL=debug  # More verbose logging
export CALDERA_ENABLE_ASAN=1    # Enable ASan at build time
```

## Performance Impact

| Tool | CPU Overhead | Memory Overhead | Use Case |
|------|-------------|----------------|----------|  
| AddressSanitizer | 2-3x | 3x | Development, CI |
| Valgrind Memcheck | 10-50x | 5-10x | Deep analysis |
| ThreadSanitizer | 5-15x | 5x | Concurrency bugs |
| No sanitizers | 1x | 1x | Production |

## Troubleshooting

### ASan Reports False Positives
```bash
# Add to valgrind.supp or create asan.supp
{
   LibraryLeak
   Memcheck:Leak
   ...
   obj:*/libfreenect2.so*
}
```

### Tests Fail Only Under Sanitizers
- Check for undefined behavior (uninitialized variables)
- Verify thread synchronization
- Review assumptions about memory layout

### High Memory Usage in Long-Running Tests
```bash
# Profile memory growth over time
./memory_profile.sh massif SensorBackend

# Check for accumulating buffers/caches
```

## Future Improvements

1. **Static Analysis Integration:**
   - Clang Static Analyzer
   - Cppcheck with memory leak detection
   - PVS-Studio (commercial)

2. **Runtime Monitoring:**
   - Custom memory allocator with tracking
   - RSS/VmSize monitoring in production
   - Automatic leak detection in CI

3. **Smart Pointer Migration:**
   - Replace raw pointers with `unique_ptr`/`shared_ptr`
   - Use `std::weak_ptr` for observer relationships
   - RAII wrapper for system resources (file handles, etc.)

## Quick Reference

```bash
# Daily development
./memory_check.sh asan

# Before committing major changes  
./memory_check.sh all

# Investigating specific issues
./memory_profile.sh massif SensorBackend
./memory_profile.sh analyze

# Check for leaked shared memory
ls /dev/shm/caldera_*

# Manual Valgrind with full options
valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all \
         --track-origins=yes --suppressions=valgrind.supp ./build/CalderaTests
```