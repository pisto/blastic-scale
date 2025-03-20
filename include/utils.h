#pragma once

#include <type_traits>
#include <array>
#include <cstring>
#include <cm_backtrace/cm_backtrace.h>
#include <Arduino.h>

namespace util {

template <size_t size> struct StringBuffer : public std::array<char, size> {

  using std::array<char, size>::array;

  StringBuffer &operator=(const char *src) { this->strncpy(src); }

  operator char *() { return this->data(); }

  operator const char *() const { return this->data(); }

  StringBuffer &strncpy(const char *src, size_t len = size) {
    auto charsLen = len < size ? len : size - 1;
    (*this)[charsLen] = 0;
    __asm volatile ("" ::: "memory");
    std::strncpy(*this, src, charsLen);
    return *this;
  }
};

/*
  Some Arduino API is especially badly designed as some critical class members are private.
  Here use some magic to access them.

  https://stackoverflow.com/a/3173080
  http://bloglitb.blogspot.com/2011/12/access-to-private-members-safer.html
*/

template <typename Tag, typename Tag::type M> struct ClassPrivateMemberBackdoor {
  friend typename Tag::type get(Tag) { return M; }
};

// use this macro to define the necessary template specializations

#define ClassPrivateMemberAccessor(class, memberType, member)                                                          \
  namespace util {                                                                                                     \
  struct class##Backdoor {                                                                                             \
    typedef memberType class ::*type;                                                                                  \
    friend type get(class##Backdoor);                                                                                  \
  };                                                                                                                   \
  template struct ClassPrivateMemberBackdoor<class##Backdoor, &class ::member>;                                        \
  }

template <uint32_t version, uint32_t minVersion, typename enabledType>
using fromVersion = std::conditional_t<(version >= minVersion), enabledType, char[0]>;

using StackTrace = uint32_t[CMB_CALL_STACK_MAX_DEPTH];

inline size_t stackTrace(StackTrace &trace) {
  return cm_backtrace_call_stack(trace, CMB_CALL_STACK_MAX_DEPTH, cmb_get_sp());
}

void printStackTrace(const StackTrace &trace, size_t depth, Print &p);

} // namespace util
