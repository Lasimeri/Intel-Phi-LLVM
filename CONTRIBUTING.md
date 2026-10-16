# Contributing

- **Patches:** plain `git diff` output, one per step, against the pinned
  commit, named `NNNN-<summary>.patch`, and listed in `patches/SERIES` in
  order. The reason for each patch, and how it was checked, goes in a row
  of `patches/README.md` and in the commit that adds it.
- **Rebase:**
  - Change the `# LLVM tag:` and `# LLVM commit:` lines of `SERIES`
    together, taking the commit from `git ls-remote` of the peeled tag
    (`refs/tags/<tag>^{}`).
  - Re-export every patch: in a worktree at the new tag, `git apply` each
    patch in order, write `git diff` to the patch file, then commit, so
    each diff is against the previous step.
  - Then run the S0 gate: `scripts/build.sh all`, `PHI_LLVM_VARIANT=dylib
    scripts/build.sh all`, `scripts/runtimes.sh`, `scripts/rust-check.sh`,
    `PHI_LLVM_VARIANT=card scripts/build.sh configure`, then `build`,
    `scripts/card-package.sh`, `scripts/card-check.sh`.
  - `scripts/check-distro.sh` (in `make check`) reports when Arch moves.
- **Checks:**
  - `make check` must pass before every commit, and a failure stops the
    commit.
  - Build gates (`scripts/build.sh check`, `oracle/k1omni-check.sh`) are
    recorded with their numbers in `docs/results/`.
- **Languages:** C++ and TableGen inside the patches; shell, awk and C
  (tcc) for tools; never Python, Perl or JavaScript.
- **Docs:** every code file has a sibling `.md`. No em or en dashes.
  Relative links must resolve (`scripts/check-docs.sh`).
- **Commits:** plain messages, with no co-author or generated-by lines.
