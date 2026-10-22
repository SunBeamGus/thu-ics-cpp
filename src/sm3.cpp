#include "sm3.h"

#include <cstring>

namespace crypto {
namespace {

inline uint32_t rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

inline uint32_t loadBe32(const uint8_t *p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

inline void storeBe32(uint8_t *p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

inline uint32_t p0(uint32_t x) { return x ^ rotl(x, 9) ^ rotl(x, 17); }
inline uint32_t p1(uint32_t x) { return x ^ rotl(x, 15) ^ rotl(x, 23); }

void compress(uint32_t v[8], const uint8_t block[64]) {
    uint32_t w[68];
    for (int i = 0; i < 16; ++i) w[i] = loadBe32(block + i * 4);
    for (int i = 16; i < 68; ++i) {
        w[i] = p1(w[i - 16] ^ w[i - 9] ^ rotl(w[i - 3], 15)) ^ rotl(w[i - 13], 7) ^ w[i - 6];
    }
    uint32_t a = v[0], b = v[1], c = v[2], d = v[3];
    uint32_t e = v[4], f = v[5], g = v[6], h = v[7];
    for (int j = 0; j < 64; ++j) {
        const uint32_t t = j < 16 ? 0x79cc4519u : 0x7a879d8au;
        const uint32_t ss1 = rotl(rotl(a, 12) + e + rotl(t, j % 32), 7);
        const uint32_t ss2 = ss1 ^ rotl(a, 12);
        const uint32_t ff = j < 16 ? (a ^ b ^ c) : ((a & b) | (a & c) | (b & c));
        const uint32_t gg = j < 16 ? (e ^ f ^ g) : ((e & f) | (~e & g));
        const uint32_t tt1 = ff + d + ss2 + (w[j] ^ w[j + 4]);
        const uint32_t tt2 = gg + h + ss1 + w[j];
        d = c;
        c = rotl(b, 9);
        b = a;
        a = tt1;
        h = g;
        g = rotl(f, 19);
        f = e;
        e = p0(tt2);
    }
    v[0] ^= a;
    v[1] ^= b;
    v[2] ^= c;
    v[3] ^= d;
    v[4] ^= e;
    v[5] ^= f;
    v[6] ^= g;
    v[7] ^= h;
}

} // namespace

Digest32 sm3(const uint8_t *data, size_t len) {
    uint32_t v[8] = {0x7380166fu, 0x4914b2b9u, 0x172442d7u, 0xda8a0600u,
                     0xa96f30bcu, 0x163138aau, 0xe38dee4du, 0xb0fb0e4eu};
    const uint64_t bitLen = static_cast<uint64_t>(len) * 8;
    size_t full = len / 64;
    for (size_t i = 0; i < full; ++i) compress(v, data + i * 64);

    uint8_t tail[128] = {0};
    size_t rem = len - full * 64;
    if (rem) std::memcpy(tail, data + full * 64, rem);
    tail[rem] = 0x80;
    size_t tailBlocks = (rem + 1 + 8 <= 64) ? 1 : 2;
    size_t tailLen = tailBlocks * 64;
    for (int i = 0; i < 8; ++i) tail[tailLen - 1 - i] = static_cast<uint8_t>(bitLen >> (8 * i));
    compress(v, tail);
    if (tailBlocks == 2) compress(v, tail + 64);

    Digest32 out{};
    for (int i = 0; i < 8; ++i) storeBe32(out.data() + i * 4, v[i]);
    return out;
}

std::string sm3Hex(const uint8_t *data, size_t len) {
    static const char *hex = "0123456789abcdef";
    Digest32 d = sm3(data, len);
    std::string out;
    out.reserve(64);
    for (uint8_t b : d) {
        out.push_back(hex[b >> 4]);
        out.push_back(hex[b & 0x0f]);
    }
    return out;
}

} // namespace crypto
