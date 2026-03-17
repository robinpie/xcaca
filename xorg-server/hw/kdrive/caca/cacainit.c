/*
 * Xcaca - Entry point, CLI argument parsing, KdCardFuncs registration.
 * Based on xorg-server/hw/kdrive/ephyr/ephyrinit.c
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

/* forward declarations from cacainput.c */
extern int  caca_key_to_scancode(int key, Bool *needs_shift);
extern void caca_mouse_to_pixels(int cx, int cy);
extern void caca_enqueue_button(int btn, Bool release);
/* forward from caca.c */
extern void cacaScreenBlockHandler(ScreenPtr pScreen, void *timeout);

extern Bool kdHasPointer;
extern Bool kdHasKbd;

/* . ݁₊ ⊹ . ݁˖ . ݁ main . ݁₊ ⊹ . ݁˖ . ݁ */

int
main(int argc, char *argv[], char *envp[])
{
    return dix_main(argc, argv, envp);
}

/* . ݁₊ ⊹ . ݁˖ . ݁ Kdrive entry points . ݁₊ ⊹ . ݁˖ . ݁ */

void
InitCard(char *name)
{
    KdCardInfoAdd(&cacaFuncs, 0);
}

void
InitOutput(ScreenInfo *pScreenInfo, int argc, char **argv)
{
    KdInitOutput(pScreenInfo, argc, argv);
}

void
InitInput(int argc, char **argv)
{
    KdKeyboardInfo *ki;
    KdPointerInfo  *pi;

    KdAddKeyboardDriver(&CacaKeyboardDriver);
    KdAddPointerDriver(&CacaMouseDriver);

    if (!kdHasKbd) {
        ki = KdNewKeyboard();
        if (!ki)
            FatalError("Xcaca: couldn't create keyboard\n");
        ki->driver = &CacaKeyboardDriver;
        KdAddKeyboard(ki);
    }

    if (!kdHasPointer) {
        pi = KdNewPointer();
        if (!pi)
            FatalError("Xcaca: couldn't create pointer\n");
        pi->driver = &CacaMouseDriver;
        KdAddPointer(pi);
    }

    KdInitInput();
}

void
CloseInput(void)
{
    KdCloseInput();
}

#if INPUTTHREAD
void
ddxInputThreadInit(void)
{
}
#endif

#ifdef DDXBEFORERESET
void
ddxBeforeReset(void)
{
}
#endif

/* . ݁₊ ⊹ . ݁˖ . ݁ CLI argument parsing . ݁₊ ⊹ . ݁˖ . ݁ */

/*
 * Add a screen at the given size string (e.g. "640x480").
 */
static void
processScreenArg(const char *screen_size)
{
    KdCardInfo   *card;
    KdScreenInfo *screen;

    InitCard(0);
    card = KdCardInfoLast();
    if (!card) {
        ErrorF("Xcaca: no card found!\n");
        return;
    }

    screen = KdScreenInfoAdd(card);
    KdParseScreen(screen, screen_size);

    screen->driver = calloc(1, sizeof(CacaScrPriv));
    if (!screen->driver)
        FatalError("Xcaca: couldn't alloc screen private\n");

    /* Set default cell aspect ratio */
    ((CacaScrPriv *)screen->driver)->cell_aspect_ratio = 0.5f;
}

int
ddxProcessArgument(int argc, char **argv, int i)
{
    if (!strcmp(argv[i], "-screen")) {
        if (i + 1 < argc) {
            processScreenArg(argv[i + 1]);
            return 2;
        }
        UseMsg();
        exit(1);
    }
    else if (!strcmp(argv[i], "-dither")) {
        if (i + 1 < argc) {
            caca_host_set_dither_algorithm(argv[i + 1]);
            return 2;
        }
        UseMsg();
        exit(1);
    }
    else if (!strcmp(argv[i], "-charset")) {
        if (i + 1 < argc) {
            caca_host_set_charset(argv[i + 1]);
            return 2;
        }
        UseMsg();
        exit(1);
    }
    else if (!strcmp(argv[i], "-brightness")) {
        if (i + 1 < argc) {
            caca_host_set_brightness(atof(argv[i + 1]));
            return 2;
        }
        UseMsg();
        exit(1);
    }
    else if (!strcmp(argv[i], "-gamma")) {
        if (i + 1 < argc) {
            caca_host_set_gamma(atof(argv[i + 1]));
            return 2;
        }
        UseMsg();
        exit(1);
    }
    else if (!strcmp(argv[i], "-contrast")) {
        if (i + 1 < argc) {
            caca_host_set_contrast(atof(argv[i + 1]));
            return 2;
        }
        UseMsg();
        exit(1);
    }
    else if (!strcmp(argv[i], "-cell-aspect")) {
        if (i + 1 < argc) {
            const char *val = argv[i + 1];
            float r;
            if (!strcmp(val, "auto")) {
                r = caca_host_query_cell_aspect();
            } else {
                r = atof(val);
            }
            caca_host_set_cell_aspect(r);
            /* Also update any already-created screen privates */
            {
                int s;
                for (s = 0; s < screenInfo.numScreens; s++) {
                    ScreenPtr pScreen = screenInfo.screens[s];
                    KdPrivScreenPtr kp = KdGetScreenPriv(pScreen);
                    if (kp && kp->screen && kp->screen->driver) {
                        ((CacaScrPriv *)kp->screen->driver)->cell_aspect_ratio = r;
                    }
                }
            }
            return 2;
        }
        UseMsg();
        exit(1);
    }

    return KdProcessArgument(argc, argv, i);
}

void
ddxUseMsg(void)
{
    KdUseMsg();

    ErrorF("\nXcaca Options:\n");
    ErrorF("-screen WxH            Set screen resolution (default: 640x480)\n");
    ErrorF("-dither <alg>          Dither algorithm (none,ordered2,ordered4,\n");
    ErrorF("                       ordered8,random,fstein) [default: fstein]\n");
    ErrorF("-charset <set>         Character set (ascii,blocks,shades,utf8,...)\n");
    ErrorF("-brightness <f>        Brightness multiplier (default: 1.0)\n");
    ErrorF("-gamma <f>             Gamma correction (default: 1.0)\n");
    ErrorF("-contrast <f>          Contrast adjustment (default: 1.0)\n");
    ErrorF("-cell-aspect <f|auto>  Cell width/height ratio (default: 0.5)\n");
    ErrorF("                       Use 'auto' to detect from TIOCGWINSZ\n");
    ErrorF("\n");
}

/* . ݁₊ ⊹ . ݁˖ . ݁ OsVendorInit . ݁₊ ⊹ . ݁˖ . ݁ */

void
OsVendorInit(void)
{
    if (serverGeneration == 1) {
        /* Auto-detect cell aspect ratio from terminal */
        float r = caca_host_query_cell_aspect();
        caca_host_set_cell_aspect(r);

        if (!KdCardInfoLast()) {
            processScreenArg("640x480");
        }

        if (!caca_host_init())
            FatalError("Xcaca: caca_host_init() failed\n");
    }
}
