/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* Checks for the Schallmauer. Usage: test_dsp <bundle folder>
 * Part 1 drives the circuit code directly: the network solver against exact responses worked
 * out independently, the valve tables against Koren's equations, the operating points, the
 * oversampling filters, the controls, the rated power, and stability. Part 2 loads the built
 * plugin library the way a host does. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#    include <windows.h>
#    define LIB_NAME "schallmauer.dll"
#    define LIB_OPEN(p) ((void*)LoadLibraryA(p))
#    define LIB_SYM(l, s) ((void*)GetProcAddress((HMODULE)(l), s))
#else
#    include <dlfcn.h>
#    ifdef __APPLE__
#        define LIB_NAME "schallmauer.dylib"
#    else
#        define LIB_NAME "schallmauer.so"
#    endif
#    define LIB_OPEN(p) dlopen(p, RTLD_NOW)
#    define LIB_SYM(l, s) dlsym(l, s)
#endif

/* After the system headers: amp.c names its parts R1, C3 and so on. */
#include "../src/lv2_abi.h"
#include "../src/amp.c" /* the circuit, compiled in so its internals can be checked */
#include "../src/oversample.h"
#include "../src/ports.h"

#define PI 3.14159265358979323846
#define FS_OS 192000.0

static int failures;

static void check(const char* what, int ok)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++failures;
}

static double db(double x) { return 20.0 * log10(x + 1e-30); }

/* ---- 1. The network solver ---- */

/* Drives node 0 of a network through its source and returns the gain at node `out` for a sine
 * at f, measured over whole cycles after the start-up has died away. */
static double net_gain(Net* n, int src, int out, double f, double fs)
{
    const int per = (int)(fs / f + 0.5);
    const int settle = per * (int)(1 + 0.2 * f), meas = per * 20;
    double sx = 0.0, sy = 0.0;
    int i;
    net_build(n, 0);
    for (i = 0; i < settle + meas; ++i) {
        const double x = sin(2.0 * PI * f * i / fs);
        n->e[src].src = x;
        net_hist(n, 0);
        net_commit(n, 0);
        if (i >= settle) {
            sx += x * x;
            sy += n->x[out] * n->x[out];
        }
    }
    return sqrt(sy / sx);
}

/* Exact magnitude of the bilinear (trapezoidal) discretisation of H(s) at f: H evaluated at
 * the pre-warped frequency. */
static double warp(double f, double fs) { return 2.0 * fs * tan(PI * f / fs); }

static void test_solver(void)
{
    Net n;
    char label[160];
    const double fs = 48000.0, R = 10e3, C = 10e-9; /* RC low-pass, corner 1.59 kHz */
    const double fr[3] = { 300.0, 1591.5, 8000.0 };
    int k, src;
    printf("Network solver\n");
    net_init(&n, 1, 1.0 / fs);
    src = net_src(&n, 0, R, 0.0);
    net_c(&n, 0, NET_GND, C);
    for (k = 0; k < 3; ++k) {
        const double w = warp(fr[k], fs);
        const double exact = 1.0 / sqrt(1.0 + w * w * R * R * C * C);
        const double got = net_gain(&n, src, 0, fr[k], fs);
        snprintf(label, sizeof label, "RC low-pass at %.0f Hz: %.4f, exact %.4f", fr[k], got, exact);
        /* 1%: the trapezoidal rule is exact only at DC and its warped frequencies, and the
         * tangent warp is 0.3% at the corner at 48 kHz (the amp itself runs at 192 kHz). */
        check(label, fabs(got - exact) < 1e-2 * exact);
    }
    /* Series R, then L and C in parallel to ground: a band-pass peak at 1/(2 pi sqrt(LC)). */
    {
        const double L = 0.1, Cp = 1e-6, Rs = 1e3;
        const double f0 = 1.0 / (2.0 * PI * sqrt(L * Cp));
        const double fr2[3] = { 0.5 * f0, f0, 2.0 * f0 };
        net_init(&n, 1, 1.0 / fs);
        src = net_src(&n, 0, Rs, 0.0);
        net_l(&n, 0, NET_GND, L);
        net_c(&n, 0, NET_GND, Cp);
        for (k = 0; k < 3; ++k) {
            const double w = warp(fr2[k], fs);
            /* Z of L || C = jwL / (1 - w^2 LC); gain = Z / (Rs + Z) */
            const double x = w * L / (1.0 - w * w * L * Cp);
            const double exact = fabs(x) / sqrt(Rs * Rs + x * x);
            const double got = net_gain(&n, src, 0, fr2[k], fs);
            snprintf(label, sizeof label, "LC tank at %.0f Hz: %.4f, exact %.4f", fr2[k], got, exact);
            check(label, fabs(got - exact) < 2e-3);
        }
    }
}

/* ---- 2. Valve tables ---- */

static void test_tables(void)
{
    TubeLut t, p;
    double worst_t = 0.0, worst_p = 0.0, gm, gp;
    char label[160];
    int i;
    printf("Valve tables\n");
    lut_build(&t, &TUBE_12AX7, -40.0, 6.0, 737, 0.0, 520.0, 261);
    lut_build(&p, &TUBE_EL34, -160.0, 40.0, 401, 0.0, 1000.0, 501); /* koren() at the nominal screen */
    srand(1);
    for (i = 0; i < 20000; ++i) {
        const double vg = -4.0 + 4.5 * rand() / RAND_MAX, vp = 40.0 + 400.0 * rand() / RAND_MAX;
        const double e = koren(&TUBE_12AX7, vg, vp);
        if (e > 1e-4)
            worst_t = fmax(worst_t, fabs(lut_eval(&t, vg, vp, &gm, &gp) - e) / e);
    }
    for (i = 0; i < 20000; ++i) {
        const double vg = -60.0 + 60.0 * rand() / RAND_MAX, vp = 30.0 + 800.0 * rand() / RAND_MAX;
        const double e = koren(&TUBE_EL34, vg, vp);
        if (e > 1e-3)
            worst_p = fmax(worst_p, fabs(lut_eval(&p, vg, vp, &gm, &gp) - e) / e);
    }
    snprintf(label, sizeof label, "12AX7 table within %.2f%% of Koren's equation above 0.1 mA", 100.0 * worst_t);
    check(label, worst_t < 0.02);
    snprintf(label, sizeof label, "EL34 table within %.2f%% of Koren's equation above 1 mA", 100.0 * worst_p);
    check(label, worst_p < 0.02);
    /* The 12AX7 fit at a data-sheet point: 250 V, -2 V gives about 1.2 mA. */
    snprintf(label, sizeof label, "12AX7 at 250 V, -2 V: %.2f mA (data sheet about 1.2)", 1e3 * koren(&TUBE_12AX7, -2.0, 250.0));
    check(label, fabs(koren(&TUBE_12AX7, -2.0, 250.0) - 1.2e-3) < 0.4e-3);
    lut_free(&t);
    lut_free(&p);
}

/* ---- 3. The amplifier ---- */

static Amp amp;
static AmpPots manual = { 1.0, 0.8, 0.4, 1.0, 1.0, 1.0, 1.0, 0.6, 0.6, 0.6, 0.6, 0.6, 0.6, 0.2 };

static int in_range(double v, double lo, double hi) { return v >= lo && v <= hi; }

static void test_operating_point(void)
{
    char label[200];
    printf("Operating points (no signal)\n");
    amp_reset(&amp, &manual);
    snprintf(label, sizeof label, "preamp plates V1a %.0f, V1b %.0f, V2b %.0f, V3a %.0f, V4b %.0f, V5b %.0f V (130-300)",
             amp.n1.x[N1_P], amp.n2.x[N2_P], amp.n3.x[N2_P], amp.n4.x[N4_P], amp.n5c.x[N5C_P], amp.n7.x[N7_P]);
    check(label, in_range(amp.n1.x[N1_P], 130, 300) && in_range(amp.n2.x[N2_P], 130, 300)
                     && in_range(amp.n3.x[N2_P], 130, 300) && in_range(amp.n4.x[N4_P], 130, 300)
                     && in_range(amp.n5c.x[N5C_P], 130, 300) && in_range(amp.n7.x[N7_P], 130, 300));
    snprintf(label, sizeof label, "gain stage cathodes V1a %.2f, V1b %.2f, V2b %.2f, V3a %.2f V (0.8-3)",
             amp.v1a.vk, amp.v1b.vk, amp.v2b.vk, amp.v3a.vk);
    check(label, in_range(amp.v1a.vk, 0.8, 3) && in_range(amp.v1b.vk, 0.8, 3) && in_range(amp.v2b.vk, 0.8, 3)
                     && in_range(amp.v3a.vk, 0.8, 3));
    snprintf(label, sizeof label, "follower V3b sits %.2f V above its grid at %.0f V", amp.v3b.vk - amp.v3b.vg, amp.v3b.vg);
    check(label, in_range(amp.v3b.vk - amp.v3b.vg, 0.1, 3.0));
    snprintf(label, sizeof label, "phase inverter: %.2f and %.2f mA, cathodes at %.0f V, plates %.0f and %.0f V",
             1e3 * amp.pia, 1e3 * amp.pib, amp.pi_vk, amp.n8.x[N8_PA], amp.n8.x[N8_PB]);
    check(label, in_range(amp.pia, 1e-3, 3.5e-3) && in_range(amp.pib, 1e-3, 3.5e-3) && in_range(amp.pi_vk, 30, 80)
                     && in_range(amp.n8.x[N8_PA], 250, 400) && in_range(amp.n8.x[N8_PB], 250, 400));
    snprintf(label, sizeof label, "EL34s idle at %.1f and %.1f mA each with %.1f V bias (manual: 25 mA)",
             500.0 * amp.ia_side, 500.0 * amp.ib_side, amp.vbias);
    check(label, fabs(500.0 * amp.ia_side - 25.0) < 1.5 && fabs(500.0 * amp.ib_side - 25.0) < 1.5
                     && in_range(amp.vbias, -60, -30));
    snprintf(label, sizeof label, "speaker idles at %.2f mV", 1e3 * amp.out);
    check(label, fabs(amp.out) < 0.01);
}

/* RMS output for a sine at f and level lv, after the start-up has passed. */
static double amp_level(AmpPots* p, double f, double lv, double secs)
{
    const int n = (int)(FS_OS * secs), m = (int)(FS_OS * 0.1);
    double s = 0.0;
    int i;
    amp_set_pots(&amp, p);
    amp_update(&amp);
    amp_reset(&amp, p);
    for (i = 0; i < n; ++i) {
        const double y = amp_tick(&amp, lv * sin(2.0 * PI * f * i / FS_OS));
        if (i >= n - m)
            s += y * y;
    }
    return sqrt(s / m);
}

typedef struct {
    const char* name;
    size_t field;
    int channel;
    double f_up, f_ref; /* turning it up should raise f_up against f_ref */
    double lv;
} Ctl;

static void test_controls(void)
{
    static const Ctl ctls[] = {
        { "clean bass", offsetof(AmpPots, cl_bass), 0, 80.0, 1000.0, 3e-3 },
        { "clean middle", offsetof(AmpPots, cl_mid), 0, 600.0, 80.0, 3e-3 },
        { "clean treble", offsetof(AmpPots, cl_treble), 0, 4000.0, 600.0, 3e-3 },
        { "clean presence", offsetof(AmpPots, cl_pres), 0, 4000.0, 600.0, 3e-3 },
        { "high gain bass", offsetof(AmpPots, hg_bass), 1, 80.0, 1000.0, 1e-4 },
        { "high gain middle", offsetof(AmpPots, hg_mid), 1, 800.0, 80.0, 1e-4 },
        { "high gain treble", offsetof(AmpPots, hg_treble), 1, 4000.0, 600.0, 1e-4 },
        /* Presence works in the feedback loop: more feedback holds down the speaker's bass
         * resonance against the midrange (the manual's "rolling off the sub-bass"). */
        { "high gain presence", offsetof(AmpPots, hg_pres), 1, 800.0, 100.0, 1e-4 },
    };
    char label[200];
    int k;
    printf("Controls (small signals, so the stages stay near linear)\n");
    for (k = 0; k < (int)(sizeof ctls / sizeof ctls[0]); ++k) {
        AmpPots p = manual;
        double* v = (double*)((char*)&p + ctls[k].field);
        double up, ref, d_lo, d_hi;
        p.channel = ctls[k].channel;
        p.cl_gain = 0.3;
        p.hg_gain = 0.01;
        p.master = 0.3;
        *v = 0.0;
        up = amp_level(&p, ctls[k].f_up, ctls[k].lv, 0.4);
        ref = amp_level(&p, ctls[k].f_ref, ctls[k].lv, 0.4);
        d_lo = db(up / ref);
        *v = 1.0;
        up = amp_level(&p, ctls[k].f_up, ctls[k].lv, 0.4);
        ref = amp_level(&p, ctls[k].f_ref, ctls[k].lv, 0.4);
        d_hi = db(up / ref);
        snprintf(label, sizeof label, "%s up lifts %.0f Hz against %.0f Hz by %.1f dB", ctls[k].name, ctls[k].f_up,
                 ctls[k].f_ref, d_hi - d_lo);
        check(label, d_hi - d_lo > 2.0);
    }
    /* Gain and volume do what they say. */
    {
        AmpPots p = manual;
        double lo, hi;
        /* 10 uV: even at 1 the high gain channel clips a millivolt. 100 Hz: higher up, the
         * bright caps across the pot carry the treble past it at low settings. */
        p.hg_gain = 0.1;
        lo = amp_level(&p, 100.0, 1e-5, 0.3);
        p.hg_gain = 0.6;
        hi = amp_level(&p, 100.0, 1e-5, 0.3);
        snprintf(label, sizeof label, "high gain Gain 1 to 6 raises a 10 uV 100 Hz note by %.1f dB", db(hi / lo));
        check(label, hi > lo * 2.0);
        p.master = 0.05;
        lo = amp_level(&p, 500.0, 0.1, 0.3);
        p.master = 0.5;
        hi = amp_level(&p, 500.0, 0.1, 0.3);
        snprintf(label, sizeof label, "Master 0.5 to 5 raises the output by %.1f dB", db(hi / lo));
        check(label, hi > lo * 3.0);
    }
}

static void test_power(void)
{
    AmpPots p = manual;
    char label[200];
    double v, w;
    printf("Power\n");
    p.channel = 0.0;
    p.cl_gain = 0.5;
    p.master = 1.0;
    /* Where the output starts to clip (a 50 mV input), then driven into a square wave. */
    v = amp_level(&p, 400.0, 0.05, 0.6);
    w = v * v / Z_SPK;
    snprintf(label, sizeof label, "clean channel at the edge of clipping: %.0f W RMS into 16 ohms (rated 120 W)", w);
    check(label, in_range(w, 80.0, 170.0));
    v = amp_level(&p, 400.0, 0.5, 0.6);
    w = v * v / Z_SPK;
    snprintf(label, sizeof label, "clean channel flat out: %.0f W RMS (a square wave), B+ sags to %.0f V", w, amp.bplus);
    check(label, in_range(w, 120.0, 250.0) && amp.bplus < B_PLUS - 5.0);
}

static void test_stability(void)
{
    AmpPots p = manual;
    char label[200];
    double peak = 0.0, jump = 0.0, jump_play = 0.0, last = 0.0, tail = 0.0;
    int i, ok = 1;
    printf("Stability\n");
    /* Everything on 10, a loud input, then silence. The peak includes the flyback spikes when
     * both sides of the output stage cut off at once; a runaway would go far past it. The
     * coupling capacitors charged by grid current take a while to let go (blocking), so the
     * tail is checked a second after the input stops, by which time it must be silent. */
    p.hg_gain = p.hg_bass = p.hg_mid = p.hg_treble = p.hg_pres = p.hg_vol = p.master = 1.0;
    amp_reset(&amp, &p);
    for (i = 0; i < (int)(FS_OS * 2.2); ++i) {
        const double x = i < FS_OS ? 2.0 * sin(2.0 * PI * 82.4 * i / FS_OS) + 1.5 * sin(2.0 * PI * 1318.5 * i / FS_OS) : 0.0;
        const double y = amp_tick(&amp, x);
        if (!isfinite(y))
            ok = 0;
        peak = fmax(peak, fabs(y));
        if (i > FS_OS * 2.0)
            tail = fmax(tail, fabs(y));
    }
    snprintf(label, sizeof label, "all controls on 10 with a 2 V input: finite, peak %.0f V", peak);
    check(label, ok && peak < 600.0);
    snprintf(label, sizeof label, "1 s after the input stops the speaker is at %.3f V", tail);
    check(label, tail < 0.1);

    /* Switching channels while playing: no jumps beyond what the signal itself makes. */
    p = manual;
    amp_reset(&amp, &p);
    ok = 1;
    for (i = 0; i < (int)(FS_OS * 0.8); ++i) {
        double y;
        if (i % 64 == 0) {
            const double t = i / FS_OS;
            /* the channel moves over 30 ms, like the engine's smoothing */
            p.channel = t < 0.3 ? 1.0 : (t < 0.33 ? 1.0 - (t - 0.3) / 0.03 : 0.0);
            amp_set_pots(&amp, &p);
            amp_update(&amp);
        }
        y = amp_tick(&amp, 0.2 * sin(2.0 * PI * 196.0 * i / FS_OS));
        if (!isfinite(y))
            ok = 0;
        /* steps while the channel moves (and the relay and optos settle) against steps in the
         * clipped note itself on either channel */
        if (i > FS_OS * 0.3 && i < FS_OS * 0.45)
            jump = fmax(jump, fabs(y - last));
        else if (i > FS_OS * 0.1)
            jump_play = fmax(jump_play, fabs(y - last));
        last = y;
    }
    snprintf(label, sizeof label, "switching channels mid-note: largest step between samples %.1f V (%.1f V while playing)",
             jump, jump_play);
    check(label, ok && jump < 1.25 * jump_play + 2.0);
}

static void test_oversampler(void)
{
    HalfBand up, dn;
    char label[160];
    double pass = 0.0, stop = 0.0, sx;
    int i;
    printf("Oversampling filters\n");
    hb_init(&up);
    hb_init(&dn);
    /* Upsample a tone at 0.4 of the input rate's Nyquist and downsample again: the level holds. */
    {
        double s = 0.0, o0, o1;
        sx = 0.0;
        for (i = 0; i < 20000; ++i) {
            const double x = sin(2.0 * PI * 0.2 * i);
            double y;
            hb_up(&up, x, &o0, &o1);
            y = hb_down(&dn, o0, o1);
            if (i > 2000) {
                s += y * y;
                sx += x * x;
            }
        }
        pass = db(sqrt(s / sx));
    }
    /* A tone above the original Nyquist at the high rate must not come back down. */
    hb_reset(&dn);
    {
        double s = 0.0;
        sx = 0.0;
        for (i = 0; i < 20000; ++i) {
            const double a = sin(2.0 * PI * 0.3 * (2 * i)), b = sin(2.0 * PI * 0.3 * (2 * i + 1));
            const double y = hb_down(&dn, a, b);
            if (i > 2000) {
                s += y * y;
                sx += 0.5;
            }
        }
        stop = db(sqrt(s / sx));
    }
    snprintf(label, sizeof label, "passband tone through up and down: %+.3f dB", pass);
    check(label, fabs(pass) < 0.1);
    snprintf(label, sizeof label, "out-of-band tone rejected by %.0f dB", -stop);
    check(label, stop < -60.0);
}

/* ---- Part 2: the plugin library ---- */

typedef struct {
    const LV2_Descriptor* d;
    LV2_Handle h;
    float c[PORT_COUNT];
} Inst;

static void inst_open(Inst* x, const LV2_Descriptor* d, double fs)
{
    static const float defs[PORT_COUNT] = { 0, 0, 0, 0, 1, 0.8f, 0.4f, 1, 1, 1, 1, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.2f, 1, 1, 0, 1, 0 };
    int i;
    x->d = d;
    memcpy(x->c, defs, sizeof defs);
    x->h = d->instantiate(d, fs, ".", NULL);
    for (i = 4; i < PORT_COUNT; ++i)
        d->connect_port(x->h, (uint32_t)i, &x->c[i]);
    d->activate(x->h);
}

static void inst_render(Inst* x, const float* in, float* oa, float* ob, long n, long block)
{
    long pos;
    for (pos = 0; pos < n; pos += block) {
        const long k = n - pos < block ? n - pos : block;
        x->d->connect_port(x->h, PORT_IN_A, (void*)(in + pos));
        x->d->connect_port(x->h, PORT_IN_B, (void*)(in + pos));
        x->d->connect_port(x->h, PORT_OUT_A, oa + pos);
        x->d->connect_port(x->h, PORT_OUT_B, ob + pos);
        x->d->run(x->h, (uint32_t)k);
    }
}

static void inst_close(Inst* x) { x->d->cleanup(x->h); }

static void test_library(const char* bundle)
{
    char path[1024], label[200];
    const double fs = 48000.0;
    const long n = (long)(fs * 2.0);
    float* in = (float*)malloc(sizeof(float) * n);
    float* oa = (float*)malloc(sizeof(float) * n);
    float* ob = (float*)malloc(sizeof(float) * n);
    float* oa2 = (float*)malloc(sizeof(float) * n);
    float* ob2 = (float*)malloc(sizeof(float) * n);
    const LV2_Descriptor* (*get)(uint32_t);
    const LV2_Descriptor* d;
    void* lib;
    Inst x;
    static const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
    long i;
    int r;

    printf("Plugin library\n");
    snprintf(path, sizeof path, "%s/%s", bundle, LIB_NAME);
    lib = LIB_OPEN(path);
    check("library loads", lib != NULL);
    if (!lib)
        return;
    *(void**)&get = LIB_SYM(lib, "lv2_descriptor");
    check("exports lv2_descriptor", get != NULL);
    if (!get)
        return;
    d = get(0);
    check("descriptor has the plugin's URI", d && strcmp(d->URI, "urn:davidian:schallmauer") == 0);
    check("only one plugin in the library", get(1) == NULL);

    /* A plucked-string-like test note: an E2 with harmonics, decaying. */
    for (i = 0; i < n; ++i) {
        const double t = i / fs, e = exp(-1.5 * fmod(t, 1.0));
        in[i] = (float)(0.25 * e * (sin(2 * PI * 82.4 * t) + 0.5 * sin(2 * PI * 164.8 * t) + 0.3 * sin(2 * PI * 247.2 * t)));
    }

    inst_open(&x, d, fs);
    inst_render(&x, in, oa, ob, n, 256);
    {
        double s = 0.0, peak = 0.0;
        int finite = 1;
        for (i = 0; i < n; ++i) {
            finite &= isfinite(oa[i]) && oa[i] == ob[i];
            s += (double)oa[i] * oa[i];
            peak = fmax(peak, fabs(oa[i]));
        }
        snprintf(label, sizeof label, "default settings: %.1f dBFS RMS, %.1f dBFS peak, both outputs the same",
                 db(sqrt(s / n)), db(peak));
        check(label, finite && peak > 0.01 && peak < 4.0);
        snprintf(label, sizeof label, "power lamp output reads %.2f", x.c[PORT_POWER]);
        check(label, x.c[PORT_POWER] > 0.0f && x.c[PORT_POWER] <= 1.0f);
    }
    inst_close(&x);

    /* Block size must not change the audio. */
    inst_open(&x, d, fs);
    inst_render(&x, in, oa, ob, n / 4, 1);
    inst_close(&x);
    inst_open(&x, d, fs);
    inst_render(&x, in, oa2, ob2, n / 4, 512);
    inst_close(&x);
    check("output is the same at block sizes 1 and 512", memcmp(oa, oa2, sizeof(float) * (n / 4)) == 0);

    /* Hosts may pass the same buffer for input and output. */
    memcpy(oa2, in, sizeof(float) * n);
    inst_open(&x, d, fs);
    inst_render(&x, oa2, oa2, ob2, n / 4, 512);
    inst_close(&x);
    check("works with the same buffer for in and out", memcmp(oa, oa2, sizeof(float) * (n / 4)) == 0);

    /* Host bypass passes audio untouched. */
    inst_open(&x, d, fs);
    x.c[PORT_ENABLED] = 0.0f;
    d->activate(x.h);
    inst_render(&x, in, oa, ob, n / 4, 256);
    inst_close(&x);
    check("host bypass output is identical to input",
          memcmp(in, oa, sizeof(float) * (n / 4)) == 0 && memcmp(in, ob, sizeof(float) * (n / 4)) == 0);

    /* Every supported rate runs and makes sound. */
    for (r = 0; r < 5; ++r) {
        const long m = (long)(rates[r] * 0.5);
        float* a = (float*)malloc(sizeof(float) * m);
        float* b = (float*)malloc(sizeof(float) * m);
        float* src = (float*)malloc(sizeof(float) * m);
        double peak = 0.0;
        int finite = 1;
        clock_t t0;
        double secs;
        for (i = 0; i < m; ++i)
            src[i] = (float)(0.2 * sin(2 * PI * 110.0 * i / rates[r]));
        inst_open(&x, d, rates[r]);
        t0 = clock();
        inst_render(&x, src, a, b, m, 256);
        secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
        inst_close(&x);
        for (i = 0; i < m; ++i) {
            finite &= isfinite(a[i]);
            peak = fmax(peak, fabs(a[i]));
        }
        snprintf(label, sizeof label, "%.0f Hz: runs, peak %.2f, %.0f%% of one core", rates[r], peak, 100.0 * secs / 0.5);
        check(label, finite && peak > 0.01);
        free(a);
        free(b);
        free(src);
    }
    free(in);
    free(oa);
    free(ob);
    free(oa2);
    free(ob2);
}

int main(int argc, char** argv)
{
    test_solver();
    test_tables();
    test_oversampler();
    if (!amp_init(&amp, FS_OS)) {
        printf("cannot allocate the valve tables\n");
        return 1;
    }
    test_operating_point();
    test_controls();
    test_power();
    test_stability();
    amp_free(&amp);
    if (argc > 1)
        test_library(argv[1]);
    else
        printf("(no bundle folder given: skipping the plugin library)\n");
    printf(failures ? "\n%d failed\n" : "\nAll passed\n", failures);
    return failures ? 1 : 0;
}
