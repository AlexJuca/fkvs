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
