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
| `extract-ops.awk` | Reads the same text and writes `knc-ops.txt`: the data flow of each instruction's Operation section |
| `knc-ops.txt` | That extraction, committed and reproduced by `scripts/isa-extract.sh` |

`tools/knc-gen.c` turns `knc-isa.txt` and `knc-overrides.txt` into `X86InstrKNC.td` and
`X86KNCInfo.inc` of patch 0013, and with `knc-ops.txt` into the code generation
files of patches 0016 and 0017 (`scripts/check-knc-gen.sh` checks them all).

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

The code generation overrides (S3) correct what `extract-ops.awk` reads
from an Operation section, each with the manual's line as evidence:
`untied MNEMONIC` (the destination read is another form's, the store's),
`tied MNEMONIC` (the destination keeps what the instruction does not
write, as `vloadunpackh*` does), `blend MNEMONIC` (the mask selects
between two sources and every element is written).

## knc-ops.txt

One record a line: `O|line|mnemonic|dest|reads-dest|writes`, one per
instruction page. `dest` is the first operand of the page's first opcode
row, `reads-dest` is 1 when the destination is also a source, and
`writes` lists the vector and mask registers the page assigns to. The
destination counts as read when it appears on the right of an assignment
or in a condition other than the write mask, when the element loop stops
writing early (a `break` line, not the `break;` of a switch case), or when
a field "remains unchanged"; a read after the whole register is assigned
(`DEST = 0`) does not count. The scalar pages call the destination `DEST`.
`tools/knc-gen.c --codegen` ties such a destination to its result and
does not yet generate the forms that write a mask beside their result
(`vaddsetsps`, `vpadcd` and the other carry and borrow forms).
