# Integration Testing Plan (Backend Pipeline)

Location rationale: lives alongside tests to co-evolve with test harness code (design intent doc; user-facing docs stay under `/docs`).

## Scope & Goals
Vertical validation from synthetic sensor source → HAL → ProcessingManager (current minimal transform) → Shared Memory Transport → Test client (reader). No new business logic; tests exercise existing code paths and provide scaffolding for future expansion (multi-sensor, filtering, events).

---
## Legend
- **Existing Infra**: Code already present and reusable.
- **Existing Tests**: Current test files in `backend/tests`.
- **Gap**: Missing capability for integration scope.
- **Planned**: To be implemented after approval.
- **Deferred**: Not implementable until feature lands.

---
## Phase 0: Harness Enablement & Synthetic Sensor
Purpose: Deterministic frame generation & unified start/stop harness.

### Existing Infra
- HAL: `HAL_Manager.*`, `ISensorDevice.h`, `MockSensorDevice.*` (file playback only), recorder & real Kinect devices.
- Processing: `ProcessingManager.*` (depth→height scaling + frame counter).
- Transport: `SharedMemoryTransportServer.*`, `SharedMemoryReader.*`, `LocalTransportServer.*` (for metadata/stats heartbeat).
- Utilities: `TestLocalTransportClient.*`, `TestFramePattern.h`, `Checksum.h`, `StatsUtil.h`, logger.
- SHM verification tests (`test_shm_verified_matrix.cpp`) already use a deterministic pattern (direct writer path, not via HAL/Processing).

### Gaps
- No synthetic in-memory sensor (no file dependency) for integration path.
- No harness orchestrating sensor(s) + processing + transport publication.
- No integration test that consumes WorldFrame via SHM produced through full chain.
- No hidden ground-truth checksum for integrity assertion across the pipeline.

### Planned Additions
1. `SyntheticSensorDevice` (implements `ISensorDevice`):
   - Config: width, height, fps, pattern enum {RAMP, CONSTANT, CHECKER}.
   - Internal frame_id counter; generates `RawDepthFrame` only (color left empty for now).
   - Generates deterministic data each frame (pattern does not change except frame_id/timestamp).
   - Maintains internal secret checksum of base pattern (stored once) — not exposed; tests recompute from received float map and compare to expected pattern regenerated independently.
2. `IntegrationHarness` (light struct/class under `tests/` or `src/integration/`):
   - Members: vector of `unique_ptr<ISensorDevice>`, HAL_Manager (optional if needed— or direct control), ProcessingManager, SharedMemoryTransportServer.
   - API: `addSyntheticSensor(config)`, `start(shm_config)`, `waitForFrames(count, timeout_ms)`, `stop()`.
   - Processing callback -> calls `transport.sendWorldFrame(frame)`.
3. Test file: `test_integration_phase0_basic.cpp`:
   - Test: `IntegrationPhase0.SingleSyntheticSensorPassThrough`.
   - Steps: start harness with 16x16 RAMP @ ~30 FPS; collect ≥10 frames via SharedMemoryReader; assert:
     - Monotonic frame_id.
     - Height map size correctness.
     - CRC32(heightMap floats reinterpreted bytes or via stable float->uint32 hash) == expected recomputed from pattern.
     - Transport stats frames_published ≥ received.
4. Helper: pattern regeneration for verification (no dependency on sensor internals) + CRC helper.

### Acceptance Criteria
- Test passes reliably (re-run safe).
- No SHM leak (segment unlinked after stop()).
- Secret checksum never directly exposed (only derived in test by regeneration).
- Harness idempotent start/stop.

### Implementation Status (Live Notes)
- SyntheticSensorDevice implemented (RAMP, CONSTANT, CHECKER, STRIPES, RADIAL) in `hal/`.
- IntegrationHarness implemented (single-sensor start/stop, SHM publish via ProcessingManager callback).
- Pass-through test relocated & renamed: `integration/synthetic_sensor_pass_through.cpp` (suite `SyntheticSensorPipeline.SingleSensorPassThroughRamp`).
- Next: latency measurement (Phase 5) after confirming scale semantics.

### Pattern Glossary (SyntheticSensorDevice)
| Pattern | Definition | Purpose |
|---------|------------|---------|
| RAMP | depth[x,y] = x + y | Simple monotonic gradient; easy to predict & scale. |
| CONSTANT | depth[x,y] = constantValue | Baseline for verifying uniform scaling / filter neutrality. |
| CHECKER | Alternating 2x2 blocks: (x/2 + y/2) parity -> high (2000) or low (500) | Creates sharp edges to later stress filters / compositor (edge preservation). |
| STRIPES | Horizontal 4-pixel tall bands alternating 1800 / 600 | Directional structure (Y-axis) to test future anisotropic filters / blending. |
| RADIAL | Concentric gradient center→edge (center≈2000 edge≈0) | Radial symmetry for testing isotropic smoothing & distance-based logic. |

Checker rationale: using 2x2 block size avoids trivial alternation at every pixel (which can over-emphasize cache/memory effects) while still producing high‑contrast, spatially structured regions to test future smoothing, blending or compression logic. Values (2000 / 500) chosen to produce a clear scaled float delta without overflow.

### Risks / Notes
- Keep SyntheticSensorDevice minimal (no color, no noise) to avoid accidental semantic coupling.
- Using system sleep for FPS is acceptable; deterministic timing not required now.
- HAL_Manager may be bypassed initially if it adds complexity; optional integration in later refactor.

---
## Phase 1: Single Sensor Semantic Integrity
Purpose: Assert scaling factor semantics of ProcessingManager.

### Existing Infra
- `ProcessingManager::processRawDepthFrame` applies depth * scale (scale from ctor or env `CALDERA_DEPTH_SCALE`).

### Gaps
- No test verifying scaled float values end-to-end.

### Planned / In Progress
- Test file `integration/processing_scale_semantics.cpp` with two cases (scale 0.001 and 0.010) using harness `processing_scale` override.
- RAMP pattern ensures expected[i] = (x+y)*scale.
- Element-wise ASSERT_NEAR epsilon 1e-6.

### Implementation Status
- Harness supports `processing_scale` injection.
- Scale semantics test passing under new suite name `ProcessingScaleSemantics`.

### Acceptance
- All values pass epsilon check.
- Frame counter starts at 0 and increments by 1.

---
## Phase 2: Multi-Sensor Composition (Deferred)
Current codebase: single-source pipeline; no compositor abstraction yet.

### Feasibility Note
- Future plan: HAL_Manager holds multiple devices; ProcessingManager introduces aggregation stage (FrameCompositor) keyed by sensorId; final frame_id increments per composed set.
- Architectural barrier: none (RawDepthFrame contains sensorId; can bucket frames). Low future complexity.

### Status
- Deferred; no test until compositor lands.

---
## Phase 3: Stabilization / Filtering (Placeholder)
Current behavior: identity pass-through (no smoothing). Future `IHeightMapFilter` interface to allow pluggable filters (NoOpFilter for baseline, SmoothingFilter later).

### Planned (Now)
- Add interface skeleton `IHeightMapFilter` + `NoOpHeightMapFilter` (returns input unchanged). Wire into ProcessingManager internally or via optional injection parameter (deferred actual integration until needed if risk of churn).

### Acceptance (Now)
- Document only; tests continue to expect identity.

---
## Phase 4: Events & Objects Path (Deferred)
- Data structures present; no producers.
- Future: object & event batches over FIFO heartbeat JSON. (Large bulk stays in SHM.)
- Deferred until producers implemented.

---
## Phase 5: Transport Round-Trip Latency & Integrity
Status: IMPLEMENTED

### Implementation
- New test `integration/test_transport_latency.cpp` (suite `TransportLatency.SingleSensorLatencyP95WithinBudget`).
- Uses 16x16 RAMP synthetic sensor @30 FPS with scale=0.001.
- Polls `SharedMemoryReader.latest()` to collect 25 frames; computes latency = now - frame.timestamp_ns (steady_clock) and validates integrity via regenerated RAMP pattern CRC over float buffer.
- P95 assertion: <15 ms (soft target remains <10 ms per plan; cushion added to avoid CI flakiness). Max latency must be <25 ms.

### Rationale for Slight Relaxation
- Polling interval (1 ms) and general-purpose CI scheduling can inflate latency distribution; buffer allows stability while still catching regressions.

### Next
- Phase 6 (fault injection) will reuse this latency infrastructure to observe impact of drops / pauses.

---
## Phase 6: Fault Injection (Sensor Drop / Stutter)
### Planned
- Extend SyntheticSensorDevice with `stopAfter(n)` + `pause()/resume()`.
- Test ensures pipeline continues running (no deadlocks) and stats reflect reduced frames.

---
## Phase 7: Performance / Stress (Lightweight)
### Planned
- 120 FPS synthetic for short window (e.g., 2s) – assert received/published ratio ≥ 0.8; zero unexpected drops.

---
## Phase 8: Protocol / Versioning Integration
### Planned
- Start harness; connect transport client mid-stream; confirm first received frame_id within last 1–2 published ids.

---
## Phase 9: Metrics & Observability
### Planned
- Introduce `PipelineStats` (frames_in, frames_out, frames_published, drops, last_latency_ns) maintained in harness callback.
- Test ensures frames_in == frames_out when no drop scenarios.

---
## Phase 10: Future Extensions
- Network transport replication of Phase 5 tests.
- Multi-process black-box (launch `SensorBackend`).
- Advanced filters (variance/hysteresis) wired via `IHeightMapFilter`.

---
## Data Flow (Current Copy Chain)
Synthetic (uint16_t vector) → ProcessingManager (scaled float vector) → SHM double buffer memcpy → Reader (memcpy or direct pointer snapshot). Multiple copies acceptable for correctness focus; future optimization optional.

---
## Integrity Strategy
- SyntheticSensorDevice generates deterministic base pattern; internal (secret) checksum stored once for debug (not exposed in API).
- Tests independently regenerate expected pattern and compute CRC32 over raw float bytes (reinterpret_cast to uint8_t sequence) — comparison validates full end-to-end path without trusting device internals.

---
## Virtual Clock (Deferred)
- Not implemented; real time adequate (target sensors ≤ 40 FPS). Option kept in plan only if latency assertions become flaky under CI contention.

---
## Global Metrics Targets (Initial)
| Metric | Target | Phases |
|--------|--------|--------|
| Frame ID Monotonicity | Strict | 0+ |
| CRC Integrity | 100% | 0+ |
| Scale Error | <1e-6 | 1 |
| Latency P95 | <10ms (small 16x16) | 5 |
| Coverage (no fault) | ≥0.95 | 5,7 |
| Coverage (stress 120 FPS) | ≥0.80 | 7 |
| Recovery (drop) | <1 frame gap | 6 |

---
## Implementation Order (Confirmed)
1. Phase 0: SyntheticSensorDevice + IntegrationHarness + basic pass-through test.
2. Phase 1: Scale test.
3. Phase 5: Latency & integrity extension.
4. Phase 6: Fault injection.
5. Phase 7: Throughput/coverage.
6. Phases 2/3/4/8/9 evolve as features appear (deferred items tracked).

---
## Appendix: Feasibility – Multi-Sensor
- Add vector of devices to harness; pass identical callback.
- Introduce compositor later; no need to change SyntheticSensorDevice.
- Minimal disruption expected.

(End of Integration Testing Plan)
