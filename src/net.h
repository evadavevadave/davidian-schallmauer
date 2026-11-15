/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* A small linear circuit solver for the passive networks between the valves: coupling caps,
 * tone stacks, dividers, the speaker load and the feedback network.
 *
 * Each network is solved by nodal analysis with trapezoidal companion models for capacitors
 * and inductors (the same discretisation as the bilinear transform), so a network's response
 * is exact for its component values at the oversampled rate. Sources are Norton equivalents:
 * a voltage behind a resistor. A network driven by currents that switch off abruptly (the
 * output valves into the transformer) can use backward Euler instead, which damps the
 * ringing at the Nyquist frequency that the trapezoidal rule would leave undamped.
 *
 * The valves connect at "ports": nodes where a non-linear part injects current. For a port p,
 * the node voltages are x = xh + inv[.][p] * I, where xh is the solution with no injected
 * current (from the sources and the reactive parts' history) and inv the inverse of the
 * conductance matrix. So each valve sees its network as a Thevenin source (xh[p], inv[p][p])
 * and can be solved on its own, and the network is then updated with the currents found. */
#ifndef SH_NET_H
#define SH_NET_H

#include <math.h>
#include <string.h>

#define NET_MAXN 24
#define NET_MAXE 48
#define NET_GND (-1)

enum { NE_R, NE_C, NE_L, NE_SRC };

typedef struct {
    int type, a, b;
    double val;   /* ohms, farads or henries; NE_SRC: series resistance */
    double src;   /* NE_SRC: source voltage */
    double g;     /* companion conductance */
    double v, i;  /* last voltage across (a minus b) and current from a to b */
} NetElem;

typedef struct {
    int n, ne;
    double T;     /* sample period */
    int dirty;    /* values changed since the last inverse */
    int be;       /* backward Euler instead of trapezoidal */
    int nre, nsr; /* reactive parts and sources, listed by net_build */
    int re[NET_MAXE], sr[NET_MAXE];
    NetElem e[NET_MAXE];
    double inv[NET_MAXN][NET_MAXN];
    double b[NET_MAXN];
    double xh[NET_MAXN]; /* solution with no port currents */
    double x[NET_MAXN];  /* solution with them */
} Net;

static inline void net_init(Net* n, int nodes, double T)
{
    memset(n, 0, sizeof *n);
    n->n = nodes;
    n->T = T;
    n->dirty = 1;
}

static inline int net_add(Net* n, int type, int a, int b, double val)
{
    NetElem* e = &n->e[n->ne];
    memset(e, 0, sizeof *e);
    e->type = type;
    e->a = a;
    e->b = b;
    e->val = val;
    n->dirty = 1;
    return n->ne++;
}

static inline int net_r(Net* n, int a, int b, double r) { return net_add(n, NE_R, a, b, r); }
static inline int net_c(Net* n, int a, int b, double c) { return net_add(n, NE_C, a, b, c); }
static inline int net_l(Net* n, int a, int b, double l) { return net_add(n, NE_L, a, b, l); }
/* A voltage source v behind resistance r, from node a to ground. */
static inline int net_src(Net* n, int a, double r, double v)
{
    const int k = net_add(n, NE_SRC, a, NET_GND, r);
    n->e[k].src = v;
    return k;
}

/* Changes a resistor (or source resistance). Small changes are ignored to spare re-inverting. */
static inline void net_set_r(Net* n, int k, double r)
{
    if (r < 0.5)
        r = 0.5;
    if (fabs(r - n->e[k].val) > 1e-6 * r) {
        n->e[k].val = r;
        n->dirty = 1;
    }
}

static inline void stamp(Net* n, double (*m)[NET_MAXN], int a, int b, double g)
{
    (void)n;
    if (a >= 0)
        m[a][a] += g;
    if (b >= 0)
        m[b][b] += g;
    if (a >= 0 && b >= 0) {
        m[a][b] -= g;
        m[b][a] -= g;
    }
}

/* Builds and inverts the conductance matrix. dc = 1: capacitors open, inductors nearly short. */
static inline void net_build(Net* n, int dc)
{
    double m[NET_MAXN][NET_MAXN];
    const int N = n->n;
    int i, j, k;
    memset(m, 0, sizeof m);
    for (i = 0; i < N; ++i)
        m[i][i] = 1e-12; /* leakage, so no node floats */
    for (k = 0; k < n->ne; ++k) {
        NetElem* e = &n->e[k];
        switch (e->type) {
        case NE_R:
        case NE_SRC: e->g = 1.0 / e->val; break;
        case NE_C: e->g = dc ? 0.0 : (n->be ? 1.0 : 2.0) * e->val / n->T; break;
        case NE_L: e->g = dc ? 1e3 : n->T / ((n->be ? 1.0 : 2.0) * e->val); break;
        }
        stamp(n, m, e->a, e->b, e->g);
    }
    n->nre = n->nsr = 0;
    for (k = 0; k < n->ne; ++k) {
        if (n->e[k].type == NE_SRC)
            n->sr[n->nsr++] = k;
        else if (n->e[k].type != NE_R)
            n->re[n->nre++] = k;
    }
    /* Gauss-Jordan with partial pivoting. */
    for (i = 0; i < N; ++i)
        for (j = 0; j < N; ++j)
            n->inv[i][j] = (i == j) ? 1.0 : 0.0;
    for (i = 0; i < N; ++i) {
        int p = i;
        double d;
        for (j = i + 1; j < N; ++j)
            if (fabs(m[j][i]) > fabs(m[p][i]))
                p = j;
        if (p != i) {
            for (j = 0; j < N; ++j) {
                double t = m[i][j];
                m[i][j] = m[p][j];
                m[p][j] = t;
                t = n->inv[i][j];
                n->inv[i][j] = n->inv[p][j];
                n->inv[p][j] = t;
            }
        }
        d = 1.0 / m[i][i];
        for (j = 0; j < N; ++j) {
            m[i][j] *= d;
            n->inv[i][j] *= d;
        }
        for (k = 0; k < N; ++k) {
            const double f = m[k][i];
            if (k == i || f == 0.0)
                continue;
            for (j = 0; j < N; ++j) {
                m[k][j] -= f * m[i][j];
                n->inv[k][j] -= f * n->inv[i][j];
            }
        }
    }
    n->dirty = 0;
}

/* The history solution xh: sources plus the reactive parts' companion currents. In DC mode
 * (after net_build(n, 1)) only the sources count. */
static inline void net_hist(Net* n, int dc)
{
    const int N = n->n;
    int i, j, k;
    for (i = 0; i < N; ++i)
        n->b[i] = 0.0;
    for (k = 0; k < n->nsr; ++k) {
        const NetElem* e = &n->e[n->sr[k]];
        n->b[e->a] += e->src * e->g;
    }
    if (!dc) {
        for (k = 0; k < n->nre; ++k) {
            const NetElem* e = &n->e[n->re[k]];
            /* capacitor: +ieq into a; inductor: -ieq */
            const double ieq = n->be ? (e->type == NE_C ? e->g * e->v : -e->i)
                                     : (e->type == NE_C ? e->g * e->v + e->i : -(e->i + e->g * e->v));
            if (e->a >= 0) n->b[e->a] += ieq;
            if (e->b >= 0) n->b[e->b] -= ieq;
        }
    }
    for (i = 0; i < N; ++i) {
        const double* row = n->inv[i];
        double s = 0.0;
        for (j = 0; j < N; ++j)
            s += row[j] * n->b[j];
        n->xh[i] = s;
        n->x[i] = s;
    }
}

/* Adds current i injected into port node p to the solution. */
static inline void net_inject(Net* n, int p, double i)
{
    int k;
    if (i == 0.0)
        return;
    for (k = 0; k < n->n; ++k)
        n->x[k] += n->inv[k][p] * i;
}

static inline double nv(const Net* n, int node) { return node >= 0 ? n->x[node] : 0.0; }

/* Moves the reactive parts on to this sample's solution. dc = 1 sets them to the DC
 * operating point instead: capacitors charged, inductors carrying their DC current. */
static inline void net_commit(Net* n, int dc)
{
    int k;
    for (k = 0; k < n->nre; ++k) {
        NetElem* e = &n->e[n->re[k]];
        const double v = nv(n, e->a) - nv(n, e->b);
        if (e->type == NE_C) {
            e->i = dc ? 0.0 : (n->be ? e->g * (v - e->v) : e->g * v - (e->g * e->v + e->i));
            e->v = v;
        } else {
            e->i = dc ? v * 1e3 : (n->be ? e->i + e->g * v : e->g * v + (e->i + e->g * e->v));
            e->v = dc ? 0.0 : v;
        }
    }
}

#endif
