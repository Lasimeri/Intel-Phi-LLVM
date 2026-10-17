# scripts/s3-gate.sh, scripts/s3-libknc-gate.sh

The S3 gate: the stack's hand-encoded vector code against the same code
written with the Knights Corner intrinsics, on a card.

```
scripts/s3-gate.sh NAME [CARD]        # NAME: vpu_memcpy, mandel_vpu, vpu_int, bitunpack
scripts/s3-libknc-gate.sh [CARD]      # libknc
LLVM_BIN=<bin dir> ...                # the clang to test (default: the series build)
```

`s3-gate.sh` assembles Intel-Phi-3120A's `card/examples/NAME.S` with the
series clang and prefixes its symbols `ref_` (`llvm-objcopy
--prefix-symbols`), compiles `tests/s3/NAME.c` with the clang under test
(`config/knc.cfg`, `-O2`), and links both with `tests/s3/NAME-gate.c`.
Before the card run it checks that every Knights Corner instruction the
compiler emitted reassembles to the same bytes in Intel's gas
(`oracle/codegen-check.sh`), and that the stack's audit finds no
instruction the card lacks. The gate binary prints the comparison and
`NAME-gate: PASS`, `SLOWER` or `FAIL`.

`s3-libknc-gate.sh` builds the reference as the stack's
`card/lib/knc/build.sh` does: `knc-mvex-gen codec`, `fastlanes` and
`memcpy`, with `knc-mvex` built from the stack's current source into
`$llvm_build/knc-mvex-target` (the stack's own build directory is not
touched). The candidate is `knc.c` from `tests/s3/knc-c-gen.c` plus
`tests/s3/vpu_memcpy.c`; the exported symbols must be the same as the
library's. The gate binary is linked static, not PIE, as the stack links
libknc: its dispatch tables are absolute `.quad` entries in `.rodata`.

Kernels, rules and results: [tests/s3/README.md](../tests/s3/README.md).
