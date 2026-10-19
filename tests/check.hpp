// A tiny check helper for the plain CTest executables in tests/.
#pragma once
#include <cstdio>

namespace test {

inline int& failures() {
  static int n = 0;
  return n;
}

inline int finish() {
  if (failures() != 0) std::fprintf(stderr, "%d check(s) failed\n", failures());
  return failures() == 0 ? 0 : 1;
}

}  // namespace test

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      ++test::failures();                                                    \
    }                                                                        \
  } while (0)
