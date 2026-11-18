/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* Sound regression check and CPU benchmark for the Schallmauer engine.
 *
 *   golden render <file> [high]  renders the scenarios below at 48 kHz and saves them
 *   golden compare <ref> <new>   compares two renders scenario by scenario
 *   golden bench [high]          times each scenario (percent of one core, real time)
 *
 * "high" sets the Quality control to High (4x oversampling at 48 kHz).
 *
 * Render the reference from a known-good build (for example the v1.0.0 tag), render again
 * after a change, and compare. The comparison reports the waveform error (RMS of the
 * difference, relative to the reference), the worst 20 ms window of it, the level difference,
 * and the largest difference in third-octave band levels from 60 Hz to 12 kHz, which ignores
 * phase and so is the fairer measure where the oversampling filters change. */
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#    define _CRT_SECURE_NO_WARNINGS /* fopen and getenv */
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../src/engine.c"
#include "../src/amp.c"

#define FS 48000.0
#define BLOCK 256
#define PI 3.14159265358979323846

typedef struct {
    const char* name;
    double secs;
} Scenario;

/* Port values for the scenarios: the defaults, then each scenario's changes. */
typedef struct {
    float c[PORT_COUNT];
} Ports;

static void defaults(Ports* p)
{
    int i;
    memset(p, 0, sizeof *p);
    for (i = PORT_CHANNEL; i < PORT_COUNT; ++i)
        if (sh_param(i))
            p->c[i] = sh_param(i)->def;
}

/* A plucked note: harmonics decaying faster as they go up, with a sharp attack. */
static double pluck(double t, double f, double amp)
{
    double s = 0.0;
    int k;
    if (t < 0.0)
        return 0.0;
    for (k = 1; k <= 12; ++k)
        s += sin(2.0 * PI * f * k * t + 0.3 * k) * exp(-t * (1.5 + 0.8 * k)) / k;
    return amp * s * (1.0 - exp(-t * 2000.0));
}

/* A riff: notes every 0.25 s, with a chord every second. */
static double riff(double t, double amp)
{
    static const double notes[8] = { 82.41, 82.41, 98.0, 82.41, 110.0, 82.41, 123.47, 116.54 };
    const int i = (int)(t / 0.25);
    const double u = t - i * 0.25;
    double s = pluck(u, notes[i % 8], amp);
    if (i % 4 == 0)
        s += pluck(u, notes[i % 8] * 1.5, 0.7 * amp) + pluck(u, notes[i % 8] * 2.0, 0.5 * amp);
    if (i > 0) /* the previous note rings on, damped */
        s += 0.3 * pluck(u + 0.25, notes[(i - 1) % 8], amp);
    return s;
}

static const Scenario SCEN[] = {
    { "silence, high gain", 1.0 },
    { "riff, high gain defaults", 3.0 },
    { "riff, high gain flat out", 3.0 },
    { "riff, clean defaults", 3.0 },
    { "riff, clean pushed", 3.0 },
    { "quiet sweep, high gain", 2.0 },
    { "channel switch while playing", 3.0 },
    { "gain and treble moving", 3.0 },
    { "note then silence (tail)", 3.0 },
    { "quiet sweep, clean (linear)", 2.0 },
    { "-90 dBFS input noise, high gain", 2.0 },
};
#define NSCEN ((int)(sizeof SCEN / sizeof SCEN[0]))

/* Input and port changes for scenario s at sample i. Returns the input in volts at 0 dBFS. */
static float scen_input(int s, long i, Ports* p)
{
    const double t = i / FS;
    switch (s) {
    case 0:
        return 0.0f;
    case 1:
        return (float)riff(t, 0.12);
    case 2:
        p->c[PORT_HG_GAIN] = 1.0f;
        p->c[PORT_HG_BASS] = p->c[PORT_HG_MIDDLE] = p->c[PORT_HG_TREBLE] = 1.0f;
        p->c[PORT_HG_PRESENCE] = p->c[PORT_HG_VOLUME] = 1.0f;
        p->c[PORT_MASTER] = 0.5f;
        return (float)riff(t, 0.3);
    case 3:
        p->c[PORT_CHANNEL] = 0.0f;
        return (float)riff(t, 0.2);
    case 4:
        p->c[PORT_CHANNEL] = 0.0f;
        p->c[PORT_CL_GAIN] = 1.0f;
        p->c[PORT_MASTER] = 0.6f;
        return (float)riff(t, 0.5);
    case 5: { /* log sweep 40 Hz - 10 kHz at 10 mV */
        const double T = 2.0, f0 = 40.0, f1 = 10000.0, k = log(f1 / f0);
        return (float)(0.01 * sin(2.0 * PI * f0 * T / k * (exp(t / T * k) - 1.0)));
    }
    case 6:
        p->c[PORT_CHANNEL] = (t > 1.0 && t < 2.0) ? 0.0f : 1.0f;
        return (float)riff(t, 0.15);
    case 7:
        p->c[PORT_HG_GAIN] = (float)(0.5 + 0.5 * sin(2.0 * PI * 0.4 * t));
        p->c[PORT_HG_TREBLE] = (float)(0.5 - 0.5 * sin(2.0 * PI * 0.7 * t));
        return (float)riff(t, 0.12);
    case 8:
        return (float)(t < 0.6 ? pluck(t, 82.41, 0.3) : 0.0);
    case 9: { /* log sweep 40 Hz - 10 kHz at 2 mV through a clean channel that stays linear */
        const double T = 2.0, f0 = 40.0, f1 = 10000.0, k = log(f1 / f0);
        p->c[PORT_CHANNEL] = 0.0f;
        p->c[PORT_CL_GAIN] = 0.3f;
        p->c[PORT_MASTER] = 0.3f;
        return (float)(0.002 * sin(2.0 * PI * f0 * T / k * (exp(t / T * k) - 1.0)));
    }
    case 10: { /* an audio interface's noise floor: white noise at -90 dBFS RMS */
        static unsigned int seed;
        if (i == 0)
            seed = 12345u;
        seed = seed * 1664525u + 1013904223u;
        return (float)(3.16e-5 * 1.732 * ((seed >> 8) / 8388608.0 - 1.0));
    }
    }
    return 0.0f;
}

static long scen_len(int s) { return (long)(SCEN[s].secs * FS); }

/* Renders scenario s into out (mono: output A). Returns the seconds of CPU it took. */
static int high_quality; /* render with Quality on High */

static double render(int s, float* out)
{
    const long n = scen_len(s);
    ShEngine* e = sh_engine_new(FS);
    float in[BLOCK], oa[BLOCK], ob[BLOCK];
    Ports p;
    long pos;
    int i;
    clock_t t0;
    defaults(&p);
    scen_input(s, 0, &p);
    p.c[PORT_QUALITY] = (float)high_quality;
    for (i = PORT_CHANNEL; i < PORT_COUNT; ++i)
        sh_engine_connect(e, (uint32_t)i, &p.c[i]);
    sh_engine_connect(e, PORT_IN_A, in);
    sh_engine_connect(e, PORT_IN_B, in);
    sh_engine_connect(e, PORT_OUT_A, oa);
    sh_engine_connect(e, PORT_OUT_B, ob);
    sh_engine_activate(e);
    t0 = clock();
    for (pos = 0; pos < n; pos += BLOCK) {
        const long k = n - pos < BLOCK ? n - pos : BLOCK;
        long j;
        for (j = 0; j < k; ++j)
            in[j] = scen_input(s, pos + j, &p); /* ports take the block's last values */
        sh_engine_run(e, (uint32_t)k);
        memcpy(out + pos, oa, sizeof(float) * k);
    }
    {
        const double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
        sh_engine_free(e);
        return secs;
    }
}

/* ---- Third-octave band levels, from an averaged power spectrum ---- */
#define FFT_N 8192

static void fft(double* re, double* im, int n)
{
    int i, j, k, len;
    for (i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i];
            re[i] = re[j];
            re[j] = t;
            t = im[i];
            im[i] = im[j];
            im[j] = t;
        }
    }
    for (len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * PI / len;
        for (i = 0; i < n; i += len)
            for (k = 0; k < len / 2; ++k) {
                const double wr = cos(ang * k), wi = sin(ang * k);
                const double ur = re[i + k], ui = im[i + k];
                const double vr = re[i + k + len / 2] * wr - im[i + k + len / 2] * wi;
                const double vi = re[i + k + len / 2] * wi + im[i + k + len / 2] * wr;
                re[i + k] = ur + vr;
                im[i + k] = ui + vi;
                re[i + k + len / 2] = ur - vr;
                im[i + k + len / 2] = ui - vi;
            }
    }
}

#define NBANDS 24 /* 60 Hz to about 12 kHz */
static void bands(const float* x, long n, double* b)
{
    static double re[FFT_N], im[FFT_N], ps[FFT_N / 2];
    long start;
    int i, k;
    memset(ps, 0, sizeof ps);
    for (start = 0; start + FFT_N <= n; start += FFT_N / 2) {
        for (i = 0; i < FFT_N; ++i) {
            const double w = 0.5 - 0.5 * cos(2.0 * PI * i / FFT_N);
            re[i] = x[start + i] * w;
            im[i] = 0.0;
        }
        fft(re, im, FFT_N);
        for (i = 0; i < FFT_N / 2; ++i)
            ps[i] += re[i] * re[i] + im[i] * im[i];
    }
    for (k = 0; k < NBANDS; ++k) {
        const double fc = 60.0 * pow(2.0, k / 3.0);
        const int lo = (int)(fc * pow(2.0, -1.0 / 6.0) * FFT_N / FS);
        const int hi = (int)(fc * pow(2.0, 1.0 / 6.0) * FFT_N / FS);
        double s = 1e-30;
        for (i = lo; i <= hi && i < FFT_N / 2; ++i)
            s += ps[i];
        b[k] = 10.0 * log10(s);
    }
}

static double rms(const float* x, long n)
{
    double s = 0.0;
    long i;
    for (i = 0; i < n; ++i)
        s += (double)x[i] * x[i];
    return sqrt(s / (n > 0 ? n : 1));
}

static double dbv(double x) { return 20.0 * log10(x + 1e-30); }

static int cmd_render(const char* path)
{
    FILE* f = fopen(path, "wb");
    int s;
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return 1;
    }
    for (s = 0; s < NSCEN; ++s) {
        const long n = scen_len(s);
        float* y = (float*)malloc(sizeof(float) * n);
        const double secs = render(s, y);
        fwrite(y, sizeof(float), (size_t)n, f);
        printf("%-32s %6.1f dBFS RMS  %5.1f%% of one core\n", SCEN[s].name, dbv(rms(y, n)), 100.0 * secs / SCEN[s].secs);
        free(y);
    }
    fclose(f);
    return 0;
}

static int load(FILE* f, float* y, long n) { return fread(y, sizeof(float), (size_t)n, f) == (size_t)n; }

static int cmd_compare(const char* a, const char* b)
{
    FILE* fa = fopen(a, "rb");
    FILE* fb = fopen(b, "rb");
    int s;
    if (!fa || !fb) {
        fprintf(stderr, "cannot read %s\n", fa ? b : a);
        return 1;
    }
    printf("%-32s %9s %9s %8s %9s %9s\n", "scenario", "error", "worst20ms", "level", "bands", "peakdiff");
    for (s = 0; s < NSCEN; ++s) {
        const long n = scen_len(s), w = (long)(0.02 * FS);
        float* ya = (float*)malloc(sizeof(float) * n);
        float* yb = (float*)malloc(sizeof(float) * n);
        float* d = (float*)malloc(sizeof(float) * n);
        double ba[NBANDS], bb[NBANDS], worst = -400.0, band = 0.0, ra, peak = 0.0;
        long i;
        int k;
        if (!load(fa, ya, n) || !load(fb, yb, n)) { /* a render from before this scenario existed */
            printf("%-32s (not in both files)\n", SCEN[s].name);
            free(ya);
            free(yb);
            free(d);
            break;
        }
        for (i = 0; i < n; ++i) {
            d[i] = ya[i] - yb[i];
            peak = fmax(peak, fabs(d[i]));
        }
        ra = rms(ya, n);
        for (i = 0; i + w <= n; i += w / 2) {
            const double r = rms(ya + i, w);
            if (r > 0.01 * ra) /* skip windows that are nearly silent in the reference */
                worst = fmax(worst, dbv(rms(d + i, w) / r));
        }
        bands(ya, n, ba);
        bands(yb, n, bb);
        {
            double top = -400.0;
            for (k = 0; k < NBANDS; ++k)
                top = fmax(top, ba[k]);
            for (k = 0; k < NBANDS; ++k) /* bands within 60 dB of the loudest */
                if (ba[k] > top - 60.0)
                    band = fmax(band, fabs(ba[k] - bb[k]));
            if (getenv("GOLDEN_BANDS")) { /* each band: Hz, level below the loudest, change */
                for (k = 0; k < NBANDS; ++k)
                    printf("  %5.0f:%+5.0f/%+5.1f", 60.0 * pow(2.0, k / 3.0), ba[k] - top, bb[k] - ba[k]);
                printf("\n");
            }
        }
        if (ra < 1e-6) /* the silent scenario: report the largest output instead */
            printf("%-32s %9s %9s %8s %9s %8.1f\n", SCEN[s].name, "-", "-", "-", "-", dbv(peak));
        else
            printf("%-32s %7.1fdB %7.1fdB %6.2fdB %7.2fdB %7.1fdB\n", SCEN[s].name, dbv(rms(d, n) / ra), worst,
                   dbv(rms(yb, n)) - dbv(ra), band, dbv(peak));
        free(ya);
        free(yb);
        free(d);
    }
    fclose(fa);
    fclose(fb);
    return 0;
}

static int cmd_bench(void)
{
    int s;
    for (s = 0; s < NSCEN; ++s) {
        const long n = scen_len(s);
        float* y = (float*)malloc(sizeof(float) * n);
        const double secs = render(s, y);
        printf("%-32s %5.1f%% of one core\n", SCEN[s].name, 100.0 * secs / SCEN[s].secs);
        free(y);
    }
    return 0;
}

int main(int argc, char** argv)
{
    high_quality = argc > 2 && !strcmp(argv[argc - 1], "high");
    if (argc == 3 + high_quality && !strcmp(argv[1], "render"))
        return cmd_render(argv[2]);
    if (argc == 4 && !strcmp(argv[1], "compare"))
        return cmd_compare(argv[2], argv[3]);
    if (argc == 2 + high_quality && !strcmp(argv[1], "bench"))
        return cmd_bench();
    fprintf(stderr, "usage: golden render <file> | golden compare <ref> <new> | golden bench\n");
    return 2;
}
