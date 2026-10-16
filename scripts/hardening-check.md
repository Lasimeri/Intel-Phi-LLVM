# scripts/hardening-check.sh

Checks the defaults in `config/knc.cfg` on the card:
`scripts/hardening-check.sh [CARD]` (default 0). Every test program is
built with that file and nothing else (plus the sysroot and `-O2`), so it
tests what users get.

| Program | Binary inspection | Run on the card |
| --- | --- | --- |
| `ssp`: `strcpy` into a 16-byte buffer | `__stack_chk_fail` linked; canary load from `%fs:0x28` in `copy()` | short input exits 0; 64 bytes is stopped (musl's `__stack_chk_fail` crashes deliberately), so the exit must not be 0 |
| `clash`: a 1 MiB frame written every 4 KiB through `volatile` | a stack probe in `big()` | exits 0 |
| `pie`: prints the addresses of `main`, a static and a local | ELF type DYN, no `PT_INTERP`, `PT_GNU_RELRO`, `BIND_NOW`, `GNU_STACK` without E | two runs print two different addresses |
| `cxx`: the C++ exception test from `runtimes.sh` | | exits 0 |

All four are checked for static PIE and audited by `phi-isa-audit`. The
card's `/proc/cpuinfo` `nx` flag and `randomize_va_space` are printed.

The overflow test overflows only its own buffer, in a program built for
the test, to show the protection stops it on this hardware.

A first version of `clash` used `memset` on a `volatile` array; the
optimizer removed the array, so there was nothing to probe. The current
version writes through `volatile`.
