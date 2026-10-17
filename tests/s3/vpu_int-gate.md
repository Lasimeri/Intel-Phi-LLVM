# tests/s3/vpu_int-gate.c

The S3 gate for `vpu_int`: links the intrinsic version and the stack's
`.byte` version (symbols prefixed `ref_`), compares their output on the
same inputs and times them interleaved. Run by `scripts/s3-gate.sh vpu_int`;
results in [README.md](README.md).
