/*
 * Xcaca - libcaca display abstraction (analogous to Xephyr's hostx.c).
 *
 * Manages a single caca canvas+display+dither.  All state is file-static;
 * Xcaca is single-screen.
 */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include "xcaca.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>

/* ------------------------------------------------------------------ */
/* File-static state                                                   */
/* ------------------------------------------------------------------ */

static caca_canvas_t  *s_canvas;
static caca_display_t *s_display;
static caca_dither_t  *s_dither;

/* pixel dimensions of the framebuffer */
static int s_width;
static int s_height;

/* dither options (stored so we can re-apply after screen init) */
static const char *s_dither_algorithm = "fstein";
static const char *s_charset          = "ascii";
static float       s_brightness       = 1.0f;
static float       s_gamma            = 1.0f;
static float       s_contrast         = 1.0f;

/* cell aspect ratio: cell_width / cell_height.
 * 0.5 is a safe default (typical terminals have cells ~8×16 px). */
static float s_cell_aspect = 0.5f;

/* Destination rectangle for dither_bitmap (aspect-ratio corrected). */
static int s_dst_x, s_dst_y, s_dst_w, s_dst_h;

/* Saved terminal state, captured before caca takes over. */
static struct termios s_saved_termios;
static Bool s_termios_saved = FALSE;

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

/*
 * Recompute the letterbox/pillarbox destination rect so that the
 * framebuffer pixels map 1:1 in apparent aspect ratio.
 *
 * Terminal canvas: cols × rows cells.
 * Each cell occupies cell_aspect (= w/h) in normalised units.
 * Effective canvas pixel extent: cols × (rows / cell_aspect) virtual px.
 *
 * We scale the framebuffer to fit inside that extent, preserving
 * the pixel aspect ratio, then centre the result.
 */
static void
recompute_dst_rect(void)
{
    if (!s_canvas)
        return;

    int cols = caca_get_canvas_width(s_canvas);
    int rows = caca_get_canvas_height(s_canvas);

    if (cols <= 0 || rows <= 0 || s_width <= 0 || s_height <= 0 ||
        s_cell_aspect <= 0.0f)
        return;

    /* Effective canvas dimensions in "virtual pixels" where each cell
     * counts as cell_aspect wide and 1 tall. */
    float canvas_vw = (float)cols;
    float canvas_vh = (float)rows / s_cell_aspect;

    /* Scale framebuffer to fit, preserving pixel AR. */
    float scale_x = canvas_vw / (float)s_width;
    float scale_y = canvas_vh / (float)s_height;
    float scale   = (scale_x < scale_y) ? scale_x : scale_y;

    int dst_w_cells = (int)(s_width  * scale + 0.5f);
    int dst_h_cells = (int)(s_height * scale * s_cell_aspect + 0.5f);

    /* clamp */
    if (dst_w_cells > cols) dst_w_cells = cols;
    if (dst_h_cells > rows) dst_h_cells = rows;

    s_dst_x = (cols - dst_w_cells) / 2;
    s_dst_y = (rows - dst_h_cells) / 2;
    s_dst_w = dst_w_cells;
    s_dst_h = dst_h_cells;
}

/* Apply all stored dither settings to s_dither. */
static void
apply_dither_settings(void)
{
    if (!s_dither)
        return;
    caca_set_dither_algorithm(s_dither, s_dither_algorithm);
    caca_set_dither_charset(s_dither, s_charset);
    caca_set_dither_brightness(s_dither, s_brightness);
    caca_set_dither_gamma(s_dither, s_gamma);
    caca_set_dither_contrast(s_dither, s_contrast);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

Bool
caca_host_init(void)
{
    /* Save terminal state before caca switches to raw/alternate screen */
    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &s_saved_termios) == 0)
        s_termios_saved = TRUE;

    s_canvas = caca_create_canvas(0, 0);
    if (!s_canvas) {
        ErrorF("Xcaca: caca_create_canvas failed\n");
        return FALSE;
    }

    s_display = caca_create_display(s_canvas);
    if (!s_display) {
        ErrorF("Xcaca: caca_create_display failed\n");
        caca_free_canvas(s_canvas);
        s_canvas = NULL;
        return FALSE;
    }

    ErrorF("Xcaca: using caca driver '%s'\n", caca_get_display_driver(s_display));
    caca_set_display_title(s_display, "Xcaca");
    caca_set_display_time(s_display, 0);
    caca_set_mouse(s_display, 1);

    /* Explicitly enable xterm any-event mouse tracking.
     * ncurses REPORT_MOUSE_POSITION may not work on all terminals;
     * this escape sequence is widely supported (xterm, Konsole, etc.). */
    fprintf(stdout, "\033[?1003h");
    fflush(stdout);

    /* Ensure terminal is restored on any exit path.
     * Do NOT override SIGINT/SIGTERM — the X server installs GiveUp()
     * for those, which triggers a graceful shutdown.  atexit is enough
     * to guarantee caca_host_fini runs on every exit path. */
    atexit(caca_host_fini);

    return TRUE;
}

Bool
caca_host_screen_init(int width, int height)
{
    s_width  = width;
    s_height = height;

    if (s_dither) {
        caca_free_dither(s_dither);
        s_dither = NULL;
    }

    /*
     * 32bpp ARGB framebuffer layout:
     *   bpp=32, depth=24, R=0xFF0000, G=0x00FF00, B=0x0000FF, A=0xFF000000
     */
    s_dither = caca_create_dither(
        32,             /* bits per pixel */
        width,          /* source width   */
        height,         /* source height  */
        width * 4,      /* bytes per line */
        0x00FF0000,     /* red mask       */
        0x0000FF00,     /* green mask     */
        0x000000FF,     /* blue mask      */
        0x00000000      /* alpha mask — ignore alpha; X11 depth-24 leaves it 0 */
    );

    if (!s_dither) {
        ErrorF("Xcaca: caca_create_dither failed\n");
        return FALSE;
    }

    apply_dither_settings();
    recompute_dst_rect();

    ErrorF("Xcaca: screen_init fb=%dx%d canvas=%dx%d dst=(%d,%d %dx%d) aspect=%.3f\n",
           width, height,
           s_canvas ? caca_get_canvas_width(s_canvas) : 0,
           s_canvas ? caca_get_canvas_height(s_canvas) : 0,
           s_dst_x, s_dst_y, s_dst_w, s_dst_h, s_cell_aspect);

    return TRUE;
}

void
caca_host_paint(void *framebuffer)
{
    int x, y, w, h;

    if (!s_canvas || !s_display || !s_dither || !framebuffer)
        return;

    /* If dst rect hasn't been computed yet (e.g. canvas was 0×0 at init
     * time), recompute now — the display may have resized the canvas. */
    if (s_dst_w <= 0 || s_dst_h <= 0)
        recompute_dst_rect();

    /* Fallback: use full canvas if rect is still empty */
    if (s_dst_w > 0 && s_dst_h > 0) {
        x = s_dst_x; y = s_dst_y;
        w = s_dst_w; h = s_dst_h;
    } else {
        x = 0; y = 0;
        w = caca_get_canvas_width(s_canvas);
        h = caca_get_canvas_height(s_canvas);
        if (w <= 0 || h <= 0)
            return;
    }

    caca_clear_canvas(s_canvas);
    caca_dither_bitmap(s_canvas, x, y, w, h, s_dither, framebuffer);
    caca_refresh_display(s_display);
}

/* Returns 1 if an event was available, 0 otherwise (non-blocking). */
int
caca_host_poll_event(caca_event_t *ev)
{
    if (!s_display)
        return 0;
    return caca_get_event(s_display, CACA_EVENT_ANY, ev, 0);
}

void
caca_host_get_canvas_size(int *cols, int *rows)
{
    if (s_canvas) {
        *cols = caca_get_canvas_width(s_canvas);
        *rows = caca_get_canvas_height(s_canvas);
    } else {
        *cols = 80;
        *rows = 24;
    }
}

void
caca_host_set_cell_aspect(float r)
{
    if (r > 0.0f)
        s_cell_aspect = r;
    recompute_dst_rect();
}

float
caca_host_query_cell_aspect(void)
{
#ifdef TIOCGWINSZ
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 &&
        ws.ws_col > 0 && ws.ws_row > 0 &&
        ws.ws_xpixel > 0 && ws.ws_ypixel > 0) {
        float cell_w = (float)ws.ws_xpixel / (float)ws.ws_col;
        float cell_h = (float)ws.ws_ypixel / (float)ws.ws_row;
        float r = cell_w / cell_h;
        ErrorF("Xcaca: detected cell aspect ratio %.3f "
               "(%dx%d px, %dx%d cells)\n",
               r, ws.ws_xpixel, ws.ws_ypixel, ws.ws_col, ws.ws_row);
        return r;
    }
#endif
    ErrorF("Xcaca: TIOCGWINSZ unavailable, using default cell aspect 0.5\n");
    return 0.5f;
}

void
caca_host_fini(void)
{
    if (s_dither) {
        caca_free_dither(s_dither);
        s_dither = NULL;
    }
    if (s_display) {
        caca_free_display(s_display);
        s_display = NULL;
    }
    if (s_canvas) {
        caca_free_canvas(s_canvas);
        s_canvas = NULL;
    }

    /* Write directly to the terminal fd (not stdio, which may be redirected).
     * Leave alternate screen, show cursor, reset attributes, then newline. */
    {
        int tty = -1;
        if (isatty(STDOUT_FILENO))
            tty = STDOUT_FILENO;
        else if (isatty(STDERR_FILENO))
            tty = STDERR_FILENO;
        else
            tty = open("/dev/tty", O_WRONLY);

        if (tty >= 0) {
            static const char reset_seq[] = "\033[?1003l\033[?1049l\033[?25h\033[0m\n";
            (void)!write(tty, reset_seq, sizeof(reset_seq) - 1);
            if (tty != STDOUT_FILENO && tty != STDERR_FILENO)
                close(tty);
        }
    }

    /* Restore original terminal settings (cooked mode, echo, etc.) */
    if (s_termios_saved) {
        int tty = -1;
        if (isatty(STDIN_FILENO))
            tty = STDIN_FILENO;
        else if (isatty(STDOUT_FILENO))
            tty = STDOUT_FILENO;
        if (tty >= 0)
            tcsetattr(tty, TCSANOW, &s_saved_termios);
    }
}

/* ------------------------------------------------------------------ */
/* Dither config setters                                               */
/* ------------------------------------------------------------------ */

void caca_host_set_dither_algorithm(const char *alg)
{
    s_dither_algorithm = alg;
    if (s_dither) caca_set_dither_algorithm(s_dither, alg);
}

void caca_host_set_charset(const char *charset)
{
    s_charset = charset;
    if (s_dither) caca_set_dither_charset(s_dither, charset);
}

void caca_host_set_brightness(float b)
{
    s_brightness = b;
    if (s_dither) caca_set_dither_brightness(s_dither, b);
}

void caca_host_set_gamma(float g)
{
    s_gamma = g;
    if (s_dither) caca_set_dither_gamma(s_dither, g);
}

void caca_host_set_contrast(float c)
{
    s_contrast = c;
    if (s_dither) caca_set_dither_contrast(s_dither, c);
}
