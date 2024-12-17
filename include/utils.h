#pragma once

#include <array>
#include <cstring>

namespace util {

template <size_t size> struct StringBuffer : public std::array<char, size> {

  using std::array<char, size>::array;

  StringBuffer &operator=(const char *src) { this->strncpy(src); }

  operator char *() { return this->data(); }

  operator const char *() const { return this->data(); }

  StringBuffer &strncpy(const char *src, size_t len = size) {
    std::strncpy(*this, src, len);
    (*this)[len < size ? len : size - 1] = 0;
    return *this;
  }
};

} // namespace util
