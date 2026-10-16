# oracle/k1omni-check.sh

The MVEX encoding gate (stage S2, `docs/plan.md`).

1. It assembles Intel's own K1OM vector test, `gas/testsuite/gas/i386/k1omni.s`
   (6,708 instructions, an AT&T half and an `.intel_syntax noprefix`
   half), with the oracle's `as-new -march=k1om`. Without `-march=k1om`
   every vector line is rejected.
2. It compares the oracle's `objdump -dw` bytes with the expected
   `k1omni.d`. Measured 2026-10-15: 6,708 instructions, 4 byte mismatches.
   All four are `vpadcd 0x99(%rip),...` lines whose expected pattern this
   field split does not parse (a parsing gap in the comparison, not a
   wrong encoding).

With `LLVM_MC=/path/to/llvm-mc` (and `llvm-objdump`, `llvm-objcopy` beside
it) it runs the S2 gate on that build, all with `-mcpu=knc`:

3. **Bytes:** the whole file assembled, disassembled by the oracle's
   objdump, compared line for line; 0 mismatches required.
4. **Per section:** `-show-encoding`, each source line against the
   oracle's bytes (`k1omni-sections.awk`), one line per `# Tests for`
   section in `sections.txt`; 0 differ and 0 rejected required.
5. **Round trip:** `llvm-objdump --mcpu=knc` in AT&T and in Intel
   syntax, assembled again; `.text` identical required.
6. **Text:** `llvm-objdump --mcpu=knc` against the oracle's objdump on
   the same bytes, both in the canonical form of `disnorm.awk` (no
   whitespace, no comments, `,1)` and `0x0(` dropped, the wrapped 64-bit
   displacement negative, `{eh}` after the conversion, a lone rounding
   first). It is a display check, so known classes are counted and only
   an unknown one fails.

Measured 2026-10-16 on the series build: 6,708 of 6,708, 134 sections
clean, both round trips identical (43,862 bytes), text 6,660 of 6,708
with 36 + 8 + 4 in the three known classes. Exit status 1 on any failure.

Parsing note: the `.d` lines begin with the pattern `[ <TAB>]*`, so after
splitting on tabs the bytes are field 3 and the text field 4. In objdump's
own output they are fields 2 and 3.

Outputs: `~/.cache/intel-phi-llvm-build/oracle/check/`.
