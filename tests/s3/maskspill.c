/* maskspill.c: a self-checking card test of the paths that keep a mask in
 * a k register: a mask carried around a loop that calls a function (the
 * call clobbers k0-k7, so the mask is spilled and reloaded: KNC_SPILL_K and
 * KNC_RELOAD_K), and twelve masks live at once. Every lane is checked
 * against scalar C. Prints "maskspill: PASS" or "FAIL". See maskspill.md. */
#include <immintrin.h>
#include <stdio.h>

__attribute__((noinline)) int bump(int x)
{
	__asm__ volatile("" ::: "memory");
	return x + 1;
}

/* count, per lane, the steps until v reaches lim (at most n), calling bump
 * once per step with the mask live across the call */
__attribute__((noinline)) void loop_call(const float *a, const float *lim, float *cnt_out, float *v_out, int n)
{
	__m512 v = _mm512_load_ps(a), l = _mm512_load_ps(lim), one = _mm512_set1_ps(1.0f), cnt = _mm512_setzero_ps();
	__mmask16 k = 0xffff;
	int i = 0;
	do {
		k = _mm512_mask_cmplt_ps_mask(k, v, l);
		if (_mm512_kortestz(k, k))
			break;
		v = _mm512_mask_add_ps(v, k, v, one);
		cnt = _mm512_mask_add_ps(cnt, k, cnt, one);
		i = bump(i);
	} while (i < n);
	_mm512_store_ps(cnt_out, cnt);
	_mm512_store_ps(v_out, v);
}

/* twelve masks computed first, live across a call, and used after: acc gets 2^j in the lanes
 * where x < j */
#define K(j) __mmask16 k##j = _mm512_cmplt_ps_mask(x, _mm512_set1_ps((float)(j)));
#define U(j) acc = _mm512_mask_add_ps(acc, k##j, acc, _mm512_set1_ps((float)(1 << (j))));
__attribute__((noinline)) void many(const float *a, float *out)
{
	__m512 x = _mm512_load_ps(a), acc = _mm512_setzero_ps();
	K(0) K(1) K(2) K(3) K(4) K(5) K(6) K(7) K(8) K(9) K(10) K(11)
	(void)bump(0); /* all twelve live across a call: each is spilled */
	U(0) U(1) U(2) U(3) U(4) U(5) U(6) U(7) U(8) U(9) U(10) U(11)
	_mm512_store_ps(out, acc);
}

int main(void)
{
	_Alignas(64) float a[16], lim[16], cnt[16], v[16], x[16], acc[16];
	int bad = 0, checks = 0;
	for (int n = 1; n <= 40; n += 13)
		for (int t = 0; t < 4; t++) {
			for (int i = 0; i < 16; i++) { a[i] = (float)((i * 7 + t) % 11); lim[i] = (float)((i * 5 + 3 * t) % 23); }
			loop_call(a, lim, cnt, v, n);
			for (int i = 0; i < 16; i++) {
				float vv = a[i], c = 0; int alive = 1;
				for (int s = 0; s < n && alive; s++) { if (vv < lim[i]) { vv += 1; c += 1; } else alive = 0; }
				checks += 2; bad += cnt[i] != c; bad += v[i] != vv;
			}
		}
	for (int t = 0; t < 4; t++) {
		for (int i = 0; i < 16; i++) x[i] = (float)((i + 3 * t) % 13) - 0.5f;
		many(x, acc);
		for (int i = 0; i < 16; i++) {
			float want = 0;
			for (int j = 0; j < 12; j++) if (x[i] < (float)j) want += (float)(1 << j);
			checks++; bad += acc[i] != want;
		}
	}
	printf("maskspill: %s (%d checks, %d wrong)\n", bad ? "FAIL" : "PASS", checks, bad);
	return bad != 0;
}
