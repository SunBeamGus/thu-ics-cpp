#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace crypto {

// Encrypts with an SM2 public key (hex, "04||X||Y"; the leading 04 may be
// omitted). Returns "04||C1x||C1y||C3||C2" in lowercase hex, which is what
// sm-crypto / id.tsinghua.edu.cn expect (C1C3C2 mode).
std::string sm2Encrypt(const std::string &publicKeyHex, const std::vector<uint8_t> &message, std::string &error);

// Decrypts a ciphertext produced by sm2Encrypt. Only used by tests.
bool sm2Decrypt(const std::string &privateKeyHex, const std::string &cipherHex, std::vector<uint8_t> &message);

// Computes d*G; returns "04||X||Y". Only used by tests.
bool sm2PublicKeyFromPrivate(const std::string &privateKeyHex, std::string &publicKeyHex);

// Internal consistency checks (SM3 vectors + EC vectors).
bool sm2SelfCheck(std::string &error);

} // namespace crypto
