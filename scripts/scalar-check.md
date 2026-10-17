# scripts/scalar-check.sh

What S3's changes to the `knc` CPU do to scalar code, on a card: the
in-order scheduling model (`X86ScheduleKNC.td`), the `sahf` feature, and
`-falign-loops=1` in `config/knc.cfg`.

```
REF_BIN=~/.cache/intel-phi-llvm-build/ref-s2/bin scripts/scalar-check.sh [CARD] [ROUNDS]
```

Three of Intel-Phi-3120A's scalar programs are built by the compiler
before S3 (`REF_BIN`, with `knc.cfg` minus `-falign-loops`) and by this
one, and run in alternating order: `pi-bbp 3000 1` (x87 sums), `mandel
/dev/null 512 384 512 1` (scalar x87 iteration, then zlib deflate) and
`membw 64 1 3` (one thread of scalar loads). Each program times itself
(the card's `/proc/uptime` has 10 ms steps).

Results: [docs/results/2026-10-16-s3-intrinsics.md](../docs/results/2026-10-16-s3-intrinsics.md).
