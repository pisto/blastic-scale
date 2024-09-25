#pragma once

#include <cstdint>

namespace util {

// constexpr strlen
constexpr static uint32_t strlen(const char *const str) { return *str ? 1 + strlen(str + 1) : 0; }

// Reference: https://en.wikipedia.org/wiki/MurmurHash#Algorithm

constexpr static uint32_t murmur3_32_scramble(uint32_t dword) {
  dword *= 0xcc9e2d51;
  dword = (dword << 15) | (dword >> 17);
  dword *= 0x1b873593;
  return dword;
}

constexpr static uint32_t murmur3_32(const char *const str) {
  uint32_t h = 0xfaa7c96c, len = strlen(str), dwordLen = len & ~uint32_t(3),
           leftoverBytes = len & uint32_t(3);
  for (uint32_t i = 0; i < dwordLen; i += 4) {
    uint32_t dword = 0;
    for (uint32_t j = 0; j < 4; j++) dword |= str[i + j] << j * 8;
    h ^= murmur3_32_scramble(dword);
    h = (h << 13) | (h >> 19);
    h = h * 5 + 0xe6546b64;
  }
  uint32_t partialDword = 0;
  for (uint32_t i = 0; i < leftoverBytes; i++) partialDword |= str[dwordLen + i] << i * 8;
  h ^= murmur3_32_scramble(partialDword);
  h ^= len;
  h ^= h >> 16;
  h *= 0x85ebca6b;
  h ^= h >> 13;
  h *= 0xc2b2ae35;
  h ^= h >> 16;
  return h;
}

} // namespace util
