# isa/

The Knights Corner instruction table, taken from Intel's Xeon Phi
Coprocessor Instruction Set Architecture Reference Manual (327364-001,
in Intel-Phi-3120A's `vendor/docs`, SHA-256 pinned in
`scripts/isa-extract.sh`).

| File | What |
| --- | --- |
| `extract-isa.awk` | Reads `pdftotext -layout` output of the manual and writes `knc-isa.txt` |
| `knc-isa.txt` | The extraction, committed so building needs no PDF tools; `scripts/isa-extract.sh` reproduces it (pdftotext 26.08.0) |
| `knc-overrides.txt` | Where Intel's K1OM gas, the oracle, and the manual disagree; the oracle decides the bytes |

`tools/knc-gen.c` turns the two text files into `X86InstrKNC.td` and
`X86KNCInfo.inc` of patch 0013 (`scripts/check-knc-gen.sh` checks that).

## knc-isa.txt

One record a line, fields separated by `|`, the manual's text line second:

| Record | Fields | Meaning |
| --- | --- | --- |
| `B` | line | an "Opcode Instruction Description" table starts a section |
| `I` | line, opcode, instruction | one opcode row, e.g. `MVEX.NDS.512.0F.W0 58 /r` and `vaddps zmm1 {k1}, zmm2, Sf32(zmm3/mt)` |
| `T` | line, kind, class, rows | a conversion table of the section: kind `up` or `down` (memory, with disp8*N), `reg0` (register swizzles, MVEX.EH=0) or `reg1` (static rounding and SAE, MVEX.EH=1); rows `SSS:usage:N` |
| `P` | line, pseudo-op, base, predicate | an "Instruction Pseudo-ops" row (`vcmpeqps` = `vcmpps` with `{eq}`) |
| `C` | line, predicate, immediate | an "Immediate Format" row (`eq` = 0) |

The PDF drops the fi and fl ligatures ("loat" for float, "v ixupnanpd",
"sp lt"); `extract-isa.awk` puts them back. Only chapter 6 and the scalar
appendix (text lines 3,500 to 29,600) are read.

## knc-overrides.txt

`rounding MNEMONIC` gives a register form the full static rounding set;
`pseudo NAME BASE IMM|-` adds or replaces a pseudo-op. Each line carries
its evidence from the oracle. The cases the parser handles in code
(`{sae}` with a swizzle, `{eh}` on a register, ymm half sources, `ds:`,
the prefetch form choice) are listed in
[docs/results/2026-10-16-s2-mc-layer.md](../docs/results/2026-10-16-s2-mc-layer.md).
