# tests/s3/bitunpack-gate.c

The S3 gate for `bitunpack`: links the intrinsic version and the stack's
`.byte` version (symbols prefixed `ref_`), compares their output on the
same inputs and times them interleaved. Run by `scripts/s3-gate.sh bitunpack`;
results in [README.md](README.md).
