# Hot-path allocation & zero-copy decisions

Branch `perf/hot-path-zero-copy`. Tackles items #1 (eliminate hot-path allocations)
and #2 (zero-copy reads) from `scaling-to-4M-roadmap.md`. Decisions kept short for review.

## #1 — Eliminate hot-path allocations

**D1. Store value bytes inline in the `value_entry_t` allocation.**
One `malloc(sizeof(value_entry_t) + value_len + 1)` instead of two (`calloc` entry +
`malloc` bytes). `ptr` points just past the header into the same block, so every
existing `value->ptr` use is unchanged and `free_value_entry` becomes a single
`free`. *Was 2 allocs/value → 1.*

**D2. Store the key inline in the `hash_table_entry_t` allocation.**
One `malloc(sizeof(node) + key_len)` instead of `malloc(node)` + `malloc(key)`.
`node->key` points just past the header. Key never changes for an entry, so this is
safe; node address stays stable, preserving the resize invariant ("migration relinks
nodes, never copies/frees them"). Also puts the key next to the metadata used by
`find_entry`'s `memcmp`, improving locality. *Was 2 allocs/new key → 1.*

**Net SET cost:** new key 4 allocs → **2**; different-length overwrite 2 → **1**;
same-length overwrite **0** (unchanged fast path). Delete frees 4 → **2**.

**D3. Deferred (not in this branch):** object-pool/freelist for nodes, full
open-addressing/Swiss table, xxHash. These are roadmap items #3/#5 and are larger,
separately-reviewable changes. Inlining is the self-contained, correctness-preserving
first cut that directly attacks the allocation-bound random-key case.

## #2 — Zero-copy reads

**D4. Add a borrowing lookup `lookup_value()` that returns a `const value_entry_t *`
into the live table — no allocation, no copy.** Contract: caller must not free it and
must treat it as invalidated by any later `set_value`/`delete_value`/expiry on that
key. The owning `get_value()` is kept only where a snapshot must survive a mutation.

**D5. GET writes the stored bytes straight into the write buffer.**
Old GET path did *three* copies + two allocs (deep-copy in `get_value`, a redundant
`resp_buffer` malloc+copy, then the `wbuf` copy) and **leaked both allocations**.
New path: `lookup_value` → `send_reply(value->ptr, value->value_len)`. The only copy
left is the unavoidable framing copy into `wbuf`. *GET: 3 allocs + leak → 0 allocs.*

**D6. Migrate the other read paths to the borrow API:** INCR/DECR/INCR_BY/DECR_BY
(read the integer before the rewrite `set_value`, then drop the borrow), EXPIRE/TTL
existence checks, and ttl.c `get_deadline`. Removes their per-call alloc+copy+free.

**D7. Keep owning `get_value()` for SET's TTL-rollback snapshot only.** It reads the
old value *after* `set_value` may have freed the live entry, so it genuinely needs an
owned copy. This path is off the hot SET path (only when `EX` is supplied).

## Measured impact

**Environment:** Linux/epoll via OrbStack on Apple Silicon (Ubuntu, Linux 6.19,
aarch64, 10 vCPU), system allocator, loopback TCP, 5-byte values. Best-of-3.
`before` = `20b9cc4` (branch base), `after` = `70f2423` (this branch's perf
commits). Command: `fkvs-benchmark -t set [-r] -c <C> -P <P> -n <N>`.

> Linux/epoll is the representative target. macOS/kqueue is *not* a useful
> measurement here: the benchmark issues one `send()` per command and macOS
> syscalls are ~5–6× costlier, so it pins ~0.46–0.59M regardless of server speed
> (PING ≈ SET confirms it's harness-bound, not server-bound).

| Workload | `-c` | `-P` | before | after | Δ |
|---|---|---|---|---|---|
| PING (control, no hashtable) | 128 | 128 | 3,025,635 | 3,234,180 | +6.9% |
| SET fixed-key (control, in-place, 0 allocs) | 128 | 128 | 3,171,660 | 3,216,862 | +1.4% |
| SET random-key | 50 | 1 | 229,738 | 242,309 | +5.5% |
| SET random-key | 50 | 32 | 1,308,548 | 1,305,551 | ~0% |
| **SET random-key** | **50** | **128** | **1,388,398** | **1,744,866** | **+25.7%** |

**Reading it:** the gain lands on the **allocation-bound new-key path** (random-key
SET at `-P 128`: **+26%**), exactly what D1/D2 target (4 allocs → 2 per insert).
The controls behave correctly — fixed-key (in-place fast path, 0 allocs both
versions) is flat, and PING (no hashtable) moves only with run-to-run variance.
The PING drift sets the **noise floor at ~±7%** on this VM, so the real win on the
changed path is ~+18–26%, and it appears *only* on the workload it should.

**Not shown:** the zero-copy GET win (D5: 3 allocs + a leak → 0) — `fkvs-benchmark`
has no `get` workload, so it isn't in this table; it is covered by the
sanitizer-clean hashtable tests instead.

**Caveat:** the open-model `-R` latency sweep on a *single* co-located VM is
rig-limited (~1 ms p99 floor from client/server core contention, ~200K depth-1
generation ceiling), so absolute latency knees there reflect the test rig, not the
server. A clean latency-at-rate measurement needs the client on a separate host
over a low-latency wired/Thunderbolt link (see [remote-benchmarking.md](remote-benchmarking.md)).
