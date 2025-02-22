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

} // namespace util
