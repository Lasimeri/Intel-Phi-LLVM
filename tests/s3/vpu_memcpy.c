/* vpu_memcpy.c: Intel-Phi-3120A's card/examples/vpu_memcpy.S (generated
 * .byte lines from knc-mvex) written with the Knights Corner intrinsics.
 * Same names, same contracts: whole 64-byte blocks between 64-byte aligned
 * pointers. The S3 gate links both into one binary (vpu_memcpy-gate.c) and
 * compares their output and speed on a card. The loops are not unrolled
 * further, as in the .byte version: the core is in order and a call of a
 * few blocks pays for every extra instruction. See README.md. */
#include <immintrin.h>
#include <stddef.h>

/* Copy, unrolled by four so four loads issue before the first store needs
 * one: the core is in order. */
void *knc_memcpy64(void *dst, const void *src, size_t blocks)
{
	char *d = dst;
	const char *s = src;
	_Pragma("clang loop unroll(disable)")
	for (; blocks >= 4; blocks -= 4, s += 256, d += 256) {
		__m512 a = _mm512_load_ps(s), b = _mm512_load_ps(s + 64);
		__m512 c = _mm512_load_ps(s + 128), e = _mm512_load_ps(s + 192);
		_mm512_store_ps(d, a);
		_mm512_store_ps(d + 64, b);
		_mm512_store_ps(d + 128, c);
		_mm512_store_ps(d + 192, e);
	}
	_Pragma("clang loop unroll(disable)")
	for (; blocks; blocks--, s += 64, d += 64)
		_mm512_store_ps(d, _mm512_load_ps(s));
	return dst;
}

/* Clear with ordinary stores. */
void *knc_memset64_ord(void *dst, size_t blocks)
{
	char *d = dst;
	__m512 z = _mm512_setzero_ps();
	_Pragma("clang loop unroll(disable)")
	for (; blocks >= 4; blocks -= 4, d += 256) {
		_mm512_store_ps(d, z);
		_mm512_store_ps(d + 64, z);
		_mm512_store_ps(d + 128, z);
		_mm512_store_ps(d + 192, z);
	}
	_Pragma("clang loop unroll(disable)")
	for (; blocks; blocks--, d += 64)
		_mm512_store_ps(d, z);
	return dst;
}

/* Clear with vmovnrngoaps, which is not globally ordered: a fence after. */
void *knc_memset64(void *dst, size_t blocks)
{
	char *d = dst;
	__m512 z = _mm512_setzero_ps();
	_Pragma("clang loop unroll(disable)")
	for (; blocks >= 4; blocks -= 4, d += 256) {
		_mm512_storenrngo_ps(d, z);
		_mm512_storenrngo_ps(d + 64, z);
		_mm512_storenrngo_ps(d + 128, z);
		_mm512_storenrngo_ps(d + 192, z);
	}
	_Pragma("clang loop unroll(disable)")
	for (; blocks; blocks--, d += 64)
		_mm512_storenrngo_ps(d, z);
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	return dst;
}
