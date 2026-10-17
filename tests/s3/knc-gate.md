# tests/s3/knc-gate.c

The S3 gate for libknc: the stack's library (symbols prefixed `ref_`) and
`knc.c` from `knc-c-gen` on the same inputs. Output: 40 inputs (all
zeros, all ones, random) through every width of unpack, FOR, delta and
pack, FastLanes `unffor` at widths 0 to 32 and four misalignments, both
encodes, and widths outside the range (which must leave memory as it
was). Speed: all 163 kernels, 2,000 calls a round, 7 rounds interleaved.
Run by `scripts/s3-libknc-gate.sh`; results in [README.md](README.md).
