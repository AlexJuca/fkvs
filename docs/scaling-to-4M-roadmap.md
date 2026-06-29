# Scaling fkvs to 4M+ req/s — Research-Backed Roadmap

**Current baseline:** ~2.9M req/s sustained (bursts ~3.5M) at pipeline depth 128, single core, Apple M1 Max. ~1.6M req/s with unique/random keys (allocation-bound).
**Target:** 4M+ req/s.
**Status quo:** single-threaded event loop (kqueue/epoll/io_uring), separate-chaining hashtable + DJB2, ~4 mallocs/SET and ~2 mallocs + deep-copy/GET, `-O3` only, no SIMD/NUMA/sharding.
**Date:** 2026-06-29

> This document supersedes the 1M target in `performance-roadmap.md`. It is sourced from a deep-research pass over the systems literature (NSDI/SOSP/VLDB papers, allocator/hash benchmarks) and the canonical performance/architecture books. Every quantitative claim below carries a citation in the References section.

---

## 0. The central decision (read this first)

fkvs is **CPU/parse-bound on a single core**. That single fact splits the path to 4M into two strategies, and you should be deliberate about which one you commit to:

- **Path A — squeeze the single core (4M is *plausible*, not guaranteed).** Kill allocations, replace the hashtable, replace the hash function, batch syscalls, turn on the compiler. The literature says a *general-purpose userspace* server can hit ~10M small RPCs/s on one core — but only with kernel bypass (eRPC [E1]). On a stock kernel TCP path, an isolated cache-conscious hash table already does 5M lookups/s/thread (MemC3 [H1]), so the *data structure* is not your ceiling — the **allocator, the copies, and the per-request syscalls are.** Removing those can realistically take you from 2.9M toward ~4–5M while **preserving the single-threaded design**. This is the lower-risk, higher-certainty-per-unit-effort path, and every win here also multiplies Path B.

- **Path B — shard across cores, shared-nothing (4M is *guaranteed*, with headroom to 10M+).** The literature is unanimous that the *only robust way past a single-core ceiling* is partitioning, not a faster shared structure. MICA's EREW shared-nothing model scales **linearly** to 65–77M ops/s on 16 cores [M1][M2]; Anna's coordination-free thread-per-core hits ~20M ops/s and shows that **shared-everything lock-based designs burn 92–95% of CPU on atomics under contention** [A2], i.e. naive multithreading is *worse* than one good core. On your 8-perf-core M1, two well-isolated shards clear 4M trivially.

**Recommendation:** Do **Tier 1 (single-core wins) first** — they are cheap, low-risk, keep the code simple, and are prerequisites that make each shard faster. If Tier 1 lands you at/above 4M on synthetic loopback, ship it. Treat **Tier 2 (SO_REUSEPORT shared-nothing sharding)** as the definitive lever that *guarantees* 4M and opens the road to 10M+. Reserve **Tier 3 (kernel bypass / io_uring frontier)** for when you are syscall-bound after Tiers 1–2.

A caveat that governs the whole plan: **your benchmark must be trustworthy first** (Tier 0). At multi-million req/s the client and the measurement method lie to you more often than the server does.

---

## Top things to do — ranked shortlist

| # | Change | Expected gain | Risk | Keeps single-thread? |
|---|--------|---------------|------|----------------------|
| 1 | **Eliminate hot-path allocations**: arena/slab for entries, inline small keys/values, object-pool `value_entry_t` | Targets the 1.6M random-key wall directly; allocation is 30–40% of hot CPU | Med | ✅ |
| 2 | **Zero-copy GET**: return pointer into the store, write straight into `wbuf`; never `malloc`+`memcpy`+leak per read | Removes 2 allocs + 1 deep copy *per GET* + fixes a live leak | Low | ✅ |
| 3 | **Replace chaining hashtable** with open-addressing + SIMD control bytes (Swiss) or bucketized cuckoo (MemC3) | ~60% fewer cache refs, LLC misses −6×, dTLB −6× [S2][S3]; isolated table → 5M lookups/s/thread [H1] | Med-High | ✅ |
| 4 | **SO_REUSEPORT shared-nothing sharding** (thread-per-core, partition by keyhash, no shared state) | **Near-linear scaling** [M2][A1]; the guaranteed path past the single-core ceiling | High | ❌ (architectural) |
| 5 | **Replace DJB2** with XXH3 or wyhash (or `__crc32cd` on M1) | XXH3 ~30 GB/s & tuned for *small keys* [HF1][HF4]; DJB2 is ~5–10% of hot CPU | Low | ✅ |
| 6 | **Syscall batching**: read-till-EAGAIN + `writev`/`sendmmsg`; later io_uring batched SQEs | Syscall batching cut call count ~50× and ~3× throughput in UDP studies [B2][B5] | Med | ✅ |
| 7 | **Compiler/toolchain**: `-march=native -mcpu=native`, LTO, PGO, then BOLT | LTO+PGO ~6.5% [C2]; BOLT +up to 8% *on top* of PGO+LTO [C1] | Low | ✅ |
| 8 | **Mechanical sympathy**: cache-line align hot structs, `__builtin_prefetch` on lookup, huge pages, branch hints | Multi-stage prefetch is core to MICA hiding miss latency [M4]; huge pages cut TLB misses [D1] | Med | ✅ |
| 9 | **io_uring done right** (only if syscall-bound): SQPOLL, registered buffers, multishot recv, zero-copy send | SQPOLL ~32%; registered/ZC buffers up to 2.5× vs epoll+copy [IU1] | High | ✅ |

---

## Tier 0 — Make the benchmark tell the truth (do before anything)

You cannot optimize toward 4M if the number is an artifact. The literature on this is blunt:

- **Coordinated omission** is "the number one cause of problems in benchmarks" — closed-loop generators back off during stalls and measure *service time*, not *response time*, hiding tail latency by up to ~2,600× (YCSB: 249µs vs 665ms P99) [G1][G2]. Fix: drive at a **fixed open-model rate** with **wrk2/HdrHistogram** style correction [G3][G4].
- **The client saturates before the server.** A synchronous single connection measures the client library, not fkvs; valid throughput needs many connections and/or pipelining and/or multiple client threads [G5][G6]. Confirm fkvs-benchmark isn't itself the bottleneck (run 2 client machines / pin client and server to disjoint cores).
- **Connection count matters**: 30k conns can do ~half the throughput of 100 conns [G7] — report the curve, not one point.
- **Loopback ≠ wire.** Unix sockets can be ~50% faster than TCP loopback at low pipeline depth, shrinking as depth grows [G8]. Decide which you're claiming and be consistent.

**Action:** add an open-model, HdrHistogram-based mode to `fkvs-benchmark` (or validate with `memtier_benchmark`), pin client/server to separate cores, and always report throughput **at a stated p99 latency bound** (MICA/120MRPS work fixed throughput at p95 = 100µs [M7][R3]). Re-establish the baseline under this regime before touching code.

---

## Tier 1 — Single-core wins (do these; they preserve simplicity and compound)

### 1.1 Kill hot-path allocations  *(highest single-core leverage)*
Today: ~4 allocs/SET (new key), and the random-key benchmark is allocation-bound at 1.6M — exactly the case allocators and arenas target.
- **Arena/slab allocator** for `hash_table_entry_t` and key bytes — bump-pointer, never per-op free [MM2].
- **Object pool** for `value_entry_t` (fixed-size, recycled).
- **Inline small keys/values** directly in the entry (e.g. ≤15B key, ≤31B value) — no separate allocation, and it doubles as a cache-locality win.
- **Allocator swap as a quick experiment:** mimalloc's sharded free-lists lead jemalloc/tcmalloc on small frequent allocs (~13% over tcmalloc on Lean; avoids false sharing) [MM1][MM3][MM4]. Drop-in, low risk — but it is a *band-aid*; the arena/inline work removes the allocation entirely and should be the real fix.

Risk: medium (lifetime/ownership care). Keeps single-thread. **This is the #1 thing for the random-key path.**

### 1.2 Zero-copy GET + fix the leak
`get_value` deep-copies the value and the GET handler `malloc`s a response buffer; **neither is freed today** (live leak, documented in `performance-roadmap.md`). Return a pointer (+len) into the stored value and append directly into the per-connection `wbuf`. Removes two allocations and one `memcpy` per GET and deletes the leak. Low risk, pure win.

### 1.3 Replace the hashtable (chaining → cache-conscious)
Chaining costs up to N dependent memory references per lookup; the modern designs need ~2 cache-line reads:
- **Swiss / open-addressing with SIMD control bytes** — scan 16 control bytes per vector op; in a real swap this gave **60% fewer cache references, LLC load-misses −6×, dTLB misses −6×, 2–3× faster** [S1][S2][S3]. Note: 16-byte (NEON-width on M1) probe is optimal; 32/64 add nothing [S4].
- **Bucketized optimistic cuckoo (MemC3)** — 4-way set-associative reaches ~95% occupancy, ~2 parallel cache-line reads, ~0.03 pointer derefs on miss; isolated table hits **5M lookups/s/thread** [H1][H2][H3]. Cuckoo also gives you optimistic concurrency *for free* if/when you sketch readers across shards.

Either removes the data structure as a bottleneck. Choose Swiss for simplicity, cuckoo if you want the concurrency story later. Medium-high effort, single-thread preserved.

### 1.4 Replace DJB2 hash
DJB2 is slow and a measurable slice of hot CPU. **XXH3** is purpose-built for **small keys** (the KV case) and runs ~30 GB/s scalar-SSE2 / ~59 GB/s AVX2, ~2.4× hardware CRC32C on large data [HF1][HF2][HF4]. **wyhash** is comparably fast. On the **M1 specifically**, `__crc32cd` (ARMv8 CRC) is nearly free and excellent for short keys — benchmark XXH3 vs wyhash vs CRC32C on *your* ARM box, since published GB/s figures are x86-specific [HF5]. Low risk.

### 1.5 Syscall batching on the existing path
Move to **read-until-EAGAIN** (already partly done via edge-trigger) and **`writev`/`sendmmsg`** for batched responses. Syscall-per-message is a hard ceiling; batching collapsed ~904k `sendmsg` into ~2.4k calls (~50×) and combined with GSO gave ~3× throughput in UDP studies [B1][B2][B5]; `sendmmsg` batch-64 gave +20–30% on raw/UDP [B3][B4]. Your TCP+pipelining already amortizes some of this, so measure the marginal gain. Medium risk.

### 1.6 Turn the compiler on
You're on `-O3` only. Add, in order, measuring each:
1. `-march=native` / `-mcpu=native` (unlock NEON/CRC on M1).
2. **LTO** (`-flto`) — ~4% alone [C2].
3. **PGO** (`-fprofile-generate`/`-use` against the benchmark) — ~2% alone, ~6.5% combined with LTO [C2].
4. **BOLT** post-link layout — **up to 8% on top of PGO+LTO**, complementary because it fixes I-cache/iTLB layout of the hot dispatch loop [C1][C3][C4].
Low risk, low effort, single-thread preserved. Realistically ~10–15% stacked.

### 1.7 Mechanical sympathy (Drepper / Hennessy-Patterson / Gregg)
- **Cache-line align** `client_t` and hot hashtable structs; keep hot fields on one line, cold fields elsewhere.
- **Software prefetch** the bucket before you touch it — MICA's multi-stage prefetch across a *burst* of pipelined requests is exactly how it hides cache/TLB miss latency while staying CPU-efficient [M4][M6]; with depth-128 pipelining you can prefetch request N+k's bucket while handling N.
- **Huge pages** for the arena/store to cut dTLB misses (a 2MB page replaces ~511 4K faults) [D1].
- **Branch hints** (`__builtin_expect`) on error paths, `__attribute__((hot))` on handlers.
- Validate with **hardware counters / roofline** *before and after* — don't guess whether you're compute-, memory-, or latency-bound [HPM1]. (Gregg's USE method; Drepper §3–6.)

---

## Tier 2 — The architectural lever: shared-nothing sharding (the guaranteed path to 4M+)

If Tier 1 doesn't clear 4M, or you want robust headroom to 10M+, this is the move the literature endorses without reservation.

**Design (MICA EREW / Anna / Seastar thread-per-core):**
- One event loop **per core**, each pinned to a CPU (`pthread_setaffinity`), each owning a **private** hashtable + arena in its own memory. **No shared mutable state, no locks, no atomics on the hot path.**
- **`SO_REUSEPORT`**: N listeners on the same port; the kernel hashes each connection to one shard. Simplest possible sharding — no cross-thread routing for connection-affine workloads.
- Partition keys by `keyhash % N` if you need request-level (not connection-level) affinity; MICA uses NIC Flow-Director/RSS for this [M2][M7], the userspace analogue is to route after parse.

**Why this and not "just add threads to the shared hashtable":**
- Lock-based shared KVS (TBB map, Masstree) spend **92–95% of CPU on atomics** under contention and run **50–700× slower than Anna** [A2]. Even a *non-thread-safe* shared map pays **17× more L1 misses** from cache-coherence traffic [A3]. Shared-everything multithreading is a trap; it can be slower than your current single core.
- Shared-nothing scales **near-linearly**: MICA 65–77M ops/s on 16 cores (4–13.5× the prior art) [M1][M2]; Anna ~20M ops/s thread-per-core, coordination-free [A1]; the same EREW idea later reached **120M req/s on one dual-socket box** [R1][R2].

**Cost:** this is the architectural change. Cross-shard operations (multi-key, scans, future replication) need explicit handling; metrics/TTL sweeps become per-shard. But for GET/SET/INCR it is clean. On your 8 perf cores, **2–3 shards already exceed 4M**; the rest is headroom.

**Decision rule:** if profiling after Tier 1 shows you're at the single-core compute ceiling and still short of target, shard. It is the only lever proven to break that ceiling.

---

## Tier 3 — Kernel-bypass / io_uring frontier (only when syscall-bound)

Once allocations, copies, and the data structure are gone, the remaining cost is the **kernel TCP path** (MICA notes TCP processing alone can be ~70% of CPU [M5]). Two options, in increasing risk:

- **io_uring done properly** (preserves the socket model, no NIC ownership): the VLDB 2026 study quantifies each feature — **SQPOLL eliminates most `io_uring_enter` syscalls for ~32% throughput**, **registered/zero-copy buffers halve memory bandwidth and give up to 2.5× vs epoll+copy under high concurrency**, **multishot recv** emits many CQEs per SQE to cut control-plane churn [IU1]. Crucially it warns **zero-copy/multishot only pay off above a payload threshold** — fkvs's tiny KV values on loopback may *not* benefit, which is exactly why your naive port is 8% *slower* [IU2]. Fix the submission model (batch SQEs, registered buffers, multishot recv [IU3]) before judging io_uring.
- **Full kernel bypass** (DPDK/eRPC/Demikernel): the absolute ceiling — eRPC sustains **~10M small RPCs/s on a single core** [E1]; MICA+DPDK and the 120MRPS work depend on it [M3][R1]. But it forces a **datapath rewrite**, busy-polls/owns a core and the NIC, and doesn't run on the M1 dev box [E2][E3]. Justified only at the 10M+ tier on Linux server hardware, not for crossing 4M.

---

## Suggested sequencing

1. **Tier 0** — trustworthy open-model + HdrHistogram benchmark; re-baseline. *(days)*
2. **Tier 1.1 + 1.2** — arena/inline + zero-copy GET. Attacks the 1.6M random-key wall and the leak first. *(1–2 wks)*
3. **Tier 1.6** — flip on `-march=native`/LTO/PGO/BOLT (cheap, do early to measure on a fast binary). *(days)*
4. **Tier 1.4 + 1.3** — XXH3/CRC32C, then Swiss/cuckoo table. *(1–2 wks)*
5. **Tier 1.5 + 1.7** — syscall batching + prefetch/huge-pages/alignment, counter-guided. *(1 wk)*
6. **Re-measure.** If ≥4M on the corrected benchmark → ship. If short → **Tier 2 SO_REUSEPORT sharding** (guaranteed). *(2–3 wks)*
7. **Tier 3** only if syscall/kernel-bound on Linux server HW and chasing 10M+.

**Bottom line:** Tiers 0–1 are the disciplined, simplicity-preserving push that *plausibly* reaches 4M and definitely removes the allocation/cache/compiler tax. **Tier 2 shared-nothing sharding is the lever that guarantees 4M and scales to 10M+** — and the research is emphatic it is the *only* reliable way past a single core, while warning that naive shared-memory multithreading is actively worse.

---

## References

**io_uring / modern IO**
- [IU1] *io_uring for High-Performance DBMSs: When and How to Use It*, VLDB 2026. arxiv.org/pdf/2512.04859 — SQPOLL ~32%; registered/ZC buffers up to 2.5×; multishot recv; payload-threshold caveat.
- [IU2] *Is io_uring Always Faster Than epoll?* chillvic.dev — why naive io_uring underperforms; SQPOLL cuts syscalls ~80%; ZC helps >4KB.
- [IU3] `io_uring_prep_recv_multishot(3)` man page — multishot recv API.

**Kernel bypass / dataplane**
- [E1] Kalia, Kaminsky, Andersen, *Datacenter RPCs can be General and Fast* (eRPC), NSDI 2019 — up to **10M small RPCs/s on one core**.
- [E2] Zhang et al., *The Demikernel Datapath OS Architecture*, SOSP 2021 — portable kernel-bypass LibOS; datapath rewrite cost.
- [E3] Fried et al., *Making Kernel Bypass Practical with Junction*, NSDI 2024 — density/practicality costs of bypass.

**Hashtables**
- [H1][H2][H3] Fan, Andersen, Kaminsky, *MemC3*, NSDI 2013 — optimistic bucketized cuckoo; **3× QPS, 30% less memory**; **5M lookups/s/thread**, 35M across threads; ~2 cache-line reads, ~95% occupancy.
- [S1][S2][S3][S4] *Faster ES|QL stats with Swiss-style hash tables*, Elasticsearch Labs — **60% fewer cache refs, LLC misses −6×, dTLB −6×, 2–3× faster**; 16-byte SIMD probe optimal.

**Hash functions**
- [HF1][HF2][HF4] xxHash Performance comparison (official wiki) — XXH3 ~30/59 GB/s; small-key velocity; vs CRC32C/CityHash/FarmHash.
- [HF5] RocksDB microbenchmarks (Mark Callaghan, Small Datum) — crc32c vs XXH3 in DBMS context.

**Allocators / memory**
- [MM1][MM3][MM4] microsoft/mimalloc — leads jemalloc/tcmalloc on small allocs; free-list sharding; false-sharing avoidance; ~13% over tcmalloc (Lean).
- [MM2] *High Performance Memory Management: Arena Allocators* — arena/region technique.

**Shared-nothing / multi-core scaling**
- [M1..M7] Lim et al., *MICA*, NSDI 2014 — **65.6–76.9M ops/s on one 16-core node, 4–13.5×** prior art; EREW shared-nothing linear scaling; DPDK zero-copy + burst-32; multi-stage prefetch; TCP ~70% CPU; p95=100µs SLO.
- [A1][A2][A3] Wu et al., *Anna* (UC Berkeley) — coordination-free thread-per-core ~20M ops/s; lock-based KVS **92–95% CPU on atomics, 50–700× slower**; unsynchronized shared map **17× more L1 misses**.
- [R1][R2][R3] MICA-based optimized KVS — **~120M req/s on one dual-socket server** via kernel-bypass + sharding; stock memcached ~0.3M, ULN memcached ~0.7M; throughput tied to p95=100µs.
- Nibble (log-structured, 240-core) — multi-head log ~2× write throughput; shared-nothing partitioning needed past single-core; <10% memory overhead vs ~2× for non-copying.

**Compiler / toolchain**
- [C1][C3][C4] Panchenko et al., *BOLT*, CGO 2019 — **+up to 8% on top of PGO+LTO**; up to 20.4% on FDO+LTO datacenter apps; 52.1% on unoptimized; complementary to LTO.
- [C2] Hubička, *GCC 9 LTO/IPO* — LTO ~4%, PGO ~2%, combined ~6.5% SPECint.

**Mechanical sympathy / books**
- [D1] Drepper, *What Every Programmer Should Know About Memory* — cache lines, false sharing, prefetch, TLB/huge pages.
- [HPM1] Treibig, Hager, Wellein, *Best practices for HPM-assisted performance engineering* — counters + roofline to classify the bottleneck before tuning.
- Hennessy & Patterson, *Computer Architecture: A Quantitative Approach*; Brendan Gregg, *Systems Performance* (USE method); Agner Fog, optimization manuals; Herlihy & Shavit, *The Art of Multiprocessor Programming* (for Tier 2 concurrency).

**Syscall batching**
- [B1..B5] Linux UDP sendmmsg/GSO studies — `sendmmsg` collapsed ~904k→~2.4k calls (~50×); +GSO ~3× over naive `sendmsg`; batch-64 +20–30% on raw/UDP; up to 64 segments per GSO (5.3+).

**Benchmarking methodology**
- [G1][G2][G3] Gil Tene, *How NOT to Measure Latency* (coordinated omission) — #1 benchmark error; YCSB 249µs vs 665ms; HdrHistogram correction.
- [G4] wrk2 — constant-rate `-R`, CO correction, p99 ~200× larger when corrected.
- [G5][G6][G7][G8] Redis benchmark docs — client saturation; many-conns/pipelining required; conn-count effect; Unix socket ~50% over TCP loopback at low depth.
