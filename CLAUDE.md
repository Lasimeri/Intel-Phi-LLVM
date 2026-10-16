# Intel-Phi-LLVM: notes for an agent working here

Read `CONTRIBUTING.md` and `SECURITY.md` first; they are the authority.

- What this is: the KNC (Knights Corner, Xeon Phi 3120A) target in LLVM,
  the vector unit included. The plan and its gates are in `docs/plan.md`.
  The decisions open for the person are D1 to D3 in
  `docs/decisions/0001-knc-feature-outside-sse-ladder.md`.
- Everything runs on the GPU rack: builds (`-j128`), oracle runs, card
  gates. The working copy is `/mnt/raid5/phi/Intel Phi LLVM` (alias
  `~/.cache/intel-phi-llvm`); a desktop clone only relays pushes.
- Defensive security only. This is a compiler for hardware the person
  owns. Pinned and verified inputs, no root, and no network outside
  `build.sh fetch`.
- Never Python, Perl or JavaScript, not even for a one-line edit. LLVM's
  lit and update scripts are Python, so tests are `llvm-mc`/`llc` output
  compared by shell, awk or C (tcc).
- MPSS binutils and gcc are GPL: run them, never copy from them.
  Instruction definitions come from Intel ISA reference 327364
  (`vendor/docs/` in the stack).
- Every code file has a sibling `.md`. No em or en dashes. Every hardware
  claim cites a source or a measurement with its command.
- Card gates take a card out of production (llama.phi on :8001 uses all
  four). Ask first.
