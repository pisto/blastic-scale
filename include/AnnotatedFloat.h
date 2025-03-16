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
    uint32_t annotation : 22, isnan : 1, exponent : 8, sign : 1;
  };

  AnnotatedFloat() = default;
  explicit constexpr AnnotatedFloat(float f) : f(f) {}

  explicit AnnotatedFloat(const char *msg) : f(NAN) {
    annotation = 0;
    for (int i = 0; i < 3 && msg[i]; i++) annotation |= msg[i] << (i * 8);
  }

  void getAnnotation(char *msg) const {
    for (uint32_t annotation = isnan ? this->annotation : 0, i = 0; i < 3; i++, annotation >>= 8)
      msg[i] = annotation;
    msg[3] = 0;
  }

  bool operator==(const AnnotatedFloat &o) const {
    if (isnan && o.isnan) return annotation == o.annotation;
    return f == o.f;
  }
  bool operator!=(const AnnotatedFloat &o) const { return !(*this == o); }
  operator float() const { return f; }
  operator float &() { return f; }
};

static_assert(sizeof(AnnotatedFloat) == sizeof(float));

} // namespace util
