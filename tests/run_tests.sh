#!/usr/bin/env bash
# . ݁₊ ⊹ . ݁˖ . ݁ ✨ . ݁₊ ⊹ . ݁˖ . ݁
# Xcaca Comprehensive Test Suite
# . ݁₊ ⊹ . ݁˖ . ݁ ✨ . ݁₊ ⊹ . ݁˖ . ݁
#
# Tests all output drivers, CLI options, X clients, and screen sizes.
#
# For each test, one or more of the following are captured:
#   - Framebuffer PNG:  what X clients rendered  (xwd on Xcaca's display)
#   - Terminal PNG:     what libcaca rendered as ASCII art
#                       (xwd on a headless Xvfb display showing the xterm)
#   - ASCII text:       tmux capture-pane of the libcaca terminal output
#
# Usage:
#   bash tests/run_tests.sh [--filter PATTERN]
#
# Requirements (hard):  ImageMagick (convert/identify), xwd, xdpyinfo,
#                       xeyes, xclock, xlogo, xterm
# Requirements (soft):  Xvfb  — terminal PNG screenshots (skipped without it)
#                       tmux  — ASCII text capture      (skipped without it)
#                       xdotool — resize test           (skipped without it)
#
# Output: tests/output/*.png  (screenshots)
#         tests/output/*.txt  (ASCII art text captures)
#         tests/output/*.log  (Xcaca server logs)
# . ݁₊ ⊹ . ݁˖ . ݁ ✨ . ݁₊ ⊹ . ݁˖ . ݁

set -o pipefail

# . ݁₊ ⊹ . ݁˖ . ݁
# Paths
# . ݁₊ ⊹ . ݁˖ . ݁
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
XCACA_BIN="$REPO_ROOT/xorg-server/builddir/hw/kdrive/caca/Xcaca"
OUTPUT_DIR="$SCRIPT_DIR/output"
mkdir -p "$OUTPUT_DIR"

# . ݁₊ ⊹ . ݁˖ . ݁
# Configuration
# . ݁₊ ⊹ . ݁˖ . ݁
XCACA_D=9          # display number for Xcaca
XVFB_D=10          # display number for the headless Xvfb terminal
STARTUP_TIMEOUT=20 # seconds to wait for Xcaca to become ready
RENDER_WAIT=2      # seconds to let an X client render before screenshotting

# . ݁₊ ⊹ . ݁˖ . ݁
# Counters & colour output
# . ݁₊ ⊹ . ݁˖ . ݁
PASS=0; FAIL=0; SKIP=0
declare -a FAILURES=()

R='\033[0;31m'; G='\033[0;32m'; Y='\033[1;33m'; B='\033[0;34m'; N='\033[0m'

pass()   { echo -e "${G}[PASS]${N} $1";         ((PASS++))  || true; }
_fail()  { echo -e "${R}[FAIL]${N} $1: $2";     ((FAIL++))  || true; FAILURES+=("$1: $2"); }
_skip()  { echo -e "${Y}[SKIP]${N} $1: $2";     ((SKIP++))  || true; }
info()   { echo -e "       ${B}>${N} $1"; }
header() { echo -e "\n${B}--- $1 ---${N}"; }

# . ݁₊ ⊹ . ݁˖ . ݁
# Helpers
# . ݁₊ ⊹ . ݁˖ . ݁
have() { command -v "$1" >/dev/null 2>&1; }

# Return 77 (skip code) if any listed command is missing.
need() {
    for cmd in "$@"; do
        have "$cmd" || return 77
    done
}

# . ݁₊ ⊹ . ݁˖ . ݁
# Xcaca process management
# . ݁₊ ⊹ . ݁˖ . ݁
_XCACA_PID=""

xcaca_cleanup() {
    pkill -9 -x Xcaca 2>/dev/null || true
    local deadline=$(( $(date +%s) + 5 ))
    while pgrep -x Xcaca >/dev/null 2>&1; do
        [ "$(date +%s)" -ge "$deadline" ] && break
        sleep 0.1
    done
    rm -f "/tmp/.X${XCACA_D}-lock" "/tmp/.X11-unix/X${XCACA_D}" 2>/dev/null || true
}

# Start Xcaca with given args and wait until xdpyinfo succeeds.
# Returns 0 on success, 1 on timeout.
xcaca_start() {
    local logfile="$OUTPUT_DIR/xcaca_last.log"
    "$XCACA_BIN" ":$XCACA_D" "$@" >"$logfile" 2>&1 &
    _XCACA_PID=$!

    local deadline=$(( $(date +%s) + STARTUP_TIMEOUT ))
    while [ "$(date +%s)" -lt "$deadline" ]; do
        DISPLAY=":$XCACA_D" xdpyinfo >/dev/null 2>&1 && return 0
        sleep 0.2
    done
    return 1
}

xcaca_stop() {
    xcaca_cleanup
    _XCACA_PID=""
}

# . ݁₊ ⊹ . ݁˖ . ݁
# Xvfb terminal management (for ASCII art visual screenshots)
#
# Architecture:
#   Xvfb :XVFB_D  (headless 1920x1080 virtual display)
#     └── xterm running "Xcaca :XCACA_D [args]"  (shows ASCII art in terminal)
#
# Screenshots of :XVFB_D capture the xterm window showing libcaca's output.
# . ݁₊ ⊹ . ݁˖ . ݁
_XVFB_PID=""
_XTERM_PID=""

xvfb_start() {
    # Kill any leftover Xvfb on our display
    pkill -f "Xvfb :${XVFB_D} " 2>/dev/null || true
    rm -f "/tmp/.X${XVFB_D}-lock" "/tmp/.X11-unix/X${XVFB_D}" 2>/dev/null || true
    sleep 0.2

    Xvfb ":$XVFB_D" -screen 0 1920x1080x24 >/dev/null 2>&1 &
    _XVFB_PID=$!
    sleep 0.5  # let Xvfb initialise
}

# Open an xterm on the Xvfb display, running Xcaca inside it.
# "$@" = extra Xcaca args (e.g. -screen 640x480 -charset blocks)
xvfb_launch_xcaca() {
    DISPLAY=":$XVFB_D" xterm \
        -geometry 200x50+0+0 \
        -title "xcaca_test_terminal" \
        -fa "Monospace" -fs 10 \
        -e "$XCACA_BIN" ":$XCACA_D" "$@" &
    _XTERM_PID=$!
}

xvfb_stop() {
    kill "$_XTERM_PID" 2>/dev/null || true
    kill "$_XVFB_PID"  2>/dev/null || true
    wait "$_XTERM_PID" 2>/dev/null || true
    wait "$_XVFB_PID"  2>/dev/null || true
    rm -f "/tmp/.X${XVFB_D}-lock" "/tmp/.X11-unix/X${XVFB_D}" 2>/dev/null || true
    _XVFB_PID=""; _XTERM_PID=""
}

# Wait for Xcaca started inside Xvfb to become ready.
xvfb_wait_xcaca() {
    local deadline=$(( $(date +%s) + STARTUP_TIMEOUT ))
    while [ "$(date +%s)" -lt "$deadline" ]; do
        DISPLAY=":$XCACA_D" xdpyinfo >/dev/null 2>&1 && return 0
        sleep 0.2
    done
    return 1
}

# Screenshot of the Xvfb display (captures the xterm showing ASCII art).
take_terminal_shot() {
    DISPLAY=":$XVFB_D" xwd -root -silent 2>/dev/null \
        | convert xwd:- "$1" 2>/dev/null
}

# . ݁₊ ⊹ . ݁˖ . ݁
# tmux management (for ASCII text capture of libcaca output)
# . ݁₊ ⊹ . ݁˖ . ݁
_TMUX_SESSION=""

tmux_start_xcaca() {
    _TMUX_SESSION="xcaca_test_$$"
    tmux new-session -d -s "$_TMUX_SESSION" -x 200 -y 50

    # Write launch command to a helper script to avoid tmux quoting issues
    # when XCACA_BIN or OUTPUT_DIR contain spaces (e.g. "Robin_s Card").
    local helper="/tmp/xcaca_tmux_$$.sh"
    {
        printf '#!/bin/bash\n'
        printf '%q' "$XCACA_BIN"
        printf ' %q' ":${XCACA_D}"
        for a in "$@"; do printf ' %q' "$a"; done
        printf ' 2>%q\n' "$OUTPUT_DIR/xcaca_tmux.log"
    } > "$helper"
    chmod +x "$helper"

    tmux send-keys -t "$_TMUX_SESSION" "bash $helper" Enter
}

tmux_wait_xcaca() {
    local deadline=$(( $(date +%s) + STARTUP_TIMEOUT ))
    while [ "$(date +%s)" -lt "$deadline" ]; do
        DISPLAY=":$XCACA_D" xdpyinfo >/dev/null 2>&1 && return 0
        sleep 0.2
    done
    return 1
}

# Capture the tmux pane content (plain text, no colours).
tmux_capture() {
    tmux capture-pane -t "$_TMUX_SESSION" -p > "$1" 2>/dev/null
}

tmux_stop() {
    tmux kill-session -t "$_TMUX_SESSION" 2>/dev/null || true
    _TMUX_SESSION=""
}

# . ݁₊ ⊹ . ݁˖ . ݁
# Framebuffer screenshot
# . ݁₊ ⊹ . ݁˖ . ݁
take_fb_shot() {
    DISPLAY=":$XCACA_D" xwd -root -silent 2>/dev/null \
        | convert xwd:- "$1" 2>/dev/null
}

# Start an X client in the background. PID is stored in _CLIENT_PID.
# Do NOT use $() to call this — that creates a subshell which waits for the
# background client before exiting, causing a deadlock.
_CLIENT_PID=""
run_client() {
    local client="$1"; shift
    DISPLAY=":$XCACA_D" "$client" "$@" &
    _CLIENT_PID=$!
}

# . ݁₊ ⊹ . ݁˖ . ݁
# Image analysis (ImageMagick)
# . ݁₊ ⊹ . ݁˖ . ݁
img_mean() { convert "$1" -colorspace gray -format "%[fx:mean]"               info: 2>/dev/null; }
img_std()  { convert "$1" -colorspace gray -format "%[fx:standard_deviation]" info: 2>/dev/null; }
img_dims() { identify -format "%wx%h" "$1" 2>/dev/null; }

# Assert an image file has non-trivial content (not blank/black/solid).
# Returns 0 on success, 1 on failure.
assert_image_content() {
    local f="$1" label="${2:-image}"
    if [ ! -f "$f" ]; then
        info "MISSING: $f"
        return 1
    fi
    local mean std
    mean=$(img_mean "$f")
    std=$(img_std  "$f")
    local ok_m ok_s
    ok_m=$(echo "$mean > 0.02" | bc -l 2>/dev/null || echo 0)
    ok_s=$(echo "$std  > 0.02" | bc -l 2>/dev/null || echo 0)
    if [ "$ok_m" = "1" ] && [ "$ok_s" = "1" ]; then
        info "$label: mean=${mean} std=${std} ✓"
        return 0
    else
        info "$label: BLANK — mean=${mean} std=${std}"
        return 1
    fi
}

# Assert an image's mean brightness is above (gt) or below (lt) a threshold.
assert_image_mean() {
    local f="$1" op="$2" threshold="$3" label="${4:-image}"
    [ -f "$f" ] || { info "MISSING: $f"; return 1; }
    local mean result
    mean=$(img_mean "$f")
    result=$(echo "$mean $op $threshold" | bc -l 2>/dev/null || echo 0)
    if [ "$result" = "1" ]; then
        info "$label: mean=$mean $op $threshold ✓"
        return 0
    else
        info "$label: mean=$mean — expected $op $threshold"
        return 1
    fi
}

# Assert an image's standard deviation is above a threshold (has variation).
assert_image_std() {
    local f="$1" op="$2" threshold="$3" label="${4:-image}"
    [ -f "$f" ] || { info "MISSING: $f"; return 1; }
    local std result
    std=$(img_std "$f")
    result=$(echo "$std $op $threshold" | bc -l 2>/dev/null || echo 0)
    if [ "$result" = "1" ]; then
        info "$label: std=$std $op $threshold ✓"
        return 0
    else
        info "$label: std=$std — expected $op $threshold"
        return 1
    fi
}

# Assert image dimensions match WxH string.
assert_image_dims() {
    local f="$1" expected="$2" label="${3:-image}"
    [ -f "$f" ] || { info "MISSING: $f"; return 1; }
    local actual
    actual=$(img_dims "$f")
    if [ "$actual" = "$expected" ]; then
        info "$label: dimensions=${actual} ✓"
        return 0
    else
        info "$label: dimensions=${actual} — expected ${expected}"
        return 1
    fi
}

# Assert a text file has at least MIN non-whitespace characters.
assert_ascii_content() {
    local f="$1" min="${2:-200}"
    [ -f "$f" ] || { info "MISSING: $f"; return 1; }
    local count
    count=$(tr -cd '[:graph:]' < "$f" | wc -c)
    if [ "$count" -ge "$min" ]; then
        info "ASCII: $count non-whitespace chars ✓"
        return 0
    else
        info "ASCII: only $count non-whitespace chars (need $min)"
        return 1
    fi
}

# . ݁₊ ⊹ . ݁˖ . ݁
# Standard framebuffer-test body
#
# Starts Xcaca with given args, runs CLIENT for RENDER_WAIT seconds,
# takes a framebuffer screenshot, then validates it has content.
# Output file: $OUTPUT_DIR/${LABEL}_fb.png
# . ݁₊ ⊹ . ݁˖ . ݁
run_fb_test() {
    local label="$1"; shift
    local client="$1"; shift
    # remaining "$@" = xcaca args
    local fb="$OUTPUT_DIR/${label}_fb.png"

    xcaca_cleanup
    if ! xcaca_start "$@"; then
        info "Xcaca failed to start (see $OUTPUT_DIR/xcaca_last.log)"
        xcaca_stop
        return 1
    fi

    run_client "$client"
    sleep "$RENDER_WAIT"
    take_fb_shot "$fb"
    kill "$_CLIENT_PID" 2>/dev/null || true

    xcaca_stop

    assert_image_content "$fb" "$label/fb"
}

# ===========================================================================
# Test functions
#
# Each function:
#   - returns 0   on pass
#   - returns 1   on fail (non-zero)
#   - returns 77  to signal "skip" (runner prints [SKIP])
#
# The runner (at the bottom) calls pass/fail/_skip based on the return code.
# ===========================================================================

# . ݁₊ ⊹ . ݁˖ . ݁
# Infrastructure
# . ݁₊ ⊹ . ݁˖ . ݁

t_binary_exists() {
    [ -x "$XCACA_BIN" ] || { info "Not found: $XCACA_BIN"; return 1; }
}

t_usage_message() {
    local out
    out=$("$XCACA_BIN" -help 2>&1 || true)
    echo "$out" | grep -q "Xcaca Options" \
        || { info "'-help' output did not contain 'Xcaca Options'"; return 1; }
}

t_start_stop() {
    xcaca_cleanup
    xcaca_start -screen 640x480 || { xcaca_stop; return 1; }
    xcaca_stop
}

t_multiple_restarts() {
    local i
    for i in 1 2 3; do
        xcaca_cleanup
        xcaca_start -screen 640x480 || { xcaca_stop; return 1; }
        xcaca_stop
    done
}

# . ݁₊ ⊹ . ݁˖ . ݁
# Output drivers
# . ݁₊ ⊹ . ݁˖ . ݁

t_driver_ncurses() {
    CACA_DRIVER=ncurses run_fb_test "driver_ncurses" xeyes -screen 640x480
}

t_driver_slang() {
    CACA_DRIVER=slang run_fb_test "driver_slang" xeyes -screen 640x480
}

# . ݁₊ ⊹ . ݁˖ . ݁
# Screen sizes
# . ݁₊ ⊹ . ݁˖ . ݁

t_screen_320x240() {
    run_fb_test "screen_320x240" xeyes -screen 320x240
    # Framebuffer should be exactly 320×240
    assert_image_dims "$OUTPUT_DIR/screen_320x240_fb.png" "320x240" "screen_320x240"
}

t_screen_640x480() {
    run_fb_test "screen_640x480" xeyes -screen 640x480
    assert_image_dims "$OUTPUT_DIR/screen_640x480_fb.png" "640x480" "screen_640x480"
}

t_screen_800x600() {
    run_fb_test "screen_800x600" xeyes -screen 800x600
    assert_image_dims "$OUTPUT_DIR/screen_800x600_fb.png" "800x600" "screen_800x600"
}

# . ݁₊ ⊹ . ݁˖ . ݁
# Dither algorithms
# . ݁₊ ⊹ . ݁˖ . ݁

t_dither_none()     { run_fb_test "dither_none"     xclock -screen 640x480 -dither none;     }
t_dither_ordered2() { run_fb_test "dither_ordered2" xclock -screen 640x480 -dither ordered2; }
t_dither_ordered4() { run_fb_test "dither_ordered4" xclock -screen 640x480 -dither ordered4; }
t_dither_ordered8() { run_fb_test "dither_ordered8" xclock -screen 640x480 -dither ordered8; }
t_dither_random()   { run_fb_test "dither_random"   xclock -screen 640x480 -dither random;   }
t_dither_fstein()   { run_fb_test "dither_fstein"   xclock -screen 640x480 -dither fstein;   }

# . ݁₊ ⊹ . ݁˖ . ݁
# Character sets
# . ݁₊ ⊹ . ݁˖ . ݁

t_charset_ascii()  { run_fb_test "charset_ascii"  xlogo -screen 640x480 -charset ascii;  }
t_charset_blocks() { run_fb_test "charset_blocks" xlogo -screen 640x480 -charset blocks; }
t_charset_shades() { run_fb_test "charset_shades" xlogo -screen 640x480 -charset shades; }
t_charset_utf8()   { run_fb_test "charset_utf8"   xlogo -screen 640x480 -charset utf8;   }

# . ݁₊ ⊹ . ݁˖ . ݁
# Brightness / contrast / gamma
# . ݁₊ ⊹ . ݁˖ . ݁

t_brightness_low()  { run_fb_test "brightness_0.5" xeyes -screen 640x480 -brightness 0.5; }
t_brightness_high() { run_fb_test "brightness_1.5" xeyes -screen 640x480 -brightness 1.5; }

t_contrast_low()  { run_fb_test "contrast_0.5" xclock -screen 640x480 -contrast 0.5; }
t_contrast_high() { run_fb_test "contrast_1.5" xclock -screen 640x480 -contrast 1.5; }

t_gamma_low()  { run_fb_test "gamma_0.5" xclock -screen 640x480 -gamma 0.5; }
t_gamma_high() { run_fb_test "gamma_2.0" xclock -screen 640x480 -gamma 2.0; }

# Verify brightness actually affects the ASCII art terminal output.
# Brightness/contrast/gamma are libcaca dither settings — they affect which
# characters libcaca chooses, NOT the raw X11 pixel values.
#
# We measure this via tmux text capture: higher brightness causes libcaca to
# map more pixels to non-space characters (denser ASCII art).
t_brightness_affects_output() {
    need tmux || return 77

    xcaca_cleanup
    tmux_start_xcaca -screen 640x480 -brightness 0.2
    if ! tmux_wait_xcaca; then tmux_stop; xcaca_cleanup; return 1; fi
    DISPLAY=":$XCACA_D" xlogo &
    _CLIENT_PID=$!
    sleep "$RENDER_WAIT"
    tmux_capture "$OUTPUT_DIR/brightness_lo_ascii.txt"
    kill "$_CLIENT_PID" 2>/dev/null || true
    tmux_stop; xcaca_cleanup

    xcaca_cleanup
    tmux_start_xcaca -screen 640x480 -brightness 2.0
    if ! tmux_wait_xcaca; then tmux_stop; xcaca_cleanup; return 1; fi
    DISPLAY=":$XCACA_D" xlogo &
    _CLIENT_PID=$!
    sleep "$RENDER_WAIT"
    tmux_capture "$OUTPUT_DIR/brightness_hi_ascii.txt"
    kill "$_CLIENT_PID" 2>/dev/null || true
    tmux_stop; xcaca_cleanup

    local c_lo c_hi ok
    c_lo=$(tr -cd '[:graph:]' < "$OUTPUT_DIR/brightness_lo_ascii.txt" | wc -c)
    c_hi=$(tr -cd '[:graph:]' < "$OUTPUT_DIR/brightness_hi_ascii.txt" | wc -c)
    info "Brightness chars: low=$c_lo  high=$c_hi"
    ok=$(echo "$c_hi > $c_lo" | bc -l 2>/dev/null || echo 0)
    if [ "$ok" = "1" ]; then
        info "Brightness effect confirmed: low_chars=$c_lo  high_chars=$c_hi ✓"
        return 0
    else
        info "Brightness had no measurable effect: low_chars=$c_lo  high_chars=$c_hi"
        return 1
    fi
}

# Verify contrast actually affects the ASCII art terminal output.
# Higher contrast → more extreme light/dark mapping → more varied characters.
t_contrast_affects_output() {
    need tmux || return 77

    xcaca_cleanup
    tmux_start_xcaca -screen 640x480 -contrast 0.2
    if ! tmux_wait_xcaca; then tmux_stop; xcaca_cleanup; return 1; fi
    DISPLAY=":$XCACA_D" xlogo &
    _CLIENT_PID=$!
    sleep "$RENDER_WAIT"
    tmux_capture "$OUTPUT_DIR/contrast_lo_ascii.txt"
    kill "$_CLIENT_PID" 2>/dev/null || true
    tmux_stop; xcaca_cleanup

    xcaca_cleanup
    tmux_start_xcaca -screen 640x480 -contrast 2.0
    if ! tmux_wait_xcaca; then tmux_stop; xcaca_cleanup; return 1; fi
    DISPLAY=":$XCACA_D" xlogo &
    _CLIENT_PID=$!
    sleep "$RENDER_WAIT"
    tmux_capture "$OUTPUT_DIR/contrast_hi_ascii.txt"
    kill "$_CLIENT_PID" 2>/dev/null || true
    tmux_stop; xcaca_cleanup

    local c_lo c_hi
    c_lo=$(tr -cd '[:graph:]' < "$OUTPUT_DIR/contrast_lo_ascii.txt" | wc -c)
    c_hi=$(tr -cd '[:graph:]' < "$OUTPUT_DIR/contrast_hi_ascii.txt" | wc -c)
    info "Contrast chars: low=$c_lo  high=$c_hi"
    # High contrast → more extreme mapping → character count should differ.
    # Accept if they differ at all (either direction is meaningful).
    if [ "$c_lo" != "$c_hi" ]; then
        info "Contrast effect confirmed: low_chars=$c_lo  high_chars=$c_hi ✓"
        return 0
    else
        info "Contrast had no measurable effect: low_chars=$c_lo  high_chars=$c_hi"
        return 1
    fi
}

# . ݁₊ ⊹ . ݁˖ . ݁
# Cell aspect ratio
# . ݁₊ ⊹ . ݁˖ . ݁

t_cell_aspect_05()   { run_fb_test "cell_aspect_05"   xeyes -screen 640x480 -cell-aspect 0.5;  }
t_cell_aspect_10()   { run_fb_test "cell_aspect_10"   xeyes -screen 640x480 -cell-aspect 1.0;  }
t_cell_aspect_auto() { run_fb_test "cell_aspect_auto" xeyes -screen 640x480 -cell-aspect auto; }

# . ݁₊ ⊹ . ݁˖ . ݁
# X clients
# . ݁₊ ⊹ . ݁˖ . ݁

t_client_xeyes() { run_fb_test "client_xeyes"  xeyes  -screen 640x480; }
t_client_xclock() { run_fb_test "client_xclock" xclock -screen 640x480; }
t_client_xlogo()  { run_fb_test "client_xlogo"  xlogo  -screen 640x480; }

t_client_xterm() {
    local fb="$OUTPUT_DIR/client_xterm_fb.png"

    xcaca_cleanup
    xcaca_start -screen 640x480 || { xcaca_stop; return 1; }

    # xterm takes longer to draw; give it extra time
    DISPLAY=":$XCACA_D" xterm &
    local cpid=$!
    sleep 3
    take_fb_shot "$fb"
    kill "$cpid" 2>/dev/null || true

    xcaca_stop
    assert_image_content "$fb" "client_xterm/fb"
}

# . ݁₊ ⊹ . ݁˖ . ݁
# Terminal (ASCII art) visual screenshots via Xvfb
#
# These capture what libcaca actually renders in the terminal window — the
# ASCII art representation of the X client's pixels.
# . ݁₊ ⊹ . ݁˖ . ݁

_run_terminal_screenshot_test() {
    local label="$1"; shift
    # remaining "$@" = Xcaca args
    need Xvfb || return 77

    local term_png="$OUTPUT_DIR/${label}_terminal.png"
    local fb_png="$OUTPUT_DIR/${label}_fb.png"

    xcaca_cleanup
    xvfb_start
    xvfb_launch_xcaca "$@"

    if ! xvfb_wait_xcaca; then
        info "Xcaca (inside Xvfb xterm) failed to start"
        xvfb_stop; xcaca_cleanup
        return 1
    fi

    run_client xeyes
    sleep "$RENDER_WAIT"

    take_terminal_shot "$term_png"
    take_fb_shot       "$fb_png"

    kill "$_CLIENT_PID" 2>/dev/null || true
    xvfb_stop
    xcaca_cleanup

    assert_image_content "$term_png" "$label/terminal" || return 1
    assert_image_content "$fb_png"   "$label/fb"
}

t_terminal_screenshot_ncurses() {
    CACA_DRIVER=ncurses _run_terminal_screenshot_test "terminal_ncurses" -screen 640x480
}

t_terminal_screenshot_slang() {
    CACA_DRIVER=slang _run_terminal_screenshot_test "terminal_slang" -screen 640x480
}

# Terminal screenshots for a selection of interesting option combos.
t_terminal_blocks_charset() {
    need Xvfb || return 77
    _run_terminal_screenshot_test "terminal_blocks" -screen 640x480 -charset blocks
}

t_terminal_shades_ordered4() {
    need Xvfb || return 77
    _run_terminal_screenshot_test "terminal_shades_ordered4" \
        -screen 640x480 -charset shades -dither ordered4
}

t_terminal_small_screen() {
    need Xvfb || return 77
    _run_terminal_screenshot_test "terminal_small" -screen 320x240
}

t_terminal_high_contrast() {
    need Xvfb || return 77
    _run_terminal_screenshot_test "terminal_high_contrast" \
        -screen 640x480 -contrast 1.5 -brightness 1.2
}

# . ݁₊ ⊹ . ݁˖ . ݁
# ASCII text capture via tmux
#
# Captures the raw text content libcaca writes to the terminal, saved as
# .txt files for inspection or diffing.
# . ݁₊ ⊹ . ݁˖ . ݁

_run_ascii_text_test() {
    local label="$1"; shift
    need tmux || return 77

    local txt="$OUTPUT_DIR/${label}_ascii.txt"
    local fb="$OUTPUT_DIR/${label}_ascii_fb.png"

    xcaca_cleanup
    tmux_start_xcaca "$@"

    if ! tmux_wait_xcaca; then
        info "Xcaca (inside tmux) failed to start"
        tmux_stop; xcaca_cleanup
        return 1
    fi

    run_client xeyes
    sleep "$RENDER_WAIT"

    tmux_capture "$txt"
    take_fb_shot  "$fb"

    kill "$_CLIENT_PID" 2>/dev/null || true
    tmux_stop
    xcaca_cleanup

    assert_ascii_content "$txt" 50 || return 1
    assert_image_content "$fb"  "ascii_text/$label/fb"
}

t_ascii_text_ncurses() {
    CACA_DRIVER=ncurses _run_ascii_text_test "ascii_ncurses" -screen 640x480
}

t_ascii_text_slang() {
    CACA_DRIVER=slang _run_ascii_text_test "ascii_slang" -screen 640x480
}

# . ݁₊ ⊹ . ݁˖ . ݁
# Terminal resize test
#
# Starts Xcaca in an Xvfb xterm, resizes the xterm while a client is
# running, and verifies Xcaca still renders correctly after the resize.
# . ݁₊ ⊹ . ݁˖ . ݁
t_resize() {
    need Xvfb xdotool || return 77

    local before="$OUTPUT_DIR/resize_before_terminal.png"
    local after="$OUTPUT_DIR/resize_after_terminal.png"
    local fb_after="$OUTPUT_DIR/resize_after_fb.png"

    xcaca_cleanup
    xvfb_start
    xvfb_launch_xcaca -screen 640x480

    if ! xvfb_wait_xcaca; then
        info "Xcaca failed to start"
        xvfb_stop; xcaca_cleanup
        return 1
    fi

    run_client xeyes
    sleep 1

    take_terminal_shot "$before"

    # Resize the xterm window
    local wid
    wid=$(DISPLAY=":$XVFB_D" xdotool search --name "xcaca_test_terminal" 2>/dev/null | head -1)
    if [ -z "$wid" ]; then
        info "Could not find xterm window — skipping resize step"
    else
        DISPLAY=":$XVFB_D" xdotool windowsize "$wid" 1400 800
        info "Resized xterm to 1400x800"
        sleep 1
    fi

    take_terminal_shot "$after"
    take_fb_shot       "$fb_after"

    kill "$_CLIENT_PID" 2>/dev/null || true
    xvfb_stop
    xcaca_cleanup

    assert_image_content "$before"   "resize/before"   || return 1
    assert_image_content "$after"    "resize/after"    || return 1
    assert_image_content "$fb_after" "resize/fb_after"
}

# ===========================================================================
# Test runner
# ===========================================================================

TESTS=(
    # Infrastructure
    t_binary_exists
    t_usage_message
    t_start_stop
    t_multiple_restarts

    # Output drivers
    t_driver_ncurses
    t_driver_slang

    # Screen sizes
    t_screen_320x240
    t_screen_640x480
    t_screen_800x600

    # Dither algorithms
    t_dither_none
    t_dither_ordered2
    t_dither_ordered4
    t_dither_ordered8
    t_dither_random
    t_dither_fstein

    # Character sets
    t_charset_ascii
    t_charset_blocks
    t_charset_shades
    t_charset_utf8

    # Brightness / contrast / gamma
    t_brightness_low
    t_brightness_high
    t_contrast_low
    t_contrast_high
    t_gamma_low
    t_gamma_high

    # Cell aspect ratio
    t_cell_aspect_05
    t_cell_aspect_10
    t_cell_aspect_auto

    # X clients
    t_client_xeyes
    t_client_xclock
    t_client_xlogo
    t_client_xterm

    # Terminal (ASCII art) visual screenshots — require Xvfb
    t_terminal_screenshot_ncurses
    t_terminal_screenshot_slang
    t_terminal_blocks_charset
    t_terminal_shades_ordered4
    t_terminal_small_screen
    t_terminal_high_contrast

    # ASCII text capture — require tmux
    t_ascii_text_ncurses
    t_ascii_text_slang

    # Resize — requires Xvfb + xdotool
    t_resize
)

# . ݁₊ ⊹ . ݁˖ . ݁
# Parse args
# . ݁₊ ⊹ . ݁˖ . ݁
FILTER=""
while [ $# -gt 0 ]; do
    case "$1" in
        --filter) FILTER="$2"; shift 2 ;;
        -h|--help)
            echo "Usage: $0 [--filter PATTERN]"
            echo "  --filter PATTERN   run only tests whose name contains PATTERN"
            exit 0 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# . ݁₊ ⊹ . ݁˖ . ݁
# Preflight checks
# . ݁₊ ⊹ . ݁˖ . ݁
for cmd in convert identify xwd xdpyinfo xeyes xclock xlogo xterm; do
    if ! have "$cmd"; then
        echo "ERROR: required command not found: $cmd"
        exit 1
    fi
done

if [ ! -x "$XCACA_BIN" ]; then
    echo "ERROR: Xcaca binary not found or not executable: $XCACA_BIN"
    echo "       Build with:  cd xorg-server && ninja -C builddir"
    exit 1
fi

# Ensure clean state before starting
xcaca_cleanup

# . ݁₊ ⊹ . ݁˖ . ݁
# Print banner
# . ݁₊ ⊹ . ݁˖ . ݁
echo "================================================================"
echo " Xcaca Test Suite"
echo "================================================================"
echo " Binary:  $XCACA_BIN"
echo " Output:  $OUTPUT_DIR"
printf " Xvfb:   "; have Xvfb    && echo "yes  (terminal screenshots enabled)" \
                                 || echo "NO   (terminal screenshots will be skipped)"
printf " tmux:   "; have tmux    && echo "yes  (ASCII text capture enabled)" \
                                 || echo "NO   (ASCII text capture will be skipped)"
printf " xdotool:"; have xdotool && echo "yes  (resize test enabled)" \
                                 || echo "NO   (resize test will be skipped)"
echo "================================================================"

# . ݁₊ ⊹ . ݁˖ . ݁
# Run tests
# . ݁₊ ⊹ . ݁˖ . ݁
for test_fn in "${TESTS[@]}"; do
    # Apply --filter
    if [ -n "$FILTER" ] && [[ "$test_fn" != *"$FILTER"* ]]; then
        continue
    fi

    ret=0
    "$test_fn" || ret=$?

    case "$ret" in
        0)  pass  "$test_fn" ;;
        77) _skip "$test_fn" "missing optional dependency" ;;
        *)  _fail "$test_fn" "returned $ret (see output above)" ;;
    esac
done

# . ݁₊ ⊹ . ݁˖ . ݁
# Summary
# . ݁₊ ⊹ . ݁˖ . ݁
echo ""
echo "================================================================"
echo " Results: $PASS passed  $FAIL failed  $SKIP skipped"
if [ "${#FAILURES[@]}" -gt 0 ]; then
    echo ""
    echo " Failures:"
    for f in "${FAILURES[@]}"; do
        echo "   - $f"
    done
fi
echo "================================================================"
echo " Screenshots saved to: $OUTPUT_DIR/"
echo "================================================================"

[ "$FAIL" -eq 0 ]
