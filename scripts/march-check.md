# scripts/march-check.sh

The S1 gate: `-march=knc` must mean exactly what the stack's `knc-cc`
spelled out flag by flag.

1. **Macros:** `-march=knc` predefines `__MIC__ 1`, `__KNC__ 1`, `__knc`,
   `__knc__` and `__tune_knc__`, and no `__SSE*` or `__MMX__`.
2. **Backend alone:** `llc -mcpu=knc`, with no `-mattr`, on a `double`
   multiply and an `i64` select gives x87 code (`fldl`, `fmull`) and a
   branch, with no `xmm` register and no `cmov`.
3. **Objects:** each source is compiled twice with the same clang:
   - once with `knc-cc`'s flags (`-march=x86-64` and 31 `-mno-*`), plus
     `-msahf -mtune=knc` since S3, which gave the `knc` CPU `sahf` and its
     own scheduling model;
   - once with `-march=knc`.

   Both use `-fcf-protection=none -fno-jump-tables`. The corpus is musl
   1.2.5's generic and x86_64 sources with musl's own CFLAGS, plus the
   stack's `card/examples/*.c` and `toolchain/check/hello.c`. Every pair
   must be byte-identical (`cmp`), and any compile failure must be the
   same on both sides.

Measured 2026-10-15 on the rack: 1,359 sources, 1,359 identical, 6.7 s at
`-j128`.

The first run, before the tuning fix, had 271 differences, all
`decl`/`incl` against `addl $-1`. `X86TargetMachine::getSubtargetImpl`
tunes CPU "x86-64" as "generic", so the `knc` CPU takes `generic`'s
tuning list (patch 0011).

Since S3 each compile runs under a 300 s limit, so a hang fails the gate.
The S3 build first crashed on 19 musl complex-float sources and then
differed in one (`fstatat`); both came from the newly legal 512-bit types
reaching scalar code, and both are fixed in patch 0016
(docs/results/2026-10-16-s3-intrinsics.md).
