# scripts/check-knc-gen.sh

Builds `tools/knc-gen.c` with `CC` (default `cc`) under the build root,
runs it on `isa/`, and compares every file it generates with the copy
inside a patch (the `+` lines of each new file):

| Patch | Files | From |
| --- | --- | --- |
| 0013 | `X86InstrKNC.td`, `X86KNCInfo.inc` | `knc-isa.txt`, `knc-overrides.txt` |
| 0016 | `IntrinsicsX86KNC.td`, `X86InstrKNCIntrin.td`, `X86KNCPseudos.inc`, `X86KNCPassThru.inc`, `X86KNCMemIntrinsics.inc` | the same, and `knc-ops.txt` (`knc-gen --codegen`) |
| 0017 | `BuiltinsX86KNC.td`, `SemaX86KNC.inc` | the same |

A difference prints the first 20 lines of the diff and exits 1. Part of
`make check`.

A change to the table goes: edit `isa/` or `tools/knc-gen.c`, regenerate
into the patch worktree, rebuild, run the gates, export the patches again.
