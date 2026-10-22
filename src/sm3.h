#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace crypto {

using Digest32 = std::array<uint8_t, 32>;

Digest32 sm3(const uint8_t *data, size_t len);
inline Digest32 sm3(const std::vector<uint8_t> &data) { return sm3(data.data(), data.size()); }
inline Digest32 sm3(const std::string &data) {
    return sm3(reinterpret_cast<const uint8_t *>(data.data()), data.size());
}
std::string sm3Hex(const uint8_t *data, size_t len);
inline std::string sm3Hex(const std::string &data) {
    return sm3Hex(reinterpret_cast<const uint8_t *>(data.data()), data.size());
}

} // namespace crypto
