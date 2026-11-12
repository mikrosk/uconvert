/* fp8.c: see fp8.h.
 *
 * Method.  Which entry holds which colour on which scanline is fixed by
 * the display routine ("schedule": every written colour is an "instance"
 * that lives in one entry from the scanline after it is written to the
 * scanline before the entry is written again).  Free are the colours of
 * the instances and the entry number of every pixel.
 *
 * 1. Greedy start, top to bottom: the new colours of a scanline are the
 *    pixels of the next scanlines that the colours kept are furthest
 *    from.
 * 2. Cost of a picture: its difference e from the original, in Y'CbCr of
 *    the gamma-coded values, seen through a low-pass g per channel:
 *        E = sum_c sum_p (g_c * e_c)(p)^2 + lambda_c e_c(p)^2
 *          = sum_c sum_p e_c(p) (k_c * e_c)(p),   k_c = g_c (*) g_c + lambda_c at 0
 *    Luminance is blurred less than colour, so the dithering noise goes
 *    where the eye resolves it least.  Without dithering k is 1 at 0.
 * 3. With cpe_c = k_c * e_c kept up to date, changing one pixel by d
 *    changes E by sum_c 2 d_c cpe_c(p) + d_c^2 k_c(0): cheap and exact.
 *    Three kinds of steps use it:
 *    - pixel sweep: every pixel takes the entry of its scanline that
 *      lowers E most;
 *    - palette sweep: every instance takes the 6-bit colour that lowers E
 *      most with its pixels as they are; unused instances go where a
 *      pixel gains most from a colour of its own;
 *    - annealing: a random instance takes the colour of a random pixel of
 *      its scanlines (the original's colour, or the colour that pixel is
 *      short of), its pixels and the pixels of its scanlines follow, and
 *      the move is kept if E fell, else with probability exp(-dE / T).
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "fp8.h"

#define LEV (255.0f / 63.0f)    /* one 6-bit level in 8-bit units */
#define SIGMA_Y 0.4f            /* low-pass of the luminance, pixels */
#define SIGMA_C 0.8f            /* low-pass of the two colour channels */
#define LAMBDA_C 0.1f           /* weight of unblurred colour error */
#define ROUNDS 12               /* pixel sweeps + palette sweep, after the annealing */
#define SWEEPS 6                /* most pixel sweeps in a row */
#define SA_T0 0.2               /* start temperature / median cost increase of a move */
#define SA_RATIO 1e-3           /* end temperature / start temperature */

static const float YCC[9] = {0.299f, 0.587f, 0.114f, -0.168736f, -0.331264f, 0.5f, 0.5f, -0.418688f, -0.081312f};
static const float RGB[9] = {1.0f, 0.0f, 1.402f, 1.0f, -0.344136f, -0.714136f, 1.0f, 1.772f, 0.0f};

typedef struct {
    int H, W, N, R, n_inst;
    int32_t *inst_id;           /* H * N: the instance in entry k on scanline y */
    int32_t *birth, *death;     /* first and last scanline of an instance */
    int32_t *ent_of;            /* its entry */
    float *C;                   /* n_inst * 3: R, G, B as 0..255, on the 6-bit grid */
    float *Copp;                /* n_inst * 3: the same in Y'CbCr */
    uint8_t *ent;               /* H * W: the pixels */
    float *rgb;                 /* H * W * 3: the original */
    float *kern;                /* 3 * (2R+1)^2 */
    float k0[3], sk0[3];        /* k_c(0) and its root */
    float *e[3], *cpe[3];       /* H * W each */
    float *pal;                 /* H * 3 * N: sqrt(k0_c) * Y'CbCr_c of the scanline's entries */
} st;

static inline void to_opp(const float *rgb, float *o)
{
    for (int c = 0; c < 3; c++)
        o[c] = YCC[3 * c] * rgb[0] + YCC[3 * c + 1] * rgb[1] + YCC[3 * c + 2] * rgb[2];
}

static inline int level(float v)
{
    int q = (int)floorf(v / LEV + 0.5f);
    return q < 0 ? 0 : q > 63 ? 63 : q;
}

/* ------------------------------------------------------------ schedule */

static int schedule(st *s, int K, int32_t **start_out)
{
    int H = s->H, N = s->N;
    int nb = N / K + (N % K != 0);
    int32_t *cur = malloc((size_t)N * sizeof(int32_t));
    int32_t *start = malloc((size_t)H * sizeof(int32_t));
    s->n_inst = N + (H - 1) * K;
    s->inst_id = malloc((size_t)H * N * sizeof(int32_t));
    s->birth = malloc((size_t)s->n_inst * sizeof(int32_t));
    s->death = malloc((size_t)s->n_inst * sizeof(int32_t));
    s->ent_of = malloc((size_t)s->n_inst * sizeof(int32_t));
    if (!cur || !start || !s->inst_id || !s->birth || !s->death || !s->ent_of)
        return -1;
    for (int k = 0; k < N; k++) {
        cur[k] = k;
        s->birth[k] = 0;
        s->death[k] = H - 1;
        s->ent_of[k] = k;
    }
    memcpy(s->inst_id, cur, (size_t)N * sizeof(int32_t));
    start[0] = -1;
    int n = N;
    for (int y = 1; y < H; y++) {
        int j = (y - 1) % nb;
        int e0 = j * K + K <= N ? j * K : N - K;
        for (int e = e0; e < e0 + K; e++) {
            s->death[cur[e]] = y - 1;
            cur[e] = n;
            s->birth[n] = y;
            s->death[n] = H - 1;
            s->ent_of[n] = e;
            n++;
        }
        memcpy(s->inst_id + (size_t)y * N, cur, (size_t)N * sizeof(int32_t));
        start[y] = e0;
    }
    free(cur);
    *start_out = start;
    return 0;
}

/* -------------------------------------------------------- greedy start */

static inline float d2(const float *a, const float *b)
{
    float r = a[0] - b[0], g = a[1] - b[1], bl = a[2] - b[2];
    return r * r + g * g + bl * bl;
}

/* k colours, each the pixel worst covered so far (d: squared distance of
 * every pixel to the colours there already, updated) */
static void maxmin(const float *px, int n, float *d, float *out, int k, int fresh)
{
    for (int j = 0; j < k; j++) {
        int a = 0;
        for (int i = 1; i < n; i++)
            if (d[i] > d[a])
                a = i;
        for (int c = 0; c < 3; c++)
            out[3 * j + c] = level(px[3 * a + c]) * LEV;
        for (int i = 0; i < n; i++) {
            float dn = d2(px + 3 * i, out + 3 * j);
            if ((fresh && j == 0) || dn < d[i])
                d[i] = dn;
        }
    }
}

/* Lloyd iterations for the k colours; with dkeep only pixels closer to
 * them than to the colours kept elsewhere count */
static void kmeans(const float *px, int n, float *pal, int k, int its, const float *dkeep, double *sum, int *cnt)
{
    for (int it = 0; it < its; it++) {
        memset(sum, 0, (size_t)k * 3 * sizeof(double));
        memset(cnt, 0, (size_t)k * sizeof(int));
        for (int i = 0; i < n; i++) {
            int a = 0;
            float best = d2(px + 3 * i, pal);
            for (int j = 1; j < k; j++) {
                float d = d2(px + 3 * i, pal + 3 * j);
                if (d < best) {
                    best = d;
                    a = j;
                }
            }
            if (dkeep && !(best < dkeep[i]))
                continue;
            cnt[a]++;
            for (int c = 0; c < 3; c++)
                sum[3 * a + c] += px[3 * i + c];
        }
        for (int j = 0; j < k; j++)
            if (cnt[j])
                for (int c = 0; c < 3; c++)
                    pal[3 * j + c] = level((float)(sum[3 * j + c] / cnt[j])) * LEV;
    }
}

static int greedy(st *s, int K, const int32_t *start)
{
    int H = s->H, W = s->W, N = s->N;
    int life = N / K + (N % K != 0);
    int sub = N > 16 ? 2 : 1, wpx = (W + sub - 1) / sub;
    float *px = malloc((size_t)life * wpx * 3 * sizeof(float));
    float *d = malloc((size_t)life * wpx * sizeof(float));
    float *dkeep = malloc((size_t)life * wpx * sizeof(float));
    float *state = malloc((size_t)N * 3 * sizeof(float));
    double *sum = malloc((size_t)N * 3 * sizeof(double));
    int *cnt = malloc((size_t)N * sizeof(int));
    if (!px || !d || !dkeep || !state || !sum || !cnt)
        return -1;
    for (int y = 0; y < H; y++) {
        int n = 0;
        for (int yy = y; yy < H && yy < y + life; yy++)
            for (int x = 0; x < W; x += sub, n++)
                memcpy(px + 3 * n, s->rgb + 3 * ((size_t)yy * W + x), 3 * sizeof(float));
        if (y == 0) {
            float mean[3] = {0, 0, 0};
            for (int i = 0; i < n; i++)
                for (int c = 0; c < 3; c++)
                    mean[c] += px[3 * i + c] / n;
            for (int i = 0; i < n; i++)
                d[i] = d2(px + 3 * i, mean);
            maxmin(px, n, d, state, N, 1);
            kmeans(px, n, state, N, 4, NULL, sum, cnt);
            memcpy(s->C, state, (size_t)N * 3 * sizeof(float));
            continue;
        }
        int e0 = start[y];
        float *fresh = s->C + 3 * (size_t)s->inst_id[(size_t)y * N + e0];
        for (int i = 0; i < n; i++) {
            float best = 3e38f;
            for (int k = 0; k < N; k++) {
                if (k >= e0 && k < e0 + K)
                    continue;
                float dd = d2(px + 3 * i, state + 3 * k);
                if (dd < best)
                    best = dd;
            }
            dkeep[i] = d[i] = best;
        }
        maxmin(px, n, d, fresh, K, 0);
        kmeans(px, n, fresh, K, 2, dkeep, sum, cnt);
        memcpy(state + 3 * e0, fresh, (size_t)K * 3 * sizeof(float));
    }
    free(px); free(d); free(dkeep); free(state); free(sum); free(cnt);
    return 0;
}

/* ----------------------------------------------------------- the cost */

static void build_pal(st *s)
{
    for (int i = 0; i < s->n_inst; i++)
        to_opp(s->C + 3 * i, s->Copp + 3 * i);
    for (int y = 0; y < s->H; y++)
        for (int k = 0; k < s->N; k++) {
            const float *o = s->Copp + 3 * (size_t)s->inst_id[(size_t)y * s->N + k];
            for (int c = 0; c < 3; c++)
                s->pal[((size_t)y * 3 + c) * s->N + k] = s->sk0[c] * o[c];
        }
}

/* add d (Y'CbCr) to the picture at (y, x): e and cpe follow */
static inline void bump(st *s, int y, int x, const float *d)
{
    int R = s->R, D = 2 * R + 1, W = s->W;
    int y0 = y - R < 0 ? 0 : y - R, y1 = y + R >= s->H ? s->H - 1 : y + R;
    int x0 = x - R < 0 ? 0 : x - R, x1 = x + R >= W ? W - 1 : x + R;
    for (int c = 0; c < 3; c++) {
        float dc = d[c];
        if (dc == 0)
            continue;
        const float *k = s->kern + (size_t)c * D * D;
        float *cp = s->cpe[c];
        s->e[c][(size_t)y * W + x] += dc;
        for (int yy = y0; yy <= y1; yy++) {
            const float *kr = k + (size_t)(yy - y + R) * D + (x0 - x + R);
            float *row = cp + (size_t)yy * W + x0;
            for (int i = 0; i <= x1 - x0; i++)
                row[i] += dc * kr[i];
        }
    }
}

/* e and cpe from scratch */
static void rebuild(st *s)
{
    size_t np = (size_t)s->H * s->W;
    for (int c = 0; c < 3; c++) {
        memset(s->e[c], 0, np * sizeof(float));
        memset(s->cpe[c], 0, np * sizeof(float));
    }
    for (int y = 0; y < s->H; y++)
        for (int x = 0; x < s->W; x++) {
            size_t p = (size_t)y * s->W + x;
            float t[3], d[3];
            const float *o = s->Copp + 3 * (size_t)s->inst_id[(size_t)y * s->N + s->ent[p]];
            to_opp(s->rgb + 3 * p, t);
            for (int c = 0; c < 3; c++)
                d[c] = o[c] - t[c];
            bump(s, y, x, d);
        }
}

/* --------------------------------------------------------- pixel sweep */

static long pixel_sweep(st *s, int reverse)
{
    int N = s->N, W = s->W, H = s->H;
    long changed = 0;
    float dist[256];
    for (int yi = 0; yi < H; yi++) {
        int y = reverse ? H - 1 - yi : yi;
        const float *P0 = s->pal + ((size_t)y * 3) * N, *P1 = P0 + N, *P2 = P1 + N;
        for (int xi = 0; xi < W; xi++) {
            int x = reverse ? W - 1 - xi : xi;
            size_t p = (size_t)y * W + x;
            int a = s->ent[p];
            /* the colour that would cancel the error seen here */
            float t0 = P0[a] - s->cpe[0][p] / s->sk0[0];
            float t1 = P1[a] - s->cpe[1][p] / s->sk0[1];
            float t2 = P2[a] - s->cpe[2][p] / s->sk0[2];
            for (int k = 0; k < N; k++) {
                float d0 = P0[k] - t0, d1 = P1[k] - t1, d2 = P2[k] - t2;
                dist[k] = d0 * d0 + d1 * d1 + d2 * d2;
            }
            int b = a;
            float bd = dist[a] * 0.999999f - 1e-6f;
            for (int k = 0; k < N; k++)
                if (dist[k] < bd) {
                    bd = dist[k];
                    b = k;
                }
            if (b != a) {
                float d[3] = {(P0[b] - P0[a]) / s->sk0[0], (P1[b] - P1[a]) / s->sk0[1],
                              (P2[b] - P2[a]) / s->sk0[2]};
                bump(s, y, x, d);
                s->ent[p] = (uint8_t)b;
                changed++;
            }
        }
    }
    return changed;
}

static void settle(st *s, int phase)
{
    long few = (long)s->H * s->W / 2000;
    for (int sw = 0; sw < SWEEPS; sw++)
        if (pixel_sweep(s, (phase + sw) & 1) < few)
            break;
}

/* ------------------------------------------------------- palette sweep */

/* change of E when the instance at levels u goes to levels v */
static inline double quad(const int *v, const int *u, const double *S1, const double *S2)
{
    float dr[3] = {(v[0] - u[0]) * LEV, (v[1] - u[1]) * LEV, (v[2] - u[2]) * LEV}, d[3];
    to_opp(dr, d);
    double q = 0;
    for (int c = 0; c < 3; c++)
        q += 2.0 * d[c] * S1[c] + (double)d[c] * d[c] * S2[c];
    return q;
}

static int palette_sweep(st *s)
{
    int N = s->N, W = s->W, H = s->H, R = s->R, D = 2 * R + 1;
    size_t np = (size_t)H * W;
    int32_t *inst_of = malloc(np * sizeof(int32_t));
    int32_t *first = calloc((size_t)s->n_inst + 1, sizeof(int32_t));
    int32_t *fill = malloc((size_t)s->n_inst * sizeof(int32_t));
    int32_t *list = malloc(np * sizeof(int32_t));
    if (!inst_of || !first || !fill || !list)
        return -1;
    for (size_t p = 0; p < np; p++) {
        inst_of[p] = s->inst_id[(p / W) * N + s->ent[p]];
        first[inst_of[p] + 1]++;
    }
    for (int i = 0; i < s->n_inst; i++)
        first[i + 1] += first[i];
    memcpy(fill, first, (size_t)s->n_inst * sizeof(int32_t));
    for (size_t p = 0; p < np; p++)
        list[fill[inst_of[p]]++] = (int32_t)p;
    for (int i = 0; i < s->n_inst; i++) {
        int n = first[i + 1] - first[i];
        const int32_t *pl = list + first[i];
        int e = s->ent_of[i];
        if (n == 0) {
            /* unused: to the pixel of its scanlines that gains most from a colour of its own */
            double best = -1e-3;
            int by = -1, bx = -1, bv[3] = {0, 0, 0};
            float bd[3] = {0, 0, 0};
            for (int y = s->birth[i]; y <= s->death[i]; y++)
                for (int x = 0; x < W; x++) {
                    size_t p = (size_t)y * W + x;
                    const float *cur = s->Copp + 3 * (size_t)inst_of[p];
                    float want[3], rgb[3], got[3], d[3];
                    int v[3];
                    for (int c = 0; c < 3; c++)
                        want[c] = cur[c] - s->cpe[c][p] / s->k0[c];
                    for (int c = 0; c < 3; c++) {
                        v[c] = level(RGB[3 * c] * want[0] + RGB[3 * c + 1] * want[1] + RGB[3 * c + 2] * want[2]);
                        rgb[c] = v[c] * LEV;
                    }
                    to_opp(rgb, got);
                    double de = 0;
                    for (int c = 0; c < 3; c++) {
                        d[c] = got[c] - cur[c];
                        de += 2.0 * d[c] * s->cpe[c][p] + (double)d[c] * d[c] * s->k0[c];
                    }
                    if (de < best) {
                        best = de; by = y; bx = x;
                        memcpy(bv, v, sizeof bv);
                        memcpy(bd, d, sizeof bd);
                    }
                }
            if (by >= 0) {
                size_t p = (size_t)by * W + bx;
                for (int c = 0; c < 3; c++)
                    s->C[3 * i + c] = bv[c] * LEV;
                to_opp(s->C + 3 * i, s->Copp + 3 * i);
                bump(s, by, bx, bd);
                s->ent[p] = (uint8_t)e;
                inst_of[p] = i;
            }
            continue;
        }
        double S1[3] = {0, 0, 0}, S2[3] = {0, 0, 0};
        for (int j = 0; j < n; j++) {
            size_t p = pl[j];
            if (inst_of[p] != i)        /* taken by a colour moved above */
                continue;
            int y = (int)(p / W), x = (int)(p % W);
            for (int c = 0; c < 3; c++)
                S1[c] += s->cpe[c][p];
            int y0 = y - R < 0 ? 0 : y - R, y1 = y + R >= H ? H - 1 : y + R;
            int x0 = x - R < 0 ? 0 : x - R, x1 = x + R >= W ? W - 1 : x + R;
            for (int yy = y0; yy <= y1; yy++) {
                const int32_t *io = inst_of + (size_t)yy * W;
                size_t ko = (size_t)(yy - y + R) * D + (size_t)(R - x);
                for (int xx = x0; xx <= x1; xx++)
                    if (io[xx] == i)
                        for (int c = 0; c < 3; c++)
                            S2[c] += s->kern[(size_t)c * D * D + ko + xx];
            }
        }
        int u[3], v[3];
        float want[3];
        for (int c = 0; c < 3; c++) {
            u[c] = level(s->C[3 * i + c]);
            want[c] = S2[c] > 1e-9 ? (float)(-S1[c] / S2[c]) : 0;
        }
        for (int c = 0; c < 3; c++) {
            float r = RGB[3 * c] * want[0] + RGB[3 * c + 1] * want[1] + RGB[3 * c + 2] * want[2];
            int q = u[c] + (int)floorf(r / LEV + 0.5f);
            v[c] = q < 0 ? 0 : q > 63 ? 63 : q;
        }
        double q = quad(v, u, S1, S2);
        for (int it = 0; it < 64; it++) {       /* down the 6-bit grid */
            int bc = -1, bs = 0;
            double bq = q;
            for (int c = 0; c < 3; c++)
                for (int sg = -1; sg <= 1; sg += 2) {
                    int w[3] = {v[0], v[1], v[2]};
                    w[c] += sg;
                    if (w[c] < 0 || w[c] > 63)
                        continue;
                    double qq = quad(w, u, S1, S2);
                    if (qq < bq) {
                        bq = qq; bc = c; bs = sg;
                    }
                }
            if (bc < 0)
                break;
            v[bc] += bs;
            q = bq;
        }
        if (q < -1e-3 && (v[0] != u[0] || v[1] != u[1] || v[2] != u[2])) {
            float dr[3] = {(v[0] - u[0]) * LEV, (v[1] - u[1]) * LEV, (v[2] - u[2]) * LEV}, d[3];
            to_opp(dr, d);
            for (int j = 0; j < n; j++)
                if (inst_of[pl[j]] == i)
                    bump(s, pl[j] / W, pl[j] % W, d);
            for (int c = 0; c < 3; c++)
                s->C[3 * i + c] = v[c] * LEV;
        }
    }
    free(inst_of); free(first); free(fill); free(list);
    build_pal(s);
    return 0;
}

/* ----------------------------------------------------------- annealing */

static inline uint64_t rnd64(uint64_t *s)
{
    uint64_t x = *s;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *s = x;
    return x * 2685821657736338717ULL;
}

typedef struct {
    int32_t p;
    uint8_t old;
    float d[3];
} change;

/* Instance i takes the levels v; the pixels of its scanlines follow:
 * those that showed it choose again among all entries, the others take
 * it if that lowers E.  Returns the change of E. */
static double recolour(st *s, int i, const int *v, change *log, int *n_log)
{
    int N = s->N, W = s->W, e = s->ent_of[i];
    float old[3], now[3], dist[256];
    double dE = 0;
    int n = 0;
    memcpy(old, s->Copp + 3 * i, sizeof old);
    for (int c = 0; c < 3; c++)
        s->C[3 * i + c] = v[c] * LEV;
    to_opp(s->C + 3 * i, s->Copp + 3 * i);
    memcpy(now, s->Copp + 3 * i, sizeof now);
    for (int y = s->birth[i]; y <= s->death[i]; y++) {
        float *P0 = s->pal + ((size_t)y * 3) * N, *P1 = P0 + N, *P2 = P1 + N;
        P0[e] = s->sk0[0] * now[0]; P1[e] = s->sk0[1] * now[1]; P2[e] = s->sk0[2] * now[2];
        for (int x = 0; x < W; x++) {
            size_t p = (size_t)y * W + x;
            int a = s->ent[p];
            float d[3];
            if (a == e) {
                float t0 = s->sk0[0] * old[0] - s->cpe[0][p] / s->sk0[0];
                float t1 = s->sk0[1] * old[1] - s->cpe[1][p] / s->sk0[1];
                float t2 = s->sk0[2] * old[2] - s->cpe[2][p] / s->sk0[2];
                for (int k = 0; k < N; k++) {
                    float d0 = P0[k] - t0, d1 = P1[k] - t1, d2 = P2[k] - t2;
                    dist[k] = d0 * d0 + d1 * d1 + d2 * d2;
                }
                int b = e;
                for (int k = 0; k < N; k++)
                    if (dist[k] < dist[b])
                        b = k;
                d[0] = P0[b] / s->sk0[0] - old[0];
                d[1] = P1[b] / s->sk0[1] - old[1];
                d[2] = P2[b] / s->sk0[2] - old[2];
                a = b;
            } else {
                double de = 0;
                d[0] = now[0] - P0[a] / s->sk0[0];
                d[1] = now[1] - P1[a] / s->sk0[1];
                d[2] = now[2] - P2[a] / s->sk0[2];
                for (int c = 0; c < 3; c++)
                    de += 2.0 * d[c] * s->cpe[c][p] + (double)d[c] * d[c] * s->k0[c];
                if (de >= -1e-4)
                    continue;
                a = e;
            }
            for (int c = 0; c < 3; c++)
                dE += 2.0 * d[c] * s->cpe[c][p] + (double)d[c] * d[c] * s->k0[c];
            log[n].p = (int32_t)p;
            log[n].old = s->ent[p];
            memcpy(log[n].d, d, sizeof d);
            n++;
            bump(s, y, x, d);
            s->ent[p] = (uint8_t)a;
        }
    }
    *n_log = n;
    return dE;
}

static void undo(st *s, int i, const float *oldC, const change *log, int n)
{
    int N = s->N, W = s->W, e = s->ent_of[i];
    for (int j = n - 1; j >= 0; j--) {
        float d[3] = {-log[j].d[0], -log[j].d[1], -log[j].d[2]};
        bump(s, log[j].p / W, log[j].p % W, d);
        s->ent[log[j].p] = log[j].old;
    }
    memcpy(s->C + 3 * i, oldC, 3 * sizeof(float));
    to_opp(s->C + 3 * i, s->Copp + 3 * i);
    for (int y = s->birth[i]; y <= s->death[i]; y++)
        for (int c = 0; c < 3; c++)
            s->pal[((size_t)y * 3 + c) * N + e] = s->sk0[c] * s->Copp[3 * i + c];
}

static int anneal(st *s, int64_t moves, uint64_t seed)
{
    int N = s->N, W = s->W;
    int maxrows = 0;
    for (int i = 0; i < s->n_inst; i++)
        if (s->death[i] - s->birth[i] + 1 > maxrows)
            maxrows = s->death[i] - s->birth[i] + 1;
    change *log = malloc((size_t)maxrows * W * sizeof(change));
    double probe[256], t0 = -1;
    int nprobe = 0;
    if (!log)
        return -1;
    if (!seed)
        seed = 1;
    for (int64_t n = 0; n < moves; n++) {
        if (n % 20000 == 19999)
            rebuild(s);         /* drops the rounding left by the updates */
        int i = (int)((rnd64(&seed) >> 33) % (uint64_t)s->n_inst);
        int y = s->birth[i] + (int)((rnd64(&seed) >> 33) % (uint64_t)(s->death[i] - s->birth[i] + 1));
        size_t p = (size_t)y * W + (rnd64(&seed) >> 33) % (uint64_t)W;
        float rgb[3];
        if (n & 1) {
            /* the colour this pixel is short of */
            const float *cur = s->Copp + 3 * (size_t)s->inst_id[(size_t)y * N + s->ent[p]];
            float want[3];
            for (int c = 0; c < 3; c++)
                want[c] = cur[c] - s->cpe[c][p] / s->k0[c];
            for (int c = 0; c < 3; c++)
                rgb[c] = RGB[3 * c] * want[0] + RGB[3 * c + 1] * want[1] + RGB[3 * c + 2] * want[2];
        } else {
            memcpy(rgb, s->rgb + 3 * p, sizeof rgb);
        }
        int v[3], same = 1, nl;
        float oldC[3];
        memcpy(oldC, s->C + 3 * i, sizeof oldC);
        for (int c = 0; c < 3; c++) {
            v[c] = level(rgb[c]);
            same &= v[c] == level(oldC[c]);
        }
        if (same)
            continue;
        double dE = recolour(s, i, v, log, &nl);
        if (t0 < 0) {
            /* the first moves set the temperature; only improvements are kept */
            if (dE > 0) {
                probe[nprobe++] = dE;
                undo(s, i, oldC, log, nl);
            }
            if (nprobe < 256 && n < moves / 10)
                continue;
            if (nprobe) {
                for (int a = 1; a < nprobe; a++) {
                    double k = probe[a];
                    int b = a - 1;
                    for (; b >= 0 && probe[b] > k; b--)
                        probe[b + 1] = probe[b];
                    probe[b + 1] = k;
                }
                t0 = SA_T0 * probe[nprobe / 2];
            } else {
                t0 = 1e-6;
            }
            continue;
        }
        double T = t0 * pow(SA_RATIO, (double)n / (double)moves);
        if (dE > 0 && (double)(rnd64(&seed) >> 11) * (1.0 / 9007199254740992.0) >= exp(-dE / T))
            undo(s, i, oldC, log, nl);
    }
    free(log);
    rebuild(s);
    return 0;
}

/* ---------------------------------------------------------------- main */

static int kernels(st *s, int dither)
{
    float sigma[3] = {SIGMA_Y, SIGMA_C, SIGMA_C}, lambda[3] = {0, LAMBDA_C, LAMBDA_C};
    s->R = dither ? (int)ceil(3 * SIGMA_C * sqrt(2.0)) : 1;
    int D = 2 * s->R + 1;
    double k1[64];
    s->kern = calloc((size_t)3 * D * D, sizeof(float));
    if (!s->kern || D > 64)
        return -1;
    for (int c = 0; c < 3; c++) {
        float *k = s->kern + (size_t)c * D * D;
        if (!dither) {
            k[(size_t)s->R * D + s->R] = 1;
        } else {
            /* g (*) g of a Gaussian of deviation sigma: a Gaussian of variance 2 sigma^2 */
            double sum = 0;
            for (int i = 0; i < D; i++)
                sum += k1[i] = exp(-(double)(i - s->R) * (i - s->R) / (4.0 * sigma[c] * sigma[c]));
            for (int a = 0; a < D; a++)
                for (int b = 0; b < D; b++)
                    k[(size_t)a * D + b] = (float)(k1[a] * k1[b] / (sum * sum));
            k[(size_t)s->R * D + s->R] += lambda[c];
        }
        s->k0[c] = k[(size_t)s->R * D + s->R];
        s->sk0[c] = sqrtf(s->k0[c]);
    }
    return 0;
}

int fp8_convert(const fp8_params *par, const uint8_t *rgb, uint8_t *pix, uint8_t *pal, uint8_t *rec,
                uint16_t *first)
{
    static const int per_colour[10] = {0, 25, 50, 100, 200, 400, 800, 1600, 3200, 6400};
    st s;
    int32_t *start = NULL;
    int W = par->width, H = par->height, N = par->entries, K = par->per_line, rc = -1;
    size_t np = (size_t)W * H;
    if (W < 1 || H < 2 || N < 2 || N > 256 || K < 1 || K >= N || par->effort < 0 || par->effort > 9)
        return -1;
    memset(&s, 0, sizeof s);
    s.H = H; s.W = W; s.N = N;
    if (schedule(&s, K, &start))
        goto out;
    s.rgb = malloc(np * 3 * sizeof(float));
    s.C = calloc((size_t)s.n_inst * 3, sizeof(float));
    s.Copp = malloc((size_t)s.n_inst * 3 * sizeof(float));
    s.pal = malloc((size_t)H * 3 * N * sizeof(float));
    for (int c = 0; c < 3; c++) {
        s.e[c] = malloc(np * sizeof(float));
        s.cpe[c] = malloc(np * sizeof(float));
        if (!s.e[c] || !s.cpe[c])
            goto out;
    }
    if (!s.rgb || !s.C || !s.Copp || !s.pal || kernels(&s, par->dither))
        goto out;
    s.ent = pix;
    for (size_t i = 0; i < np * 3; i++)
        s.rgb[i] = rgb[i];
    if (greedy(&s, K, start))
        goto out;
    /* start: every pixel the nearest colour of its scanline */
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            size_t p = (size_t)y * W + x;
            int b = 0;
            float bd = 3e38f;
            for (int k = 0; k < N; k++) {
                float d = d2(s.rgb + 3 * p, s.C + 3 * (size_t)s.inst_id[(size_t)y * N + k]);
                if (d < bd) {
                    bd = d;
                    b = k;
                }
            }
            pix[p] = (uint8_t)b;
        }
    build_pal(&s);
    rebuild(&s);
    settle(&s, 0);
    if (per_colour[par->effort] && anneal(&s, (int64_t)per_colour[par->effort] * s.n_inst, par->seed))
        goto out;
    for (int r = 0; r < ROUNDS; r++) {
        settle(&s, r);
        if (palette_sweep(&s))
            goto out;
    }
    settle(&s, 0);
    for (int k = 0; k < N; k++)
        for (int c = 0; c < 3; c++)
            pal[3 * k + c] = (uint8_t)level(s.C[3 * (size_t)s.inst_id[k] + c]);
    for (int r = 0; r < H - 1; r++) {
        int e0 = start[r + 1];
        first[r] = (uint16_t)e0;
        for (int k = 0; k < K; k++)
            for (int c = 0; c < 3; c++)
                rec[((size_t)r * K + k) * 3 + c] =
                    (uint8_t)level(s.C[3 * (size_t)s.inst_id[(size_t)(r + 1) * N + e0 + k] + c]);
    }
    rc = 0;
out:
    free(start); free(s.inst_id); free(s.birth); free(s.death); free(s.ent_of);
    free(s.rgb); free(s.C); free(s.Copp); free(s.pal); free(s.kern);
    for (int c = 0; c < 3; c++) {
        free(s.e[c]); free(s.cpe[c]);
    }
    return rc;
}
