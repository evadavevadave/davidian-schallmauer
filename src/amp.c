/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* The amplifier circuit: passive networks (net.h) joined by eleven valves (tube.h).
 *
 *   n0   input jack, R1, R2                                   -> V1a grid
 *   n1   V1a plate, R5, CX2; the optos OC1/OC2 then feed
 *   n1h  the high-gain input: K5, K4, gain P1, C201/C202, KX2, R9, R8   -> V1b grid
 *   n1c  the clean input: C303, C305, C306, R301-R303, RX5, C301, bass P8, gain P7, C302
 *                                                              -> V4b grid
 *   n2   V1b plate, R7, KX1, R12, R13, R14                    -> V2b grid
 *   n3   V2b plate, R15, K8, R18, R19                         -> V3a grid
 *   n4   V3a plate, RX2, direct to the V3b grid (cathode follower)
 *   n5h  V3b cathode, R21 and the high-gain tone stack, a British-style stack: R60, C206,
 *        treble P4, C203, bass P2, C204, middle P3; R70 from the treble wiper to volume P6
 *   n5c  V4b plate, RX6, CX5 and the clean tone stack: C309, treble P10, R305, C308, R306, C4,
 *        middle P9, C310, presence P11, R320, R308, C311, volume P12
 *   n5r  the channel relay RE101B, R102, C101, R103           -> V5a grid
 *   n6   V5a cathode (loop send), R104, R100, C100, loop return R108/C133, R109 -> V5b grid
 *   n7   V5b plate, R106, C102, RX4, master P13, C103, R112   -> phase inverter grid
 *   n8   phase inverter plates R111/R118, Cx7, C111, C104/C106, bias feeds R27/R28,
 *        grid stoppers                                        -> EL34 grids
 *   n9   output transformer secondary, speaker, and the feedback network: the feedback opto,
 *        C117, R126/C136, R121, C9/ES3, C208/C209 and the high-gain presence P5
 *
 * Each sample the valves are solved in signal order. A valve's grid current reaches the
 * network before it on the next sample, a few microseconds later at 4x oversampling. The
 * phase inverter, power valves and feedback loop are solved together. */
#include <stdlib.h>
#include <string.h>

#include "amp.h"

#ifndef AMP_SETTLE_S
#    define AMP_SETTLE_S 0.05 /* seconds run after the DC solution */
#endif
static const double amp_settle_s = AMP_SETTLE_S;

/* ---- Values read off the schematic (ohms, farads) ---- */
#define R1 100e3
#define R2 68e3
#define R5 180e3
#define CX2 100e-9
#define K5 2.2e-9
#define K4 330e-12
#define P1 475e3
#define C201_202 (3e-9 + 300e-12)
#define KX2 22e-9
#define R9 68e3
#define R8 68e3
#define R7 100e3
#define KX1 22e-9
#define R12 470e3
#define R13 330e3
#define R14 330e3
#define R16 1.5e3
#define R17 1.5e3
#define K12 2.2e-6
#define R10 2.4e3
#define K3 470e-9
#define R6 1.8e3
#define C3 4.7e-6
/* R15 reads "1K", which would leave V2b with no gain; every other plate load here is 100k
 * or more, so it is taken as 100k (a dropped "00"). */
#define R15 100e3
#define K8 22e-9
#define R18 470e3
#define R19 470e3
#define RX2 100e3
#define RX1 1e3
#define R21 100e3
#define R60 33e3
#define C206 330e-12
#define P4 470e3
#define R70 270e3
#define C203 47e-9
#define P2 1e6
#define C204 22e-9
#define P3 25e3
/* The drawing loops R70 from the treble wiper back to the treble pot's own end, and hangs the
 * volume pot P6 off the middle pot's wiper with nothing in the stack grounded. A published
 * hot-rodded British-style amp with the same stack takes the output from the treble wiper
 * through a series 220k into a 1M volume; here R70 (270k) is that series resistor, feeding P6
 * to ground, and the middle pot's far end is grounded. P6 reads "10K", but behind 270k that
 * would cap the channel at under 2 W with every knob up; it is taken as 1M, as in that amp,
 * which puts the channel a few dB above the clean one and reaches full power near the top of
 * the master, as the original does. */
#define P6 1e6
/* Clean channel */
#define R303 499e3
#define C305 680e-12
#define C306 3.3e-9
#define RX5 499e3
#define P8 220e3
#define R302 340e3
#define C303 150e-12
#define R301 100e3
#define C301 470e-12
#define P7 390e3
#define C302 82e-12
#define RX6 100e3
#define R26A 1e3
#define K16 10e-6
#define CX5 47e-9
#define C309 330e-12
#define P10 240e3
#define R305 100e3
#define C308 100e-9
#define R306 220e3
#define C4 22e-9   /* no value on the schematic: estimate */
#define P9 22e3
#define C310 1e-9  /* "???" on the schematic: estimate */
#define P11 270e3
#define R320 22e3
#define R308 360e3
#define C311 100e-12
#define P12 430e3
/* Loop and master */
#define R102 1e6
#define C101 100e-9
#define R103 1e6
#define RX3 22e3
#define R104 470.0
#define R100 22e3
#define C100 1e-6
#define R108 180e3
#define C133 22e-12
#define R109 56e3
#define R106 100e3
#define R110 1.8e3
#define C102 100e-9
#define RX4 180e3
#define P13 220e3
#define C103 30e-9
/* Phase inverter */
#define R112 1e6
#define R114 470.0
#define R123 10e3
#define R111 68e3
#define R118 82e3
#define CX7 68e-12
#define C111 270e-12
#define C104 100e-9
#define R27 220e3
#define R23 4.7e3
/* Feedback network */
#define C117 22e-6
#define R126 10e3
#define C136 100e-12 /* no value on the schematic: estimate */
#define R121 4.7e3
#define C9 22e-9     /* "??" on the schematic: estimate */
#define ES3 150.0
#define C208_209 (2.2e-6 + 10e-6)
#define P5 2.2e3
/* Supply dropping resistors */
#define R33 10e3
#define R34 10e3
#define R32 6.8e3

/* ---- Estimates (not on the schematic) ---- */
#define B_PLUS 475.0      /* idle plate supply */
#define R_CHOKE 100.0     /* choke L6 resistance to the screen supply */
#define C_SCR 50e-6       /* screen supply cap, C7/C8 in series */
#define R_G2 1e3          /* each EL34's screen resistor */
#define R_WIND 1.0        /* OT winding resistance, seen from the secondary */
#define C_WIND 50e-9      /* OT winding capacitance, seen from the secondary (0.5 nF at the primary) */
#define RES_R 40.0        /* rectifier and transformer winding resistance */
#define RES_C 470e-6      /* reservoir: C10-C13 */
#define PRE_I 0.015       /* preamp and phase inverter current, from the screen supply */
#define BIAS_I 0.025      /* idle current per EL34, from the owner's manual */
#define RAA 1700.0        /* plate-to-plate load */
#define Z_SPK 16.0        /* speaker tap and cabinet */
#define LP_PRI 25.0       /* OT primary inductance, henries */
#define F_LEAK 25e3       /* OT leakage corner */
#define R_CORE 2000.0     /* OT core loss, on the secondary */
#define LDR_ON 2e3        /* lit opto LDR */
#define LDR_OFF 20e6      /* dark opto LDR */
#define C_MILLER_PF(a, cgp) (((a) + 1.0) * (cgp) + 1.6)
/* Speaker: a 16 ohm 4x12 of 12" drivers in a closed box. Voice coil Re, inductance Le with eddy
 * loss Rle, and the box resonance as a parallel RLC (about 56 ohms at 105 Hz). */
#define SPK_RE 11.5
#define SPK_LE 1.5e-3
#define SPK_RLE 60.0
#define SPK_RRES 45.0
#define SPK_LRES 19.5e-3
#define SPK_CRES 117.8e-6

#define R_LOAD_O 470e3    /* the selected channel's input resistance, seen from CX2 */

/* Node numbers */
enum { N0_IN, N0_G, N0_N };
enum { N1_P, N1_O, N1_N };
enum { NH_IN, NH_GT, NH_GW, NH_KX2, NH_G, NH_N };
enum { NC_N0, NC_X, NC_N1, NC_M, NC_G, NC_G4B, NC_N };
enum { N2_P, N2_A, N2_G, N2_N };
enum { N4_P, N4_N };
enum { N5H_A, N5H_S, N5H_TT, N5H_W4, N5H_J1, N5H_J2, N5H_V, N5H_OUT, N5H_N };
enum { N5C_P, N5C_T, N5C_TT, N5C_W, N5C_V, N5C_U, N5C_Y, N5C_Z, N5C_Q, N5C_H, N5C_OUT, N5C_N };
enum { N5R_REL, N5R_G, N5R_N };
enum { N6_K, N6_J, N6_L, N6_G, N6_N };
enum { N7_P, N7_A, N7_T, N7_W, N7_G, N7_N };
enum { N8_PA, N8_PB, N8_CA, N8_GA, N8_CB, N8_GB, N8_N };
enum { N9_S0, N9_W, N9_S, N9_E1, N9_E2, N9_F1, N9_F2, N9_F, N9_C9, N9_P5, N9_N };

#define INV(n, i, j) ((n)->inv[(i)][(j)])

static double ldr(double lit) /* opto LDR resistance as its LED fades in, log scale */
{
    lit = lit < 0.0 ? 0.0 : (lit > 1.0 ? 1.0 : lit);
    return exp(log(LDR_OFF) + (log(LDR_ON) - log(LDR_OFF)) * lit);
}

static double relay(double on) /* the relay contact, with a short ramp so it doesn't click */
{
    on = on < 0.0 ? 0.0 : (on > 1.0 ? 1.0 : on);
    return exp(log(1e9) + (log(1.0) - log(1e9)) * on);
}

static double clamp01(double x) { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); }

/* The networks. Where a high resistance (a dark LDR, an open relay contact) or a high input
 * impedance separates two parts, they are solved as separate networks joined by a Thevenin
 * source, which saves most of the work. */
static void build_nets(Amp* a)
{
    const double T = a->T;
    Net* n;

    n = &a->n0;
    net_init(n, N0_N, T);
    a->src_in = net_src(n, N0_IN, 1e3, 0.0); /* the guitar, as 1k source impedance */
    net_r(n, N0_IN, NET_GND, R1);
    net_r(n, N0_IN, N0_G, R2);
    net_c(n, N0_G, NET_GND, C_MILLER_PF(60.0, 1.7 + 4.7) * 1e-12); /* Miller, with CX1 4.7p */

    /* V1a plate and CX2; the optos OC1/OC2 lead from O to the two channel inputs. */
    n = &a->n1;
    net_init(n, N1_N, T);
    a->src_r5 = net_src(n, N1_P, R5, 0.0);
    net_c(n, N1_P, N1_O, CX2);
    net_r(n, N1_O, NET_GND, R_LOAD_O);

    /* High-gain input: K5, K4, gain P1 with C201/C202, KX2, R9, R8 -> V1b grid */
    n = &a->n1h;
    net_init(n, NH_N, T);
    a->src_hin = net_src(n, NH_IN, LDR_OFF, 0.0);
    net_c(n, NH_IN, NH_GT, K5);
    net_c(n, NH_GT, NET_GND, K4);
    a->p1t = net_r(n, NH_GT, NH_GW, P1);
    a->p1b = net_r(n, NH_GW, NET_GND, P1);
    net_c(n, NH_GT, NH_GW, C201_202);
    net_c(n, NH_GW, NH_KX2, KX2);
    net_r(n, NH_KX2, NET_GND, R9);
    net_r(n, NH_KX2, NH_G, R8);
    net_c(n, NH_G, NET_GND, C_MILLER_PF(60.0, 1.7) * 1e-12);

    /* Clean input: R303, C305, C306/RX5, bass P8, R302, C303, R301/C301, gain P7, C302 -> V4b */
    n = &a->n1c;
    net_init(n, NC_N, T);
    a->src_cin = net_src(n, NC_N0, LDR_OFF, 0.0);
    net_r(n, NC_N0, NC_N1, R303);
    net_c(n, NC_N0, NC_N1, C305);
    net_c(n, NC_N0, NC_X, C306);
    net_r(n, NC_X, NC_N1, RX5);
    a->p8 = net_r(n, NC_N1, NET_GND, P8);
    net_r(n, NC_N1, NC_M, R302);
    net_c(n, NC_N0, NC_M, C303);
    net_r(n, NC_M, NC_G, R301);
    net_c(n, NC_M, NC_G, C301);
    a->p7t = net_r(n, NC_G, NC_G4B, P7);
    a->p7b = net_r(n, NC_G4B, NET_GND, P7);
    net_c(n, NC_G, NC_G4B, C302);
    net_c(n, NC_G4B, NET_GND, C_MILLER_PF(50.0, 1.7) * 1e-12);

    n = &a->n2;
    net_init(n, N2_N, T);
    a->src_r7 = net_src(n, N2_P, R7, 0.0);
    net_c(n, N2_P, N2_A, KX1);
    net_r(n, N2_A, NET_GND, R12);
    net_r(n, N2_A, N2_G, R13);
    net_r(n, N2_G, NET_GND, R14);
    net_c(n, N2_G, NET_GND, C_MILLER_PF(30.0, 1.7) * 1e-12);

    n = &a->n3;
    net_init(n, N2_N, T);
    a->src_r15 = net_src(n, N2_P, R15, 0.0);
    net_c(n, N2_P, N2_A, K8);
    net_r(n, N2_A, NET_GND, R18);
    net_r(n, N2_A, N2_G, R19);
    net_c(n, N2_G, NET_GND, C_MILLER_PF(35.0, 1.7) * 1e-12);

    n = &a->n4;
    net_init(n, N4_N, T);
    a->src_rx2 = net_src(n, N4_P, RX2, 0.0);
    net_c(n, N4_P, NET_GND, 5e-12); /* V3a plate and the follower's grid */

    /* High-gain tone stack from the V3b cathode: R21, R60, C206, treble P4 with R70, C203,
     * bass P2, C204, middle P3, volume P6. */
    n = &a->n5h;
    net_init(n, N5H_N, T);
    net_r(n, N5H_A, NET_GND, R21);
    net_c(n, N5H_A, N5H_TT, C206);              /* treble cap */
    a->p4a = net_r(n, N5H_TT, N5H_W4, P4);       /* treble pot, log taper */
    a->p4b = net_r(n, N5H_W4, N5H_J1, P4);
    net_r(n, N5H_A, N5H_S, R60);                 /* slope resistor */
    net_c(n, N5H_S, N5H_J1, C203);               /* bass cap */
    a->p2a = net_r(n, N5H_J1, N5H_J2, P2);       /* bass pot as a variable resistor */
    net_c(n, N5H_S, N5H_J2, C204);               /* middle cap */
    a->p3a = net_r(n, N5H_J2, NET_GND, P3);      /* middle pot as a variable resistor */
    net_r(n, N5H_W4, N5H_V, R70);                /* series feed from the treble wiper */
    a->p6a = net_r(n, N5H_V, N5H_OUT, P6);       /* channel volume */
    a->p6b = net_r(n, N5H_OUT, NET_GND, P6);

    /* Clean stage plate and tone stack: RX6, CX5, C309, treble P10, R305, C308, R306, C4,
     * middle P9, C310, presence P11, R320, R308/C311, volume P12. */
    n = &a->n5c;
    net_init(n, N5C_N, T);
    a->src_rx6 = net_src(n, N5C_P, RX6, 0.0);
    net_c(n, N5C_P, N5C_T, CX5);
    net_c(n, N5C_T, N5C_TT, C309);
    a->p10a = net_r(n, N5C_TT, N5C_W, P10);
    a->p10b = net_r(n, N5C_W, N5C_V, P10);
    net_r(n, N5C_T, N5C_U, R305);
    net_c(n, N5C_U, N5C_V, C308);
    net_r(n, N5C_V, NET_GND, R306);
    net_c(n, N5C_U, N5C_Y, C4);
    a->p9 = net_r(n, N5C_Y, NET_GND, P9);
    net_c(n, N5C_W, N5C_Z, C310);
    a->p11 = net_r(n, N5C_Z, N5C_Q, P11);
    net_r(n, N5C_Q, NET_GND, R320);
    net_r(n, N5C_W, N5C_H, R308);
    net_c(n, N5C_W, N5C_H, C311);
    a->p12a = net_r(n, N5C_H, N5C_OUT, P12);
    a->p12b = net_r(n, N5C_OUT, NET_GND, P12);

    /* The channel relay RE101B, R102, C101 and R103 -> V5a grid. Each channel's volume
     * wiper drives the relay contact as a Thevenin source. */
    n = &a->n5r;
    net_init(n, N5R_N, T);
    a->src_hout = net_src(n, N5R_REL, 1e9, 0.0);
    a->src_cout = net_src(n, N5R_REL, 1e9, 0.0);
    net_r(n, N5R_REL, NET_GND, R102);
    net_c(n, N5R_REL, N5R_G, C101);
    a->src_r103 = net_src(n, N5R_G, R103, 0.0);
    net_c(n, N5R_G, NET_GND, 4e-12);

    n = &a->n6;
    net_init(n, N6_N, T);
    net_r(n, N6_K, N6_J, R104);
    net_r(n, N6_J, NET_GND, R100);
    net_c(n, N6_K, N6_L, C100);
    net_r(n, N6_L, N6_G, R108);
    net_c(n, N6_L, N6_G, C133);
    net_r(n, N6_G, NET_GND, R109);
    net_c(n, N6_G, NET_GND, C_MILLER_PF(30.0, 1.7) * 1e-12);

    n = &a->n7;
    net_init(n, N7_N, T);
    a->src_r106 = net_src(n, N7_P, R106, 0.0);
    net_c(n, N7_P, N7_A, C102);
    net_r(n, N7_A, N7_T, RX4);
    a->p13a = net_r(n, N7_T, N7_W, P13);
    a->p13b = net_r(n, N7_W, NET_GND, P13);
    net_c(n, N7_W, N7_G, C103);
    a->src_r112 = net_src(n, N7_G, R112, 0.0);
    net_c(n, N7_G, NET_GND, 30e-12);

    /* Two EL34s per side share a coupling cap: their 4.7k stoppers in parallel. */
    n = &a->n8;
    net_init(n, N8_N, T);
    a->src_r111 = net_src(n, N8_PA, R111, 0.0);
    a->src_r118 = net_src(n, N8_PB, R118, 0.0);
    net_c(n, N8_PA, N8_PB, CX7);
    net_c(n, N8_PB, NET_GND, C111);
    net_c(n, N8_PA, N8_CA, C104);
    a->src_r27 = net_src(n, N8_CA, R27, 0.0);
    net_r(n, N8_CA, N8_GA, R23 / 2.0);
    net_c(n, N8_GA, NET_GND, 2.0 * 25e-12);
    net_c(n, N8_PB, N8_CB, C104);
    a->src_r28 = net_src(n, N8_CB, R27, 0.0);
    net_r(n, N8_CB, N8_GB, R23 / 2.0);
    net_c(n, N8_GB, NET_GND, 2.0 * 25e-12);

    n = &a->n9;
    net_init(n, N9_N, T);
    n->be = 1; /* driven by the output valves, which switch off abruptly */
    {
        const double n2 = RAA / Z_SPK;
        net_l(n, N9_S0, NET_GND, LP_PRI / n2);
        net_r(n, N9_S0, NET_GND, R_CORE);
        net_c(n, N9_S0, NET_GND, C_WIND);
        net_r(n, N9_S0, N9_W, R_WIND);
        net_l(n, N9_W, N9_S, Z_SPK / (2.0 * 3.14159265358979 * F_LEAK));
    }
    net_r(n, N9_S, N9_E1, SPK_RE);
    net_l(n, N9_E1, N9_E2, SPK_LE);
    net_r(n, N9_E1, N9_E2, SPK_RLE);
    net_r(n, N9_E2, NET_GND, SPK_RRES);
    net_l(n, N9_E2, NET_GND, SPK_LRES);
    net_c(n, N9_E2, NET_GND, SPK_CRES);
    a->ldr_fb = net_r(n, N9_S, N9_F1, LDR_OFF);
    net_c(n, N9_F1, N9_F2, C117);
    net_r(n, N9_F2, N9_F, R126);
    net_c(n, N9_F2, N9_F, C136);
    net_r(n, N9_F, NET_GND, R121);
    net_c(n, N9_F, N9_C9, C9);
    net_r(n, N9_C9, NET_GND, ES3);
    net_c(n, N9_F, N9_P5, C208_209);
    a->p5 = net_r(n, N9_P5, NET_GND, P5);
}

#define NNETS 14
static Net* net_at(Amp* a, int i)
{
    Net* nets[NNETS] = { &a->n0, &a->n1, &a->n1h, &a->n1c, &a->n2, &a->n3, &a->n4,
                         &a->n5h, &a->n5c, &a->n5r, &a->n6, &a->n7, &a->n8, &a->n9 };
    return nets[i];
}

static void pot(Net* n, int top, int bot, double r, double rot)
{
    net_set_r(n, top, (1.0 - rot) * r);
    net_set_r(n, bot, rot * r);
}

void amp_set_pots(Amp* a, const AmpPots* p)
{
    const double ch = clamp01(p->channel);
    const double t = clamp01((ch - 0.4) / 0.2);
    pot(&a->n1h, a->p1t, a->p1b, P1, clamp01(p->hg_gain));
    pot(&a->n1c, a->p7t, a->p7b, P7, clamp01(p->cl_gain));
    net_set_r(&a->n1c, a->p8, clamp01(p->cl_bass) * P8);
    pot(&a->n5h, a->p4a, a->p4b, P4, clamp01(p->hg_treble));
    net_set_r(&a->n5h, a->p2a, clamp01(p->hg_bass) * P2);
    net_set_r(&a->n5h, a->p3a, clamp01(p->hg_mid) * P3);
    pot(&a->n5h, a->p6a, a->p6b, P6, clamp01(p->hg_vol));
    pot(&a->n5c, a->p10a, a->p10b, P10, clamp01(p->cl_treble));
    net_set_r(&a->n5c, a->p9, clamp01(p->cl_mid) * P9);
    net_set_r(&a->n5c, a->p11, clamp01(p->cl_pres) * P11);
    pot(&a->n5c, a->p12a, a->p12b, P12, clamp01(p->cl_vol));
    pot(&a->n7, a->p13a, a->p13b, P13, clamp01(p->master));
    net_set_r(&a->n9, a->p5, clamp01(p->hg_pres) * P5);
    /* Channel: the input optos (OC1, OC2) and the feedback opto fade with their LEDs; the
     * relay (RE101B) changes over in the middle. */
    a->r_ldr_h = ldr(ch);
    a->r_ldr_c = ldr(1.0 - ch);
    a->r_rel_h = relay(t);
    a->r_rel_c = relay(1.0 - t);
    net_set_r(&a->n9, a->ldr_fb, ldr(ch));
    /* Once the switch-over is complete, the idle channel's valves are left alone. */
    a->run_hg = ch > 1e-4;
    a->run_cl = ch < 1.0 - 1e-4;
}

/* Rebuilds the networks whose parts moved, then the Thevenin sources between them. */
static void build_all(Amp* a, int dc, int force)
{
    int i;
    double z;
    for (i = 0; i < NNETS; ++i) {
        Net* n = net_at(a, i);
        if (force || n->dirty)
            net_build(n, dc);
    }
    /* At O: the network plus V1a's plate resistance across its port. */
    z = INV(&a->n1, N1_O, N1_O) - INV(&a->n1, N1_O, N1_P) * INV(&a->n1, N1_P, N1_O) / (INV(&a->n1, N1_P, N1_P) + a->rp1a);
    net_set_r(&a->n1h, a->src_hin, z + a->r_ldr_h);
    net_set_r(&a->n1c, a->src_cin, z + a->r_ldr_c);
    z = INV(&a->n5h, N5H_OUT, N5H_OUT);
    net_set_r(&a->n5r, a->src_hout, z + a->r_rel_h);
    z = INV(&a->n5c, N5C_OUT, N5C_OUT)
        - INV(&a->n5c, N5C_OUT, N5C_P) * INV(&a->n5c, N5C_P, N5C_OUT) / (INV(&a->n5c, N5C_P, N5C_P) + a->rp4b);
    net_set_r(&a->n5r, a->src_cout, z + a->r_rel_c);
    for (i = 0; i < NNETS; ++i) {
        Net* n = net_at(a, i);
        if (n->dirty)
            net_build(n, dc);
    }
}

void amp_update(Amp* a) { build_all(a, 0, 0); }

static void set_supplies(Amp* a)
{
    a->n1.e[a->src_r5].src = a->s3;
    a->n2.e[a->src_r7].src = a->s3;
    a->n3.e[a->src_r15].src = a->s3;
    a->n4.e[a->src_rx2].src = a->s2;
    a->n5c.e[a->src_rx6].src = a->s1;
    a->n7.e[a->src_r106].src = a->s1;
    a->n8.e[a->src_r111].src = a->s_pi;
    a->n8.e[a->src_r118].src = a->s_pi;
    a->n8.e[a->src_r27].src = a->vbias;
    a->n8.e[a->src_r28].src = a->vbias;
    a->v3b.vs = a->s2;
    a->v5a.vs = a->s1;
}

/* ---- The power section's equations ---- */

typedef struct {
    int dc;
    double ga_voc, ga_rth, c105, bplus; /* inverter grid a's source */
    double vg2a, vg2b, vg2a_ex, vg2b_ex; /* screens, held for the sample */
    double vah, vbh, goa, gob, vfh, s0h; /* history voltages, before this sample's currents */
    const Net *n8, *n9;
    const Amp* a;
    /* results of the last evaluation */
    double it, is, vf, vk, ig_a, ig_b, i_a, i_b, ig2a, ig2b, vga, ig_pi;
} PowerCtx;

/* Residuals r and Jacobian J for x = { phase inverter currents ia, ib; half-primary voltage v }.
 * The EL34 plates on side A sit at B+ + v and on side B at B+ - v, and the secondary carries
 * h (Ib - Ia): the winding sense that makes the loop negative feedback. */
static void power_eval(PowerCtx* c, const double* x, double* r, double (*J)[3])
{
    const double rt = R123 + R114;
    const double h = sqrt(RAA / Z_SPK) / 2.0;
    const Net* n8 = c->n8;
    const Net* n9 = c->n9;
    const double ia = x[0], ib = x[1], v = x[2], it = ia + ib;
    const double zFF = INV(n9, N9_F, N9_F), zFS = INV(n9, N9_F, N9_S0);
    const double zSF = INV(n9, N9_S0, N9_F), zSS = INV(n9, N9_S0, N9_S0);
    const double zaa = INV(n8, N8_PA, N8_PA), zab = INV(n8, N8_PA, N8_PB);
    const double zba = INV(n8, N8_PB, N8_PA), zbb = INV(n8, N8_PB, N8_PB);
    double gA, gB, igs, sa, sb, slope, gmA, gpA, gmB, gpB, IA, IB, is, vf, vk, vgb;
    double dgA[2], dgB[2], dIs[3], dVF[3], dvk[3], dvgb[3], gma, gpa, gmb, gpb, ta, tb, vga, sga;
    int k;

    /* EL34 grids, two valves per side behind half the stopper. */
    gA = grid_solve(&c->a->gpen, c->goa - INV(n8, N8_GA, N8_PA) * ia - INV(n8, N8_GA, N8_PB) * ib,
                    2.0 * INV(n8, N8_GA, N8_GA), 0.0, &igs);
    c->ig_a = 2.0 * igs;
    grid_i(&c->a->gpen, gA, &slope);
    sa = 1.0 / (1.0 + 2.0 * INV(n8, N8_GA, N8_GA) * slope);
    gB = grid_solve(&c->a->gpen, c->gob - INV(n8, N8_GB, N8_PA) * ia - INV(n8, N8_GB, N8_PB) * ib,
                    2.0 * INV(n8, N8_GB, N8_GB), 0.0, &igs);
    c->ig_b = 2.0 * igs;
    grid_i(&c->a->gpen, gB, &slope);
    sb = 1.0 / (1.0 + 2.0 * INV(n8, N8_GB, N8_GB) * slope);
    dgA[0] = -INV(n8, N8_GA, N8_PA) * sa;
    dgA[1] = -INV(n8, N8_GA, N8_PB) * sa;
    dgB[0] = -INV(n8, N8_GB, N8_PA) * sb;
    dgB[1] = -INV(n8, N8_GB, N8_PB) * sb;

    IA = 2.0 * pent_eval(&c->a->pen, gA, c->bplus + v, c->vg2a, c->vg2a_ex, &gmA, &gpA, &c->ig2a);
    IB = 2.0 * pent_eval(&c->a->pen, gB, c->bplus - v, c->vg2b, c->vg2b_ex, &gmB, &gpB, &c->ig2b);
    gmA *= 2.0;
    gpA *= 2.0;
    gmB *= 2.0;
    gpB *= 2.0;
    is = h * (IB - IA);
    dIs[0] = h * (gmB * dgB[0] - gmA * dgA[0]);
    dIs[1] = h * (gmB * dgB[1] - gmA * dgA[1]);
    dIs[2] = h * (-gpB - gpA);

    vf = c->vfh + zFF * it + zFS * is;
    vk = vf + rt * it;
    vgb = c->dc ? vf + R123 * it : vf + c->c105;
    for (k = 0; k < 3; ++k) {
        const double dit = k < 2 ? 1.0 : 0.0;
        dVF[k] = zFF * dit + zFS * dIs[k];
        dvk[k] = dVF[k] + rt * dit;
        dvgb[k] = dVF[k] + (c->dc ? R123 * dit : 0.0);
    }

    vga = grid_at(&c->a->gtri, c->ga_voc, c->ga_rth, vk, &c->ig_pi, &sga);
    c->vga = vga;
    ta = lut_eval(&c->a->tri, vga - vk, c->vah - zaa * ia - zab * ib - vk, &gma, &gpa);
    tb = lut_eval(&c->a->tri, vgb - vk, c->vbh - zba * ia - zbb * ib - vk, &gmb, &gpb);
    r[0] = ia - ta;
    r[1] = ib - tb;
    r[2] = v - h * (c->s0h + zSF * it + zSS * is);
    for (k = 0; k < 3; ++k) {
        const double dva = k == 0 ? -zaa : (k == 1 ? -zab : 0.0);
        const double dvb = k == 0 ? -zba : (k == 1 ? -zbb : 0.0);
        const double dit = k < 2 ? 1.0 : 0.0;
        J[0][k] = (k == 0) - (gma * (sga - 1.0) * dvk[k] + gpa * (dva - dvk[k]));
        J[1][k] = (k == 1) - (gmb * (dvgb[k] - dvk[k]) + gpb * (dvb - dvk[k]));
        J[2][k] = (k == 2) - h * (zSF * dit + zSS * dIs[k]);
    }
    c->it = it;
    c->is = is;
    c->vf = vf;
    c->vk = vk;
    c->i_a = IA;
    c->i_b = IB;
}

/* Solves J d = r by Gaussian elimination with partial pivoting. */
static int solve3(double (*J)[3], const double* r, double* d)
{
    double m[3][4];
    int i, j, k;
    for (i = 0; i < 3; ++i) {
        for (j = 0; j < 3; ++j)
            m[i][j] = J[i][j];
        m[i][3] = r[i];
    }
    for (i = 0; i < 3; ++i) {
        int p = i;
        for (j = i + 1; j < 3; ++j)
            if (fabs(m[j][i]) > fabs(m[p][i]))
                p = j;
        if (fabs(m[p][i]) < 1e-300)
            return 0;
        if (p != i)
            for (k = 0; k < 4; ++k) {
                const double t = m[i][k];
                m[i][k] = m[p][k];
                m[p][k] = t;
            }
        for (j = i + 1; j < 3; ++j) {
            const double f = m[j][i] / m[i][i];
            for (k = i; k < 4; ++k)
                m[j][k] -= f * m[i][k];
        }
    }
    for (i = 2; i >= 0; --i) {
        double s = m[i][3];
        for (j = i + 1; j < 3; ++j)
            s -= m[i][j] * d[j];
        d[i] = s / m[i][i];
    }
    return 1;
}

static double tick(Amp* a, double vin, int dc)
{
    const GridLut* gl = &a->gtri;
    const int hg = dc || a->run_hg, cl = dc || a->run_cl;
    double ip;

    /* Each valve is solved with its grid network (still open) and its plate or cathode network;
     * then the grid network takes the grid current and moves on. A grid's current reaches the
     * plate network before it through the coupling parts on the next sample. */

    /* V1a: grid in n0, plate in n1 */
    a->n0.e[a->src_in].src = vin;
    net_hist(&a->n0, dc);
    net_hist(&a->n1, dc);
    ip = stage_solve(&a->v1a, gl, a->n0.xh[N0_G], INV(&a->n0, N0_G, N0_G), a->n1.xh[N1_P],
                     INV(&a->n1, N1_P, N1_P), dc);
    net_inject(&a->n0, N0_G, -a->v1a.ig);
    net_commit(&a->n0, dc);
    net_inject(&a->n1, N1_P, -ip);
    net_commit(&a->n1, dc);

    if (hg) {
        Net *n1h = &a->n1h, *n2 = &a->n2, *n3 = &a->n3, *n4 = &a->n4, *n5h = &a->n5h;
        double ip1b, ip2b, ip3a, z4;
        /* V1b: grid in n1h (fed from O through the opto), plate in n2 */
        n1h->e[a->src_hin].src = a->n1.x[N1_O];
        net_hist(n1h, dc);
        net_hist(n2, dc);
        ip1b = stage_solve(&a->v1b, gl, n1h->xh[NH_G], INV(n1h, NH_G, NH_G),
                           n2->xh[N2_P] - INV(n2, N2_P, N2_G) * a->v2b.ig, INV(n2, N2_P, N2_P), dc);
        net_inject(n1h, NH_G, -a->v1b.ig);
        net_commit(n1h, dc);
        /* V2b: grid in n2, plate in n3 */
        net_hist(n3, dc);
        ip2b = stage_solve(&a->v2b, gl, n2->xh[N2_G] - INV(n2, N2_G, N2_P) * ip1b, INV(n2, N2_G, N2_G),
                           n3->xh[N2_P] - INV(n3, N2_P, N2_G) * a->v3a.ig, INV(n3, N2_P, N2_P), dc);
        net_inject(n2, N2_P, -ip1b);
        net_inject(n2, N2_G, -a->v2b.ig);
        net_commit(n2, dc);
        /* V3a: grid in n3, plate in n4 (which is also the V3b grid) */
        net_hist(n4, dc);
        z4 = INV(n4, N4_P, N4_P);
        ip3a = stage_solve(&a->v3a, gl, n3->xh[N2_G] - INV(n3, N2_G, N2_P) * ip2b, INV(n3, N2_G, N2_G),
                           n4->xh[N4_P] - z4 * a->v3b.ig, z4, dc);
        net_inject(n3, N2_P, -ip2b);
        net_inject(n3, N2_G, -a->v3a.ig);
        net_commit(n3, dc);
        /* V3b, the cathode follower: grid at the V3a plate, cathode into the tone stack n5h */
        net_hist(n5h, dc);
        follower_solve(&a->v3b, gl, n4->xh[N4_P] - z4 * ip3a, z4, n5h->xh[N5H_A], INV(n5h, N5H_A, N5H_A));
        net_inject(n4, N4_P, -ip3a - a->v3b.ig);
        net_commit(n4, dc);
        net_inject(n5h, N5H_A, a->v3b.ip + a->v3b.ig);
        net_commit(n5h, dc);
        a->n5r.e[a->src_hout].src = n5h->x[N5H_OUT];
    }

    if (cl) {
        Net *n1c = &a->n1c, *n5c = &a->n5c;
        /* V4b: grid in n1c (fed from O through the opto), plate into the clean tone stack n5c */
        n1c->e[a->src_cin].src = a->n1.x[N1_O];
        net_hist(n1c, dc);
        net_hist(n5c, dc);
        ip = stage_solve(&a->v4b, gl, n1c->xh[NC_G4B], INV(n1c, NC_G4B, NC_G4B), n5c->xh[N5C_P],
                         INV(n5c, N5C_P, N5C_P), dc);
        net_inject(n1c, NC_G4B, -a->v4b.ig);
        net_commit(n1c, dc);
        net_inject(n5c, N5C_P, -ip);
        net_commit(n5c, dc);
        a->n5r.e[a->src_cout].src = n5c->x[N5C_OUT];
    }

    /* V5a, the loop send follower: grid in n5r (after the relay), cathode in n6 */
    {
        Net *n5r = &a->n5r, *n6 = &a->n6, *n7 = &a->n7;
        double ik, ip5b;
        net_hist(n5r, dc);
        net_hist(n6, dc);
        follower_solve(&a->v5a, gl, n5r->xh[N5R_G], INV(n5r, N5R_G, N5R_G),
                       n6->xh[N6_K] - INV(n6, N6_K, N6_G) * a->v5b.ig, INV(n6, N6_K, N6_K));
        ik = a->v5a.ip + a->v5a.ig;
        net_inject(n5r, N5R_G, -a->v5a.ig);
        net_commit(n5r, dc);
        a->vj = n6->xh[N6_J] + INV(n6, N6_J, N6_K) * ik;
        /* V5b: grid in n6 (loop return), plate in n7 (master, to the phase inverter) */
        n7->e[a->src_r112].src = a->pi_vx;
        net_hist(n7, dc);
        ip5b = stage_solve(&a->v5b, gl, n6->xh[N6_G] + INV(n6, N6_G, N6_K) * ik, INV(n6, N6_G, N6_G),
                           n7->xh[N7_P] - INV(n7, N7_P, N7_G) * a->pi_ig, INV(n7, N7_P, N7_P), dc);
        net_inject(n6, N6_K, ik);
        net_inject(n6, N6_G, -a->v5b.ig);
        net_commit(n6, dc);
        /* The phase inverter's grid is solved with the power section, below. */
        a->pi_voc = n7->xh[N7_G] - INV(n7, N7_G, N7_P) * ip5b;
        a->pi_rth = INV(n7, N7_G, N7_G);
        a->ip5b = ip5b;
    }

    /* n8, n9: phase inverter, EL34s, output transformer and speaker, feedback. The feedback
     * loop closes within the sample, so the phase inverter currents (ia, ib) and the
     * half-primary voltage v are solved together: a three-variable Newton iteration. */
    {
        Net* n8 = &a->n8;
        Net* n9 = &a->n9;
        PowerCtx c;
        double x[3], r[3], J[3][3];
        int k;
        net_hist(n8, dc);
        net_hist(n9, dc);
        c.dc = dc;
        c.ga_voc = a->pi_voc;
        c.ga_rth = a->pi_rth;
        c.c105 = a->c105;
        c.bplus = a->bplus;
        c.vg2a = a->vg2a;
        c.vg2b = a->vg2b;
        c.vg2a_ex = pow(a->vg2a, TUBE_EL34.ex);
        c.vg2b_ex = pow(a->vg2b, TUBE_EL34.ex);
        c.vah = n8->xh[N8_PA] - INV(n8, N8_PA, N8_GA) * a->ig_a - INV(n8, N8_PA, N8_GB) * a->ig_b;
        c.vbh = n8->xh[N8_PB] - INV(n8, N8_PB, N8_GA) * a->ig_a - INV(n8, N8_PB, N8_GB) * a->ig_b;
        c.goa = n8->xh[N8_GA] - INV(n8, N8_GA, N8_GB) * a->ig_b;
        c.gob = n8->xh[N8_GB] - INV(n8, N8_GB, N8_GA) * a->ig_a;
        c.vfh = n9->xh[N9_F];
        c.s0h = n9->xh[N9_S0];
        c.n8 = n8;
        c.n9 = n9;
        c.a = a;
        x[0] = a->pia;
        x[1] = a->pib;
        x[2] = a->vhalf;
        /* Newton with a backtracking line search: a step that leaves the residual larger is
         * halved, which keeps the solve on track when a side of the inverter is cut off and
         * the valve curves have corners. The residual is weighed in milliamps and volts. */
        {
            double norm, best_x[3], d[3] = { 0.0, 0.0, 0.0 };
            int halvings = 0;
            power_eval(&c, x, r, J);
            norm = 1e3 * (fabs(r[0]) + fabs(r[1])) + fabs(r[2]);
            for (k = 0; k < 40; ++k) {
                double trial;
                if (norm < 2e-6 || !solve3(J, r, d))
                    break;
                if (d[2] > 40.0) d[2] = 40.0;
                if (d[2] < -40.0) d[2] = -40.0;
                best_x[0] = x[0];
                best_x[1] = x[1];
                best_x[2] = x[2];
                for (halvings = 0; halvings < 8; ++halvings) {
                    x[0] = best_x[0] - d[0];
                    x[1] = best_x[1] - d[1];
                    x[2] = best_x[2] - d[2];
                    if (x[0] < 0.0) x[0] = 0.0;
                    if (x[1] < 0.0) x[1] = 0.0;
                    power_eval(&c, x, r, J);
                    trial = 1e3 * (fabs(r[0]) + fabs(r[1])) + fabs(r[2]);
                    if (trial < norm)
                        break;
                    d[0] *= 0.5;
                    d[1] *= 0.5;
                    d[2] *= 0.5;
                }
                norm = trial;
            }
        }
        a->pia = x[0];
        a->pib = x[1];
        a->vhalf = x[2];
        a->it = c.it;
        a->is = c.is;
        a->ig_a = c.ig_a;
        a->ig_b = c.ig_b;
        a->ia_side = c.i_a;
        a->ib_side = c.i_b;
        a->pi_vk = c.vk;
        a->pi_vg = c.vga;
        a->pi_ig = c.ig_pi;
        net_inject(&a->n7, N7_P, -a->ip5b);
        net_inject(&a->n7, N7_G, -a->pi_ig);
        net_commit(&a->n7, dc);
        a->pi_vx = c.vf + R123 * c.it;
        if (dc)
            a->c105 = R123 * c.it;
        else
            a->c105 += (a->T / (1e6 * 100e-9)) * (R123 * c.it - a->c105); /* C105 through R122 */

        net_inject(n8, N8_PA, -x[0]);
        net_inject(n8, N8_PB, -x[1]);
        net_inject(n8, N8_GA, -a->ig_a);
        net_inject(n8, N8_GB, -a->ig_b);
        net_commit(n8, dc);
        net_inject(n9, N9_F, c.it);
        net_inject(n9, N9_S0, c.is);
        net_commit(n9, dc);

        /* Supplies: the reservoir feeds the plates and, through the choke, the screens (each
         * behind its 1k) and everything else. */
        a->ig2a = c.ig2a;
        a->ig2b = c.ig2b;
        if (!dc) {
            const double ichoke = (a->bplus - a->vscr) / R_CHOKE;
            a->bplus += a->T / RES_C * ((a->v0 - a->bplus) / RES_R - c.i_a - c.i_b - ichoke);
            a->vscr += a->T / C_SCR * (ichoke - 2.0 * (c.ig2a + c.ig2b) - PRE_I);
        }
        a->vg2a += 0.5 * (a->vscr - R_G2 * c.ig2a - a->vg2a);
        a->vg2b += 0.5 * (a->vscr - R_G2 * c.ig2b - a->vg2b);
        a->out = n9->x[N9_S];
    }
    return a->out;
}

int amp_init(Amp* a, double fs)
{
    memset(a, 0, sizeof *a);
    a->T = 1.0 / fs;
    if (!lut_build(&a->tri, &TUBE_12AX7, -40.0, 6.0, 737, 0.0, 520.0, 261)
) {
        amp_free(a);
        return 0;
    }
    grid_lut_build(&a->gtri, &TUBE_12AX7);
    grid_lut_build(&a->gpen, &TUBE_EL34);
    pent_build(&a->pen, &TUBE_EL34);
    stage_setup(&a->v1a, &a->tri, 0.0, R6, C3, a->T);
    stage_setup(&a->v1b, &a->tri, 0.0, R10, K3, a->T);
    stage_setup(&a->v2b, &a->tri, R16, R17, K12, a->T);
    stage_setup(&a->v3a, &a->tri, 0.0, RX1, 0.0, a->T);
    stage_setup(&a->v4b, &a->tri, 0.0, R26A, K16, a->T);
    stage_setup(&a->v5b, &a->tri, 0.0, R110, 0.0, a->T);
    a->v3b.lut = &a->tri;
    a->v3b.rp = 0.0;
    a->v5a.lut = &a->tri;
    a->v5a.rp = RX3;
    a->rp1a = a->rp4b = 62.5e3;
    a->run_hg = a->run_cl = 1;
    build_nets(a);
    return 1;
}

void amp_free(Amp* a)
{
    lut_free(&a->tri);
}

/* The bias voltage that sets each EL34 to BIAS_I with its plate at vp and its screen fed
 * from vscr through its 1k; also sets the screen voltages and currents. */
static double find_bias(Amp* a, double vp, double vscr)
{
    double lo = -150.0, hi = 0.0, vg2 = vscr, ig2 = 0.0, gm, gp;
    int k, j;
    for (k = 0; k < 60; ++k) {
        const double mid = 0.5 * (lo + hi);
        double ip = 0.0;
        for (j = 0; j < 20; ++j) {
            ip = pent_eval(&a->pen, mid, vp, vg2, pow(vg2, TUBE_EL34.ex), &gm, &gp, &ig2);
            vg2 = vscr - R_G2 * ig2;
        }
        if (ip > BIAS_I)
            hi = mid;
        else
            lo = mid;
    }
    a->vg2a = a->vg2b = vg2;
    a->ig2a = a->ig2b = ig2;
    return 0.5 * (lo + hi);
}

/* A stage's plate resistance at its operating point. */
static double plate_r(const Stage* s, double vp)
{
    double gm, gp;
    lut_eval(s->lut, s->vg - s->vk, vp - s->vk, &gm, &gp);
    return gp > 1e-9 ? 1.0 / gp : 1e9;
}

void amp_reset(Amp* a, const AmpPots* p)
{
    Stage* st[6] = { &a->v1a, &a->v1b, &a->v2b, &a->v3a, &a->v4b, &a->v5b };
    int k, i;
    a->bplus = B_PLUS;
    a->s1 = 350.0;
    a->s2 = 280.0;
    a->s3 = 250.0;
    /* Idle: the screen supply sits below B+ by the choke's drop, and the rectifier voltage is
     * what gives B_PLUS with the idle current drawn. */
    a->ig2a = 0.002;
    for (k = 0; k < 3; ++k) {
        a->vscr = B_PLUS - R_CHOKE * (PRE_I + 4.0 * a->ig2a);
        a->vbias = find_bias(a, B_PLUS, a->vscr);
    }
    a->s_pi = a->vscr;
    a->v0 = B_PLUS + RES_R * (4.0 * BIAS_I + PRE_I + 4.0 * a->ig2a);
    a->pia = a->pib = 1e-3;
    a->pi_vx = 20.0;
    a->vhalf = 0.0;
    a->is = a->it = a->ig_a = a->ig_b = 0.0;
    a->power = 0.0;
    for (i = 0; i < 6; ++i) {
        st[i]->ip = 1e-3;
        st[i]->vc = st[i]->ic = st[i]->ig = st[i]->vk = st[i]->vg = 0.0;
    }
    a->v3b.ip = a->v5a.ip = 2e-3;
    a->v3b.ig = a->v5a.ig = 0.0;
    a->v3b.vk = 150.0;
    a->v5a.vk = 50.0;
    a->pi_ig = 0.0;
    a->pi_vk = 20.0;
    a->vj = 50.0;
    amp_set_pots(a, p);

    /* DC operating point: capacitors open, then the supply chain from the stage currents. */
    build_all(a, 1, 1);
    for (k = 0; k < 60; ++k) {
        double i3, i2, i1;
        set_supplies(a);
        a->n5r.e[a->src_r103].src = a->vj; /* R103 returns to the R104/R100 junction */
        tick(a, 0.0, 1);
        i3 = a->v1a.ip + a->v1b.ip + a->v2b.ip;
        i2 = a->v3a.ip + a->v3b.ip + i3;
        i1 = i2 + a->v4b.ip + a->v5a.ip + a->v5b.ip;
        a->s1 = 0.5 * a->s1 + 0.5 * (a->s_pi - R33 * i1);
        a->s2 = 0.5 * a->s2 + 0.5 * (a->s1 - R34 * i2);
        a->s3 = 0.5 * a->s3 + 0.5 * (a->s2 - R32 * i3);
    }
    set_supplies(a);
    a->n5r.e[a->src_r103].src = a->vj;
    tick(a, 0.0, 1);
    a->rp1a = plate_r(&a->v1a, a->n1.x[N1_P]);
    a->rp4b = plate_r(&a->v4b, a->n5c.x[N5C_P]);

    /* Running state from the operating point, then a short settle. */
    amp_set_pots(a, p);
    build_all(a, 0, 1);
    for (k = 0; k < (int)(amp_settle_s / a->T); ++k)
        tick(a, 0.0, 0);
}

double amp_tick(Amp* a, double vin)
{
    const double y = tick(a, vin, 0);
    a->power += 0.0005 * (y * y / Z_SPK - a->power);
    return y;
}
