#pragma once
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// Implemented by Grok, I just needed random keys for benchmarking
static atomic_uint_fast64_t fkvs_counter = 0;
static atomic_bool fkvs_inited = false;
static uint64_t fkvs_prefix = 0;

static uint64_t fkvs_splitmix64(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

static uint64_t fkvs_now_ns_coarse(void)
{
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
#if defined(CLOCK_REALTIME)
    {
        struct timespec ts;
        if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
            return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    }
#endif
    // Fallback for very old libcs
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000000ULL + (uint64_t)tv.tv_usec * 1000ULL;
}

static void fkvs_keygen_init_once(void)
{
    if (atomic_exchange(&fkvs_inited, true))
        return;
    uint64_t seed = fkvs_now_ns_coarse();
    seed ^= ((uint64_t)getpid() << 32);
    seed ^= (uint64_t)(uintptr_t)&fkvs_counter;
    fkvs_prefix = fkvs_splitmix64(seed);
    atomic_store(&fkvs_counter, fkvs_splitmix64(seed ^ 0xA55A5AA5ULL));
}

static void fkvs_bytes_to_hex32(const uint8_t in[16], char out[33])
{
    static const char hexd[16] = "0123456789abcdef";
    for (int i = 0; i < 16; ++i) {
        out[i * 2 + 0] = hexd[in[i] >> 4];
        out[i * 2 + 1] = hexd[in[i] & 0xF];
    }
    out[32] = '\0';
}

// Writes a 32-char lowercase hex string (plus '\0') to `out`.
// Returns 32 on success.
static inline size_t generate_unique_key(char out[33])
{
    fkvs_keygen_init_once();
    const uint64_t ctr =
        atomic_fetch_add_explicit(&fkvs_counter, 1, memory_order_relaxed);

    uint8_t b[16];
    // Compose 128 bits: [prefix:64][counter:64], big-endian for nice lexical
    // order
    for (int i = 0; i < 8; ++i) {
        b[i] = (uint8_t)((fkvs_prefix >> (56 - 8 * i)) & 0xFF);
        b[8 + i] = (uint8_t)((ctr >> (56 - 8 * i)) & 0xFF);
    }
    fkvs_bytes_to_hex32(b, out);
    return 32;
}
