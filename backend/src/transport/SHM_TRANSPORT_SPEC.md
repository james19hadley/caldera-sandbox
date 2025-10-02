# Shared Memory Transport Specification (Version 2 Implemented)

## Goals
Provide low-latency one-writer (backend) to one-reader (frontend) sharing of the latest `WorldFrame` height map.
Not a historical buffer; reader always sees most recently published frame.

## Design Overview
- POSIX shared memory object via `shm_open` with a fixed-size region sized for maximum configured map dimensions.
- Memory layout: [Header][Float height data]
- Writer (backend) overwrites the buffer each frame.
- Reader polls `ready` flag and `frame_id`; if changed, it copies or maps data.

## Header Structure (version 2)
Version 2 introduces a double-buffer header and per-buffer metadata eliminating the single-buffer race window and enabling lock-free flips.

```
struct BufferMeta {
  uint64_t frame_id;
  uint64_t timestamp_ns;    // production timestamp (steady clock)
  uint32_t width;           // active width in floats
  uint32_t height;          // active height in floats
  uint32_t float_count;     // width * height (redundant for convenience)
  uint32_t checksum;        // CRC32 (if algorithm=1) over float payload, 0 if not computed
  uint32_t ready;           // 0=being written,1=valid
};
struct ShmHeader {
  uint32_t magic;            // 0x43414C44 'CALD'
  uint32_t version;          // 2 (double buffer) -- bump if layout changes
  uint32_t active_index;     // buffer index reader should use (0/1)
  uint32_t checksum_algorithm;// 0 = none, 1 = CRC32 (poly 0xEDB88320)
  BufferMeta buffers[2];
};
```

### Checksum Semantics (Version 2 Extension)
The writer may optionally compute a CRC32 of the float buffer and store it in `BufferMeta.checksum` with `ShmHeader.checksum_algorithm = 1`.

Policies supported by current writer implementation:
1. Explicit: If the published `WorldFrame` already contains a non-zero checksum, that value is forwarded (no recompute) and `checksum_algorithm` is set to 1.
2. Periodic Auto: If configured with a non-zero `checksum_interval_ms`, the writer recomputes CRC32 no more often than that interval; frames in-between reuse the last computed checksum (still published) to amortize cost.
3. Disabled: If neither explicit nor periodic policy triggers, `checksum` remains 0 and `checksum_algorithm` is set to 0.

Reader Guidance:
- If `checksum_algorithm == 1` and `checksum != 0`, the reader may recompute CRC32 over the float payload to verify integrity.
- If `checksum_algorithm == 0`, the checksum field MUST be ignored (treat as not set).
- A non-zero checksum with `checksum_algorithm == 0` is a protocol violation (health test should flag).

Future algorithms (e.g., xxHash, CRC64) would use distinct non-zero ids; the field is intentionally global (per header) not per buffer to simplify negotiation. All buffers in the mapping use the same algorithm id.

## Publication Protocol (Double Buffer)
1. Writer chooses `write_index = 1 ^ active_index`.
2. Sets `buffers[write_index].ready = 0`.
3. Writes metadata (frame_id, timestamp, dims, float_count) and bulk float copy into the selected buffer region.
4. Issues full memory barrier.
5. Sets `buffers[write_index].ready = 1`.
6. Issues barrier and updates `active_index = write_index` (publishes frame atomically w.r.t. reader load order).

Reader algorithm:
1. Read `active_index` once.
2. Copy (or use view into) the floats of that buffer only if `buffers[idx].ready == 1`.
3. Optional: re-check `frame_id` / `active_index`; if changed mid-copy and strict coherence required, retry (currently tests rely on latest only so not needed).

## Capacity & Resizing
- Initial capacity fixed at construction: each buffer sized for `max_width * max_height` floats; total region contains two buffers.
- On overflow (dimensions exceed capacity) frame is dropped and a rate-limited warning (`shm_drop`) is emitted at most every 2s.

## Concurrency & Memory Ordering
- Single writer -> no locking needed.
- `__sync_synchronize()` barrier used before `ready=1` to ensure header/data visibility.
- Reader should issue an acquire fence when reading `ready` (on most compilers a simple volatile read + subsequent read is fine; future improvement: C++20 atomic_ref).

## Error Handling
- On `shm_open` / `mmap` failure: log error and drop frames until recovery attempt (not implemented yet).
- On capacity overflow: warning per frame (could rate-limit).

## Future Extensions
- Versioned extension blocks for objects / events.
- Atomics / C++20 memory model primitives (std::atomic_ref) for explicit release/acquire semantics.
- Named semaphore or eventfd for low-latency wakeups instead of polling.
- Partial frame / delta publishing.
 - Per-buffer algorithm id (if mixed algorithms become necessary) and integrity metadata (e.g., hash tree) for sub-region validation.

### Version Migration Notes
Previous version (1) used a single buffer + `ready` flag and is now superseded. Reader must check `version==2` to operate with the double-buffer layout. Backward compatibility shim not provided (prototype phase). If version mismatch occurs, reader should fail fast and request a rebuild / upgrade.

## Testing Strategy
Planned tests (future):
- Create writer, publish N frames, use a lightweight reader fixture to validate monotonically increasing frame_id & data integrity.
- Capacity overflow case triggers warnings and frame drop (simulate bigger width/height). 
- Stop/start id continuity (should continue incrementing, not reset).

Implemented tests:
- `SharedMemory.WriterReaderBasic` (updated) validates version 2 double-buffer publication with multiple frames and data scaling.
- `SharedMemory.OverflowDropFrame` validates overflow drop behavior and stable frame id.


