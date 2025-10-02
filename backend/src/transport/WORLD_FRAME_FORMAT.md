## WorldFrame Binary / SHM Representation

Authoritative layout for the shared memory transport (see also `SHM_TRANSPORT_SPEC.md`).

Logical WorldFrame (producer side):
```
struct WorldFrame {
  uint64_t frame_id;       // monotonic
  uint64_t timestamp_ns;   // steady_clock
  HeightMap {
     int width;
     int height;
     std::vector<float> data; // width*height floats
  } heightMap;
  uint32_t checksum;       // 0 or CRC32 of data
};
```

Shared Memory Mapping:
```
[ ShmHeader | Buffer0 float region | Buffer1 float region ]

struct BufferMeta {
  uint64_t frame_id;
  uint64_t timestamp_ns;
  uint32_t width;
  uint32_t height;
  uint32_t float_count;    // width*height
  uint32_t checksum;       // CRC32 if header.checksum_algorithm==1 else 0
  uint32_t ready;          // 0 writing, 1 valid
};
struct ShmHeader {
  uint32_t magic;             // 'CALD'
  uint32_t version;           // 2
  uint32_t active_index;      // 0/1 selects buffer
  uint32_t checksum_algorithm;// 0 none, 1 CRC32
  BufferMeta buffers[2];
};
```

Double-buffer publication steps (writer):
1. write_index = 1 ^ active_index
2. meta.ready=0; fill metadata & floats
3. barrier; meta.ready=1
4. barrier; active_index=write_index

Reader algorithm simplified: read active_index once, copy/view buffer if ready==1.

Versioning Notes:
- Increment header.version if order/size changes or semantics become incompatible.
- Add new fields at tail to preserve struct prefix layout (future compatibility).

Checksum Policy Recap:
- frame.checksum!=0 -> forwarded verbatim, checksum_algorithm=1
- frame.checksum==0 & interval triggered -> compute CRC32
- else -> checksum=0 (algorithm may remain 1 to indicate capability or 0 to indicate none; currently writer sets 1 globally and leaves per-frame 0 when skipped)

Any consumer must treat (checksum==0) as "no integrity guarantee".
