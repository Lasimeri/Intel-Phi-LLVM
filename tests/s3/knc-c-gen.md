# tests/s3/knc-c-gen.c

Writes `knc.c`: Intel-Phi-3120A's libknc (`card/lib/knc`, `knc.h`) in C
with the Knights Corner intrinsics. A generator, as `knc-mvex-gen` is for
the `.byte` version, because a shift count must be a constant expression:
each output position of an unpack, each word of a pack and each FastLanes
position is written out with its numbers.

```
cc -O2 -o knc-c-gen tests/s3/knc-c-gen.c && ./knc-c-gen > knc.c
```

| Family | Per width | What a position or word does |
| --- | --- | --- |
| `knc_unpack_bB` (1 to 32), `_for_bB`, `_delta_bB` | 64 positions | the word shifted right, ORed with the next word shifted left where the value crosses a word, ANDed with the mask unless the shift already leaves B bits; FOR adds the base vector, delta keeps a running sum per lane |
| `knc_pack_bB` | 2B words | the masked values that start in the word, shifted left, ORed with the part of the one that crosses into it; the first one starts the word (no zeroing, no mask at 32 bits) |
| `knc_fls_unffor_bB` (0 to 32) | 32 positions, two halves of 16 lanes | the same on the FastLanes layout (rows of 32 words), the input read with `vloadunpackld`/`hd` pairs because it is only 4-byte aligned; plus the scalar base |
| `knc_encode_for`, `knc_encode_delta` | 64 vectors | subtract the base, or the vector before |

It also writes the five dispatch tables and functions (a width outside the
range touches nothing, as in the original). `knc_memcpy64` and the two
clears are `vpu_memcpy.c`. Results: [README.md](README.md).
