# Socket Transport Specification (UDS, v1)

This document describes the minimal wire protocol used by Caldera backend to stream `WorldFrame` data over stream sockets. The initial implementation uses Unix Domain Sockets (UDS). The protocol is transport-agnostic and designed to be reused over TCP without changes to the header or payload format.

## Goals
- Provide a simple, robust stream format for world frames (height maps) with optional integrity checks.
- Keep framing and metadata identical across UDS and future TCP.
- Ensure clean shutdown and predictable behavior under disconnects and reconnects.

## Endpoint
- UDS endpoint string: `unix:/absolute/path/to.sock`
- Server creates the socket file, listens with `backlog=1` (single subscriber initially), accepts the first client.

## Framing and Wire Format
Each frame is transmitted as a fixed-size header followed by a raw float32 payload.

Header layout (packed, little-endian):

```
struct WireHeader {
  char     magic[4];        // 'C','A','L','D'
  uint16_t version;         // 1
  uint16_t header_bytes;    // sizeof(WireHeader)
  uint64_t frame_id;        // monotonically increasing id
  uint64_t timestamp_ns;    // production timestamp (ns since epoch)
  uint32_t width;           // active width
  uint32_t height;          // active height
  uint32_t float_count;     // width * height
  uint32_t checksum;        // CRC32 of payload if checksum_algorithm==1, else 0
  uint32_t checksum_algorithm; // 0=none, 1=CRC32
} __attribute__((packed));
```

Payload:
- `float_count` 32-bit floats (`sizeof(float) * float_count` bytes), contiguous, row-major.

Validation rules:
- `magic == 'C','A','L','D'`
- `version == 1 && header_bytes == sizeof(WireHeader)`
- If `checksum_algorithm==1` and `checksum!=0`, client may compute CRC32 and compare to `checksum`.

## Checksum Policy
- If the originating `WorldFrame` provides a non-zero checksum, it is forwarded with `checksum_algorithm=1`.
- Otherwise, the server may periodically compute CRC32 based on `checksum_interval_ms` (environment variable `CALDERA_SOCKET_CHECKSUM_INTERVAL_MS`). In-between frames will have `checksum=0`.
- If interval is 0 (default), checksum is omitted (0).

## Server Behavior
- Accept loop runs on a background thread.
- On client accept, the client socket is set non-blocking to avoid shutdown stalls.
- `sendWorldFrame()` writes header then payload with a best-effort loop (`send` until complete). On any write error, the client is closed. Server continues listening for a new client.
- Shutdown: `stop()` calls `shutdown()` on client and listen sockets, joins the accept thread, closes file descriptors, and unlinks the UDS file.

## Client Behavior
- `SocketWorldFrameClient::connect(timeout_ms)` implements a non-blocking `connect()` with `select()`-based wait slices, then restores blocking mode for reads.
- `latest(verify_checksum)` blocks to read exactly one header+payload, validates header, optionally verifies CRC32, and returns a view over an internal payload buffer.
- Stats are tracked: observed frames, distinct frames, checksum present/verified/mismatch.

## Error Handling
- Header validation failure or short reads cause the client to disconnect and report `nullopt`.
- Server write failures close the client; new client may connect later.
- UDS path length is validated against `sockaddr_un.sun_path` size.

## Stepping Stone to TCP
The same header/payload format works over TCP with minimal code changes:
- Endpoint parsing: support `tcp:host:port` and use `AF_INET` sockets.
- Replace `AF_UNIX` + `sockaddr_un` with `AF_INET` + `sockaddr_in` in client/server.
- Reuse the exact same framing, checksum verification, and state machines.
No test or application changes are required: `IWorldFrameClient` and tests rely solely on the abstract interface.

## Testing
- Integration test `TransportSocketParity.ShmVsSocket_BasicCoverageAndCRC` launches `SensorBackend` in a child process with `CALDERA_TRANSPORT=socket` and connects a client:
  - Asserts `distinct_frames >= 10` within a short window.
  - Ensures `checksum_mismatch == 0` when checksums are present.
  - If latency samples recorded, asserts `p95_ms < 50`.

## Environment Variables
- `CALDERA_TRANSPORT=socket` — selects socket transport in `SensorBackend`.
- `CALDERA_SOCKET_ENDPOINT` — e.g., `unix:/tmp/caldera_worldframe.sock`.
- `CALDERA_SOCKET_CHECKSUM_INTERVAL_MS` — periodic checksum (0=disabled).

## Build Flag
- CMake option `CALDERA_TRANSPORT_SOCKETS` (default OFF). When ON, socket server/client and parity test are compiled. The `backend/build.sh` forwards this flag from env if `CALDERA_TRANSPORT_SOCKETS=1|ON`.
