/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* An optional, rough 4x12 closed-back cabinet curve, so the amp can be monitored without an
 * impulse-response loader. It is not part of the amplifier circuit: switch it off to get the
 * speaker-terminal voltage (what a load box's line output gives) and use your own cabinet.
 * Biquads from Robert Bristow-Johnson's Audio EQ Cookbook. */
#ifndef SH_CAB_H
#define SH_CAB_H

#include <math.h>
#include <string.h>

typedef struct { double b0, b1, b2, a1, a2, z1, z2; } Biquad;

enum { BQ_LP, BQ_HP, BQ_PEAK };

static void bq_design(Biquad* q, int type, double fs, double f, double Q, double gain_db)
{
    const double w = 2.0 * 3.14159265358979323846 * f / fs;
    const double cw = cos(w), sw = sin(w), alpha = sw / (2.0 * Q);
    const double A = pow(10.0, gain_db / 40.0);
    double b0, b1, b2, a0, a1, a2;
    switch (type) {
    case BQ_LP:
        b0 = (1.0 - cw) / 2.0; b1 = 1.0 - cw; b2 = b0;
        a0 = 1.0 + alpha; a1 = -2.0 * cw; a2 = 1.0 - alpha;
        break;
    case BQ_HP:
        b0 = (1.0 + cw) / 2.0; b1 = -(1.0 + cw); b2 = b0;
        a0 = 1.0 + alpha; a1 = -2.0 * cw; a2 = 1.0 - alpha;
        break;
    default:
        b0 = 1.0 + alpha * A; b1 = -2.0 * cw; b2 = 1.0 - alpha * A;
        a0 = 1.0 + alpha / A; a1 = -2.0 * cw; a2 = 1.0 - alpha / A;
        break;
    }
    q->b0 = b0 / a0;
    q->b1 = b1 / a0;
    q->b2 = b2 / a0;
    q->a1 = a1 / a0;
    q->a2 = a2 / a0;
    q->z1 = q->z2 = 0.0;
}

static inline double bq_tick(Biquad* q, double x)
{
    const double y = q->b0 * x + q->z1;
    q->z1 = q->b1 * x - q->a1 * y + q->z2;
    q->z2 = q->b2 * x - q->a2 * y;
    return y;
}

#define CAB_STAGES 7

typedef struct { Biquad s[CAB_STAGES]; } Cab;

static void cab_init(Cab* c, double fs)
{
    /* Low frequencies roll off below the closed-box resonance; a lift at the resonance, a dip
     * in the low mids, the cone's upper-mid presence peak and a steep fall above 5 kHz. Cut
     * offs above Nyquist are pulled in for low sample rates. */
    const double top = fs * 0.45;
    bq_design(&c->s[0], BQ_HP, fs, 70.0, 0.71, 0.0);
    bq_design(&c->s[1], BQ_PEAK, fs, 110.0, 1.4, 3.5);
    bq_design(&c->s[2], BQ_PEAK, fs, 420.0, 1.0, -3.5);
    bq_design(&c->s[3], BQ_PEAK, fs, 2600.0, 1.5, 4.5);
    bq_design(&c->s[4], BQ_LP, fs, 5200.0 < top ? 5200.0 : top, 0.71, 0.0);
    bq_design(&c->s[5], BQ_LP, fs, 6500.0 < top ? 6500.0 : top, 1.1, 0.0);
    bq_design(&c->s[6], BQ_PEAK, fs, 8500.0 < top ? 8500.0 : top, 2.0, -6.0);
}

static void cab_reset(Cab* c)
{
    int i;
    for (i = 0; i < CAB_STAGES; ++i)
        c->s[i].z1 = c->s[i].z2 = 0.0;
}

static inline double cab_tick(Cab* c, double x)
{
    int i;
    for (i = 0; i < CAB_STAGES; ++i)
        x = bq_tick(&c->s[i], x);
    return x;
}

#endif
