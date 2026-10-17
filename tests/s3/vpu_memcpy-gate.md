# tests/s3/vpu_memcpy-gate.c

The S3 gate for `vpu_memcpy`: links the intrinsic version and the stack's
`.byte` version (symbols prefixed `ref_`), compares their output on the
same inputs and times them interleaved. Run by `scripts/s3-gate.sh vpu_memcpy`;
results in [README.md](README.md).
