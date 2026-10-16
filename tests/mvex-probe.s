# mvex-probe.s: Knights Corner vector instructions for tests/mvex-probe.c, in
# the K1OM AT&T syntax. scripts/mvex-card-check.sh assembles this file with
# clang (-march=knc, LLVM's integrated assembler) and with Intel's K1OM gas,
# requires the same .text bytes, links the clang object with the C harness
# and runs it on a card. Every pointer argument is 64-byte aligned. SysV
# arguments: rdi, rsi, rdx, rcx. See mvex-probe.md.

	.text

# knc_swizzle(const int in[16], int out[8][16]): out[s] = in under register
# swizzle s ({dcba} {cdab} {badc} {dacb} {aaaa} {bbbb} {cccc} {dddd}),
# added to zero.
	.globl	knc_swizzle
	.type	knc_swizzle,@function
knc_swizzle:
	vmovdqa32 (%rdi),%zmm0
	vpxord	%zmm1,%zmm1,%zmm1
	vpaddd	%zmm0{dcba},%zmm1,%zmm2
	vmovdqa32 %zmm2,(%rsi)
	vpaddd	%zmm0{cdab},%zmm1,%zmm2
	vmovdqa32 %zmm2,0x40(%rsi)
	vpaddd	%zmm0{badc},%zmm1,%zmm2
	vmovdqa32 %zmm2,0x80(%rsi)
	vpaddd	%zmm0{dacb},%zmm1,%zmm2
	vmovdqa32 %zmm2,0xc0(%rsi)
	vpaddd	%zmm0{aaaa},%zmm1,%zmm2
	vmovdqa32 %zmm2,0x100(%rsi)
	vpaddd	%zmm0{bbbb},%zmm1,%zmm2
	vmovdqa32 %zmm2,0x140(%rsi)
	vpaddd	%zmm0{cccc},%zmm1,%zmm2
	vmovdqa32 %zmm2,0x180(%rsi)
	vpaddd	%zmm0{dddd},%zmm1,%zmm2
	vmovdqa32 %zmm2,0x1c0(%rsi)
	ret
	.size	knc_swizzle,.-knc_swizzle

# knc_broadcast(const int in[16], int out[2][16]): out[0] = in[0] x16
# ({1to16}), out[1] = in[0..3] x4 ({4to16}); the int32 load-op form.
	.globl	knc_broadcast
	.type	knc_broadcast,@function
knc_broadcast:
	vpxord	%zmm1,%zmm1,%zmm1
	vpaddd	(%rdi){1to16},%zmm1,%zmm2
	vmovdqa32 %zmm2,(%rsi)
	vpaddd	(%rdi){4to16},%zmm1,%zmm2
	vmovdqa32 %zmm2,0x40(%rsi)
	ret
	.size	knc_broadcast,.-knc_broadcast

# knc_upconv(const void *in, int out[4][16], float half[16]): the load-op
# up-conversions of 16 bytes ({uint8} {sint8}) and 16 words ({uint16}
# {sint16}) at in (bytes at +0, words at +64), and {float16} of the words
# at +128 into float32 (vaddps to zero).
	.globl	knc_upconv
	.type	knc_upconv,@function
knc_upconv:
	vpxord	%zmm1,%zmm1,%zmm1
	vpaddd	(%rdi){uint8},%zmm1,%zmm2
	vmovdqa32 %zmm2,(%rsi)
	vpaddd	(%rdi){sint8},%zmm1,%zmm2
	vmovdqa32 %zmm2,0x40(%rsi)
	vpaddd	0x40(%rdi){uint16},%zmm1,%zmm2
	vmovdqa32 %zmm2,0x80(%rsi)
	vpaddd	0x40(%rdi){sint16},%zmm1,%zmm2
	vmovdqa32 %zmm2,0xc0(%rsi)
	vaddps	0x80(%rdi){float16},%zmm1,%zmm2
	vmovaps	%zmm2,(%rdx)
	ret
	.size	knc_upconv,.-knc_upconv

# knc_downconv(const int in[16], void *out): the store down-conversions
# {uint8} to out+0 (16 bytes) and {sint16} to out+64 (32 bytes).
	.globl	knc_downconv
	.type	knc_downconv,@function
knc_downconv:
	vmovdqa32 (%rdi),%zmm0
	vmovdqa32 %zmm0{uint8},(%rsi)
	vmovdqa32 %zmm0{sint16},0x40(%rsi)
	ret
	.size	knc_downconv,.-knc_downconv

# knc_mask(const int a[16], const int b[16], int out[16], unsigned mask):
# out = b, then out += a in the lanes of mask (merge masking).
	.globl	knc_mask
	.type	knc_mask,@function
knc_mask:
	kmov	%ecx,%k1
	vmovdqa32 (%rsi),%zmm2
	vpaddd	(%rdi),%zmm2,%zmm2{%k1}
	vmovdqa32 %zmm2,(%rdx)
	ret
	.size	knc_mask,.-knc_mask

# knc_gather(const int *table, const int idx[16], int out[16]): out[i] =
# table[idx[i]]; vpgatherdd clears the mask bits of the lanes it has done,
# so it runs until k1 is zero.
	.globl	knc_gather
	.type	knc_gather,@function
knc_gather:
	vmovdqa32 (%rsi),%zmm1
	vpxord	%zmm2,%zmm2,%zmm2
	kxnor	%k1,%k1
1:	vpgatherdd (%rdi,%zmm1,4),%zmm2{%k1}
	kortest	%k1,%k1
	jnz	1b
	vmovdqa32 %zmm2,(%rdx)
	ret
	.size	knc_gather,.-knc_gather

# knc_scatter(int *table, const int idx[16], const int val[16]):
# table[idx[i]] = val[i], until k1 is zero.
	.globl	knc_scatter
	.type	knc_scatter,@function
knc_scatter:
	vmovdqa32 (%rsi),%zmm1
	vmovdqa32 (%rdx),%zmm2
	kxnor	%k1,%k1
1:	vpscatterdd %zmm2,(%rdi,%zmm1,4){%k1}
	kortest	%k1,%k1
	jnz	1b
	ret
	.size	knc_scatter,.-knc_scatter

# knc_pack(const int in[16], int packed[16], int unpacked[16], unsigned
# mask): vpackstoreld stores the lanes of mask contiguously at packed;
# vloadunpackld loads in[0], in[1], ... into the lanes of mask of a zero
# vector, stored at unpacked.
	.globl	knc_pack
	.type	knc_pack,@function
knc_pack:
	kmov	%ecx,%k1
	vmovdqa32 (%rdi),%zmm0
	vpackstoreld %zmm0,(%rsi){%k1}
	vpxord	%zmm3,%zmm3,%zmm3
	vloadunpackld (%rdi),%zmm3{%k1}
	vmovdqa32 %zmm3,(%rdx)
	ret
	.size	knc_pack,.-knc_pack

# knc_round(const float a[16], const float b[16], float out[4][16]):
# a + b with the static roundings {rn-sae} {rd-sae} {ru-sae} {rz-sae}.
	.globl	knc_round
	.type	knc_round,@function
knc_round:
	vmovaps	(%rdi),%zmm0
	vmovaps	(%rsi),%zmm1
	vaddps	{rn-sae},%zmm1,%zmm0,%zmm2
	vmovaps	%zmm2,(%rdx)
	vaddps	{rd-sae},%zmm1,%zmm0,%zmm2
	vmovaps	%zmm2,0x40(%rdx)
	vaddps	{ru-sae},%zmm1,%zmm0,%zmm2
	vmovaps	%zmm2,0x80(%rdx)
	vaddps	{rz-sae},%zmm1,%zmm0,%zmm2
	vmovaps	%zmm2,0xc0(%rdx)
	ret
	.size	knc_round,.-knc_round

# knc_cmplt(const float a[16], const float b[16]) -> mask of a < b
# (the vcmpltps pseudo-op of vcmpps), through kmov to eax.
	.globl	knc_cmplt
	.type	knc_cmplt,@function
knc_cmplt:
	vmovaps	(%rdi),%zmm0
	vcmpltps (%rsi),%zmm0,%k2
	kmov	%k2,%eax
	ret
	.size	knc_cmplt,.-knc_cmplt

# knc_bits(unsigned long x, unsigned long out[3]): the KNC VEX forms of
# popcnt, lzcnt and tzcnt.
	.globl	knc_bits
	.type	knc_bits,@function
knc_bits:
	popcnt	%rdi,%rax
	movq	%rax,(%rsi)
	lzcnt	%rdi,%rax
	movq	%rax,8(%rsi)
	tzcnt	%rdi,%rax
	movq	%rax,16(%rsi)
	ret
	.size	knc_bits,.-knc_bits

	.section .note.GNU-stack,"",@progbits
