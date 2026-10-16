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
