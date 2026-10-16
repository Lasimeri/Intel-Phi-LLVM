# scripts/mvex-card-check.sh

The hardware check of S2, on card `CARD` (default 0):

1. `tests/mvex-probe.s` is assembled by this repository's clang with
   `config/knc.cfg` (so `-march=knc` and the integrated assembler) and by
   Intel's K1OM gas from `oracle/` (`as-new -march=k1om`); the two `.text`
   sections must be identical;
2. the clang object is linked with `tests/mvex-probe.c` (static PIE, the
   hardening defaults) and audited with the stack's `phi-isa-audit`;
3. it runs on the card through `phi`, and must print
   `mvex-probe: PASS` and exit 0.

Measured 2026-10-16 on card 0: 474 bytes, 90 instructions, identical to
gas; audit 0 illegal, 64 KNC vector; 516 checks, 0 wrong, in 3 of 3 runs.
