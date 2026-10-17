/* mandel_vpu-gate.c: the S3 gate for mandel_vpu. The intrinsic mandel8_vpu
 * (mandel_vpu.c) and the stack's .byte one (mandel_vpu.S, symbols prefixed
 * ref_) run over the same grid of points: escape counts and |z|^2 must be
 * identical bit for bit; speed is measured interleaved. See README.md. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void mandel8_vpu(const double *c, double *out, int maxiter);
void ref_mandel8_vpu(const double *c, double *out, int maxiter);

static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec * 1e-9;
}

static int cmp_d(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return x < y ? -1 : x > y;
}

#define W 256
#define H 192
#define ROUNDS 9

/* one row: W points, 8 per call; returns nothing, fills out */
static void row(void (*f)(const double *, double *, int), int y, int maxiter, double *out)
{
	_Alignas(64) double c[16];
	for (int x = 0; x < W; x += 8) {
		for (int l = 0; l < 8; l++) {
			c[l] = -2.25 + 3.0 * (x + l) / W;
			c[8 + l] = -1.25 + 2.5 * y / H;
		}
		f(c, out + 2 * x, maxiter);
	}
}

int main(void)
{
	static _Alignas(64) double a[2 * W], b[2 * W];
	static const int iters[] = {1, 2, 17, 256, 1000};
	size_t cases = 0, bad = 0;
	for (size_t m = 0; m < sizeof iters / sizeof iters[0]; m++)
		for (int y = 0; y < H; y++) {
			memset(a, 0xAA, sizeof a); memset(b, 0xAA, sizeof b);
			row(ref_mandel8_vpu, y, iters[m], a);
			row(mandel8_vpu, y, iters[m], b);
			cases += W / 8;
			bad += memcmp(a, b, sizeof a) != 0;
		}
	printf("output: %zu calls (%d x %d points at maxiter 1, 2, 17, 256, 1000), %zu rows differ\n",
	       cases, W, H, bad);

	int slow = 0;
	static const int titers[] = {64, 1000};
	for (size_t m = 0; m < 2; m++) {
		double tr[ROUNDS], ti[ROUNDS];
		for (int r = 0; r < ROUNDS; r++)
			for (int o = 0; o < 2; o++) {
				int intr = (r + o) & 1;
				double t0 = now();
				for (int y = 0; y < H; y++) row(intr ? mandel8_vpu : ref_mandel8_vpu, y, titers[m], a);
				(intr ? ti : tr)[r] = (now() - t0) * 1e3;
			}
		qsort(tr, ROUNDS, sizeof tr[0], cmp_d); qsort(ti, ROUNDS, sizeof ti[0], cmp_d);

		/* equal within the spread: the median is inside the .byte range, or better */
		int within = ti[ROUNDS / 2] <= tr[ROUNDS - 1];
		slow += !within;
		printf("mandel8 maxiter %4d, %dx%d: .byte median %8.3f ms [%8.3f..%8.3f]  intrinsics median %8.3f ms [%8.3f..%8.3f]  ratio %.3f %s\n",
		       titers[m], W, H, tr[ROUNDS / 2], tr[0], tr[ROUNDS - 1], ti[ROUNDS / 2], ti[0], ti[ROUNDS - 1],
		       tr[ROUNDS / 2] / ti[ROUNDS / 2], within ? "within" : "SLOWER");
	}
	printf("mandel_vpu-gate: %s\n", bad ? "FAIL (output)" : slow ? "SLOWER" : "PASS");
	return bad ? 1 : slow ? 3 : 0;
}
