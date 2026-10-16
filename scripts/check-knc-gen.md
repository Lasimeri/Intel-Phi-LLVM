# scripts/check-knc-gen.sh

Builds `tools/knc-gen.c` with `CC` (default `cc`) under the build root,
runs it on `isa/knc-isa.txt` and `isa/knc-overrides.txt`, and compares
its two outputs with the copies inside
`patches/0013-x86-knc-mvex-instruction-table-and-encoder.patch` (the `+`
lines of each new file). A difference prints the first 20 lines of the
diff and exits 1. Part of `make check`.

A change to the table goes: edit `isa/` or `tools/knc-gen.c`, regenerate
into the patch worktree, rebuild, run the gates, export patch 0013 again.
