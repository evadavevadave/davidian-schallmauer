/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* The Schallmauer engine as a small C API, shared by the LV2, VST3 and Audio Unit builds.
 * It follows the LV2 model: every port in ports.h is a float pointer the wrapper connects,
 * control ports read one value per block, and the power lamp output is written per block. */
#ifndef SH_ENGINE_H
#define SH_ENGINE_H

#include <stdint.h>

#include "ports.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ShEngine ShEngine;

ShEngine* sh_engine_new(double sample_rate); /* allocates; not real-time safe */
void sh_engine_connect(ShEngine* e, uint32_t port, void* data);
void sh_engine_activate(ShEngine* e); /* resets the circuit; connect the controls first */
void sh_engine_run(ShEngine* e, uint32_t frames); /* in place is fine: each sample is read before it is written */
void sh_engine_free(ShEngine* e);

#ifdef __cplusplus
}
#endif

#endif
