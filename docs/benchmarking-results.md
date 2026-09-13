## Benchmark Results

These figures measure **server-side CPU efficiency** — `fkvs-benchmark` talks to
the server over loopback TCP on a single machine, so they show how many
operations one fkvs event-loop core can push, not end-to-end latency over a real
network.

**Environment**

| | |
|---|---|
| Host | Apple M1 Max — 8 performance + 2 efficiency cores, macOS 26.5 |
| Linux | Ubuntu 24.04 LTS in an OrbStack VM — 10 vCPU, 16 GiB, Linux 6.19, `aarch64` |
| Build | `-DCMAKE_BUILD_TYPE=Release`, branch `feat/jemalloc-support` (with SET hot-path optimizations) |
| Allocators | jemalloc 5.3.0 vs system (glibc) malloc |
| Workload | `fkvs-benchmark`, loopback TCP, `SET` (5-byte value) |
| Method | best-of-2 per cell (best-of-3 for `-r`); `-n 1,000,000` (`-n 300,000` at `-P 1`) |

> The Linux numbers come from a **virtualized VM on Apple Silicon**, not
> bare-metal Linux, and loopback TCP removes the network — treat them as
> **relative**, not absolute. Run-to-run variance is ~10–20% on this shared VM.

### Throughput vs pipeline depth — `SET` reusing one key (update-in-place)

`fkvs-benchmark -n 1000000 -t set -c 128 -P <P>` — req/s, higher is better:

| Pipeline (`-P`) | epoll · jemalloc | epoll · system | io_uring · jemalloc | io_uring · system |
|---|---|---|---|---|
| 1   | 197K  | 230K  | 231K  | 242K  |
| 8   | 1.01M | 985K  | 984K  | 966K  |
| 32  | 1.78M | 1.96M | 1.87M | 1.84M |
| 128 | 2.88M | 2.94M | 2.88M | 2.92M |

At `-P 128` the single event-loop core sustains **~2.9M req/s**; with more
connections and on quieter runs it peaks around **3.1–3.5M**. Connection count
(`-c`) otherwise had only marginal effect (50–256 within ~10%): the
single-threaded loop is already saturated by a handful of pipelined clients.

### Throughput with unique keys — `SET -r` (allocation-heavy, every op inserts)

`fkvs-benchmark -n 1000000 -t set -r -c 50 -P 128`, fresh server per run — req/s:

| Backend | jemalloc | system |
|---|---|---|
| epoll | 1.64M | 1.69M |
| io_uring | 1.60M | 1.59M |

### Reading the numbers

- **Pipelining is the dominant lever** — ~15× from `-P 1` to `-P 128`. With deep
  pipelining the workload is CPU/parse-bound on a single core (~2.9M ops/s, peaks
  ~3.5M).
- **epoll ≈ io_uring** — within noise across the board; io_uring only pulls
  marginally ahead at `-P 1`, its syscall-bound sweet spot.
- **jemalloc ≈ system** on the optimized hot path. Earlier builds allocated
  several small objects per `SET`, which gave jemalloc an edge; once the hot path
  was trimmed to near-zero per-op allocation, that edge largely disappeared.
  jemalloc still does no harm and remains the default on Linux (it can help
  long-running, fragmentation-prone workloads) — see the
  [jemalloc Build Guide](docs/jemalloc.md).
- **`-r` is lower** (~1.6M) because every op inserts a new key — real
  allocation plus cache-cold access across a large keyspace — versus the
  cache-hot single-key update path above.
  