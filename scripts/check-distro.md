# scripts/check-distro.sh

Decision D1 (`docs/decisions/0001-knc-feature-outside-sse-ladder.md`):
the series follows the LLVM that Arch ships, so that the distro rustc
(and distro tools on Arch and Arch-like systems) can load the patched
library.

This script compares three versions:

- the tag in `patches/SERIES`;
- `pacman -Q llvm-libs`, with the package release suffix removed;
- the LLVM version printed by `/usr/bin/rustc -vV`.

When Arch has moved, it exits 1 with "DRIFT". On a system without pacman
it reports and exits 0. `make check` runs it.

Measured 2026-10-15 on the rack: pinned 23.1.1, llvm-libs 23.1.1, rustc's
LLVM 23.1.1.
