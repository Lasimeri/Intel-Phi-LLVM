# Contributing

- **Patches:** `git format-patch` output against the pinned commit, named
  `NNNN-<summary>.patch`, and listed in `patches/SERIES` in order. Each
  patch's message says what the source showed on inspection and how the
  change was checked. A row in `patches/README.md` per patch.
- **Rebase:**
  - Change the `# LLVM tag:` and `# LLVM commit:` lines of `SERIES`
    together, taking the commit from `git ls-remote` of the peeled tag
    (`refs/tags/<tag>^{}`).
  - Re-export every patch.
  - Then `scripts/build.sh all`.
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
