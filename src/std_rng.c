/*
 * AiScript Math:gen_rng — seedrandom ARC4 + ChaCha20-legacy, matching aiscript-rs.
 */

#include "std_rng.h"
#include "error.h"
#include "memory.h"
#include "types.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#define RNG_WIDTH 256
#define FRACTION_BITS 52
#define FRACTION_MASK ((1ULL << FRACTION_BITS) - 1ULL)
#define SAFE_INTEGER_BITS 53
#define MAX_SAFE_INTEGER ((1LL << SAFE_INTEGER_BITS) - 1)

enum rng_algo {
    RNG_CHACHA20,
    RNG_RC4,
    RNG_RC4_LEGACY
};

typedef struct {
    uint8_t s[RNG_WIDTH];
    uint8_t i;
    uint8_t j;
} arc4_t;

typedef struct {
    uint32_t kn[16];
    uint64_t counter;
    uint8_t buf[64];
    size_t buf_len;
    size_t buf_off;
} chacha_t;

typedef struct {
    enum rng_algo algo;
    arc4_t arc4;
    chacha_t chacha;
    int legacy_ready;
    /* rc4_legacy uses the classic seedrandom double generator */
} rng_t;

/* ---- SHA-384 (SHA-512 truncated) ---- */

static const uint64_t SHA512_K[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

static uint64_t rotr64(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }
static uint64_t load_be64(const uint8_t* p)
{
    return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) | ((uint64_t)p[2] << 40) |
           ((uint64_t)p[3] << 32) | ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
           ((uint64_t)p[6] << 8) | (uint64_t)p[7];
}
static void store_be64(uint8_t* p, uint64_t v)
{
    p[0] = (uint8_t)(v >> 56); p[1] = (uint8_t)(v >> 48); p[2] = (uint8_t)(v >> 40);
    p[3] = (uint8_t)(v >> 32); p[4] = (uint8_t)(v >> 24); p[5] = (uint8_t)(v >> 16);
    p[6] = (uint8_t)(v >> 8);  p[7] = (uint8_t)v;
}

static void sha384(const uint8_t* data, size_t len, uint8_t out[48])
{
    uint64_t h[8] = {
        0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL, 0x9159015a3070dd17ULL, 0x152fecd8f70e5939ULL,
        0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL, 0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL
    };
    uint8_t block[128];
    size_t n = len;
    const uint8_t* p = data;
    uint64_t bitlen_hi = 0;
    uint64_t bitlen_lo = (uint64_t)len * 8ULL;

    while (n >= 128) {
        uint64_t w[80];
        uint64_t a, b, c, d, e, f, g, hh, t1, t2;
        int i;
        for (i = 0; i < 16; i++) {
            w[i] = load_be64(p + i * 8);
        }
        for (i = 16; i < 80; i++) {
            uint64_t s0 = rotr64(w[i - 15], 1) ^ rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7);
            uint64_t s1 = rotr64(w[i - 2], 19) ^ rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        a = h[0]; b = h[1]; c = h[2]; d = h[3];
        e = h[4]; f = h[5]; g = h[6]; hh = h[7];
        for (i = 0; i < 80; i++) {
            uint64_t S1 = rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41);
            uint64_t ch = (e & f) ^ ((~e) & g);
            t1 = hh + S1 + ch + SHA512_K[i] + w[i];
            uint64_t S0 = rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39);
            uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
            t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
        p += 128;
        n -= 128;
    }

    memset(block, 0, sizeof(block));
    memcpy(block, p, n);
    block[n] = 0x80;
    if (n >= 112) {
        /* process this block then a zero block with length */
        uint64_t w[80];
        uint64_t a, b, c, d, e, f, g, hh, t1, t2;
        int i;
        for (i = 0; i < 16; i++) {
            w[i] = load_be64(block + i * 8);
        }
        for (i = 16; i < 80; i++) {
            uint64_t s0 = rotr64(w[i - 15], 1) ^ rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7);
            uint64_t s1 = rotr64(w[i - 2], 19) ^ rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        a = h[0]; b = h[1]; c = h[2]; d = h[3];
        e = h[4]; f = h[5]; g = h[6]; hh = h[7];
        for (i = 0; i < 80; i++) {
            uint64_t S1 = rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41);
            uint64_t ch = (e & f) ^ ((~e) & g);
            t1 = hh + S1 + ch + SHA512_K[i] + w[i];
            uint64_t S0 = rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39);
            uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
            t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
        memset(block, 0, sizeof(block));
    }
    store_be64(block + 112, bitlen_hi);
    store_be64(block + 120, bitlen_lo);
    {
        uint64_t w[80];
        uint64_t a, b, c, d, e, f, g, hh, t1, t2;
        int i;
        for (i = 0; i < 16; i++) {
            w[i] = load_be64(block + i * 8);
        }
        for (i = 16; i < 80; i++) {
            uint64_t s0 = rotr64(w[i - 15], 1) ^ rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7);
            uint64_t s1 = rotr64(w[i - 2], 19) ^ rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        a = h[0]; b = h[1]; c = h[2]; d = h[3];
        e = h[4]; f = h[5]; g = h[6]; hh = h[7];
        for (i = 0; i < 80; i++) {
            uint64_t S1 = rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41);
            uint64_t ch = (e & f) ^ ((~e) & g);
            t1 = hh + S1 + ch + SHA512_K[i] + w[i];
            uint64_t S0 = rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39);
            uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
            t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    for (n = 0; n < 6; n++) {
        store_be64(out + n * 8, h[n]);
    }
}

/* ---- UTF-16 mixkey / ARC4 (seedrandom) ---- */

static uint32_t rotl32(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

static void utf8_next(const unsigned char** p, uint32_t* cp)
{
    unsigned char c = **p;
    if (c < 0x80) {
        *cp = c;
        *p += 1;
    } else if ((c & 0xE0) == 0xC0) {
        *cp = ((c & 0x1F) << 6) | ((*p)[1] & 0x3F);
        *p += 2;
    } else if ((c & 0xF0) == 0xE0) {
        *cp = ((c & 0x0F) << 12) | (((*p)[1] & 0x3F) << 6) | ((*p)[2] & 0x3F);
        *p += 3;
    } else {
        *cp = ((c & 0x07) << 18) | (((*p)[1] & 0x3F) << 12) | (((*p)[2] & 0x3F) << 6) | ((*p)[3] & 0x3F);
        *p += 4;
    }
}

static void mixkey(const char* seed, uint8_t* key, size_t* keylen)
{
    uint32_t smear = 0;
    size_t j = 0;
    const unsigned char* p = (const unsigned char*)(seed ? seed : "");
    *keylen = 0;
    memset(key, 0, 256);
    while (*p) {
        uint32_t cp;
        uint16_t units[2];
        int nu = 1;
        int u;
        utf8_next(&p, &cp);
        if (cp < 0x10000) {
            units[0] = (uint16_t)cp;
        } else {
            units[0] = (uint16_t)(0xD800 + ((cp - 0x10000) >> 10));
            units[1] = (uint16_t)(0xDC00 + ((cp - 0x10000) & 0x3FF));
            nu = 2;
        }
        for (u = 0; u < nu; u++, j++) {
            uint8_t i = (uint8_t)j;
            uint32_t prev = (i < *keylen) ? key[i] : 0;
            uint8_t value;
            smear ^= prev * 19;
            value = (uint8_t)(smear + (uint8_t)units[u]);
            if (j <= 255) {
                key[j] = value;
                *keylen = j + 1;
            } else {
                key[i] = value;
            }
        }
    }
}

static void arc4_g(arc4_t* a, uint32_t count, uint8_t* out)
{
    uint32_t n;
    for (n = 0; n < count; n++) {
        uint8_t i = (uint8_t)(a->i + 1);
        uint8_t t = a->s[i];
        uint8_t j = (uint8_t)(a->j + t);
        a->s[i] = a->s[j];
        a->s[j] = t;
        out[n] = a->s[(uint8_t)(a->s[i] + a->s[j])];
        a->i = i;
        a->j = j;
    }
}

static void arc4_init(arc4_t* a, const uint8_t* key, size_t keylen)
{
    uint8_t tmpkey[1] = {0};
    const uint8_t* k = key;
    size_t klen = keylen;
    int i;
    uint8_t j = 0;
    uint8_t discard[RNG_WIDTH];
    if (klen == 0) {
        k = tmpkey;
        klen = 1;
    }
    for (i = 0; i < RNG_WIDTH; i++) {
        a->s[i] = (uint8_t)i;
    }
    for (i = 0; i < RNG_WIDTH; i++) {
        uint8_t t = a->s[i];
        j = (uint8_t)(j + k[i % klen] + t);
        a->s[i] = a->s[j];
        a->s[j] = t;
    }
    a->i = 0;
    a->j = 0;
    arc4_g(a, RNG_WIDTH, discard);
}

static double seedrandom_next(arc4_t* a)
{
    const double WIDTH = 256.0;
    const uint32_t CHUNKS = 6;
    const double STARTDENOM = 281474976710656.0; /* 256^6 */
    const double SIGNIFICANCE = 4503599627370496.0; /* 2^52 */
    const double OVERFLOW = SIGNIFICANCE * 2.0;
    uint8_t buf[6];
    uint8_t xbuf[1];
    double n = 0;
    double d = STARTDENOM;
    uint32_t x = 0;
    uint32_t i;
    arc4_g(a, CHUNKS, buf);
    for (i = 0; i < CHUNKS; i++) {
        n = n * WIDTH + buf[i];
    }
    while (n < SIGNIFICANCE) {
        n = (n + x) * WIDTH;
        d *= WIDTH;
        arc4_g(a, 1, xbuf);
        x = xbuf[0];
    }
    while (n >= OVERFLOW) {
        n /= 2.0;
        d /= 2.0;
        x >>= 1;
    }
    return (n + x) / d;
}

/* ---- ChaCha20-legacy ---- */

static void qround(uint32_t* x, int a, int b, int c, int d)
{
    x[a] += x[b]; x[d] = rotl32(x[d] ^ x[a], 16);
    x[c] += x[d]; x[b] = rotl32(x[b] ^ x[c], 12);
    x[a] += x[b]; x[d] = rotl32(x[d] ^ x[a], 8);
    x[c] += x[d]; x[b] = rotl32(x[b] ^ x[c], 7);
}

static void chacha_block(uint32_t* dst, const uint32_t* state)
{
    int i;
    memcpy(dst, state, 16 * sizeof(uint32_t));
    for (i = 0; i < 20; i += 2) {
        qround(dst, 0, 4, 8, 12);
        qround(dst, 1, 5, 9, 13);
        qround(dst, 2, 6, 10, 14);
        qround(dst, 3, 7, 11, 15);
        qround(dst, 0, 5, 10, 15);
        qround(dst, 1, 6, 11, 12);
        qround(dst, 2, 7, 8, 13);
        qround(dst, 3, 4, 9, 14);
    }
    for (i = 0; i < 16; i++) {
        dst[i] += state[i];
    }
}

static void chacha_init(chacha_t* c, const uint8_t seed[40])
{
    static const char sigma[] = "expand 32-byte k";  /* 16 bytes + NUL */
    int i;
    memset(c, 0, sizeof(*c));
    memcpy(c->kn, sigma, 16);
    memcpy(((uint8_t*)c->kn) + 16, seed, 32);
    memcpy(((uint8_t*)c->kn) + 56, seed + 32, 8);
    c->counter = 0;
    c->buf_off = 64;
    c->buf_len = 64;
    (void)i;
}

static void chacha_fill(chacha_t* c)
{
    uint32_t state[16];
    uint32_t out[16];
    memcpy(state, c->kn, sizeof(state));
    state[12] = (uint32_t)(c->counter & 0xffffffffu);
    state[13] = (uint32_t)(c->counter >> 32);
    chacha_block(out, state);
    memcpy(c->buf, out, 64);
    c->buf_off = 0;
    c->counter++;
}

static void chacha_bytes(chacha_t* c, uint8_t* dst, size_t n)
{
    while (n) {
        size_t have;
        if (c->buf_off >= 64) {
            chacha_fill(c);
        }
        have = 64 - c->buf_off;
        if (have > n) {
            have = n;
        }
        memcpy(dst, c->buf + c->buf_off, have);
        c->buf_off += have;
        dst += have;
        n -= have;
    }
}

/* ---- Rng trait ---- */

static int u64_bit_width(uint64_t x)
{
    if (x == 0) {
        return 0;
    }
    return 64 - __builtin_clzll(x);
}

static uint64_t rng_int_by_bytes(rng_t* r, uint8_t bytes)
{
    uint8_t buf[8] = {0};
    uint64_t v = 0;
    size_t i;
    if (bytes == 0) {
        return 0;
    }
    if (bytes > 8) {
        bytes = 8;
    }
    if (r->algo == RNG_CHACHA20) {
        chacha_bytes(&r->chacha, buf, bytes);
    } else {
        arc4_g(&r->arc4, bytes, buf);
    }
    for (i = 0; i < bytes; i++) {
        v |= ((uint64_t)buf[i]) << (8 * i);
    }
    return v;
}

static uint64_t rng_int_by_bits(rng_t* r, uint8_t bits)
{
    uint8_t nbytes;
    uint8_t wasted;
    if (bits == 0) {
        return 0;
    }
    nbytes = (uint8_t)((bits + 7) / 8);
    wasted = (uint8_t)((nbytes << 3) - bits);
    return rng_int_by_bytes(r, nbytes) >> wasted;
}

static double rng_number_0_to_1(rng_t* r)
{
    uint64_t fraction = rng_int_by_bits(r, SAFE_INTEGER_BITS);
    uint64_t exponent = 1022;
    int remaining = SAFE_INTEGER_BITS - u64_bit_width(fraction);
    while (remaining > 0 && exponent >= SAFE_INTEGER_BITS) {
        exponent -= (uint64_t)remaining;
        fraction <<= remaining;
        fraction |= rng_int_by_bits(r, (uint8_t)remaining);
        remaining = SAFE_INTEGER_BITS - u64_bit_width(fraction);
    }
    if (remaining > 0) {
        uint64_t shift = (exponent - 1) < (uint64_t)remaining ? (exponent - 1) : (uint64_t)remaining;
        exponent -= shift;
        fraction <<= shift;
        fraction |= rng_int_by_bits(r, (uint8_t)shift);
    }
    {
        uint64_t bits = (exponent << FRACTION_BITS) | (fraction & FRACTION_MASK);
        double out;
        memcpy(&out, &bits, sizeof(out));
        return out;
    }
}

static uint64_t rng_uniform(rng_t* r, uint64_t max_inclusive)
{
    int bw = u64_bit_width(max_inclusive);
    for (;;) {
        uint64_t result = rng_int_by_bits(r, (uint8_t)bw);
        if (result <= max_inclusive) {
            return result;
        }
    }
}

static int rng_int_in_range(rng_t* r, double min, double max, int64_t* out)
{
    int64_t cmin = (int64_t)ceil(min);
    int64_t fmax = (int64_t)floor(max);
    int64_t scale, sign;
    if (cmin < fmax) {
        scale = fmax - cmin;
        sign = 1;
    } else if (cmin > fmax) {
        scale = cmin - fmax;
        sign = -1;
    } else {
        *out = cmin;
        return 1;
    }
    if (scale <= MAX_SAFE_INTEGER && llabs(cmin) <= MAX_SAFE_INTEGER && llabs(fmax) <= MAX_SAFE_INTEGER) {
        *out = (int64_t)rng_uniform(r, (uint64_t)scale) * sign + cmin;
        return 1;
    }
    return 0;
}

static void num_to_seed_str(double n, char* buf, size_t nbuf)
{
    if (n == 0.0) {
        snprintf(buf, nbuf, signbit(n) ? "-0" : "0");
        return;
    }
    snprintf(buf, nbuf, "%.16g", n);
}

as_value_t* as_std_rng_call(as_value_t** args, size_t arg_count, void* userdata)
{
    rng_t* r = (rng_t*)userdata;
    if (!r) {
        return as_value_num(0);
    }
    if (arg_count >= 2 && args[0] && args[1] &&
        args[0]->type == AS_VALUE_NUM && args[1]->type == AS_VALUE_NUM) {
        double min = args[0]->data.num_val;
        double max = args[1]->data.num_val;
        if (r->algo == RNG_RC4_LEGACY) {
            double rv = seedrandom_next(&r->arc4);
            double hi = floor(max);
            double lo = ceil(min);
            return as_value_num(floor(rv * (hi - lo + 1.0)) + lo);
        } else {
            int64_t v;
            if (rng_int_in_range(r, min, max, &v)) {
                return as_value_num((double)v);
            }
            return as_value_null();
        }
    }
    if (r->algo == RNG_RC4_LEGACY) {
        return as_value_num(seedrandom_next(&r->arc4));
    }
    return as_value_num(rng_number_0_to_1(r));
}

as_value_t* as_std_gen_rng(as_value_t** args, size_t arg_count, void* userdata)
{
    as_value_t* seed;
    as_value_t* options = NULL;
    const char* algo = "chacha20";
    int legacy_num = 0;
    rng_t* r;
    uint8_t key[256];
    size_t keylen = 0;
    (void)userdata;

    if (arg_count < 1 || !args[0]) {
        as_raise_runtime_error("`seed` is required.");
        return as_value_null();
    }
    seed = args[0];
    /* Mirrors AiScript 1.3.0: when options is given it must be an object that
     * carries a string `algorithm`; the seed is validated afterwards. */
    if (arg_count >= 2 && args[1]) {
        if (args[1]->type != AS_VALUE_OBJ) {
            as_raise_runtime_error("`options` must be an object if specified.");
            return as_value_null();
        }
        options = args[1];
        {
            as_value_t* a = as_obj_get(options->data.obj_val, "algorithm");
            if (!a || a->type != AS_VALUE_STR || !a->data.str_val) {
                as_raise_runtime_error("`options.algorithm` must be string.");
                return as_value_null();
            }
            algo = a->data.str_val;
            {
                as_value_t* lg = as_obj_get(options->data.obj_val, "chacha20_number_seed_legacy_behavior");
                if (lg && lg->type == AS_VALUE_BOOL) {
                    legacy_num = lg->data.bool_val;
                }
            }
        }
    }

    if (seed->type != AS_VALUE_STR && seed->type != AS_VALUE_NUM) {
        as_raise_runtime_error("`seed` must be either number or string.");
        return as_value_null();
    }

    r = as_calloc(1, sizeof(rng_t));
    if (strcmp(algo, "rc4_legacy") == 0) {
        char nbuf[64];
        const char* s;
        r->algo = RNG_RC4_LEGACY;
        if (seed->type == AS_VALUE_NUM) {
            num_to_seed_str(seed->data.num_val, nbuf, sizeof(nbuf));
            s = nbuf;
        } else {
            s = seed->data.str_val;
        }
        mixkey(s, key, &keylen);
        arc4_init(&r->arc4, key, keylen);
    } else if (strcmp(algo, "rc4") == 0) {
        char nbuf[64];
        const char* s;
        r->algo = RNG_RC4;
        if (seed->type == AS_VALUE_NUM) {
            num_to_seed_str(seed->data.num_val, nbuf, sizeof(nbuf));
            s = nbuf;
        } else {
            s = seed->data.str_val;
        }
        mixkey(s, key, &keylen);
        arc4_init(&r->arc4, key, keylen);
    } else if (strcmp(algo, "chacha20") == 0) {
        uint8_t digest[48];
        r->algo = RNG_CHACHA20;
        if (seed->type == AS_VALUE_NUM) {
            if (legacy_num) {
                /* Pre-1.3.0 behaviour: truncate to an integer, keep the low byte. */
                double n = seed->data.num_val;
                uint8_t b = 0;
                if (n == n && n - n == 0) {  /* finite */
                    b = (uint8_t)((long long)fmod(trunc(n), 256.0) & 0xFF);
                }
                sha384(&b, 1, digest);
            } else {
                uint8_t le[8];
                uint64_t bits;
                memcpy(&bits, &seed->data.num_val, 8);
                le[0] = (uint8_t)bits; le[1] = (uint8_t)(bits >> 8);
                le[2] = (uint8_t)(bits >> 16); le[3] = (uint8_t)(bits >> 24);
                le[4] = (uint8_t)(bits >> 32); le[5] = (uint8_t)(bits >> 40);
                le[6] = (uint8_t)(bits >> 48); le[7] = (uint8_t)(bits >> 56);
                sha384(le, 8, digest);
            }
        } else {
            const char* s = seed->data.str_val ? seed->data.str_val : "";
            sha384((const uint8_t*)s, strlen(s), digest);
        }
        chacha_init(&r->chacha, digest);
    } else {
        as_free(r);
        as_raise_runtime_error("`options.algorithm` must be one of these: `chacha20`, `rc4`, or `rc4_legacy`.");
        return as_value_null();
    }

    return as_value_fn_native(as_std_rng_call, r);
}
