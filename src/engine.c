/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* The Schallmauer engine: controls, oversampling and the per-sample loop around the circuit in
 * amp.c. The LV2, VST3 and Audio Unit wrappers all drive it through engine.h. */
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#    define _CRT_SECURE_NO_WARNINGS /* params.h has sscanf, which isn't used here */
#endif
#include <stdlib.h>

#include "amp.h"
#include "params.h"
#include "cab.h"
#include "engine.h"
#include "oversample.h"

/* Denormal flushing: decaying signals would otherwise slow the CPU. x86 uses the SSE control
 * register (FTZ and DAZ bits), 64-bit ARM (Apple silicon, Linux arm64) the FPCR FZ bit. */
#if defined(_M_X64) || defined(__SSE2__)
#    include <xmmintrin.h>
#    define SH_HAS_CSR 1
typedef unsigned int fpstate;
static inline fpstate fp_flush_denormals(void)
{
    const unsigned int csr = _mm_getcsr();
    _mm_setcsr(csr | 0x8040);
    return csr;
}
static inline void fp_restore(fpstate s) { _mm_setcsr(s); }
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
#    include <stdint.h>
#    define SH_HAS_CSR 1
typedef uint64_t fpstate;
static inline fpstate fp_flush_denormals(void)
{
    uint64_t fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr | (UINT64_C(1) << 24)));
    return fpcr;
}
static inline void fp_restore(fpstate s) { __asm__ __volatile__("msr fpcr, %0" : : "r"(s)); }
#endif

#define SMOOTH_SECONDS 0.015
#define CHANNEL_SECONDS 0.03 /* the optos' LEDs and LDRs */
#define BYPASS_SECONDS 0.02
#define CAB_SECONDS 0.02
#define SUB 16               /* controls move every 16 samples */
#define POWER_FULL 120.0     /* watts shown as a fully lit power lamp */
/* Speaker volts to sample value: 30 V is 0 dBFS, so full power (62 V peaks into 16 ohms)
 * peaks at about +6 dBFS and the default settings sit around -18 dBFS RMS. */
#define OUT_SCALE (1.0 / 30.0)
/* Sleep: once the speaker has stayed below SLEEP_OUT for SLEEP_WINDOWS windows of SLEEP_SECONDS
 * with the controls still, the circuit stops running and the output is silent. It wakes, from
 * the state it stopped in, when the input rises 12 dB above the loudest it was in the last
 * window, or a control moves. SLEEP_OUT is -100 dBFS at the output. */
#define SLEEP_OUT (1e-5 / OUT_SCALE)
#define SLEEP_SECONDS 0.25
#define SLEEP_WINDOWS 2
#define WAKE_FLOOR 1e-7 /* volts: the input always wakes it above this */

/* Knob order in the smoothing array: every control from PORT_CHANNEL to PORT_MASTER. */
#define NKNOB (PORT_MASTER - PORT_CHANNEL + 1)

typedef struct ShEngine {
    const float* port[PORT_COUNT];
    double fs;
    int os; /* oversampling factor: 4, 2 or 1 */
    HalfBand up1, up2, dn1, dn2;
    Amp amp;
    Cab cab;
    int ok;
    double knob[NKNOB];
    double sm_coef, ch_coef, cab_coef;
    double volts, gain, cab_mix;
    double fade, fade_step;
    int sub;
    /* Sleep: see sh_engine_run. */
    int asleep, quiet, win, win_len, moving;
    double in_peak, out_peak, wake_in;
} Plugin;

static double ctl(const Plugin* p, int port, double lo, double hi, double fallback)
{
    const float* v = p->port[port];
    if (!v || !isfinite(*v))
        return fallback;
    return *v < lo ? lo : (*v > hi ? hi : *v);
}

/* Audio taper (10% at half rotation) and linear. */
static double ataper(double k) { return (pow(81.0, k) - 1.0) / 80.0; }

static void pots_from_knobs(const double* k, AmpPots* a)
{
#define K(port) (k[(port) - PORT_CHANNEL])
    a->channel = K(PORT_CHANNEL);
    a->cl_gain = ataper(K(PORT_CL_GAIN));
    a->cl_bass = ataper(K(PORT_CL_BASS));
    a->cl_mid = K(PORT_CL_MIDDLE);
    a->cl_treble = ataper(K(PORT_CL_TREBLE));
    a->cl_pres = K(PORT_CL_PRESENCE);
    a->cl_vol = ataper(K(PORT_CL_VOLUME));
    a->hg_gain = ataper(K(PORT_HG_GAIN));
    a->hg_bass = ataper(K(PORT_HG_BASS));
    a->hg_mid = K(PORT_HG_MIDDLE);
    a->hg_treble = ataper(K(PORT_HG_TREBLE));
    a->hg_pres = K(PORT_HG_PRESENCE);
    a->hg_vol = ataper(K(PORT_HG_VOLUME));
    a->master = ataper(K(PORT_MASTER));
#undef K
}

static void knob_targets(const Plugin* p, double* t)
{
    int i;
    for (i = 0; i < NKNOB; ++i) {
        const int port = PORT_CHANNEL + i;
        t[i] = ctl(p, port, 0.0, 1.0, sh_param(port)->def);
        if (port == PORT_CHANNEL)
            t[i] = t[i] > 0.5 ? 1.0 : 0.0;
    }
}

/* The oversampling factor. Standard runs the circuit at 88.2 kHz or more (2x at 44.1 and
 * 48 kHz), High at 176.4 kHz or more (4x at 44.1 and 48 kHz, 2x at 88.2 and 96 kHz). */
static int os_factor(double fs, int high)
{
    if (high)
        return fs <= 50000.0 ? 4 : (fs <= 100000.0 ? 2 : 1);
    return fs <= 50000.0 ? 2 : 1;
}

static int want_high(const Plugin* p) { return ctl(p, PORT_QUALITY, 0, 1, 0) > 0.5; }

/* Moves the circuit to the rate the Quality control asks for and clears the filters.
 * The caller then resets the circuit. */
static void set_quality(Plugin* p)
{
    const int os = os_factor(p->fs, want_high(p));
    if (os != p->os) {
        p->os = os;
        amp_set_rate(&p->amp, p->fs * os);
    }
    hb_reset(&p->up1);
    hb_reset(&p->up2);
    hb_reset(&p->dn1);
    hb_reset(&p->dn2);
}

ShEngine* sh_engine_new(double sample_rate)
{
    Plugin* p = (Plugin*)calloc(1, sizeof(Plugin));
    if (!p)
        return NULL;
    p->fs = sample_rate;
    p->os = os_factor(sample_rate, 0);
    if (!amp_init(&p->amp, sample_rate * p->os)) {
        free(p);
        return NULL;
    }
    hb_init(&p->up1);
    hb_init(&p->up2);
    hb_init(&p->dn1);
    hb_init(&p->dn2);
    cab_init(&p->cab, sample_rate);
    p->sm_coef = 1.0 - exp(-SUB / (SMOOTH_SECONDS * sample_rate));
    p->ch_coef = 1.0 - exp(-SUB / (CHANNEL_SECONDS * sample_rate));
    p->cab_coef = 1.0 - exp(-1.0 / (CAB_SECONDS * sample_rate));
    p->fade_step = 1.0 / (BYPASS_SECONDS * sample_rate);
    p->win_len = (int)(SLEEP_SECONDS * sample_rate);
    p->ok = 1;
    return p;
}

void sh_engine_connect(ShEngine* h, uint32_t port, void* data)
{
    Plugin* p = (Plugin*)h;
    if (port < PORT_COUNT)
        p->port[port] = (const float*)data;
}

void sh_engine_activate(ShEngine* h)
{
    Plugin* p = (Plugin*)h;
    AmpPots pots;
    knob_targets(p, p->knob);
    set_quality(p);
    pots_from_knobs(p->knob, &pots);
    amp_reset(&p->amp, &pots);
    cab_reset(&p->cab);
    p->volts = ctl(p, PORT_VOLTS, 0.1, 10.0, 1.0);
    p->gain = pow(10.0, ctl(p, PORT_OUTPUT, -24.0, 12.0, 0.0) / 20.0);
    p->cab_mix = ctl(p, PORT_CAB, 0, 1, 1) > 0.5 ? 1.0 : 0.0;
    p->fade = ctl(p, PORT_ENABLED, 0, 1, 1) > 0.5 ? 1.0 : 0.0;
    p->sub = 0;
    p->asleep = p->quiet = p->win = p->moving = 0;
    p->in_peak = p->out_peak = 0.0;
}

static inline double run_amp(Plugin* p, double x)
{
    double a, b, v[4];
    int k;
    switch (p->os) {
    case 4:
        hb_up4(&p->up1, &p->up2, x, v);
        for (k = 0; k < 4; ++k)
            v[k] = amp_tick(&p->amp, v[k]);
        return hb_down4(&p->dn1, &p->dn2, v);
    case 2:
        hb_up(&p->up1, x, &a, &b);
        a = amp_tick(&p->amp, a);
        b = amp_tick(&p->amp, b);
        return hb_down(&p->dn1, a, b);
    default:
        return amp_tick(&p->amp, x);
    }
}

void sh_engine_run(ShEngine* h, uint32_t n)
{
    Plugin* p = (Plugin*)h;
    const float* in = p->port[PORT_IN_A];
    const float* in_b = p->port[PORT_IN_B] ? p->port[PORT_IN_B] : in;
    float* out_a = (float*)p->port[PORT_OUT_A];
    float* out_b = (float*)p->port[PORT_OUT_B];
    const double fade_to = ctl(p, PORT_ENABLED, 0, 1, 1) > 0.5 ? 1.0 : 0.0;
    const double cab_to = ctl(p, PORT_CAB, 0, 1, 1) > 0.5 ? 1.0 : 0.0;
    const double volts_to = ctl(p, PORT_VOLTS, 0.1, 10.0, 1.0);
    const double gain_to = pow(10.0, ctl(p, PORT_OUTPUT, -24.0, 12.0, 0.0) / 20.0);
    double t[NKNOB];
    uint32_t i;
#ifdef SH_HAS_CSR
    fpstate csr;
#endif

    if (!in || !out_a || !out_b)
        return;
#ifdef SH_HAS_CSR
    csr = fp_flush_denormals();
#endif
    knob_targets(p, t);
    if (os_factor(p->fs, want_high(p)) != p->os) {
        /* A new rate: the circuit restarts from its operating point at that rate (a few
         * milliseconds of work, once). */
        AmpPots pots;
        set_quality(p);
        pots_from_knobs(p->knob, &pots);
        amp_reset(&p->amp, &pots);
        p->asleep = p->quiet = p->win = 0;
        p->in_peak = p->out_peak = 0.0;
    }

    for (i = 0; i < n; ++i) {
        /* Both inputs are read before either output is written, so processing in place is fine.
         * The amp is mono: the inputs are averaged, so a mono source on both gives its own level. */
        double xa = in[i], xb = in_b[i], x, xv, y, spk, cab;
        if (!isfinite(xa))
            xa = 0.0;
        if (!isfinite(xb))
            xb = 0.0;
        x = 0.5 * (xa + xb);
        if (p->sub == 0) {
            AmpPots pots;
            int k;
            p->moving = 0;
            for (k = 0; k < NKNOB; ++k) {
                const double d = t[k] - p->knob[k];
                p->knob[k] += (k == 0 ? p->ch_coef : p->sm_coef) * d;
                p->moving |= fabs(d) > 1e-6;
            }
            if (p->moving)
                p->asleep = 0;
            if (!p->asleep) {
                pots_from_knobs(p->knob, &pots);
                amp_set_pots(&p->amp, &pots);
                amp_update(&p->amp);
            }
        }
        p->sub = (p->sub + 1) % SUB;
        p->volts += 0.002 * (volts_to - p->volts);
        p->gain += 0.002 * (gain_to - p->gain);
        p->cab_mix += p->cab_coef * (cab_to - p->cab_mix);
        if (p->fade < fade_to)
            p->fade = fade_to < p->fade + p->fade_step ? fade_to : p->fade + p->fade_step;
        else if (p->fade > fade_to)
            p->fade = fade_to > p->fade - p->fade_step ? fade_to : p->fade - p->fade_step;

        /* The amp keeps running while the host bypasses it, so it comes back without a thump. */
        xv = x * p->volts;
        if (p->asleep && fabs(xv) > p->wake_in)
            p->asleep = 0;
        if (p->asleep) {
            spk = 0.0;
        } else {
            spk = run_amp(p, xv);
            if (!isfinite(spk)) {
                AmpPots pots;
                pots_from_knobs(p->knob, &pots);
                amp_reset(&p->amp, &pots);
                spk = 0.0;
            }
            /* Watch for silence, a window at a time. */
            p->in_peak = fmax(p->in_peak, fabs(xv));
            p->out_peak = fmax(p->out_peak, fabs(spk));
            if (++p->win >= p->win_len) {
                p->quiet = (p->out_peak < SLEEP_OUT && !p->moving) ? p->quiet + 1 : 0;
                if (p->quiet >= SLEEP_WINDOWS) {
                    p->asleep = 1;
                    p->quiet = 0;
                    p->wake_in = fmax(4.0 * p->in_peak, WAKE_FLOOR);
                }
                p->win = 0;
                p->in_peak = p->out_peak = 0.0;
            }
        }
        cab = cab_tick(&p->cab, spk);
        y = (p->cab_mix * cab + (1.0 - p->cab_mix) * spk) * OUT_SCALE * p->gain;
        out_a[i] = (float)((1.0 - p->fade) * xa + p->fade * y);
        out_b[i] = (float)((1.0 - p->fade) * xb + p->fade * y);
    }

    if (p->port[PORT_POWER]) {
        double v = sqrt(p->amp.power / POWER_FULL);
        *(float*)p->port[PORT_POWER] = (float)(v > 1.0 ? 1.0 : v);
    }
#ifdef SH_HAS_CSR
    fp_restore(csr);
#endif
}

void sh_engine_free(ShEngine* h)
{
    Plugin* p = (Plugin*)h;
    if (!p)
        return;
    amp_free(&p->amp);
    free(p);
}
