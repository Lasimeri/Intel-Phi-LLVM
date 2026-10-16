# oracle/k1omni-check.sh

The MVEX encoding gate (stage S2, `docs/plan.md`).

1. It assembles Intel's own K1OM vector test, `gas/testsuite/gas/i386/k1omni.s`
   (6,708 instructions), with the oracle's `as-new -march=k1om`. Without
   `-march=k1om` every vector line is rejected.
2. It compares the oracle's `objdump -dw` bytes with the expected
   `k1omni.d`. Measured 2026-10-15: 6,708 instructions, 4 byte mismatches.
   All four are `vpadcd 0x99(%rip),...` lines whose expected pattern this
   field split does not parse (a parsing gap in the comparison, not a
   wrong encoding).
3. With `LLVM_MC=/path/to/llvm-mc` it also assembles the file with
   `-mcpu=knc`, disassembles it with the oracle's `objdump` and compares the
   bytes line for line. The S2 gate is 0 mismatches. The exit status is 1
   on any mismatch.

Parsing note: the `.d` lines begin with the pattern `[ <TAB>]*`, so after
splitting on tabs the bytes are field 3 and the text field 4. In objdump's
own output they are fields 2 and 3.

Outputs: `~/.cache/intel-phi-llvm-build/oracle/check/`.
