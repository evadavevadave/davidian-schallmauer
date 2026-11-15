/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* 2x up- and downsampling with polyphase IIR half-band filters: two chains of first-order
 * allpass sections whose coefficients come from an elliptic half-band design (the method of
 * Valenzuela and Constantinides, as used in Laurent de Soras's HIIR library; this is an
 * independent implementation of the published formulas). Cascade two for 4x. The filters
 * are minimum phase, so the plugin adds no latency. */
#ifndef SH_OVERSAMPLE_H
#define SH_OVERSAMPLE_H

#include <math.h>
#include <string.h>

#define HB_COEFS 8 /* 8 allpass coefficients, transition band 0.04: about 70 dB of rejection */

typedef struct {
    double c[HB_COEFS];
    double x[HB_COEFS], y[HB_COEFS];
} HalfBand;

static double hb_ipow(double q, int n)
{
    double r = 1.0;
    while (n-- > 0)
        r *= q;
    return r;
}

static void hb_design(double* coef, int n, double transition)
{
    const double pi = 3.14159265358979323846;
    double k = tan((1.0 - transition * 2.0) * pi / 4.0), kksqrt, e, e2, e4, q;
    const int order = n * 2 + 1;
    int idx;
    k *= k;
    kksqrt = pow(1.0 - k * k, 0.25);
    e = 0.5 * (1.0 - kksqrt) / (1.0 + kksqrt);
    e2 = e * e;
    e4 = e2 * e2;
    q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));
    for (idx = 0; idx < n; ++idx) {
        const int c = idx + 1;
        double num = 0.0, den = 0.0, t, ww, wwsq, x;
        int i = 0, j = 1;
        do {
            t = hb_ipow(q, i * (i + 1)) * sin((i * 2 + 1) * c * pi / order) * j;
            num += t;
            j = -j;
            ++i;
        } while (fabs(t) > 1e-100 && i < 100);
        i = 1;
        j = -1;
        do {
            t = hb_ipow(q, i * i) * cos(i * 2 * c * pi / order) * j;
            den += t;
            j = -j;
            ++i;
        } while (fabs(t) > 1e-100 && i < 100);
        num *= pow(q, 0.25);
        den += 0.5;
        ww = num / den;
        wwsq = ww * ww;
        x = sqrt((1.0 - wwsq * k) * (1.0 - wwsq / k)) / (1.0 + wwsq);
        coef[idx] = (1.0 - x) / (1.0 + x);
    }
}

static void hb_init(HalfBand* h)
{
    memset(h, 0, sizeof *h);
    hb_design(h->c, HB_COEFS, 0.04);
}

static void hb_reset(HalfBand* h)
{
    memset(h->x, 0, sizeof h->x);
    memset(h->y, 0, sizeof h->y);
}

/* One input sample in, two output samples out. */
static inline void hb_up(HalfBand* h, double in, double* o0, double* o1)
{
    double even = in, odd = in;
    int i;
    for (i = 0; i < HB_COEFS; i += 2) {
        const double t0 = (even - h->y[i]) * h->c[i] + h->x[i];
        const double t1 = (odd - h->y[i + 1]) * h->c[i + 1] + h->x[i + 1];
        h->x[i] = even;
        h->x[i + 1] = odd;
        h->y[i] = t0;
        h->y[i + 1] = t1;
        even = t0;
        odd = t1;
    }
    *o0 = even;
    *o1 = odd;
}

/* Two input samples in (in0 first), one output sample out. */
static inline double hb_down(HalfBand* h, double in0, double in1)
{
    double s0 = in1, s1 = in0;
    int i;
    for (i = 0; i < HB_COEFS; i += 2) {
        const double t0 = (s0 - h->y[i]) * h->c[i] + h->x[i];
        const double t1 = (s1 - h->y[i + 1]) * h->c[i + 1] + h->x[i + 1];
        h->x[i] = s0;
        h->x[i + 1] = s1;
        h->y[i] = t0;
        h->y[i + 1] = t1;
        s0 = t0;
        s1 = t1;
    }
    return 0.5 * (s0 + s1);
}

#endif
