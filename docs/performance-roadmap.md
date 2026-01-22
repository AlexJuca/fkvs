# Performance Optimization Roadmap: 75K → 1M req/s

**Current baseline:** ~75K req/s (SET with random keys, TCP)
**Target:** 1M req/s
**Date:** 2026-01-22

## Critical Path Optimizations

Ordered by expected impact, with cumulative performance targets.

---

### 1. Pipelining - HIGHEST IMPACT (~4-10x gain)
**Expected: 300K-750K req/s**

Redis gets most of its speed from pipelining. The benchmark currently does:
```
send request → wait for response → send next request
```

With pipelining:
```
send 100 requests → read 100 responses (amortize network RTT)
```

**What to change:**
- Modify `fkvs-benchmark.c` to batch requests (lines 168-180)
- Server-side: read multiple commands from socket buffer before responding
- Write responses in a batch with `writev()` instead of individual `send()` calls

**Quick win:** Add `-P` flag to benchmark that sends N requests before reading responses.

**Implementation notes:**
- Start with depth of 10-100 pipelined requests
- Server needs to parse buffer until empty before writing responses
- Use `writev()` for batched response writes

---

### 2. Fix Hashtable - SECOND HIGHEST IMPACT (~2-3x gain)
**Expected: 150K-225K req/s → combine with pipelining for 600K-900K**

The hashtable does **4 mallocs per SET** and **2 mallocs + deep copy per GET**. This is killing performance.

**Current problems (src/core/hashtable.c):**
- Line 61, 65, 81, 95: Separate malloc for entry, key, value_entry_t, value
- Lines 136-156: Deep copy on GET with 2 mallocs
- Linked list chaining (cache misses on collision)

**Implement Swiss Tables (or similar):**
- Open addressing with SIMD probing
- Inline small keys/values (< 64 bytes) directly in entry
- No separate allocations for small data
- 2x better cache locality

**Or simpler optimization:**
- **Arena allocator** for hashtable entries (pre-allocate 10MB slab)
- Inline keys ≤ 15 bytes, inline values ≤ 31 bytes in entry struct
- Only return pointer on GET (no deep copy)
- This is the `perf-use-swiss-data-structure-for-increased-performance` branch work

**References:**
- Swiss Tables: https://abseil.io/about/design/swisstables
- Google's implementation: https://github.com/google/swiss-tables

---

### 3. Memory Allocation - CRITICAL (~1.5-2x gain)
**Expected: Combined with above = 900K+ req/s**

**Problems:**
- `src/commands/server/server_command_handlers.c:104`: `malloc(value_len + 1)` then immediately `free(data)` line 121
- `src/commands/server/server_command_handlers.c:136`: malloc for response buffer
- Every command does malloc/free on hot path

**Solutions:**
- **Per-connection buffer pool**: Preallocate 4KB buffer per client, reuse for all commands
- **Object pools**: Pre-allocate arrays of entry structs, never free them
- **Arena/region allocator**: Bump pointer allocation, reset after response sent
- **Remove pointless malloc** at lines 104-121 in `handle_set_command` (malloc, memcpy, then immediate free!)

**Implementation approach:**
```c
typedef struct client_t {
    // ... existing fields
    unsigned char *cmd_buffer;    // 4KB reusable buffer
    size_t cmd_buffer_size;
    unsigned char *resp_buffer;   // 4KB reusable buffer
    size_t resp_buffer_size;
} client_t;
```

---

### 4. Zero-Copy and I/O Batching (~1.3-1.5x gain)
**Expected: Push to 1M+ req/s**

**Current:**
- Single `recv()` call per event
- Single `send()` per response

**Optimize:**
- Read until `EAGAIN` (fill 64KB buffer)
- Parse multiple commands from buffer
- Batch responses with `writev()` or io_uring `IOSQE_IO_LINK`
- Avoid memcpy where possible (parse in place)

**Implementation:**
```c
// Read loop
while (1) {
    ssize_t n = recv(fd, buffer + offset, buffer_size - offset, 0);
    if (n <= 0) {
        if (errno == EAGAIN) break;  // Drained socket
        // handle error
    }
    offset += n;
}

// Parse all commands in buffer
while (offset >= MIN_COMMAND_SIZE) {
    // parse and execute command
    // add response to iovec array
}

// Write all responses at once
writev(fd, iovecs, iovec_count);
```

---

### 5. Protocol Efficiency (~1.1-1.2x gain)

The binary protocol is decent, but can be improved:

**Current overhead:**
- 2 bytes length + 1 byte cmd + 2 bytes key_len = 5 bytes minimum

**Optimizations:**
- Pre-encoded integer responses (PING → single byte 0x01)
- Varint encoding for lengths (saves bytes for small keys/values)
- Inline small responses (e.g., OK, PONG) without length prefix

**Example:**
```
PING response: currently [0x00, 0x01, 'O', 'K']
Optimized:     [0x01]  // 4 bytes → 1 byte
```

---

### 6. CPU Optimizations (~1.1-1.3x gain)

**Compiler flags:**
```makefile
CFLAGS += -O3 -march=native -flto -fno-omit-frame-pointer
```

**Hash function:**
- Replace DJB2 (src/core/hashtable.c:8-16) with xxHash or CityHash (3-5x faster)
- Or use hardware CRC32C (`_mm_crc32_u64` on x86, `__crc32cd` on ARM)

**Hot path optimization:**
- Mark command handlers with `__attribute__((hot))`
- Use `__builtin_expect` for error paths
- Use `__builtin_prefetch` for hashtable lookups
- Profile with `perf record` and `perf report` to find hotspots

**Example:**
```c
__attribute__((hot))
void handle_set_command(int client_fd, unsigned char *buffer, size_t bytes_read) {
    if (__builtin_expect(bytes_read < 5, 0)) {
        // error handling
        return;
    }
    // hot path
}
```

---

## Implementation Priority

### Phase 1: Low-Hanging Fruit (→ 300K req/s)
**Effort: 1-2 days**

1. ✅ Remove pointless malloc/free in command handlers (lines 104-121 in server_command_handlers.c)
2. ✅ Add pipelining support to benchmark (`-P` flag)
3. ✅ Server: batch read until EAGAIN
4. ✅ Server: batch write with writev()
5. ✅ Add per-connection buffer pools (4KB)

**Expected result:** ~250-300K req/s

---

### Phase 2: Data Structure Overhaul (→ 600K req/s)
**Effort: 1-2 weeks**

1. ✅ Implement arena allocator for hashtable entries
2. ✅ Inline small keys/values in entry struct
3. ✅ Remove deep copy on GET (return pointer)
4. ✅ Switch to Swiss Tables or open addressing
5. ✅ Replace DJB2 with xxHash

**Expected result:** ~500-700K req/s

---

### Phase 3: Polish to 1M (→ 1M req/s)
**Effort: 1 week**

1. ✅ Full zero-copy parsing
2. ✅ io_uring optimization (batch operations with SQ/CQ)
3. ✅ Protocol efficiency improvements
4. ✅ Profile-guided optimization (PGO)
5. ✅ Compiler optimizations (-march=native, LTO)

**Expected result:** ~900K-1.2M req/s

---

## Quick Wins (This Weekend)

These four changes could get you to **250-300K req/s**:

1. **Remove wasteful malloc** in `src/commands/server/server_command_handlers.c:104-121`
   - Currently: allocate, copy, immediately free
   - Just remove it entirely

2. **Add pipelining to benchmark**
   - Add `-P N` flag to send N requests before reading
   - Start with N=100

3. **Pre-allocate buffers per client**
   - Add 4KB cmd_buffer and resp_buffer to client_t
   - Reuse for all operations

4. **Don't deep copy on GET**
   - Lines 136-156 in `src/core/hashtable.c`
   - Return pointer instead of allocating + copying

---

## Measurement Strategy

After each optimization phase:

1. **Run baseline benchmark:**
   ```bash
   ./fkvs-benchmark -n 1000000 -c 25 -t set -r
   ```

2. **Profile with perf:**
   ```bash
   perf record -g ./fkvs-server -c
   perf report
   ```

3. **Check for regressions:**
   - Memory usage (should stay flat or decrease)
   - Latency percentiles (p50, p95, p99)
   - CPU utilization

4. **Document results:**
   - Update this file with actual numbers
   - Note any unexpected bottlenecks discovered

---

## Bottleneck Analysis (Current State)

Based on code review, expected hot spots:

1. **malloc/free** - Every command allocates/frees memory (30-40% of CPU time)
2. **hashtable lookups** - Linked list chaining causes cache misses (20-30%)
3. **memcpy** - Deep copies on GET, unnecessary copies in SET (10-15%)
4. **Network I/O** - Single recv/send per command (10-20%)
5. **Hash function** - DJB2 is slow (5-10%)

Run `perf record` to validate these assumptions.

---

## References

- Redis performance: https://redis.io/docs/management/optimization/benchmarks/
- TigerBeetle architecture: https://github.com/tigerbeetle/tigerbeetle/blob/main/docs/DESIGN.md
- Swiss Tables: https://abseil.io/about/design/swisstables
- xxHash: https://github.com/Cyan4973/xxHash
- Linux perf: https://perf.wiki.kernel.org/index.php/Tutorial

---

## Notes

- Redis (single-threaded) achieves ~100K-200K req/s over TCP without pipelining
- With pipelining, Redis can hit 1M+ req/s on Unix sockets
- TigerBeetle shows that single-threaded design is production-ready when optimized
- The current ~75K req/s is respectable but has clear optimization opportunities
- Swiss table branch (`perf-use-swiss-data-structure-for-increased-performance`) should be prioritized
