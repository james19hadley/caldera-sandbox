# Caldera Backend Logging Subsystem

## 1. High-Level Idea (Short Version)
A single `Logger` singleton configures shared output sinks (color console + rotating file) exactly once. Every module asks for a named logger (`Logger::instance().get("HAL")`). All named loggers write through the same sinks, inherit a global log level unless a per-logger override is specified, and honor flush policies (periodic + on warning level and above). Log levels are controllable by code and by an environment variable (`CALDERA_LOG_LEVEL`). The system prevents accidental use before initialization, safely ignores repeated initialization, and automatically creates the target log directory.

---
## 2. Goals
- Central place to configure formatting, sinks, rotation, and flushing
- Cheap creation of many semantic loggers (per subsystem) without duplicating file handles
- Runtime control over verbosity (global + per logger)
- Fail fast if the API is misused (no silent logging black holes)
- Allow ops / developers to raise or lower verbosity without code changes (env var)
- Minimize I/O loss on crash (periodic + level-triggered flush)
- Keep implementation thread-safe

---
## 3. Key Components
### 3.1 Class: `Logger`
Responsibility: own shared sinks and provide factory/accessor for `spdlog::logger` instances.

Internal members:
- `bool initialized_` – guard against premature use / repeat init
- `spdlog::level::level_enum globalLevel_` – current global fallback level
- `std::unordered_map<std::string, level_enum> perLoggerLevels_` – explicit overrides
- `std::vector<std::string> loggerNames_` – names of created loggers for bulk updates
- `std::mutex mutex_` – protects mutable shared state
- `console_sink_` (`stdout_color_sink_mt`)
- `file_sink_` (`rotating_file_sink_mt`)

### 3.2 Sinks
- Console: colored, immediate developer feedback
- Rotating file: (size = 5 MB, backups = 3) -> simple bounded retention (~20 MB total)

### 3.3 Environment Variable
`CALDERA_LOG_LEVEL` (e.g. `trace`, `debug`, `info`, `warn`, `err`, `critical`, `off`)
If set and valid, it overrides the `defaultLevel` passed to `initialize()`.

---
## 4. Lifecycle & Control Flow
### 4.1 Initialization
`Logger::initialize(path, defaultLevel, flushEvery, flushOn)`:
1. Acquire mutex.
2. Abort (warn) if already initialized.
3. Create directory for `path` (non-throwing; warn on failure).
4. Construct shared sinks.
5. Set global pattern: `[date time.micros] [logger-name] [LEVEL] message`.
6. Determine effective global level: env var first, else provided `defaultLevel`.
7. Apply global level to spdlog registry (`spdlog::set_level`).
8. Configure flushing:
   - `flush_on(flushOn)` (every message >= `flushOn` forces flush)
   - `flush_every(flushEvery)` (background thread periodic flush)
9. Mark `initialized_ = true`.

### 4.2 Creating / Fetching a Named Logger
`get(name)`:
1. Acquire mutex.
2. Throw if not initialized (fail fast).
3. Return existing if registered in spdlog.
4. Build a `dist_sink_mt` and attach the stored sinks (console + file).
5. Create new `spdlog::logger` with that dist sink.
6. Apply per-logger level override if present, else global level.
7. Register with spdlog and record name in `loggerNames_`.
8. Return shared pointer.

### 4.3 Setting Global Level
`setGlobalLevel(level)`:
1. Acquire mutex.
2. Store `globalLevel_` and call `spdlog::set_level(level)`.
3. Iterate known `loggerNames_`; for each without an explicit override update its level.

### 4.4 Per-Logger Override
`setLoggerLevel(name, level)` stores override; if logger already exists sets immediately.
`clearLoggerLevel(name)` removes override; if logger exists resets it to `globalLevel_`.

### 4.5 Shutdown
`shutdown()`:
1. Acquire mutex; if not initialized return.
2. Call `spdlog::shutdown()` (closes/flushes all registered loggers & sinks).
3. Reset sink shared pointers.
4. Mark `initialized_ = false`.

---
## 5. Thread Safety
- All state mutations guarded by `std::mutex`.
- spdlog's `*_mt` sinks provide internal thread-safety for concurrent logging.
- Avoids direct access to spdlog private registry (we track created names ourselves).

---
## 6. Error Handling & Edge Cases
| Scenario | Behavior |
|----------|----------|
| `get()` before `initialize()` | Throws `std::runtime_error` |
| Repeated `initialize()` | First wins, later calls log a warning |
| Invalid `CALDERA_LOG_LEVEL` | Ignored (fallback to provided default) |
| Directory creation failure | Warning, continues (may still fail opening file) |
| Env level change after init | Ignored (only read once) |
| Per-logger override then global level change | Override persists |
| Logger created after setting per-logger override | Gets override immediately |
| `shutdown()` without init | No-op |

---
## 7. Performance Considerations
- Single shared rotating file sink prevents file descriptor explosion.
- Dist sink fan-out cost is minimal (two sinks only).
- Periodic flush (1s default) adds lightweight background thread; can be increased if I/O pressure is high.
- Fine-grained per-logger levels allow silencing hot paths.
- For even higher performance later: switch to async logging (introduce thread pool + `spdlog::async_logger`). Currently not enabled to keep failure/debug path simple.

---
## 8. Example Usage
```cpp
using caldera::backend::common::Logger;

int main() {
    Logger::instance().initialize("logs/backend/backend.log", spdlog::level::info);

    // Make HAL chatty
    Logger::instance().setLoggerLevel("HAL", spdlog::level::trace);

    auto core = Logger::instance().get("Core");
    core->info("Boot sequence start");

    auto hal = Logger::instance().get("HAL");
    hal->trace("Enumerating devices...");

    // Lower noise globally (HAL stays trace due to override)
    Logger::instance().setGlobalLevel(spdlog::level::warn);

    core->warn("Low memory warning");

    Logger::instance().shutdown();
}
```

Exporting an environment variable to silence almost everything at runtime:
```bash
export CALDERA_LOG_LEVEL=off
./SensorBackend
```

---
## 9. Future Enhancements (Optional Roadmap)
- Async logging with bounded queue for bursts.
- JSON sink for structured log ingestion.
- Session / correlation IDs appended automatically.
- Compile-time disabling of low levels via `SPDLOG_ACTIVE_LEVEL` macro in release builds.
- Custom sink broadcasting logs over UDP to a central aggregator (multi-process merging).

---
## 10. Summary
This logging subsystem centralizes configuration, enforces correct initialization, supports dynamic verbosity control (global + per-logger), and improves reliability with automatic directory creation and flush strategies—all while remaining simple and synchronous for straightforward debugging.
