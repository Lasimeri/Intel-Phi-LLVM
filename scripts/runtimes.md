# scripts/runtimes.sh

The C and C++ half of the S0 gate. It builds the runtimes with this
repository's clang, with the same cmake options as Intel-Phi-3120A's
`toolchain/compiler-rt`, `libunwind` and `libcxx` scripts:

- **compiler-rt:** builtins and crt, with `COMPILER_RT_X86_NO_SSE`
  (patch 0007), installed into this clang's resource directory.
- **The C++ runtimes:** libunwind, libc++abi and libc++, static, installed
  into `~/.cache/intel-phi-llvm-build/sysroot`.

That sysroot is a fresh copy of the stack's sysroot, made on every run, with the stack's libc++ and libunwind headers and libraries removed first (measured 2026-10-15: a libc++ 22 header left in place broke `<vector>` under libc++ 23),
so the stack's live sysroot (which the card's userland builds against) is
never written. musl in the copy is still the stack's 22.1.8-built musl.

Checks:

- The source must be the pinned commit.
- `phi-isa-audit` runs on each library (the builtins with
  `--allow-suspect`, as in the stack).
- Two static programs are linked, audited and run on the host, whose
  instruction set covers the card's:
  - C with libm and pthreads (expects `c 2.121320`, exit 0);
  - C++ with an exception and a vector (expects `c++ x 3.8`).

The output ends with `runtimes.sh: PASS`, and any failure stops the
script.
