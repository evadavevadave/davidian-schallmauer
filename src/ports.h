/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* Port indices, shared by the plugin and its GUI. They must match bundle/schallmauer.ttl. */
#ifndef PORTS_H
#define PORTS_H

#define SH_URI    "urn:davidian:schallmauer"
#define SH_UI_URI "urn:davidian:schallmauer#ui"

enum {
    PORT_IN_A, PORT_IN_B, PORT_OUT_A, PORT_OUT_B,
    PORT_CHANNEL,
    PORT_CL_GAIN, PORT_CL_BASS, PORT_CL_MIDDLE, PORT_CL_TREBLE, PORT_CL_PRESENCE, PORT_CL_VOLUME,
    PORT_HG_GAIN, PORT_HG_BASS, PORT_HG_MIDDLE, PORT_HG_TREBLE, PORT_HG_PRESENCE, PORT_HG_VOLUME,
    PORT_MASTER, PORT_CAB, PORT_VOLTS, PORT_OUTPUT, PORT_ENABLED,
    PORT_POWER, /* output: output power 0-1, for the GUI's power lamp */
    PORT_COUNT
};

#endif
