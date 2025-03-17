#pragma once

#include <cstdint>
#include <cmath>

namespace util {

/*
  AnnotatedFloat is a wrapper around float that can be used to store "error codes" in the unused bits of a NaN float
  value.
*/

union AnnotatedFloat {
  float f;
  struct {
    uint32_t fraction : 22, signaling : 1, exponent : 8, sign : 1;
  };

  AnnotatedFloat() = default;
  explicit constexpr AnnotatedFloat(float f) : f(f) {}

  explicit AnnotatedFloat(const char *msg) : f(NAN) {
    fraction = 0;
    for (int i = 0; i < 3 && msg[i]; i++) fraction |= msg[i] << (i * 8);
  }

  void getAnnotation(char *msg) const {
    for (uint32_t fraction = isnan(f) ? this->fraction : 0, i = 0; i < 3; i++, fraction >>= 8)
      msg[i] = fraction;
    msg[3] = 0;
  }

  bool operator==(const AnnotatedFloat &o) const {
    if (isnan(f) && isnan(o.f)) return fraction == o.fraction && signaling == o.signaling && sign == o.sign;
    return f == o.f;
  }
  bool operator!=(const AnnotatedFloat &o) const { return !(*this == o); }
  operator float() const { return f; }
  operator float &() { return f; }
};

static_assert(sizeof(AnnotatedFloat) == sizeof(float));

} // namespace util
