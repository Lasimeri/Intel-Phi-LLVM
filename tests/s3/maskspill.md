# tests/s3/maskspill.c

A self-checking card test of the code paths that keep a mask in a k
register, which the kernel gates do not reach:

- `loop_call`: a mask carried around a loop whose body calls a function.
  The call clobbers k0 to k7, so the mask is spilled before it and reloaded
  after it (`KNC_SPILL_K`, `KNC_RELOAD_K` in patch 0016).
- `many`: twelve masks computed first, live across a call and used after,
  each spilled and reloaded; more than the seven
  allocatable k registers.

Every lane is checked against the same computation in scalar C. The program
prints `maskspill: PASS (N checks, 0 wrong)` and exits 0, or `FAIL` and
exits 1.

    clang --config=config/knc.cfg --sysroot=SYSROOT -O2 -o maskspill tests/s3/maskspill.c
