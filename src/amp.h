/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* The amplifier circuit, from the guitar input to the speaker terminals, run at the
 * oversampled rate. Part numbers refer to the reference schematic (two sheets: preamp and
 * power amp). See amp.c for the circuit and README.md for the values that were estimated. */
#ifndef SH_AMP_H
#define SH_AMP_H

#include "net.h"
#include "tube.h"

/* Pot rotations, 0 = fully anticlockwise, 1 = fully clockwise, after the pot's taper. */
typedef struct {
    double channel; /* 0 clean, 1 high gain; moves smoothly, like the opto switching */
    double cl_gain, cl_bass, cl_mid, cl_treble, cl_pres, cl_vol;
    double hg_gain, hg_bass, hg_mid, hg_treble, hg_pres, hg_vol;
    double master;
} AmpPots;

typedef struct {
    double T;
    TubeLut tri;
    PentLut pen;
    GridLut gtri, gpen;
    Net n0, n1, n1h, n1c, n2, n3, n4, n5h, n5c, n5r, n6, n7, n8, n9;
    Stage v1a, v1b, v2b, v3a, v4b, v5b;
    Follower v3b, v5a;

    /* Phase inverter (long-tailed pair, V6) */
    double pia, pib, pi_vk, pi_vx, pi_vg, pi_ig, c105;
    double pi_voc, pi_rth, ip5b; /* the inverter grid's source, and V5b's current, this sample */
    double el_ga, el_gb;  /* the EL34 grids (each side's pair) */
    /* Power stage (V7-V10, two EL34s each side) */
    double vbias, vhalf, is, it, ig_a, ig_b, ia_side, ib_side;
    double bplus, v0;
    double vscr;          /* screen supply, after the choke */
    double vg2a, vg2b;    /* each side's screen voltage, after its 1k */
    double g2_coef;       /* how far the screen voltages move towards their target each sample */
    double ig2a, ig2b;    /* screen current per valve */
    /* Supply nodes: screens/PI, then the preamp chain through R33, R34, R32 */
    double s_pi, s1, s2, s3;
    double vj; /* V5a bootstrap junction (R104/R100), for R103 */

    /* Element indices of the parts that move */
    int src_in, src_r5, src_r7, src_r15, src_rx2, src_rx6, src_r103, src_r106, src_r112;
    int src_r111, src_r118, src_r27, src_r28;
    int src_hin, src_cin, src_hout, src_cout, ldr_fb;
    double r_ldr_h, r_ldr_c, r_rel_h, r_rel_c; /* channel optos and relay contacts */
    double rp1a;         /* plate resistance of V1a at its operating point */
    int run_hg, run_cl;  /* channels being computed */
    int p1t, p1b, p7t, p7b, p8, p2a, p3a, p4a, p4b, p6a, p6b;
    int p9, p10a, p10b, p11, p12a, p12b, p13a, p13b, p5;

    double out;   /* speaker terminal voltage */
    double power_coef;
    double power; /* output power, watts, smoothed */
} Amp;

int amp_init(Amp* a, double fs);          /* allocates the valve tables */
void amp_free(Amp* a);
void amp_set_rate(Amp* a, double fs);      /* oversampled rate; follow with amp_reset */
void amp_set_pots(Amp* a, const AmpPots* p); /* marks the networks to rebuild */
void amp_update(Amp* a);                  /* rebuilds the networks whose parts moved */
void amp_reset(Amp* a, const AmpPots* p); /* DC operating point, then a short settle */
double amp_tick(Amp* a, double vin);      /* one sample: input volts in, speaker volts out */

#endif
