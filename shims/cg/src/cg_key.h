// Lookup key for a Cg program: FNV-1a 64 over the source text, the profile and
// the entry point.
#pragma once

#include <stdint.h>

static inline uint64_t cg_fnv1a(uint64_t h, const void *data, unsigned long n) {
    const unsigned char *p = (const unsigned char *)data;
    while (n--) h = (h ^ *p++) * 0x100000001b3ULL;
    return h;
}

static inline uint64_t cg_program_key(const char *source, int profile, const char *entry) {
    uint64_t h = 0xcbf29ce484222325ULL;
    unsigned long n = 0;
    if (source) while (source[n]) n++;
    h = cg_fnv1a(h, source, n);
    h = cg_fnv1a(h, &profile, sizeof profile);   // little-endian int32 on both targets
    n = 0;
    const char *e = entry ? entry : "main";
    while (e[n]) n++;
    return cg_fnv1a(h, e, n);
}
