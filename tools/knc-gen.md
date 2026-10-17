# tools/knc-gen.c

Generates the two machine-made files of patch 0013 from `isa/`:

```
cc -O2 -o knc-gen tools/knc-gen.c
knc-gen isa/knc-isa.txt isa/knc-overrides.txt X86InstrKNC.td X86KNCInfo.inc [listing]
```

- **`X86InstrKNC.td`:** one def per form: 330 from 217 opcode rows, two
  for a "register or memory" source. A def carries the opcode, map, prefix,
  W, the ModRM form, `EVEX_K` and `VVVV`, and `isMVEX`. Its operands are in
  the order the code emitter reads them (reg, mask, vvvv, r/m, imm, then
  the `mvex_sss` operand holding MVEX.E << 3 | SSS). All are
  `isCodeGenOnly`; class `KNCEnc` marks them for the TableGen backends that
  must skip them.
- **`X86KNCInfo.inc`:** the table the assembler, the printers, the encoder
  and the disassembler read. Per form: mnemonic, encoding fields, the
  operands in Intel order (kind, encoding role, MCInst index, flags for
  mask, conversion, `{eh}` and ymm half sources), and indexes into 25
  decoration tables (the spelling and disp8*N of each SSS value). Then the
  pseudo-ops.
- **Listing:** with a fifth argument, one line per def and table, for
  review.

Rules it applies:

- every vector opcode has a ModRM byte, even where the manual drops `/r`;
- a store's register carries the down-conversion, its memory the `{eh}`;
- `vprefetch*` and `clevict*` take disp8*64;
- a `zmm/mt` source without a table takes no decoration and N = 64;
- a load-op whose memory form reads eight 32-bit elements (N = 32 for
  SSS 000) reads the low half of its register, written ymm.

`scripts/check-knc-gen.sh` (part of `make check`) runs it and compares the
output with patch 0013 byte for byte.

## Code generation (S3)

```
knc-gen --codegen isa/knc-isa.txt isa/knc-overrides.txt isa/knc-ops.txt OUTDIR
```

From the same defs, for every form that can be generated (297 of 330;
`OUTDIR/codegen-report.txt` lists the others and why):

| File | Patch | What |
| --- | --- | --- |
| `IntrinsicsX86KNC.td` | 0016 | `llvm.x86.knc.<form>`: [pass-through, or the destination when the instruction reads it], [i16 write mask, -1 = none], the operands in Intel order, [MVEX.E << 3 \| SSS]. Memory forms carry `SDNPMemOperand`; an 8-element compare returns `range(i16 0, 256)` |
| `X86InstrKNCIntrin.td` | 0016 | per form a pseudo for each variant (`_U` unmasked, `_M` masked, `_P` without a mask), with a read destination or a merge pass-through tied to the result, and its pattern; then the folded loads: a register form fed by an aligned load with no decoration becomes the memory form |
| `X86KNCPseudos.inc` | 0016 | for each pseudo, its MC def and, per MC operand, the pseudo operand it comes from (-1 = k0) |
| `X86KNCPassThru.inc` | 0016 | the intrinsics with a merge pass-through (an all-ones mask makes it undef) |
| `X86KNCMemIntrinsics.inc` | 0016 | the memory each memory form touches, for `getTgtMemIntrinsic` |
| `BuiltinsX86KNC.td` | 0017 | `__builtin_ia32_knc_<form>`, which clang maps to the intrinsic by name |
| `SemaX86KNC.inc` | 0017 | per builtin, which immediates are imm8 and which decoration values the form's tables allow |

Rules: the element type of a vector operand is its conversion class, or
the mnemonic's (a conversion's destination is what follows the `2`; the
`vp` integer forms end in `d` or `q`; `vloadunpack` and `vpackstore` in
their element type); the scheduling class follows the kind (store, load,
conversion, shuffle, other vector, mask and scalar), priced by the knc
model. Gathers and scatters (they repeat until their mask is clear) and
the forms that write a mask beside their result are not generated yet.
