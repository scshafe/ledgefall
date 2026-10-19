// The skeleton's one test: the library links and reports its version.
#include <cstdio>
#include <cstring>

#include "ledgefall/version.hpp"

int main() {
  if (std::strcmp(ledgefall::version(), "0.0.1") != 0) {
    std::fprintf(stderr, "unexpected version %s\n", ledgefall::version());
    return 1;
  }
  return 0;
}
