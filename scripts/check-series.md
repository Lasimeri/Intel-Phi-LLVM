# scripts/check-series.sh

Part of `make check`. Checks `patches/SERIES` without a source tree:

- a `# LLVM tag: llvmorg-X.Y.Z` line;
- a `# LLVM commit:` line with 40 hex digits (the peeled tag commit that
  `scripts/build.sh` verifies the clone against);
- every listed patch exists;
- every `patches/*.patch` is listed exactly once.

Whether the patches apply is checked by `scripts/build.sh patch`, which
needs the verified source tree.
