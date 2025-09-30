# Project Caldera

## Backend Build & Test Workflow

The backend (`backend/`) uses CMake + vcpkg and provides convenience scripts.

### Prerequisites
1. Environment variable `VCPKG_ROOT` must point to your vcpkg installation.
2. Dependencies are declared in `backend/vcpkg.json` (currently `spdlog`, `gtest`).

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

---
For architectural overview see `01_Architectural_Overview.md`.
