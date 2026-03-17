/*
 * Xcaca - Keyboard and mouse input drivers.
 *
 * libcaca provides key characters and mouse cell coordinates, not hardware
 * scancodes.  We synthesise evdev scancodes (with +8 offset per X11
 * convention) from the caca key values.
 *
 * Copyright © 2025 Robin
 */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include "xcaca.h"

#include <stdlib.h>
#include <string.h>

/* . ݁₊ ⊹ . ݁˖ . ݁ Evdev scancode helpers . ݁₊ ⊹ . ݁˖ . ݁ */

/* Evdev scancodes are Linux input keycodes + 8 (the X11 offset). */
#define EV(kc) ((kc) + 8)

/* Linux input keycodes for common keys (from linux/input-event-codes.h) */
#define KEY_ESC        1
#define KEY_1          2
#define KEY_2          3
#define KEY_3          4
#define KEY_4          5
#define KEY_5          6
#define KEY_6          7
#define KEY_7          8
#define KEY_8          9
#define KEY_9          10
#define KEY_0          11
#define KEY_MINUS      12
#define KEY_EQUAL      13
#define KEY_BACKSPACE  14
#define KEY_TAB        15
#define KEY_Q          16
#define KEY_W          17
#define KEY_E          18
#define KEY_R          19
#define KEY_T          20
#define KEY_Y          21
#define KEY_U          22
#define KEY_I          23
#define KEY_O          24
#define KEY_P          25
#define KEY_LEFTBRACE  26
#define KEY_RIGHTBRACE 27
#define KEY_ENTER      28
#define KEY_LEFTCTRL   29
#define KEY_A          30
#define KEY_S          31
#define KEY_D          32
#define KEY_F          33
#define KEY_G          34
#define KEY_H          35
#define KEY_J          36
#define KEY_K          37
#define KEY_L          38
#define KEY_SEMICOLON  39
#define KEY_APOSTROPHE 40
#define KEY_GRAVE      41
#define KEY_LEFTSHIFT  42
#define KEY_BACKSLASH  43
#define KEY_Z          44
#define KEY_X          45
#define KEY_C          46
#define KEY_V          47
#define KEY_B          48
#define KEY_N          49
#define KEY_M          50
#define KEY_COMMA      51
#define KEY_DOT        52
#define KEY_SLASH      53
#define KEY_RIGHTSHIFT 54
#define KEY_KPASTERISK 55
#define KEY_LEFTALT    56
#define KEY_SPACE      57
#define KEY_CAPSLOCK   58
#define KEY_F1         59
#define KEY_F2         60
#define KEY_F3         61
#define KEY_F4         62
#define KEY_F5         63
#define KEY_F6         64
#define KEY_F7         65
#define KEY_F8         66
#define KEY_F9         67
#define KEY_F10        68
#define KEY_F11        87
#define KEY_F12        88
#define KEY_F13        183
#define KEY_F14        184
#define KEY_F15        185
#define KEY_UP         103
#define KEY_LEFT       105
#define KEY_RIGHT      106
#define KEY_DOWN       108
#define KEY_INSERT     110
#define KEY_DELETE     111
#define KEY_HOME       102
#define KEY_END        107
#define KEY_PAGEUP     104
#define KEY_PAGEDOWN   109

/*
 * Map caca special keys → evdev scancode (+8 offset already applied).
 * Terminal order: these are the CACA_KEY_* constants.
 */
typedef struct {
    int caca_key;
    int evdev_scan; /* already has +8 */
} SpecialKeyEntry;

static const SpecialKeyEntry special_key_map[] = {
    { CACA_KEY_ESCAPE,    EV(KEY_ESC)       },
    { CACA_KEY_RETURN,    EV(KEY_ENTER)     },
    { CACA_KEY_BACKSPACE, EV(KEY_BACKSPACE) },
    { CACA_KEY_TAB,       EV(KEY_TAB)       },
    { CACA_KEY_DELETE,    EV(KEY_DELETE)    },
    { CACA_KEY_UP,        EV(KEY_UP)        },
    { CACA_KEY_DOWN,      EV(KEY_DOWN)      },
    { CACA_KEY_LEFT,      EV(KEY_LEFT)      },
    { CACA_KEY_RIGHT,     EV(KEY_RIGHT)     },
    { CACA_KEY_INSERT,    EV(KEY_INSERT)    },
    { CACA_KEY_HOME,      EV(KEY_HOME)      },
    { CACA_KEY_END,       EV(KEY_END)       },
    { CACA_KEY_PAGEUP,    EV(KEY_PAGEUP)    },
    { CACA_KEY_PAGEDOWN,  EV(KEY_PAGEDOWN)  },
    { CACA_KEY_F1,        EV(KEY_F1)        },
    { CACA_KEY_F2,        EV(KEY_F2)        },
    { CACA_KEY_F3,        EV(KEY_F3)        },
    { CACA_KEY_F4,        EV(KEY_F4)        },
    { CACA_KEY_F5,        EV(KEY_F5)        },
    { CACA_KEY_F6,        EV(KEY_F6)        },
    { CACA_KEY_F7,        EV(KEY_F7)        },
    { CACA_KEY_F8,        EV(KEY_F8)        },
    { CACA_KEY_F9,        EV(KEY_F9)        },
    { CACA_KEY_F10,       EV(KEY_F10)       },
    { CACA_KEY_F11,       EV(KEY_F11)       },
    { CACA_KEY_F12,       EV(KEY_F12)       },
    { CACA_KEY_F13,       EV(KEY_F13)       },
    { CACA_KEY_F14,       EV(KEY_F14)       },
    { CACA_KEY_F15,       EV(KEY_F15)       },
    { 0, 0 }
};

/*
 * ASCII → evdev scancode (US layout, unshifted).
 * Index by ASCII code.  0 = no mapping.
 */
static const int ascii_to_scancode[128] = {
    /* 0x00 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x08 */ EV(KEY_BACKSPACE), EV(KEY_TAB), EV(KEY_ENTER), 0, 0, EV(KEY_ENTER), 0, 0,
    /* 0x10 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x18 */ 0, 0, 0, EV(KEY_ESC), 0, 0, 0, 0,
    /* 0x20 */ EV(KEY_SPACE),
    /* '!' */ EV(KEY_1),
    /* '"' */ EV(KEY_APOSTROPHE),
    /* '#' */ EV(KEY_3),
    /* '$' */ EV(KEY_4),
    /* '%' */ EV(KEY_5),
    /* '&' */ EV(KEY_7),
    /* '\'' */ EV(KEY_APOSTROPHE),
    /* '(' */ EV(KEY_9),
    /* ')' */ EV(KEY_0),
    /* '*' */ EV(KEY_8),
    /* '+' */ EV(KEY_EQUAL),
    /* ',' */ EV(KEY_COMMA),
    /* '-' */ EV(KEY_MINUS),
    /* '.' */ EV(KEY_DOT),
    /* '/' */ EV(KEY_SLASH),
    /* '0' */ EV(KEY_0),
    /* '1' */ EV(KEY_1),
    /* '2' */ EV(KEY_2),
    /* '3' */ EV(KEY_3),
    /* '4' */ EV(KEY_4),
    /* '5' */ EV(KEY_5),
    /* '6' */ EV(KEY_6),
    /* '7' */ EV(KEY_7),
    /* '8' */ EV(KEY_8),
    /* '9' */ EV(KEY_9),
    /* ':' */ EV(KEY_SEMICOLON),
    /* ';' */ EV(KEY_SEMICOLON),
    /* '<' */ EV(KEY_COMMA),
    /* '=' */ EV(KEY_EQUAL),
    /* '>' */ EV(KEY_DOT),
    /* '?' */ EV(KEY_SLASH),
    /* '@' */ EV(KEY_2),
    /* 'A' */ EV(KEY_A),
    /* 'B' */ EV(KEY_B),
    /* 'C' */ EV(KEY_C),
    /* 'D' */ EV(KEY_D),
    /* 'E' */ EV(KEY_E),
    /* 'F' */ EV(KEY_F),
    /* 'G' */ EV(KEY_G),
    /* 'H' */ EV(KEY_H),
    /* 'I' */ EV(KEY_I),
    /* 'J' */ EV(KEY_J),
    /* 'K' */ EV(KEY_K),
    /* 'L' */ EV(KEY_L),
    /* 'M' */ EV(KEY_M),
    /* 'N' */ EV(KEY_N),
    /* 'O' */ EV(KEY_O),
    /* 'P' */ EV(KEY_P),
    /* 'Q' */ EV(KEY_Q),
    /* 'R' */ EV(KEY_R),
    /* 'S' */ EV(KEY_S),
    /* 'T' */ EV(KEY_T),
    /* 'U' */ EV(KEY_U),
    /* 'V' */ EV(KEY_V),
    /* 'W' */ EV(KEY_W),
    /* 'X' */ EV(KEY_X),
    /* 'Y' */ EV(KEY_Y),
    /* 'Z' */ EV(KEY_Z),
    /* '[' */ EV(KEY_LEFTBRACE),
    /* '\\'*/ EV(KEY_BACKSLASH),
    /* ']' */ EV(KEY_RIGHTBRACE),
    /* '^' */ EV(KEY_6),
    /* '_' */ EV(KEY_MINUS),
    /* '`' */ EV(KEY_GRAVE),
    /* 'a' */ EV(KEY_A),
    /* 'b' */ EV(KEY_B),
    /* 'c' */ EV(KEY_C),
    /* 'd' */ EV(KEY_D),
    /* 'e' */ EV(KEY_E),
    /* 'f' */ EV(KEY_F),
    /* 'g' */ EV(KEY_G),
    /* 'h' */ EV(KEY_H),
    /* 'i' */ EV(KEY_I),
    /* 'j' */ EV(KEY_J),
    /* 'k' */ EV(KEY_K),
    /* 'l' */ EV(KEY_L),
    /* 'm' */ EV(KEY_M),
    /* 'n' */ EV(KEY_N),
    /* 'o' */ EV(KEY_O),
    /* 'p' */ EV(KEY_P),
    /* 'q' */ EV(KEY_Q),
    /* 'r' */ EV(KEY_R),
    /* 's' */ EV(KEY_S),
    /* 't' */ EV(KEY_T),
    /* 'u' */ EV(KEY_U),
    /* 'v' */ EV(KEY_V),
    /* 'w' */ EV(KEY_W),
    /* 'x' */ EV(KEY_X),
    /* 'y' */ EV(KEY_Y),
    /* 'z' */ EV(KEY_Z),
    /* '{' */ EV(KEY_LEFTBRACE),
    /* '|' */ EV(KEY_BACKSLASH),
    /* '}' */ EV(KEY_RIGHTBRACE),
    /* '~' */ EV(KEY_GRAVE),
    /* DEL */ EV(KEY_DELETE),
};

/* Returns TRUE if the ASCII character requires shift on a US keyboard. */
static Bool
ascii_needs_shift(int ch)
{
    static const char shifted[] = "~!@#$%^&*()_+{}|:\"<>?ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    return (ch > 0 && ch < 128 && strchr(shifted, (char)ch) != NULL);
}

/*
 * Translate a caca key value to an evdev scancode (with +8 offset).
 * Returns 0 if not mappable.
 * If needs_shift is non-NULL it is set for characters requiring Shift.
 */
int
caca_key_to_scancode(int key, Bool *needs_shift)
{
    int i;

    if (needs_shift)
        *needs_shift = FALSE;

    /* Handle CACA_KEY_CTRL_A..Z (values 1..26) */
    if (key >= CACA_KEY_CTRL_A && key <= CACA_KEY_CTRL_Z) {
        int letter = key - CACA_KEY_CTRL_A; /* 0=A, 25=Z */
        /* ctrl modifier is synthesised by cacainit.c caller */
        return EV(KEY_A + letter);
    }

    /* Special keys */
    for (i = 0; special_key_map[i].caca_key != 0; i++) {
        if (special_key_map[i].caca_key == key)
            return special_key_map[i].evdev_scan;
    }

    /* ASCII printable */
    if (key >= 0 && key < 128) {
        if (needs_shift)
            *needs_shift = ascii_needs_shift(key);
        return ascii_to_scancode[key];
    }

    return 0;
}

/* . ݁₊ ⊹ . ݁˖ . ݁ Mouse coordinate / button helpers (called from caca.c) . ݁₊ ⊹ . ݁˖ . ݁ */

void
caca_mouse_to_pixels(int cx, int cy)
{
    int dst_x, dst_y, dst_w, dst_h;
    int px, py;

    if (!cacaMouse)
        return;

    caca_host_get_dst_rect(&dst_x, &dst_y, &dst_w, &dst_h);
    if (dst_w <= 0 || dst_h <= 0)
        return;

    if (screenInfo.numScreens < 1)
        return;

    ScreenPtr pScreen = screenInfo.screens[0];

    /* Map cell coordinates relative to the dither destination rect */
    px = ((cx - dst_x) * pScreen->width)  / dst_w;
    py = ((cy - dst_y) * pScreen->height) / dst_h;

    /* Clamp to screen bounds */
    if (px < 0) px = 0;
    if (py < 0) py = 0;
    if (px >= pScreen->width)  px = pScreen->width  - 1;
    if (py >= pScreen->height) py = pScreen->height - 1;

    KdEnqueuePointerEvent(cacaMouse, KD_POINTER_DESKTOP, px, py, 0);
}

void
caca_enqueue_button(int caca_btn, Bool release)
{
    static int button_state = 0;
    int kd_btn;

    switch (caca_btn) {
    case 1: kd_btn = KD_BUTTON_1; break;
    case 2: kd_btn = KD_BUTTON_2; break;
    case 3: kd_btn = KD_BUTTON_3; break;
    default: return;
    }

    if (release)
        button_state &= ~kd_btn;
    else
        button_state |= kd_btn;

    if (cacaMouse)
        KdEnqueuePointerEvent(cacaMouse, button_state | KD_MOUSE_DELTA, 0, 0, 0);
}

/* . ݁₊ ⊹ . ݁˖ . ݁ Keyboard driver . ݁₊ ⊹ . ݁˖ . ݁ */

static Bool
CacaKeyboardInit(KdKeyboardInfo *ki)
{
    ki->minScanCode = 8;
    ki->maxScanCode = 255;

    free(ki->xkbRules);  ki->xkbRules  = strdup("evdev");
    free(ki->xkbModel);  ki->xkbModel  = strdup("pc105");
    free(ki->xkbLayout); ki->xkbLayout = strdup("us");

    free(ki->name);
    ki->name = strdup("Xcaca virtual keyboard");

    cacaKbd = ki;
    return TRUE;
}

static Bool
CacaKeyboardEnable(KdKeyboardInfo *ki)
{
    return TRUE;
}

static void
CacaKeyboardLeds(KdKeyboardInfo *ki, int leds)
{
}

static void
CacaKeyboardBell(KdKeyboardInfo *ki, int volume, int frequency, int duration)
{
}

static void
CacaKeyboardDisable(KdKeyboardInfo *ki)
{
}

static void
CacaKeyboardFini(KdKeyboardInfo *ki)
{
    cacaKbd = NULL;
}

KdKeyboardDriver CacaKeyboardDriver = {
    "caca",
    CacaKeyboardInit,
    CacaKeyboardEnable,
    CacaKeyboardLeds,
    CacaKeyboardBell,
    CacaKeyboardDisable,
    CacaKeyboardFini,
    NULL,
};

/* . ݁₊ ⊹ . ݁˖ . ݁ Mouse driver . ݁₊ ⊹ . ݁˖ . ݁ */

static Status
CacaMouseInit(KdPointerInfo *pi)
{
    pi->nAxes   = 2;
    pi->nButtons = 5;
    pi->transformCoordinates = FALSE;

    free(pi->name);
    pi->name = strdup("Xcaca virtual mouse");

    cacaMouse = pi;
    return Success;
}

static Status
CacaMouseEnable(KdPointerInfo *pi)
{
    return Success;
}

static void
CacaMouseDisable(KdPointerInfo *pi)
{
}

static void
CacaMouseFini(KdPointerInfo *pi)
{
    cacaMouse = NULL;
}

KdPointerDriver CacaMouseDriver = {
    "caca",
    CacaMouseInit,
    CacaMouseEnable,
    CacaMouseDisable,
    CacaMouseFini,
    NULL,
};
