/* mandel_vpu.c: Intel-Phi-3120A's card/examples/mandel_vpu.S (knc-mvex .byte
 * lines) written with the Knights Corner intrinsics: the same operations in
 * the same order, so the same results bit for bit. See README.md.
 *
 * void mandel8_vpu(const double *c, double *out, int maxiter):
 * c[0..7] = cr, c[8..15] = ci; out[0..7] = escape counts, out[8..15] = |z|^2
 * at exit; 64-byte aligned. The loop runs at least once, as the .S does. */
#include <immintrin.h>

void mandel8_vpu(const double *c, double *out, int maxiter)
{
	const __m512d cr = _mm512_load_pd(c), ci = _mm512_load_pd(c + 8);
	const __m512d two = _mm512_set1_pd(2.0), four = _mm512_set1_pd(4.0), one = _mm512_set1_pd(1.0);
	__m512d zr = _mm512_setzero_pd(), zi = zr, zr2 = zr, zi2 = zr, n = zr;
	__mmask8 k = 0xff;
	int i = 0;
	do {
		/* lanes still inside |z|^2 < 4; none left: done */
		k = _mm512_mask_cmplt_pd_mask(k, _mm512_add_pd(zr2, zi2), four);
		if (_mm512_kortestz(k, k))
			break;
		__m512d t = _mm512_mul_pd(zr, two);           /* 2 zr */
		zi = _mm512_mask_fmadd_pd(zi, k, t, ci);       /* zi = 2 zr zi + ci, one rounding */
		zr = _mm512_mask_sub_pd(zr, k, zr2, zi2);      /* zr = zr^2 - zi^2 + cr */
		zr = _mm512_mask_add_pd(zr, k, zr, cr);
		zr2 = _mm512_mask_mul_pd(zr2, k, zr, zr);
		zi2 = _mm512_mask_mul_pd(zi2, k, zi, zi);
		n = _mm512_mask_add_pd(n, k, n, one);
	} while (++i < maxiter);
	_mm512_store_pd(out, n);
	_mm512_store_pd(out + 8, _mm512_add_pd(zr2, zi2));
}
