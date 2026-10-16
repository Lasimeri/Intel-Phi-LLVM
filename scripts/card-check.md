# scripts/card-check.sh

The S0 card gate: `scripts/card-check.sh [CARD]` (default card 0).

1. It copies the tarball from `scripts/card-package.sh` and the two test
   sources from `scripts/runtimes.sh` to the card's `/data` with
   `phi put`.
2. On the card it recomputes the tarball's SHA-256 and refuses to unpack
   unless the sum matches the one computed on the host. A corrupted or
   substituted copy never runs.
3. It unpacks into the prefix (`/data/llvm-knc`), prints the version,
   compiles the C program (pthreads and libm) and the C++ program
   (exceptions and a vector) with the card's own clang, times each
   compile, runs both and prints their exit codes. Expected output:
   `c 2.121320`, `c exit 0`, `c++ x 3.8`, `c++ exit 0`.
4. It removes the test binaries and sources. The toolchain stays in the
   prefix.

Only one test should use a card at a time. The co-processor worker on the
card is not touched; this runs as an ordinary card program over the
control socket.
