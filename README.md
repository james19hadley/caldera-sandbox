# Project Caldera

## Backend Build & Test Workflow

The backend (`backend/`) uses CMake + vcpkg and provides convenience scripts.

### Prerequisites
1. Environment variable `VCPKG_ROOT` must point to your vcpkg installation (auto-detected at `~/vcpkg` if unset).
2. Initialize git submodules (needed for Kinect v1 vendored dependency):
```
git submodule update --init --recursive
```
3. Dependencies managed by vcpkg are declared in `backend/vcpkg.json` (currently `spdlog`, `gtest`, plus others as added).
4. (Optional) If you do NOT need Kinect v1 support you can temporarily remove / skip the `vendor/libfreenect` submodule, but note: current `CMakeLists.txt` treats libfreenect as mandatory and will FATAL_ERROR if the vendor directory is absent. (A toggle `CALDERA_WITH_KINECT_V1` may be added later.)

### Build (fresh)
From `backend/` directory:
```
./build.sh
```
This performs a clean (removes `build/`), configures CMake with tests enabled, and builds the test target (which also builds the main executable via static core library linkage).

### Incremental Build
When you only changed a few source files:
```
./build.sh -i
```
or
```
./build.sh --incremental
```
This reuses the existing `build/` directory and only recompiles what changed.

### Running Tests
After building:
```
./test.sh
```
Pass Google Test filters / flags directly:
```
./test.sh --gtest_filter=LoggerBasic.*
```

Alternate (CTest enumeration of each test case) – useful in CI pipelines for selective runs:
```
./test.sh --ctest -R SharedMemory
```
This uses CTest's discovery (internally powered by `gtest_discover_tests`) so every individual GoogleTest TEST/TEST_F appears as its own CTest test. The default `./test.sh` invocation runs the raw gtest binary (faster startup, full colored output).

Colorized output is forced when run in a TTY; for CI logs you can still pass `--gtest_color=yes`.

### Artifacts
- Executable: `backend/build/SensorBackend`
- Tests binary: `backend/build/CalderaTests`
- Logs (runtime): `backend/logs/...` (logger creates directories automatically)

### Logging Architecture Summary
- Central singleton `Logger` sets up shared sinks (console + rotating file, 5MB x3).
- Named loggers (e.g. `Processing.Fusion`, `HAL.Manager`) requested via `Logger::instance().get(name)`.
- Global level + per-logger overrides with environment override `CALDERA_LOG_LEVEL`.
- Flush policies: periodic + level-triggered.
- Tests cover initialization, level inheritance, overrides, directory creation.

### Static Core Library
Common backend code (logger, HAL mock, processing, transport, orchestrator) is compiled into `caldera_backend_core` (static library). Both the main executable and tests link against it, reducing duplication and ensuring consistency.

### Typical Developer Loop
```
./build.sh -i            # fast incremental build
./test.sh --gtest_filter=Levels.*
./build.sh               # occasional full clean build
```

### Next Steps (Roadmap Hints)
- Add integration pipeline test with mock transport.
- Expand data structures (`RawDataPacket`, `WorldFrame`).
- Introduce async logging (optional) and structured logging (JSON) if needed.
- Optional CMake flag to make Kinect v1 truly optional (relax current mandatory vendor requirement).

---
For architectural overview see `01_Architectural_Overview.md`.

## Kinect Sensors Note

Kinect v1 support (experimental) and general sensor setup details are consolidated in `backend/KINECT_SETUP.md`. The v1 path relies on a vendored `libfreenect` submodule auto-built by `build.sh` (because upstream vcpkg lacks a port). A future build option will allow disabling it for minimal environments.

## Shared Memory Transport Specification

The binary layout and publish protocol for the shared memory world-frame transport is documented in:
`backend/docs/SHM_TRANSPORT_SPEC.md`

Version 2 (double-buffer) is implemented: two buffers + `active_index` with memory barriers ensure readers always see a fully written frame. Oversized frames are dropped with a rate-limited warning.

## Environment Variables Summary

| Variable | Purpose | Example | Default |
|----------|---------|---------|---------|
| `CALDERA_LOG_LEVEL` | Override global log level (`trace,debug,info,warn,error,critical,off`) | `CALDERA_LOG_LEVEL=debug` | `info` |
| `CALDERA_LOG_QUEUE_SIZE` | Async spdlog queue size | `CALDERA_LOG_QUEUE_SIZE=8192` | 4096 (example if set in code; see Logger.cpp) |
| `CALDERA_LOG_WORKERS` | Async logging worker thread count | `CALDERA_LOG_WORKERS=2` | 1 (if not overridden) |
| `CALDERA_DEPTH_SCALE` | Scale factor converting depth units to height map | `CALDERA_DEPTH_SCALE=0.001` | 0.01 (code default) |

Notes:
- Per-logger overrides can still be set at runtime; env only sets initial global level.
- If queue/workers are provided, async logging is initialized; otherwise fallback is synchronous.

## Testing Overview (Detailed)

Two layers:
1. Raw GoogleTest binary (`CalderaTests`) – fast iteration, full color, gtest-native filters.
2. CTest integration – enumerates each test individually so CI can run subsets (e.g. `ctest -R Processing`).

Script `backend/test.sh` abstracts both: standard run uses binary; passing `--ctest` switches to CTest mode. Example selective runs:
```
# Single test case via gtest filter
./test.sh --gtest_filter=SharedMemory.WriterReaderBasic

# All SharedMemory related tests via CTest regex
./test.sh --ctest -R SharedMemory
```

Why CTest? Benefits:
- Native integration with CMake/IDE tools (CTest dashboards, CDash, etc.).
- Granular pass/fail accounting per test case in CI.
- Easy future extension (memcheck, sanitizers, labels).

Why keep the direct gtest path? Simplicity + fastest local feedback (no discovery step) + guaranteed color formatting.

## Adding New Tests
1. Drop a new `test_*.cpp` file in `backend/tests/` and add it to the `CalderaTests` executable in `tests/CMakeLists.txt`.
2. Use standard GoogleTest macros.
3. Re-run `./build.sh -i` then `./test.sh`.
4. For large stress tests, consider adding a `// LARGE:` comment so they can be optionally filtered later (`-R` or `--gtest_filter`).

## Troubleshooting
| Symptom | Cause | Fix |
|---------|-------|-----|
| `No tests were found` (ctest) | Top-level `enable_testing()` missing (fixed now) | Ensure `enable_testing()` present before adding tests | 
| Log spam in concurrency test | Global level not set early enough | Test sets level to `off`; verify before spawning threads |
| Missing shared memory file | Writer not started yet | Start backend or ensure test creates writer before reader |

## CI Suggestions (Optional)
- Fast tier: run `./test.sh --gtest_filter=-ProcessingStress.*` to skip heavy tests.
- Full tier (nightly): `./test.sh --ctest` plus sanitizer builds (`-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined"`).

---
Reach out / open an issue for questions about extending the transport, logging strategy, or adding new environment-configurable parameters.
