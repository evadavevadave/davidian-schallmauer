/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* The Schallmauer panel in a native window: a Win32 child window, a Cocoa NSView or an X11 window.
 * The LV2 UI, the VST3 editor and the Audio Unit view all use this, each with its own host
 * callbacks. view.c is compiled as Objective-C on macOS. */
#ifndef SH_VIEW_H_INCLUDED
#define SH_VIEW_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

#define SH_VIEW_W 960
#define SH_VIEW_H 470

typedef struct ShView ShView;

typedef struct {
    void* host;
    /* A plain value (as in ports.h) the user set on the panel. */
    void (*write)(void* host, int port, float value);
    /* Optional: brackets each edit, begin = 1 then begin = 0. */
    void (*gesture)(void* host, int port, int begin);
    /* Optional: called about 30 times a second on the UI thread, before the panel redraws. */
    void (*tick)(void* host);
} ShViewHost;

/* Opens the panel inside parent (an HWND, NSView* or X11 Window, cast to void*), or as its own
 * view or window when parent is NULL. Returns NULL on failure. */
ShView* sh_view_open(void* parent, const ShViewHost* host);
/* The HWND, NSView* or X11 Window. */
void* sh_view_widget(ShView* v);
/* A value from the host; it shows on the next redraw. */
void sh_view_set(ShView* v, int port, float value);
/* X11 has no event loop of its own here, so the host's idle call or timer pumps it. Returns 0. */
int sh_view_idle(ShView* v);
/* X11: the display connection's file descriptor, readable when events wait for sh_view_idle.
 * -1 on Windows and macOS, which deliver events themselves. */
int sh_view_fd(ShView* v);
void sh_view_close(ShView* v);

#ifdef __cplusplus
}
#endif

#endif
