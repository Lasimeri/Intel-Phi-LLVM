# oracle/knc-sweep.sh

`k1omni.s` holds the forms Intel chose to test, with few registers. The
sweep takes every one of the 330 defs through Intel's K1OM gas.

```
LLVM_MC=~/.cache/intel-phi-llvm-build/llvm/bin/llvm-mc oracle/knc-sweep.sh
```

1. `tools/knc-gen.c` writes `sweep.s` (its sixth argument). Each def gets
   a `# def NAME` line and then these lines:
   - six with zmm 1, 7, 8, 15, 16 and 31 rotated through the reg, vvvv,
     r/m and VSIB-index roles;
   - k1 to k7, and eight general registers including r8 to r15;
   - seven memory forms (rsp, rbp, r12, r13, r8 and r15 bases, a SIB, a
     disp8 and a disp32) and four VSIB forms;
   - one line for each decoration of its tables and each rounding mode;
   - `{eh}`.

   One line in three has no write mask.
2. Gas assembles it; the lines it rejects are commented out and counted,
   and gas assembles the rest again.
3. Each remaining line goes through `llvm-mc -show-encoding` and is
   compared with gas's bytes (`k1omni-sections.awk`, one section per def).
   0 differ and 0 rejected are required.
4. **Acceptance:** each line gas rejects is given to `llvm-mc` alone. It
   must reject it too, except `%ymm16` to `%ymm31`: gas 2.22 has no such
   names, but they are the low halves of zmm16 to zmm31.

Measured 2026-10-16: 3,847 instructions identical; all 330 defs with
every line identical. Gas rejected 101 lines, and `llvm-mc` accepts only
the 6 `%ymm16` to `%ymm31` ones. Outputs are in
`~/.cache/intel-phi-llvm-build/oracle/sweep/`.
