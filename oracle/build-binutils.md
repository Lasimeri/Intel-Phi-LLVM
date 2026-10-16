# oracle/build-binutils.sh

Builds the MVEX encoding oracle: `gas` (`as-new`) and `objdump` from Intel's
`binutils-2.22+mpss3.8.6`, the only free tool that ever encoded Knights
Corner's vector instructions. Upstream binutils removed K1OM in 2022.

- **Input:** `mpss-src-3.8.6.tar` from the stack's `vendor/mpss-3.8.6/`
  (archive.org item `intel-mpss-3.8.6`), or `PHI_MPSS_SRC_TAR`.
- **Verification:** the outer tar and the inner
  `binutils-2.22+mpss3.8.6.tar.bz2` are both checked against
  `oracle/SHA256SUMS` before anything is extracted or built. A mismatch
  stops the script.
- **Build:** under `~/.cache/intel-phi-llvm-build/oracle/`, as an ordinary
  user, with no install step. Nothing is put on `PATH`.
- **Flags:** `-fcommon -std=gnu89` and four `-Wno-*` let 2011 C build with
  a 2026 gcc. `MAKEINFO=true` skips the texinfo manual, which is the only
  part that fails (measured 2026-10-15: `binutils.info` Error 1).
- **Licence:** GPL-3. It is run as a black box only. No table, test or line
  from it is copied into `patches/`, which are Apache-2.0 WITH
  LLVM-exception. The instruction definitions come from Intel's ISA
  reference 327364.

Measured 2026-10-15 on the rack: both tools build, at 1,969,656 B and
2,600,208 B.
