/* bitunpack.c: Intel-Phi-3120A's card/examples/bitunpack.S (knc-mvex .byte
 * lines) written with the Knights Corner intrinsics. See README.md.
 *
 * void knc_unpack_bB(int *out, const unsigned *packed), B = 5, 11, 16:
 * 1024 values of B bits, value i in lane i % 16 at position i / 16; lane
 * l's bit stream is the 32-bit words packed[16 w + l]. Output position p
 * is bits [p B, p B + B) of each lane's stream: one word shifted right,
 * ORed with the next word shifted left where it crosses a word, and ANDed
 * with the B-bit mask unless the shift already leaves exactly B bits. The
 * 64 positions are written out by the preprocessor, because a shift count
 * must be a constant expression. out and packed do not overlap (restrict),
 * as the .byte version assumes, so the loads may move above the stores. */
#include <immintrin.h>

#define R8(M, b) M(b + 0) M(b + 1) M(b + 2) M(b + 3) M(b + 4) M(b + 5) M(b + 6) M(b + 7)
#define R64(M) R8(M, 0) R8(M, 8) R8(M, 16) R8(M, 24) R8(M, 32) R8(M, 40) R8(M, 48) R8(M, 56)

#define POS(p)                                                                                     \
	{                                                                                          \
		__m512i v = _mm512_load_epi32(packed + 16 * ((p) * B / 32));                       \
		if ((p) * B % 32)                                                                   \
			v = _mm512_srli_epi32(v, (p) * B % 32);                                     \
		if ((p) * B % 32 + B > 32)                                                          \
			v = _mm512_or_epi32(v, _mm512_slli_epi32(                                   \
			        _mm512_load_epi32(packed + 16 * ((p) * B / 32 + 1)), 32 - (p) * B % 32)); \
		if ((p) * B % 32 + B != 32)                                                         \
			v = _mm512_and_epi32(v, mask);                                              \
		_mm512_store_epi32(out + 16 * (p), v);                                              \
	}

#define B 5
void knc_unpack_b5(int *restrict out, const unsigned *restrict packed)
{
	const __m512i mask = _mm512_set1_epi32((1 << B) - 1);
	R64(POS)
}
#undef B
#define B 11
void knc_unpack_b11(int *restrict out, const unsigned *restrict packed)
{
	const __m512i mask = _mm512_set1_epi32((1 << B) - 1);
	R64(POS)
}
#undef B
#define B 16
void knc_unpack_b16(int *restrict out, const unsigned *restrict packed)
{
	const __m512i mask = _mm512_set1_epi32((1 << B) - 1);
	R64(POS)
}
#undef B
