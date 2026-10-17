# scripts/isa-extract.sh

Reproduces `isa/knc-isa.txt` and `isa/knc-ops.txt` from the manual:

1. checks `$PHI_STACK_ROOT/vendor/docs/isa-reference-327364-001.pdf`
   against its SHA-256 and refuses another file;
2. `pdftotext -layout` into the build root;
3. `isa/extract-isa.awk` (the opcode and conversion tables) and
   `isa/extract-ops.awk` (the data flow of each Operation section);
4. compares both with the committed copies.

Measured 2026-10-16 with pdftotext 26.08.0: both reproduced, 804 and 192
records. Another pdftotext may lay the text out differently; the
committed copies are what the generator reads, so building never depends
on it.
