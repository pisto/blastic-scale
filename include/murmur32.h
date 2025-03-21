#pragma once

#include <cstdint>
#include <type_traits>

namespace util {

namespace {

// constexpr strlen
constexpr uint32_t strlen(const char *const str) { return *str ? 1 + strlen(str + 1) : 0; }

// Reference: https://en.wikipedia.org/wiki/MurmurHash#Algorithm

constexpr uint32_t murmur3_32_scramble(uint32_t dword) {
  dword *= 0xcc9e2d51u;
  dword = (dword << 15) | (dword >> 17);
  dword *= 0x1b873593u;
  return dword;
}

} // namespace

/*
  This constexpr function calculates the murmur3 32bit hash of a string. Can be used to make lookup tables for strings.
*/

template <typename T> constexpr uint32_t murmur3_32(const T *const buff, size_t len) {
  static_assert(sizeof(T) == 1);
  uint32_t h = 0xfaa7c96cu, dwordLen = len & ~uint32_t(3), leftoverBytes = len & uint32_t(3);
  for (uint32_t i = 0; i < dwordLen; i += 4) {
    uint32_t dword = 0;
    for (uint32_t j = 0; j < 4; j++) dword |= std::make_unsigned_t<T>(buff[i + j]) << j * 8;
    h ^= murmur3_32_scramble(dword);
    h = (h << 13) | (h >> 19);
    h = h * 5 + 0xe6546b64u;
  }
  uint32_t partialDword = 0;
  for (uint32_t i = 0; i < leftoverBytes; i++) partialDword |= std::make_unsigned_t<T>(buff[dwordLen + i]) << i * 8;
  h ^= murmur3_32_scramble(partialDword);
  h ^= len;
  h ^= h >> 16;
  h *= 0x85ebca6bu;
  h ^= h >> 13;
  h *= 0xc2b2ae35u;
  h ^= h >> 16;
  return h;
}

constexpr uint32_t murmur3_32(const char *buff) { return murmur3_32(buff, strlen(buff)); }
constexpr uint32_t murmur3_32(char *buff) { return murmur3_32(buff, strlen(buff)); }
template <typename T> uint32_t murmur3_32(const T &obj) {
  return murmur3_32(reinterpret_cast<const unsigned char *>(&obj), sizeof(obj));
}

} // namespace util
