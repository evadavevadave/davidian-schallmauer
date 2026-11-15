/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* Valve models: Norman Koren's triode and pentode plate-current equations (the published
 * 12AX7 and EL34 fits), tabulated over grid and plate voltage so each sample costs a table
 * lookup, plus a smooth grid-current curve (the form from Dempwolf and Zolzer's triode model).
 * The solvers find each valve's currents against its circuit by Newton's method. */
#ifndef SH_TUBE_H
#define SH_TUBE_H

#include <math.h>
#include <stdlib.h>

typedef struct {
    double mu, ex, kg1, kp, kvb; /* Koren constants */
    int pentode;
    double vg2;                  /* pentode: nominal screen voltage, for koren() */
    double gg, cg, xi;           /* grid current: gg * (softplus(cg * v) / cg)^xi */
    double kg2;                  /* pentode screen current constant */
} TubeSpec;

/* 12AX7 / ECC83 (Koren). */
static const TubeSpec TUBE_12AX7 = { 100.0, 1.4, 1060.0, 600.0, 300.0, 0, 0.0, 6.06e-4, 13.9, 1.354, 0.0 };
/* EL34 / 6CA7 (Koren). Grid current is an estimate. */
static const TubeSpec TUBE_EL34 = { 11.0, 1.35, 650.0, 60.0, 24.0, 1, 470.0, 1.0e-3, 5.0, 1.5, 4200.0 };

static double koren(const TubeSpec* s, double vg, double vp)
{
    double e1, a;
    if (vp < 0.0)
        vp = 0.0;
    if (s->pentode) {
        a = s->kp * (1.0 / s->mu + vg / s->vg2);
        e1 = s->vg2 / s->kp * (a > 30.0 ? a : log1p(exp(a)));
        return e1 > 0.0 ? 2.0 * pow(e1, s->ex) / s->kg1 * atan(vp / s->kvb) : 0.0;
    }
    a = s->kp * (1.0 / s->mu + vg / sqrt(s->kvb + vp * vp));
    e1 = vp / s->kp * (a > 30.0 ? a : log1p(exp(a)));
    return e1 > 0.0 ? 2.0 * pow(e1, s->ex) / s->kg1 : 0.0;
}

/* Grid current and its slope, for grid-to-cathode voltage v (the exact curve). */
static double grid_i_exact(const TubeSpec* s, double v, double* slope)
{
    const double a = s->cg * v;
    double sp, sig, base;
    if (a < -12.0) { /* below about -0.9 V for a 12AX7: no measurable current */
        *slope = 0.0;
        return 0.0;
    }
    sp = a > 30.0 ? a : log1p(exp(a));
    sig = a > 30.0 ? 1.0 : 1.0 / (1.0 + exp(-a));
    base = sp / s->cg;
    *slope = s->gg * s->xi * pow(base, s->xi - 1.0) * sig;
    return s->gg * pow(base, s->xi);
}

/* The same curve tabulated, since it is evaluated inside every grid's Newton loop. */
#define GRID_N 2048
typedef struct {
    const TubeSpec* s;
    double u0, du; /* table over cg * v from -12 to 60 */
    float i[GRID_N];
} GridLut;

static void grid_lut_build(GridLut* g, const TubeSpec* s)
{
    int k;
    double sl;
    g->s = s;
    g->u0 = -12.0;
    g->du = 72.0 / (GRID_N - 1);
    for (k = 0; k < GRID_N; ++k)
        g->i[k] = (float)grid_i_exact(s, (g->u0 + k * g->du) / s->cg, &sl);
}

static inline double grid_i(const GridLut* g, double v, double* slope)
{
    const double f = (g->s->cg * v - g->u0) / g->du;
    int k;
    double w;
    if (f <= 0.0) {
        *slope = 0.0;
        return 0.0;
    }
    if (f >= GRID_N - 1)
        return grid_i_exact(g->s, v, slope);
    k = (int)f;
    w = f - k;
    *slope = (g->i[k + 1] - g->i[k]) * g->s->cg / g->du;
    return g->i[k] + w * (g->i[k + 1] - g->i[k]);
}

/* Plate current over grid voltage (x) and plate voltage (y), bilinear between points. */
typedef struct {
    float* t;
    int nx, ny;
    double x0, y0, dx, dy, idx, idy;
} TubeLut;

static int lut_build(TubeLut* L, const TubeSpec* s, double x0, double x1, int nx, double y0, double y1, int ny)
{
    int i, j;
    L->t = (float*)malloc(sizeof(float) * (size_t)nx * (size_t)ny);
    if (!L->t)
        return 0;
    L->nx = nx;
    L->ny = ny;
    L->x0 = x0;
    L->y0 = y0;
    L->dx = (x1 - x0) / (nx - 1);
    L->dy = (y1 - y0) / (ny - 1);
    L->idx = 1.0 / L->dx;
    L->idy = 1.0 / L->dy;
    for (j = 0; j < ny; ++j)
        for (i = 0; i < nx; ++i)
            L->t[j * nx + i] = (float)koren(s, x0 + i * L->dx, y0 + j * L->dy);
    return 1;
}

static void lut_free(TubeLut* L)
{
    free(L->t);
    L->t = NULL;
}

/* Current at (vg, vp), with its slopes: gm = dI/dvg and gp = dI/dvp. Outside the table the
 * edge cell's plane carries on, clamped at zero current. */
static inline double lut_eval(const TubeLut* L, double vg, double vp, double* gm, double* gp)
{
    const double fx = (vg - L->x0) * L->idx, fy = (vp - L->y0) * L->idy;
    double u, w, i00, i10, i01, i11, v;
    const int ix = !(fx >= 0.0) ? 0 : (fx > L->nx - 2 ? L->nx - 2 : (int)fx);
    const int iy = !(fy >= 0.0) ? 0 : (fy > L->ny - 2 ? L->ny - 2 : (int)fy);
    const float* r;
    u = fx - ix;
    w = fy - iy;
    r = L->t + iy * L->nx + ix;
    i00 = r[0];
    i10 = r[1];
    i01 = r[L->nx];
    i11 = r[L->nx + 1];
    *gm = ((i10 - i00) * (1.0 - w) + (i11 - i01) * w) * L->idx;
    *gp = ((i01 - i00) * (1.0 - u) + (i11 - i10) * u) * L->idy;
    v = (i00 * (1.0 - u) + i10 * u) * (1.0 - w) + (i01 * (1.0 - u) + i11 * u) * w;
    if (v <= 0.0) {
        *gm = *gp = 0.0;
        return 0.0;
    }
    return v;
}

/* The pentode with a moving screen voltage. Koren's pentode equation separates:
 *   Ip = (2 / Kg1) Vg2^Ex G(Vg1 / Vg2) atan(Vp / Kvb),  G(r) = (ln(1 + e^(Kp (1/mu + r))) / Kp)^Ex
 * so G is tabulated over r and the rest is computed. The screen takes (2 / Kg2) Vg2^Ex G
 * (Koren's screen term) plus half the current the plate fails to collect when its voltage
 * falls into the knee; that second part is an estimate of the screen-current rise that limits
 * a real output stage when it clips. */
#define PENT_N 2048
typedef struct {
    const TubeSpec* s;
    double r0, dr, idr;
    float g[PENT_N];
} PentLut;

static void pent_build(PentLut* L, const TubeSpec* s)
{
    int k;
    L->s = s;
    L->r0 = -0.6;
    L->dr = 0.8 / (PENT_N - 1);
    L->idr = 1.0 / L->dr;
    for (k = 0; k < PENT_N; ++k) {
        const double a = s->kp * (1.0 / s->mu + L->r0 + k * L->dr);
        const double g = (a > 30.0 ? a : log1p(exp(a))) / s->kp;
        L->g[k] = (float)pow(g, s->ex);
    }
}

/* Plate current for one valve; vg2ex = vg2^Ex, computed once per sample by the caller.
 * Returns the plate current, with *gm = dIp/dVg1, *gp = dIp/dVp and *ig2 the screen current. */
static inline double pent_eval(const PentLut* L, double vg1, double vp, double vg2, double vg2ex,
                               double* gm, double* gp, double* ig2)
{
    const TubeSpec* s = L->s;
    const double f = (vg1 / vg2 - L->r0) * L->idr;
    double G, dG, A, base;
    int k;
    if (!(f > 0.0)) {
        *gm = *gp = *ig2 = 0.0;
        return 0.0;
    }
    k = f > PENT_N - 2 ? PENT_N - 2 : (int)f;
    dG = (L->g[k + 1] - L->g[k]) * L->idr;
    G = L->g[k] + (f - k) * L->dr * dG;
    if (G <= 0.0) {
        *gm = *gp = *ig2 = 0.0;
        return 0.0;
    }
    if (vp < 0.0)
        vp = 0.0;
    A = atan(vp / s->kvb);
    base = 2.0 / s->kg1 * vg2ex;
    *gm = base * dG / vg2 * A;
    *gp = base * G * s->kvb / (s->kvb * s->kvb + vp * vp);
    *ig2 = 2.0 / s->kg2 * vg2ex * G + 0.5 * base * G * (1.5707963267948966 - A);
    return base * G * A;
}

/* Grid voltage against a Thevenin source (voc behind rth), with the cathode at vk.
 * Returns the grid voltage; *ig gets the grid current (flowing into the grid). */
static inline double grid_solve(const GridLut* s, double voc, double rth, double vk, double* ig)
{
    double vg = voc, slope, i;
    int k;
    i = grid_i(s, vg - vk, &slope);
    if (i < 1e-12) {
        *ig = 0.0;
        return vg;
    }
    for (k = 0; k < 8; ++k) {
        const double f = vg - voc + rth * i;
        const double d = 1.0 + rth * slope;
        const double step = f / d;
        vg -= step;
        i = grid_i(s, vg - vk, &slope);
        if (fabs(step) < 1e-7)
            break;
    }
    *ig = i;
    return vg;
}

/* A triode gain stage: plate into a network port, cathode to ground through rk1 in series with
 * rk2 in parallel with ck (ck = 0: no bypass cap). */
typedef struct {
    const TubeLut* lut;
    double rk1, rk2, ck;
    double gc;        /* cathode cap companion conductance */
    double vc, ic;    /* cathode cap voltage and current */
    double ip, vk, vg, ig;
} Stage;

static void stage_setup(Stage* st, const TubeLut* lut, double rk1, double rk2, double ck, double T)
{
    st->lut = lut;
    st->rk1 = rk1;
    st->rk2 = rk2;
    st->ck = ck;
    st->gc = ck > 0.0 ? 2.0 * ck / T : 0.0;
}

/* The grid against its Thevenin source (voc, rth) with the cathode at vk: the grid voltage,
 * the grid current, and how much of a cathode move the grid follows (s, from grid current). */
static inline double grid_at(const GridLut* gl, double voc, double rth, double vk, double* ig, double* s)
{
    double slope;
    const double vg = grid_solve(gl, voc, rth, vk, ig);
    if (*ig > 0.0) {
        grid_i(gl, vg - vk, &slope);
        *s = rth * slope / (1.0 + rth * slope);
    } else {
        *s = 0.0;
    }
    return vg;
}

/* Plate current tolerance. The first stage feeds about 100 dB of gain with every control up,
 * so a 2 nA error there (0.2 mV on its plate) came out as volts at the speaker. */
#ifndef STAGE_TOL
#    define STAGE_TOL 1e-12
#endif
/* The followers have no voltage gain and come late in the chain. */
#ifndef FOLLOWER_TOL
#    define FOLLOWER_TOL 1e-10
#endif

/* One Newton step on a current whose residual f rises with it, kept inside the bracket [lo, hi]
 * of the points already tried. Where the grid starts or stops conducting the slope changes
 * sharply, and a plain step would jump past the root and back for ever; a step that leaves
 * the bracket bisects it instead. Returns the size of the step. */
static inline double newton_bracket(double* x, double f, double d, double* lo, double* hi)
{
    const double x0 = *x;
    double x1 = x0 - f / d;
    if (f == 0.0)
        return 0.0;
    if (f < 0.0)
        *lo = x0;
    else
        *hi = x0;
    if (x1 < 0.0)
        x1 = 0.0; /* cut off: reachable in one step while nothing below it has been tried */
    if (!(x1 >= *lo && x1 < *hi) || (x1 == *lo && x1 > 0.0))
        x1 = 0.5 * (*lo + *hi);
    *x = x1;
    return x0 - x1;
}

/* Solves the plate current, with the grid fed from (voc, rth) and the plate port's Thevenin
 * source (vh, z). The grid current is found in the same iteration, against this sample's
 * cathode voltage. dc = 1: cathode cap open (DC operating point). Commits the cathode state. */
static double stage_solve(Stage* st, const GridLut* gl, double voc, double rth, double vh, double z, int dc)
{
    const double gc = dc ? 0.0 : st->gc;
    const double ieq = dc ? 0.0 : st->gc * st->vc + st->ic;
    const double gk2 = 1.0 / st->rk2 + gc;
    const double rkx = st->rk1 + 1.0 / gk2;
    double ip = st->ip, ig = st->ig, vk = 0.0, vc = 0.0, vg = voc, s = 0.0, lo = 0.0, hi = 1.0;
    int k;
    for (k = 0; k < 40; ++k) {
        double gm, gp, f, d, step, t, vk0;
        /* The cathode sits at vk0 + rkx * ig, so the grid sees the cathode resistance in series
         * with its own source: solve the grid-cathode voltage directly. */
        vk0 = ip * rkx + ieq / gk2;
        vg = grid_at(gl, voc - vk0, rth + rkx, 0.0, &ig, &s);
        vk = vk0 + rkx * ig;
        vg += vk;
        t = lut_eval(st->lut, vg - vk, vh - z * ip - vk, &gm, &gp);
        f = ip - t;
        d = 1.0 + gm * rkx * (1.0 - s) + gp * (z + rkx);
        step = newton_bracket(&ip, f, d, &lo, &hi);
        if (fabs(step) < STAGE_TOL)
            break;
    }
    {
        const double ik = ip + ig;
        vc = (ik + ieq) / gk2;
        vk = ik * st->rk1 + vc;
    }
    st->ic = dc ? 0.0 : st->gc * vc - ieq;
    st->vc = vc;
    st->vk = vk;
    st->vg = vg;
    st->ig = ig;
    st->ip = ip;
    return ip;
}

/* A cathode follower: plate to a supply vs through rp, cathode into a network port (vh, z),
 * grid fed from (voc, rth) and solved in the same iteration. */
typedef struct {
    const TubeLut* lut;
    double vs, rp;
    double ip, vk, vg, ig;
} Follower;

static double follower_solve(Follower* f, const GridLut* gl, double voc, double rth, double vh, double z)
{
    double ip = f->ip, ig = f->ig, vk = vh, vg = voc, s = 0.0, lo = 0.0, hi = 1.0;
    int k;
    for (k = 0; k < 40; ++k) {
        double gm, gp, t, r, d, step, vk0;
        vk0 = vh + z * ip;
        vg = grid_at(gl, voc - vk0, rth + z, 0.0, &ig, &s);
        vk = vk0 + z * ig;
        vg += vk;
        t = lut_eval(f->lut, vg - vk, f->vs - f->rp * ip - vk, &gm, &gp);
        r = ip - t;
        d = 1.0 + gm * z * (1.0 - s) + gp * (z + f->rp);
        step = newton_bracket(&ip, r, d, &lo, &hi);
        if (fabs(step) < FOLLOWER_TOL)
            break;
    }
    f->ip = ip;
    f->ig = ig;
    f->vk = vh + z * (ip + ig);
    f->vg = vg;
    return ip;
}

#endif
