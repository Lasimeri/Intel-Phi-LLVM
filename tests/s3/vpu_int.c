/* vpu_int.c: Intel-Phi-3120A's card/examples/vpu_int.S (knc-mvex .byte
 * lines) written with the Knights Corner intrinsics, one operation per
 * output slot as there. See README.md.
 *
 * void vpu_int_probe(const int *in, int *out): in = 16 int32 a, then 16 b,
 * both 64-byte aligned; out = 18 slots of 16 int32.
 * void vpu_int_unaligned(const int *un, int *out): un any multiple of 4;
 * out 64-byte aligned. */
#include <immintrin.h>

void vpu_int_probe(const int *in, int *out)
{
	const __m512i a = _mm512_load_epi32(in), b = _mm512_load_epi32(in + 16);
	_mm512_store_epi32(out + 0 * 16, _mm512_add_epi32(a, b));
	_mm512_store_epi32(out + 1 * 16, _mm512_sub_epi32(a, b));
	_mm512_store_epi32(out + 2 * 16, _mm512_and_epi32(a, b));
	_mm512_store_epi32(out + 3 * 16, _mm512_andnot_epi32(a, b));
	_mm512_store_epi32(out + 4 * 16, _mm512_or_epi32(a, b));
	_mm512_store_epi32(out + 5 * 16, _mm512_xor_epi32(a, b));
	_mm512_store_epi32(out + 6 * 16, _mm512_slli_epi32(a, 1));
	_mm512_store_epi32(out + 7 * 16, _mm512_slli_epi32(a, 11));
	_mm512_store_epi32(out + 8 * 16, _mm512_srli_epi32(a, 11));
	_mm512_store_epi32(out + 9 * 16, _mm512_srai_epi32(a, 11));
	_mm512_store_epi32(out + 10 * 16, _mm512_srai_epi32(a, 31));
	_mm512_store_epi32(out + 11 * 16, _mm512_slli_epi32(_mm512_load_epi32(in), 11));
	_mm512_store_epi32(out + 12 * 16, _mm512_sllv_epi32(a, b));
	_mm512_store_epi32(out + 13 * 16, _mm512_srlv_epi32(a, _mm512_load_epi32(in + 16)));
	/* merge masking: lanes off in the mask keep a reload of a */
	_mm512_store_epi32(out + 14 * 16, _mm512_mask_add_epi32(_mm512_load_epi32(in), 0x00ff, a, b));
	_mm512_store_epi32(out + 15 * 16, _mm512_mask_slli_epi32(_mm512_load_epi32(in), 0x0f0f, b, 3));
	/* broadcast of in[17], then a lo/hi unpack of in[0..15] over it */
	__m512i v = _mm512_extload_epi32(in + 17, _MM_UPCONV_EPI32_NONE, _MM_BROADCAST_1X16, _MM_HINT_NONE);
	_mm512_store_epi32(out + 16 * 16, v);
	v = _mm512_loadunpackhi_epi32(_mm512_loadunpacklo_epi32(v, in), in + 16);
	_mm512_store_epi32(out + 17 * 16, v);
}

void vpu_int_unaligned(const int *un, int *out)
{
	/* lo and hi together write all 16 elements: the source is not needed */
	__m512i v = _mm512_loadunpacklo_epi32(_mm512_undefined_epi32(), un);
	_mm512_store_epi32(out, _mm512_loadunpackhi_epi32(v, un + 16));
}
