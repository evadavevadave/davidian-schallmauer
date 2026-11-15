/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* Control metadata for the hosts that don't read bundle/schallmauer.ttl: the VST3 and Audio
 * Unit builds. Values are the same plain values as the LV2 ports, so a setting means the same
 * thing in every format. test/params_check.c checks this table against the TTL and the panel. */
#ifndef SH_PARAMS_H
#define SH_PARAMS_H

#include <math.h>
#include <stdio.h>

#include "ports.h"

typedef struct {
    int port;          /* also the parameter ID in VST3 and the Audio Unit */
    const char* name;
    const char* short_name;
    float min, max, def;
    int log;           /* logarithmic */
    int steps;         /* 0: continuous; otherwise a switch with steps + 1 positions */
    const char* unit;  /* "V", "dB", or "" for 0-1 knobs shown as 0-10 like the panel */
    const char* opts[3];
    int output;        /* read only: the power lamp */
} ShParam;

/* Defaults are the owner's manual's starting settings (o'clock positions as 0-10). */
static const ShParam SH_PARAMS[] = {
    { PORT_CHANNEL, "Channel", "Channel", 0, 1, 1, 0, 1, "", { "Clean", "High Gain", 0 }, 0 },
    { PORT_CL_GAIN, "Clean Gain", "Cl Gain", 0, 1, 0.8f, 0, 0, "", { 0, 0, 0 }, 0 },
    { PORT_CL_BASS, "Clean Bass", "Cl Bass", 0, 1, 0.4f, 0, 0, "", { 0, 0, 0 }, 0 },
    { PORT_CL_MIDDLE, "Clean Middle", "Cl Mid", 0, 1, 1.0f, 0, 0, "", { 0, 0, 0 }, 0 },
    { PORT_CL_TREBLE, "Clean Treble", "Cl Treb", 0, 1, 1.0f, 0, 0, "", { 0, 0, 0 }, 0 },
    { PORT_CL_PRESENCE, "Clean Presence", "Cl Pres", 0, 1, 1.0f, 0, 0, "", { 0, 0, 0 }, 0 },
    { PORT_CL_VOLUME, "Clean Volume", "Cl Vol", 0, 1, 1.0f, 0, 0, "", { 0, 0, 0 }, 0 },
    { PORT_HG_GAIN, "High Gain Gain", "HG Gain", 0, 1, 0.6f, 0, 0, "", { 0, 0, 0 }, 0 },
    { PORT_HG_BASS, "High Gain Bass", "HG Bass", 0, 1, 0.6f, 0, 0, "", { 0, 0, 0 }, 0 },
    { PORT_HG_MIDDLE, "High Gain Middle", "HG Mid", 0, 1, 0.6f, 0, 0, "", { 0, 0, 0 }, 0 },
    { PORT_HG_TREBLE, "High Gain Treble", "HG Treb", 0, 1, 0.6f, 0, 0, "", { 0, 0, 0 }, 0 },
    { PORT_HG_PRESENCE, "High Gain Presence", "HG Pres", 0, 1, 0.6f, 0, 0, "", { 0, 0, 0 }, 0 },
    { PORT_HG_VOLUME, "High Gain Volume", "HG Vol", 0, 1, 0.6f, 0, 0, "", { 0, 0, 0 }, 0 },
    { PORT_MASTER, "Master", "Master", 0, 1, 0.2f, 0, 0, "", { 0, 0, 0 }, 0 },
    { PORT_CAB, "Cabinet Sim", "Cab", 0, 1, 1, 0, 1, "", { "Off", "4x12", 0 }, 0 },
    { PORT_VOLTS, "Volts at 0 dBFS", "Volts", 0.1f, 10.0f, 1.0f, 1, 0, "V", { 0, 0, 0 }, 0 },
    { PORT_OUTPUT, "Output", "Output", -24.0f, 12.0f, 0.0f, 0, 0, "dB", { 0, 0, 0 }, 0 },
    { PORT_ENABLED, "Enabled", "Enabled", 0, 1, 1, 0, 1, "", { "Off", "On", 0 }, 0 },
    { PORT_POWER, "Output Power", "Power", 0, 1, 0, 0, 0, "", { 0, 0, 0 }, 1 },
};
#define SH_NUM_PARAMS ((int)(sizeof SH_PARAMS / sizeof SH_PARAMS[0]))

/* The first control port and the number of saved values: every input control, in port order. */
#define SH_FIRST_CONTROL PORT_CHANNEL
#define SH_NUM_INPUTS (PORT_ENABLED - PORT_CHANNEL + 1)

static const ShParam* sh_param(int port)
{
    int i;
    for (i = 0; i < SH_NUM_PARAMS; ++i)
        if (SH_PARAMS[i].port == port)
            return &SH_PARAMS[i];
    return NULL;
}

static float sh_clamp(const ShParam* p, float v)
{
    if (!(v >= p->min)) /* also catches NaN */
        v = p->min;
    if (v > p->max)
        v = p->max;
    return p->steps ? floorf(v + 0.5f) : v;
}

/* 0-1 host value to plain value. Switches split 0-1 into equal parts, as the VST3 SDK does. */
static float sh_from_norm(const ShParam* p, double n)
{
    n = n < 0.0 ? 0.0 : (n > 1.0 ? 1.0 : n);
    if (p->steps) {
        int i = (int)(n * (p->steps + 1));
        return p->min + (float)(i > p->steps ? p->steps : i);
    }
    if (p->log)
        return (float)(p->min * pow((double)p->max / p->min, n));
    return (float)(p->min + n * (p->max - p->min));
}

static double sh_to_norm(const ShParam* p, float v)
{
    v = sh_clamp(p, v);
    if (p->steps)
        return (double)(v - p->min) / p->steps;
    if (p->log)
        return log((double)v / p->min) / log((double)p->max / p->min);
    return (double)(v - p->min) / (p->max - p->min);
}

/* Text for a value, matching the panel's display. */
static void sh_format(const ShParam* p, float v, char* buf, size_t n)
{
    v = sh_clamp(p, v);
    if (p->steps)
        snprintf(buf, n, "%s", p->opts[(int)(v - p->min)]);
    else if (p->unit[0] == 'd')
        snprintf(buf, n, "%+.1f", v);
    else if (p->unit[0])
        snprintf(buf, n, "%.2f", v);
    else if (p->output)
        snprintf(buf, n, "%.0f%%", 100.0f * v);
    else
        snprintf(buf, n, "%.1f", 10.0f * v);
}

/* The reverse of sh_format; returns 0 if the text means nothing. */
static int sh_parse(const ShParam* p, const char* s, float* out)
{
    double d;
    int i;
    for (i = 0; p->steps && i <= p->steps; ++i) {
        const char *a = p->opts[i], *b = s;
        while (*a && *b && (*a | 32) == (*b | 32))
            ++a, ++b;
        if (!*a && !*b) {
            *out = p->min + (float)i;
            return 1;
        }
    }
    if (sscanf(s, "%lf", &d) != 1)
        return 0;
    if (!p->unit[0] && !p->steps)
        d /= 10.0; /* 0-10 scale */
    *out = sh_clamp(p, (float)d);
    return 1;
}

#endif
