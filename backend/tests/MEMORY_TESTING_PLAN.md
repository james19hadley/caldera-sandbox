# Memory Testing Plan (Backend Pipeline)

Location rationale: Lives alongside tests to co-evolve with test harness code and memory infrastructure (design intent doc; user-facing docs stay under `/docs`).

## Scope & Goals
Comprehensive memory validation from sensor data acquisition through processing pipeline to transport delivery. Focus on leak detection, memory growth patterns, resource cleanup, and multi-component memory interactions. Ensures production-ready backend with zero memory leaks and optimal resource management under all conditions.

Target Architecture: HAL (SensorDevice) → Processing (ProcessingManager) → Transport (SHM/Socket) → Client Consumers

---
## Legend
- **Existing Infra**: Memory testing infrastructure already implemented and functional.
- **Existing Tests**: Current memory-related test files in `backend/tests/memory/`.
- **Gap**: Missing memory validation capability for pipeline scope.
- **Planned**: To be implemented in current phase implementation.
- **Critical**: High-priority memory scenarios that could cause production instability.

---
## Phase 0: Memory Testing Infrastructure & Individual Component Baseline
Purpose: Establish reliable memory leak detection for isolated components and validate testing methodology.

### Existing Infra
- **Sanitizers**: AddressSanitizer (ASan), UndefinedBehaviorSanitizer (UBSan), ThreadSanitizer (TSan), MemorySanitizer (MSan) integrated in CMake build system
- **Valgrind Integration**: Configured with suppressions for third-party libraries (libfreenect, system libs)
- **Memory Testing Scripts**: `memory_check.sh` and `memory_profile.sh` with comprehensive sanitizer support
- **Existing Memory Tests**: `backend/tests/memory/test_memory_leaks.cpp` with basic component lifecycle tests
- **Test Integration**: Memory category in `test.sh` for organized execution
- **Build Configuration**: CMake flags for sanitizer selection with mutual exclusivity checks

### Existing Tests Status
Current `MemoryLeakTest` suite covers:
- ✅ **SharedMemoryTransport**: Basic create/destroy cycles (FIXED: added `shm_unlink()` cleanup)
- ✅ **ProcessingManager**: Basic lifecycle validation 
- ✅ **RapidCreateDestroy**: Stress test rapid component instantiation
- ✅ **LongRunningStability**: Extended runtime validation (30-second duration)

### Gaps for Phase 0
- **HAL Component Memory**: No memory tests for HAL_Manager and sensor device lifecycle
- **Individual Transport Types**: Missing memory validation for SocketTransportServer, LocalTransportServer
- **Component State Transitions**: No validation of memory behavior during start/stop/restart cycles
- **Memory Growth Monitoring**: No tests measuring memory growth patterns over time

### Planned Additions - Phase 0
1. **HAL Memory Tests** (`tests/memory/test_hal_memory.cpp`):
   ```cpp
   TEST(HALMemoryTest, HALManagerLifecycle)
   TEST(HALMemoryTest, SensorDeviceCreateDestroy) 
   TEST(HALMemoryTest, MockSensorExtendedRuntime)
   TEST(HALMemoryTest, SyntheticSensorMemoryStability)
   ```

2. **Transport Memory Validation** (`tests/memory/test_transport_memory.cpp`):
   ```cpp
   TEST(TransportMemoryTest, AllTransportTypesLifecycle)
   TEST(TransportMemoryTest, SHMSegmentCleanupValidation)
   TEST(TransportMemoryTest, SocketTransportMemoryStability)
   TEST(TransportMemoryTest, LocalTransportMemoryBaseline)
   ```

3. **Memory Growth Monitoring** (`tests/memory/test_memory_growth.cpp`):
   ```cpp
   TEST(MemoryGrowthTest, ProcessingManagerMemoryGrowth)
   TEST(MemoryGrowthTest, TransportPublishMemoryGrowth)
   TEST(MemoryGrowthTest, ComponentRestartMemoryAccumulation)
   ```

### Memory Testing Methodology
- **Sanitizer Priority**: AddressSanitizer for leak detection, Valgrind for comprehensive analysis
- **Measurement Approach**: Process RSS monitoring via `/proc/self/status` parsing
- **Leak Detection**: Combination of sanitizer reports and explicit resource counting
- **Growth Validation**: Baseline memory → operation cycles → final memory comparison
- **Cleanup Verification**: Explicit validation of resource deallocation (SHM segments, file descriptors, heap)

### Acceptance Criteria - Phase 0
- All existing `MemoryLeakTest.*` continue to pass
- New HAL memory tests show zero leaks across all sensor device types
- Transport tests validate proper cleanup of all transport mechanisms
- Memory growth tests establish baseline behavior patterns
- All tests pass under ASan, Valgrind, and TSan without errors
- Integration with `test.sh memory` category successful
- Documentation updated with new test descriptions

### Implementation Notes
- Use existing `IntegrationHarness` pattern for controlled component lifecycle
- Leverage `SyntheticSensorDevice` for deterministic memory testing
- Implement memory measurement helpers for consistent RSS tracking
- Extend existing CMake sanitizer configuration as needed

---
## Phase 1: Multi-Component Memory Interactions
Purpose: Validate memory behavior across component boundaries and data flow paths.

### Target Interactions
- **HAL → Processing**: Raw depth frame handoff and processing pipeline memory
- **Processing → Transport**: World frame publishing and SHM buffer management  
- **Transport → Client**: Reader attachment/detachment and buffer sharing
- **Full Pipeline**: End-to-end memory behavior from sensor to client consumption

### Planned Additions - Phase 1
1. **Pipeline Memory Flow** (`tests/memory/test_pipeline_memory.cpp`):
   ```cpp
   TEST(PipelineMemoryTest, HALToProcessingMemoryHandoff)
   TEST(PipelineMemoryTest, ProcessingToTransportMemoryFlow)
   TEST(PipelineMemoryTest, FullPipelineMemoryStability)
   TEST(PipelineMemoryTest, MultiReaderMemorySharing)
   ```

2. **Buffer Management Validation** (`tests/memory/test_buffer_memory.cpp`):
   ```cpp
   TEST(BufferMemoryTest, SHMDoubleBufferMemoryBehavior)
   TEST(BufferMemoryTest, FrameBufferReuseValidation)
   TEST(BufferMemoryTest, TransportBufferOverflowHandling)
   TEST(BufferMemoryTest, ReaderBufferLifecycleSafety)
   ```

3. **Component Integration Memory** (`tests/memory/test_integration_memory.cpp`):
   ```cpp
   TEST(IntegrationMemoryTest, SensorToClientMemoryPath)
   TEST(IntegrationMemoryTest, ComponentRestartMemoryConsistency)
   TEST(IntegrationMemoryTest, ProcessBlackBoxMemoryValidation)
   TEST(IntegrationMemoryTest, MultiTransportMemoryComparison)
   ```

### Memory Interaction Scenarios
- **Data Copy Chains**: Validate memory usage across synthetic→HAL→processing→transport copy chain
- **Shared Memory Safety**: Ensure SHM segments properly managed with multiple readers
- **Resource Handoff**: Verify clean memory transfer between components without leaks
- **Concurrent Access**: Memory safety with multiple threads accessing shared buffers

### Acceptance Criteria - Phase 1
- Zero memory leaks across all component interaction paths
- Memory usage remains bounded during continuous operation
- SHM segment lifecycle properly managed with multiple consumers
- Thread-safe memory access validated under TSan
- Component restart scenarios show no memory accumulation
- Integration with existing `IntegrationHarness` framework

---
## Phase 2: Stress Testing & Production Scenario Memory Validation
Purpose: Validate memory behavior under production-like stress, extended runtime, and resource pressure.

### Stress Scenarios
- **High Throughput**: 120+ FPS sustained operation with memory monitoring
- **Extended Runtime**: 24+ hour continuous operation validation
- **Memory Pressure**: Operation under constrained memory conditions
- **Rapid Cycling**: Fast component restart/shutdown cycles
- **Multi-Consumer Load**: Multiple simultaneous SHM readers

### Planned Additions - Phase 2
1. **High Load Memory Testing** (`tests/memory/test_memory_stress.cpp`):
   ```cpp
   TEST(MemoryStressTest, HighThroughputMemoryStability)
   TEST(MemoryStressTest, ExtendedRuntimeMemoryValidation)
   TEST(MemoryStressTest, RapidComponentCyclingMemory)
   TEST(MemoryStressTest, MultiConsumerMemoryScaling)
   ```

2. **Production Scenario Memory** (`tests/memory/test_production_memory.cpp`):
   ```cpp
   TEST(ProductionMemoryTest, KinectDeviceMemoryStability)
   TEST(ProductionMemoryTest, NetworkTransportMemoryBehavior)
   TEST(ProductionMemoryTest, FaultRecoveryMemoryConsistency)
   TEST(ProductionMemoryTest, ResourceExhaustionGracefulDegradation)
   ```

3. **Long-Running Memory Validation** (`tests/memory/test_longterm_memory.cpp`):
   ```cpp
   TEST(LongTermMemoryTest, ContinuousOperationMemoryGrowth)
   TEST(LongTermMemoryTest, PeriodicCleanupValidation)
   TEST(LongTermMemoryTest, MemoryFragmentationResistance)
   ```

### Memory Profiling Integration
- **Heaptrack Integration**: Detailed heap allocation tracking for optimization
- **Massif Integration**: Memory usage over time profiling
- **Custom Memory Metrics**: Peak usage, allocation patterns, fragmentation analysis
- **Performance Impact**: Memory testing overhead measurement and minimization

### Acceptance Criteria - Phase 2
- Memory usage remains stable under high throughput (120+ FPS)
- No memory growth during extended runtime operations
- Graceful handling of memory pressure scenarios
- Component cycling shows no accumulation patterns
- Production device scenarios (Kinect) validate cleanly
- Memory profiling data available for performance optimization

---
## Phase 3: Memory Testing Automation & CI Integration
Purpose: Ensure memory validation is integrated into development workflow and CI pipeline.

### Automation Goals
- **Automated Memory Regression Detection**: CI pipeline catches memory regressions
- **Performance Baseline Tracking**: Memory usage baselines maintained over time  
- **Developer Workflow Integration**: Easy memory testing during development
- **Comprehensive Coverage**: All memory scenarios covered in automated testing

### Planned Additions - Phase 3
1. **CI Memory Testing Pipeline** (`.github/workflows/memory-tests.yml`):
   - ASan build and test execution
   - Valgrind full pipeline validation
   - Memory usage baseline comparison
   - Performance regression detection

2. **Memory Test Orchestration** (`tests/memory/memory_test_runner.py`):
   - Automated test suite execution with proper sanitizer configuration
   - Memory usage baseline capture and comparison
   - Report generation with memory usage trends
   - Integration with existing `test.sh` infrastructure

3. **Developer Memory Tools** (`scripts/memory_dev_tools.sh`):
   - Quick memory validation during development
   - Interactive memory profiling setup
   - Memory leak investigation helpers
   - Integration with VS Code tasks

### Memory Testing Infrastructure Enhancements
- **Memory Usage Baselines**: Establish and maintain baseline memory usage for all components
- **Regression Detection**: Automated detection of memory usage increases
- **Sanitizer Configuration Management**: Streamlined sanitizer setup and configuration
- **Report Generation**: Automated memory testing reports with trends and recommendations

### Acceptance Criteria - Phase 3
- Memory tests integrated into CI pipeline with proper failure detection
- Developer workflow includes easy access to memory validation tools
- Memory usage baselines established and maintained
- Automated regression detection prevents memory issues from entering main branch
- Documentation provides clear guidance for memory testing during development

---
## Phase 4: Advanced Memory Scenarios & Edge Cases
Purpose: Cover advanced memory scenarios and edge cases that could occur in production.

### Advanced Scenarios
- **Memory Fragmentation**: Long-running operation with varied allocation patterns
- **Resource Exhaustion**: Behavior under system memory pressure
- **Error Condition Memory Safety**: Memory behavior during error scenarios
- **Concurrent Access Patterns**: Complex multi-threaded memory access validation

### Planned Additions - Phase 4
1. **Edge Case Memory Testing** (`tests/memory/test_memory_edge_cases.cpp`):
   ```cpp
   TEST(MemoryEdgeCasesTest, SystemMemoryExhaustionHandling)
   TEST(MemoryEdgeCasesTest, ErrorConditionMemoryCleanup)
   TEST(MemoryEdgeCasesTest, RaceConditionMemoryFreedomSafety)
   TEST(MemoryEdgeCasesTest, MemoryFragmentationResistance)
   ```

2. **Concurrency Memory Validation** (`tests/memory/test_concurrent_memory.cpp`):
   ```cpp
   TEST(ConcurrentMemoryTest, MultiThreadedSHMAccess)
   TEST(ConcurrentMemoryTest, ProducerConsumerMemorySafety)
   TEST(ConcurrentMemoryTest, ReaderWriterMemoryConsistency)
   ```

### Memory Safety Validation
- **Thread Safety**: Memory access patterns under concurrent usage
- **Exception Safety**: Memory cleanup during exception scenarios  
- **Resource Cleanup**: Proper cleanup during abnormal termination
- **Memory Corruption Detection**: Detection of buffer overruns and memory corruption

### Acceptance Criteria - Phase 4
- All edge case scenarios handle memory safely without leaks or corruption
- Concurrent access patterns validated under thread sanitizer
- Error conditions result in proper memory cleanup
- System resource exhaustion handled gracefully
- Memory corruption detection mechanisms in place

---
## Memory Testing Tools & Infrastructure

### Sanitizer Configuration
```bash
# AddressSanitizer (Primary leak detection)
export ASAN_OPTIONS="abort_on_error=1:halt_on_error=1:check_initialization_order=1"

# Valgrind (Comprehensive analysis)
valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes

# Thread Sanitizer (Concurrency safety)
export TSAN_OPTIONS="abort_on_error=1:halt_on_error=1"

# Memory Sanitizer (Uninitialized memory)
export MSAN_OPTIONS="abort_on_error=1:halt_on_error=1"
```

### Memory Measurement Utilities
```cpp
// Memory usage tracking helpers
class MemoryTracker {
public:
    static size_t getCurrentRSS();
    static size_t getPeakRSS();
    static void resetBaseline();
    static bool checkMemoryGrowth(double maxGrowthPercent);
};
```

### Test Execution Integration
```bash
# Execute via test.sh with memory category
./test.sh memory

# Individual memory test execution with sanitizers
./memory_check.sh asan tests/memory/test_memory_leaks.cpp
./memory_check.sh valgrind tests/memory/test_pipeline_memory.cpp

# Memory profiling
./memory_profile.sh heaptrack tests/memory/test_memory_stress.cpp
```

---
## Memory Testing Targets & Metrics

### Memory Leak Tolerance
| Component | Memory Leak Target | Validation Method |
|-----------|-------------------|-------------------|
| HAL Components | 0 bytes leaked | ASan + Valgrind |
| ProcessingManager | 0 bytes leaked | ASan + Valgrind |
| Transport Servers | 0 bytes leaked | ASan + Valgrind |
| Full Pipeline | 0 bytes leaked | ASan + Valgrind |
| Integration Tests | 0 bytes leaked | ASan + Valgrind |

### Memory Usage Baselines
| Scenario | Memory Baseline | Growth Limit | Measurement |
|----------|----------------|--------------|-------------|
| Component Idle | <10 MB RSS | 0% growth | `/proc/self/status` |
| 30 FPS Operation | <50 MB RSS | <5% growth/hour | RSS monitoring |
| 120 FPS Stress | <100 MB RSS | <10% growth/hour | RSS monitoring |
| Extended Runtime | Baseline +10% | <1% growth/day | Long-term tracking |

### Performance Impact Targets
| Test Category | Runtime Overhead | Memory Overhead | Sanitizer |
|---------------|------------------|-----------------|-----------|
| Basic Tests | <2x baseline | <3x baseline | ASan |
| Integration | <3x baseline | <5x baseline | ASan |  
| Stress Tests | <5x baseline | <10x baseline | ASan |
| Production | <1.5x baseline | <2x baseline | None |

---
## Implementation Timeline & Phases

### Phase 0: Foundation (Current Priority)
**Timeline**: 1-2 days
- ✅ Basic memory testing infrastructure (COMPLETE)
- 🔄 Individual component memory validation (IN PROGRESS)
- ⏳ HAL and Transport memory tests
- ⏳ Memory growth monitoring

### Phase 1: Integration (Next Priority) 
**Timeline**: 2-3 days
- Multi-component memory interactions
- Pipeline memory flow validation
- Buffer management memory safety
- Component integration memory tests

### Phase 2: Production Readiness
**Timeline**: 2-3 days  
- Stress testing under high load
- Extended runtime validation
- Production scenario memory testing
- Memory profiling integration

### Phase 3: Automation
**Timeline**: 1-2 days
- CI pipeline integration
- Developer workflow tools
- Automated regression detection
- Memory usage baseline tracking

### Phase 4: Advanced Scenarios
**Timeline**: 1-2 days
- Edge case memory validation
- Concurrency memory safety
- Error condition memory handling
- Advanced memory corruption detection

**Total Estimated Timeline**: 8-12 days for complete implementation

---
## Risk Assessment & Mitigation

### High Risk Memory Areas
1. **SHM Segment Lifecycle**: Complex cleanup requirements across process boundaries
   - **Mitigation**: Extensive SHM cleanup validation and automated testing
2. **Multi-Threading Safety**: Concurrent access to shared buffers
   - **Mitigation**: Thread sanitizer validation and explicit locking verification  
3. **Resource Cleanup on Error**: Memory cleanup during exception scenarios
   - **Mitigation**: Exception safety testing and RAII pattern enforcement
4. **Long-Running Memory Growth**: Subtle memory leaks during extended operation
   - **Mitigation**: Extended runtime testing and memory growth monitoring

### Testing Infrastructure Risks
1. **Sanitizer Performance Impact**: Tests may be too slow for CI
   - **Mitigation**: Tiered testing with fast ASan tests and slower comprehensive Valgrind
2. **False Positives**: Third-party library memory reports
   - **Mitigation**: Comprehensive suppression files and baseline establishment
3. **Test Environment Variability**: Memory behavior differences across systems
   - **Mitigation**: Relative memory growth testing rather than absolute values

---
## Success Metrics

### Development Quality Metrics
- **Zero Memory Leaks**: All components and integration scenarios leak-free
- **Memory Growth Bounded**: Predictable and bounded memory usage patterns
- **CI Integration**: Automated memory regression detection in place
- **Developer Experience**: Easy memory validation during development

### Production Readiness Metrics  
- **Extended Runtime Stability**: 24+ hour operation with stable memory usage
- **High Load Performance**: Stable memory under 120+ FPS sustained load
- **Error Recovery**: Proper memory cleanup during all error scenarios
- **Resource Efficiency**: Optimal memory usage for production deployment

### Documentation & Process Metrics
- **Comprehensive Documentation**: Complete memory testing procedures documented
- **Developer Training**: Team understanding of memory testing methodology
- **Process Integration**: Memory testing integrated into development workflow
- **Continuous Improvement**: Memory usage optimization based on testing results

---
## Appendix: Component Memory Architecture

### HAL Layer Memory Patterns
- **SensorDevice Lifecycle**: Create → Start → Frame Generation → Stop → Destroy
- **Frame Buffer Management**: Raw depth frame allocation and cleanup
- **Device Resource Management**: Hardware resource allocation and release

### Processing Layer Memory Patterns  
- **Frame Processing Pipeline**: Input buffer → Processing → Output buffer
- **Scale Transform Memory**: Float conversion and scaling operations
- **Callback Memory Safety**: Processing callback memory management

### Transport Layer Memory Patterns
- **SHM Double Buffering**: Producer/consumer buffer management
- **Socket Transport**: Network buffer allocation and cleanup  
- **Reader Lifecycle**: Attach → Read → Detach memory patterns

This comprehensive memory testing plan provides a structured approach to ensuring zero memory leaks and optimal resource management across the entire Caldera backend pipeline, with phased implementation and clear success metrics.