# tests/s3/mandel_vpu-gate.c

The S3 gate for `mandel_vpu`: links the intrinsic version and the stack's
`.byte` version (symbols prefixed `ref_`), compares their output on the
same inputs and times them interleaved. Run by `scripts/s3-gate.sh mandel_vpu`;
results in [README.md](README.md).
