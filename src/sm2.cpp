#include "sm2.h"

#include "sm3.h"
#include "util.h"

#include <windows.h>
#include <bcrypt.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace crypto {
namespace {

// ---------------------------------------------------------------------------
// 256-bit unsigned integers (little-endian 32-bit limbs)
// ---------------------------------------------------------------------------

struct U256 {
    uint32_t w[8]{};
};

U256 u256FromU64(uint64_t v) {
    U256 r;
    r.w[0] = static_cast<uint32_t>(v);
    r.w[1] = static_cast<uint32_t>(v >> 32);
    return r;
}

bool u256IsZero(const U256 &a) {
    for (uint32_t v : a.w)
        if (v) return false;
    return true;
}

int u256Cmp(const U256 &a, const U256 &b) {
    for (int i = 7; i >= 0; --i) {
        if (a.w[i] < b.w[i]) return -1;
        if (a.w[i] > b.w[i]) return 1;
    }
    return 0;
}

U256 u256Add(const U256 &a, const U256 &b, uint32_t &carry) {
    U256 r;
    uint64_t c = 0;
    for (int i = 0; i < 8; ++i) {
        uint64_t s = static_cast<uint64_t>(a.w[i]) + b.w[i] + c;
        r.w[i] = static_cast<uint32_t>(s);
        c = s >> 32;
    }
    carry = static_cast<uint32_t>(c);
    return r;
}

U256 u256Sub(const U256 &a, const U256 &b, uint32_t &borrow) {
    U256 r;
    uint64_t br = 0;
    for (int i = 0; i < 8; ++i) {
        uint64_t sub = static_cast<uint64_t>(b.w[i]) + br;
        uint64_t cur = a.w[i];
        r.w[i] = static_cast<uint32_t>(cur - sub);
        br = cur < sub ? 1 : 0;
    }
    borrow = static_cast<uint32_t>(br);
    return r;
}

U256 u256SubSmall(const U256 &a, uint32_t v) {
    U256 r = a;
    uint64_t borrow = v;
    for (int i = 0; i < 8 && borrow; ++i) {
        uint64_t cur = r.w[i];
        r.w[i] = static_cast<uint32_t>(cur - borrow);
        borrow = cur < borrow ? 1 : 0;
    }
    return r;
}

U256 u256ShiftRight2(U256 a) {
    U256 out;
    uint32_t carry = 0;
    for (int i = 7; i >= 0; --i) {
        out.w[i] = (a.w[i] >> 2) | (carry << 30);
        carry = a.w[i] & 3u;
    }
    return out;
}

U256 u256FromHex(const std::string &hex) {
    U256 r;
    std::string clean;
    for (char c : hex) {
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) clean.push_back(c);
    }
    if (clean.size() > 64) clean = clean.substr(clean.size() - 64);
    while (clean.size() % 8 != 0) clean.insert(clean.begin(), '0');
    int limb = 0;
    for (size_t end = clean.size(); end > 0 && limb < 8; end -= 8) {
        r.w[limb++] = static_cast<uint32_t>(std::stoul(clean.substr(end - 8, 8), nullptr, 16));
    }
    return r;
}

std::string u256ToHex(const U256 &a) {
    char buf[65];
    std::snprintf(buf, sizeof(buf), "%08x%08x%08x%08x%08x%08x%08x%08x", a.w[7], a.w[6], a.w[5], a.w[4], a.w[3],
                  a.w[2], a.w[1], a.w[0]);
    return buf;
}

void u256ToBytes(const U256 &a, uint8_t out[32]) {
    for (int i = 0; i < 8; ++i) {
        out[4 * i] = static_cast<uint8_t>(a.w[7 - i] >> 24);
        out[4 * i + 1] = static_cast<uint8_t>(a.w[7 - i] >> 16);
        out[4 * i + 2] = static_cast<uint8_t>(a.w[7 - i] >> 8);
        out[4 * i + 3] = static_cast<uint8_t>(a.w[7 - i]);
    }
}

U256 u256FromBytes(const uint8_t in[32]) {
    U256 r;
    for (int i = 0; i < 8; ++i) {
        r.w[7 - i] = (static_cast<uint32_t>(in[4 * i]) << 24) | (static_cast<uint32_t>(in[4 * i + 1]) << 16) |
                     (static_cast<uint32_t>(in[4 * i + 2]) << 8) | static_cast<uint32_t>(in[4 * i + 3]);
    }
    return r;
}

int bitAt(const U256 &a, int i) { return (a.w[i / 32] >> (i % 32)) & 1u; }

// ---------------------------------------------------------------------------
// Field arithmetic modulo p (Montgomery form, 32-bit limbs)
// ---------------------------------------------------------------------------

const U256 P = {{0xffffffffu, 0xffffffffu, 0x00000000u, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
                 0xfffffffeu}};
const U256 R2 = {{0x00000003u, 0x00000002u, 0xffffffffu, 0x00000002u, 0x00000001u, 0x00000001u, 0x00000002u,
                  0x00000004u}};
const U256 N = {{0x39d54123u, 0x53bbf409u, 0x21c6052bu, 0x7203df6bu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
                 0xfffffffeu}};

const uint32_t MONT_N0 = 0x00000001u; // -p^{-1} mod 2^32

U256 montMul(const U256 &a, const U256 &b) {
    uint32_t t[10] = {0};
    for (int i = 0; i < 8; ++i) {
        uint64_t carry = 0;
        for (int j = 0; j < 8; ++j) {
            uint64_t sum = static_cast<uint64_t>(t[j]) + static_cast<uint64_t>(a.w[j]) * b.w[i] + carry;
            t[j] = static_cast<uint32_t>(sum);
            carry = sum >> 32;
        }
        uint64_t sum = static_cast<uint64_t>(t[8]) + carry;
        t[8] = static_cast<uint32_t>(sum);
        t[9] = static_cast<uint32_t>(sum >> 32);

        uint32_t m = static_cast<uint32_t>(static_cast<uint64_t>(t[0]) * MONT_N0);
        carry = 0;
        for (int j = 0; j < 8; ++j) {
            uint64_t s = static_cast<uint64_t>(t[j]) + static_cast<uint64_t>(m) * P.w[j] + carry;
            if (j > 0) t[j - 1] = static_cast<uint32_t>(s);
            carry = s >> 32;
        }
        sum = static_cast<uint64_t>(t[8]) + carry;
        t[7] = static_cast<uint32_t>(sum);
        t[8] = t[9] + static_cast<uint32_t>(sum >> 32);
        t[9] = 0;
    }
    U256 r;
    for (int i = 0; i < 8; ++i) r.w[i] = t[i];
    if (t[8] != 0 || u256Cmp(r, P) >= 0) {
        uint32_t borrow;
        r = u256Sub(r, P, borrow);
    }
    return r;
}

U256 fAdd(const U256 &a, const U256 &b) {
    uint32_t carry;
    U256 r = u256Add(a, b, carry);
    if (carry || u256Cmp(r, P) >= 0) {
        uint32_t borrow;
        r = u256Sub(r, P, borrow);
    }
    return r;
}

U256 fSub(const U256 &a, const U256 &b) {
    uint32_t borrow;
    U256 r = u256Sub(a, b, borrow);
    if (borrow) {
        uint32_t c2;
        r = u256Add(r, P, c2);
    }
    return r;
}

U256 fMul(const U256 &a, const U256 &b) { return montMul(a, b); }
U256 fSqr(const U256 &a) { return montMul(a, a); }

U256 fDouble(const U256 &a) { return fAdd(a, a); }

U256 fMulSmall(const U256 &a, unsigned n) {
    U256 r = a;
    for (unsigned i = 1; i < n; ++i) r = fAdd(r, a);
    return r;
}

U256 gOneMont;

U256 fPow(const U256 &base, const U256 &exp) {
    U256 result = gOneMont;
    for (int i = 255; i >= 0; --i) {
        result = fSqr(result);
        if (bitAt(exp, i)) result = fMul(result, base);
    }
    return result;
}

U256 fInv(const U256 &a) {
    U256 exp = u256SubSmall(P, 2);
    return fPow(a, exp);
}

U256 fToMont(const U256 &a) { return montMul(a, R2); }
U256 fFromMont(const U256 &a) { return montMul(a, u256FromU64(1)); }

// ---------------------------------------------------------------------------
// Curve points (Jacobian, coordinates in Montgomery form)
// ---------------------------------------------------------------------------

struct Point {
    U256 X{}, Y{}, Z{};
    bool inf = true;
};

const char *GX_HEX = "32c4ae2c1f1981195f9904466a39c9948fe30bbff2660be1715a4589334c74c7";
const char *GY_HEX = "bc3736a2f4f6779c59bdcee36b692153d0a9877cc62a474002df32e52139f0a0";
const char *B_HEX = "28e9fa9e9d9f5e344d5a9e4bcf6509a7f39789f515ab8f92ddbcbd414d940e93";

U256 gB;
U256 gA;

Point gG;

Point makeInfinity() {
    Point p;
    p.inf = true;
    return p;
}

Point makeAffine(const U256 &xMont, const U256 &yMont) {
    Point p;
    p.X = xMont;
    p.Y = yMont;
    p.Z = gOneMont;
    p.inf = false;
    return p;
}

Point pointDouble(const Point &p) {
    if (p.inf || u256IsZero(p.Y)) return makeInfinity();
    U256 delta = fSqr(p.Z);
    U256 gamma = fSqr(p.Y);
    U256 beta = fMul(p.X, gamma);
    U256 alpha = fMulSmall(fMul(fSub(p.X, delta), fAdd(p.X, delta)), 3);
    U256 x3 = fSub(fSqr(alpha), fMulSmall(beta, 8));
    U256 z3 = fSub(fSub(fSqr(fAdd(p.Y, p.Z)), gamma), delta);
    U256 y3 = fSub(fMul(alpha, fSub(fMulSmall(beta, 4), x3)), fMulSmall(fSqr(gamma), 8));
    return {x3, y3, z3, false};
}

Point pointAdd(const Point &a, const Point &b) {
    if (a.inf) return b;
    if (b.inf) return a;
    U256 z1z1 = fSqr(a.Z);
    U256 z2z2 = fSqr(b.Z);
    U256 u1 = fMul(a.X, z2z2);
    U256 u2 = fMul(b.X, z1z1);
    U256 s1 = fMul(a.Y, fMul(b.Z, z2z2));
    U256 s2 = fMul(b.Y, fMul(a.Z, z1z1));
    if (u256Cmp(u1, u2) == 0) {
        if (u256Cmp(s1, s2) != 0) return makeInfinity();
        return pointDouble(a);
    }
    U256 h = fSub(u2, u1);
    U256 i = fSqr(fDouble(h));
    U256 j = fMul(h, i);
    U256 r = fDouble(fSub(s2, s1));
    U256 v = fMul(u1, i);
    U256 x3 = fSub(fSub(fSqr(r), j), fDouble(v));
    U256 y3 = fSub(fMul(r, fSub(v, x3)), fMul(fDouble(s1), j));
    U256 z3 = fMul(fSub(fSub(fSqr(fAdd(a.Z, b.Z)), z1z1), z2z2), h);
    return {x3, y3, z3, false};
}

Point scalarMul(const U256 &k, const Point &p) {
    Point result = makeInfinity();
    for (int i = 255; i >= 0; --i) {
        result = pointDouble(result);
        if (bitAt(k, i)) result = pointAdd(result, p);
    }
    return result;
}

bool toAffine(const Point &p, U256 &xMont, U256 &yMont) {
    if (p.inf) return false;
    U256 zinv = fInv(p.Z);
    U256 zinv2 = fSqr(zinv);
    xMont = fMul(p.X, zinv2);
    yMont = fMul(p.Y, fMul(zinv2, zinv));
    return true;
}

bool onCurve(const U256 &xMont, const U256 &yMont) {
    U256 y2 = fSqr(yMont);
    U256 x3 = fMul(fSqr(xMont), xMont);
    U256 rhs = fAdd(fAdd(x3, fMul(gA, xMont)), gB);
    return u256Cmp(y2, rhs) == 0;
}

void initCurve() {
    static bool done = false;
    if (done) return;
    gOneMont = montMul(u256FromU64(1), R2);
    gA = fToMont(u256SubSmall(P, 3));
    gB = fToMont(u256FromHex(B_HEX));
    gG = makeAffine(fToMont(u256FromHex(GX_HEX)), fToMont(u256FromHex(GY_HEX)));
    done = true;
}

// ---------------------------------------------------------------------------
// SM2 helpers
// ---------------------------------------------------------------------------

bool randomBytes(uint8_t *buf, size_t len) {
    NTSTATUS status = BCryptGenRandom(nullptr, buf, static_cast<ULONG>(len), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return status == 0;
}

U256 randomScalar() {
    for (;;) {
        uint8_t buf[32];
        if (!randomBytes(buf, sizeof(buf))) return u256FromU64(1);
        U256 k = u256FromBytes(buf);
        while (u256Cmp(k, N) >= 0) {
            uint32_t borrow;
            k = u256Sub(k, N, borrow);
        }
        if (!u256IsZero(k)) return k;
    }
}

std::vector<uint8_t> kdf(const U256 &x2Mont, const U256 &y2Mont, size_t len) {
    uint8_t xb[32], yb[32];
    u256ToBytes(fFromMont(x2Mont), xb);
    u256ToBytes(fFromMont(y2Mont), yb);
    std::vector<uint8_t> out;
    out.reserve(len);
    uint32_t ct = 1;
    while (out.size() < len) {
        std::vector<uint8_t> input(xb, xb + 32);
        input.insert(input.end(), yb, yb + 32);
        input.push_back(static_cast<uint8_t>(ct >> 24));
        input.push_back(static_cast<uint8_t>(ct >> 16));
        input.push_back(static_cast<uint8_t>(ct >> 8));
        input.push_back(static_cast<uint8_t>(ct));
        Digest32 h = sm3(input);
        size_t need = len - out.size();
        size_t take = need < 32 ? need : 32;
        out.insert(out.end(), h.begin(), h.begin() + static_cast<std::ptrdiff_t>(take));
        ++ct;
    }
    return out;
}

bool parsePublicKey(const std::string &hex, U256 &xMont, U256 &yMont) {
    std::string clean;
    for (char c : hex) {
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) clean.push_back(c);
    }
    if (clean.size() == 130 && (clean[0] == '0') && (clean[1] == '4')) {
        clean = clean.substr(2);
    } else if (clean.size() == 66 && (clean.substr(0, 2) == "02" || clean.substr(0, 2) == "03")) {
        // Compressed key: recover y from y^2 = x^3 - 3x + b.
        U256 x = fToMont(u256FromHex(clean.substr(2)));
        U256 rhs = fAdd(fAdd(fMul(fSqr(x), x), fMul(gA, x)), gB);
        uint32_t carry = 0;
        U256 exp = u256ShiftRight2(u256Add(P, u256FromU64(1), carry)); // (p+1)/4
        U256 y = fPow(rhs, exp);
        bool wantOdd = clean[1] == '3';
        uint8_t yb[32];
        u256ToBytes(fFromMont(y), yb);
        bool isOdd = (yb[31] & 1) != 0;
        if (isOdd != wantOdd) y = fSub(u256FromU64(0), y);
        if (!onCurve(x, y)) return false;
        xMont = x;
        yMont = y;
        return true;
    }
    if (clean.size() != 128) return false;
    U256 x = fToMont(u256FromHex(clean.substr(0, 64)));
    U256 y = fToMont(u256FromHex(clean.substr(64, 64)));
    if (!onCurve(x, y)) return false;
    xMont = x;
    yMont = y;
    return true;
}

void appendU256Hex(std::string &out, const U256 &normal) {
    out += u256ToHex(normal);
}

} // namespace

std::string sm2Encrypt(const std::string &publicKeyHex, const std::vector<uint8_t> &message, std::string &error) {
    initCurve();
    U256 pubX, pubY;
    if (!parsePublicKey(publicKeyHex, pubX, pubY)) {
        error = "invalid SM2 public key";
        return "";
    }
    Point pub = makeAffine(pubX, pubY);

    for (int attempt = 0; attempt < 16; ++attempt) {
        U256 k = randomScalar();
        U256 x1, y1, x2, y2;
        if (!toAffine(scalarMul(k, gG), x1, y1)) continue;
        if (!toAffine(scalarMul(k, pub), x2, y2)) continue;

        std::vector<uint8_t> t = kdf(x2, y2, message.size());
        if (!message.empty()) {
            bool allZero = true;
            for (uint8_t b : t)
                if (b) {
                    allZero = false;
                    break;
                }
            if (allZero) continue;
        }

        std::vector<uint8_t> c2(message.size());
        for (size_t i = 0; i < message.size(); ++i) c2[i] = static_cast<uint8_t>(message[i] ^ t[i]);

        uint8_t xb[32], yb[32];
        u256ToBytes(fFromMont(x2), xb);
        u256ToBytes(fFromMont(y2), yb);
        std::vector<uint8_t> hashInput(xb, xb + 32);
        hashInput.insert(hashInput.end(), message.begin(), message.end());
        hashInput.insert(hashInput.end(), yb, yb + 32);
        Digest32 c3 = sm3(hashInput);

        std::string out = "04";
        appendU256Hex(out, fFromMont(x1));
        appendU256Hex(out, fFromMont(y1));
        out += util::hexEncode(c3.data(), c3.size());
        out += util::hexEncode(c2);
        return out;
    }
    error = "SM2 encryption failed (KDF produced all-zero key)";
    return "";
}

bool sm2Decrypt(const std::string &privateKeyHex, const std::string &cipherHex, std::vector<uint8_t> &message) {
    initCurve();
    std::string clean;
    for (char c : cipherHex) {
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) clean.push_back(c);
    }
    if (clean.size() < 128 + 64) return false;
    if (clean.size() >= 130 && clean.substr(0, 2) == "04") clean = clean.substr(2);
    if (clean.size() < 128 + 64) return false;

    U256 x = fToMont(u256FromHex(clean.substr(0, 64)));
    U256 y = fToMont(u256FromHex(clean.substr(64, 64)));
    if (!onCurve(x, y)) return false;
    std::string c3Hex = clean.substr(128, 64);
    std::string c2Hex = clean.substr(192);

    U256 d = u256FromHex(privateKeyHex);
    if (u256IsZero(d) || u256Cmp(d, N) >= 0) return false;

    U256 x2, y2;
    if (!toAffine(scalarMul(d, makeAffine(x, y)), x2, y2)) return false;
    std::vector<uint8_t> c2 = util::hexDecode(c2Hex);
    std::vector<uint8_t> t = kdf(x2, y2, c2.size());
    message.resize(c2.size());
    for (size_t i = 0; i < c2.size(); ++i) message[i] = static_cast<uint8_t>(c2[i] ^ t[i]);

    uint8_t xb[32], yb[32];
    u256ToBytes(fFromMont(x2), xb);
    u256ToBytes(fFromMont(y2), yb);
    std::vector<uint8_t> hashInput(xb, xb + 32);
    hashInput.insert(hashInput.end(), message.begin(), message.end());
    hashInput.insert(hashInput.end(), yb, yb + 32);
    Digest32 c3 = sm3(hashInput);
    return util::hexEncode(c3.data(), c3.size()) == util::toLower(c3Hex);
}

bool sm2PublicKeyFromPrivate(const std::string &privateKeyHex, std::string &publicKeyHex) {
    initCurve();
    U256 d = u256FromHex(privateKeyHex);
    if (u256IsZero(d) || u256Cmp(d, N) >= 0) return false;
    U256 x, y;
    if (!toAffine(scalarMul(d, gG), x, y)) return false;
    publicKeyHex = "04" + u256ToHex(fFromMont(x)) + u256ToHex(fFromMont(y));
    return true;
}

bool sm2SelfCheck(std::string &error) {
    initCurve();
    {
        Digest32 d = sm3(std::string("abc"));
        std::string hex = util::hexEncode(d.data(), d.size());
        if (hex != "66c7f0f462eeedd9d1f2d46bdc10e4e24167c4875cf2f7a2297da02b8f4ba8e0") {
            error = "SM3 test vector failed: " + hex;
            return false;
        }
    }
    const std::string priv = "3945208f7b2144b13f36e38ac6d39f95889393692860b51a42fb81ef4df7c5b8";
    const std::string pub =
        "0409f9df311e5421a150dd7d161e4bc5c672179fad1833fc076bb08ff356f35020"
        "ccea490ce26775a52dc6ea718cc1aa600aed05fbf35e084a6632f6072da9ad13";
    std::string derived;
    if (!sm2PublicKeyFromPrivate(priv, derived) || derived != pub) {
        error = "SM2 dG mismatch: " + derived;
        return false;
    }
    const std::string cipher =
        "783f23c439f47c2235d978399a879a865a96bec8c181beb0c7941d40f5102b0d"
        "1bd9f182def9800df4a3589804ff196dbecd3fe3bcf1025937cc27fd1fd80f83"
        "09911556441d5aa9f955d10c536deaeb0cb7172e2c116ecee1f768e4c0b8f0fb"
        "164129c6ebadae103b7868f620891d95d9fd";
    std::vector<uint8_t> plain;
    if (!sm2Decrypt(priv, cipher, plain)) {
        error = "SM2 decrypt of reference ciphertext failed";
        return false;
    }
    std::string text(plain.begin(), plain.end());
    if (text != "Hello, 清华! 123") {
        error = "SM2 plaintext mismatch: " + text;
        return false;
    }
    std::string encErr;
    std::string enc = sm2Encrypt(pub, std::vector<uint8_t>(text.begin(), text.end()), encErr);
    if (enc.empty()) {
        error = "SM2 encrypt failed: " + encErr;
        return false;
    }
    std::vector<uint8_t> round;
    if (!sm2Decrypt(priv, enc, round) || std::string(round.begin(), round.end()) != text) {
        error = "SM2 round-trip failed";
        return false;
    }
    return true;
}

} // namespace crypto
