# io_uring Findings: When and How to Use It in fkvs

**Date:** 2026-02-25
**Based on:** Benchmark data from the `implement-pipelining` branch and analysis of
*"io_uring for High-Performance DBMSs: When and How to Use It"* (Jasny et al., VLDB 2026)

**Paper reference:** https://github.com/mjasny/vldb26-iouring

---

## Current State: Why io_uring is 8% Slower Than epoll

Our benchmarks on the `implement-pipelining` branch show that epoll (Linux) outperforms
io_uring by ~8% at P=128 with UDS:

| Event Loop | P=128 Avg (req/s) | Std Dev |
|---|---|---|
| epoll | 3,490,216 | ~73K (2.1%) |
| io_uring | 3,218,621 | ~22K (0.7%) |

This is the **expected outcome** for a naive io_uring integration. The VLDB paper
confirms that a drop-in replacement of epoll with io_uring yields only 1.10x improvement
for network I/O, and can even regress when the architecture doesn't exploit io_uring's
capabilities.

---

## Root Causes (Paper-Validated)

### 1. fkvs uses io_uring as a drop-in, not an architectural redesign

The paper identifies three io_uring execution paths:

- **(2a) Inline completion** -- operation completes immediately (socket has data)
- **(2b) Non-blocking poll set** -- io_uring installs event handler, wakes on readiness
- **(2c) Blocking fallback** -- delegates to `io_worker` threads, adds ~7.3us overhead

Our `event_dispatcher_io_uring.c` submits individual `recv()`/`send()` operations
through the ring without batching them. Each client event = one SQE submission + one
CQE reap. This is the paper's "synchronous io_uring" pattern where io_uring matches
or slightly underperforms the traditional interface.

### 2. SQE batching is not happening

The paper shows batch size 16 reduces CPU cycles per operation by 5-6x:

```
Cycles/OP at batch=1:   ~900
Cycles/OP at batch=16:  ~150
```

In fkvs, pipelining batches at the **application protocol level** (128 commands before
reading responses), but the io_uring event loop still processes events one-at-a-time.
The 3.49M req/s gains come from `wbuf_flush()` write coalescing, not from io_uring
SQE batching.

### 3. Missing critical io_uring features

| Paper Guideline | fkvs Status | Expected Impact |
|---|---|---|
| **GL1**: I/O is the bottleneck? | CPU is the bottleneck (single-threaded) | io_uring optimizations have limited impact when CPU-bound |
| **GL2**: Align architecture | Event loop is reactive, no fiber/coroutine overlap | No compute/I/O overlap within the event loop |
| **GL3**: Use DeferTR + single-issuer | Not configured | ~17% latency reduction |
| **GL4**: Registered buffers | Not used | Eliminates per-I/O page pinning cost |
| **GL4**: Zero-copy send/recv | Not used | 2x memory bandwidth reduction |
| **GL4**: RECVSEND_POLL_FIRST | Not used | Up to 1.5x reduction in CPU cycles |

### 4. epoll wins because application-level batching already amortizes syscall cost

The paper states:

> "Without zero-copy, epoll is only marginally slower than io_uring despite issuing
> more system calls. This behavior stems from both implementations transferring tuples
> in large chunks, which amortizes syscall and I/O-path overhead."

In fkvs with pipelining, `wbuf_flush()` sends a 64KB buffer in a single `send()`, and
`recv()` reads large chunks of pipelined commands. Per-syscall overhead is already
amortized. Meanwhile io_uring adds overhead from ring buffer management, memory
barriers, and speculative inline completion attempts.

---

## The Single-Threaded CPU Ceiling

The paper's most relevant finding for fkvs (Section 3.3.2):

> "At this concurrency level, the system becomes CPU- rather than latency-bound"

Their throughput model:

```
throughput = clock_frequency / (c_tx + r_io * c_io)
```

Where:
- `c_tx` = CPU cycles for transaction logic (hashtable ops, frame parsing, serialization)
- `r_io` = I/O rate (fraction of ops requiring I/O)
- `c_io` = CPU cycles for I/O processing

**fkvs is CPU-bound.** Reducing `c_tx` (hashtable mallocs, deep copies in Phase 2 of
the roadmap) will deliver more throughput than reducing `c_io` via io_uring tuning.
The paper saw this with TPC-C in-memory workloads where IOPoll and SQPoll provided
**zero or negative** benefit.

---

## What fkvs Needs to Actually Benefit from io_uring

### Phase A: Architectural Changes (paper's 2x+ gains)

These changes would make io_uring outperform epoll:

1. **Batch SQE submissions**: Accumulate multiple socket operations (reads from
   multiple clients, writes to multiple clients) and submit with a single
   `io_uring_enter()` call
2. **Use `IORING_OP_RECV` / `IORING_OP_SEND`** instead of regular `recv()`/`send()`
   through the ring -- enables io_uring's native async paths
3. **Multishot receive** for small messages (paper shows it's optimal below ~1KB,
   matching fkvs's small binary frames)
4. **DeferTR + single-issuer** flags to eliminate IPIs and give the application
   control over completion reaping
5. **RECVSEND_POLL_FIRST** flag to skip speculative non-blocking attempts on sockets
   where we know the state

### Phase B: Zero-Copy (paper's additional 2x memory bandwidth reduction)

1. **Registered buffers** for client read/write buffers (already 64KB aligned)
2. **Zero-copy send** for response batches (paper shows beneficial above 1KB;
   fkvs flushes up to 64KB at once)
3. **Zero-copy receive** (requires kernel 6.17+ and NIC support for header splitting)

### Phase C: Polling (paper's additional 20-30%)

1. **NAPI busy polling** for network sockets to reduce latency
2. **SQPoll** thread if dedicating a CPU core is acceptable
3. **IOPoll** for storage operations (if fkvs adds persistence)

---

## Important Thresholds from the Paper

### Zero-copy send threshold: ~1KB

Below 1KB, zero-copy send **performs worse** than plain io_uring due to buffer
management overhead. Above 1KB, registered buffers amortize this cost and achieve
up to 3.5x fewer cycles per byte.

**fkvs implication:** Our `wbuf_flush()` sends up to 64KB at once. Zero-copy send
would be highly beneficial.

### Zero-copy receive threshold: ~1KB

Below 1KB, multishot receive is more efficient. Above 1KB, zero-copy receive wins.
Above ~13KB, even normal single-shot receive outperforms multishot.

**fkvs implication:** Individual fkvs frames are small (5-byte minimum), but with
pipelining, recv() reads large chunks. Standard recv or zero-copy recv is the right
choice, not multishot.

### Batch size and latency tradeoff

| Batch Size | Avg Latency | Std Dev |
|---|---|---|
| 1 | 11.51 us | 0.95 us |
| 8 | 24.22 us | 1.71 us |
| 32 | 60.62 us | 3.91 us |
| 128 | 200.85 us | 7.47 us |

Larger batches reduce submission overhead but cause higher variance. For
latency-sensitive workloads, batch sizes of 8-32 are optimal.

### Worker thread fallback

io_uring's `io_worker` threads add ~7.3us overhead per operation. Frequent fallback
indicates suboptimal I/O patterns. Avoid by ensuring all operations execute on the
inline or poll-set paths.

---

## io_uring Configuration Recommendations for fkvs

When implementing proper io_uring support, use these flags:

```c
// Ring setup
struct io_uring_params params = {
    .flags = IORING_SETUP_DEFER_TASKRUN    // Eliminate IPIs, control completion reaping
           | IORING_SETUP_SINGLE_ISSUER    // Single-threaded optimization
           | IORING_SETUP_COOP_TASKRUN     // Fallback if DeferTR unavailable
};

// Per-operation flags
// Use RECVSEND_POLL_FIRST on recv() when socket is expected to be empty
// Use RECVSEND_POLL_FIRST on send() when socket buffer is expected to be full

// Register buffers at startup
io_uring_register_buffers(ring, client_buffers, num_clients);
```

---

## Priority Assessment

Based on the paper's findings and our benchmark data:

1. **Phase 2 of the roadmap (fix hashtable)** will deliver more throughput than any
   io_uring optimization because fkvs is CPU-bound (`c_tx` dominates)
2. **Phase A (io_uring architectural changes)** should come after the hashtable fix,
   when I/O processing becomes a larger fraction of total CPU time
3. **Phase B (zero-copy)** is high-value for UDS workloads where memory bandwidth
   matters
4. **Phase C (polling)** only matters once I/O becomes the bottleneck again

**One positive note:** io_uring showed **lower variance** (0.7% vs 2.1%) in our
benchmarks, suggesting more predictable latency under load. This could matter for
latency-sensitive deployments even before throughput parity is achieved.

---

## Benchmark Data Reference

All benchmark data collected on 2026-02-25 during the pipelining PR benchmarks.

### Docker/Linux (epoll vs io_uring, UDS, 8 clients, 10M SET ops)

| Pipeline | epoll (req/s) | io_uring (req/s) | Delta |
|---|---|---|---|
| P=1 | 344,847 | 312,188 | epoll +10.5% |
| P=16 | 2,005,112 | 1,883,996 | epoll +6.4% |
| P=64 | 3,051,123 | 2,789,166 | epoll +9.4% |
| P=128 | 3,490,216 | 3,218,621 | epoll +8.4% |
| P=256 | 3,496,211 | 3,415,908 | epoll +2.3% |
| P=512 | 3,099,907 | 2,949,844 | epoll +5.1% |

### Hardware

- **Docker**: ARM64 via OrbStack, Linux 6.17.8, 16GB RAM
- **Bare metal**: Apple M1 Max (10 cores), macOS Darwin 25.2.0, 32GB RAM, kqueue
