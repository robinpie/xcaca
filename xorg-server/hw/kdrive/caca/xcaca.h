/*
 * Xcaca - A kdrive X server that renders via libcaca ASCII art.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation. This software is provided "as is" without express or
 * implied warranty.
 */

#ifndef _XCACA_H_
#define _CACA_H_

#include <stdio.h>
#include <unistd.h>
#include <caca.h>

#include "os.h"
#include "kdrive.h"

#ifdef RANDR
#include "randrstr.h"
#endif

#include "damage.h"

typedef struct _CacaScrPriv {
    /* libcaca handles (owned by caca_host.c, mirrored here for cleanup) */
    caca_canvas_t      *canvas;
    caca_display_t     *display;
    caca_dither_t      *dither;

    /* damage tracking */
    DamagePtr           pDamage;

    /* BlockHandler chain */
    ScreenBlockHandlerProcPtr BlockHandler;

    /* framebuffer dimensions (pixels) */
    int                 width;
    int                 height;

    /* raw framebuffer (32bpp ARGB, width*height*4 bytes) */
    unsigned char      *fb_data;

    /* font cell aspect ratio: cell_width / cell_height
     * typical terminal ≈ 0.5 (cells are half as wide as tall) */
    float               cell_aspect_ratio;

    /* back-reference */
    KdScreenInfo       *screen;
} CacaScrPriv;

/* ------------------------------------------------------------------ */
/* Globals declared in cacainit.c / caca.c                             */
/* ------------------------------------------------------------------ */

extern KdCardFuncs       cacaFuncs;
extern KdKeyboardDriver  CacaKeyboardDriver;
extern KdPointerDriver   CacaMouseDriver;
extern KdKeyboardInfo   *cacaKbd;
extern KdPointerInfo    *cacaMouse;

/* ------------------------------------------------------------------ */
/* caca.c                                                              */
/* ------------------------------------------------------------------ */

Bool  cacaCardInit(KdCardInfo *card);
Bool  cacaScreenInitialize(KdScreenInfo *screen);
Bool  cacaInitScreen(ScreenPtr pScreen);
Bool  cacaFinishInitScreen(ScreenPtr pScreen);
Bool  cacaCreateResources(ScreenPtr pScreen);
void  cacaScreenFini(KdScreenInfo *screen);
void  cacaCardFini(KdCardInfo *card);
void  cacaGetColors(ScreenPtr pScreen, int n, xColorItem *pdefs);
void  cacaPutColors(ScreenPtr pScreen, int n, xColorItem *pdefs);
void  cacaCloseScreen(ScreenPtr pScreen);
Bool  cacaMapFramebuffer(KdScreenInfo *screen);

#ifdef RANDR
Bool  cacaRandRGetInfo(ScreenPtr pScreen, Rotation *rotations);
Bool  cacaRandRSetConfig(ScreenPtr pScreen, Rotation randr, int rate,
                         RRScreenSizePtr pSize);
Bool  cacaRandRInit(ScreenPtr pScreen);
#endif

/* ------------------------------------------------------------------ */
/* caca_host.c                                                         */
/* ------------------------------------------------------------------ */

Bool  caca_host_init(void);
void  caca_host_fini(void);
Bool  caca_host_screen_init(int width, int height);
void  caca_host_paint(void *framebuffer);
int   caca_host_poll_event(caca_event_t *ev);
void  caca_host_get_canvas_size(int *cols, int *rows);
void  caca_host_set_cell_aspect(float r);
float caca_host_query_cell_aspect(void);

/* dither config setters */
void  caca_host_set_dither_algorithm(const char *alg);
void  caca_host_set_charset(const char *charset);
void  caca_host_set_brightness(float b);
void  caca_host_set_gamma(float g);
void  caca_host_set_contrast(float c);

#endif /* _CACA_H_ */
