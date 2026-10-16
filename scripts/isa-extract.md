# scripts/isa-extract.sh

Reproduces `isa/knc-isa.txt` from the manual:

1. checks `$PHI_STACK_ROOT/vendor/docs/isa-reference-327364-001.pdf`
   against its SHA-256 and refuses another file;
2. `pdftotext -layout` into the build root;
3. `isa/extract-isa.awk`;
4. compares with the committed `isa/knc-isa.txt`.

Measured 2026-10-16 with pdftotext 26.08.0: reproduced, 804 records.
Another pdftotext may lay the text out differently; the committed copy is
what the generator reads, so building never depends on it.
