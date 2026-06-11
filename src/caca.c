/*
 * Xcaca - KdCardFuncs implementation.
 * Based on xorg-server/hw/kdrive/ephyr/ephyr.c
 *
 * Copyright © 2004 Nokia
 * Copyright © 2025 Robin
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Nokia not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission. Nokia makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * NOKIA DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL NOKIA BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include "xcaca.h"

#include <stdlib.h>
#include <string.h>

#include "inputstr.h"
#include "scrnintstr.h"
#include "fb.h"
#include "shadow.h"

/* Evdev scancode helpers — duplicated from cacainput.c for key synthesis */
#define EV(kc) ((kc) + 8)
#define KEY_LEFTSHIFT  42
#define KEY_LEFTCTRL   29

#ifdef RANDR
#include "randrstr.h"
#endif

KdKeyboardInfo *cacaKbd   = NULL;
KdPointerInfo  *cacaMouse = NULL;

static void cacaScreenBlockHandler(ScreenPtr pScreen, void *timeout);
static Bool s_resize_pending = FALSE; /* set by resize handler, cleared after repaint */
static int  s_last_cols = -1;         /* canvas size after last processed resize */
static int  s_last_rows = -1;

/* . ݁₊ ⊹ . ݁˖ . ݁ Card-level lifecycle . ݁₊ ⊹ . ݁˖ . ݁ */

Bool
cacaCardInit(KdCardInfo *card)
{
    /* No card-private data needed. */
    card->driver = NULL;
    return TRUE;
}

void
cacaCardFini(KdCardInfo *card)
{
}

/* . ݁₊ ⊹ . ݁˖ . ݁ Screen initialisation . ݁₊ ⊹ . ݁˖ . ݁ */

Bool
cacaScreenInitialize(KdScreenInfo *screen)
{
    CacaScrPriv *scrpriv = screen->driver;

    if (!screen->width || !screen->height) {
        screen->width  = 640;
        screen->height = 480;
    }

    /* Fixed 32bpp ARGB framebuffer */
    screen->fb.depth       = 24;
    screen->fb.bitsPerPixel = 32;
    screen->fb.visuals     = (1 << TrueColor);
    screen->fb.redMask     = 0x00FF0000;
    screen->fb.greenMask   = 0x0000FF00;
    screen->fb.blueMask    = 0x000000FF;
    screen->rate           = 30;

    scrpriv->width  = screen->width;
    scrpriv->height = screen->height;
    scrpriv->screen = screen;

    return cacaMapFramebuffer(screen);
}

Bool
cacaMapFramebuffer(KdScreenInfo *screen)
{
    CacaScrPriv *scrpriv = screen->driver;
    size_t bytes = (size_t)screen->width * screen->height * 4;

    free(scrpriv->fb_data);
    scrpriv->fb_data = calloc(1, bytes);
    if (!scrpriv->fb_data) {
        ErrorF("Xcaca: failed to allocate framebuffer (%zu bytes)\n", bytes);
        return FALSE;
    }

    screen->fb.byteStride  = screen->width * 4;
    screen->fb.pixelStride = screen->width;
    screen->fb.frameBuffer = (CARD8 *)scrpriv->fb_data;

    return TRUE;
}

/* . ݁₊ ⊹ . ݁˖ . ݁ Screen init chain . ݁₊ ⊹ . ݁˖ . ݁ */

Bool
cacaInitScreen(ScreenPtr pScreen)
{
    /* Nothing xv/glamor-specific needed. */
    return TRUE;
}

Bool
cacaFinishInitScreen(ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    CacaScrPriv  *scrpriv = screen->driver;

    if (!shadowSetup(pScreen))
        return FALSE;

#ifdef RANDR
    if (!cacaRandRInit(pScreen))
        return FALSE;
#endif

    /* Tell the host about the new pixel dimensions */
    caca_host_screen_init(screen->width, screen->height);

    /* Wrap BlockHandler */
    scrpriv->BlockHandler    = pScreen->BlockHandler;
    pScreen->BlockHandler    = cacaScreenBlockHandler;

    return TRUE;
}

/* . ݁₊ ⊹ . ݁˖ . ݁ CreateResources . ݁₊ ⊹ . ݁˖ . ݁ */

Bool
cacaCreateResources(ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    CacaScrPriv  *scrpriv = screen->driver;
    PixmapPtr     pPixmap;

    scrpriv->pDamage = DamageCreate((DamageReportFunc) 0,
                                    (DamageDestroyFunc) 0,
                                    DamageReportNone, TRUE, pScreen, pScreen);
    if (!scrpriv->pDamage)
        return FALSE;

    pPixmap = (*pScreen->GetScreenPixmap)(pScreen);
    DamageRegister(&pPixmap->drawable, scrpriv->pDamage);

    return TRUE;
}

/* . ݁₊ ⊹ . ݁˖ . ݁ BlockHandler — main render/poll loop . ݁₊ ⊹ . ݁˖ . ݁ */

static void
cacaInternalDamageRedisplay(ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    CacaScrPriv  *scrpriv = screen->driver;
    RegionPtr     pRegion;

    if (!scrpriv || !scrpriv->pDamage)
        return;

    pRegion = DamageRegion(scrpriv->pDamage);
    if (RegionNotEmpty(pRegion) || s_resize_pending) {
        caca_host_paint(scrpriv->fb_data);
        DamageEmpty(scrpriv->pDamage);
        s_resize_pending = FALSE;
    }
}

/* Forward declarations for input helpers defined in cacainput.c */
int  caca_key_to_scancode(int key, Bool *needs_shift);
void caca_mouse_to_pixels(int cx, int cy);
void caca_enqueue_button(int btn, Bool release);


static void
cacaPollEvents(void)
{
    caca_event_t ev;

    while (caca_host_poll_event(&ev)) {
        enum caca_event_type type = caca_get_event_type(&ev);

        ErrorF("Xcaca: event type=0x%x\n", type);

        if (type & CACA_EVENT_KEY_PRESS) {
            int sym  = caca_get_event_key_ch(&ev);
            ErrorF("Xcaca: KEY_PRESS sym=%d (0x%x) cacaKbd=%p\n", sym, sym, (void*)cacaKbd);

            /* Ctrl+C: shut down the server (terminal convention) */
            if (sym == CACA_KEY_CTRL_C) {
                ErrorF("Xcaca: Ctrl+C received, shutting down\n");
                GiveUp(SIGTERM);
                return;
            }

            Bool needs_shift = FALSE;
            Bool is_ctrl = (sym >= CACA_KEY_CTRL_A && sym <= CACA_KEY_CTRL_Z);
            int scan = caca_key_to_scancode(sym, &needs_shift);
            if (scan) {
                /* Synthesise modifier press if needed */
                if (needs_shift)
                    KdEnqueueKeyboardEvent(cacaKbd, EV(KEY_LEFTSHIFT), FALSE);
                if (is_ctrl)
                    KdEnqueueKeyboardEvent(cacaKbd, EV(KEY_LEFTCTRL), FALSE);

                KdEnqueueKeyboardEvent(cacaKbd, scan, FALSE);

                /* Terminal backends never generate KEY_RELEASE events,
                 * so synthesise an immediate release to prevent the key
                 * from getting "stuck" (endless auto-repeat). */
                KdEnqueueKeyboardEvent(cacaKbd, scan, TRUE);

                if (is_ctrl)
                    KdEnqueueKeyboardEvent(cacaKbd, EV(KEY_LEFTCTRL), TRUE);
                if (needs_shift)
                    KdEnqueueKeyboardEvent(cacaKbd, EV(KEY_LEFTSHIFT), TRUE);
            }
        }
        else if (type & CACA_EVENT_KEY_RELEASE) {
            int sym  = caca_get_event_key_ch(&ev);
            int scan = caca_key_to_scancode(sym, NULL);
            if (scan)
                KdEnqueueKeyboardEvent(cacaKbd, scan, TRUE);
        }
        else if (type & CACA_EVENT_MOUSE_MOTION) {
            int cx = caca_get_event_mouse_x(&ev);
            int cy = caca_get_event_mouse_y(&ev);
            caca_mouse_to_pixels(cx, cy);
        }
        else if (type & CACA_EVENT_MOUSE_PRESS) {
            int btn = caca_get_event_mouse_button(&ev);
            caca_enqueue_button(btn, FALSE);
        }
        else if (type & CACA_EVENT_MOUSE_RELEASE) {
            int btn = caca_get_event_mouse_button(&ev);
            caca_enqueue_button(btn, TRUE);
        }
        else if (type & CACA_EVENT_RESIZE) {
            /* Only act on resizes where the canvas dimensions actually changed.
             * caca_refresh_display can trigger spurious RESIZE events without
             * changing the canvas size, causing an infinite repaint loop. */
            int new_cols, new_rows;
            caca_host_get_canvas_size(&new_cols, &new_rows);
            if (new_cols != s_last_cols || new_rows != s_last_rows) {
                s_last_cols = new_cols;
                s_last_rows = new_rows;
                if (screenInfo.numScreens > 0) {
                    KdPrivScreenPtr kp = KdGetScreenPriv(screenInfo.screens[0]);
                    if (kp && kp->screen)
                        caca_host_screen_init(kp->screen->width, kp->screen->height);
                }
                /* Damage was already consumed; force a repaint next cycle. */
                s_resize_pending = TRUE;
            }
        }
        else if (type & CACA_EVENT_QUIT) {
            GiveUp(SIGTERM);
            return;
        }
    }
}

static void
cacaScreenBlockHandler(ScreenPtr pScreen, void *timeout)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    CacaScrPriv  *scrpriv = screen->driver;

    pScreen->BlockHandler = scrpriv->BlockHandler;
    (*pScreen->BlockHandler)(pScreen, timeout);
    scrpriv->BlockHandler = pScreen->BlockHandler;
    pScreen->BlockHandler = cacaScreenBlockHandler;

    cacaInternalDamageRedisplay(pScreen);
    cacaPollEvents();

    /* libcaca has no pollable fd — we must poll in BlockHandler.
     * Cap the select/poll timeout so we wake up regularly to
     * check for new caca events and process any queued input. */
    AdjustWaitForDelay(timeout, 16);  /* ~60 Hz */
}

/* . ݁₊ ⊹ . ݁˖ . ݁ Screen/card fini . ݁₊ ⊹ . ݁˖ . ݁ */

void
cacaScreenFini(KdScreenInfo *screen)
{
    CacaScrPriv *scrpriv = screen->driver;

    scrpriv->BlockHandler = NULL;

    free(scrpriv->fb_data);
    scrpriv->fb_data = NULL;
}

void
cacaCloseScreen(ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    CacaScrPriv  *scrpriv = screen->driver;

    if (scrpriv->pDamage) {
        DamageDestroy(scrpriv->pDamage);
        scrpriv->pDamage = NULL;
    }
}

/* . ݁₊ ⊹ . ݁˖ . ݁ Color map stubs (TrueColor only) . ݁₊ ⊹ . ݁˖ . ݁ */

void
cacaGetColors(ScreenPtr pScreen, int n, xColorItem *pdefs)
{
    while (n--) {
        pdefs->red   = 0;
        pdefs->green = 0;
        pdefs->blue  = 0;
        pdefs++;
    }
}

void
cacaPutColors(ScreenPtr pScreen, int n, xColorItem *pdefs)
{
    /* TrueColor — nothing to do */
}

/* . ݁₊ ⊹ . ݁˖ . ݁ RandR . ݁₊ ⊹ . ݁˖ . ݁ */

#ifdef RANDR

Bool
cacaRandRGetInfo(ScreenPtr pScreen, Rotation *rotations)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    RRScreenSizePtr pSize;

    *rotations = RR_Rotate_0;

    pSize = RRRegisterSize(pScreen,
                           screen->width, screen->height,
                           screen->width_mm, screen->height_mm);
    RRSetCurrentConfig(pScreen, RR_Rotate_0, 0, pSize);

    return TRUE;
}

Bool
cacaRandRSetConfig(ScreenPtr pScreen, Rotation randr, int rate,
                   RRScreenSizePtr pSize)
{
    /* We don't support resize for now. */
    return FALSE;
}

Bool
cacaRandRInit(ScreenPtr pScreen)
{
    rrScrPrivPtr pScrPriv;

    if (!RRScreenInit(pScreen))
        return FALSE;

    pScrPriv = rrGetScrPriv(pScreen);
    pScrPriv->rrGetInfo   = cacaRandRGetInfo;
    pScrPriv->rrSetConfig = cacaRandRSetConfig;

    return TRUE;
}

#endif /* RANDR */

/* . ݁₊ ⊹ . ݁˖ . ݁ KdCardFuncs . ݁₊ ⊹ . ݁˖ . ݁ */

KdCardFuncs cacaFuncs = {
    cacaCardInit,           /* cardinit */
    cacaScreenInitialize,   /* scrinit */
    cacaInitScreen,         /* initScreen */
    cacaFinishInitScreen,   /* finishInitScreen */
    cacaCreateResources,    /* createRes */
    cacaScreenFini,         /* scrfini */
    cacaCardFini,           /* cardfini */

    0,                      /* initCursor */
    0,                      /* initAccel */
    0,                      /* enableAccel */
    0,                      /* disableAccel */
    0,                      /* finiAccel */

    cacaGetColors,          /* getColors */
    cacaPutColors,          /* putColors */

    cacaCloseScreen,        /* closeScreen */
};
