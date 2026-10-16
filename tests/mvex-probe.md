# tests/mvex-probe.s, tests/mvex-probe.c

A self-checking program for a card. `mvex-probe.s` holds eleven
functions in the K1OM AT&T syntax: register swizzles, broadcasts, load-op
up-conversions (`{uint8}` `{sint8}` `{uint16}` `{sint16}` `{float16}`),
store down-conversions, merge masking, a gather and a scatter loop, pack
and unpack under a mask, static rounding, a compare into a mask register,
and the KNC VEX `popcnt`, `lzcnt` and `tzcnt`. `mvex-probe.c` computes each
expected lane in scalar code from the manual's definitions and prints
`right of total` per test, then `mvex-probe: PASS` or `FAIL`.

The probe checks what the hardware does with the bytes the assembler
writes. Agreement with gas is checked separately by
`scripts/mvex-card-check.sh`. Every vector pointer is 64-byte aligned, as
the instructions require.

The `{float16}` test adds the converted value to +0.0, so -0.0 comes out
as +0.0 (round to nearest); the expected values say so.
