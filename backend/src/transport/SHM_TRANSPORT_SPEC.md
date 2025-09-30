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
  uint64_t timestamp_ns;
  uint32_t width;
  uint32_t height;
  uint32_t float_count;
  uint32_t ready; // 0=being written,1=valid
};
struct ShmHeader {
  uint32_t magic;       // 0x43414C44 'CALD'
  uint32_t version;     // 2
  uint32_t active_index;// buffer index reader should use (0/1)
  uint32_t reserved;    // future flags / alignment
  BufferMeta buffers[2];
};
```

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


